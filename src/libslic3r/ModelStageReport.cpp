#include "ModelStageReport.hpp"
#include "ModelReconstruction.hpp"
#include "ModelRepairEvidence.hpp"
#include "I18N.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace Slic3r {
namespace {
namespace fs = boost::filesystem;
using json   = nlohmann::json;

// Same exact-position welding and edge-connected surface definition as
// ModelInspection, including duplicate/degenerate records. Point contact does
// not join components. No repeated self-intersection or containment scan.
std::size_t raw_component_count(const TriangleMesh& mesh)
{
    const auto count = mesh.its.indices.size();
    if (count == 0 || count > RECONSTRUCTION_MAX_INPUT_FACES)
        throw std::runtime_error(L("Stage report requires a bounded nonempty STL mesh."));
    std::vector<std::size_t> parents(count), ranks(count, 0);
    std::iota(parents.begin(), parents.end(), 0);
    const auto root = [&](std::size_t id) {
        while (parents[id] != id) {
            parents[id] = parents[parents[id]];
            id          = parents[id];
        }
        return id;
    };
    std::map<std::array<float, 3>, std::size_t>                vertices;
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> first_face;
    std::size_t                                                components = count;
    for (std::size_t id = 0; id < count; ++id) {
        std::array<std::size_t, 3> face;
        for (int i = 0; i < 3; ++i) {
            const auto index = mesh.its.indices[id][i];
            if (index < 0 || std::size_t(index) >= mesh.its.vertices.size() || !mesh.its.vertices[index].allFinite())
                throw std::runtime_error(L("Stage report contains invalid mesh coordinates or indices."));
            const auto& v = mesh.its.vertices[index];
            face[i]       = vertices.emplace(std::array<float, 3>{v.x(), v.y(), v.z()}, vertices.size()).first->second;
        }
        for (int i = 0; i < 3; ++i) {
            const auto a = face[i], b = face[(i + 1) % 3];
            if (a == b)
                continue;
            const auto prior = first_face.emplace(std::minmax(a, b), id);
            if (!prior.second) {
                auto left = root(prior.first->second), right = root(id);
                if (left != right) {
                    if (ranks[left] < ranks[right])
                        std::swap(left, right);
                    parents[right] = left;
                    if (ranks[left] == ranks[right])
                        ++ranks[left];
                    --components;
                }
            }
        }
    }
    return components;
}

std::vector<std::array<float, 9>> physical_triangles(const TriangleMesh& mesh)
{
    std::vector<std::array<float, 9>> triangles;
    triangles.reserve(mesh.its.indices.size());
    for (const auto& face : mesh.its.indices) {
        std::array<float, 9> t;
        for (int i = 0; i < 3; ++i)
            for (int axis = 0; axis < 3; ++axis)
                t[i * 3 + axis] = mesh.its.vertices[face[i]][axis];
        triangles.push_back(t);
    }
    std::sort(triangles.begin(), triangles.end());
    return triangles;
}

fs::path require_stl(const std::string& file)
{
    const auto path = fs::canonical(file);
    if (boost::algorithm::to_lower_copy(path.extension().string()) != ".stl" || !fs::is_regular_file(path))
        throw std::invalid_argument(L("Stage reports support only uncolored STL files in millimeters."));
    return path;
}
} // namespace

ModelStageReportContext prepare_model_stage_report(const std::string& input_file,
                                                   const std::string& reference_file,
                                                   const std::string& output_directory)
{
    const auto report_path = fs::absolute(output_directory.empty() ? "." : output_directory) / "model-stage.json";
    if (fs::exists(report_path) || fs::is_symlink(report_path))
        throw std::invalid_argument(L("Stage report already exists; use a fresh output directory."));
    ModelStageReportContext context;
    context.input_file     = require_stl(input_file).string();
    context.reference_file = require_stl(reference_file.empty() ? input_file : reference_file).string();
    auto input             = read_bounded_model_stl(context.input_file, context.input_sha256);
    if (context.input_file == context.reference_file) {
        context.reference_mesh   = std::move(input);
        context.reference_sha256 = context.input_sha256;
    } else {
        context.reference_mesh = read_bounded_model_stl(context.reference_file, context.reference_sha256);
    }
    return context;
}

