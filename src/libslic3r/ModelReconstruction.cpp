#include "ModelReconstruction.hpp"
#include "I18N.hpp"

#include "Model.hpp"
#include "ModelRepairEvidence.hpp"
#include "TriangleMesh.hpp"
#include "Utils.hpp"
#include "Format/STL.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cctype>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace Slic3r {
namespace {
namespace fs                        = boost::filesystem;
using json                          = nlohmann::json;
constexpr size_t  MAX_REPORT_BYTES  = 256 * 1024;
constexpr size_t  MAX_FILE_BYTES    = 256 * 1024 * 1024;
constexpr size_t  INPUT_FACE_LIMIT  = RECONSTRUCTION_MAX_INPUT_FACES;
constexpr size_t  OUTPUT_FACE_LIMIT = RECONSTRUCTION_MAX_OUTPUT_FACES;
constexpr char    MESH_MAGIC[8]     = {'O', 'R', 'C', 'A', 'R', 'C', '0', '1'};
std::atomic<bool> reconstruction_running{false};

std::string digest(boost::uuids::detail::sha1& hash)
{
    unsigned int words[5];
    hash.get_digest(words);
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto word : words)
        out << std::setw(8) << word;
    return out.str();
}

std::string mesh_identity(const TriangleMesh& mesh)
{
    boost::uuids::detail::sha1 hash;
    const uint64_t             nv = mesh.its.vertices.size(), nf = mesh.its.indices.size();
    hash.process_bytes(MESH_MAGIC, sizeof(MESH_MAGIC));
    hash.process_bytes(&nv, sizeof(nv));
    hash.process_bytes(&nf, sizeof(nf));
    for (const auto& vertex : mesh.its.vertices)
        hash.process_bytes(vertex.data(), 3 * sizeof(float));
    for (const auto& face : mesh.its.indices)
        hash.process_bytes(face.data(), 3 * sizeof(int32_t));
    return digest(hash);
}

void validate_mesh_storage(const TriangleMesh& mesh, size_t face_limit)
{
    if (mesh.its.indices.empty() || mesh.its.vertices.empty())
        throw std::runtime_error(L("Reconstruction input or output is empty."));
    if (mesh.its.indices.size() > face_limit || mesh.its.vertices.size() > face_limit * 3)
        throw std::runtime_error(L("Reconstruction mesh exceeds the supported size limit."));
    for (const auto& v : mesh.its.vertices)
        if (!v.allFinite())
            throw std::runtime_error(L("Reconstruction mesh contains non-finite coordinates."));
    for (const auto& f : mesh.its.indices)
        for (int i = 0; i != 3; ++i)
            if (f[i] < 0 || size_t(f[i]) >= mesh.its.vertices.size())
                throw std::runtime_error(L("Reconstruction mesh contains an invalid face index."));
}

// Fixed-size, same-executable local protocol. File size/counts are checked
// before allocation; STL importers are never used to interpret worker output.
void write_mesh(const fs::path& path, const TriangleMesh& mesh, size_t limit)
{
    validate_mesh_storage(mesh, limit);
    boost::nowide::ofstream out(path.string(), std::ios::binary | std::ios::trunc);
    const uint64_t          nv = mesh.its.vertices.size(), nf = mesh.its.indices.size();
    out.write(MESH_MAGIC, sizeof(MESH_MAGIC));
    out.write(reinterpret_cast<const char*>(&nv), sizeof(nv));
    out.write(reinterpret_cast<const char*>(&nf), sizeof(nf));
    for (const auto& v : mesh.its.vertices)
        out.write(reinterpret_cast<const char*>(v.data()), 3 * sizeof(float));
    for (const auto& f : mesh.its.indices)
        out.write(reinterpret_cast<const char*>(f.data()), 3 * sizeof(int32_t));
    out.close();
    if (!out)
        throw std::runtime_error(L("Could not write the reconstruction mesh."));
}

TriangleMesh read_mesh(const fs::path& path, size_t limit)
{
    if (!fs::is_regular_file(fs::symlink_status(path)))
        throw std::runtime_error(L("Reconstruction mesh is missing or is not a regular file."));
    const auto bytes = fs::file_size(path);
    if (bytes < 24 || bytes > 24 + uint64_t(limit) * 48)
        throw std::runtime_error(L("Reconstruction mesh file exceeds its size limit."));
    boost::nowide::ifstream in(path.string(), std::ios::binary);
    char                    magic[8];
    uint64_t                nv = 0, nf = 0;
    in.read(magic, sizeof(magic));
    in.read(reinterpret_cast<char*>(&nv), sizeof(nv));
    in.read(reinterpret_cast<char*>(&nf), sizeof(nf));
    if (!in || std::memcmp(magic, MESH_MAGIC, sizeof(magic)) || nf == 0 || nf > limit || nv == 0 || nv > limit * 3 ||
        bytes != 24 + 12 * (nv + nf))
        throw std::runtime_error(L("Invalid or truncated reconstruction mesh protocol."));
    indexed_triangle_set its;
    its.vertices.resize(size_t(nv));
    its.indices.resize(size_t(nf));
    for (auto& v : its.vertices)
        in.read(reinterpret_cast<char*>(v.data()), 3 * sizeof(float));
    for (auto& f : its.indices)
        in.read(reinterpret_cast<char*>(f.data()), 3 * sizeof(int32_t));
    if (!in)
        throw std::runtime_error(L("Could not read the complete reconstruction mesh."));
    // TriangleMesh's constructor computes volume using face indices. Validate
    // raw storage before that constructor can dereference untrusted indices.
    for (const auto& v : its.vertices)
        if (!v.allFinite())
            throw std::runtime_error(L("Reconstruction mesh contains non-finite coordinates."));
    for (const auto& f : its.indices)
        for (int i = 0; i != 3; ++i)
            if (f[i] < 0 || size_t(f[i]) >= its.vertices.size())
                throw std::runtime_error(L("Reconstruction mesh contains an invalid face index."));
    TriangleMesh mesh(std::move(its));
    validate_mesh_storage(mesh, limit);
    return mesh;
}

