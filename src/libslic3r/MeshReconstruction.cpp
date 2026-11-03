#include "MeshReconstruction.hpp"
#include "I18N.hpp"
#include "ModelInspection.hpp"
#include "TriangleMesh.hpp"
#undef PI

#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_triangle_primitive_3.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <CGAL/Polygon_mesh_processing/measure.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Side_of_triangle_mesh.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/boost/graph/helpers.h>
#include <boost/container_hash/hash.hpp>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Slic3r {
namespace {
namespace PMP   = CGAL::Polygon_mesh_processing;
using Kernel    = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point     = Kernel::Point_3;
using Face      = std::array<std::size_t, 3>;
using Surface   = CGAL::Surface_mesh<Point>;
using Triangle  = Kernel::Triangle_3;
using Primitive = CGAL::AABB_triangle_primitive_3<Kernel, std::vector<Triangle>::const_iterator>;
using Tree      = CGAL::AABB_tree<CGAL::AABB_traits_3<Kernel, Primitive>>;

struct Rejection : std::runtime_error
{
    std::string status;
    Rejection(const char* code, const char* message) : std::runtime_error(message), status(code) {}
};

void require(bool condition, const char* code, const char* message)
{
    if (!condition)
        throw Rejection(code, message);
}

struct Soup
{
    std::vector<Point> points;
    std::vector<Face>  faces;
    std::size_t        removed_degenerate = 0;
    std::size_t        removed_duplicates = 0;
    Vec3d              minimum            = Vec3d::Constant(std::numeric_limits<double>::infinity());
    Vec3d              maximum            = Vec3d::Constant(-std::numeric_limits<double>::infinity());
};

Soup read_soup(const TriangleMesh& source, std::size_t face_limit, bool clean_input = false)
{
    require(!source.its.indices.empty() && !source.its.vertices.empty(), "invalid_input", L("The mesh is empty."));
    require(source.its.indices.size() <= face_limit, "resource_limit", L("The triangle limit was exceeded."));
    require(source.its.vertices.size() <= face_limit * 3 && source.its.vertices.size() <= std::size_t(INT_MAX), "resource_limit",
            L("The vertex limit was exceeded."));
    for (const auto& vertex : source.its.vertices)
        require(vertex.allFinite(), "invalid_input", L("The mesh contains a non-finite coordinate."));
    Soup soup;
    // Canonical IDs are assigned on first encounter, never by table iteration.
    // Exact equality and cleaned face order are identical to the ordered tables.
    using Vertex = std::array<double, 3>;
    std::unordered_map<Vertex, std::size_t, boost::hash<Vertex>> canonical;
    canonical.reserve(std::min(source.its.vertices.size(), source.its.indices.size() * 3));
    soup.faces.reserve(source.its.indices.size());
    std::unordered_set<Face, boost::hash<Face>> unique_faces;
    if (clean_input)
        unique_faces.reserve(source.its.indices.size());
    // Shared vertices appear in several faces. Canonicalize each source index
    // once, while retaining first-face encounter order and exact-position welds.
    std::vector<std::size_t> vertex_ids(source.its.vertices.size(), std::numeric_limits<std::size_t>::max());
    for (const auto& face : source.its.indices) {
        Face ids;
        for (int corner = 0; corner != 3; ++corner) {
            require(face[corner] >= 0 && std::size_t(face[corner]) < source.its.vertices.size(), "invalid_input",
                    L("The mesh contains an invalid triangle index."));
            auto& index = vertex_ids[face[corner]];
            if (index == std::numeric_limits<std::size_t>::max()) {
                const Vec3d v        = source.its.vertices[face[corner]].cast<double>();
                const auto  inserted = canonical.emplace(std::array<double, 3>{v.x(), v.y(), v.z()}, soup.points.size());
                if (inserted.second)
                    soup.points.emplace_back(v.x(), v.y(), v.z());
                index = inserted.first->second;
            }
            ids[corner] = index;
        }
        const bool degenerate = CGAL::collinear(soup.points[ids[0]], soup.points[ids[1]], soup.points[ids[2]]);
        if (clean_input && degenerate) {
            ++soup.removed_degenerate;
            continue;
        }
        require(!degenerate, "unsupported_degenerate", L("The float mesh contains a degenerate triangle."));
        Face key = ids;
        std::sort(key.begin(), key.end());
        if (clean_input && !unique_faces.insert(key).second) {
            ++soup.removed_duplicates;
            continue;
        }
        soup.faces.push_back(ids);
    }
    require(!soup.faces.empty(), "invalid_input", L("No non-degenerate triangles remain for reconstruction."));
    // Exclude vertices referenced only by removed zero-area/duplicate faces.
    std::vector<Point>       used_points;
    std::vector<std::size_t> remap(soup.points.size(), std::numeric_limits<std::size_t>::max());
    for (auto& face : soup.faces)
        for (auto& index : face) {
            if (remap[index] == std::numeric_limits<std::size_t>::max()) {
                remap[index]  = used_points.size();
                const auto& p = soup.points[index];
                used_points.push_back(p);
                const Vec3d v(p.x(), p.y(), p.z());
                soup.minimum = soup.minimum.cwiseMin(v);
                soup.maximum = soup.maximum.cwiseMax(v);
            }
            index = remap[index];
        }
    soup.points = std::move(used_points);
    return soup;
}

Surface to_surface(const Soup& soup)
{
    Surface surface;
    for (const auto& p : soup.points)
        surface.add_vertex(p);
    for (const auto& f : soup.faces)
        require(surface.add_face(Surface::Vertex_index(f[0]), Surface::Vertex_index(f[1]), Surface::Vertex_index(f[2])) !=
                    Surface::null_face(),
                "unsupported_topology", L("The triangle soup has duplicate, non-manifold, or inconsistently oriented faces."));
    require(CGAL::is_valid_polygon_mesh(surface) && CGAL::is_triangle_mesh(surface), "unsupported_topology",
            L("The input does not define a valid surface graph."));
    std::vector<Surface::Halfedge_index> singular;
    PMP::non_manifold_vertices(surface, std::back_inserter(singular));
    require(singular.empty(), "unsupported_topology", L("The reconstructed surface contains non-manifold vertices."));
    return surface;
}

// Compare the exact physical surfaces already prepared for the stage distance
// report. Sort only indices; no second triangle soup or spatial index is built.
bool same_surface(const Soup& source, const Soup& candidate)
{
    if (source.faces.size() != candidate.faces.size())
        return false;
    const auto key = [](const Soup& soup, uint32_t index) {
        std::array<std::array<double, 3>, 3> triangle;
        for (int i = 0; i < 3; ++i) {
            const auto& p = soup.points[soup.faces[index][i]];
            triangle[i]   = {p.x(), p.y(), p.z()};
        }
        std::sort(triangle.begin(), triangle.end());
        return triangle;
    };
    const auto ordered = [&](const Soup& soup) {
        std::vector<uint32_t> indices(soup.faces.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), [&](uint32_t a, uint32_t b) { return key(soup, a) < key(soup, b); });
        return indices;
    };
    const auto left = ordered(source), right = ordered(candidate);
    for (std::size_t i = 0; i < left.size(); ++i)
        if (key(source, left[i]) != key(candidate, right[i]))
            return false;
    return true;
}

