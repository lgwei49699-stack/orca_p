#include "libslic3r/ReconstructionProcess.hpp"
#include "runtime_identity.hpp"

#include <MRMesh/MRBitSet.h>
#include <MRMesh/MRMesh.h>
#include <MRMesh/MRMeshLoad.h>
#include <MRMesh/MRMeshLoadSettings.h>
#include <MRMesh/MRSystem.h>
#include <MRVoxels/MRRebuildMesh.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
namespace fs = std::filesystem;
using Json   = nlohmann::json;
using Clock  = std::chrono::steady_clock;

constexpr char     SDK_VERSION[]    = "3.1.3.429";
constexpr char     MESH_MAGIC[8]    = {'O', 'R', 'C', 'A', 'R', 'C', '0', '1'};
constexpr uint64_t MAX_FILE_BYTES   = 256ull * 1024 * 1024;
constexpr size_t   MAX_REPORT_BYTES = 256 * 1024;
constexpr uint64_t MAX_INPUT_FACES  = 3000000;
constexpr uint64_t MAX_OUTPUT_FACES = 1000000;
static_assert(std::endian::native == std::endian::little, "The local ORCARC01 protocol requires little-endian storage");
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

struct Failure : std::runtime_error
{
    std::string status;
    Failure(std::string code, const std::string& message) : std::runtime_error(message), status(std::move(code)) {}
};

void require(bool condition, const char* status, const char* message)
{
    if (!condition)
        throw Failure(status, message);
}

struct Options
{
    fs::path task;
    uint64_t parent_pid       = 0;
    unsigned timeout_ms       = 0;
    size_t   memory_limit_mb  = 0;
    float    voxel_mm         = 0;
    uint64_t max_input_faces  = 0;
    uint64_t max_output_faces = 0;
};

uint64_t unsigned_value(const std::string& text)
{
    uint64_t   result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    require(!text.empty() && parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() && result > 0, "invalid_options",
            "Expected a strictly positive decimal integer.");
    return result;
}

Options parse_options(const std::vector<std::string>& arguments)
{
    Options                         options;
    std::unordered_set<std::string> seen;
    for (size_t i = 1; i < arguments.size(); i += 2) {
        const auto& key = arguments[i];
        require(i + 1 < arguments.size() && seen.insert(key).second, "invalid_options", "Missing or duplicate worker option.");
        const auto& value = arguments[i + 1];
        if (key == "--task-directory") {
            options.task = fs::path(std::u8string(value.begin(), value.end()));
        } else if (key == "--voxel-mm") {
            size_t end   = 0;
            double voxel = 0;
            try {
                voxel = std::stod(value, &end);
            } catch (const std::exception&) {
                throw Failure("invalid_options", "Voxel size must be a finite positive number.");
            }
            options.voxel_mm = static_cast<float>(voxel);
            require(end == value.size() && std::isfinite(voxel) && voxel > 0 && std::isfinite(options.voxel_mm) && options.voxel_mm > 0,
                    "invalid_options", "Voxel size must be finite, positive and representable as a float.");
        } else {
            const uint64_t number = unsigned_value(value);
            if (key == "--parent-pid")
                options.parent_pid = number;
            else if (key == "--timeout-ms") {
                require(number <= 300000, "invalid_options", "Worker timeout exceeds 300000 ms.");
                options.timeout_ms = static_cast<unsigned>(number);
            } else if (key == "--memory-limit-mb") {
                require(number >= 128 && number <= 4096, "invalid_options", "Worker memory budget must be 128 to 4096 MiB.");
                options.memory_limit_mb = static_cast<size_t>(number);
            } else if (key == "--max-input-faces") {
                require(number <= MAX_INPUT_FACES, "invalid_options", "Input face budget exceeds the supported limit.");
                options.max_input_faces = number;
            } else if (key == "--max-output-faces") {
                require(number <= MAX_OUTPUT_FACES, "invalid_options", "Output face budget exceeds the supported limit.");
                options.max_output_faces = number;
            } else {
                throw Failure("invalid_options", "Unsupported worker option: " + key);
            }
        }
    }
    require(seen.size() == 7 && options.task.is_absolute() && options.parent_pid && options.timeout_ms && options.memory_limit_mb &&
                options.voxel_mm > 0 && options.max_input_faces && options.max_output_faces,
            "invalid_options", "All seven worker options are required, including an absolute task directory.");
    require(fs::is_directory(fs::symlink_status(options.task)), "invalid_input", "Task directory is missing or is a symbolic link.");
    return options;
}

