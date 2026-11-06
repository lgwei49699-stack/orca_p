#include "MeshLibAdapter.hpp"
#include "runtime_identity.hpp"

#include <MRMesh/MRMesh.h>
#include <MRMesh/MRMeshBuilder.h>
#include <MRMesh/MRMeshLoad.h>
#include <MRMesh/MRFixSelfIntersections.h>
#include <MRMesh/MRSystem.h>
#include <MRMesh/MRTriMesh.h>
#include <MRVoxels/MRRebuildMesh.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace OrcaMeshLib {
namespace {
namespace fs                           = std::filesystem;
using Json                             = nlohmann::json;
using Clock                            = std::chrono::steady_clock;
constexpr const char* SDK_VERSION      = "3.1.3.429";
constexpr size_t      MAX_INPUT_FACES  = 3000000;
constexpr size_t      MAX_OUTPUT_FACES = 1000000;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

struct Failure : std::runtime_error
{
    std::string status;
    Failure(const char* code, const std::string& message) : std::runtime_error(message), status(code) {}
};

void require(bool condition, const char* status, const char* message)
{
    if (!condition)
        throw Failure(status, message);
}

double elapsed_ms(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

// Never allow a caller's exception to unwind through the SDK's thread pool.
class Progress
{
public:
    explicit Progress(const std::function<bool(float)>& callback) : callback_(callback) {}

    bool operator()(float value) noexcept
    {
        if (stopped_.load(std::memory_order_relaxed))
            return false;
        try {
            if (!callback_ || callback_(value))
                return true;
        } catch (...) {
            callback_failed_.store(true, std::memory_order_relaxed);
        }
        stopped_.store(true, std::memory_order_relaxed);
        return false;
    }

    void checkpoint()
    {
        if (!(*this)(0.f))
            throw Failure(callback_failed_.load(std::memory_order_relaxed) ? "callback_error" : "cancelled",
                          "MeshLib operation stopped by its cooperative control callback.");
    }

    bool stopped() const noexcept { return stopped_.load(std::memory_order_relaxed); }
    bool callback_failed() const noexcept { return callback_failed_.load(std::memory_order_relaxed); }

private:
    const std::function<bool(float)>& callback_;
    std::atomic<bool>                 stopped_{false};
    std::atomic<bool>                 callback_failed_{false};
};

std::string sha256_file(const fs::path& path, Progress& progress)
{
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(context && EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) == 1, "sdk_version_mismatch",
            "Cannot initialize the SDK identity digest.");
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "sdk_version_mismatch", "Cannot read the loaded SDK library.");
    std::array<char, 65536> buffer{};
    while (input) {
        progress.checkpoint();
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

fs::path loaded_library_path(const void* symbol)
{
#ifdef _WIN32
    HMODULE module = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(symbol), &module) != 0,
            "sdk_version_mismatch", "Cannot locate the loaded SDK library.");
    // Use the Unicode API and grow the buffer for extended Windows paths. The
    // handle is borrowed; the loaded SDK must not be released by this check.
    for (size_t size = 512; size <= 65536; size *= 2) {
        std::wstring buffer(size, L'\0');
        const DWORD  length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        require(length != 0, "sdk_version_mismatch", "Cannot read the loaded SDK library path.");
        if (length < buffer.size()) {
            buffer.resize(length);
            return fs::canonical(fs::path(buffer));
        }
    }
    throw Failure("sdk_version_mismatch", "The loaded SDK library path is too long.");
#else
    Dl_info info{};
    require(dladdr(symbol, &info) != 0 && info.dli_fname, "sdk_version_mismatch", "Cannot locate the loaded SDK library.");
    return fs::canonical(info.dli_fname);
#endif
}

