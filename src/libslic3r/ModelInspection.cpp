#include "ModelInspection.hpp"
#include "TriangleMesh.hpp"
#undef PI

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Intersections_3/Triangle_3_Triangle_3.h>
#include <CGAL/Kernel/global_functions_3.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/measure.h>
#include <CGAL/Side_of_triangle_mesh.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/number_utils.h>
#include <boost/variant/get.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace Slic3r {
namespace {
using json                             = nlohmann::json;
using Kernel                           = CGAL::Exact_predicates_exact_constructions_kernel;
using ExactPoint                       = Kernel::Point_3;
using Face                             = std::array<std::size_t, 3>;
using Edge                             = std::array<std::size_t, 2>;
using FacePair                         = std::array<std::size_t, 2>;
using XYZ                              = std::array<double, 3>;
constexpr double SMALL_COMPONENT_RATIO = 0.01;

// Ported from the local mesh-inspector demo, without its parser/server/renderer.
// Keep count semantics and CGAL preconditions aligned; see doc/model-check-workflow.md.
struct Mesh
{
    const ModelInspectionOptions& options;
    std::vector<ExactPoint>       points;
    std::vector<XYZ>              coordinates;
    std::vector<Face>             faces;
    std::map<XYZ, std::size_t>    point_index;

    std::size_t add_point(const XYZ& xyz)
    {
        for (double value : xyz)
            if (!std::isfinite(value))
                throw std::runtime_error("Non-finite model vertex.");
        const auto found = point_index.find(xyz);
        if (found != point_index.end())
            return found->second;
        if (points.size() >= options.max_vertices)
            throw std::runtime_error("Model inspection vertex limit exceeded.");
        const auto id = points.size();
        point_index.emplace(xyz, id);
        coordinates.push_back(xyz);
        points.emplace_back(xyz[0], xyz[1], xyz[2]);
        return id;
    }
};

void validate_mesh(const TriangleMesh& mesh)
{
    for (const auto& vertex : mesh.its.vertices)
        if (!vertex.allFinite())
            throw std::runtime_error("Non-finite model vertex.");
    for (const auto& face : mesh.its.indices)
        for (int i = 0; i < 3; ++i)
            if (face[i] < 0 || std::size_t(face[i]) >= mesh.its.vertices.size())
                throw std::runtime_error("Invalid model triangle index.");
}

struct IntersectionGeometry
{
    std::string      kind;
    std::vector<XYZ> points;
};

IntersectionGeometry intersection_geometry(const Mesh& mesh, const FacePair& pair)
{
    const auto triangle = [&mesh](std::size_t id) {
        const Face& face = mesh.faces.at(id);
        return Kernel::Triangle_3(mesh.points[face[0]], mesh.points[face[1]], mesh.points[face[2]]);
    };
    // Construct only the already detected pairs, not another all-pairs search.
    // Keep exact constructions until conversion to display-only coordinates.
    const auto result = CGAL::intersection(triangle(pair[0]), triangle(pair[1]));
    if (!result)
        throw std::runtime_error("CGAL reported a pair without intersection geometry.");
    IntersectionGeometry    geometry;
    std::vector<ExactPoint> points;
    if (const auto* point = boost::get<ExactPoint>(&*result)) {
        geometry.kind = "point";
        points.push_back(*point);
    } else if (const auto* segment = boost::get<Kernel::Segment_3>(&*result)) {
        geometry.kind = "segment";
        points        = {segment->source(), segment->target()};
    } else if (const auto* area = boost::get<Kernel::Triangle_3>(&*result)) {
        geometry.kind = "area";
        points        = {area->vertex(0), area->vertex(1), area->vertex(2)};
    } else if (const auto* polygon = boost::get<std::vector<ExactPoint>>(&*result)) {
        geometry.kind = "area";
        points        = *polygon; // CGAL supplies cyclic order, not a bag of vertices.
    }
    if (points.empty() || points.size() > 6 || (geometry.kind == "area" && points.size() < 3))
        throw std::runtime_error("Invalid CGAL triangle intersection geometry.");
    for (const ExactPoint& point : points) {
        XYZ xyz{CGAL::to_double(point.x()), CGAL::to_double(point.y()), CGAL::to_double(point.z())};
        for (double value : xyz)
            if (!std::isfinite(value))
                throw std::runtime_error("Intersection coordinates cannot be displayed safely.");
        geometry.points.push_back(xyz);
    }
    return geometry;
}

struct Issue
{
    std::string                       key;
    std::size_t                       count    = 0;
    bool                              complete = true;
    std::vector<std::size_t>          faces;
    std::vector<Edge>                 edges;
    std::vector<std::size_t>          vertices;
    std::vector<FacePair>             pairs;
    std::vector<IntersectionGeometry> pair_geometry;
};

struct Use
{
    std::size_t face;
    bool        forward;
};

std::vector<Issue> inspect(const Mesh& mesh)
{
    std::vector<Issue>                       issues{{"boundary"},   {"nonmanifold_edge"}, {"nonmanifold_vertex"}, {"self_intersection"},
                                                    {"degenerate"}, {"duplicate"},        {"orientation"}};
    std::map<Edge, std::vector<Use>>         edge_uses;
    std::map<Face, std::vector<std::size_t>> same_faces;
    std::vector<std::vector<std::size_t>>    incident(mesh.points.size());
    std::vector<Face>                        valid_faces;
    std::vector<std::size_t>                 valid_ids;
    valid_faces.reserve(mesh.faces.size());
    valid_ids.reserve(mesh.faces.size());
    for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
        const Face& face   = mesh.faces[i];
        Face        sorted = face;
        std::sort(sorted.begin(), sorted.end());
        same_faces[sorted].push_back(i);
        if (CGAL::collinear(mesh.points[face[0]], mesh.points[face[1]], mesh.points[face[2]])) {
            issues[4].faces.push_back(i);
            ++issues[4].count;
            for (std::size_t j = 0; j < 3; ++j) {
                issues[4].edges.push_back({face[j], face[(j + 1) % 3]});
                issues[4].vertices.push_back(face[j]);
            }
            continue;
        }
        valid_faces.push_back(face);
        valid_ids.push_back(i);
        for (std::size_t j = 0; j < 3; ++j) {
            const auto a = face[j], b = face[(j + 1) % 3];
            edge_uses[{std::min(a, b), std::max(a, b)}].push_back({i, a < b});
            incident[a].push_back(i);
        }
    }
    for (const auto& entry : same_faces) {
        if (entry.second.size() < 2)
            continue;
        ++issues[5].count; // Duplicate groups, not all pairs (which grow quadratically).
        issues[5].faces.insert(issues[5].faces.end(), entry.second.begin(), entry.second.end());
    }
    for (const auto& entry : edge_uses) {
        const auto& uses  = entry.second;
        Issue*      issue = nullptr;
        if (uses.size() == 1)
            issue = &issues[0];
        else if (uses.size() > 2)
            issue = &issues[1];
        else if (uses[0].forward == uses[1].forward)
            issue = &issues[6];
        if (issue) {
            ++issue->count;
            issue->edges.push_back(entry.first);
            for (const Use& use : uses)
                issue->faces.push_back(use.face);
        }
    }