json read_json(const fs::path& path)
{
    if (!fs::is_regular_file(fs::symlink_status(path)) || fs::file_size(path) > MAX_REPORT_BYTES)
        throw std::runtime_error(L("Reconstruction report is missing or exceeds its size limit."));
    boost::nowide::ifstream in(path.string());
    json                    data;
    in >> data;
    if (!in || !data.is_object())
        throw std::runtime_error(L("Invalid reconstruction report."));
    return data;
}

void write_json(const fs::path& path, const json& data)
{
    const auto encoded = data.dump(2);
    if (encoded.size() > MAX_REPORT_BYTES)
        throw std::runtime_error(L("Reconstruction report exceeds its size limit."));
    const fs::path          temporary(path.string() + ".tmp");
    boost::nowide::ofstream out(temporary.string(), std::ios::trunc);
    out << encoded << '\n';
    out.close();
    if (!out)
        throw std::runtime_error(L("Could not write the reconstruction report."));
    fs::rename(temporary, path);
}

json parameters(const MeshReconstructionOptions& o)
{
    return {{"alpha_mm", o.alpha_mm},
            {"offset_mm", o.offset_mm},
            {"max_deviation_mm", o.max_deviation_mm},
            {"close_open_boundaries", o.close_open_boundaries},
            {"deviation_policy", "warning_only"},
            {"max_input_faces", o.max_input_faces},
            {"max_output_faces", o.max_output_faces}};
}

MeshReconstructionOptions parse_parameters(const json& j)
{
    MeshReconstructionOptions o;
    o.alpha_mm              = j.at("alpha_mm").get<double>();
    o.offset_mm             = j.at("offset_mm").get<double>();
    o.max_deviation_mm      = j.at("max_deviation_mm").get<double>();
    o.close_open_boundaries = j.value("close_open_boundaries", true);
    o.max_input_faces       = j.at("max_input_faces").get<size_t>();
    o.max_output_faces      = j.at("max_output_faces").get<size_t>();
    if (o.max_input_faces == 0 || o.max_input_faces > INPUT_FACE_LIMIT || o.max_output_faces == 0 ||
        o.max_output_faces > OUTPUT_FACE_LIMIT || !std::isfinite(o.alpha_mm) || !std::isfinite(o.offset_mm) ||
        !std::isfinite(o.max_deviation_mm) || o.alpha_mm <= 0 || o.offset_mm <= 0 || o.max_deviation_mm <= 0 ||
        o.offset_mm > o.alpha_mm * 0.25)
        throw std::invalid_argument(L("Invalid reconstruction parameters or unsupported resource limits."));
    return o;
}

struct TaskDirectory
{
    fs::path path;
    TaskDirectory()
    {
        path = fs::temp_directory_path() / fs::unique_path("orca-reconstruction-%%%%-%%%%-%%%%-%%%%");
        if (!fs::create_directory(path))
            throw std::runtime_error(L("Could not create a private reconstruction directory."));
        fs::permissions(path, fs::owner_all);
    }
    ~TaskDirectory()
    {
        boost::system::error_code ec;
        fs::remove_all(path, ec);
    }
};

struct RunningGuard
{
    bool acquired = !reconstruction_running.exchange(true);
    ~RunningGuard()
    {
        if (acquired)
            reconstruction_running.store(false);
    }
};

json base_report()
{
    return {{"schema", "model_reconstruction_v1"},
            {"status", "failed"},
            {"mesh_validated", false},
            {"shape_confirmation", "not_confirmed"},
            {"trial_slice", "not_run"},
            {"print_ready", false},
            {"applied", false},
            {"requires_manual_confirmation", true}};
}