std::size_t component_count(Surface& surface)
{
    auto       components = surface.add_property_map<Surface::Face_index, std::size_t>("f:reconstruction_component", 0).first;
    const auto count      = PMP::connected_components(surface, components);
    surface.remove_property_map(components);
    return count;
}

void validate_output_surface(Surface& surface)
{
    require(CGAL::is_closed(surface), "candidate_rejected", L("The float mesh is not closed."));
    require(!PMP::does_self_intersect(surface), "candidate_rejected", L("The float mesh self-intersects."));
    // Valid handles and disconnected solids are allowed. The volume predicate
    // validates the orientation of nested boundaries, not just one arbitrary face.
    require(PMP::is_outward_oriented(surface) && PMP::does_bound_a_volume(surface), "candidate_rejected",
            L("The float mesh does not bound an outward-oriented solid."));
    const double volume = double(PMP::volume(surface));
    require(std::isfinite(volume) && volume > 0, "candidate_rejected", L("The float mesh has no finite positive solid volume."));
}

std::vector<Triangle> triangles(const Soup& soup)
{
    std::vector<Triangle> result;
    result.reserve(soup.faces.size());
    for (const auto& f : soup.faces)
        result.emplace_back(soup.points[f[0]], soup.points[f[1]], soup.points[f[2]]);
    return result;
}