    // The link of a manifold vertex is one cycle (interior) or one path
    // (boundary). This checks the unmodified soup, where Surface_mesh could
    // reject faces before we had a chance to diagnose them.
    for (std::size_t vertex = 0; vertex < incident.size(); ++vertex) {
        if (incident[vertex].empty())
            continue;
        std::map<std::size_t, std::vector<std::size_t>> link;
        for (auto face_id : incident[vertex]) {
            const auto& face   = mesh.faces[face_id];
            std::size_t corner = 0;
            while (face[corner] != vertex)
                ++corner;
            const auto a = face[(corner + 1) % 3], b = face[(corner + 2) % 3];
            link[a].push_back(b);
            link[b].push_back(a);
        }
        std::size_t ends = 0;
        bool        bad  = false;
        for (const auto& entry : link) {
            if (entry.second.size() == 1)
                ++ends;
            else if (entry.second.size() != 2)
                bad = true;
        }
        std::set<std::size_t>    visited;
        std::vector<std::size_t> pending{link.begin()->first};
        while (!pending.empty()) {
            const auto next = pending.back();
            pending.pop_back();
            if (!visited.insert(next).second)
                continue;
            for (auto neighbor : link.at(next))
                if (!visited.count(neighbor))
                    pending.push_back(neighbor);
        }
        if (bad || (ends != 0 && ends != 2) || visited.size() != link.size()) {
            ++issues[2].count;
            issues[2].vertices.push_back(vertex);
            issues[2].faces.insert(issues[2].faces.end(), incident[vertex].begin(), incident[vertex].end());
        }
    }