TriangleMesh read_reconstruction_stl(const fs::path& path)
{
    if (!fs::is_regular_file(path))
        throw std::invalid_argument(L("STL input is missing or is not a regular file."));
    const auto file_bytes = fs::file_size(path);
    if (file_bytes < 15 || file_bytes > MAX_FILE_BYTES)
        throw std::invalid_argument(L("STL input is missing, empty, or larger than 256 MiB."));
    // Parse one bounded snapshot. Do not validate a path and then reopen it with
    // an unrestricted importer, whose ASCII header scanner has smaller buffers.
    boost::nowide::ifstream in(path.string(), std::ios::binary);
    std::string             data(size_t(file_bytes), '\0');
    in.read(&data[0], std::streamsize(data.size()));
    if (!in || in.peek() != std::char_traits<char>::eof())
        throw std::invalid_argument(L("Could not read a stable, bounded STL input."));
    const auto* bytes    = reinterpret_cast<const unsigned char*>(data.data());
    const auto  read_u32 = [](const unsigned char* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    };
    indexed_triangle_set its;
    const auto           append_triangle = [&](const std::array<Vec3f, 3>& vertices) {
        if (its.indices.size() >= INPUT_FACE_LIMIT)
            throw std::invalid_argument(L("STL exceeds the reconstruction face limit."));
        for (const auto& vertex : vertices)
            if (!vertex.allFinite())
                throw std::invalid_argument(L("STL contains a non-finite coordinate."));
        const int first = int(its.vertices.size());
        its.vertices.insert(its.vertices.end(), vertices.begin(), vertices.end());
        its.indices.emplace_back(first, first + 1, first + 2);
    };
    const uint32_t faces = data.size() >= 84 ? read_u32(bytes + 80) : 0;
    if (data.size() >= 84 && data.size() == 84 + uint64_t(faces) * 50) {
        const std::string header(data.data(), 80);
        if (header.find("COLOR=") != std::string::npos || header.find("MATERIAL=") != std::string::npos)
            throw std::invalid_argument(L("Colored or extended binary STL is not supported by reconstruction."));
        if (faces == 0 || faces > INPUT_FACE_LIMIT)
            throw std::invalid_argument(L("STL exceeds the reconstruction face limit."));
        its.vertices.reserve(size_t(faces) * 3);
        its.indices.reserve(faces);
        for (uint32_t i = 0; i < faces; ++i) {
            const unsigned char* face = bytes + 84 + size_t(i) * 50;
            if (face[48] != 0 || face[49] != 0)
                throw std::invalid_argument(L("Colored or extended binary STL is not supported by reconstruction."));
            std::array<Vec3f, 3> vertices;
            for (size_t vertex = 0; vertex < 3; ++vertex)
                for (size_t axis = 0; axis < 3; ++axis) {
                    const uint32_t bits = read_u32(face + 12 + vertex * 12 + axis * 4);
                    float          value;
                    static_assert(sizeof(value) == sizeof(bits));
                    std::memcpy(&value, &bits, sizeof(value));
                    vertices[vertex][axis] = value;
                }
            append_triangle(vertices);
        }
    } else {
        // Keywords are case-sensitive per STL syntax. Names may contain UTF-8
        // and spaces but must fit a bounded line; numeric tokens have a separate
        // limit and always use the C numeric grammar, irrespective of UI locale.
        size_t line_length = 0;
        for (const unsigned char c : data) {
            if (c == '\0' || (c != '\n' && ++line_length > 4096))
                throw std::invalid_argument(L("Invalid or overlong ASCII STL line."));
            if (c == '\n')
                line_length = 0;
        }
        const auto is_space   = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; };
        size_t     position   = 0;
        const auto next_token = [&]() {
            while (position < data.size() && is_space(static_cast<unsigned char>(data[position])))
                ++position;
            const size_t start = position;
            while (position < data.size() && !is_space(static_cast<unsigned char>(data[position]))) {
                if (position - start >= 128)
                    throw std::invalid_argument(L("An ASCII STL token exceeds the supported length."));
                ++position;
            }
            return data.substr(start, position - start);
        };
        const auto expect = [&](const char* expected) {
            if (next_token() != expected)
                throw std::invalid_argument(L("Invalid or truncated ASCII STL triangle syntax."));
        };
        const auto skip_name = [&]() {
            while (position < data.size() && data[position] != '\n')
                ++position;
        };
        std::istringstream number;
        number.imbue(std::locale::classic());
        const auto read_number = [&]() {
            const std::string token = next_token();
            if (token.empty())
                throw std::invalid_argument(L("Missing ASCII STL coordinate."));
            number.clear();
            number.str(token);
            double value = 0;
            if (!(number >> value) || number.peek() != std::char_traits<char>::eof() || !std::isfinite(value) ||
                std::abs(value) > std::numeric_limits<float>::max())
                throw std::invalid_argument(L("Invalid or non-finite ASCII STL coordinate."));
            return static_cast<float>(value);
        };
        bool inside_solid = false;
        for (std::string token = next_token(); !token.empty(); token = next_token()) {
            if (token == "solid") {
                if (inside_solid)
                    throw std::invalid_argument(L("Nested ASCII STL solids are not supported."));
                skip_name();
                inside_solid = true;
            } else if (token == "endsolid") {
                if (!inside_solid)
                    throw std::invalid_argument(L("Unexpected ASCII STL endsolid."));
                skip_name();
                inside_solid = false;
            } else if (token == "facet" && inside_solid) {
                expect("normal");
                for (size_t axis = 0; axis < 3; ++axis)
                    read_number();
                expect("outer");
                expect("loop");
                std::array<Vec3f, 3> vertices;
                for (auto& vertex : vertices) {
                    expect("vertex");
                    for (size_t axis = 0; axis < 3; ++axis)
                        vertex[axis] = read_number();
                }
                expect("endloop");
                expect("endfacet");
                append_triangle(vertices);
            } else {
                throw std::invalid_argument(L("Invalid ASCII STL input."));
            }
        }
        if (inside_solid || its.indices.empty())
            throw std::invalid_argument(L("ASCII STL is incomplete or contains no triangles."));
    }
    // All indices are created above, after bounded vertex insertion; no file
    // indices can reach TriangleMesh's statistics/volume constructor unchecked.
    return TriangleMesh(std::move(its));
}
} // namespace