double elapsed_ms(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

bool path_absent(const fs::path& path)
{
    std::error_code error;
    const auto      status = fs::symlink_status(path, error);
    return status.type() == fs::file_type::not_found && (!error || error == std::errc::no_such_file_or_directory);
}

void require_new_output(const fs::path& path)
{
    require(path_absent(path), "output_error", "Worker output or temporary file already exists; refusing to overwrite it.");
}

std::string utf8_path(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path loaded_library_path(const void* symbol)
{
#ifdef _WIN32
    HMODULE module = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(symbol), &module) != 0,
            "sdk_version_mismatch", "Cannot locate the loaded SDK library.");
    std::wstring buffer(32768, L'\0');
    const DWORD  length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    require(length > 0 && length < buffer.size(), "sdk_version_mismatch", "Cannot read the loaded SDK library path.");
    buffer.resize(length);
    return fs::canonical(buffer);
#else
    Dl_info info{};
    require(dladdr(symbol, &info) != 0 && info.dli_fname, "sdk_version_mismatch", "Cannot locate the loaded SDK library.");
    return fs::canonical(info.dli_fname);
#endif
}

std::string sha256_file(const fs::path& path)
{
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(context && EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) == 1, "sdk_version_mismatch",
            "Cannot initialize the SDK identity digest.");
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "sdk_version_mismatch", "Cannot read the loaded SDK library.");
    std::array<char, 65536> buffer{};
    while (input) {
        input.read(buffer.data(), buffer.size());
        require(EVP_DigestUpdate(context.get(), buffer.data(), static_cast<size_t>(input.gcount())) == 1, "sdk_version_mismatch",
                "Cannot compute the SDK identity digest.");
    }
    require(input.eof(), "sdk_version_mismatch", "SDK library read failed during identity verification.");
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned                                   length = 0;
    require(EVP_DigestFinal_ex(context.get(), digest.data(), &length) == 1 && length == 32, "sdk_version_mismatch",
            "Cannot finish the SDK identity digest.");
    static constexpr char HEX[] = "0123456789abcdef";
    std::string           result;
    result.reserve(64);
    for (unsigned i = 0; i < length; ++i) {
        result += HEX[digest[i] >> 4];
        result += HEX[digest[i] & 15];
    }
    return result;
}

Json verify_runtime_libraries()
{
    Json       libraries = Json::array();
    const auto verify    = [&](const char* name, const void* symbol, const char* configured_path, const char* expected_sha256) {
        const fs::path loaded = loaded_library_path(symbol);
        require(loaded == fs::canonical(fs::path(std::u8string(configured_path, configured_path + std::strlen(configured_path)))),
                   "sdk_version_mismatch", "A different SDK library was loaded than the one selected at build time.");
        const auto digest = sha256_file(loaded);
        require(digest == expected_sha256, "sdk_version_mismatch", "The loaded SDK library changed since worker configuration.");
        libraries.push_back({{"name", name}, {"path", utf8_path(loaded)}, {"sha256", digest}});
    };
    verify("MRMesh", reinterpret_cast<const void*>(&MR::GetMRVersionString), MESH_RUNTIME, MESH_SHA256);
    verify("MRVoxels", reinterpret_cast<const void*>(&MR::rebuildMesh), VOXELS_RUNTIME, VOXELS_SHA256);
    return libraries;
}

// The parent writes binary STL. Check its bounded record count and coordinates
// before the SDK is allowed to allocate topology from the file.
uint64_t validate_input(const fs::path& path, uint64_t face_limit)
{
    require(fs::is_regular_file(fs::symlink_status(path)), "invalid_input", "Input must be a regular, non-symlink binary STL file.");
    const uint64_t bytes = fs::file_size(path);
    require(bytes >= 84 && bytes <= MAX_FILE_BYTES, "resource_limit", "Input STL is empty, truncated, or exceeds 256 MiB.");
    std::ifstream        input(path, std::ios::binary);
    std::array<char, 80> header{};
    uint32_t             faces = 0;
    input.read(header.data(), header.size());
    input.read(reinterpret_cast<char*>(&faces), sizeof(faces));
    require(bool(input) && faces > 0 && bytes == 84 + uint64_t(faces) * 50, "invalid_input",
            "Input is not an exact binary STL; ASCII STL is not part of the internal worker protocol.");
    require(faces <= face_limit, "resource_limit", "Input STL exceeds the requested face budget.");
    std::array<char, 50> record{};
    for (uint32_t face = 0; face < faces; ++face) {
        input.read(record.data(), record.size());
        require(bool(input), "invalid_input", "Input STL changed or was truncated during validation.");
        for (size_t corner = 0; corner < 9; ++corner) {
            float coordinate = 0;
            std::memcpy(&coordinate, record.data() + 12 + corner * sizeof(float), sizeof(float));
            require(std::isfinite(coordinate), "invalid_input", "Input STL contains a non-finite vertex coordinate.");
        }
    }
    return faces;
}