    // Topology diagnostics are complete. Release their large lookup tables
    // before CGAL allocates the self-intersection acceleration structures.
    edge_uses.clear();
    same_faces.clear();
    std::vector<std::vector<std::size_t>>().swap(incident);

    // EPECK uses exact predicates/constructions on the decoded input doubles.
    // No Surface_mesh conversion, orientation, hole filling or repair occurs.

    std::vector<std::pair<std::size_t, std::size_t>> intersections;
    if (!valid_faces.empty())
        CGAL::Polygon_mesh_processing::triangle_soup_self_intersections<CGAL::Sequential_tag>(mesh.points, valid_faces,
                                                                                              std::back_inserter(intersections),
                                                                                              CGAL::parameters::maximum_number(
                                                                                                  mesh.options.max_intersections + 1));
    issues[3].complete = intersections.size() <= mesh.options.max_intersections;
    if (intersections.size() > mesh.options.max_intersections)
        intersections.resize(mesh.options.max_intersections);
    issues[3].count = intersections.size();
    for (const auto& pair : intersections) {
        const auto first = valid_ids.at(pair.first), second = valid_ids.at(pair.second);
        issues[3].faces.push_back(first);
        issues[3].faces.push_back(second);
        // Preserve the CGAL pairing in ORIGINAL triangle IDs, not the compact
        // valid_faces indices (degenerate triangles were excluded above).
        issues[3].pairs.push_back({std::min(first, second), std::max(first, second)});
    }
    std::sort(issues[3].pairs.begin(), issues[3].pairs.end());
    // Construct AFTER sorting so each geometry stays aligned with its pair.

    const auto geometry_count = std::min(issues[3].pairs.size(), mesh.options.detail_limit);
    issues[3].pair_geometry.reserve(geometry_count);
    for (std::size_t i = 0; i < geometry_count; ++i)
        issues[3].pair_geometry.push_back(intersection_geometry(mesh, issues[3].pairs[i]));
    for (auto& issue : issues) {
        std::sort(issue.faces.begin(), issue.faces.end());
        issue.faces.erase(std::unique(issue.faces.begin(), issue.faces.end()), issue.faces.end());
        std::sort(issue.vertices.begin(), issue.vertices.end());
        issue.vertices.erase(std::unique(issue.vertices.begin(), issue.vertices.end()), issue.vertices.end());
    }
    return issues;
}

struct Component
{
    std::vector<std::size_t> faces;
    std::vector<std::size_t> containers;
    std::set<std::string>    blocked;
    XYZ                      min, max;
    double                   area = 0, area_ratio = -1;
    double                   signed_volume = std::numeric_limits<double>::quiet_NaN();
    bool                     closed = true, small = false, orientation_suspect = false;
    std::string              orientation = "not_checked", expected_orientation = "unknown";
};

struct ComponentRelation
{
    std::size_t              a, b;
    std::string              kind;
    std::vector<std::size_t> intersection_pairs;
};

struct ComponentAnalysis
{
    std::vector<Component>         components;
    std::vector<ComponentRelation> relations;
    bool                           containment_complete   = true;
    bool                           intersections_complete = true;
    bool                           size_complete          = true;
    bool                           limit_reached          = false;
};