ReconstructionProcessOptions model_reconstruction_process_options(size_t input_faces,
                                                                  const ReconstructionProcessOptions& requested,
                                                                  size_t physical_memory_bytes)
{
    auto resolved = requested;
    if (resolved.timeout_ms == 0)
        resolved.timeout_ms = input_faces > 1000000 ? 300000 : (input_faces > 200000 ? 180000 : 60000);
    if (resolved.memory_limit_mb == 0) {
        const size_t target_mb = input_faces > 1000000 ? 4096 : (input_faces > 200000 ? 2048 : 1024);
        // Leave RAM for the caller's model, UI, other applications and the OS.
        // Unknown RAM must not silently select the largest worker budget.
        const size_t host_mb = physical_memory_bytes == 0 ? 1024 : physical_memory_bytes / (4 * 1024 * 1024);
        resolved.memory_limit_mb = std::min(target_mb, std::max(size_t(128), host_mb));
    }
    if (resolved.timeout_ms > 300000 || resolved.memory_limit_mb < 128 || resolved.memory_limit_mb > 4096)
        throw std::invalid_argument(L("Invalid reconstruction parameters or unsupported resource limits."));
    return resolved;
}

std::string model_reconstruction_ineligibility(const ModelObject& object)
{
    if (object.volumes.size() != 1 || !object.volumes.front() || !object.volumes.front()->is_model_part())
        return L("Reconstruction requires one ordinary model part without modifiers or negative volumes.");
    if (object.instances.size() != 1 || !object.instances.front())
        return L("Reconstruction currently requires exactly one model instance.");
    const auto& volume = *object.volumes.front();
    if (volume.mesh().its.indices.size() > INPUT_FACE_LIMIT || volume.mesh().its.vertices.size() > INPUT_FACE_LIMIT * 3)
        return L("Reconstruction mesh exceeds the supported size limit.");
    if (volume.is_any_painted())
        return L("Reconstruction does not support painted colors, supports, seams, or fuzzy skin.");
    if (!object.layer_config_ranges.empty() || !object.layer_height_profile.empty() || !object.brim_points.empty() ||
        !object.sla_support_points.empty() || !object.sla_drain_holes.empty())
        return L("Remove layer profiles, custom brim points, and resin annotations before reconstruction.");
    if (object.is_cut() || !object.cut_connectors.empty() || volume.cut_info.is_connector || volume.cut_info.painting_source_id.valid() ||
        volume.text_configuration || volume.emboss_shape)
        return L("Cut connectors and editable text or embossed shapes are not supported by reconstruction.");
    if (object.get_model() && object.get_model()->texture_mesh)
        return L("Textured models are not supported by reconstruction.");
    const Transform3d transform = object.instances.front()->get_matrix() * volume.get_matrix();
    if (!transform.matrix().allFinite() || std::abs(transform.linear().determinant()) < 1e-12)
        return L("The model transform is invalid or singular.");
    return {};
}

std::string model_reconstruction_source_identity(const ModelObject& object)
{
    json state{{"object", object.id().id},
               {"name", object.name},
               {"printable", object.printable},
               {"eligibility", model_reconstruction_ineligibility(object)},
               {"config", json::object()}};
    for (const auto& key : object.config.keys())
        state["config"][key] = object.config.opt_serialize(key);
    for (const auto* volume : object.volumes) {
        if (!volume) {
            state["volumes"].push_back(nullptr);
            continue;
        }
        json       item{{"id", volume->id().id},
                        {"name", volume->name},
                        {"type", int(volume->type())},
                        {"material", volume->material_id()},
                        {"mesh", mesh_identity(volume->mesh())}};
        const auto matrix         = volume->get_matrix().matrix().eval();
        item["transform"]         = std::vector<double>(matrix.data(), matrix.data() + 16);
        const auto  source_matrix = volume->source.transform.get_matrix().matrix().eval();
        const auto& origin        = volume->source;
        item["source"]            = {origin.input_file,
                                     origin.object_idx,
                                     origin.volume_idx,
                                     std::vector<double>(origin.mesh_offset.data(), origin.mesh_offset.data() + 3),
                                     std::vector<double>(source_matrix.data(), source_matrix.data() + 16),
                                     origin.is_converted_from_inches,
                                     origin.is_converted_from_meters,
                                     origin.is_from_builtin_objects};
        for (const auto& key : volume->config.keys())
            item["config"][key] = volume->config.opt_serialize(key);
        state["volumes"].push_back(std::move(item));
    }
    for (const auto* instance : object.instances) {
        if (!instance) {
            state["instances"].push_back(nullptr);
            continue;
        }
        const auto  matrix   = instance->get_matrix().matrix().eval();
        const auto  assembly = instance->get_assemble_transformation().get_matrix().matrix().eval();
        const Vec3d offset   = instance->get_offset_to_assembly();
        state["instances"].push_back({{"id", instance->id().id},
                                      {"printable", instance->printable},
                                      {"assembly", std::vector<double>(assembly.data(), assembly.data() + 16)},
                                      {"assembly_offset", std::vector<double>(offset.data(), offset.data() + 3)},
                                      {"transform", std::vector<double>(matrix.data(), matrix.data() + 16)}});
    }
    boost::uuids::detail::sha1 hash;
    const auto                 encoded = state.dump();
    hash.process_bytes(encoded.data(), encoded.size());
    return digest(hash);
}