std::string utf8_path(const fs::path& path)
{
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

Json verify_runtime_libraries(Progress& progress)
{
    Json       libraries = Json::array();
    const auto verify    = [&](const char* name, const void* symbol, const char* expected_sha256) {
        const fs::path loaded = loaded_library_path(symbol);
        const auto     digest = sha256_file(loaded, progress);
        require(digest == expected_sha256, "sdk_version_mismatch", "The loaded SDK library changed since configuration.");
        // Content identity survives relocation; a build-machine absolute path
        // must not become a deployment requirement. This is not a loader sandbox.
        libraries.push_back({{"name", name}, {"path", utf8_path(loaded)}, {"sha256", digest}});
    };
    verify("MRMesh", reinterpret_cast<const void*>(&MR::GetMRVersionString), MESH_SHA256);
    verify("MRVoxels", reinterpret_cast<const void*>(&MR::rebuildMesh), VOXELS_SHA256);
    return libraries;
}

// Use exact vertex sharing even when the input STL stored separate corners.
MR::Mesh input_mesh(const Mesh& input, const Options& options, Progress& progress, bool& split_fans)
{
    require(options.max_input_faces > 0 && options.max_input_faces <= MAX_INPUT_FACES && options.max_output_faces > 0 &&
                options.max_output_faces <= MAX_OUTPUT_FACES,
            "invalid_options", "MeshLib face budgets exceed the supported limits.");
    require(!input.faces.empty() && !input.vertices.empty(), "invalid_input", "Input mesh is empty.");
    require(input.faces.size() <= options.max_input_faces && input.vertices.size() <= 3 * options.max_input_faces, "resource_limit",
            "MeshLib input exceeds the face or vertex budget.");
    // Load the selected volume through the same SDK STL importer as a file.
    // The coordinates already include the object's physical transform.
    static_assert(std::endian::native == std::endian::little);
    const uint32_t count = static_cast<uint32_t>(input.faces.size());
    std::string    bytes(84 + size_t(count) * 50, '\0');
    std::memcpy(bytes.data() + 80, &count, sizeof(count));
    for (size_t i = 0; i < count; ++i) {
        if ((i & 1023u) == 0)
            progress.checkpoint();
        for (size_t c = 0; c < 3; ++c) {
            const auto id = input.faces[i][c];
            require(id >= 0 && size_t(id) < input.vertices.size(), "invalid_input", "Input mesh contains an invalid vertex index.");
            const auto& p = input.vertices[id];
            require(std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]), "invalid_input",
                    "Input mesh contains a non-finite coordinate.");
            std::memcpy(bytes.data() + 84 + 50 * i + 12 * (1 + c), p.data(), 3 * sizeof(float));
        }
    }
    {
        std::istringstream   stream(std::move(bytes), std::ios::binary);
        MR::MeshLoadSettings load;
        load.telemetrySignal = false;
        load.callback        = [&](float) { return progress(0.f); };
        auto loaded          = MR::MeshLoad::fromBinaryStl(stream, load);
        progress.checkpoint();
        if (!loaded)
            throw Failure("invalid_input", loaded.error());
        if (size_t(loaded->topology.numValidFaces()) == count)
            return std::move(*loaded);
    }

    // The SDK can omit faces in conflicting non-manifold fans. Preserve those
    // inputs as independent triangles before invoking the single rebuild.
    split_fans = true;
    MR::Mesh source;
    source.points.resize(3 * size_t(count));
    MR::Triangulation triangles;
    triangles.resize(count);
    for (size_t i = 0; i < count; ++i) {
        if ((i & 1023u) == 0)
            progress.checkpoint();
        for (int c = 0; c < 3; ++c) {
            const MR::VertId id(int(3 * i + c));
            const auto&      p               = input.vertices[input.faces[i][c]];
            source.points[id]                = {p[0], p[1], p[2]};
            triangles[MR::FaceId(int(i))][c] = id;
        }
    }
    source.topology.edgeReserve(6 * size_t(count));
    MR::MeshBuilder::addTriangles(source.topology, triangles);
    return source;
}