ComponentAnalysis inspect_components(const Mesh& mesh, const std::vector<Issue>& issues)
{
    ComponentAnalysis        result;
    std::vector<std::size_t> parent(mesh.faces.size()), rank(mesh.faces.size(), 0);
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::size_t id) {
        while (parent[id] != id) {
            parent[id] = parent[parent[id]];
            id         = parent[id];
        }
        return id;
    };
    std::map<Edge, std::vector<std::size_t>> edge_faces;
    for (std::size_t id = 0; id < mesh.faces.size(); ++id) {
        const auto&    face = mesh.faces[id];
        std::set<Edge> unique_edges;
        for (std::size_t j = 0; j < 3; ++j) {
            const auto a = face[j], b = face[(j + 1) % 3];
            if (a != b)
                unique_edges.insert({std::min(a, b), std::max(a, b)});
        }
        for (const auto& edge : unique_edges) {
            auto& uses = edge_faces[edge];
            if (!uses.empty()) {
                auto a = root(uses.front()), b = root(id);
                if (a != b) {
                    if (rank[a] < rank[b])
                        std::swap(a, b);
                    parent[b] = a;
                    if (rank[a] == rank[b])
                        ++rank[a];
                }
            }
            uses.push_back(id);
        }
    }
    std::map<std::size_t, std::size_t> component_ids;
    std::vector<std::size_t>           face_component(mesh.faces.size());
    for (std::size_t id = 0; id < mesh.faces.size(); ++id) {
        const auto leader   = root(id);
        auto       inserted = component_ids.emplace(leader, result.components.size());
        if (inserted.second) {
            Component component;
            component.min = component.max = mesh.coordinates[mesh.faces[id][0]];
            result.components.push_back(std::move(component));
        }
        const auto index   = inserted.first->second;
        face_component[id] = index;
        auto& component    = result.components[index];
        component.faces.push_back(id);
        for (const auto vertex : mesh.faces[id]) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                component.min[axis] = std::min(component.min[axis], mesh.coordinates[vertex][axis]);
                component.max[axis] = std::max(component.max[axis], mesh.coordinates[vertex][axis]);
            }
        }
        const auto&  face    = mesh.faces[id];
        const auto   squared = CGAL::squared_area(mesh.points[face[0]], mesh.points[face[1]], mesh.points[face[2]]);
        const double numeric = CGAL::to_double(squared);
        if (!std::isfinite(numeric) || numeric < 0 || (numeric == 0 && squared != 0))
            component.area = std::numeric_limits<double>::quiet_NaN();
        else
            component.area += std::sqrt(numeric);
    }
    for (const auto& entry : edge_faces) {
        if (entry.second.size() != 2)
            result.components[face_component[entry.second.front()]].closed = false;
    }
    edge_faces.clear(); // Do not retain the second edge index while building CGAL trees.
    // Preserve all original faces, including degenerate/duplicate records. A
    // point-only contact does not merge edge-connected surface components.
    const auto               missing = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> vertex_component(mesh.points.size(), missing);
    for (std::size_t id = 0; id < mesh.faces.size(); ++id) {
        const auto current = face_component[id];
        for (const auto vertex : mesh.faces[id]) {
            auto& previous = vertex_component[vertex];
            if (previous != missing && previous != current) {
                result.components[previous].blocked.insert("shared_vertex");
                result.components[current].blocked.insert("shared_vertex");
            } else {
                previous = current;
            }
        }
    }
    for (const auto& issue : issues) {
        if (issue.key == "boundary")
            continue; // Open surfaces can be tested as targets, never containers.
        for (const auto face : issue.faces)
            result.components[face_component[face]].blocked.insert(issue.key);
    }
    const auto& intersections     = issues[3];
    result.intersections_complete = intersections.complete;
    std::map<Edge, std::size_t> crossing_relations;
    for (std::size_t id = 0; id < intersections.pairs.size(); ++id) {
        auto a = face_component[intersections.pairs[id][0]], b = face_component[intersections.pairs[id][1]];
        if (a == b)
            continue;
        if (a > b)
            std::swap(a, b);
        const auto entry = crossing_relations.emplace(Edge{a, b}, result.relations.size());
        if (entry.second)
            result.relations.push_back({a, b, "surface_intersection", {}});
        result.relations[entry.first->second].intersection_pairs.push_back(id);
    }
    double total_area = 0;
    for (auto& component : result.components) {
        if (component.blocked.count("degenerate") || component.blocked.count("duplicate"))
            component.closed = false;
        total_area += component.area;
    }
    result.size_complete = std::isfinite(total_area) && total_area > 0;
    for (auto& component : result.components) {
        if (result.size_complete) {
            component.area_ratio = component.area / total_area;
            component.small      = result.components.size() > 1 && component.area_ratio <= SMALL_COMPONENT_RATIO;
        }
        if (!intersections.complete)
            component.blocked.insert("self_intersection_incomplete");
    }
    result.limit_reached = result.components.size() > mesh.options.max_solid_components;
    if (result.limit_reached) {
        result.containment_complete = false;
        for (auto& component : result.components)
            component.blocked.insert("component_limit");
        return result;
    }
    using Surface = CGAL::Surface_mesh<ExactPoint>;
    for (std::size_t i = 0; i < result.components.size(); ++i) {
        auto& component = result.components[i];
        if (!component.blocked.empty()) {
            result.containment_complete = false;
            continue;
        }
        if (!component.closed)
            continue;
        Surface                                      surface;
        std::map<std::size_t, Surface::Vertex_index> vertices;
        bool                                         converted = true;
        for (const auto face_id : component.faces) {
            std::array<Surface::Vertex_index, 3> triangle;
            for (std::size_t j = 0; j < 3; ++j) {
                const auto vertex_id = mesh.faces[face_id][j];
                auto       entry     = vertices.find(vertex_id);
                if (entry == vertices.end())
                    entry = vertices.emplace(vertex_id, surface.add_vertex(mesh.points[vertex_id])).first;
                triangle[j] = entry->second;
            }
            if (surface.add_face(triangle[0], triangle[1], triangle[2]) == Surface::null_face()) {
                converted = false;
                break;
            }
        }
        if (!converted || !CGAL::is_valid_polygon_mesh(surface) || !CGAL::is_closed(surface) || !CGAL::is_triangle_mesh(surface)) {
            component.blocked.insert("surface_conversion_failed");
            result.containment_complete = false;
            continue;
        }
        // All geometric/topological gates precede these CGAL volume queries.
        const auto volume = CGAL::Polygon_mesh_processing::volume(surface);
        if (volume == 0) {
            component.blocked.insert("zero_volume");
            result.containment_complete = false;
            continue;
        }
        component.signed_volume = CGAL::to_double(volume);
        if (component.signed_volume == 0) // Nonzero exact value underflowed for display.
            component.signed_volume = std::numeric_limits<double>::quiet_NaN();
        component.orientation = CGAL::Polygon_mesh_processing::is_outward_oriented(surface) ? "outward" : "inward";
        CGAL::Side_of_triangle_mesh<Surface, Kernel> side(surface);
        for (std::size_t j = 0; j < result.components.size(); ++j) {
            auto& target = result.components[j];
            if (i == j || !target.blocked.empty())
                continue;
            bool bbox_contains = true;
            for (std::size_t axis = 0; axis < 3; ++axis)
                if (target.min[axis] < component.min[axis] || target.max[axis] > component.max[axis])
                    bbox_contains = false;
            if (!bbox_contains)
                continue; // Broad-phase rejection only, never a positive containment result.
            // A connected surface without intersection or shared-vertex
            // contacts cannot change sides. Query one ORIGINAL vertex only
            // after the complete global triangle-intersection check above.
            const auto position = side(mesh.points[mesh.faces[target.faces.front()][0]]);
            if (position == CGAL::ON_BOUNDED_SIDE) {
                result.relations.push_back({i, j, "contains", {}});
            } else if (position == CGAL::ON_BOUNDARY) {
                result.containment_complete = false;
                target.blocked.insert("ambiguous_contact");
            }
        }
    }
    // Keep partial results only when both participating components retained
    // their preconditions; a late ambiguous contact must not leave stale claims.
    for (auto& component : result.components) {
        component.containers.clear();
        if (!component.blocked.empty()) {
            component.orientation   = "not_checked";
            component.signed_volume = std::numeric_limits<double>::quiet_NaN();
        }
    }
    result.relations.erase(std::remove_if(result.relations.begin(), result.relations.end(),
                                          [&](const auto& relation) {
                                              return relation.kind == "contains" && (!result.components[relation.a].blocked.empty() ||
                                                                                     !result.components[relation.b].blocked.empty());
                                          }),
                           result.relations.end());
    for (const auto& relation : result.relations)
        if (relation.kind == "contains")
            result.components[relation.b].containers.push_back(relation.a);
    // An even/odd shell convention is an explicit interpretation, not proof
    // of design intent. Never classify expected orientation from partial nesting.
    if (result.containment_complete) {
        for (auto& component : result.components) {
            if (component.orientation == "not_checked")
                continue;
            component.expected_orientation = component.containers.size() % 2 ? "inward" : "outward";
            component.orientation_suspect  = component.orientation != component.expected_orientation;
        }
    }
    return result;
}