Json parameters(const Options& options)
{
    return {{"voxel_mm", options.voxel_mm},
            {"sign_mode", "Auto"},
            {"offset_mode", "Standard"},
            {"pre_subdivide", true},
            {"close_holes_in_hole_winding_number", true},
            {"winding_number_threshold", 0.5},
            {"winding_number_beta", 1.5},
            {"decimate", true},
            {"reduce_angle_iterations", 3},
            {"tiny_edge_length_mm", 0.005f},
            {"fragment_removal", "none"},
            {"max_input_faces", options.max_input_faces},
            {"max_output_faces", options.max_output_faces}};
}

void publish_report(const fs::path& task, const Json& report)
{
    const auto text = report.dump(2) + '\n';
    require(text.size() <= MAX_REPORT_BYTES, "output_error", "Worker JSON report exceeds 256 KiB.");
    const auto target    = task / "meshlib-report.json";
    const auto temporary = task / "meshlib-report.json.tmp";
    require_new_output(target);
    require_new_output(temporary);
    std::ofstream output(temporary, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    require(bool(output), "output_error", "Could not write the worker JSON report.");
    fs::rename(temporary, target);
}

// Use only vertices referenced by valid faces and explicitly remap indices.
// No shell, face or geometric detail is deleted by this serialization step.
Json publish_mesh(const fs::path& task, const MR::Mesh& mesh, uint64_t face_limit)
{
    const auto face_count = mesh.topology.numValidFaces();
    require(face_count > 0, "candidate_rejected", "MeshLib returned an empty mesh.");
    require(uint64_t(face_count) <= face_limit, "resource_limit", "MeshLib candidate exceeds the output face budget.");
    require(mesh.points.size() <= 3 * face_limit && mesh.points.size() <= uint64_t(std::numeric_limits<int32_t>::max()), "resource_limit",
            "MeshLib candidate vertex storage exceeds the output budget.");
    std::vector<int32_t>                mapping(mesh.points.size(), -1);
    std::vector<std::array<float, 3>>   vertices;
    std::vector<std::array<int32_t, 3>> faces;
    vertices.reserve(std::min<size_t>(mesh.points.size(), size_t(face_count) * 3));
    faces.reserve(size_t(face_count));
    for (const auto face : mesh.topology.getValidFaces()) {
        const auto             ids = mesh.topology.getTriVerts(face);
        std::array<int32_t, 3> triangle{};
        for (size_t corner = 0; corner < 3; ++corner) {
            const int index = int(ids[corner]);
            require(index >= 0 && size_t(index) < mesh.points.size(), "candidate_rejected", "MeshLib returned an invalid vertex index.");
            if (mapping[size_t(index)] < 0) {
                const auto& point = mesh.points[ids[corner]];
                require(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z), "candidate_rejected",
                        "MeshLib returned a non-finite vertex coordinate.");
                mapping[size_t(index)] = static_cast<int32_t>(vertices.size());
                vertices.push_back({point.x, point.y, point.z});
            }
            triangle[corner] = mapping[size_t(index)];
        }
        faces.push_back(triangle);
    }
    require(faces.size() == size_t(face_count) && !vertices.empty(), "candidate_rejected", "MeshLib topology counts are inconsistent.");
    const auto target    = task / "meshlib-candidate.mesh";
    const auto temporary = task / "meshlib-candidate.mesh.tmp";
    require_new_output(target);
    require_new_output(temporary);
    std::ofstream  output(temporary, std::ios::binary);
    const uint64_t nv = vertices.size(), nf = faces.size();
    output.write(MESH_MAGIC, sizeof(MESH_MAGIC));
    output.write(reinterpret_cast<const char*>(&nv), sizeof(nv));
    output.write(reinterpret_cast<const char*>(&nf), sizeof(nf));
    for (const auto& vertex : vertices)
        output.write(reinterpret_cast<const char*>(vertex.data()), 3 * sizeof(float));
    for (const auto& face : faces)
        output.write(reinterpret_cast<const char*>(face.data()), 3 * sizeof(int32_t));
    output.close();
    require(bool(output), "output_error", "Could not write the worker candidate mesh.");
    require(fs::file_size(temporary) == 24 + 12 * (nv + nf), "output_error", "Worker candidate mesh has an unexpected size.");
    fs::rename(temporary, target);
    return {{"faces", nf}, {"vertices", nv}, {"bytes", 24 + 12 * (nv + nf)}};
}