double sampled_distance(const Soup& from, const Soup& to, std::size_t& samples)
{
    const auto target = triangles(to);
    Tree       tree(target.begin(), target.end());
    tree.accelerate_distance_queries();
    double     maximum_squared = 0;
    const auto sample          = [&](const Point& p) {
        maximum_squared = std::max(maximum_squared, double(tree.squared_distance(p)));
        ++samples;
    };
    // Deterministic, bounded sampling is an estimate, never a Hausdorff proof.
    const std::size_t point_stride = 1 + (from.points.size() - 1) / 100000;
    for (std::size_t i = 0; i < from.points.size(); i += point_stride)
        sample(from.points[i]);
    const std::size_t face_stride = 1 + (from.faces.size() - 1) / 20000;
    for (std::size_t i = 0; i < from.faces.size(); i += face_stride) {
        const auto& f = from.faces[i];
        const auto& a = from.points[f[0]];
        const auto& b = from.points[f[1]];
        const auto& c = from.points[f[2]];
        sample(CGAL::midpoint(a, b));
        sample(CGAL::midpoint(b, c));
        sample(CGAL::midpoint(c, a));
        sample(CGAL::centroid(a, b, c));
    }
    return std::sqrt(maximum_squared);
}

nlohmann::json comparison_metrics(const Soup& source, const Soup& output, double warning_mm)
{
    const double bbox            = std::max((output.minimum - source.minimum).cwiseAbs().maxCoeff(),
                                            (output.maximum - source.maximum).cwiseAbs().maxCoeff());
    std::size_t  forward_samples = 0, reverse_samples = 0;
    const double forward = sampled_distance(source, output, forward_samples);
    const double reverse = sampled_distance(output, source, reverse_samples);
    require(std::isfinite(forward) && std::isfinite(reverse) && std::isfinite(bbox), "candidate_rejected",
            L("The reconstructed surface comparison is not finite."));
    return {{"bbox_deviation_mm", bbox},
            {"sampled_source_to_candidate_mm", forward},
            {"sampled_candidate_to_source_mm", reverse},
            {"source_samples", forward_samples},
            {"candidate_samples", reverse_samples},
            {"deviation_warning_mm", warning_mm},
            {"deviation_warning", std::max({forward, reverse, bbox}) > warning_mm}};
}