template<class Range> json sample(const Range& values, std::size_t limit)
{
    json out = json::array();
    for (const auto& value : values) {
        if (out.size() == limit)
            break;
        out.push_back(value);
    }
    return out;
}

json finite_number(double value) { return std::isfinite(value) ? json(value) : json(nullptr); }
} // namespace

nlohmann::json inspect_model_mesh(const TriangleMesh& source, const ModelInspectionOptions& options)
{
    if (source.its.indices.empty())
        throw std::runtime_error("Cannot inspect an empty model.");
    if (source.its.indices.size() > options.max_triangles)
        throw std::runtime_error("Model inspection triangle limit exceeded.");
    if (options.max_intersections == std::numeric_limits<unsigned>::max())
        throw std::invalid_argument("Intersection limit must allow a sentinel result.");
    validate_mesh(source);
    Mesh mesh{options};
    mesh.faces.reserve(source.its.indices.size());
    for (const auto& face : source.its.indices) {
        Face canonical;
        for (int i = 0; i < 3; ++i) {
            const auto& v = source.its.vertices[face[i]];
            canonical[i]  = mesh.add_point({v.x(), v.y(), v.z()});
        }
        mesh.faces.push_back(canonical);
    }
    mesh.point_index.clear();
    const auto issues         = inspect(mesh);
    const auto components     = inspect_components(mesh, issues);
    json       report         = {{"schema_version", 1},
                                 {"algorithm", "demo_geometry_v1"},
                                 {"coordinate_basis", "decoded_orca_mesh"},
                                 {"triangle_count", mesh.faces.size()},
                                 {"geometric_vertex_count", mesh.points.size()},
                                 {"detail_limit", options.detail_limit},
                                 {"issues", json::object()}};
    bool       basic_complete = true, has_issues = false;
    for (const auto& issue : issues) {
        basic_complete       = basic_complete && issue.complete;
        has_issues           = has_issues || issue.count != 0;
        json item            = {{"count", issue.count},
                                {"complete", issue.complete},
                                {"count_is_lower_bound", !issue.complete},
                                {"faces", sample(issue.faces, options.detail_limit)},
                                {"vertices", sample(issue.vertices, options.detail_limit)},
                                {"edges", sample(issue.edges, options.detail_limit)},
                                {"details_truncated", issue.faces.size() > options.detail_limit || issue.edges.size() > options.detail_limit ||
                                                          issue.vertices.size() > options.detail_limit || issue.pairs.size() > options.detail_limit}};
        item["face_samples"] = json::array();
        for (std::size_t i = 0; i < std::min(issue.faces.size(), options.detail_limit); ++i) {
            const auto  id = issue.faces[i];
            const auto& f  = mesh.faces[id];
            item["face_samples"].push_back(
                {{"id", id}, {"points", {mesh.coordinates[f[0]], mesh.coordinates[f[1]], mesh.coordinates[f[2]]}}});
        }
        item["edge_samples"] = json::array();
        for (std::size_t i = 0; i < std::min(issue.edges.size(), options.detail_limit); ++i) {
            const auto& e = issue.edges[i];
            item["edge_samples"].push_back({mesh.coordinates[e[0]], mesh.coordinates[e[1]]});
        }
        if (issue.key == "self_intersection") {
            item["pairs"]         = sample(issue.pairs, options.detail_limit);
            item["pair_geometry"] = json::array();
            for (const auto& geometry : issue.pair_geometry)
                item["pair_geometry"].push_back({{"kind", geometry.kind}, {"points", geometry.points}});
        }
        report["issues"][issue.key] = std::move(item);
    }
    json        supplemental = {{"containment_complete", components.containment_complete},
                                {"intersections_complete", components.intersections_complete},
                                {"size_complete", components.size_complete},
                                {"limit_reached", components.limit_reached},
                                {"max_solid_components", options.max_solid_components},
                                {"small_area_ratio", SMALL_COMPONENT_RATIO},
                                {"component_count", components.components.size()},
                                {"components", json::array()},
                                {"relations", json::array()},
                                {"details_truncated",
                                 components.components.size() > options.detail_limit || components.relations.size() > options.detail_limit}};
    std::size_t small = 0, internal = 0, actual_known = 0, expected_unknown = 0, suspect = 0;
    for (std::size_t id = 0; id < components.components.size(); ++id) {
        const auto& c = components.components[id];
        small += c.small;
        internal += !c.containers.empty();
        actual_known += c.orientation != "not_checked";
        expected_unknown += c.expected_orientation == "unknown";
        suspect += c.orientation_suspect;
        if (id < options.detail_limit)
            supplemental["components"].push_back({{"id", id},
                                                  {"face_count", c.faces.size()},
                                                  {"faces", sample(c.faces, options.detail_limit)},
                                                  {"faces_truncated", c.faces.size() > options.detail_limit},
                                                  {"min", c.min},
                                                  {"max", c.max},
                                                  {"area", finite_number(c.area)},
                                                  {"area_ratio", c.area_ratio < 0 ? json(nullptr) : finite_number(c.area_ratio)},
                                                  {"signed_volume", finite_number(c.signed_volume)},
                                                  {"closed", c.closed},
                                                  {"small", c.small},
                                                  {"orientation", c.orientation},
                                                  {"expected_orientation", c.expected_orientation},
                                                  {"orientation_suspect", c.orientation_suspect},
                                                  {"containers", c.containers},
                                                  {"blocked_by", c.blocked}});
    }
    for (const auto& relation : components.relations) {
        if (supplemental["relations"].size() == options.detail_limit)
            break;
        json visible_pairs = json::array();
        for (const auto id : relation.intersection_pairs)
            if (id < options.detail_limit)
                visible_pairs.push_back(id);
        supplemental["relations"].push_back({{"a", relation.a},
                                             {"b", relation.b},
                                             {"kind", relation.kind},
                                             {"intersection_pairs", visible_pairs},
                                             {"pairs_truncated", relation.intersection_pairs.size() != visible_pairs.size()}});
    }
    supplemental["small_component_candidates"]    = small;
    supplemental["confirmed_internal_components"] = internal;
    supplemental["actual_orientation_known"]      = actual_known;
    supplemental["expected_orientation_unknown"]  = expected_unknown;
    supplemental["orientation_suspect_count"]     = suspect;
    supplemental["orientation_complete"]          = expected_unknown == 0;
    report["component_analysis"]                  = std::move(supplemental);
    // Keep orientation metadata for internal/legacy diagnostic readers, but
    // whole-shell direction is no longer a user-facing check or task gate.
    // Local winding conflicts remain one of the seven geometry checks.
    const bool complete             = basic_complete && components.containment_complete && components.size_complete;
    report["basic_checks_complete"] = basic_complete;
    report["check_complete"]        = complete;
    report["mesh_status"]           = has_issues ? "needs_repair" : (!complete ? "unknown" : "passed");
    report["next_action"]           = has_issues ? "advanced_repair" : (!complete ? "review_structure" : "none");
    report["printability_verified"] = false;
    return report;
}

ModelMeshCleanupPlan plan_model_mesh_cleanup(const TriangleMesh& source)
{
    validate_mesh(source);
    ModelMeshCleanupPlan plan;
    plan.kept_faces.reserve(source.its.indices.size());
    std::set<std::array<XYZ, 3>> seen;
    for (std::size_t id = 0; id < source.its.indices.size(); ++id) {
        const auto&               f = source.its.indices[id];
        std::array<XYZ, 3>        key;
        std::array<ExactPoint, 3> points;
        for (int j = 0; j < 3; ++j) {
            const auto& v = source.its.vertices[f[j]];
            key[j]        = {v.x(), v.y(), v.z()};
            points[j]     = ExactPoint(v.x(), v.y(), v.z());
        }
        if (CGAL::collinear(points[0], points[1], points[2])) {
            ++plan.zero_area_faces;
            continue;
        }
        std::rotate(key.begin(), std::min_element(key.begin(), key.end()), key.end());
        if (!seen.insert(key).second) {
            ++plan.same_winding_duplicates;
            continue;
        }
        plan.kept_faces.push_back(id);
    }
    return plan;
}
} // namespace Slic3r