void write_model_stage_report(const ModelStageReportContext& context,
                              const std::string&             output_directory,
                              const std::string&             artifact_file,
                              const TriangleMesh&            inspected_mesh,
                              const json&                    inspection,
                              const std::string&             stage,
                              const std::string&             repair_status,
                              const json&                    reconstruction)
{
    const auto directory = fs::canonical(output_directory.empty() ? "." : output_directory);
    const auto artifact  = require_stl(artifact_file);
    const auto relative  = fs::relative(artifact, directory);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." || artifact == fs::path(context.input_file) ||
        artifact == fs::path(context.reference_file))
        throw std::runtime_error(L("Stage artifact must be a new file inside its output directory."));
    std::string artifact_sha256;
    const auto  reread = read_bounded_model_stl(artifact.string(), artifact_sha256);
    if (physical_triangles(reread) != physical_triangles(inspected_mesh))
        throw std::runtime_error(L("Stage artifact does not match the inspected triangles."));
    auto change = compare_model_stage_meshes(context.reference_mesh, reread);
    // Raw referenced geometry includes zero-area tails excluded from distance
    // queries. Do not hide a far-away deleted feature in the bounding-box gate.
    const auto original_bounds     = context.reference_mesh.bounding_box();
    const auto candidate_bounds    = reread.bounding_box();
    change["bbox_deviation_mm"]    = std::max((original_bounds.min - candidate_bounds.min).cwiseAbs().maxCoeff(),
                                              (original_bounds.max - candidate_bounds.max).cwiseAbs().maxCoeff());
    change["reference_components"] = raw_component_count(context.reference_mesh);
    change["candidate_components"] = raw_component_count(reread);
    change["components_complete"]  = true;
    change["component_method"]     = "exact_position_welded_edge_connected_all_faces";
    json issues                    = json::object();
    for (const char* key :
         {"boundary", "nonmanifold_edge", "nonmanifold_vertex", "self_intersection", "degenerate", "duplicate", "orientation"}) {
        const auto& issue = inspection.at("issues").at(key);
        issues[key]       = {{"count", issue.at("count")}, {"complete", issue.at("complete")}};
    }
    json result = {{"schema", "orca_model_stage_v1"},
                   {"status", "completed"},
                   {"stage", stage},
                   {"input_sha256", context.input_sha256},
                   {"reference_sha256", context.reference_sha256},
                   {"artifact", {{"path", relative.generic_string()}, {"sha256", artifact_sha256}, {"readback_verified", true}}},
                   {"inspection",
                    {{"status", model_inspection_boundary(inspection).at("status")},
                     {"mesh_status", inspection.at("mesh_status")},
                     {"check_complete", inspection.at("check_complete")},
                     {"issues", std::move(issues)}}},
                   {"repair_status", repair_status},
                   {"change", std::move(change)},
                   {"reconstruction", reconstruction}};
    if (model_mesh_file_sha256(context.input_file) != context.input_sha256 ||
        model_mesh_file_sha256(context.reference_file) != context.reference_sha256 ||
        model_mesh_file_sha256(artifact.string()) != artifact_sha256)
        throw std::runtime_error(L("Stage input, reference or artifact changed during execution."));
    const auto destination = directory / "model-stage.json";
    const auto temporary   = directory / fs::unique_path("model-stage-%%%%-%%%%.tmp");
    try {
        boost::nowide::ofstream out(temporary.string(), std::ios::binary | std::ios::trunc);
        out << result.dump(2) << '\n';
        out.close();
        if (!out || fs::exists(destination) || fs::is_symlink(destination))
            throw std::runtime_error(L("Could not publish the stage report without overwriting an existing file."));
        // Publishing a link is atomic and fails if another writer already owns
        // the destination. rename() could overwrite it on POSIX after the check.
        fs::create_hard_link(temporary, destination);
        fs::remove(temporary);
    } catch (...) {
        boost::system::error_code ec;
        fs::remove(temporary, ec);
        throw;
    }
}
} // namespace Slic3r
