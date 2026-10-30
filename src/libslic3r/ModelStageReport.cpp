#include "ModelStageReport.hpp"
#include "ModelReconstruction.hpp"
#include "ModelRepairEvidence.hpp"
#include "I18N.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/container_hash/hash.hpp>
#include <boost/nowide/fstream.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
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
    // IDs follow input order; neither table is iterated. Hashing preserves exact
    // coordinate/edge equality (including signed zero) without tree comparisons.
    using Vertex = std::array<float, 3>;
    using Edge   = std::pair<std::size_t, std::size_t>;
    std::unordered_map<Vertex, std::size_t, boost::hash<Vertex>> vertices;
    std::unordered_map<Edge, std::size_t, boost::hash<Edge>>     first_face;
    vertices.reserve(std::min(mesh.its.vertices.size(), count * 3));
    first_face.reserve(count * 2);
    std::vector<std::size_t> vertex_ids(mesh.its.vertices.size(), std::numeric_limits<std::size_t>::max());
    std::size_t              components = count;
    for (std::size_t id = 0; id < count; ++id) {
        std::array<std::size_t, 3> face;
        for (int i = 0; i < 3; ++i) {
            const auto index = mesh.its.indices[id][i];
            if (index < 0 || std::size_t(index) >= mesh.its.vertices.size() || !mesh.its.vertices[index].allFinite())
                throw std::runtime_error(L("Stage report contains invalid mesh coordinates or indices."));
            auto& canonical = vertex_ids[index];
            if (canonical == std::numeric_limits<std::size_t>::max()) {
                const auto& v = mesh.its.vertices[index];
                canonical     = vertices.emplace(std::array<float, 3>{v.x(), v.y(), v.z()}, vertices.size()).first->second;
            }
            face[i] = canonical;
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

bool same_physical_triangles(const TriangleMesh& first, const TriangleMesh& second)
{
    if (first.its.indices.size() != second.its.indices.size())
        return false;
    // STL readback normally retains face order. Only pay for two sorted copies
    // when order differs; changed coordinates/winding must still be rejected.
    for (std::size_t face = 0; face < first.its.indices.size(); ++face)
        for (int corner = 0; corner < 3; ++corner)
            if (first.its.vertices[first.its.indices[face][corner]] != second.its.vertices[second.its.indices[face][corner]])
                return physical_triangles(first) == physical_triangles(second);
    return true;
}

fs::path require_stl(const std::string& file)
{
    const auto path = fs::canonical(file);
    if (boost::algorithm::to_lower_copy(path.extension().string()) != ".stl" || !fs::is_regular_file(path))
        throw std::invalid_argument(L("Stage reports support only STL files in millimeters."));
    return path;
}
} // namespace

bool model_stage_preserves_surface(const TriangleMesh& source, const TriangleMesh& candidate)
{
    if (source.its.indices.empty() || source.its.indices.size() != candidate.its.indices.size())
        return false;
    const auto key = [](const TriangleMesh& mesh, uint32_t id) {
        std::array<std::array<double, 3>, 3> triangle;
        for (int corner = 0; corner < 3; ++corner) {
            const auto& v = mesh.its.vertices[mesh.its.indices[id][corner]];
            for (int axis = 0; axis < 3; ++axis)
                triangle[corner][axis] = std::round(double(v[axis]) * 1e6);
        }
        std::sort(triangle.begin(), triangle.end());
        return triangle;
    };
    const auto order = [&](const TriangleMesh& mesh) {
        std::vector<uint32_t> ids(mesh.its.indices.size());
        std::iota(ids.begin(), ids.end(), 0);
        std::sort(ids.begin(), ids.end(), [&](uint32_t a, uint32_t b) { return key(mesh, a) < key(mesh, b); });
        return ids;
    };
    const auto left = order(source), right = order(candidate);
    for (std::size_t i = 0; i < left.size(); ++i)
        if (key(source, left[i]) != key(candidate, right[i]))
            return false;
    return true;
}

json inspect_model_hole_fill(const TriangleMesh& source, const TriangleMesh& candidate)
{
    using Point = std::array<float, 3>;
    using Face  = std::array<Point, 3>;
    using Edge  = std::array<Point, 2>;
    // Bound only the optional proof, not repair. No coordinates/face IDs leave
    // this function. Source indexing costs ~4.125 bytes per input triangle.
    constexpr std::size_t MAX_ADDED_FACES = 100000;
    const auto            unavailable     = [](const char* reason) { return json{{"status", "unverified"}, {"reason", reason}}; };
    const auto            source_count = source.its.indices.size(), candidate_count = candidate.its.indices.size();
    if (source_count == 0 || source_count > RECONSTRUCTION_MAX_INPUT_FACES || candidate_count <= source_count)
        return unavailable("not_additive");
    if (candidate_count - source_count > MAX_ADDED_FACES)
        return unavailable("patch_limit");
    const auto triangle = [](const TriangleMesh& mesh, std::size_t id) {
        Face face;
        for (int i = 0; i < 3; ++i) {
            const auto& v = mesh.its.vertices[mesh.its.indices[id][i]];
            face[i]       = {v.x(), v.y(), v.z()};
        }
        std::sort(face.begin(), face.end());
        return face;
    };
    std::vector<uint32_t> source_order(source_count);
    std::iota(source_order.begin(), source_order.end(), 0);
    std::sort(source_order.begin(), source_order.end(), [&](uint32_t a, uint32_t b) { return triangle(source, a) < triangle(source, b); });
    for (std::size_t i = 1; i < source_count; ++i)
        if (triangle(source, source_order[i - 1]) == triangle(source, source_order[i]))
            return unavailable("duplicate_source_face");
    std::vector<uint64_t> seen((source_count + 63) / 64, 0);
    std::vector<uint32_t> added;
    added.reserve(candidate_count - source_count);
    for (std::size_t i = 0; i < candidate_count; ++i) {
        const auto face  = triangle(candidate, i);
        const auto found = std::lower_bound(source_order.begin(), source_order.end(), face,
                                            [&](uint32_t id, const Face& value) { return triangle(source, id) < value; });
        if (found == source_order.end() || triangle(source, *found) != face) {
            if (added.size() == MAX_ADDED_FACES)
                return unavailable("patch_limit");
            added.push_back(uint32_t(i));
        } else {
            const uint64_t mask = uint64_t(1) << (*found % 64);
            if (seen[*found / 64] & mask)
                return unavailable("duplicate_candidate_face");
            seen[*found / 64] |= mask;
        }
    }
    if (candidate_count - added.size() != source_count)
        return unavailable("original_surface_changed");

    struct PatchEdge
    {
        Edge     edge;
        uint32_t face;
    };
    std::vector<PatchEdge> edges;
    edges.reserve(3 * added.size());
    for (uint32_t i = 0; i < added.size(); ++i) {
        const auto face = triangle(candidate, added[i]);
        for (int j = 0; j < 3; ++j) {
            const auto ends = std::minmax(face[j], face[(j + 1) % 3]);
            if (ends.first == ends.second)
                return unavailable("degenerate_patch");
            edges.push_back({{ends.first, ends.second}, i});
        }
    }
    std::sort(edges.begin(), edges.end(), [](const PatchEdge& a, const PatchEdge& b) { return a.edge < b.edge; });
    std::vector<uint32_t> parents(added.size());
    std::iota(parents.begin(), parents.end(), 0);
    const auto root = [&](uint32_t id) {
        while (parents[id] != id) {
            parents[id] = parents[parents[id]];
            id          = parents[id];
        }
        return id;
    };
    std::vector<Edge> boundaries;
    for (std::size_t i = 0; i < edges.size();) {
        auto end = i + 1;
        while (end < edges.size() && edges[end].edge == edges[i].edge)
            ++end;
        if (end - i > 2)
            return unavailable("nonmanifold_patch");
        if (end - i == 2)
            parents[root(edges[i].face)] = root(edges[i + 1].face);
        else
            boundaries.push_back(edges[i].edge);
        i = end;
    }
    // Each patch boundary must coincide with an actual open source edge, not
    // an arbitrary interior edge or the boundary of another added surface.
    std::vector<uint32_t> source_incidence(boundaries.size(), 0);
    for (std::size_t i = 0; i < source_count; ++i) {
        const auto face = triangle(source, i);
        for (int j = 0; j < 3; ++j) {
            const auto ends = std::minmax(face[j], face[(j + 1) % 3]);
            const Edge edge{ends.first, ends.second};
            const auto found = std::lower_bound(boundaries.begin(), boundaries.end(), edge);
            if (found != boundaries.end() && *found == edge)
                ++source_incidence[std::size_t(found - boundaries.begin())];
        }
    }
    if (std::any_of(source_incidence.begin(), source_incidence.end(), [](uint32_t n) { return n != 1; }))
        return unavailable("not_source_boundary");

    struct Patch
    {
        uint32_t faces = 0, edges = 0, vertices = 0, boundary_edges = 0;
        Point    minimum = {INFINITY, INFINITY, INFINITY}, maximum = {-INFINITY, -INFINITY, -INFINITY};
        Point    boundary_minimum = minimum, boundary_maximum = maximum;
    };
    std::vector<Patch> patches(added.size());
    const auto         extend = [](Point& minimum, Point& maximum, const Point& point) {
        for (int axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], point[axis]);
            maximum[axis] = std::max(maximum[axis], point[axis]);
        }
    };
    std::vector<std::pair<uint32_t, Point>> vertices, boundary_vertices;
    vertices.reserve(3 * added.size());
    boundary_vertices.reserve(2 * boundaries.size());
    for (uint32_t i = 0; i < added.size(); ++i) {
        const auto id = root(i);
        ++patches[id].faces;
        for (const auto& point : triangle(candidate, added[i])) {
            extend(patches[id].minimum, patches[id].maximum, point);
            vertices.emplace_back(id, point);
        }
    }
    for (std::size_t i = 0; i < edges.size();) {
        auto end = i + 1;
        while (end < edges.size() && edges[end].edge == edges[i].edge)
            ++end;
        const auto id = root(edges[i].face);
        ++patches[id].edges;
        if (end - i == 1) {
            ++patches[id].boundary_edges;
            for (const auto& point : edges[i].edge) {
                extend(patches[id].boundary_minimum, patches[id].boundary_maximum, point);
                boundary_vertices.emplace_back(id, point);
            }
        }
        i = end;
    }
    std::sort(vertices.begin(), vertices.end());
    vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());
    for (const auto& vertex : vertices)
        ++patches[vertex.first].vertices;
    std::sort(boundary_vertices.begin(), boundary_vertices.end());
    for (std::size_t i = 0; i < boundary_vertices.size();) {
        auto end = i + 1;
        while (end < boundary_vertices.size() && boundary_vertices[end] == boundary_vertices[i])
            ++end;
        if (end - i != 2)
            return unavailable("branched_patch_boundary");
        i = end;
    }
    uint32_t patch_count = 0;
    for (const auto& patch : patches) {
        if (patch.faces == 0)
            continue;
        // A disk has Euler characteristic one. A closed added shell or a tube
        // joining separate holes is not a local cap. Full inspection separately
        // checks intersections, orientation and vertex manifoldness.
        if (patch.boundary_edges < 3 || patch.vertices + patch.faces != patch.edges + 1)
            return unavailable("not_disk_patch");
        for (int axis = 0; axis < 3; ++axis)
            if (patch.minimum[axis] < patch.boundary_minimum[axis] || patch.maximum[axis] > patch.boundary_maximum[axis])
                return unavailable("patch_outside_boundary_bounds");
        ++patch_count;
    }
    return {{"status", "verified"},
            {"method", "boundary_disks_preserve_source"},
            {"source_faces", source_count},
            {"preserved_faces", source_count},
            {"added_faces", added.size()},
            {"patches", patch_count},
            {"boundary_edges", boundaries.size()}};
}

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
    if (!same_physical_triangles(reread, inspected_mesh))
        throw std::runtime_error(L("Stage artifact does not match the inspected triangles."));
    const bool basic            = (stage == "check" || stage == "advanced") && inspection.value("inspection_scope", "full") == "basic";
    const auto candidate_bounds = reread.bounding_box();
    json       change           = {{"status", "not_requested"}, {"reason", "app_basic"}};
    if (!basic) {
        const auto reference_components = raw_component_count(context.reference_mesh);
        const auto candidate_components = raw_component_count(reread);
        const bool inspect_union        = stage == "reconstruction" && reference_components > candidate_components &&
                                   inspection.at("mesh_status") == "passed" && inspection.at("check_complete") == true;
        change = compare_model_stage_meshes(context.reference_mesh, reread, inspect_union);
        // Raw referenced geometry includes zero-area tails excluded from distance
        // queries. Do not hide a far-away deleted feature in the bounding-box gate.
        const auto original_bounds     = context.reference_mesh.bounding_box();
        change["bbox_deviation_mm"]    = std::max((original_bounds.min - candidate_bounds.min).cwiseAbs().maxCoeff(),
                                                  (original_bounds.max - candidate_bounds.max).cwiseAbs().maxCoeff());
        change["reference_components"] = reference_components;
        change["candidate_components"] = candidate_components;
        change["components_complete"]  = true;
        change["component_method"]     = "exact_position_welded_edge_connected_all_faces";
        // Exact-coordinate components can join when STL import welds floating-point
        // cracks. Prove all faces remain; never exempt deleted or merged real parts.
        if ((stage == "check" || stage == "advanced") && inspection.at("mesh_status") == "passed" &&
            inspection.at("check_complete") == true && change["reference_components"] != change["candidate_components"])
            change["numerical_cleanup_preserves_surface"] = model_stage_preserves_surface(context.reference_mesh, reread);
        if (stage == "advanced" && inspection.at("mesh_status") == "passed" && inspection.at("check_complete") == true)
            change["hole_fill"] = inspect_model_hole_fill(context.reference_mesh, reread);
    }
    json issues = json::object();
    for (const char* key :
         {"boundary", "nonmanifold_edge", "nonmanifold_vertex", "self_intersection", "degenerate", "duplicate", "orientation"}) {
        const auto& issue = inspection.at("issues").at(key);
        issues[key]       = {{"count", issue.at("count")}, {"complete", issue.at("complete")}};
        if (basic)
            for (const char* field :
                 {"status", "method", "count_is_lower_bound", "geometric_count", "same_winding_extra_faces", "opposite_winding_groups"})
                if (issue.contains(field))
                    issues[key][field] = issue.at(field);
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
                   {"geometry", {{"max_extent_mm", candidate_bounds.size().maxCoeff()}}},
                   {"reconstruction", reconstruction}};
    if (basic) {
        result["inspection"]["scope"]                  = "basic";
        result["inspection"]["next_action"]            = inspection.at("next_action");
        result["inspection"]["topology_statistics_ms"] = inspection.at("topology_statistics_ms");
    }
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