Mesh copy_candidate(const MR::Mesh& mesh, size_t face_limit, Progress& progress)
{
    const auto count = mesh.topology.numValidFaces();
    require(count > 0, "candidate_rejected", "MeshLib returned an empty mesh.");
    require(size_t(count) <= face_limit && size_t(mesh.topology.numValidVerts()) <= 3 * face_limit, "resource_limit",
            "MeshLib candidate exceeds the output face or vertex budget.");
    std::vector<int32_t> mapping(mesh.points.size(), -1);
    Mesh                 result;
    result.vertices.reserve(std::min<size_t>(mesh.topology.numValidVerts(), size_t(count) * 3));
    result.faces.reserve(size_t(count));
    for (const auto face : mesh.topology.getValidFaces()) {
        if ((result.faces.size() & 1023u) == 0)
            progress.checkpoint();
        const auto             ids = mesh.topology.getTriVerts(face);
        std::array<int32_t, 3> triangle{};
        for (size_t corner = 0; corner < 3; ++corner) {
            const int index = int(ids[corner]);
            require(index >= 0 && size_t(index) < mesh.points.size(), "candidate_rejected", "MeshLib returned an invalid vertex index.");
            if (mapping[size_t(index)] < 0) {
                const auto& point = mesh.points[ids[corner]];
                require(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z), "candidate_rejected",
                        "MeshLib returned a non-finite coordinate.");
                mapping[size_t(index)] = static_cast<int32_t>(result.vertices.size());
                result.vertices.push_back({point.x, point.y, point.z});
            }
            triangle[corner] = mapping[size_t(index)];
        }
        result.faces.push_back(triangle);
    }
    require(result.faces.size() == size_t(count) && !result.vertices.empty(), "candidate_rejected",
            "MeshLib output topology counts are inconsistent.");
    return result;
}
} // namespace

bool available() noexcept { return true; }