ModelReconstructionResult reconstruct_model_candidate(const Model& model, size_t object_index, const ModelReconstructionOptions& options)
{
    ModelReconstructionResult result;
    result.report        = base_report();
    const auto   started = std::chrono::steady_clock::now();
    RunningGuard running;
    try {
        if (!running.acquired)
            throw std::runtime_error(L("Another reconstruction is already running."));
        if (options.process.is_canceled && options.process.is_canceled()) {
            result.status = "canceled";
            throw std::runtime_error(L("Reconstruction was canceled."));
        }
        const auto mesh_options = parse_parameters(parameters(options.mesh));
        if (object_index >= model.objects.size() || !model.objects[object_index])
            throw std::invalid_argument(L("No model object selected for reconstruction."));
        const auto& source_object = *model.objects[object_index];
        const auto  error         = model_reconstruction_ineligibility(source_object);
        if (!error.empty()) {
            result.status = "unsupported";
            throw std::invalid_argument(error);
        }
        validate_mesh_storage(source_object.volumes.front()->mesh(), mesh_options.max_input_faces);
        const auto process_options = model_reconstruction_process_options(source_object.volumes.front()->mesh().its.indices.size(),
                                                                          options.process, total_physical_memory());
        result.report["resource_limits"] = {{"timeout_ms", process_options.timeout_ms},
                                             {"memory_limit_mb", process_options.memory_limit_mb},
                                             {"max_input_faces", mesh_options.max_input_faces},
                                             {"max_output_faces", mesh_options.max_output_faces}};
        const auto source_identity = model_reconstruction_source_identity(source_object);
        // All source object access is before the synchronous runner pumps GUI
        // events. After that, only the private staging copy is used.
        auto              staged    = std::shared_ptr<Model>(model.make_object_staging_copy({object_index}));
        auto&             object    = *staged->objects[object_index];
        auto&             volume    = *object.volumes.front();
        const Transform3d to_mm     = object.instances.front()->get_matrix() * volume.get_matrix();
        const auto        matrix    = to_mm.matrix().eval();
        TriangleMesh      source_mm = volume.mesh();
        validate_mesh_storage(source_mm, mesh_options.max_input_faces);
        source_mm.transform(to_mm, true);
        validate_mesh_storage(source_mm, mesh_options.max_input_faces);
        TaskDirectory task;
        const auto    request = json{{"schema", "reconstruction_worker_v1"},
                                     {"source_identity", source_identity},
                                     {"mesh_identity", mesh_identity(source_mm)},
                                     {"parameters", parameters(mesh_options)},
                                     {"to_mm", std::vector<double>(matrix.data(), matrix.data() + 16)},
                                     {"parent_pid", reconstruction_process_id()},
                                     {"timeout_ms", process_options.timeout_ms},
                                     {"memory_limit_mb", process_options.memory_limit_mb}};
        write_mesh(task.path / "input.mesh", source_mm, mesh_options.max_input_faces);
        write_json(task.path / "request.json", request);
        const auto process = run_reconstruction_process(options.executable, {"--reconstruction-worker", task.path.string()},
                                                        process_options);
        if (process.status != ReconstructionProcessStatus::Success) {
            result.status = reconstruction_process_status_name(process.status);
            throw std::runtime_error(process.message.empty() ? L("Reconstruction worker failed.") : process.message);
        }
        auto report = read_json(task.path / "report.json");
        if (report.value("schema", "") != "model_reconstruction_v1" || report.value("source_identity", "") != source_identity ||
            report.value("input_mesh_identity", "") != request.at("mesh_identity"))
            throw std::runtime_error(L("Reconstruction worker returned a mismatched report."));
        report["resource_limits"]   = result.report.at("resource_limits");
        result.status               = report.value("status", "failed");
        result.message              = report.value("message", "");
        result.report               = std::move(report);
        result.report["elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                                          .count();
        if (result.status != "candidate_ready" || !result.report.value("mesh_validated", false)) {
            return result;
        }
        auto candidate_mm = read_mesh(task.path / "candidate.mesh", mesh_options.max_output_faces);
        if (mesh_identity(candidate_mm) != result.report.at("output_mesh_identity").get<std::string>())
            throw std::runtime_error(L("Reconstruction output checksum does not match the validated candidate."));
        if (options.process.is_canceled && options.process.is_canceled()) {
            result.status = "canceled";
            throw std::runtime_error(L("Reconstruction was canceled before candidate delivery."));
        }
        const auto before_box      = source_mm.bounding_box();
        const auto after_box       = candidate_mm.bounding_box();
        result.report["placement"] = {{"before_min_z_mm", before_box.min.z()},
                                      {"after_min_z_mm", after_box.min.z()},
                                      {"suggested_lift_mm", std::max(0.0, double(-after_box.min.z()))},
                                      {"automatically_moved", false}};
        auto candidate_local       = read_mesh(task.path / "candidate.local.mesh", mesh_options.max_output_faces);
        if (mesh_identity(candidate_local) != result.report.at("local_mesh_identity").get<std::string>())
            throw std::runtime_error(L("Reconstruction output checksum does not match the validated candidate."));
        auto candidate_hull = read_mesh(task.path / "candidate.hull.mesh", mesh_options.max_output_faces);
        if (mesh_identity(candidate_hull) != result.report.at("convex_hull_mesh_identity").get<std::string>() ||
            !result.report.at("convex_hull_validation").at("validation").value("complete", false))
            throw std::runtime_error(L("Reconstruction convex hull does not match the validated candidate."));
        // Replace only the private mesh; deleting/re-adding a lone volume would
        // collapse its transform into the instance and change assembly metadata.
        // All hull computation stays in the limited worker. Installing its
        // matching hull also invalidates the copied source's 2D footprint cache.
        volume.set_mesh(std::move(candidate_local));
        volume.set_convex_hull(std::move(candidate_hull));
        object.instances.front()->invalidate_convex_hull_2d();
        volume.set_new_unique_id();
        volume.exterior_facets.reset();
        object.invalidate_bounding_box();
        result.candidate_model = std::move(staged);
        return result;
    } catch (const std::exception& error) {
        if (result.status == "candidate_ready")
            result.status = "failed";
        result.message              = error.what();
        result.report["status"]     = result.status;
        result.report["message"]    = result.message;
        result.report["elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                                          .count();
        result.report["mesh_validated"] = false;
        result.candidate_model.reset();
        return result;
    }
}

int run_model_reconstruction_worker(const std::string& task_directory)
{
    const fs::path task(task_directory);
    auto           report = base_report();
    try {
        const auto request = read_json(task / "request.json");
        if (request.at("schema") != "reconstruction_worker_v1")
            throw std::runtime_error(L("Unsupported reconstruction protocol."));
        const size_t memory_limit = request.at("memory_limit_mb").get<size_t>();
        if (memory_limit < 128 || memory_limit > 4096)
            throw std::runtime_error(L("Unsupported reconstruction memory limit."));
        std::string limit_error;
        if (!apply_reconstruction_worker_limits(memory_limit, limit_error))
            throw std::runtime_error(limit_error);
        const unsigned timeout_ms = request.at("timeout_ms").get<unsigned>();
        if (timeout_ms == 0 || timeout_ms > 300000)
            throw std::runtime_error(L("Unsupported reconstruction time limit."));
        ReconstructionWorkerGuard worker_guard(request.at("parent_pid").get<uint64_t>(), timeout_ms, memory_limit);
        const auto                options = parse_parameters(request.at("parameters"));
        report["source_identity"]         = request.at("source_identity");
        report["input_mesh_identity"]     = request.at("mesh_identity");
        report["parameters"]              = parameters(options);
        auto source                       = read_mesh(task / "input.mesh", options.max_input_faces);
        if (mesh_identity(source) != report.at("input_mesh_identity").get<std::string>())
            throw std::runtime_error(L("Reconstruction input checksum mismatch."));
        TriangleMesh candidate;
        json         engine_report;
        const bool   generated = reconstruct_mesh(source, candidate, engine_report, options);
        report["engine"]       = engine_report;
        report["status"]       = engine_report.value("status", generated ? "candidate_ready" : "failed");
        report["message"]      = engine_report.value("message", "");
        report["metrics"]      = engine_report.value("metrics", json::object());
        if (generated) {
            const auto transform_values = request.at("to_mm").get<std::vector<double>>();
            if (transform_values.size() != 16)
                throw std::runtime_error(L("Invalid reconstruction transform."));
            Transform3d to_mm;
            std::copy(transform_values.begin(), transform_values.end(), to_mm.matrix().data());
            if (!to_mm.matrix().allFinite() || std::abs(to_mm.linear().determinant()) < 1e-12 ||
                !to_mm.matrix().row(3).isApprox(Eigen::RowVector4d(0, 0, 0, 1)))
                throw std::runtime_error(L("Invalid reconstruction transform."));
            TriangleMesh local = candidate;
            local.transform(to_mm.inverse(), true);
            TriangleMesh round_trip = local;
            round_trip.transform(to_mm, true);
            double max_round_trip = 0;
            for (size_t i = 0; i < candidate.its.vertices.size(); ++i)
                max_round_trip = std::max(max_round_trip, (candidate.its.vertices[i] - round_trip.its.vertices[i]).cast<double>().norm());
            if (!std::isfinite(max_round_trip) || max_round_trip > std::min(1e-4, options.offset_mm * 0.01))
                throw std::runtime_error(L("The model transform loses too much precision for reconstruction."));
            json local_check, round_trip_check;
            if (!validate_reconstruction_mesh(local, local_check, options.max_output_faces) ||
                !validate_reconstruction_mesh(round_trip, round_trip_check, options.max_output_faces))
                throw std::runtime_error(L("Reconstruction geometry became invalid after transforming to model coordinates."));
            report["metrics"]["transform_round_trip_error_mm"] = max_round_trip;
            report["coordinate_validation"]                    = {{"local", local_check}, {"physical_round_trip", round_trip_check}};
            // QHull may use substantial CPU and memory and can return an empty
            // mesh on failure. Compute and validate it before leaving the worker
            // budget; the parent must never recompute a missing or invalid hull.
            TriangleMesh hull = local.convex_hull_3d();
            json         hull_check;
            if (!validate_reconstruction_mesh(hull, hull_check, options.max_output_faces))
                throw std::runtime_error(L("Could not produce a valid bounded reconstruction convex hull."));
            report["convex_hull_validation"] = std::move(hull_check);
            write_mesh(task / "candidate.mesh", candidate, options.max_output_faces);
            auto serialized = read_mesh(task / "candidate.mesh", options.max_output_faces);
            if (mesh_identity(serialized) != mesh_identity(candidate))
                throw std::runtime_error(L("Reconstruction output did not round-trip exactly."));
            report["output_mesh_identity"] = mesh_identity(serialized);
            write_mesh(task / "candidate.local.mesh", local, options.max_output_faces);
            auto local_serialized = read_mesh(task / "candidate.local.mesh", options.max_output_faces);
            if (mesh_identity(local_serialized) != mesh_identity(local))
                throw std::runtime_error(L("Reconstruction output did not round-trip exactly."));
            report["local_mesh_identity"] = mesh_identity(local_serialized);
            write_mesh(task / "candidate.hull.mesh", hull, options.max_output_faces);
            auto hull_serialized = read_mesh(task / "candidate.hull.mesh", options.max_output_faces);
            if (mesh_identity(hull_serialized) != mesh_identity(hull))
                throw std::runtime_error(L("Reconstruction convex hull did not round-trip exactly."));
            report["convex_hull_mesh_identity"] = mesh_identity(hull_serialized);
            report["mesh_validated"]            = true;
        }
        write_json(task / "report.json", report);
        // A completed rejection is a valid worker response, not process failure.
        return 0;
    } catch (const std::exception& error) {
        report["status"]         = "failed";
        report["message"]        = error.what();
        report["mesh_validated"] = false;
        try {
            write_json(task / "report.json", report);
            return 0;
        } catch (...) {
            return 1;
        }
    } catch (...) {
        return 1;
    }
}

int run_model_reconstruction_cli(const std::string& executable, const std::vector<std::string>& args)
{
    ModelReconstructionOptions options;
    options.executable = executable;
    std::string input, output;
    bool        confirmed = false;
    bool        repair_evidence = false;
    fs::path    output_path;
    bool        output_owned = false;
    try {
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& arg = args[i];
            if (arg == "--reconstruct-model")
                continue;
            if (arg == "--repair-evidence" || arg.compare(0, 18, "--repair-evidence=") == 0) {
                std::string value;
                if (arg != "--repair-evidence")
                    value = arg.substr(18);
                else if (i + 1 < args.size() && (args[i + 1] == "0" || args[i + 1] == "1"))
                    value = args[++i];
                if (!value.empty() && value != "0" && value != "1")
                    throw std::invalid_argument(L("--repair-evidence expects 0 or 1."));
                repair_evidence = value != "0";
                continue;
            }
            if (arg == "--accept-reconstruction-risk") {
                confirmed = true;
                continue;
            }
            if (arg == "--reconstruction-no-close-open-boundaries") {
                options.mesh.close_open_boundaries = false;
                continue;
            }
            if (arg == "--help") {
                boost::nowide::cout << L(
                    "Usage: --reconstruct-model INPUT.stl --outputdir NEW_DIRECTORY --accept-reconstruction-risk\n"
                    "Options: --reconstruction-alpha MM --reconstruction-offset MM --reconstruction-max-deviation MM\n"
                    "         --reconstruction-timeout SECONDS --reconstruction-no-close-open-boundaries\n"
                    "         --repair-evidence [0|1] (optional read-only boundary evidence, disabled by default)\n"
                    "Generates an approximate candidate only. Original input is never overwritten.\n"
                    "STL units are millimeters. Shape, holes and cavities may change; colored STL is unsupported.\n"
                    "Maximum deviation is a warning threshold, not a rejection limit. Up to three bounded wrap attempts.\n"
                    "Open boundaries are closed before wrapping when a validated solid can be formed; this may fill intended openings.\n"
                    "Use --reconstruction-no-close-open-boundaries to keep the original surface-wrapping behavior.\n"
                    "Input limits: 3000000 faces, 256 MiB STL. Output limit: 1000000 faces.\n"
                    "Automatic worker budgets: 60/180/300 seconds and up to 1024/2048/4096 MiB by input size.\n"
                    "Automatic memory is capped at one quarter of physical RAM; explicit timeout overrides the default.\n"
                    "Candidate requires shape confirmation and a separate slice; this command never prints.\n");
                return 0;
            }
            if (arg == "--outputdir" || arg == "--reconstruction-alpha" || arg == "--reconstruction-offset" ||
                arg == "--reconstruction-max-deviation" || arg == "--reconstruction-timeout") {
                if (++i >= args.size())
                    throw std::invalid_argument(L("Missing reconstruction option value."));
                if (arg == "--outputdir") {
                    output = args[i];
                    continue;
                }
                size_t       end   = 0;
                const double value = std::stod(args[i], &end);
                if (end != args[i].size() || !std::isfinite(value) || value <= 0)
                    throw std::invalid_argument(L("Reconstruction options require finite positive numbers."));
                if (arg == "--reconstruction-alpha")
                    options.mesh.alpha_mm = value;
                else if (arg == "--reconstruction-offset")
                    options.mesh.offset_mm = value;
                else if (arg == "--reconstruction-max-deviation")
                    options.mesh.max_deviation_mm = value;
                else {
                    if (value < 1 || value > 300)
                        throw std::invalid_argument(L("Reconstruction timeout must be 1 to 300 seconds."));
                    options.process.timeout_ms = unsigned(value * 1000);
                }
            } else if (!arg.empty() && arg.front() != '-' && input.empty())
                input = arg;
            else
                throw std::invalid_argument(L("Unsupported reconstruction option or extra input: ") + arg);
        }
        parse_parameters(parameters(options.mesh));
        if (!confirmed || input.empty() || output.empty())
            throw std::invalid_argument(L("Reconstruction requires one STL, a new --outputdir, and --accept-reconstruction-risk."));
        fs::path input_path = fs::absolute(input);
        auto     extension  = input_path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (extension != ".stl")
            throw std::invalid_argument(L("Standalone reconstruction currently accepts only an uncolored STL."));
        TriangleMesh mesh = read_reconstruction_stl(input_path);
        output_path       = fs::absolute(output);
        if (fs::exists(output_path) || !fs::is_directory(output_path.parent_path()))
            throw std::invalid_argument(L("Reconstruction output must be a new directory under an existing parent."));
        if (!fs::create_directory(output_path))
            throw std::runtime_error(L("Could not create reconstruction output directory."));
        output_owned = true;
        validate_mesh_storage(mesh, INPUT_FACE_LIMIT);
        Model model;
        auto* object = model.add_object();
        object->name = input_path.filename().string();
        object->add_volume(std::move(mesh), ModelVolumeType::MODEL_PART, false);
        object->add_instance();
        auto result = reconstruct_model_candidate(model, 0, options);
        if (result.candidate_model) {
            auto       candidate      = result.candidate_model->objects[0]->volumes.front()->mesh();
            const auto candidate_path = output_path / "candidate.stl";
            if (!store_stl(candidate_path.string().c_str(), &candidate, true))
                throw std::runtime_error(L("Could not save reconstruction candidate STL."));
            TriangleMesh reread;
            if (!reread.ReadSTLFile(candidate_path.string().c_str(), false) || reread.its.indices.size() != candidate.its.indices.size())
                throw std::runtime_error(L("Exported reconstruction candidate could not be read back."));
            // STL stores each face separately. Compare its physical triangles,
            // not importer-dependent vertex indices or face order.
            auto triangles = [](const TriangleMesh& m) {
                std::vector<std::array<float, 9>> values;
                for (const auto& f : m.its.indices) {
                    std::array<float, 9> t{};
                    for (int v = 0; v < 3; ++v)
                        for (int axis = 0; axis < 3; ++axis)
                            t[v * 3 + axis] = m.its.vertices[f[v]][axis];
                    values.push_back(t);
                }
                std::sort(values.begin(), values.end());
                return values;
            };
            if (triangles(candidate) != triangles(reread))
                throw std::runtime_error(L("Exported reconstruction STL changed its validated triangles."));
            result.report["candidate_file"]    = "candidate.stl";
            result.report["export_round_trip"] = "passed";
        }
        if (repair_evidence)
            result.report["boundary"] = model_reconstruction_boundary(result.report);
        write_json(output_path / "reconstruction.json", result.report);
        boost::nowide::cout << result.report.dump(2) << '\n';
        return result.candidate_model ? 0 : 2;
    } catch (const std::exception& error) {
        auto report       = base_report();
        report["message"] = error.what();
        if (repair_evidence)
            report["boundary"] = model_reconstruction_boundary(report);
        if (output_owned) {
            boost::system::error_code ec;
            fs::remove(output_path / "candidate.stl", ec);
            try {
                write_json(output_path / "reconstruction.json", report);
            } catch (...) {}
        }
        boost::nowide::cerr << report.dump(2) << '\n';
        return 2;
    }
}
} // namespace Slic3r
