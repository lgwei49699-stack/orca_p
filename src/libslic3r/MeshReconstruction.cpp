#include "MeshReconstruction.hpp"
#include "I18N.hpp"
#include "ModelInspection.hpp"
#include "TriangleMesh.hpp"
#undef PI

#include <CGAL/AABB_traits.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_triangle_primitive.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Polygon_mesh_processing/border.h>
#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <CGAL/Polygon_mesh_processing/measure.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/alpha_wrap_3.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/version.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
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
using Primitive = CGAL::AABB_triangle_primitive<Kernel, std::vector<Triangle>::const_iterator>;
using Tree      = CGAL::AABB_tree<CGAL::AABB_traits<Kernel, Primitive>>;

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
    Soup                                         soup;
    std::map<std::array<double, 3>, std::size_t> canonical;
    soup.faces.reserve(source.its.indices.size());
    std::set<Face> unique_faces;
    for (const auto& face : source.its.indices) {
        Face ids;
        for (int corner = 0; corner != 3; ++corner) {
            require(face[corner] >= 0 && std::size_t(face[corner]) < source.its.vertices.size(), "invalid_input",
                    L("The mesh contains an invalid triangle index."));
            const Vec3d v        = source.its.vertices[face[corner]].cast<double>();
            const auto  inserted = canonical.emplace(std::array<double, 3>{v.x(), v.y(), v.z()}, soup.points.size());
            if (inserted.second) {
                soup.points.emplace_back(v.x(), v.y(), v.z());
            }
            ids[corner] = inserted.first->second;
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

// CGAL 5.6.3's visitor is copied by value. Bounds supplement, but do not replace,
// the worker's hard memory/time limits. They also bound intermediate refinement.
struct BudgetVisitor
{
    std::size_t                  max_vertices;
    std::size_t                  max_cells;
    template<class Wrapper> void check(const Wrapper& wrapper) const
    {
        require(wrapper.triangulation().number_of_vertices() <= max_vertices && wrapper.triangulation().number_of_cells() <= max_cells,
                "resource_limit", L("Alpha wrapping exceeded the intermediate refinement budget."));
    }
    template<class Wrapper> void               on_alpha_wrapping_begin(const Wrapper& wrapper) { check(wrapper); }
    template<class Wrapper> void               on_flood_fill_begin(const Wrapper& wrapper) { check(wrapper); }
    template<class Wrapper, class Gate> void   before_facet_treatment(const Wrapper& wrapper, const Gate&) { check(wrapper); }
    template<class Wrapper, class P> void      before_Steiner_point_insertion(const Wrapper& wrapper, const P&) { check(wrapper); }
    template<class Wrapper, class Vertex> void after_Steiner_point_insertion(const Wrapper& wrapper, const Vertex&) { check(wrapper); }
    template<class Wrapper> void               on_flood_fill_end(const Wrapper& wrapper) { check(wrapper); }
    template<class Wrapper> void               on_alpha_wrapping_end(const Wrapper& wrapper) { check(wrapper); }
};

TriangleMesh to_triangle_mesh(const Surface& surface, std::size_t face_limit)
{
    require(!surface.is_empty(), "candidate_rejected", L("The reconstruction produced no surface."));
    require(surface.number_of_faces() <= face_limit && surface.number_of_vertices() <= std::size_t(INT_MAX), "resource_limit",
            L("The reconstructed surface exceeds the output limit."));
    indexed_triangle_set                 result;
    std::map<Surface::Vertex_index, int> ids;
    result.vertices.reserve(surface.number_of_vertices());
    result.indices.reserve(surface.number_of_faces());
    for (const auto v : surface.vertices()) {
        const Point& p = surface.point(v);
        Vec3f        converted(float(p.x()), float(p.y()), float(p.z()));
        require(converted.allFinite(), "candidate_rejected", L("A reconstructed coordinate cannot be represented safely."));
        ids.emplace(v, int(result.vertices.size()));
        result.vertices.push_back(converted);
    }
    for (const auto f : surface.faces()) {
        const auto h = surface.halfedge(f);
        result.indices.emplace_back(ids.at(surface.target(h)), ids.at(surface.target(surface.next(h))), ids.at(surface.source(h)));
    }
    return TriangleMesh(std::move(result));
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

// A wrap of an open surface can be a paper-thin envelope instead of the intended
// solid. Prepare a private, fully validated closed input when requested. Never
// treat a failed patch or a checker exception as successful closure.
std::optional<Soup> prepare_closed_input(const Soup& source, const MeshReconstructionOptions& options, nlohmann::json& report)
{
    constexpr std::size_t                                   MAX_BOUNDARY_EDGES = 2048;
    constexpr std::size_t                                   MAX_LOOP_EDGES     = 512;
    constexpr std::size_t                                   MAX_LOOPS          = 64;
    std::map<std::pair<std::size_t, std::size_t>, unsigned> edge_uses;
    for (const auto& face : source.faces)
        for (int i = 0; i < 3; ++i)
            ++edge_uses[std::minmax(face[i], face[(i + 1) % 3])];
    const std::size_t boundary_edges = std::count_if(edge_uses.begin(), edge_uses.end(), [](const auto& edge) { return edge.second == 1; });
    report                           = {{"requested", options.close_open_boundaries},
                                        {"input_boundary_edges", boundary_edges},
                                        {"status", boundary_edges == 0 ? "no_open_edges" : "disabled"},
                                        {"used_for_wrapping", false},
                                        {"closed_loops", 0},
                                        {"added_faces", 0}};
    if (boundary_edges == 0 || !options.close_open_boundaries)
        return std::nullopt;
    try {
        require(boundary_edges <= MAX_BOUNDARY_EDGES, "closure_budget", L("Open boundary preparation exceeded its patch budget."));
        Surface surface = to_surface(source);
        require(!PMP::does_self_intersect(surface), "closure_intersection",
                L("Intersecting open surfaces cannot be closed by the boundary preparation step."));
        std::vector<Surface::Halfedge_index> borders;
        PMP::extract_boundary_cycles(surface, std::back_inserter(borders));
        require(!borders.empty() && borders.size() <= MAX_LOOPS, "closure_budget",
                L("Open boundary preparation exceeded its patch budget."));
        // Check every loop before attempting any patch, keeping work bounded.
        for (const auto border : borders) {
            auto        edge  = border;
            std::size_t count = 0;
            do {
                require(++count <= MAX_LOOP_EDGES, "closure_budget", L("Open boundary preparation exceeded its patch budget."));
                edge = surface.next(edge);
            } while (edge != border);
        }
        const std::size_t face_limit = std::min(options.max_output_faces, source.faces.size() + MAX_BOUNDARY_EDGES);
        for (const auto border : borders) {
            std::vector<Surface::Face_index> patch;
            // Use the iterator overload supported by the pinned CGAL on all
            // platforms; do not enter the unbounded cubic fallback search.
            PMP::triangulate_hole(surface, border, std::back_inserter(patch),
                                  CGAL::parameters::use_delaunay_triangulation(true).do_not_use_cubic_algorithm(true));
            require(!patch.empty() && surface.number_of_faces() <= face_limit, "closure_patch_failed",
                    L("An open boundary could not be closed within the patch budget."));
        }
        // Rebuild from the actual float representation before solid orientation.
        // This rejects degenerate/duplicate/non-manifold caps and flat sheets.
        auto closed = to_surface(read_soup(to_triangle_mesh(surface, face_limit), face_limit));
        require(CGAL::is_closed(closed) && !PMP::does_self_intersect(closed), "closure_invalid",
                L("The closed input did not form a valid non-intersecting solid."));
        const double enclosed_volume = double(PMP::volume(closed));
        require(std::isfinite(enclosed_volume) && std::abs(enclosed_volume) > 0, "closure_invalid",
                L("The closed input did not form a valid non-intersecting solid."));
        PMP::orient_to_bound_a_volume(closed);
        const auto     mesh = to_triangle_mesh(closed, face_limit);
        nlohmann::json validation;
        if (!validate_reconstruction_mesh(mesh, validation, face_limit)) {
            report["validation"] = validation;
            if (validation.value("status", "") == "engine_error")
                throw std::runtime_error(validation.at("message").get<std::string>());
            throw Rejection("closure_invalid", L("The closed input did not form a valid non-intersecting solid."));
        }
        report["status"]            = "closed_input_ready";
        report["used_for_wrapping"] = true;
        report["closed_loops"]      = borders.size();
        report["added_faces"]       = mesh.its.indices.size() - source.faces.size();
        report["validation"]        = std::move(validation);
        return read_soup(mesh, face_limit);
    } catch (const Rejection& error) {
        // Only a controlled geometry/budget refusal falls back to the original
        // soup. Allocation errors or unexpected engine failures stop the worker.
        report["status"]  = error.status;
        report["message"] = error.what();
        return std::nullopt;
    }
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

bool reconstruct_mesh(const TriangleMesh&              source_mm,
                      TriangleMesh&                    candidate_mm,
                      nlohmann::json&                  report,
                      const MeshReconstructionOptions& options)
{
    report = {{"status", "failed"},
              {"message", L("Reconstruction did not complete.")},
              {"input", {{"faces", source_mm.its.indices.size()}, {"vertices", source_mm.its.vertices.size()}, {"units", "mm"}}},
              {"output", nlohmann::json::object()},
              {"attempts", nlohmann::json::array()},
              {"validation",
               {{"complete", false},
                {"geometry_complete", false},
                {"requires_manual_confirmation", true},
                {"printability_verified", false},
                {"design_intent_restored", false},
                {"reference_interior_check_complete", false},
                {"interior_check_status", "not_preserved"}}},
              {"metrics",
               {{"distance_method", "sampled_vertices_edge_midpoints_and_face_centroids"},
                {"distance_is_upper_bound", false},
                {"engine", "CGAL AlphaWrap"},
                {"engine_version", CGAL_VERSION_STR},
                {"deviation_policy", "warning_only"}}}};
    try {
        require(&source_mm != &candidate_mm, "invalid_input", L("Source and candidate meshes must be distinct objects."));
        require(std::isfinite(options.alpha_mm) && std::isfinite(options.offset_mm) && std::isfinite(options.max_deviation_mm) &&
                    options.alpha_mm > 0 && options.offset_mm > 0 && options.max_deviation_mm > 0,
                "invalid_options", L("Alpha, offset and deviation warning threshold must be finite and positive."));
        require(options.offset_mm <= options.alpha_mm * 0.25, "invalid_options", L("Offset must not exceed one quarter of alpha."));
        require(options.max_input_faces >= 4 && options.max_output_faces >= 4 && options.max_input_faces <= 200000 &&
                    options.max_output_faces <= 1000000,
                "invalid_options", L("Requested face limits exceed the supported reconstruction budget."));
        const Soup  source = read_soup(source_mm, options.max_input_faces, true);
        const Vec3d extent = source.maximum - source.minimum;
        require(extent.allFinite() && extent.maxCoeff() > 0, "invalid_input", L("The model has no usable spatial extent."));
        report["input"]["canonical_vertices"]       = source.points.size();
        report["input"]["cleaned_faces"]            = source.faces.size();
        report["input"]["removed_degenerate_faces"] = source.removed_degenerate;
        report["input"]["removed_duplicate_faces"]  = source.removed_duplicates;
        auto        closed_input                           = prepare_closed_input(source, options, report["boundary_closure"]);
        const Soup& wrap_input                             = closed_input ? *closed_input : source;
        report["metrics"]["open_surface_envelope_warning"] = report["boundary_closure"]["input_boundary_edges"].get<std::size_t>() > 0 &&
                                                             !closed_input;
        // Failure to prepare a closed input does not exclude defective soups.
        double area = 0;
        for (const auto& face : wrap_input.faces)
            area += std::sqrt(
                double(Triangle(wrap_input.points[face[0]], wrap_input.points[face[1]], wrap_input.points[face[2]]).squared_area()));
        require(std::isfinite(area) && area > 0, "invalid_input", L("The model has no finite positive surface area."));
        // A single worker owns the overall deadline/RSS budget for all attempts.
        // Try the requested resolution first, then two bounded coarser profiles.
        for (const double factor : {1., 2., 4.}) {
            const double   alpha   = options.alpha_mm * factor;
            const double   offset  = options.offset_mm * factor;
            nlohmann::json attempt = {{"alpha_mm", alpha}, {"offset_mm", offset}};
            try {
                require(std::isfinite(alpha) && std::isfinite(offset), "invalid_options", L("Reconstruction scales are not finite."));
                require(area / alpha / alpha <= double(options.max_output_faces), "resource_limit",
                        L("The requested alpha exceeds the estimated surface refinement budget."));
                Surface       wrapped;
                BudgetVisitor visitor{std::max<std::size_t>(64, options.max_output_faces * 2),
                                      std::max<std::size_t>(512, options.max_output_faces * 12)};
                CGAL::alpha_wrap_3(wrap_input.points, wrap_input.faces, alpha, offset, wrapped, CGAL::parameters::visitor(visitor));
                TriangleMesh   staged = to_triangle_mesh(wrapped, options.max_output_faces);
                nlohmann::json validation;
                if (!validate_reconstruction_mesh(staged, validation, options.max_output_faces)) {
                    attempt["validation"] = validation;
                    // A failed checker is not a geometric rejection. Retrying it
                    // with a coarser mesh would conceal the engine failure.
                    if (validation.value("status", "") == "engine_error")
                        throw Rejection("engine_error", validation.at("message").get<std::string>().c_str());
                    require(false, "candidate_rejected", L("The reconstructed mesh did not pass all complete model checks."));
                }
                const Soup output                        = read_soup(staged, options.max_output_faces);
                report["output"]                         = {{"faces", output.faces.size()}, {"vertices", output.points.size()}};
                report["final_mesh_check"]               = std::move(validation);
                report["metrics"]["effective_alpha_mm"]  = alpha;
                report["metrics"]["effective_offset_mm"] = offset;
                report["metrics"]["output_volume_mm3"]   = report["final_mesh_check"]["metrics"]["signed_volume"];
                report["metrics"]["component_count"]     = report["final_mesh_check"]["metrics"]["component_count"];
                report["metrics"]["bbox_deviation_mm"]   = std::max((output.minimum - source.minimum).cwiseAbs().maxCoeff(),
                                                                    (output.maximum - source.maximum).cwiseAbs().maxCoeff());
                std::size_t  forward_samples = 0, reverse_samples = 0;
                const double forward = sampled_distance(source, output, forward_samples);
                const double reverse = sampled_distance(output, source, reverse_samples);
                require(std::isfinite(forward) && std::isfinite(reverse), "candidate_rejected",
                        L("The reconstructed surface comparison is not finite."));
                report["metrics"]["sampled_source_to_candidate_mm"] = forward;
                report["metrics"]["sampled_candidate_to_source_mm"] = reverse;
                report["metrics"]["source_samples"]                 = forward_samples;
                report["metrics"]["candidate_samples"]              = reverse_samples;
                report["metrics"]["deviation_warning_mm"]           = options.max_deviation_mm;
                report["metrics"]["deviation_warning"] = std::max({forward, reverse, report["metrics"]["bbox_deviation_mm"].get<double>()}) >
                                                         options.max_deviation_mm;
                attempt["status"] = "candidate_ready";
                report["attempts"].push_back(std::move(attempt));
                report["validation"]["complete"]               = true;
                report["validation"]["geometry_complete"]      = true;
                report["validation"]["closed"]                 = true;
                report["validation"]["manifold"]               = true;
                report["validation"]["self_intersection_free"] = true;
                report["status"]                               = "candidate_ready";
                report["message"] = L("The reconstructed mesh passed complete model checks. Shape and internal structure may have changed; "
                                      "trial slicing is still required.");
                candidate_mm      = std::move(staged);
                return true;
            } catch (const Rejection& error) {
                attempt["status"]  = error.status;
                attempt["message"] = error.what();
                report["attempts"].push_back(std::move(attempt));
                // Do not retry corrupted inputs, options or engine/monitor failures.
                if (factor == 4. || (error.status != "resource_limit" && error.status != "candidate_rejected"))
                    throw;
            }
        }
    } catch (const Rejection& error) {
        report["status"]  = error.status;
        report["message"] = error.what();
    } catch (const std::exception& error) {
        report["status"]  = "engine_error";
        report["message"] = error.what();
    } catch (...) {
        report["status"]  = "engine_error";
        report["message"] = L("The reconstruction engine failed with an unknown exception.");
    }
    return false;
}
} // namespace Slic3r