Result rebuild(Mesh input, const Options& options, const std::function<bool(const Mesh&)>& needs_local_repair)
{
    const auto started = Clock::now();
    Result     result;
    Progress   progress(options.progress);
    Json       report = {{"schema", "meshlib_adapter_v1"},
                         {"engine", "meshlib"},
                         {"execution", "in_process"},
                         {"sdk_version", SDK_VERSION},
                         {"status", "failed"},
                         {"parameters", Json::object()},
                         {"input", Json::object()},
                         {"output", Json::object()},
                         {"elapsed_ms", Json::object()},
                         {"skipped_face_count", 0},
                         {"candidate_validated", false},
                         {"print_ready", false},
                         {"fragment_removal", "none"},
                         {"runtime_identity_verified", false}};
    try {
        progress.checkpoint();
        require(std::isfinite(options.voxel_mm) && options.voxel_mm >= static_cast<double>(std::numeric_limits<float>::denorm_min()) &&
                    options.voxel_mm <= static_cast<double>(std::numeric_limits<float>::max()),
                "invalid_options", "Voxel size must be finite, positive and representable as a float.");
        const float voxel                        = static_cast<float>(options.voxel_mm);
        const auto identity_started              = Clock::now();
        report["runtime_libraries"]              = verify_runtime_libraries(progress);
        report["runtime_identity_verified"]      = true;
        report["elapsed_ms"]["runtime_identity"] = elapsed_ms(identity_started);
        // Diagnostic only: this API may return a build date, not an SDK version.
        report["sdk_runtime_version"]    = MR::GetMRVersionString();
        report["input"]                  = {{"faces", input.faces.size()}, {"vertices", input.vertices.size()}, {"units", "mm"}};
        bool       split_fans            = false;
        const auto load_started          = Clock::now();
        auto       source                = input_mesh(input, options, progress, split_fans);
        report["elapsed_ms"]["sdk_load"] = elapsed_ms(load_started);
        report["skipped_face_count"]     = input.faces.size() - source.topology.numValidFaces();
        progress.checkpoint();
        report["input"]["loaded_faces"]    = source.topology.numValidFaces();
        report["input"]["loaded_vertices"] = source.topology.numValidVerts();
        require(size_t(source.topology.numValidFaces()) == input.faces.size(), "input_faces_skipped",
                "MeshLib omitted input faces while constructing topology; refusing to rebuild a truncated model.");
        input = {};

        MR::RebuildMeshSettings settings;
        settings.voxelSize                     = voxel;
        settings.closeHolesInHoleWindingNumber = options.close_holes;
        settings.reduceAngleNumIters           = 1;
        // SDK import may reconnect an open source before Auto inspects it.
        // An explicit hole-filling request must use the mode that closes holes.
        if (options.close_holes)
            settings.signMode = MR::SignDetectionModeShort::HoleWindingNumber;
        // Keep the SDK's subdivision and winding accuracy defaults. Subdivision
        // accelerates large source facets without changing the requested voxel.
        settings.progress                    = [&](float value) { return progress(value); };
        settings.onSignDetectionModeSelected = [&](MR::SignDetectionMode mode) {
            report["selected_sign_mode"]      = int(mode);
            report["selected_sign_mode_name"] = MR::asString(mode);
        };
        report["parameters"]            = {{"voxel_mm", voxel},
                                           {"input_mode", split_fans ? "independent_triangles" : "shared_vertices"},
                                           {"offset_mode", "Standard"},
                                           {"sign_mode", options.close_holes ? "HoleWindingNumber" : "Auto"},
                                           {"pre_subdivide", settings.preSubdivide},
                                           {"close_holes_in_hole_winding_number", options.close_holes},
                                           {"decimate", settings.decimate},
                                           {"winding_number_threshold", settings.windingNumberThreshold},
                                           {"winding_number_beta", settings.windingNumberBeta},
                                           {"reduce_angle_iterations", settings.reduceAngleNumIters}};
        const auto rebuild_started      = Clock::now();
        auto       candidate            = MR::rebuildMesh(source, settings);
        report["elapsed_ms"]["rebuild"] = elapsed_ms(rebuild_started);
        progress.checkpoint();
        if (!candidate)
            throw Failure("engine_error", candidate.error());
        source = MR::Mesh{};
        const auto copy_started                = Clock::now();
        result.mesh                            = copy_candidate(*candidate, options.max_output_faces, progress);
        report["elapsed_ms"]["candidate_copy"] = elapsed_ms(copy_started);
        // Keep the SDK topology intact: reimporting a failed candidate can
        // change the local repair. A valid native candidate needs no repair.
        if (needs_local_repair && needs_local_repair(result.mesh)) {
            progress.checkpoint();
            result.mesh = {};
            const auto cleanup_started = Clock::now();
            MR::SelfIntersections::Settings cleanup;
            cleanup.method           = MR::SelfIntersections::Settings::Method::CutAndFill;
            cleanup.subdivideEdgeLen = voxel;
            cleanup.callback         = [&](float value) { return progress(value); };
            const auto cleaned       = MR::SelfIntersections::fix(*candidate, cleanup);
            report["elapsed_ms"]["local_cleanup"] = elapsed_ms(cleanup_started);
            progress.checkpoint();
            if (!cleaned)
                throw Failure("engine_error", cleaned.error());
            const auto recopy_started = Clock::now();
            result.mesh = copy_candidate(*candidate, options.max_output_faces, progress);
            report["elapsed_ms"]["candidate_copy"] = report["elapsed_ms"]["candidate_copy"].get<double>() + elapsed_ms(recopy_started);
        }
        report["output"]                       = {{"faces", result.mesh.faces.size()}, {"vertices", result.mesh.vertices.size()}};
        progress.checkpoint();
        result.status  = "candidate_generated";
        result.message = "MeshLib generated a candidate; independent Orca geometry validation is still required.";
        // Publish success only after all potentially allocating result fields
        // have been assigned; an allocation failure must not retain success.
        result.success = true;
    } catch (const Failure& error) {
        result.status  = error.status;
        result.message = std::string(error.what()).substr(0, 4096);
    } catch (const std::bad_alloc&) {
        result.status  = "resource_limit";
        result.message = "MeshLib could not allocate memory.";
    } catch (const std::exception& error) {
        result.status  = "engine_error";
        result.message = std::string(error.what()).substr(0, 4096);
    } catch (...) {
        result.status  = "engine_error";
        result.message = "MeshLib failed with an unknown exception.";
    }
    if (!result.success)
        result.mesh = {};
    report["status"]              = result.status;
    report["message"]             = result.message;
    report["elapsed_ms"]["total"] = elapsed_ms(started);
    result.report_json            = report.dump();
    return result;
}
} // namespace OrcaMeshLib