// A union can remove internal interfaces without losing an exterior surface.
// Classify against the ORIGINAL closed solids, never against the candidate.
// This is bounded sampled evidence, not a Hausdorff or design-intent proof.
nlohmann::json material_union_comparison(const Soup& source, const Soup& output)
{
    nlohmann::json result = {{"status", "not_applicable"}, {"method", "closed_solid_overlap_samples"}};
    // Extra trees and point-location queries are only for explicit stage reports.
    if (source.faces.size() > 50000) {
        result["reason"] = "source_face_budget";
        return result;
    }
    std::vector<std::size_t> parents(source.faces.size());
    std::iota(parents.begin(), parents.end(), 0);
    const auto root = [&](std::size_t id) {
        while (parents[id] != id) {
            parents[id] = parents[parents[id]];
            id          = parents[id];
        }
        return id;
    };
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> edges;
    for (std::size_t i = 0; i < source.faces.size(); ++i)
        for (int corner = 0; corner < 3; ++corner) {
            auto a = source.faces[i][corner], b = source.faces[i][(corner + 1) % 3];
            if (a > b)
                std::swap(a, b);
            auto entry = edges.emplace(std::make_pair(a, b), i);
            if (!entry.second)
                parents[root(i)] = root(entry.first->second);
        }
    std::map<std::size_t, std::size_t> components;
    std::vector<std::size_t> owner(source.faces.size());
    for (std::size_t i = 0; i < source.faces.size(); ++i)
        owner[i] = components.emplace(root(i), components.size()).first->second;
    const auto count             = components.size();
    result["source_components"] = count;
    if (count < 2 || count > 16) {
        result["reason"] = "source_component_budget";
        return result;
    }
    std::vector<Surface> solids(count);
    std::vector<std::map<std::size_t, Surface::Vertex_index>> vertices(count);
    for (std::size_t i = 0; i < source.faces.size(); ++i) {
        const auto id = owner[i];
        std::array<Surface::Vertex_index, 3> face;
        for (int c = 0; c < 3; ++c) {
            const auto index = source.faces[i][c];
            auto entry = vertices[id].emplace(index, Surface::null_vertex());
            if (entry.second)
                entry.first->second = solids[id].add_vertex(source.points[index]);
            face[c] = entry.first->second;
        }
        if (solids[id].add_face(face[0], face[1], face[2]) == Surface::null_face()) {
            result["reason"] = "source_not_solid";
            return result;
        }
    }
    vertices.clear();
    edges.clear();
    using Side = CGAL::Side_of_triangle_mesh<Surface, Kernel>;
    std::vector<std::unique_ptr<Side::AABB_tree>> solid_trees;
    std::vector<std::unique_ptr<Side>> sides;
    for (auto& solid : solids) {
        std::vector<Surface::Halfedge_index> singular;
        PMP::non_manifold_vertices(solid, std::back_inserter(singular));
        // A negative shell can represent a cavity. Never reinterpret it as a
        // positive solid, nor use open/self-intersecting shells for containment.
        if (!singular.empty() || !CGAL::is_closed(solid) || PMP::does_self_intersect(solid) ||
            !PMP::is_outward_oriented(solid) || !(PMP::volume(solid) > 0)) {
            result["reason"] = "source_not_solid";
            return result;
        }
        solid_trees.emplace_back(std::make_unique<Side::AABB_tree>(solid.faces().begin(), solid.faces().end(), solid));
        solid_trees.back()->build();
        sides.emplace_back(std::make_unique<Side>(*solid_trees.back()));
    }
    // Imported float coordinates can leave nanometre-scale seams between otherwise
    // opposing planes. Only these flat interfaces allow 1e-5 mm numeric noise;
    // the exterior-surface budget and distinct-part protection are unchanged.
    // Require ALL incident faces on both shells to share that plane: creases,
    // hole rims and genuine crossing edges remain exterior samples.
    constexpr double INTERFACE_TOLERANCE_MM = 1e-5;
    const auto flat_interface = [&](const Point& p, const Triangle& reference, std::size_t owner_id, std::size_t other) {
        const auto normal = CGAL::cross_product(reference[1] - reference[0], reference[2] - reference[0]);
        for (const auto id : {other, owner_id}) {
            std::vector<Surface::Face_index> incident;
            const Kernel::Iso_cuboid_3 vicinity(p.x() - INTERFACE_TOLERANCE_MM, p.y() - INTERFACE_TOLERANCE_MM,
                                               p.z() - INTERFACE_TOLERANCE_MM, p.x() + INTERFACE_TOLERANCE_MM,
                                               p.y() + INTERFACE_TOLERANCE_MM, p.z() + INTERFACE_TOLERANCE_MM);
            solid_trees[id]->all_intersected_primitives(vicinity, std::back_inserter(incident));
            if (incident.empty())
                return false;
            for (const auto face : incident) {
                const auto& mesh = solids[id];
                const auto h = mesh.halfedge(face);
                const auto& a = mesh.point(mesh.source(h));
                const auto& b = mesh.point(mesh.target(h));
                const auto& c = mesh.point(mesh.target(mesh.next(h)));
                for (const auto& v : {a, b, c})
                    if (CGAL::squared_distance(v, reference.supporting_plane()) > INTERFACE_TOLERANCE_MM * INTERFACE_TOLERANCE_MM)
                        return false;
                const double alignment = CGAL::cross_product(b - a, c - a) * normal;
                if (id == other ? alignment >= 0 : alignment <= 0)
                    return false;
            }
        }
        return true;
    };
    const auto target = triangles(output);
    Tree       tree(target.begin(), target.end());
    tree.accelerate_distance_queries();
    struct Sample
    {
        std::size_t owner;
        uint32_t    inside;
        double      distance_squared;
    };
    std::vector<Sample> samples;
    samples.reserve(source.faces.size() * 7);
    std::vector<uint32_t> overlaps(count, 0);
    const auto sample = [&](const Point& p, std::size_t id, const Triangle& reference) {
        uint32_t inside = 0, interface = 0;
        for (std::size_t other = 0; other < count; ++other) {
            if (other == id)
                continue;
            const auto side = (*sides[other])(p);
            if (side == CGAL::ON_BOUNDED_SIDE)
                inside |= uint32_t(1) << other;
            else if (solid_trees[other]->squared_distance(p) <= INTERFACE_TOLERANCE_MM * INTERFACE_TOLERANCE_MM &&
                     flat_interface(p, reference, id, other))
                interface |= uint32_t(1) << other;
        }
        overlaps[id] |= inside;
        samples.push_back({id, inside | interface, double(tree.squared_distance(p))});
    };
    for (std::size_t i = 0; i < source.faces.size(); ++i) {
        const auto& face = source.faces[i];
        const auto& a = source.points[face[0]];
        const auto& b = source.points[face[1]];
        const auto& c = source.points[face[2]];
        for (const auto& p : {a, b, c, CGAL::midpoint(a, b), CGAL::midpoint(b, c), CGAL::midpoint(c, a), CGAL::centroid(a, b, c)})
            sample(p, owner[i], Triangle(a, b, c));
    }
    // Only mutually penetrating shells may merge. Mere containment, boundary
    // contact and separated solids keep their original surface/count checks.
    parents.resize(count);
    std::iota(parents.begin(), parents.end(), 0);
    std::vector<uint32_t> mutual(count, 0);
    std::size_t expected = count;
    for (std::size_t a = 0; a < count; ++a)
        for (std::size_t b = a + 1; b < count; ++b)
            if ((overlaps[a] & (uint32_t(1) << b)) && (overlaps[b] & (uint32_t(1) << a))) {
                mutual[a] |= uint32_t(1) << b;
                mutual[b] |= uint32_t(1) << a;
                if (root(a) != root(b)) {
                    parents[root(a)] = root(b);
                    --expected;
                }
            }
    std::vector<std::size_t> exterior(count, 0);
    std::size_t internal        = 0;
    double      maximum_squared = 0;
    for (const auto& item : samples) {
        if (item.inside & mutual[item.owner]) {
            ++internal;
        } else {
            ++exterior[item.owner];
            maximum_squared = std::max(maximum_squared, item.distance_squared);
        }
    }
    if (expected == count || std::find(exterior.begin(), exterior.end(), 0) != exterior.end()) {
        result["reason"] = "overlap_not_established";
        return result;
    }
    result["status"] = "completed";
    result["expected_components"] = expected;
    result["interface_tolerance_mm"] = INTERFACE_TOLERANCE_MM;
    result["source_samples"] = samples.size();
    result["interior_samples"] = internal;
    result["exterior_samples"] = samples.size() - internal;
    result["sampled_exterior_to_candidate_mm"] = std::sqrt(maximum_squared);
    return result;
}

} // namespace