int run(const std::vector<std::string>& arguments)
{
    const auto                                         started = Clock::now();
    Options                                            options;
    bool                                               task_validated  = false;
    bool                                               candidate_owned = false;
    Json                                               report          = {{"schema", "meshlib_worker_v1"},
                                                                          {"engine", "meshlib"},
                                                                          {"sdk_version", SDK_VERSION},
                                                                          {"status", "failed"},
                                                                          {"message", "MeshLib worker did not complete."},
                                                                          {"parameters", Json::object()},
                                                                          {"input", Json::object()},
                                                                          {"output", Json::object()},
                                                                          {"skipped_face_count", 0},
                                                                          {"duplicated_vertex_count", 0},
                                                                          {"elapsed_ms", Json::object()},
                                                                          {"candidate_validated", false},
                                                                          {"print_ready", false},
                                                                          {"fragment_removal", "none"},
                                                                          {"runtime_identity_verified", false}};
    std::unique_ptr<Slic3r::ReconstructionWorkerGuard> guard;
    try {
        options                   = parse_options(arguments);
        task_validated            = true;
        report["parameters"]      = parameters(options);
        report["resource_limits"] = {{"timeout_ms", options.timeout_ms}, {"memory_limit_mb", options.memory_limit_mb}};
        std::string limit_error;
        if (!Slic3r::apply_reconstruction_worker_limits(options.memory_limit_mb, limit_error))
            throw Failure("resource_limit", limit_error);
        guard = std::make_unique<Slic3r::ReconstructionWorkerGuard>(options.parent_pid, options.timeout_ms, options.memory_limit_mb);
        // No SDK operation occurs before worker resource/parent supervision.
        const auto identity_started              = Clock::now();
        report["runtime_libraries"]              = verify_runtime_libraries();
        report["runtime_identity_verified"]      = true;
        report["elapsed_ms"]["runtime_identity"] = elapsed_ms(identity_started);
        // This API returns a resource-file value, or just the library build
        // date. It is diagnostic text, NOT proof of the loaded SDK version.
        const std::string runtime_version = MR::GetMRVersionString();
        report["sdk_runtime_version"]     = runtime_version;
        for (const auto* name : {"meshlib-candidate.mesh", "meshlib-candidate.mesh.tmp", "meshlib-report.json", "meshlib-report.json.tmp"})
            require_new_output(options.task / name);

        const auto     input_path                = options.task / "meshlib-input.stl";
        const auto     input_started             = Clock::now();
        const uint64_t input_faces               = validate_input(input_path, options.max_input_faces);
        report["input"]                          = {{"faces", input_faces}, {"bytes", fs::file_size(input_path)}, {"units", "mm"}};
        report["elapsed_ms"]["input_validation"] = elapsed_ms(input_started);

        const auto        deadline = started + std::chrono::milliseconds(options.timeout_ms);
        std::atomic<bool> callback_timed_out{false};
        const auto        progress = [&](float) {
            if (Clock::now() < deadline)
                return true;
            callback_timed_out.store(true, std::memory_order_relaxed);
            return false;
        };
        int                  skipped_faces = 0, duplicated_vertices = 0;
        MR::MeshLoadSettings load_settings;
        load_settings.skippedFaceCount      = &skipped_faces;
        load_settings.duplicatedVertexCount = &duplicated_vertices;
        load_settings.telemetrySignal       = false;
        load_settings.callback              = progress;
        const auto load_started             = Clock::now();
        auto       source                   = MR::loadBinaryStl(input_path, load_settings);
        report["elapsed_ms"]["sdk_load"]    = elapsed_ms(load_started);
        report["skipped_face_count"]        = skipped_faces;
        report["duplicated_vertex_count"]   = duplicated_vertices;
        if (!source)
            throw Failure(callback_timed_out.load(std::memory_order_relaxed) ? "timed_out" : "invalid_input", source.error());
        report["input"]["loaded_faces"]    = source->topology.numValidFaces();
        report["input"]["loaded_vertices"] = source->topology.numValidVerts();
        require(skipped_faces == 0 && uint64_t(source->topology.numValidFaces()) == input_faces, "input_faces_skipped",
                "MeshLib omitted input faces while constructing topology; refusing to rebuild a truncated model.");

        MR::RebuildMeshSettings settings;
        settings.voxelSize                     = options.voxel_mm;
        settings.signMode                      = MR::SignDetectionModeShort::Auto;
        settings.offsetMode                    = MR::OffsetMode::Standard;
        settings.preSubdivide                  = true;
        settings.closeHolesInHoleWindingNumber = true;
        settings.windingNumberThreshold        = 0.5f;
        settings.windingNumberBeta             = 1.5f;
        settings.decimate                      = true;
        settings.reduceAngleNumIters           = 3;
        settings.tinyEdgeLength                = 0.005f;
        settings.progress                      = progress;
        std::atomic<int> selected_sign_mode{-1};
        settings.onSignDetectionModeSelected = [&](MR::SignDetectionMode mode) {
            selected_sign_mode.store(static_cast<int>(mode), std::memory_order_relaxed);
        };
        const auto rebuild_started        = Clock::now();
        auto       candidate              = MR::rebuildMesh(MR::MeshPart(*source), settings);
        report["elapsed_ms"]["rebuild"]   = elapsed_ms(rebuild_started);
        const int selected                = selected_sign_mode.load(std::memory_order_relaxed);
        report["selected_sign_mode"]      = selected;
        report["selected_sign_mode_name"] = selected >= 0 && selected <= static_cast<int>(MR::SignDetectionMode::HoleWindingRule) ?
                                                MR::asString(static_cast<MR::SignDetectionMode>(selected)) :
                                                "unreported";
        if (!candidate)
            throw Failure(callback_timed_out.load(std::memory_order_relaxed) ? "timed_out" : "engine_error", candidate.error());
        require(!callback_timed_out.load(std::memory_order_relaxed) && Clock::now() < deadline, "timed_out",
                "MeshLib exceeded the worker deadline.");
        require(selected >= 0 && selected <= static_cast<int>(MR::SignDetectionMode::HoleWindingRule), "engine_error",
                "MeshLib did not report the selected sign detection mode.");
        const auto export_started                = Clock::now();
        report["output"]                         = publish_mesh(options.task, *candidate, options.max_output_faces);
        candidate_owned                          = true;
        report["elapsed_ms"]["candidate_export"] = elapsed_ms(export_started);
        report["elapsed_ms"]["total"]            = elapsed_ms(started);
        report["status"]                         = "candidate_generated";
        report["message"]                        = "MeshLib generated a candidate; independent Orca geometry validation is still required.";
        publish_report(options.task, report);
        return 0;
    } catch (const Failure& error) {
        report["status"]  = error.status;
        report["message"] = std::string(error.what()).substr(0, 4096);
    } catch (const std::bad_alloc&) {
        report["status"]  = "resource_limit";
        report["message"] = "MeshLib worker could not allocate memory.";
    } catch (const std::exception& error) {
        report["status"]  = "failed";
        report["message"] = std::string(error.what()).substr(0, 4096);
    } catch (...) {
        report["status"]  = "engine_error";
        report["message"] = "MeshLib worker failed with an unknown exception.";
    }
    report["elapsed_ms"]["total"] = elapsed_ms(started);
    if (candidate_owned) {
        std::error_code ignored;
        fs::remove(options.task / "meshlib-candidate.mesh", ignored);
    }
    if (task_validated) {
        try {
            publish_report(options.task, report);
            // Completed failure is a valid protocol response. Hard supervision
            // exits retain the runner's 124/125/126 status without a fake report.
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Cannot publish MeshLib failure report: " << error.what() << '\n';
        }
    }
    std::cerr << report.dump() << '\n';
    return 2;
}

#ifdef _WIN32
std::string utf8(const wchar_t* text)
{
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        throw std::runtime_error("Worker argument is not valid UTF-16.");
    std::string result(size_t(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), size, nullptr, nullptr))
        throw std::runtime_error("Could not convert worker argument to UTF-8.");
    result.pop_back();
    return result;
}
#endif
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    try {
        std::vector<std::string> arguments;
        arguments.reserve(size_t(argc));
        for (int i = 0; i < argc; ++i) {
#ifdef _WIN32
            arguments.push_back(utf8(argv[i]));
#else
            arguments.emplace_back(argv[i]);
#endif
        }
        return run(arguments);
    } catch (const std::exception& error) {
        std::cerr << "MeshLib worker startup failed: " << error.what() << '\n';
        return 2;
    }
}