bool validate_reconstruction_mesh(const TriangleMesh& mesh, nlohmann::json& report, std::size_t face_limit)
{
    report = {{"status", "candidate_rejected"},
              {"message", L("Final float mesh validation did not complete.")},
              {"input", {{"faces", mesh.its.indices.size()}, {"vertices", mesh.its.vertices.size()}}},
              {"output", nlohmann::json::object()},
              {"validation", {{"complete", false}, {"scope", "float_surface_and_model_checks"}, {"design_intent_restored", false}}},
              {"metrics", nlohmann::json::object()}};
    try {
        require(face_limit >= 4 && face_limit <= std::numeric_limits<std::size_t>::max() / 3, "invalid_options",
                L("The final mesh face limit is invalid."));
        const Soup soup    = read_soup(mesh, face_limit);
        Surface    surface = to_surface(soup);
        validate_output_surface(surface);
        report["metrics"]["signed_volume"]             = double(PMP::volume(surface));
        report["validation"]["closed"]                 = true;
        report["validation"]["manifold"]               = true;
        report["validation"]["outward_oriented"]       = true;
        report["validation"]["self_intersection_free"] = true;
        report["metrics"]["component_count"]           = component_count(surface);
        ModelInspectionOptions inspection_options;
        inspection_options.max_triangles = face_limit;
        inspection_options.max_vertices  = face_limit * 3;
        inspection_options.detail_limit  = 16;
        // Inspect the exact returned mesh. Do not reimport through STL auto-repair.
        report["inspection"] = inspect_model_mesh(mesh, inspection_options);
        require(report["inspection"].at("check_complete").get<bool>() && report["inspection"].at("mesh_status") == "passed",
                "candidate_rejected", L("The reconstructed mesh did not pass all complete model checks."));
        report["validation"]["complete"] = true;
        report["status"]                 = "validated_mesh";
        report["message"] = L("The final float surface passed geometric validation; restoration of design intent is not certified.");
        return true;
    } catch (const Rejection& error) {
        report["reason"] = error.status;
        if (error.status == "resource_limit" || error.status == "invalid_options")
            report["status"] = error.status;
        report["message"] = error.what();
    } catch (const std::exception& error) {
        report["status"]  = "engine_error";
        report["message"] = error.what();
    } catch (...) {
        report["status"]  = "engine_error";
        report["message"] = L("Final float mesh validation failed with an unknown exception.");
    }
    return false;
}

nlohmann::json compare_reconstruction_meshes(const TriangleMesh&              source_mm,
                                             const TriangleMesh&              candidate_mm,
                                             const MeshReconstructionOptions& options)
{
    require(std::isfinite(options.max_deviation_mm) && options.max_deviation_mm > 0, "invalid_options",
            L("Reconstruction options require finite positive numbers."));
    const auto source                               = read_soup(source_mm, options.max_input_faces, true);
    const auto output                               = read_soup(candidate_mm, options.max_output_faces);
    auto       metrics                              = comparison_metrics(source, output, options.max_deviation_mm);
    metrics["distance_method"]                      = "sampled_vertices_edge_midpoints_and_face_centroids";
    metrics["distance_is_upper_bound"]              = false;
    metrics["deviation_policy"]                     = "warning_only";
    metrics["comparison_excluded_degenerate_faces"] = source.removed_degenerate;
    metrics["comparison_excluded_duplicate_faces"]  = source.removed_duplicates;
    return metrics;
}

nlohmann::json compare_model_stage_meshes(const TriangleMesh& source_mm, const TriangleMesh& candidate_mm, bool inspect_material_union)
{
    const auto source  = read_soup(source_mm, RECONSTRUCTION_MAX_INPUT_FACES, true);
    const auto output  = read_soup(candidate_mm, RECONSTRUCTION_MAX_INPUT_FACES, true);
    auto       metrics = comparison_metrics(source, output, 0.25);
    metrics.erase("deviation_warning_mm");
    metrics.erase("deviation_warning");
    metrics["status"]                              = "completed";
    metrics["distance_method"]                     = "sampled_vertices_edge_midpoints_and_face_centroids";
    metrics["distance_is_upper_bound"]             = false;
    metrics["reference_excluded_degenerate_faces"] = source.removed_degenerate;
    metrics["reference_excluded_duplicate_faces"]  = source.removed_duplicates;
    metrics["candidate_excluded_degenerate_faces"] = output.removed_degenerate;
    metrics["candidate_excluded_duplicate_faces"]  = output.removed_duplicates;
    // A zero-area tail can change raw bounds/component counts without changing
    // any actual surface. Exact CGAL collinearity is used, never a thinness or
    // area tolerance. This evidence does not replace final mesh inspection.
    if (source.removed_degenerate > 0)
        metrics["degenerate_cleanup_preserves_surface"] = output.removed_degenerate == 0 && output.removed_duplicates == 0 &&
                                                          same_surface(source, output);
    if (inspect_material_union)
        metrics["material_union"] = material_union_comparison(source, output);
    return metrics;
}

} // namespace Slic3r
