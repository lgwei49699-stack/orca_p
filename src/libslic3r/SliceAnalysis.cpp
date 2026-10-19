#include "SliceAnalysis.hpp"

#include "AABBTreeIndirect.hpp"
#include "AABBTreeLines.hpp"
#include "ClipperUtils.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "GCode/GCodeProcessor.hpp"
#include "Geometry/Voronoi.hpp"
#include "Layer.hpp"
#include "Point.hpp"
#include "Print.hpp"
#include "SliceAnalysisGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
namespace {

using json = nlohmann::json;

struct NumericRange
{
    size_t count         = 0;
    size_t invalid_count = 0;
    double minimum       = 0.;
    double maximum       = 0.;

    void add(double value)
    {
        if (!std::isfinite(value) || value < 0.) {
            ++invalid_count;
            return;
        }
        if (count == 0)
            minimum = maximum = value;
        else {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        ++count;
    }

    json to_json() const
    {
        return {{"count", count},
                {"invalid_count", invalid_count},
                {"min", count == 0 ? json(nullptr) : json(minimum)},
                {"max", count == 0 ? json(nullptr) : json(maximum)}};
    }
};

json nonnegative_number(double value) { return std::isfinite(value) && value >= 0. ? json(value) : json(nullptr); }

json finite_number(double value) { return std::isfinite(value) ? json(value) : json(nullptr); }

double area_mm2(const ExPolygons& polygons)
{
    double area = 0.;
    for (const ExPolygon& polygon : polygons)
        area += polygon.area() * SCALING_FACTOR * SCALING_FACTOR;
    return area;
}

size_t positive_region_count(const ExPolygons& polygons)
{
    return std::count_if(polygons.begin(), polygons.end(), [](const ExPolygon& polygon) { return polygon.area() > 0.; });
}

ExPolygons bounding_box_candidates(const ExPolygons& polygons, const std::vector<BoundingBox>& bounds, const BoundingBox& subject_bounds)
{
    ExPolygons candidates;
    for (size_t index = 0; index < polygons.size(); ++index)
        if (subject_bounds.overlap(bounds[index]))
            candidates.push_back(polygons[index]);
    // Keep whole contours and holes, including touching boxes. The original
    // clipping operation still decides all intersections and region counts.
    return candidates;
}

ExPolygons bounded_voids(const ExPolygons& material)
{
    Polygons contours;
    for (const ExPolygon& island : material)
        for (Polygon hole : island.holes) {
            hole.make_counter_clockwise();
            contours.push_back(std::move(hole));
        }
    // Material islands nested inside a hole are not part of its empty space.
    return diff_ex(union_ex(contours), material);
}

void throw_if_canceled(const Print& print)
{
    if (print.canceled())
        throw CanceledException();
}

json parallel_boundary_clearance(const Print& print, const ExPolygons& section, bool complete)
{
    json result = {{"scope", "same_section_island_parallel_voronoi_sites"},
                   {"complete", complete},
                   {"min_material_width_mm", nullptr},
                   {"min_void_width_mm", nullptr}};
    if (!complete)
        return result;

    Lines               lines;
    std::vector<size_t> owners;
    for (size_t island = 0; island < section.size(); ++island) {
        throw_if_canceled(print);
        if (section[island].area() <= 0.)
            continue;
        for (const Line& line : section[island].lines())
            if (line.a != line.b) {
                lines.push_back(line);
                owners.push_back(island);
            }
    }
    if (lines.empty())
        return result;

    // Boost's default Voronoi builder uses int32 sites. Localize before
    // conversion so a large placement does not truncate otherwise small shapes.
    const BoundingBox bounds = get_extents(section);
    const Point       origin(bounds.min.x() / 2 + bounds.max.x() / 2, bounds.min.y() / 2 + bounds.max.y() / 2);
    for (Line& line : lines) {
        throw_if_canceled(print);
        for (Point* point : {&line.a, &line.b})
            for (int axis = 0; axis < 2; ++axis) {
                const long double value = static_cast<long double>((*point)[axis]) - static_cast<long double>(origin[axis]);
                if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
                    result["complete"] = false;
                    return result;
                }
                (*point)[axis] = coord_t(value);
            }
    }

    Geometry::VoronoiDiagram diagram;
    throw_if_canceled(print);
    // Validate the original diagram without rotating/repairing its input sites.
    // Reporting must not use modified geometry or MedialAxis filtering.
    diagram.construct_voronoi(lines.begin(), lines.end(), false);
    throw_if_canceled(print);
    if (Geometry::VoronoiDiagram::detect_known_issues(diagram, lines.begin(), lines.end()) !=
        Geometry::VoronoiDiagram::IssueType::NO_ISSUE_DETECTED) {
        result["complete"] = false;
        return result;
    }
    throw_if_canceled(print);

    double material_width = std::numeric_limits<double>::infinity();
    double void_width     = std::numeric_limits<double>::infinity();
    for (const auto& edge : diagram.edges()) {
        throw_if_canceled(print);
        if (!edge.is_primary() || !edge.is_linear() || !edge.is_finite() || !edge.cell()->contains_segment() ||
            !edge.twin()->cell()->contains_segment())
            continue;
        const size_t first  = edge.cell()->source_index();
        const size_t second = edge.twin()->cell()->source_index();
        if (first >= lines.size() || second >= lines.size()) {
            result["complete"] = false;
            return result;
        }
        // Keep one half-edge. All islands participate in nearest-site selection,
        // but a pair spanning different islands is the separate island-gap metric.
        if (first >= second || owners[first] != owners[second])
            continue;
        const Vec2crd first_direction  = lines[first].b - lines[first].a;
        const Vec2crd second_direction = lines[second].b - lines[second].a;
        const Vec2d   direction       = first_direction.cast<double>();
        if (int128::cross(first_direction, second_direction) != 0 || direction.dot(second_direction.cast<double>()) >= 0.)
            continue;
        const Vec2d v0(edge.vertex0()->x(), edge.vertex0()->y());
        const Vec2d v1(edge.vertex1()->x(), edge.vertex1()->y());
        if (!v0.allFinite() || !v1.allFinite()) {
            result["complete"] = false;
            return result;
        }
        if ((v1 - v0).squaredNorm() == 0.)
            continue;
        const Vec2d separation = lines[second].a.cast<double>() - lines[first].a.cast<double>();
        const double width = std::abs(direction.x() * separation.y() - direction.y() * separation.x()) /
                             direction.norm() * SCALING_FACTOR;
        if (!std::isfinite(width)) {
            result["complete"] = false;
            return result;
        }
        if (width <= 0.)
            continue;
        const Vec2d midpoint = 0.5 * (v0 + v1);
        const auto side = [&](size_t site) {
            const Vec2d d = (lines[site].b - lines[site].a).cast<double>();
            const Vec2d p = midpoint - lines[site].a.cast<double>();
            return d.x() * p.y() - d.y() * p.x();
        };
        const double first_side = side(first), second_side = side(second);
        if (first_side == 0. || second_side == 0. || (first_side > 0.) != (second_side > 0.)) {
            result["complete"] = false;
            return result;
        }
        // Normalized outer contours and holes both keep material on their left.
        // A positive-width edge equidistant from parallel nearest sites cannot
        // cross a boundary along its length. No rounded midpoint or angle cutoff.
        if (first_side > 0.)
            material_width = std::min(material_width, width);
        else
            void_width = std::min(void_width, width);
    }
    if (std::isfinite(material_width))
        result["min_material_width_mm"] = material_width;
    if (std::isfinite(void_width))
        result["min_void_width_mm"] = void_width;
    return result;
}

json section_min_island_gap(const Print& print, const ExPolygons& section)
{
    if (positive_region_count(section) < 2)
        return nullptr;

    struct Boundary
    {
        Linesf                            lines;
        AABBTreeIndirect::Tree<2, double> tree;
    };
    std::vector<Boundary> boundaries;
    boundaries.reserve(section.size());
    for (const ExPolygon& island : section) {
        throw_if_canceled(print);
        if (island.area() <= 0.)
            continue;
        Boundary boundary;
        // ExPolygon::lines includes closing edges and every hole boundary.
        // Use doubles in mm for both tree boxes and distance calculations.
        for (const Line& line : island.lines())
            boundary.lines.emplace_back(line.a.cast<double>() * SCALING_FACTOR, line.b.cast<double>() * SCALING_FACTOR);
        boundary.tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(boundary.lines);
        boundaries.push_back(std::move(boundary));
    }

    double     best_squared   = std::numeric_limits<double>::infinity();
    const auto query_vertices = [&](const Boundary& from, const Boundary& to) {
        for (const Linef& line : from.lines) {
            throw_if_canceled(print);
            size_t       nearest_index = size_t(-1);
            Vec2d        nearest_point = Vec2d::Zero();
            const double distance      = AABBTreeLines::squared_distance_to_indexed_lines(to.lines, to.tree, line.a, nearest_index,
                                                                                          nearest_point, best_squared);
            if (distance >= 0.)
                best_squared = std::min(best_squared, distance);
            if (best_squared == 0.)
                break;
        }
    };
    for (size_t first = 0; first < boundaries.size(); ++first) {
        throw_if_canceled(print);
        const auto& first_box = boundaries[first].tree.node(0).bbox;
        for (size_t second = first + 1; second < boundaries.size(); ++second) {
            throw_if_canceled(print);
            const auto& second_box    = boundaries[second].tree.node(0).bbox;
            double      lower_squared = 0.;
            for (int axis = 0; axis < 2; ++axis) {
                const double separation = std::max(
                    {0., first_box.min()[axis] - second_box.max()[axis], second_box.min()[axis] - first_box.max()[axis]});
                lower_squared += separation * separation;
            }
            if (lower_squared >= best_squared)
                continue;
            // Distinct normalized islands do not have overlapping interiors.
            // The shortest boundary distance has an endpoint on at least one
            // of its two segments; querying both directions is essential.
            query_vertices(boundaries[first], boundaries[second]);
            if (best_squared == 0.)
                return 0.;
            query_vertices(boundaries[second], boundaries[first]);
            if (best_squared == 0.)
                return 0.;
        }
    }
    return nonnegative_number(std::sqrt(best_squared));
}

struct BridgePathStats
{
    size_t path_count          = 0;
    size_t excluded_path_count = 0;
    size_t invalid_path_count  = 0;
    double path_length_mm      = 0.;
    double layer_area_sum_mm2  = 0.;
};

int bridge_role_index(ExtrusionRole role)
{
    switch (role) {
    case erBridgeInfill: return 0;
    case erInternalBridgeInfill: return 1;
    case erOverhangPerimeter: return 2;
    default: return -1;
    }
}

struct PlannedPerimeterLoops
{
    const Print& print;
    bool         complete = true;
    NumericRange lengths;
    NumericRange angles;

    void merge(const PlannedPerimeterLoops& other)
    {
        complete               = complete && other.complete;
        const auto merge_range = [](NumericRange& target, const NumericRange& source) {
            if (source.count != 0) {
                target.minimum = target.count == 0 ? source.minimum : std::min(target.minimum, source.minimum);
                target.maximum = target.count == 0 ? source.maximum : std::max(target.maximum, source.maximum);
            }
            target.count += source.count;
            target.invalid_count += source.invalid_count;
        };
        merge_range(lengths, other.lengths);
        merge_range(angles, other.angles);
    }

    void add(const ExtrusionLoop& loop)
    {
        // role() describes only the first fragment. Overhang fragments may
        // coexist with ordinary walls in one closed perimeter.
        for (const ExtrusionPath& path : loop.paths) {
            throw_if_canceled(print);
            if (!is_perimeter(path.role()) || path.is_force_no_extrusion() || path.mm3_per_mm == 0.)
                return;
        }
        Points points;
        for (const ExtrusionPath& path : loop.paths) {
            throw_if_canceled(print);
            if (path.polyline.size() < 2 || !std::isfinite(path.mm3_per_mm) || path.mm3_per_mm < 0. ||
                !std::isfinite(path.width) || path.width <= 0.f ||
                (!points.empty() && points.back() != path.polyline.points.front())) {
                complete = false;
                return;
            }
            for (const Point& point : path.polyline.points) {
                throw_if_canceled(print);
                if (points.empty() || points.back() != point)
                    points.push_back(point);
            }
        }
        if (points.size() < 4 || points.front() != points.back()) {
            complete = false;
            return;
        }
        points.pop_back(); // Closing endpoint is the first vertex, not another turn.
        double       length = 0.;
        NumericRange loop_angles;
        bool         non_collinear = false;
        for (size_t index = 0; index < points.size(); ++index) {
            throw_if_canceled(print);
            const Point& previous = points[(index + points.size() - 1) % points.size()];
            const Point& current  = points[index];
            const Point& next     = points[(index + 1) % points.size()];
            const Vec2d incoming  = current.cast<double>() - previous.cast<double>();
            const Vec2d outgoing  = next.cast<double>() - current.cast<double>();
            non_collinear = non_collinear || int128::orient(previous, current, next) != 0;
            length += outgoing.norm() * SCALING_FACTOR;
            loop_angles.add(std::atan2(std::abs(incoming.x() * outgoing.y() - incoming.y() * outgoing.x()),
                                      incoming.dot(outgoing)) * (180. / M_PI));
        }
        if (!non_collinear || !std::isfinite(length) || loop_angles.invalid_count != 0) {
            complete = false;
            return;
        }
        lengths.add(length);
        angles.add(loop_angles.minimum);
        angles.add(loop_angles.maximum);
    }

    json to_json() const
    {
        const auto range = [&](const NumericRange& values) {
            return json{{"min", complete && values.count != 0 ? json(values.minimum) : json(nullptr)},
                        {"max", complete && values.count != 0 ? json(values.maximum) : json(nullptr)}};
        };
        return {{"scope", "per_instance_closed_model_perimeter_centerlines_before_gcode"},
                {"complete", complete},
                {"loop_count", complete ? json(lengths.count) : json(nullptr)},
                {"loop_length_mm", range(lengths)},
                {"turn_angle_degrees", range(angles)}};
    }
};

struct PathSlabFootprint
{
    struct PathRange
    {
        float  height;
        size_t begin, end;
    };
    bool                      complete = true;
    std::map<float, Polygons> by_height;
    // Preserve each original path's ring group without a second width offset
    // or another copy of its vertices. Different paths must union as sets,
    // not cancel each other's opposite-winding pieces.
    std::vector<PathRange> paths;
};

struct PlannedFootprint
{
    const Print&                      print;
    Polygons                          polygons;
    size_t                            excluded_path_count = 0;
    size_t                            invalid_path_count  = 0;
    std::array<BridgePathStats, 3>    bridge_stats;
    std::array<Polygons, 3>           bridge_polygons;
    std::vector<const ExtrusionPath*> external_bridge_paths;

    void add_path(const ExtrusionPath& path, PathSlabFootprint* slabs = nullptr)
    {
        throw_if_canceled(print);
        const int        role_index = bridge_role_index(path.role());
        BridgePathStats* bridge     = role_index < 0 ? nullptr : &bridge_stats[size_t(role_index)];
        if (path.is_force_no_extrusion() || path.mm3_per_mm == 0. || path.polyline.size() < 2) {
            ++excluded_path_count;
            if (bridge != nullptr)
                ++bridge->excluded_path_count;
            return;
        }
        if (!std::isfinite(path.width) || path.width <= 0.f || !std::isfinite(path.mm3_per_mm) || path.mm3_per_mm < 0.) {
            ++invalid_path_count;
            if (slabs != nullptr)
                slabs->complete = false;
            if (bridge != nullptr)
                ++bridge->invalid_path_count;
            return;
        }
        Polygons path_polygons = path.polygons_covered_by_width(0.f);
        if (slabs != nullptr) {
            if (!std::isfinite(path.height) || path.height <= 0.f)
                slabs->complete = false;
            else {
                Polygons&    polygons = slabs->by_height[path.height];
                const size_t begin    = polygons.size();
                polygons_append(polygons, path_polygons);
                slabs->paths.push_back({path.height, begin, polygons.size()});
            }
        }
        if (bridge != nullptr) {
            ++bridge->path_count;
            bridge->path_length_mm += unscale<double>(path.length());
            polygons_append(bridge_polygons[size_t(role_index)], path_polygons);
        }
        if (path.role() == erBridgeInfill)
            external_bridge_paths.push_back(&path);
        polygons_append(polygons, std::move(path_polygons));
    }

    void add(const ExtrusionEntity& entity, PlannedPerimeterLoops* perimeter_loops = nullptr, PathSlabFootprint* slabs = nullptr)
    {
        throw_if_canceled(print);
        if (const auto* collection = dynamic_cast<const ExtrusionEntityCollection*>(&entity)) {
            for (const ExtrusionEntity* child : collection->entities)
                add(*child, perimeter_loops, slabs);
        } else if (const auto* path = dynamic_cast<const ExtrusionPath*>(&entity)) {
            add_path(*path, slabs);
        } else if (const auto* multipath = dynamic_cast<const ExtrusionMultiPath*>(&entity)) {
            for (const ExtrusionPath& path : multipath->paths)
                add_path(path, slabs);
        } else if (const auto* loop = dynamic_cast<const ExtrusionLoop*>(&entity)) {
            if (perimeter_loops != nullptr)
                perimeter_loops->add(*loop);
            for (const ExtrusionPath& path : loop->paths)
                add_path(path, slabs);
        } else {
            ++invalid_path_count;
            if (perimeter_loops != nullptr)
                perimeter_loops->complete = false;
            if (slabs != nullptr)
                slabs->complete = false;
        }
    }
};

// One width expansion per retained Layer*, shared by the old observations and
// the joint slab query. Instances reuse these local-coordinate polygons.
struct LayerPathFootprint
{
    PlannedFootprint      footprint;
    PathSlabFootprint     heights;
    PlannedPerimeterLoops loops;
    ExPolygons            covered;
    BoundingBox           bounds;

    explicit LayerPathFootprint(const Print& print) : footprint{print}, loops{print} {}
};

struct LayerPathCache
{
    const Print&                                                print;
    std::map<const Layer*, std::unique_ptr<LayerPathFootprint>> layers;

    const LayerPathFootprint& get(const Layer& layer, bool support = false)
    {
        auto inserted = layers.try_emplace(&layer);
        if (inserted.second) {
            auto cached = std::make_unique<LayerPathFootprint>(print);
            if (support)
                cached->footprint.add(static_cast<const SupportLayer&>(layer).support_fills, nullptr, &cached->heights);
            else
                for (const LayerRegion* region : layer.regions()) {
                    throw_if_canceled(print);
                    cached->footprint.add(region->perimeters, &cached->loops, &cached->heights);
                    cached->footprint.add(region->fills, nullptr, &cached->heights);
                }
            throw_if_canceled(print);
            cached->covered        = union_ex(cached->footprint.polygons);
            cached->bounds         = get_extents(cached->covered);
            inserted.first->second = std::move(cached);
        }
        return *inserted.first->second;
    }
};

using SegmentInterval = std::pair<double, double>;

std::vector<SegmentInterval> merged_intervals(std::vector<SegmentInterval> intervals)
{
    std::sort(intervals.begin(), intervals.end());
    std::vector<SegmentInterval> merged;
    for (const SegmentInterval& interval : intervals) {
        if (interval.second <= interval.first)
            continue;
        if (merged.empty() || interval.first > merged.back().second)
            merged.push_back(interval);
        else
            merged.back().second = std::max(merged.back().second, interval.second);
    }
    return merged;
}

struct FootprintBoundary
{
    const ExPolygon* polygon;
    BoundingBox      bounds;
    Lines            edges;
};

std::vector<SegmentInterval> segment_interior_coverage(const Print& print, const Point& start, const Point& end,
                                                      const std::vector<FootprintBoundary>& footprint)
{
    const Vec2d       direction = end.cast<double>() - start.cast<double>();
    const double      squared   = direction.squaredNorm();
    const BoundingBox bounds(start.cwiseMin(end), start.cwiseMax(end));
    const auto        parameter = [&](const Point& point) {
        return std::clamp((point.cast<double>() - start.cast<double>()).dot(direction) / squared, 0., 1.);
    };
    std::vector<SegmentInterval> covered, boundary;
    for (const FootprintBoundary& region : footprint) {
        throw_if_canceled(print);
        if (!bounds.overlap(region.bounds))
            continue;
        // Clipper output has no traversal order. Project every clipped piece
        // onto this original segment before sorting and joining intervals.
        for (const Polyline& clipped : intersection_pl(Polyline(start, end), *region.polygon)) {
            double first = 1., last = 0.;
            for (const Point& point : clipped.points) {
                const double t = parameter(point);
                first          = std::min(first, t);
                last           = std::max(last, t);
            }
            if (last > first)
                covered.emplace_back(first, last);
        }
        // A positive-length path lying on a polygon or hole boundary is not
        // interior coverage. Exact collinearity avoids a new offset/tolerance
        // and avoids rounding a short interval's midpoint onto the boundary.
        for (const Line& edge : region.edges) {
            throw_if_canceled(print);
            if (!bounds.overlap(BoundingBox(edge.a.cwiseMin(edge.b), edge.a.cwiseMax(edge.b))))
                continue;
            if (int128::orient(start, end, edge.a) != 0 || int128::orient(start, end, edge.b) != 0)
                continue;
            const double first = parameter(edge.a), last = parameter(edge.b);
            if (first != last)
                boundary.emplace_back(std::min(first, last), std::max(first, last));
        }
    }
    covered  = merged_intervals(std::move(covered));
    boundary = merged_intervals(std::move(boundary));
    std::vector<SegmentInterval> interior;
    size_t                       boundary_index = 0;
    for (const SegmentInterval& interval : covered) {
        double cursor = interval.first;
        while (boundary_index < boundary.size() && boundary[boundary_index].second <= cursor)
            ++boundary_index;
        for (size_t index = boundary_index; index < boundary.size() && boundary[index].first < interval.second; ++index) {
            if (boundary[index].first > cursor)
                interior.emplace_back(cursor, boundary[index].first);
            cursor = std::max(cursor, boundary[index].second);
            if (cursor >= interval.second)
                break;
        }
        if (cursor < interval.second)
            interior.emplace_back(cursor, interval.second);
    }
    return interior;
}

json bridge_coverage_runs(const Print&                             print,
                          const std::vector<const ExtrusionPath*>& paths,
                          const std::vector<FootprintBoundary>&    boundaries,
                          bool                                     complete,
                          const Point&                             shift = Point(0, 0))
{
    // There are no distinct start/end sides for a closed path in this contract.
    // Do not silently turn an unobserved case into a zero-run measurement.
    for (const ExtrusionPath* path : paths) {
        throw_if_canceled(print);
        if (path->polyline.first_point() == path->polyline.last_point() &&
            std::any_of(path->polyline.points.begin(), path->polyline.points.end(),
                        [&](const Point& point) { return point != path->polyline.first_point(); })) {
            complete = false;
            break;
        }
    }
    size_t uncovered_count = 0, two_sided_count = 0;
    double maximum = 0., two_sided_maximum = 0.;
    if (complete && !paths.empty()) {
        for (const ExtrusionPath* path : paths) {
            throw_if_canceled(print);
            // Runs belong to one retained path fragment, never to a whole
            // physical bridge. Adjacent covered runs must have positive length.
            std::vector<std::pair<bool, double>> runs;
            const auto                           add_run = [&](bool covered, double length) {
                if (length <= 0.)
                    return;
                if (runs.empty() || runs.back().first != covered)
                    runs.emplace_back(covered, length);
                else
                    runs.back().second += length;
            };
            for (size_t index = 1; index < path->polyline.points.size(); ++index) {
                throw_if_canceled(print);
                const Point start = path->polyline.points[index - 1] + shift;
                const Point end   = path->polyline.points[index] + shift;
                if (start == end)
                    continue;
                const double length = (end.cast<double>() - start.cast<double>()).norm() * SCALING_FACTOR;
                double       cursor = 0.;
                for (const SegmentInterval& interval : segment_interior_coverage(print, start, end, boundaries)) {
                    add_run(false, (interval.first - cursor) * length);
                    add_run(true, (interval.second - interval.first) * length);
                    cursor = interval.second;
                }
                add_run(false, (1. - cursor) * length);
            }
            for (size_t index = 0; index < runs.size(); ++index) {
                if (runs[index].first)
                    continue;
                ++uncovered_count;
                maximum = std::max(maximum, runs[index].second);
                if (index > 0 && index + 1 < runs.size()) {
                    ++two_sided_count;
                    two_sided_maximum = std::max(two_sided_maximum, runs[index].second);
                }
            }
        }
    }
    return {{"complete", complete},
            {"uncovered_run_count", complete ? json(uncovered_count) : json(nullptr)},
            {"max_uncovered_run_mm", complete && uncovered_count > 0 ? nonnegative_number(maximum) : json(nullptr)},
            {"two_sided_run_count", complete ? json(two_sided_count) : json(nullptr)},
            {"max_two_sided_run_mm", complete && two_sided_count > 0 ? nonnegative_number(two_sided_maximum) : json(nullptr)}};
}

json external_bridge_projection_layer(const Print&            print,
                                      const Layer&            layer,
                                      size_t                  layer_index,
                                      const Layer*            previous_layer,
                                      const PlannedFootprint& current,
                                      const ExPolygons&       previous_covered,
                                      bool                    complete)
{
    std::vector<FootprintBoundary> boundaries;
    if (complete && !current.external_bridge_paths.empty())
        for (const ExPolygon& polygon : previous_covered) {
            throw_if_canceled(print);
            boundaries.push_back({&polygon, get_extents(polygon), polygon.lines()});
        }
    json result                    = bridge_coverage_runs(print, current.external_bridge_paths, boundaries, complete);
    result["layer_index"]          = layer_index;
    result["previous_layer_index"] = previous_layer == nullptr ? json(nullptr) : json(layer_index - 1);
    result["print_z_mm"]           = finite_number(layer.print_z);
    result["previous_print_z_mm"]  = previous_layer == nullptr ? json(nullptr) : finite_number(previous_layer->print_z);
    return result;
}

struct SupportProjectionLevel
{
    double      print_z;
    ExPolygons  footprint;
    BoundingBox bounds;
};

struct PlateSupportProjection
{
    bool                                complete = true;
    std::vector<SupportProjectionLevel> levels;
};

bool valid_model_layer_geometry(const Layer& layer)
{
    return !layer.slicing_errors && std::isfinite(layer.slice_z) && std::isfinite(layer.print_z) && std::isfinite(layer.bottom_z()) &&
           std::isfinite(layer.height) && layer.height > 0.;
}

struct BedFootprint
{
    const Print& print;
    bool         complete = true;
    Polygons     polygons;

    void add_path(const ExtrusionPath& path, double print_z)
    {
        if (path.is_force_no_extrusion() || path.mm3_per_mm == 0. || path.polyline.size() < 2 ||
            path.role() == erSkirt || path.role() == erWipeTower || path.role() == erCustom)
            return;
        // Support paths in the same layer can have different heights. Do not
        // replace the path's nominal bottom with Layer::bottom_z().
        if (!std::isfinite(print_z) || !std::isfinite(path.height) || path.height <= 0.f) {
            complete = false;
            return;
        }
        if (std::abs(print_z - double(path.height)) > SCALING_FACTOR)
            return;
        if (!std::isfinite(path.width) || path.width <= 0.f || !std::isfinite(path.mm3_per_mm) || path.mm3_per_mm < 0.) {
            complete = false;
            return;
        }
        path.polygons_covered_by_width(polygons, 0.f);
    }

    void add(const ExtrusionEntity& entity, double print_z)
    {
        throw_if_canceled(print);
        if (const auto* collection = dynamic_cast<const ExtrusionEntityCollection*>(&entity)) {
            for (const ExtrusionEntity* child : collection->entities)
                add(*child, print_z);
        } else if (const auto* path = dynamic_cast<const ExtrusionPath*>(&entity)) {
            add_path(*path, print_z);
        } else if (const auto* multipath = dynamic_cast<const ExtrusionMultiPath*>(&entity)) {
            for (const ExtrusionPath& path : multipath->paths) {
                throw_if_canceled(print);
                add_path(path, print_z);
            }
        } else if (const auto* loop = dynamic_cast<const ExtrusionLoop*>(&entity)) {
            for (const ExtrusionPath& path : loop->paths) {
                throw_if_canceled(print);
                add_path(path, print_z);
            }
        } else {
            complete = false;
        }
    }
};

json bed_footprint_regions(const Print& print, const ExPolygons& merged)
{
    json regions = json::array();
    for (const ExPolygon& polygon : merged) {
        throw_if_canceled(print);
        const double area = polygon.area() * SCALING_FACTOR * SCALING_FACTOR;
        if (area <= 0.)
            continue;
        const BoundingBox bounds = get_extents(polygon);
        regions.push_back({{"area_mm2", area},
                           {"bounds_mm",
                            {{"min", json::array({unscale<double>(bounds.min.x()), unscale<double>(bounds.min.y())})},
                             {"max", json::array({unscale<double>(bounds.max.x()), unscale<double>(bounds.max.y())})}}}});
    }
    return regions;
}

json planned_plate_bed_footprint(const Print& print)
{
    BedFootprint model{print}, support{print}, brim{print};
    for (const PrintObject* object : print.objects()) {
        throw_if_canceled(print);
        if (object->instances().empty())
            continue;
        BedFootprint local_model{print}, local_support{print};
        if (object->layers().empty())
            local_model.complete = false;
        for (const Layer* layer : object->layers()) {
            throw_if_canceled(print);
            local_model.complete = local_model.complete && valid_model_layer_geometry(*layer);
            for (const LayerRegion* region : layer->regions()) {
                local_model.add(region->perimeters, layer->print_z);
                local_model.add(region->fills, layer->print_z);
            }
        }
        for (const SupportLayer* layer : object->support_layers())
            local_support.add(layer->support_fills, layer->print_z);
        model.complete = model.complete && local_model.complete;
        support.complete = support.complete && local_support.complete;
        for (const PrintInstance& instance : object->instances()) {
            throw_if_canceled(print);
            for (const auto& pair : {std::make_pair(&local_model.polygons, &model.polygons),
                                    std::make_pair(&local_support.polygons, &support.polygons)})
                for (Polygon polygon : *pair.first) {
                    polygon.translate(instance.shift);
                    pair.second->push_back(std::move(polygon));
                }
        }
    }
    const auto add_brims = [&](const auto& entries, bool support_brim) {
        for (const auto& entry : entries) {
            throw_if_canceled(print);
            if (entry.second.empty())
                continue;
            const PrintObject* object = print.get_object(entry.first);
            if (object == nullptr) {
                brim.complete = false;
                continue;
            }
            if (object->instances().empty())
                continue;
            const Layer* first = support_brim
                ? (object->support_layers().empty() ? nullptr : object->support_layers().front())
                : (object->layers().empty() ? nullptr : object->layers().front());
            // Brim maps already contain all instances in plate coordinates.
            // G-code emits each map once at its owner's first model/support Z.
            brim.add(entry.second, first == nullptr ? std::numeric_limits<double>::quiet_NaN() : first->print_z);
        }
    };
    add_brims(print.get_brimMap(), false);
    add_brims(print.get_supportBrimMap(), true);

    json result = {{"scope", "planned_path_nominal_bottom_on_bed_before_gcode"},
                   {"complete", model.complete && support.complete && brim.complete},
                   {"model_area_mm2", nullptr},
                   {"support_and_raft_area_mm2", nullptr},
                   {"brim_area_mm2", nullptr},
                   {"combined_area_mm2", nullptr},
                   {"model_regions", nullptr},
                   {"support_and_raft_regions", nullptr},
                   {"brim_regions", nullptr},
                   {"combined_regions", nullptr}};
    if (!result["complete"].get<bool>())
        return result;
    Polygons   combined;
    const auto add_category = [&](const BedFootprint& category, const char* area_key, const char* regions_key) {
        throw_if_canceled(print);
        const ExPolygons merged = union_ex(category.polygons);
        result[area_key]        = nonnegative_number(area_mm2(merged));
        result[regions_key]     = bed_footprint_regions(print, merged);
        polygons_append(combined, to_polygons(merged));
    };
    add_category(model, "model_area_mm2", "model_regions");
    add_category(support, "support_and_raft_area_mm2", "support_and_raft_regions");
    add_category(brim, "brim_area_mm2", "brim_regions");
    throw_if_canceled(print);
    const ExPolygons merged     = union_ex(combined);
    result["combined_area_mm2"] = nonnegative_number(area_mm2(merged));
    result["combined_regions"]  = bed_footprint_regions(print, merged);
    return result;
}

struct ModelProjectionLayer
{
    const PrintObject* object;
    const Layer*       layer;
};

struct ModelProjectionFootprint
{
    bool        complete = false;
    ExPolygons  polygons;
    BoundingBox bounds;
};

struct PlateModelProjection
{
    LayerPathCache&                                  paths;
    bool                                             finite_z = true;
    std::vector<ModelProjectionLayer>                layers;
    std::map<const Layer*, ModelProjectionFootprint> footprints;
};

PlateModelProjection plate_model_projection(const Print& print, LayerPathCache& paths)
{
    PlateModelProjection result{paths};
    for (const PrintObject* object : print.objects()) {
        if (object->instances().empty())
            continue;
        for (const Layer* layer : object->layers()) {
            throw_if_canceled(print);
            if (!std::isfinite(layer->print_z)) {
                // This layer cannot be placed inside or outside any Z window.
                result.finite_z = false;
                continue;
            }
            result.layers.push_back({object, layer});
        }
    }
    std::sort(result.layers.begin(), result.layers.end(), [](const ModelProjectionLayer& first, const ModelProjectionLayer& second) {
        return first.layer->print_z < second.layer->print_z;
    });
    return result;
}

const ModelProjectionFootprint& model_projection_footprint(const Print& print, PlateModelProjection& model, const Layer* layer)
{
    auto                      inserted = model.footprints.try_emplace(layer);
    ModelProjectionFootprint& cached   = inserted.first->second;
    if (inserted.second && valid_model_layer_geometry(*layer)) {
        const LayerPathFootprint& paths = model.paths.get(*layer);
        cached.complete                 = paths.footprint.invalid_path_count == 0;
        if (cached.complete) {
            throw_if_canceled(print);
            cached.polygons = paths.covered;
            cached.bounds   = paths.bounds;
        }
    }
    return cached;
}

json intervening_model_projection(
    const Print& print, PlateModelProjection& model, double support_z, double target_z, const ExPolygons& assigned)
{
    const json unavailable = {{"complete", false}, {"area_mm2", nullptr}};
    if (!model.finite_z || support_z >= target_z)
        return unavailable;

    ExPolygons  remaining = assigned;
    BoundingBox bounds    = get_extents(remaining);
    auto        candidate = std::upper_bound(model.layers.begin(), model.layers.end(), support_z,
                                            [](double z, const ModelProjectionLayer& entry) { return z < entry.layer->print_z; });
    for (; candidate != model.layers.end() && candidate->layer->print_z < target_z; ++candidate) {
        throw_if_canceled(print);
        const ModelProjectionFootprint& footprint = model_projection_footprint(print, model, candidate->layer);
        if (!footprint.complete)
            return unavailable;
        // Even complete geometric coverage must not hide an invalid later
        // layer in the window. Skip clipping, but still validate every cache.
        if (remaining.empty() || footprint.polygons.empty())
            continue;
        for (const PrintInstance& instance : candidate->object->instances()) {
            throw_if_canceled(print);
            BoundingBox shifted_bounds = footprint.bounds;
            shifted_bounds.translate(instance.shift);
            if (remaining.empty() || !bounds.overlap(shifted_bounds))
                continue;
            // Keep one local footprint per Layer*, including shared layers.
            // Transform the smaller query rather than copying a whole layer
            // for every instance. Removing overlaps counts each location once.
            for (ExPolygon& polygon : remaining)
                polygon.translate(-instance.shift);
            remaining = diff_ex(remaining, footprint.polygons);
            for (ExPolygon& polygon : remaining)
                polygon.translate(instance.shift);
            bounds = get_extents(remaining);
        }
    }
    // Clamp only floating-point area subtraction at zero, not a material or
    // manufacturing tolerance. This is a print-plane projection, not a slab.
    return {{"complete", true}, {"area_mm2", nonnegative_number(std::max(0., area_mm2(assigned) - area_mm2(remaining)))}};
}

PlateSupportProjection plate_support_projection(const Print& print, LayerPathCache& paths)
{
    PlateSupportProjection     result;
    std::map<double, Polygons> by_z;
    for (const PrintObject* object : print.objects()) {
        if (object->instances().empty())
            continue;
        for (const SupportLayer* layer : object->support_layers()) {
            throw_if_canceled(print);
            // Ordinary supports may have slice_z=-1 and mixed path heights.
            // Only the retained top/print plane is used by this projection.
            if (!std::isfinite(layer->print_z)) {
                result.complete = false;
                continue;
            }
            const PlannedFootprint& footprint = paths.get(*layer, true).footprint;
            result.complete                   = result.complete && footprint.invalid_path_count == 0;
            if (footprint.polygons.empty())
                continue;
            // Shared layers still print at every instance's own shift. Merged
            // rafts live in their current owner's fills, never in the backup.
            for (const PrintInstance& instance : object->instances()) {
                throw_if_canceled(print);
                Polygons shifted = footprint.polygons;
                for (Polygon& polygon : shifted)
                    polygon.translate(instance.shift);
                polygons_append(by_z[layer->print_z], std::move(shifted));
            }
        }
    }
    for (auto level = by_z.rbegin(); level != by_z.rend(); ++level) {
        throw_if_canceled(print);
        ExPolygons footprint = union_ex(level->second);
        if (footprint.empty())
            continue;
        const BoundingBox bounds = get_extents(footprint);
        result.levels.push_back({level->first, std::move(footprint), bounds});
    }
    return result;
}

json support_projection_layer(const Print&                  print,
                              const PlateSupportProjection& support,
                              PlateModelProjection&         model,
                              const Layer&                  layer,
                              size_t                        layer_index,
                              const ExPolygons&             target,
                              const Point&                  shift,
                              bool                          complete)
{
    json result = {{"layer_index", layer_index},
                   {"complete", complete},
                   {"model_bottom_z_mm", finite_number(layer.bottom_z())},
                   {"model_print_z_mm", finite_number(layer.print_z)},
                   {"target_area_mm2", nullptr},
                   {"unmatched_target_area_mm2", nullptr},
                   {"support_levels", nullptr}};
    if (!complete)
        return result;

    ExPolygons remaining = target;
    for (ExPolygon& polygon : remaining)
        polygon.translate(shift);
    result["target_area_mm2"] = nonnegative_number(area_mm2(target));
    json levels              = json::array();
    BoundingBox bounds       = get_extents(remaining);
    for (const SupportProjectionLevel& level : support.levels) {
        throw_if_canceled(print);
        if (remaining.empty())
            break;
        if (level.print_z > layer.print_z || !bounds.overlap(level.bounds))
            continue;
        const ExPolygons assigned = intersection_ex(remaining, level.footprint);
        const double     overlap  = area_mm2(assigned);
        if (overlap <= 0.)
            continue;
        levels.push_back({{"support_print_z_mm", level.print_z},
                          {"model_layer_bottom_gap_mm", finite_number(layer.bottom_z() - level.print_z)},
                          {"nearest_projection_area_mm2", nonnegative_number(overlap)},
                          {"intervening_model_projection",
                           intervening_model_projection(print, model, level.print_z, layer.print_z, assigned)}});
        // Each target location belongs only to its highest overlapping support
        // plane. A higher plane elsewhere must not hide a lower overlapping one.
        remaining = diff_ex(remaining, level.footprint);
        bounds    = get_extents(remaining);
    }
    result["unmatched_target_area_mm2"] = nonnegative_number(area_mm2(remaining));
    result["support_levels"]           = std::move(levels);
    return result;
}

struct SourceLayerBands
{
    struct Band
    {
        double lower_z;
        double upper_z;
        double slice_z;
    };

    std::vector<Band>   bands;
    std::vector<double> boundaries;
    bool                complete = false;
};

SourceLayerBands source_layer_bands(const PrintObject& object)
{
    SourceLayerBands result;
    for (const Layer* layer : object.layers()) {
        throw_if_canceled(*object.print());
        // new_layers stores the midpoint and height of the original source
        // band. Print Z has a separate raft offset, including Cura overlap.
        const double lower_z = layer->slice_z - 0.5 * layer->height;
        const double upper_z = layer->slice_z + 0.5 * layer->height;
        // zs_from_layers passes float planes to the mesh slicer. Keep that
        // rounding here, but reconstruct band endpoints from the saved doubles.
        const double slice_z = double(float(layer->slice_z));
        if (!valid_model_layer_geometry(*layer) || !std::isfinite(lower_z) || !std::isfinite(upper_z) ||
            !std::isfinite(slice_z) || upper_z <= lower_z ||
            (!result.bands.empty() && (lower_z < result.bands.back().lower_z || upper_z <= result.bands.back().upper_z ||
                                      slice_z <= result.bands.back().slice_z)))
            return result;
        result.bands.push_back({lower_z, upper_z, slice_z});
        result.boundaries.push_back(lower_z);
        result.boundaries.push_back(upper_z);
    }
    std::sort(result.boundaries.begin(), result.boundaries.end());
    result.boundaries.erase(std::unique(result.boundaries.begin(), result.boundaries.end()), result.boundaries.end());
    result.complete = !result.bands.empty();
    return result;
}

struct SourceSurfaceAlignment
{
    bool         complete = false;
    NumericRange normal_offset;
    NumericRange horizontal_offset;

    void add(const Print& print, const SourceLayerBands& layers, const std::array<Vec3d, 3>& vertices)
    {
        if (!complete)
            return;
        for (const Vec3d& vertex : vertices)
            if (!vertex.allFinite()) {
                complete = false;
                return;
            }
        // Recompute the normal after the slicer's float transform. Transforming
        // the original normal as a vector would be wrong for nonuniform scale.
        const Vec3d  normal = (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]);
        const double length = normal.norm();
        if (!std::isfinite(length) || length <= 0.) {
            complete = false;
            return;
        }
        const double lower_z = std::min({vertices[0].z(), vertices[1].z(), vertices[2].z()});
        const double upper_z = std::max({vertices[0].z(), vertices[1].z(), vertices[2].z()});
        if (lower_z == upper_z) {
            // A source horizontal face is not assigned to a model layer. Even
            // outside the retained range, this is only distance to that grid.
            const auto next = std::lower_bound(layers.boundaries.begin(), layers.boundaries.end(), lower_z);
            double     distance = std::numeric_limits<double>::infinity();
            if (next != layers.boundaries.end())
                distance = std::abs(*next - lower_z);
            if (next != layers.boundaries.begin())
                distance = std::min(distance, std::abs(*std::prev(next) - lower_z));
            horizontal_offset.add(distance);
        } else {
            const double normal_z = std::abs(normal.z()) / length;
            auto         band = std::upper_bound(layers.bands.begin(), layers.bands.end(), lower_z,
                                                 [](double z, const SourceLayerBands::Band& value) { return z < value.upper_z; });
            for (; band != layers.bands.end() && band->lower_z < upper_z; ++band) {
                throw_if_canceled(print);
                const double a = std::max(lower_z, band->lower_z);
                const double b = std::min(upper_z, band->upper_z);
                if (b > a) {
                    // Normal component of moving source points vertically to
                    // this slice plane, not distance to a printed staircase.
                    normal_offset.add(std::max(std::abs(a - band->slice_z), std::abs(b - band->slice_z)) * normal_z);
                }
            }
        }
        complete = normal_offset.invalid_count == 0 && horizontal_offset.invalid_count == 0;
    }

    json to_json(size_t volume_index) const
    {
        return {{"volume_index", volume_index},
                {"complete", complete},
                {"non_horizontal_facet_band_count", complete ? json(normal_offset.count) : json(nullptr)},
                {"max_vertical_projection_normal_offset_mm",
                 complete && normal_offset.count != 0 ? nonnegative_number(normal_offset.maximum) : json(nullptr)},
                {"horizontal_facet_count", complete ? json(horizontal_offset.count) : json(nullptr)},
                {"max_horizontal_boundary_offset_mm",
                 complete && horizontal_offset.count != 0 ? nonnegative_number(horizontal_offset.maximum) : json(nullptr)}};
    }
};

struct SourceVolumeSampling
{
    json                values;
    json                surface_alignment;
    const VolumeSlices* slices       = nullptr;
    bool                complete     = false;
    size_t              source_count = 0;
    size_t              final_count  = 0;
    size_t              path_count   = 0;
};

std::vector<SourceVolumeSampling> source_volume_targets(const PrintObject& object)
{
    std::vector<SourceVolumeSampling> targets;
    const PrintObject*                owner         = object.get_shared_object() == nullptr ? &object : object.get_shared_object();
    const auto&                       owner_volumes = owner->model_object()->volumes;
    const auto&                       cached_slices = owner->firstLayerObjSlice();
    const auto&                       volumes       = object.model_object()->volumes;
    const SourceLayerBands            layer_bands   = source_layer_bands(object);
    for (size_t index = 0; index < volumes.size(); ++index) {
        const ModelVolume& volume = *volumes[index];
        if (!volume.is_model_part())
            continue;
        throw_if_canceled(*object.print());
        SourceVolumeSampling target;
        target.values = {{"volume_index", index},
                         {"z_min_mm", nullptr},
                         {"z_max_mm", nullptr},
                         {"interior_plane_count", nullptr},
                         {"boundary_plane_count", nullptr}};
        // Match the slicer's float transform and zs_from_layers conversion.
        // Only referenced vertices belong to the source surface's Z extent.
        const Transform3f transform = (object.trafo_centered() * volume.get_matrix()).cast<float>();
        const auto&       mesh      = volume.mesh().its;
        float             z_min = 0.f, z_max = 0.f;
        bool              bounds_valid = !mesh.indices.empty();
        bool              initialized  = false;
        SourceSurfaceAlignment alignment;
        alignment.complete = layer_bands.complete && !mesh.indices.empty();
        for (size_t face_index = 0; face_index < mesh.indices.size(); ++face_index) {
            if (face_index % 4096 == 0)
                throw_if_canceled(*object.print());
            const auto&          face = mesh.indices[face_index];
            std::array<Vec3d, 3> vertices;
            for (int corner = 0; corner < 3; ++corner) {
                const Vec3f vertex = transform * mesh.vertices[face[corner]];
                const float z      = vertex.z();
                vertices[corner]   = vertex.cast<double>();
                if (!std::isfinite(z)) {
                    bounds_valid = false;
                    alignment.complete = false;
                    break;
                }
                if (!initialized) {
                    z_min = z_max = z;
                    initialized   = true;
                } else {
                    z_min = std::min(z_min, z);
                    z_max = std::max(z_max, z);
                }
            }
            if (!bounds_valid)
                break;
            alignment.add(*object.print(), layer_bands, vertices);
        }
        target.surface_alignment = alignment.to_json(index);
        if (bounds_valid) {
            target.values["z_min_mm"] = z_min;
            target.values["z_max_mm"] = z_max;
            size_t interior = 0, boundary = 0;
            bool   planes_valid = !object.layers().empty();
            for (const Layer* layer : object.layers()) {
                const float z = float(layer->slice_z);
                if (!std::isfinite(z)) {
                    planes_valid = false;
                    break;
                }
                if (z > z_min && z < z_max)
                    ++interior;
                else if (z == z_min || z == z_max)
                    ++boundary;
            }
            if (planes_valid) {
                target.values["interior_plane_count"] = interior;
                target.values["boundary_plane_count"] = boundary;
            }
        }
        // Shared objects reuse their owner's layers and source cache. The
        // source volume IDs differ, but sharing requires identical volume order.
        if (index < owner_volumes.size()) {
            const ObjectID id    = owner_volumes[index]->id();
            const auto     found = std::find_if(cached_slices.begin(), cached_slices.end(),
                                                [id](const VolumeSlices& slices) { return slices.volume_id == id; });
            if (found != cached_slices.end())
                target.slices = &*found;
        }
        // Slicing removes only a suffix of empty model layers. The raw cache
        // may therefore be longer; missing or shorter caches are not zero data.
        target.complete = !object.layers().empty() && target.slices != nullptr && target.slices->slices.size() >= object.layers().size();
        targets.push_back(std::move(target));
    }
    return targets;
}

struct LayerSlabDepth
{
    struct Slab
    {
        coord_t    bottom, top;
        ExPolygons polygons;
    };
    struct DepthRange
    {
        double       area_sum = 0., uncovered_area_sum = 0.;
        NumericRange depth;
    };
    const Print&                   print;
    const bool                     material_paths = false;
    bool                           complete = true;
    std::vector<Slab>               slabs;
    std::map<coord_t, ExPolygons>   top_targets, bottom_targets;

    static std::optional<coord_t> event_z(double z)
    {
        const long double value = std::round(static_cast<long double>(z) / SCALING_FACTOR);
        // Also exclude the lowest value so reversing Z cannot overflow.
        if (!std::isfinite(value) || value <= static_cast<long double>(std::numeric_limits<coord_t>::lowest()) ||
            value >= static_cast<long double>(std::numeric_limits<coord_t>::max()))
            return std::nullopt;
        return coord_t(value);
    }

    void add(const Layer& layer, const Surface& surface)
    {
        throw_if_canceled(print);
        if (!complete || !surface.is_solid() || surface.empty() || surface.area() == 0. ||
            (material_paths && !surface.is_top() && !surface.is_bottom()))
            return;
        const double thickness = surface.thickness == -1. ? layer.height : surface.thickness;
        if (!std::isfinite(thickness) || thickness <= 0. || !std::isfinite(surface.area()) || surface.area() < 0.) {
            complete = false;
            return;
        }
        const auto bottom = event_z(layer.print_z - thickness);
        const auto top    = event_z(layer.print_z);
        if (!bottom || !top || *bottom >= *top) {
            complete = false;
            return;
        }
        if (surface.is_top())
            top_targets[*top].push_back(surface.expolygon);
        if (surface.is_bottom())
            bottom_targets[*bottom].push_back(surface.expolygon);
        if (!material_paths)
            slabs.push_back({*bottom, *top, {surface.expolygon}});
    }

    void add_paths(const Layer& layer, const PathSlabFootprint& footprint, const ExPolygons& layer_covered)
    {
        complete = complete && footprint.complete;
        if (!complete)
            return;
        const auto top = event_z(layer.print_z);
        if (!top) {
            complete = false;
            return;
        }
        for (const auto& [height, polygons] : footprint.by_height) {
            throw_if_canceled(print);
            const auto bottom = event_z(layer.print_z - double(height));
            if (!bottom || *bottom >= *top) {
                complete = false;
                return;
            }
            // All paths with the same nominal slab share one union; the
            // report never expands the event sweep into one slab per path.
            ExPolygons covered = footprint.by_height.size() == 1 ? layer_covered : union_ex(polygons);
            if (!covered.empty())
                slabs.push_back({*bottom, *top, std::move(covered)});
        }
    }

    DepthRange sweep(bool downward) const
    {
        const auto& seeds = downward ? top_targets : bottom_targets;
        if (seeds.empty())
            return {};
        struct Event
        {
            std::vector<size_t> starts, ends;
            ExPolygons          targets;
        };
        std::map<coord_t, Event> events;
        for (size_t index = 0; index < slabs.size(); ++index) {
            throw_if_canceled(print);
            const Slab&   slab  = slabs[index];
            const coord_t start = downward ? slab.bottom : -slab.top;
            const coord_t end   = downward ? slab.top : -slab.bottom;
            events[start].starts.push_back(index);
            events[end].ends.push_back(index);
        }
        for (const auto& [z, polygons] : seeds)
            events[downward ? z : -z].targets = polygons;
        DepthRange       result;
        std::set<size_t> active;
        ExPolygons       previous;
        // Disjoint XY regions, grouped by the beginning of the uninterrupted
        // column. Intersecting only adjacent layers would incorrectly connect
        // a laterally wandering chain of overlaps.
        std::map<coord_t, ExPolygons> runs;
        for (const auto& [z, event] : events) {
            throw_if_canceled(print);
            const ExPolygons targets     = union_ex(event.targets);
            const double     target_area = area_mm2(targets);
            if (target_area > 0.) {
                result.area_sum += target_area;
                const BoundingBox bounds = get_extents(targets);
                const auto record_first_overlap = [&, query_z = z](auto begin, auto end) {
                    for (auto it = begin; it != end; ++it) {
                        throw_if_canceled(print);
                        if (bounds.overlap(get_extents(it->second)) && area_mm2(intersection_ex(targets, it->second)) > 0.) {
                            result.depth.add(double((static_cast<long double>(query_z) - it->first) * SCALING_FACTOR));
                            return;
                        }
                    }
                    ++result.depth.invalid_count;
                };
                // Query before removing slabs ending here. The extreme start
                // groups give the depth extrema without visiting every run.
                record_first_overlap(runs.begin(), runs.end());
                record_first_overlap(runs.rbegin(), runs.rend());
            }
            for (size_t index : event.ends)
                active.erase(index);
            for (size_t index : event.starts)
                active.insert(index);
            ExPolygons active_polygons;
            for (size_t index : active) {
                throw_if_canceled(print);
                append(active_polygons, slabs[index].polygons);
            }
            ExPolygons       current = union_ex(active_polygons);
            const ExPolygons lost = diff_ex(previous, current);
            if (!lost.empty()) {
                const BoundingBox bounds = get_extents(lost);
                for (auto it = runs.begin(); it != runs.end();) {
                    throw_if_canceled(print);
                    if (bounds.overlap(get_extents(it->second)))
                        it->second = diff_ex(it->second, lost);
                    if (it->second.empty())
                        it = runs.erase(it);
                    else
                        ++it;
                }
            }
            ExPolygons added = diff_ex(current, previous);
            if (!added.empty())
                runs.emplace(z, std::move(added));
            previous = std::move(current);
        }
        return result;
    }

    std::pair<DepthRange, DepthRange> material_ranges() const
    {
        if (top_targets.empty() && bottom_targets.empty())
            return {};
        struct Event
        {
            std::vector<size_t> starts, ends;
        };
        struct Interval
        {
            coord_t                  bottom, top;
            size_t                   single_slab = std::numeric_limits<size_t>::max();
            ExPolygons               merged;
            std::vector<BoundingBox>  bounds;
        };
        std::map<coord_t, Event> events;
        for (size_t index = 0; index < slabs.size(); ++index) {
            throw_if_canceled(print);
            events[slabs[index].bottom].starts.push_back(index);
            events[slabs[index].top].ends.push_back(index);
        }
        std::vector<Interval> intervals;
        std::set<size_t>      active;
        const auto polygons = [&](const Interval& interval) -> const ExPolygons& {
            return interval.single_slab == std::numeric_limits<size_t>::max() ? interval.merged
                                                                            : slabs[interval.single_slab].polygons;
        };
        // Each Z interval is computed once and shared by both directions.
        // Empty intervals retain genuine gaps. A single active slab is already
        // unioned, so keep a reference instead of copying/unioning it again.
        for (auto it = events.begin(); it != events.end() && std::next(it) != events.end(); ++it) {
            throw_if_canceled(print);
            for (size_t index : it->second.ends)
                active.erase(index);
            for (size_t index : it->second.starts)
                active.insert(index);
            Interval interval{it->first, std::next(it)->first};
            if (active.size() == 1)
                interval.single_slab = *active.begin();
            else if (!active.empty()) {
                ExPolygons pieces;
                for (size_t index : active) {
                    throw_if_canceled(print);
                    append(pieces, slabs[index].polygons);
                }
                interval.merged = union_ex(pieces);
            }
            interval.bounds = get_extents_vector(polygons(interval));
            intervals.push_back(std::move(interval));
        }
        const auto measure = [&](bool downward) {
            DepthRange result;
            const auto& targets = downward ? top_targets : bottom_targets;
            auto forward = targets.begin();
            auto reverse = targets.rbegin();
            for (size_t index = 0; index < targets.size(); ++index) {
                // Preserve the original sweep's summation order, including
                // descending source Z for the mirrored bottom queries.
                const auto& [z, seed] = downward ? *forward++ : *reverse++;
                throw_if_canceled(print);
                ExPolygons remaining = union_ex(seed);
                const double target_area = area_mm2(remaining);
                if (target_area <= 0.)
                    continue;
                result.area_sum += target_area;
                // Downward queries own the interval ending at z; upward
                // queries own the interval beginning at z. Point-only contact
                // from the opposite side supplies no positive material depth.
                auto it = downward
                              ? std::lower_bound(intervals.begin(), intervals.end(), z,
                                                 [](const Interval& interval, coord_t value) { return interval.top < value; })
                              : std::upper_bound(intervals.begin(), intervals.end(), z,
                                                 [](coord_t value, const Interval& interval) { return value < interval.top; });
                if (it == intervals.end() || (downward ? it->bottom >= z : it->bottom > z)) {
                    result.uncovered_area_sum += target_area;
                    result.depth.add(0.);
                    continue;
                }
                coord_t boundary = z;
                bool    first = true, have_minimum = false;
                const auto depth = [query_z = z](coord_t end) {
                    return double(std::abs(static_cast<long double>(query_z) - end) * SCALING_FACTOR);
                };
                for (;;) {
                    throw_if_canceled(print);
                    const ExPolygons candidates = bounding_box_candidates(polygons(*it), it->bounds, get_extents(remaining));
                    ExPolygons clipped = intersection_ex(remaining, candidates);
                    if (first) {
                        const double uncovered = area_mm2(diff_ex(remaining, candidates));
                        result.uncovered_area_sum += uncovered;
                        if (uncovered > 0.) {
                            result.depth.add(0.);
                            have_minimum = true;
                        }
                    } else if (!have_minimum &&
                               (area_mm2(clipped) == 0. || area_mm2(diff_ex(remaining, candidates)) > 0.)) {
                        result.depth.add(depth(boundary));
                        have_minimum = true;
                    }
                    if (area_mm2(clipped) == 0.) {
                        result.depth.add(depth(boundary));
                        break;
                    }
                    remaining = std::move(clipped);
                    first = false;
                    boundary = downward ? it->bottom : it->top;
                    if (downward ? it == intervals.begin() : std::next(it) == intervals.end()) {
                        result.depth.add(depth(boundary));
                        break;
                    }
                    if (downward)
                        --it;
                    else
                        ++it;
                }
            }
            return result;
        };
        return {measure(true), measure(false)};
    }

    json to_json() const
    {
        const auto ranges = !complete ? std::pair<DepthRange, DepthRange>{}
                                      : material_paths ? material_ranges() : std::make_pair(sweep(true), sweep(false));
        const auto& [top, bottom] = ranges;
        const bool       valid  = complete && top.depth.invalid_count == 0 && bottom.depth.invalid_count == 0 &&
                                  std::isfinite(top.area_sum) && std::isfinite(bottom.area_sum) &&
                                  std::isfinite(top.uncovered_area_sum) && std::isfinite(bottom.uncovered_area_sum);
        const auto values = [&](const DepthRange& range) {
            json result{{"target_area_sum_mm2", valid ? json(range.area_sum) : json(nullptr)},
                        {"continuous_depth_mm",
                         {{"min", valid && range.depth.count != 0 ? json(range.depth.minimum) : json(nullptr)},
                          {"max", valid && range.depth.count != 0 ? json(range.depth.maximum) : json(nullptr)}}}};
            if (material_paths)
                result["uncovered_start_area_sum_mm2"] = valid ? json(range.uncovered_area_sum) : json(nullptr);
            return result;
        };
        return {{"scope", material_paths ? "per_instance_fixed_xy_model_path_slabs_before_gcode"
                                        : "per_instance_continuous_solid_fill_surface_slabs"},
                {"z_resolution_mm", SCALING_FACTOR}, {"complete", valid}, {"top", values(top)}, {"bottom", values(bottom)}};
    }
};

struct JointLowerPathSlabs
{
    struct Source
    {
        const PrintObject* object;
        const Layer*       layer;
        bool               support;
    };
    struct Planes
    {
        bool                                                 complete = false;
        std::map<coord_t, ExPolygons>                        bottoms;
        std::map<coord_t, std::vector<const ExtrusionPath*>> bridges;
    };
    struct Mask
    {
        bool                           complete = true;
        ExPolygons                     polygons;
        std::vector<BoundingBox>       bounds;
        std::vector<FootprintBoundary> boundaries;
    };
    const Print&                               print;
    LayerPathCache&                            paths;
    bool                                       finite_source_z = true;
    std::map<double, std::vector<Source>>      sources;
    std::map<const Layer*, Planes>             planes;
    std::map<std::pair<double, coord_t>, Mask> masks;

    JointLowerPathSlabs(const Print& print, LayerPathCache& paths) : print(print), paths(paths)
    {
        for (const PrintObject* object : print.objects()) {
            if (object->instances().empty())
                continue;
            const auto append = [&](const Layer* layer, bool support) {
                throw_if_canceled(print);
                if (!std::isfinite(layer->print_z))
                    finite_source_z = false;
                else
                    sources[layer->print_z].push_back({object, layer, support});
            };
            for (const Layer* layer : object->layers())
                append(layer, false);
            for (const SupportLayer* layer : object->support_layers())
                append(layer, true);
        }
    }

    const Planes& layer_planes(const Layer& layer, bool support = false)
    {
        auto    inserted = planes.try_emplace(&layer);
        Planes& result   = inserted.first->second;
        if (!inserted.second)
            return result;
        const LayerPathFootprint& cached = paths.get(layer, support);
        const auto                top    = LayerSlabDepth::event_z(layer.print_z);
        if (!top || !cached.heights.complete || cached.footprint.invalid_path_count != 0 || (!support && !valid_model_layer_geometry(layer)))
            return result;
        std::map<coord_t, Polygons> by_bottom;
        for (const auto& [height, polygons] : cached.heights.by_height) {
            throw_if_canceled(print);
            const auto bottom = LayerSlabDepth::event_z(layer.print_z - double(height));
            if (!bottom || *bottom >= *top)
                return result;
            polygons_append(by_bottom[*bottom], polygons);
        }
        for (const auto& [bottom, polygons] : by_bottom) {
            throw_if_canceled(print);
            result.bottoms.emplace(bottom, by_bottom.size() == 1 ? cached.covered : union_ex(polygons));
        }
        for (const ExtrusionPath* path : cached.footprint.external_bridge_paths) {
            throw_if_canceled(print);
            result.bridges[*LayerSlabDepth::event_z(layer.print_z - double(path->height))].push_back(path);
        }
        result.complete = true;
        return result;
    }

    const Mask& lower_mask(double print_z, coord_t bottom)
    {
        auto  inserted = masks.try_emplace(std::make_pair(print_z, bottom));
        Mask& result   = inserted.first->second;
        if (!inserted.second)
            return result;
        result.complete = finite_source_z;
        Polygons combined;
        // Strict source print-Z, not G-code chronology. Quantization applies
        // only to nominal slab endpoints. Older tops below b cannot affect b.
        auto source_level = sources.lower_bound(print_z);
        while (result.complete && source_level != sources.begin()) {
            --source_level;
            throw_if_canceled(print);
            const auto top = LayerSlabDepth::event_z(source_level->first);
            if (!top) {
                if (source_level->first < unscale<double>(bottom))
                    break;
                result.complete = false;
                break;
            }
            if (*top < bottom)
                break;
            for (const Source& source : source_level->second) {
                throw_if_canceled(print);
                const Planes& local = layer_planes(*source.layer, source.support);
                if (!local.complete) {
                    result.complete = false;
                    break;
                }
                for (auto slab = local.bottoms.begin(); slab != local.bottoms.lower_bound(bottom); ++slab)
                    for (const PrintInstance& instance : source.object->instances()) {
                        throw_if_canceled(print);
                        ExPolygons shifted = slab->second;
                        for (ExPolygon& polygon : shifted)
                            polygon.translate(instance.shift);
                        polygons_append(combined, to_polygons(shifted));
                    }
            }
        }
        if (result.complete) {
            throw_if_canceled(print);
            result.polygons = union_ex(combined);
            result.bounds   = get_extents_vector(result.polygons);
            for (size_t index = 0; index < result.polygons.size(); ++index) {
                throw_if_canceled(print);
                const ExPolygon& polygon = result.polygons[index];
                result.boundaries.push_back({&polygon, result.bounds[index], polygon.lines()});
            }
        }
        return result;
    }

    json layer(const Layer& layer, size_t layer_index, const Point& shift)
    {
        const Planes& target = layer_planes(layer);
        json          result{{"layer_index", layer_index},
                             {"print_z_mm", finite_number(layer.print_z)},
                             {"complete", target.complete},
                             {"bottom_planes", target.complete ? json::array() : json(nullptr)}};
        if (!target.complete)
            return result;
        for (const auto& [bottom, polygons] : target.bottoms) {
            throw_if_canceled(print);
            const Mask& mask    = lower_mask(layer.print_z, bottom);
            ExPolygons  shifted = polygons;
            for (ExPolygon& polygon : shifted)
                polygon.translate(shift);
            const double area      = area_mm2(shifted);
            double       uncovered = 0.;
            if (mask.complete) {
                throw_if_canceled(print);
                uncovered = area_mm2(diff_ex(shifted, bounding_box_candidates(mask.polygons, mask.bounds, get_extents(shifted))));
                // Only cap floating area accumulation, not a geometric gap.
                uncovered = std::min(area, uncovered);
            }
            const auto                              bridges = target.bridges.find(bottom);
            const std::vector<const ExtrusionPath*> empty;
            result["bottom_planes"].push_back(
                {{"bottom_z_mm", unscale<double>(bottom)},
                 {"complete", mask.complete},
                 {"target_area_mm2", mask.complete ? nonnegative_number(area) : json(nullptr)},
                 {"uncovered_area_mm2", mask.complete ? nonnegative_number(uncovered) : json(nullptr)},
                 {"external_bridge", bridge_coverage_runs(print, bridges == target.bridges.end() ? empty : bridges->second, mask.boundaries,
                                                          mask.complete, shift)}});
            if (!mask.complete)
                result["complete"] = false;
        }
        return result;
    }
};

// Only this observation uses exact sets. The slicer's polygons and all older
// report fields retain their existing representation and operations.
using ConnectivityMeasure = SliceAnalysisGeometry::Rational;
using ConnectivityRegion  = SliceAnalysisGeometry::Region;
using ConnectivityRegions = SliceAnalysisGeometry::Regions;

struct ConnectivityBounds
{
    std::array<ConnectivityMeasure, 2> min, max;

    void merge(const ConnectivityBounds& other)
    {
        for (size_t axis = 0; axis < 2; ++axis) {
            min[axis] = std::min(min[axis], other.min[axis]);
            max[axis] = std::max(max[axis], other.max[axis]);
        }
    }
};

double connectivity_volume_mm3(const ConnectivityMeasure& twice_scaled_volume)
{
    return twice_scaled_volume.convert_to<double>() * 0.5 * SCALING_FACTOR * SCALING_FACTOR * SCALING_FACTOR;
}

ConnectivityMeasure connectivity_sum(const Print& print, const std::vector<const ConnectivityMeasure*>& values, size_t begin, size_t end)
{
    throw_if_canceled(print);
    if (begin == end)
        return 0;
    if (end - begin == 1)
        return *values[begin];
    // Keep exact arithmetic, but avoid repeatedly adding small fractions to
    // one ever-growing denominator. Missing values remain owned by the caller.
    const size_t middle = begin + (end - begin) / 2;
    auto         left   = connectivity_sum(print, values, begin, middle);
    auto         right  = connectivity_sum(print, values, middle, end);
    throw_if_canceled(print);
    return left + right;
}

struct ConnectivityGraph
{
    struct Node
    {
        size_t              parent, size = 1;
        ConnectivityBounds  bounds;
        coord_t             bottom, top;
        ConnectivityMeasure volume = 0;
    };
    std::vector<Node> nodes;

    size_t root(size_t index)
    {
        while (nodes[index].parent != index) {
            nodes[index].parent = nodes[nodes[index].parent].parent;
            index               = nodes[index].parent;
        }
        return index;
    }

    size_t add(const ConnectivityRegion& cell, coord_t z)
    {
        const size_t index = nodes.size();
        nodes.push_back({index, 1, {cell.min, cell.max}, z, z, 0});
        return index;
    }

    void join(size_t first, size_t second)
    {
        first  = root(first);
        second = root(second);
        if (first == second)
            return;
        if (nodes[first].size < nodes[second].size)
            std::swap(first, second);
        Node& parent = nodes[first];
        Node& child  = nodes[second];
        child.parent = first;
        parent.size += child.size;
        parent.bounds.merge(child.bounds);
        parent.bottom = std::min(parent.bottom, child.bottom);
        parent.top    = std::max(parent.top, child.top);
        parent.volume += child.volume;
    }

    void contribute(size_t index, coord_t bottom, coord_t top, const ConnectivityMeasure& volume)
    {
        Node& node  = nodes[root(index)];
        node.bottom = std::min(node.bottom, bottom);
        node.top    = std::max(node.top, top);
        node.volume += volume;
    }
};

struct ConnectivityFrame
{
    using Tree = AABBTreeIndirect::Tree<2, double>;
    ConnectivityRegions cells;
    std::vector<size_t> nodes;
    Tree                tree;

    static Tree::BoundingBox enclosing_box(const ConnectivityRegion& cell)
    {
        const auto enclosing = [](const ConnectivityMeasure& value, bool upper) {
            double converted = value.convert_to<double>();
            // Broad-phase boxes only: round outwards and verify the enclosure
            // exactly. Never use these doubles to measure or join regions.
            while (upper ? ConnectivityMeasure(converted) < value : ConnectivityMeasure(converted) > value)
                converted = std::nextafter(converted,
                                           upper ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity());
            return converted;
        };
        return Tree::BoundingBox(Vec2d(enclosing(cell.min[0], false), enclosing(cell.min[1], false)),
                                 Vec2d(enclosing(cell.max[0], true), enclosing(cell.max[1], true)));
    }

    void index()
    {
        struct Entry
        {
            size_t                   value;
            Tree::BoundingBox        box;
            Tree::VectorType         center;
            size_t                   idx() const { return value; }
            const Tree::BoundingBox& bbox() const { return box; }
            const Tree::VectorType&  centroid() const { return center; }
        };
        std::vector<Entry> entries;
        entries.reserve(cells.size());
        for (size_t index = 0; index < cells.size(); ++index) {
            const auto box = enclosing_box(cells[index]);
            entries.push_back({index, box, box.min() * 0.5 + box.max() * 0.5});
        }
        tree.build(std::move(entries));
    }

    std::vector<size_t> candidates(const Print& print, const ConnectivityRegion& cell) const
    {
        std::vector<size_t>     result;
        const Tree::BoundingBox box = enclosing_box(cell);
        AABBTreeIndirect::traverse(tree, AABBTreeIndirect::intersecting(box), [&](const Tree::Node& node) {
            throw_if_canceled(print);
            result.push_back(node.idx);
            return true;
        });
        std::sort(result.begin(), result.end());
        return result;
    }
};

json nominal_target_path_connectivity(const Print& print, const PrintObject& object, JointLowerPathSlabs& joint)
{
    json result{{"scope", "per_instance_compensated_region_section_and_model_path_slabs"},
                {"coordinate_frame", "print_object_local_xy_and_print_z"},
                {"z_resolution_mm", SCALING_FACTOR},
                {"complete", false},
                {"target_components", nullptr},
                {"cross_target_joins", nullptr}};
    if (object.layers().empty())
        return result;

    struct Slab
    {
        coord_t             bottom, top;
        ConnectivityRegions cells;
    };
    struct Event
    {
        std::vector<size_t> target_starts, target_ends, material_starts, material_ends;
    };
    struct Association
    {
        size_t target, material, clipped;
    };
    struct Missing
    {
        size_t                             target;
        std::array<ConnectivityMeasure, 2> min, max;
        coord_t                            bottom, top;
        ConnectivityMeasure                volume;
    };
    std::vector<Slab>        targets, materials;
    std::map<coord_t, Event> events;
    const auto               cancel   = [&] { throw_if_canceled(print); };
    const auto               add_slab = [&](std::vector<Slab>& slabs, coord_t bottom, coord_t top, ConnectivityRegions cells, bool target) {
        if (cells.empty())
            return;
        const size_t index = slabs.size();
        slabs.push_back({bottom, top, std::move(cells)});
        (target ? events[bottom].target_starts : events[bottom].material_starts).push_back(index);
        (target ? events[top].target_ends : events[top].material_ends).push_back(index);
    };
    for (const Layer* layer : object.layers()) {
        throw_if_canceled(print);
        if (!valid_model_layer_geometry(*layer))
            return result;
        const auto bottom = LayerSlabDepth::event_z(layer->bottom_z());
        const auto top    = LayerSlabDepth::event_z(layer->print_z);
        if (!bottom || !top || *bottom >= *top)
            return result;
        std::vector<const ExPolygon*> section;
        for (const LayerRegion* region : layer->regions()) {
            throw_if_canceled(print);
            // lslices may intentionally contain the pre-elephant-foot shape.
            // Current region slices retain compensation and region clipping.
            for (const Surface& surface : region->slices.surfaces)
                section.push_back(&surface.expolygon);
        }
        add_slab(targets, *bottom, *top, SliceAnalysisGeometry::union_expolygons(section, cancel), true);
        const LayerPathFootprint& cached = joint.paths.get(*layer);
        if (!cached.heights.complete || cached.footprint.invalid_path_count != 0)
            return result;
        std::map<coord_t, std::vector<Polygons>> by_bottom;
        for (const auto& path : cached.heights.paths) {
            cancel();
            const auto path_bottom = LayerSlabDepth::event_z(layer->print_z - double(path.height));
            if (!path_bottom || *path_bottom >= *top)
                return result;
            const Polygons& all = cached.heights.by_height.at(path.height);
            by_bottom[*path_bottom].emplace_back(all.begin() + path.begin, all.begin() + path.end);
        }
        for (auto& [path_bottom, groups] : by_bottom)
            add_slab(materials, path_bottom, *top, SliceAnalysisGeometry::union_nonzero_groups(groups, cancel), false);
    }

    ConnectivityGraph        target_graph, material_graph, clipped_graph;
    ConnectivityFrame        target_frame, material_frame, clipped_frame;
    std::set<size_t>         active_targets, active_materials;
    std::vector<Association> associations;
    std::vector<Missing>     missing;
    bool                     valid  = true;
    const auto               volume = [&](const ConnectivityRegion& cell, coord_t bottom, coord_t top) {
        const ConnectivityMeasure value = cell.twice_area * (ConnectivityMeasure(top) - bottom);
        valid                           = valid && value > 0;
        return value;
    };
    const auto connect = [&](const ConnectivityFrame& previous, const ConnectivityFrame& current, ConnectivityGraph& graph,
                             const auto& compatible) {
        for (size_t index = 0; index < current.cells.size(); ++index) {
            throw_if_canceled(print);
            for (size_t candidate : previous.candidates(print, current.cells[index])) {
                throw_if_canceled(print);
                if (graph.root(previous.nodes[candidate]) == graph.root(current.nodes[index]))
                    continue;
                if (!compatible(previous.nodes[candidate], current.nodes[index]))
                    continue;
                // A horizontal joining face must have positive XY area.
                // Boundary-only contact supplies no face, even at identical Z.
                if (SliceAnalysisGeometry::positive_area_overlap(current.cells[index], previous.cells[candidate], cancel))
                    graph.join(previous.nodes[candidate], current.nodes[index]);
            }
        }
    };
    const auto replace_frame = [&](const std::vector<Slab>& slabs, const std::set<size_t>& active, coord_t z, ConnectivityFrame& frame,
                                   ConnectivityGraph& graph) {
        ConnectivityFrame next;
        if (active.size() == 1)
            next.cells = slabs[*active.begin()].cells; // Already normalized; no additional Boolean operation.
        else if (!active.empty()) {
            ConnectivityRegions pieces;
            for (size_t index : active) {
                throw_if_canceled(print);
                pieces.insert(pieces.end(), slabs[index].cells.begin(), slabs[index].cells.end());
            }
            next.cells = SliceAnalysisGeometry::union_regions(pieces, cancel);
        }
        next.index();
        for (const ConnectivityRegion& cell : next.cells)
            next.nodes.push_back(graph.add(cell, z));
        connect(frame, next, graph, [](size_t, size_t) { return true; });
        frame = std::move(next); // Empty intervals must clear the previous cells too.
    };
    const auto contribute = [&](const ConnectivityFrame& frame, ConnectivityGraph& graph, coord_t bottom, coord_t top) {
        for (size_t index = 0; index < frame.cells.size(); ++index) {
            throw_if_canceled(print);
            graph.contribute(frame.nodes[index], bottom, top, volume(frame.cells[index], bottom, top));
        }
    };
    for (auto it = events.begin(); it != events.end() && std::next(it) != events.end(); ++it) {
        throw_if_canceled(print);
        const coord_t bottom = it->first, top = std::next(it)->first;
        const Event&  event = it->second;
        for (size_t index : event.target_ends)
            active_targets.erase(index);
        for (size_t index : event.target_starts)
            active_targets.insert(index);
        for (size_t index : event.material_ends)
            active_materials.erase(index);
        for (size_t index : event.material_starts)
            active_materials.insert(index);
        if (!event.target_starts.empty() || !event.target_ends.empty())
            replace_frame(targets, active_targets, bottom, target_frame, target_graph);
        if (!event.material_starts.empty() || !event.material_ends.empty())
            replace_frame(materials, active_materials, bottom, material_frame, material_graph);
        contribute(target_frame, target_graph, bottom, top);

        ConnectivityFrame clipped;
        for (size_t target = 0; target < target_frame.cells.size(); ++target) {
            throw_if_canceled(print);
            ConnectivityRegions material_candidates;
            for (size_t material : material_frame.candidates(print, target_frame.cells[target])) {
                throw_if_canceled(print);
                const ConnectivityRegion& candidate = material_frame.cells[material];
                auto                      pieces = SliceAnalysisGeometry::intersect_regions(target_frame.cells[target], candidate, cancel);
                // An empty exact intersection cannot remove positive area from T.
                if (!pieces.empty())
                    material_candidates.push_back(candidate);
                for (ConnectivityRegion& piece : pieces) {
                    const size_t node = clipped_graph.add(piece, bottom);
                    associations.push_back({target_frame.nodes[target], material_frame.nodes[material], node});
                    clipped.nodes.push_back(node);
                    clipped.cells.push_back(std::move(piece));
                }
            }
            const auto          absent = SliceAnalysisGeometry::subtract_regions(target_frame.cells[target], material_candidates, cancel);
            ConnectivityMeasure absent_area = 0;
            for (const auto& cell : absent) {
                throw_if_canceled(print);
                absent_area += cell.twice_area;
                missing.push_back(
                    {target_frame.nodes[target], cell.min, cell.max, bottom, top, cell.twice_area * (ConnectivityMeasure(top) - bottom)});
            }
            if (absent_area > target_frame.cells[target].twice_area)
                return result;
        }
        clipped.index();
        connect(clipped_frame, clipped, clipped_graph, [&](size_t previous, size_t current) {
            // C is a subset of both T and M. Their cross-Z joins are already
            // complete for this interval; different parent roots exclude a C join.
            const Association& first  = associations[previous];
            const Association& second = associations[current];
            return target_graph.root(first.target) == target_graph.root(second.target) &&
                   material_graph.root(first.material) == material_graph.root(second.material);
        });
        clipped_frame = std::move(clipped);
    }
    if (!valid)
        return result;

    std::map<size_t, size_t> target_indices, material_indices;
    for (size_t index = 0; index < target_graph.nodes.size(); ++index)
        if (target_graph.root(index) == index)
            target_indices.emplace(index, target_indices.size());
    for (size_t index = 0; index < material_graph.nodes.size(); ++index)
        if (material_graph.root(index) == index)
            material_indices.emplace(index, material_indices.size());
    std::vector<std::set<size_t>> clipped_components(target_indices.size()), joined_targets(material_indices.size());
    for (const Association& association : associations) {
        throw_if_canceled(print);
        const size_t target = target_indices.at(target_graph.root(association.target));
        clipped_components[target].insert(clipped_graph.root(association.clipped));
        joined_targets[material_indices.at(material_graph.root(association.material))].insert(target);
    }
    const auto bounds_json = [](const ConnectivityBounds& bounds, coord_t bottom, coord_t top) -> json {
        const auto coordinate_mm = [](const ConnectivityMeasure& coordinate) { return coordinate.convert_to<double>() * SCALING_FACTOR; };
        const std::array<double, 3> minimum{coordinate_mm(bounds.min[0]), coordinate_mm(bounds.min[1]), unscale<double>(bottom)};
        const std::array<double, 3> maximum{coordinate_mm(bounds.max[0]), coordinate_mm(bounds.max[1]), unscale<double>(top)};
        // Exact positive regions may be too narrow for distinct JSON double
        // coordinates. Do not enlarge them or publish zero-width boxes.
        for (size_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis] >= maximum[axis])
                return nullptr;
        return {{"min", minimum}, {"max", maximum}};
    };
    std::vector<std::vector<const ConnectivityMeasure*>> uncovered(target_indices.size());
    std::vector<json>                                    missing_regions(target_indices.size(), json::array());
    for (const Missing& cell : missing) {
        throw_if_canceled(print);
        const size_t index = target_indices.at(target_graph.root(cell.target));
        uncovered[index].push_back(&cell.volume);
        const double value = connectivity_volume_mm3(cell.volume);
        if (!std::isfinite(value) || value <= 0.)
            return result;
        json bounds = bounds_json({cell.min, cell.max}, cell.bottom, cell.top);
        if (bounds.is_null())
            return result;
        missing_regions[index].push_back({{"bounds_mm", std::move(bounds)}, {"volume_mm3", value}});
    }
    json components = json::array(), joins = json::array();
    for (const auto& [root, index] : target_indices) {
        throw_if_canceled(print);
        const ConnectivityGraph::Node& node           = target_graph.nodes[root];
        const ConnectivityMeasure      absent_volume  = connectivity_sum(print, uncovered[index], 0, uncovered[index].size());
        const double                   target_volume  = connectivity_volume_mm3(node.volume);
        const double                   missing_volume = connectivity_volume_mm3(absent_volume);
        if (!std::isfinite(target_volume) || target_volume <= 0. || !std::isfinite(missing_volume) || missing_volume < 0. ||
            absent_volume > node.volume)
            return result;
        json bounds = bounds_json(node.bounds, node.bottom, node.top);
        if (bounds.is_null())
            return result;
        components.push_back({{"component_index", index},
                              {"bounds_mm", std::move(bounds)},
                              {"target_volume_mm3", target_volume},
                              {"uncovered_volume_mm3", missing_volume},
                              {"clipped_path_component_count", clipped_components[index].size()},
                              {"missing_slab_regions", std::move(missing_regions[index])}});
    }
    for (size_t index = 0; index < joined_targets.size(); ++index)
        if (joined_targets[index].size() > 1)
            joins.push_back({{"path_component_index", index}, {"target_component_indices", joined_targets[index]}});
    result["complete"]           = true;
    result["target_components"]  = std::move(components);
    result["cross_target_joins"] = std::move(joins);
    return result;
}

json analyze_model_layers(const Print&                  print,
                          const PrintObject&            object,
                          const PlateSupportProjection& support,
                          PlateModelProjection&         model,
                          JointLowerPathSlabs&          joint)
{
    json                              layers            = json::array();
    json                              cross_layers      = json::array();
    json                              external_bridges  = json::array();
    json                              support_instances = json::array();
    json                              joint_instances   = json::array();
    const Layer*                      previous_layer    = nullptr;
    ExPolygons                        previous_section;
    std::vector<BoundingBox>          previous_section_bounds;
    ExPolygons                        previous_covered;
    bool                              previous_footprint_complete = false;
    std::array<BridgePathStats, 3>    bridge_totals;
    bool                              bridge_complete = !object.layers().empty();
    PlannedPerimeterLoops             perimeter_loops{print};
    LayerSlabDepth                    solid_depth{print};
    LayerSlabDepth                    material_depth{print, true};
    std::vector<SourceVolumeSampling> source_volumes = source_volume_targets(object);
    perimeter_loops.complete                         = !object.layers().empty();
    solid_depth.complete                             = !object.layers().empty();
    material_depth.complete                          = !object.layers().empty();
    for (size_t index = 0; index < object.instances().size(); ++index) {
        support_instances.push_back({{"instance_index", index}, {"layers", json::array()}});
        joint_instances.push_back({{"instance_index", index}, {"layers", json::array()}});
    }
    for (const Layer* layer : object.layers()) {
        throw_if_canceled(print);
        const LayerPathFootprint& cached         = joint.paths.get(*layer);
        const PlannedFootprint&   footprint      = cached.footprint;
        const PathSlabFootprint&  material_slabs = cached.heights;
        perimeter_loops.merge(cached.loops);
        ExPolygons top_target, bottom_target, solid_target;
        for (const LayerRegion* region : layer->regions()) {
            throw_if_canceled(print);
            // thin_fills are copied into fills, not separately printed.
            // These are configured fill targets, not the whole model section:
            // sparse infill and intentionally empty interiors are excluded.
            for (const Surface& surface : region->fill_surfaces.surfaces) {
                solid_depth.add(*layer, surface);
                material_depth.add(*layer, surface);
                if (surface.is_top())
                    top_target.push_back(surface.expolygon);
                if (surface.is_bottom())
                    bottom_target.push_back(surface.expolygon);
                if (surface.is_solid())
                    solid_target.push_back(surface.expolygon);
            }
        }
        throw_if_canceled(print);
        ExPolygons                     section              = union_ex(layer->lslices);
        ExPolygons                     covered              = cached.covered;
        std::vector<BoundingBox>       section_bounds       = get_extents_vector(section);
        const std::vector<BoundingBox> covered_bounds       = get_extents_vector(covered);
        const bool                     layer_geometry_valid = valid_model_layer_geometry(*layer);
        perimeter_loops.complete                            = perimeter_loops.complete && layer_geometry_valid;
        solid_depth.complete                                = solid_depth.complete && layer_geometry_valid;
        material_depth.complete                             = material_depth.complete && layer_geometry_valid;
        material_depth.add_paths(*layer, material_slabs, covered);
        for (SourceVolumeSampling& target : source_volumes) {
            throw_if_canceled(print);
            target.complete = target.complete && layer_geometry_valid && footprint.invalid_path_count == 0;
            if (!target.complete)
                continue;
            const ExPolygons& source = target.slices->slices[layers.size()];
            if (area_mm2(source) > 0.) {
                ++target.source_count;
                const ExPolygons overlap = intersection_ex(source, section);
                if (area_mm2(overlap) > 0.) {
                    ++target.final_count;
                    if (area_mm2(intersection_ex(overlap, covered)) > 0.)
                        ++target.path_count;
                }
            }
        }
        const double previous_gap = previous_layer == nullptr ? 0. : layer->bottom_z() - previous_layer->print_z;
        // CuraV1 deliberately overlaps the second model layer with the first.
        // A signed Z gap is metadata, not a validity condition for XY sections.
        const bool cross_complete = previous_layer != nullptr && layer_geometry_valid && valid_model_layer_geometry(*previous_layer) &&
                                    layer->print_z > previous_layer->print_z && layer->slice_z > previous_layer->slice_z &&
                                    std::isfinite(previous_gap);
        const ExPolygons outside_previous    = cross_complete ? diff_ex(section, previous_section) : ExPolygons{};
        size_t           no_overlap_count    = 0;
        double           no_overlap_area     = 0.;
        const ExPolygons holes               = bounded_voids(section);
        const bool       complete            = layer_geometry_valid && footprint.invalid_path_count == 0;
        external_bridges.push_back(external_bridge_projection_layer(print, *layer, layers.size(), previous_layer, footprint,
                                                                    previous_covered,
                                                                    cross_complete && complete && previous_footprint_complete));
        const bool       projection_complete = complete && support.complete && (previous_layer == nullptr || cross_complete);
        const ExPolygons projection_target   = projection_complete ?
                                                   intersection_ex(previous_layer == nullptr ? section : outside_previous, covered) :
                                                   ExPolygons{};
        for (size_t index = 0; index < object.instances().size(); ++index) {
            throw_if_canceled(print);
            support_instances[index]["layers"].push_back(support_projection_layer(print, support, model, *layer, layers.size(),
                                                                                  projection_target, object.instances()[index].shift,
                                                                                  projection_complete));
            joint_instances[index]["layers"].push_back(joint.layer(*layer, layers.size(), object.instances()[index].shift));
        }
        size_t unprinted_count          = 0;
        double unprinted_area           = 0.;
        size_t multiple_planned_regions = 0;
        for (size_t island_index = 0; island_index < section.size(); ++island_index) {
            throw_if_canceled(print);
            const ExPolygon&   island          = section[island_index];
            const BoundingBox& bounds          = section_bounds[island_index];
            const size_t       planned_regions = positive_region_count(
                intersection_ex(ExPolygons{island}, bounding_box_candidates(covered, covered_bounds, bounds)));
            if (planned_regions == 0) {
                ++unprinted_count;
                unprinted_area += island.area() * SCALING_FACTOR * SCALING_FACTOR;
            }
            if (planned_regions > 1)
                ++multiple_planned_regions;
            if (cross_complete &&
                intersection_ex(ExPolygons{island}, bounding_box_candidates(previous_section, previous_section_bounds, bounds)).empty()) {
                ++no_overlap_count;
                no_overlap_area += island.area() * SCALING_FACTOR * SCALING_FACTOR;
            }
        }
        size_t multi_island_planned_regions = 0;
        if (complete && section.size() > 1) {
            // Keep the full footprint: clipping to section first would erase
            // material crossing the negative space between separate islands.
            for (const ExPolygon& planned_region : covered) {
                throw_if_canceled(print);
                const BoundingBox bounds   = get_extents(planned_region);
                size_t            overlaps = 0;
                for (size_t index = 0; index < section.size(); ++index) {
                    throw_if_canceled(print);
                    if (bounds.overlap(section_bounds[index]) &&
                        area_mm2(intersection_ex(ExPolygons{planned_region}, ExPolygons{section[index]})) > 0.) {
                        if (++overlaps == 2) {
                            ++multi_island_planned_regions;
                            break;
                        }
                    }
                }
            }
        }
        size_t fully_covered_voids = 0;
        size_t split_voids         = 0;
        if (complete)
            for (const ExPolygon& hole : holes) {
                throw_if_canceled(print);
                if (hole.area() <= 0.)
                    continue;
                const size_t remaining_regions = positive_region_count(
                    diff_ex(ExPolygons{hole}, bounding_box_candidates(covered, covered_bounds, get_extents(hole))));
                if (remaining_regions == 0)
                    ++fully_covered_voids;
                else if (remaining_regions > 1)
                    ++split_voids;
            }
        size_t enclosed_exterior_regions = 0;
        double enclosed_exterior_area    = 0.;
        if (complete) {
            const ExPolygons planned_voids = bounded_voids(covered);
            if (!planned_voids.empty()) {
                throw_if_canceled(print);
                // Only empty space actually bounded by planned paths counts.
                // Source material and its already-bounded voids are not the
                // source exterior; neither is used to fill missing path walls.
                const ExPolygons exterior = diff_ex(planned_voids, union_ex(section, holes));
                enclosed_exterior_area    = area_mm2(exterior);
                if (enclosed_exterior_area > 0.) {
                    const BoundingBox exterior_bounds = get_extents(exterior);
                    for (const ExPolygon& planned_void : planned_voids) {
                        throw_if_canceled(print);
                        if (get_extents(planned_void).overlap(exterior_bounds) &&
                            area_mm2(intersection_ex(ExPolygons{planned_void}, exterior)) > 0.)
                            ++enclosed_exterior_regions;
                    }
                }
            }
        }
        cross_layers.push_back(
            {{"layer_index", cross_layers.size()},
             {"slice_z_mm", finite_number(layer->slice_z)},
             {"print_z_mm", finite_number(layer->print_z)},
             {"bottom_z_mm", finite_number(layer->bottom_z())},
             {"height_mm", nonnegative_number(layer->height)},
             {"previous_layer_index", previous_layer == nullptr ? json(nullptr) : json(cross_layers.size() - 1)},
             {"previous_slice_z_mm", previous_layer == nullptr ? json(nullptr) : finite_number(previous_layer->slice_z)},
             {"previous_print_z_mm", previous_layer == nullptr ? json(nullptr) : finite_number(previous_layer->print_z)},
             {"previous_model_gap_mm", previous_layer == nullptr ? json(nullptr) : finite_number(previous_gap)},
             {"complete", cross_complete},
             {"section_area_mm2", layer_geometry_valid ? nonnegative_number(area_mm2(section)) : json(nullptr)},
             {"previous_model_overlap_area_mm2",
              cross_complete ? nonnegative_number(area_mm2(intersection_ex(section, previous_section))) : json(nullptr)},
             {"outside_previous_model_area_mm2",
              cross_complete ? nonnegative_number(area_mm2(outside_previous)) : json(nullptr)},
             {"no_previous_overlap_island_count", cross_complete ? json(no_overlap_count) : json(nullptr)},
             {"no_previous_overlap_island_area_mm2", cross_complete ? nonnegative_number(no_overlap_area) : json(nullptr)}});
        bridge_complete = bridge_complete && layer_geometry_valid && footprint.invalid_path_count == 0;
        for (size_t role = 0; role < bridge_totals.size(); ++role) {
            throw_if_canceled(print);
            const BridgePathStats& stats = footprint.bridge_stats[role];
            BridgePathStats&       total = bridge_totals[role];
            total.path_count += stats.path_count;
            total.path_length_mm += stats.path_length_mm;
            total.excluded_path_count += stats.excluded_path_count;
            total.invalid_path_count += stats.invalid_path_count;
            // Layers at different Z are not one XY region. Union duplicates
            // within each layer/role, then add those independent layer areas.
            total.layer_area_sum_mm2 += area_mm2(union_ex(footprint.bridge_polygons[role]));
        }
        top_target               = union_ex(top_target);
        bottom_target            = union_ex(bottom_target);
        solid_target             = union_ex(solid_target);
        const auto coverage_area = [complete](double area) { return complete ? nonnegative_number(area) : json(nullptr); };
        layers.push_back(
            {{"layer_index", layers.size()},
             {"slice_z_mm", finite_number(layer->slice_z)},
             {"print_z_mm", finite_number(layer->print_z)},
             {"bottom_z_mm", finite_number(layer->bottom_z())},
             {"height_mm", nonnegative_number(layer->height)},
             {"complete", complete},
             {"excluded_path_count", footprint.excluded_path_count},
             {"invalid_path_count", footprint.invalid_path_count},
             {"section_area_mm2", nonnegative_number(area_mm2(section))},
             {"planned_path_area_mm2", coverage_area(area_mm2(covered))},
             {"section_covered_area_mm2", coverage_area(area_mm2(intersection_ex(section, covered)))},
             {"section_uncovered_area_mm2", coverage_area(area_mm2(diff_ex(section, covered)))},
             {"section_island_count", complete ? json(positive_region_count(section)) : json(nullptr)},
             {"section_min_island_gap_mm", complete ? section_min_island_gap(print, section) : json(nullptr)},
             {"parallel_boundary_clearance", parallel_boundary_clearance(print, section, complete)},
             {"section_islands_with_multiple_planned_regions_count", complete ? json(multiple_planned_regions) : json(nullptr)},
             {"planned_regions_overlapping_multiple_section_islands_count", complete ? json(multi_island_planned_regions) : json(nullptr)},
             {"completely_unprinted_island_count", complete ? json(unprinted_count) : json(nullptr)},
             {"completely_unprinted_island_area_mm2", coverage_area(unprinted_area)},
             {"hole_area_mm2", nonnegative_number(area_mm2(holes))},
             {"hole_intrusion_area_mm2", coverage_area(area_mm2(intersection_ex(holes, covered)))},
             {"closed_void_region_count", complete ? json(positive_region_count(holes)) : json(nullptr)},
             {"closed_void_fully_covered_count", complete ? json(fully_covered_voids) : json(nullptr)},
             {"closed_voids_with_multiple_remaining_regions_count", complete ? json(split_voids) : json(nullptr)},
             {"planned_enclosed_exterior_region_count", complete ? json(enclosed_exterior_regions) : json(nullptr)},
             {"planned_enclosed_exterior_area_mm2", coverage_area(enclosed_exterior_area)},
             {"solid_fill_target_area_mm2", nonnegative_number(area_mm2(solid_target))},
             {"solid_fill_uncovered_area_mm2", coverage_area(area_mm2(diff_ex(solid_target, covered)))},
             {"top_fill_target_area_mm2", nonnegative_number(area_mm2(top_target))},
             {"top_fill_uncovered_area_mm2", coverage_area(area_mm2(diff_ex(top_target, covered)))},
             {"bottom_fill_target_area_mm2", nonnegative_number(area_mm2(bottom_target))},
             {"bottom_fill_uncovered_area_mm2", coverage_area(area_mm2(diff_ex(bottom_target, covered)))}});
        previous_layer              = layer;
        previous_section            = std::move(section);
        previous_section_bounds     = std::move(section_bounds);
        previous_covered            = std::move(covered);
        previous_footprint_complete = complete;
    }
    json                             roles      = json::array();
    const std::array<const char*, 3> role_names = {"bridge_infill", "internal_bridge_infill", "overhang_perimeter"};
    for (size_t role = 0; role < bridge_totals.size(); ++role) {
        const BridgePathStats& stats = bridge_totals[role];
        roles.push_back({{"role", role_names[role]},
                         {"path_count", bridge_complete ? json(stats.path_count) : json(nullptr)},
                         {"path_length_mm", bridge_complete ? nonnegative_number(stats.path_length_mm) : json(nullptr)},
                         {"layer_footprint_area_sum_mm2", bridge_complete ? nonnegative_number(stats.layer_area_sum_mm2) : json(nullptr)},
                         {"excluded_path_count", stats.excluded_path_count},
                         {"invalid_path_count", stats.invalid_path_count}});
    }
    json volumes         = json::array();
    json surface_volumes = json::array();
    for (SourceVolumeSampling& target : source_volumes) {
        target.values["complete"]                          = target.complete;
        target.values["source_section_layer_count"]        = target.complete ? json(target.source_count) : json(nullptr);
        target.values["final_section_overlap_layer_count"] = target.complete ? json(target.final_count) : json(nullptr);
        target.values["planned_path_overlap_layer_count"]  = target.complete ? json(target.path_count) : json(nullptr);
        volumes.push_back(std::move(target.values));
        surface_volumes.push_back(std::move(target.surface_alignment));
    }
    return {{"planned_layer_coverage", {{"scope", "per_instance_model_paths_before_gcode"}, {"layers", std::move(layers)}}},
            {"planned_perimeter_loops", perimeter_loops.to_json()},
            {"solid_fill_target_depth", solid_depth.to_json()},
            {"planned_material_depth", material_depth.to_json()},
            {"nominal_target_path_connectivity", nominal_target_path_connectivity(print, object, joint)},
            {"joint_lower_path_slab_coverage",
             {{"scope", "per_instance_lower_source_print_z_nominal_path_slabs_before_gcode"},
              {"z_resolution_mm", SCALING_FACTOR},
              {"instances", std::move(joint_instances)}}},
            {"planned_support_projection",
             {{"scope", "per_instance_plate_support_projection_before_gcode"}, {"instances", std::move(support_instances)}}},
            {"cross_layer_geometry", {{"scope", "per_instance_adjacent_model_sections"}, {"layers", std::move(cross_layers)}}},
            {"planned_bridge_paths",
             {{"scope", "per_instance_model_paths_before_gcode"}, {"complete", bridge_complete}, {"roles", std::move(roles)}}},
            {"external_bridge_projection",
             {{"scope", "external_bridge_centerlines_over_previous_model_footprint_before_gcode"}, {"layers", std::move(external_bridges)}}},
            {"source_volume_sampling", {{"scope", "per_instance_source_model_part_on_retained_layers"}, {"volumes", std::move(volumes)}}},
            {"source_surface_layer_alignment",
             {{"scope", "source_model_part_facets_and_retained_source_layer_bands"}, {"volumes", std::move(surface_volumes)}}}};
}

json selected_settings(const ConfigBase& config, std::initializer_list<const char*> keys)
{
    json result = json::object();
    for (const char* key : keys)
        if (const ConfigOption* option = config.option(key)) {
            // Keep normal configuration snapshots serialized as before. The
            // observed cap/threshold non-finite input values cannot be serialized by Orca;
            // report this field as unavailable rather than losing the report.
            if (std::string(key) == "filament_max_volumetric_speed" || std::string(key) == "slow_down_layer_time") {
                const auto* caps = dynamic_cast<const ConfigOptionFloats*>(option);
                if (caps != nullptr &&
                    std::any_of(caps->values.begin(), caps->values.end(), [](double value) { return !std::isfinite(value); })) {
                    result[key] = nullptr;
                    continue;
                }
            }
            result[key] = option->serialize();
        }
    return result;
}

json object_settings(const PrintObject& object)
{
    json result;
    result["object"]  = selected_settings(object.config(),
                                          {"layer_height", "wall_generator", "enable_support", "support_type", "support_style",
                                           "support_threshold_angle", "support_on_build_plate_only", "support_top_z_distance",
                                           "support_object_xy_distance", "brim_type", "brim_width", "brim_object_gap", "raft_mode",
                                           "raft_layers", "raft_base_layers", "raft_interface_layers", "raft_surface_layers",
                                           "default_acceleration", "outer_wall_acceleration", "inner_wall_acceleration",
                                           "initial_layer_acceleration", "top_surface_acceleration", "bridge_acceleration",
                                           "travel_acceleration", "sparse_infill_acceleration", "internal_solid_infill_acceleration"});
    result["regions"] = json::array();
    for (size_t index = 0; index < object.num_printing_regions(); ++index) {
        result["regions"].push_back(
            {{"region_index", index},
             {"settings",
              selected_settings(object.printing_region(index).config(),
                                {"wall_loops", "outer_wall_line_width", "inner_wall_line_width", "enable_overhang_speed",
                                 "slowdown_for_curled_perimeters", "overhang_1_4_speed", "overhang_2_4_speed", "overhang_3_4_speed",
                                 "overhang_4_4_speed", "bridge_speed", "bridge_flow", "sparse_infill_density",
                                 "outer_wall_speed", "inner_wall_speed", "small_perimeter_speed", "small_perimeter_threshold"})}});
    }
    return result;
}

json object_summary(const Print&                  print,
                    const PrintObject&            object,
                    size_t                        index,
                    const PlateSupportProjection& support,
                    PlateModelProjection&         model,
                    JointLowerPathSlabs&          joint)
{
    NumericRange layer_heights;
    for (const Layer* layer : object.layers())
        layer_heights.add(layer->height);

    const Layer* first              = object.layers().empty() ? nullptr : object.layers().front();
    const Layer* last               = object.layers().empty() ? nullptr : object.layers().back();
    json         first_section_area = nullptr;
    if (first != nullptr) {
        double area = 0.;
        for (const ExPolygon& polygon : first->lslices)
            area += polygon.area() * SCALING_FACTOR * SCALING_FACTOR;
        first_section_area = nonnegative_number(area);
    }
    json layer_analysis        = analyze_model_layers(print, object, support, model, joint);
    json planned_bed_footprint = nullptr;
    if (first != nullptr && std::isfinite(first->bottom_z()) && std::isfinite(first->height) && first->height > 0.) {
        if (std::abs(first->bottom_z()) <= SCALING_FACTOR)
            planned_bed_footprint = layer_analysis.at("planned_layer_coverage").at("layers").at(0).at("planned_path_area_mm2");
        else if (first->bottom_z() > SCALING_FACTOR)
            planned_bed_footprint = 0.;
    }
    return {{"group_index", index},
            {"source_object_id", object.model_object()->id().id},
            {"instance_count", object.instances().size()},
            {"model_layer_count", object.layer_count()},
            {"support_layer_count", object.support_layer_count()},
            {"model_layer_height_mm", layer_heights.to_json()},
            {"first_model_print_z_mm", first == nullptr ? json(nullptr) : finite_number(first->print_z)},
            {"last_model_print_z_mm", last == nullptr ? json(nullptr) : finite_number(last->print_z)},
            {"first_model_layer_section_area_mm2_per_instance", std::move(first_section_area)},
            {"planned_model_bed_footprint_area_mm2_per_instance", std::move(planned_bed_footprint)},
            {"planned_layer_coverage", std::move(layer_analysis.at("planned_layer_coverage"))},
            {"planned_perimeter_loops", std::move(layer_analysis.at("planned_perimeter_loops"))},
            {"solid_fill_target_depth", std::move(layer_analysis.at("solid_fill_target_depth"))},
            {"planned_material_depth", std::move(layer_analysis.at("planned_material_depth"))},
            {"nominal_target_path_connectivity", std::move(layer_analysis.at("nominal_target_path_connectivity"))},
            {"joint_lower_path_slab_coverage", std::move(layer_analysis.at("joint_lower_path_slab_coverage"))},
            {"planned_support_projection", std::move(layer_analysis.at("planned_support_projection"))},
            {"cross_layer_geometry", std::move(layer_analysis.at("cross_layer_geometry"))},
            {"planned_bridge_paths", std::move(layer_analysis.at("planned_bridge_paths"))},
            {"external_bridge_projection", std::move(layer_analysis.at("external_bridge_projection"))},
            {"source_volume_sampling", std::move(layer_analysis.at("source_volume_sampling"))},
            {"source_surface_layer_alignment", std::move(layer_analysis.at("source_surface_layer_alignment"))},
            {"configured_settings", object_settings(object)}};
}

struct MoveCounts
{
    size_t total     = 0;
    size_t extrude   = 0;
    size_t travel    = 0;
    size_t retract   = 0;
    size_t unretract = 0;
    size_t wipe      = 0;
    size_t other     = 0;

    void add(EMoveType type)
    {
        ++total;
        switch (type) {
        case EMoveType::Extrude: ++extrude; break;
        case EMoveType::Travel: ++travel; break;
        case EMoveType::Retract: ++retract; break;
        case EMoveType::Unretract: ++unretract; break;
        case EMoveType::Wipe: ++wipe; break;
        default: ++other; break;
        }
    }

    json to_json() const
    {
        return {{"total", total},         {"extrude", extrude}, {"travel", travel}, {"retract", retract},
                {"unretract", unretract}, {"wipe", wipe},       {"other", other}};
    }
};

struct ExtrusionRanges
{
    NumericRange width;
    NumericRange height;
    NumericRange feedrate;
    NumericRange volumetric_rate;
    NumericRange segment_length;
    double       path_length = 0.;

    void add(const GCodeProcessorResult::MoveVertex& move)
    {
        width.add(move.width);
        height.add(move.height);
        feedrate.add(move.feedrate);
        segment_length.add(move.travel_dist);
        if (std::isfinite(move.travel_dist) && move.travel_dist >= 0.f)
            path_length += move.travel_dist;
        // Reuse the processor's arc-aware cross section. Do not infer a path
        // length from the straight chord between MoveVertex positions.
        if (!std::isfinite(move.feedrate) || !std::isfinite(move.mm3_per_mm) || move.feedrate < 0.f || move.mm3_per_mm < 0.f)
            ++volumetric_rate.invalid_count;
        else
            volumetric_rate.add(double(move.feedrate) * double(move.mm3_per_mm));
    }

    json to_json() const
    {
        return {{"width_mm", width.to_json()},
                {"height_mm", height.to_json()},
                {"commanded_feedrate_mm_s", feedrate.to_json()},
                {"segment_length_mm", segment_length.to_json()},
                {"path_length_mm",
                 segment_length.count == 0 || segment_length.invalid_count != 0 ? json(nullptr) : nonnegative_number(path_length)},
                {"nominal_volumetric_rate_mm3_s", volumetric_rate.to_json()}};
    }
};

json estimated_motion(const GCodeProcessorResult& result, ExtrusionRole role)
{
    const size_t index = static_cast<size_t>(role);
    const auto motion = index < result.normal_estimated_motion.size()
                            ? result.normal_estimated_motion[index] : GCodeProcessorResult::EstimatedMotion{};
    const bool complete = motion.complete && (motion.block_count == 0 ||
        (std::isfinite(motion.min_peak_speed_mm_s) && motion.min_peak_speed_mm_s > 0.f &&
         std::isfinite(motion.max_peak_speed_mm_s) && motion.max_peak_speed_mm_s >= motion.min_peak_speed_mm_s &&
         std::isfinite(motion.min_tangential_acceleration_mm_s2) && std::isfinite(motion.max_tangential_acceleration_mm_s2) &&
         motion.min_tangential_acceleration_mm_s2 <= motion.max_tangential_acceleration_mm_s2));
    const bool have_blocks = complete && motion.block_count > 0;
    return {{"scope", "normal_estimator_finalized_extrusion_blocks"},
            {"complete", complete}, {"block_count", complete ? json(motion.block_count) : json(nullptr)},
            {"peak_speed_mm_s", {{"min", have_blocks ? json(motion.min_peak_speed_mm_s) : json(nullptr)},
                                 {"max", have_blocks ? json(motion.max_peak_speed_mm_s) : json(nullptr)}}},
            {"tangential_acceleration_mm_s2", {{"min", have_blocks ? json(motion.min_tangential_acceleration_mm_s2) : json(nullptr)},
                                               {"max", have_blocks ? json(motion.max_tangential_acceleration_mm_s2) : json(nullptr)}}}};
}

const char* role_name(ExtrusionRole role)
{
    switch (role) {
    case erNone: return "none";
    case erPerimeter: return "perimeter";
    case erExternalPerimeter: return "external_perimeter";
    case erOverhangPerimeter: return "overhang_perimeter";
    case erInternalInfill: return "internal_infill";
    case erSolidInfill: return "solid_infill";
    case erTopSolidInfill: return "top_solid_infill";
    case erBottomSurface: return "bottom_surface";
    case erIroning: return "ironing";
    case erBridgeInfill: return "bridge_infill";
    case erInternalBridgeInfill: return "internal_bridge_infill";
    case erGapFill: return "gap_fill";
    case erSkirt: return "skirt";
    case erBrim: return "brim";
    case erSupportMaterial: return "support_material";
    case erSupportMaterialInterface: return "support_material_interface";
    case erSupportTransition: return "support_transition";
    case erWipeTower: return "wipe_tower";
    case erCustom: return "custom";
    case erMixed: return "mixed";
    default: return "unknown";
    }
}

json volume_for(const std::map<size_t, double>& volumes, size_t extruder)
{
    const auto found = volumes.find(extruder);
    return found == volumes.end() ? json(nullptr) : nonnegative_number(found->second);
}

std::optional<double> configured_flow_cap(const PrintConfig& config, size_t extruder)
{
    if (extruder >= config.filament_diameter.values.size() || config.filament_max_volumetric_speed.values.empty())
        return std::nullopt;
    const double cap = config.filament_max_volumetric_speed.get_at(extruder);
    return std::isfinite(cap) && cap > 0. ? std::optional<double>(cap) : std::nullopt;
}

json estimated_peak_flow(const PrintConfig& config, const GCodeProcessorResult& result, size_t extruder)
{
    GCodeProcessorResult::EstimatedPeakFlow flow;
    if (result.normal_estimated_peak_flow_available) {
        const auto it = result.normal_estimated_peak_flow.find(extruder);
        if (it == result.normal_estimated_peak_flow.end())
            flow.complete = true;
        else
            flow = it->second;
    }
    const bool complete = flow.complete && (flow.block_count == 0 ||
        (std::isfinite(flow.min_peak_volumetric_rate_mm3_s) && flow.min_peak_volumetric_rate_mm3_s >= 0. &&
         std::isfinite(flow.max_peak_volumetric_rate_mm3_s) &&
         flow.max_peak_volumetric_rate_mm3_s >= flow.min_peak_volumetric_rate_mm3_s));
    const bool have_blocks = complete && flow.block_count > 0;
    json ratio = nullptr;
    if (have_blocks)
        if (const auto cap = configured_flow_cap(config, extruder))
            ratio = nonnegative_number(flow.max_peak_volumetric_rate_mm3_s / *cap);
    return {{"scope", "normal_estimator_finalized_linear_e_blocks"},
            {"complete", complete}, {"block_count", complete ? json(flow.block_count) : json(nullptr)},
            {"peak_volumetric_rate_mm3_s", {{"min", have_blocks ? json(flow.min_peak_volumetric_rate_mm3_s) : json(nullptr)},
                                           {"max", have_blocks ? json(flow.max_peak_volumetric_rate_mm3_s) : json(nullptr)}}},
            {"max_to_configured_cap_ratio", std::move(ratio)}};
}

json estimated_flow_cap_distance(const PrintConfig& config, const GCodeProcessorResult& result, size_t extruder)
{
    GCodeProcessorResult::EstimatedFlowCapDistance observation;
    const auto                                     it = result.normal_estimated_flow_cap_distance.find(extruder);
    if (it != result.normal_estimated_flow_cap_distance.end())
        observation = it->second;
    const auto   cap      = configured_flow_cap(config, extruder);
    const double distance = observation.participating_path_length_mm;
    const double above    = observation.above_cap_path_length_mm;
    const bool   complete = observation.complete && cap && *cap == observation.configured_cap_mm3_s && std::isfinite(distance) &&
                          distance >= 0. && std::isfinite(above) && above >= 0. && above <= distance;
    return {{"scope", "normal_estimator_finalized_linear_e_block_phases"},
            {"complete", complete},
            {"participating_path_length_mm", complete ? json(distance) : json(nullptr)},
            {"above_cap_path_length_mm", complete ? json(above) : json(nullptr)},
            {"above_cap_path_length_ratio", complete && distance > 0. ? json(above / distance) : json(nullptr)}};
}

struct FlowComparison
{
    bool   complete         = true;
    double path_length      = 0.;
    double above_cap_length = 0.;

    void add(const GCodeProcessorResult::MoveVertex& move, const PrintConfig& config)
    {
        const auto cap = configured_flow_cap(config, move.extruder_id);
        if (!cap || !std::isfinite(move.travel_dist) || move.travel_dist < 0.f ||
            !std::isfinite(move.feedrate) || move.feedrate < 0.f || !std::isfinite(move.mm3_per_mm) || move.mm3_per_mm < 0.f) {
            complete = false;
            return;
        }
        path_length += move.travel_dist;
        if (double(move.feedrate) * double(move.mm3_per_mm) > *cap)
            above_cap_length += move.travel_dist;
    }

    json to_json() const
    {
        const bool valid = complete && std::isfinite(path_length) && std::isfinite(above_cap_length);
        return {{"complete", valid},
                {"above_cap_path_length_mm", valid ? json(above_cap_length) : json(nullptr)},
                {"above_cap_path_length_ratio", valid && path_length > 0. ? json(above_cap_length / path_length) : json(nullptr)}};
    }
};

json layer_time_threshold_observation(const Print& print, const GCodeProcessorResult& result, size_t extruder)
{
    const auto& config = print.config();
    json enabled = nullptr;
    json threshold = nullptr;
    if (extruder < config.filament_diameter.values.size()) {
        if (!config.slow_down_for_layer_cooling.values.empty())
            enabled = config.slow_down_for_layer_cooling.get_at(extruder);
        if (!config.slow_down_layer_time.values.empty())
            threshold = nonnegative_number(config.slow_down_layer_time.get_at(extruder));
    }
    const auto& durations = result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].layers_times;
    bool complete = enabled.is_boolean() && threshold.is_number() && result.layer_extruder_ids_complete &&
                    result.layer_extruder_ids.size() == durations.size();
    size_t active = 0;
    size_t below = 0;
    if (complete) {
        for (size_t index = 0; index < durations.size(); ++index) {
            if (index % 4096 == 0)
                throw_if_canceled(print);
            const auto& ids = result.layer_extruder_ids[index];
            if (std::find(ids.begin(), ids.end(), extruder) == ids.end())
                continue;
            if (!std::isfinite(durations[index]) || durations[index] < 0.f) {
                complete = false;
                break;
            }
            ++active;
            if (double(durations[index]) < threshold.get<double>())
                ++below;
        }
    }
    const bool applicable = complete && enabled.get<bool>() && threshold.get<double>() > 0. && active > 0;
    return {{"scope", "final_gcode_layer_sequences_per_material"},
            {"complete", complete},
            {"applicable", complete ? json(applicable) : json(nullptr)},
            {"slow_down_enabled", std::move(enabled)},
            {"configured_threshold_s", std::move(threshold)},
            {"active_layer_sequence_count", complete ? json(active) : json(nullptr)},
            {"below_threshold_layer_sequence_count", applicable ? json(below) : json(nullptr)},
            {"below_threshold_layer_ratio", applicable ? json(double(below) / double(active)) : json(nullptr)}};
}

json gcode_summary(const Print& print, const GCodeProcessorResult& result)
{
    const auto&  config     = print.config();
    const auto&  statistics = result.print_statistics;
    const auto&  normal     = statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    NumericRange layer_times;
    json         layer_time_sequence = json::array();
    for (float duration : normal.layers_times) {
        layer_times.add(duration);
        layer_time_sequence.push_back(nonnegative_number(duration));
    }

    MoveCounts                               counts;
    ExtrusionRanges                          ranges;
    std::map<size_t, ExtrusionRanges>        extruder_ranges;
    std::map<ExtrusionRole, ExtrusionRanges> role_ranges;
    std::map<ExtrusionRole, FlowComparison> role_flow;
    NumericRange                             travel_lengths;
    double                                   travel_length = 0.;
    Vec3f                                    seam_min = Vec3f::Zero(), seam_max = Vec3f::Zero();
    bool                                     seam_positions_complete = true;
    size_t                                   seam_record_count = 0;
    json                                     seam_positions = json::array();
    for (const auto& move : result.moves) {
        counts.add(move.type);
        if (counts.total % 4096 == 0)
            throw_if_canceled(print);
        if (move.type == EMoveType::Travel) {
            travel_lengths.add(move.travel_dist);
            if (std::isfinite(move.travel_dist) && move.travel_dist >= 0.f)
                travel_length += move.travel_dist;
        }
        if (move.type == EMoveType::Seam) {
            if (!move.position.allFinite()) {
                seam_positions_complete = false;
                seam_positions = nullptr;
            } else {
                if (seam_positions_complete)
                    seam_positions.push_back(json::array({move.position.x(), move.position.y(), move.position.z()}));
                if (seam_record_count == 0)
                    seam_min = seam_max = move.position;
                else {
                    seam_min = seam_min.cwiseMin(move.position);
                    seam_max = seam_max.cwiseMax(move.position);
                }
            }
            ++seam_record_count;
        }
        if (move.type == EMoveType::Extrude) {
            ranges.add(move);
            extruder_ranges[move.extruder_id].add(move);
            role_ranges[move.extrusion_role].add(move);
            role_flow[move.extrusion_role].add(move, config);
        }
    }
    // Include a material with recorded consumption even if no drawable
    // extrusion move exists (for example a flush recorded separately).
    const auto include_extruders = [&extruder_ranges](const std::map<size_t, double>& volumes) {
        for (const auto& entry : volumes)
            extruder_ranges.try_emplace(entry.first);
    };
    include_extruders(statistics.total_volumes_per_extruder);
    include_extruders(statistics.model_volumes_per_extruder);
    include_extruders(statistics.support_volumes_per_extruder);
    include_extruders(statistics.wipe_tower_volumes_per_extruder);
    include_extruders(statistics.flush_per_filament);

    // The processor only retains aggregate role masses, not a role/material
    // matrix. If any used material lacks a valid density, no aggregate role
    // mass can safely be presented as complete.
    const bool role_mass_available = !extruder_ranges.empty() &&
                                     std::all_of(extruder_ranges.begin(), extruder_ranges.end(), [&result](const auto& entry) {
                                         return entry.first < result.filament_densities.size() &&
                                                std::isfinite(result.filament_densities[entry.first]) &&
                                                result.filament_densities[entry.first] > 0.f;
                                     });

    json extruders = json::array();
    for (const auto& entry : extruder_ranges) {
        json cap                   = nullptr;
        json max_flow_to_cap_ratio = nullptr;
        // The exporter and processor use the same zero-based material IDs.
        // Use the exporter's get_at semantics (including its first-value
        // fallback), but do not assign a cap to an unknown material ID.
        if (const auto configured_cap = configured_flow_cap(config, entry.first)) {
            cap                      = *configured_cap;
            const NumericRange& flow = entry.second.volumetric_rate;
            if (flow.count > 0 && flow.invalid_count == 0)
                max_flow_to_cap_ratio = nonnegative_number(flow.maximum / *configured_cap);
        }
        extruders.push_back({{"extruder_id", entry.first},
                             {"configured_filament_max_volumetric_speed_mm3_s", std::move(cap)},
                             {"max_nominal_flow_to_cap_ratio", std::move(max_flow_to_cap_ratio)},
                             {"estimated_peak_flow", estimated_peak_flow(config, result, entry.first)},
                             {"estimated_flow_cap_distance", estimated_flow_cap_distance(config, result, entry.first)},
                             {"layer_time_threshold_observation", layer_time_threshold_observation(print, result, entry.first)},
                             {"extrusion_ranges", entry.second.to_json()},
                             {"estimated_volume_mm3",
                              {{"total", volume_for(statistics.total_volumes_per_extruder, entry.first)},
                               {"other", volume_for(statistics.model_volumes_per_extruder, entry.first)},
                               {"support", volume_for(statistics.support_volumes_per_extruder, entry.first)},
                               {"wipe_tower", volume_for(statistics.wipe_tower_volumes_per_extruder, entry.first)},
                               {"flush", volume_for(statistics.flush_per_filament, entry.first)}}}});
    }
    json by_role = json::array();
    for (const auto& entry : statistics.used_filaments_per_role)
        by_role.push_back({{"role_id", static_cast<unsigned int>(entry.first)},
                           {"role", role_name(entry.first)},
                           {"estimated_length_m", nonnegative_number(entry.second.first)},
                           {"estimated_mass_g", role_mass_available ? nonnegative_number(entry.second.second) : json(nullptr)}});

    json role_times = json::array();
    for (const auto& entry : normal.roles_times)
        role_times.push_back({{"role_id", static_cast<unsigned int>(entry.first)},
                              {"role", role_name(entry.first)},
                              {"estimated_time_s", nonnegative_number(entry.second)}});

    json extrusion_by_role = json::array();
    for (const auto& entry : role_ranges)
        extrusion_by_role.push_back({{"role_id", static_cast<unsigned int>(entry.first)},
                                     {"role", role_name(entry.first)},
                                     {"extrusion_ranges", entry.second.to_json()},
                                     {"estimated_motion", estimated_motion(result, entry.first)},
                                     {"configured_flow_comparison", role_flow.at(entry.first).to_json()}});

    const bool have_seam_bounds = seam_record_count != 0 && seam_positions_complete;

    return {{"normal_estimate",
             {{"total_time_s", nonnegative_number(normal.time)},
              {"prepare_time_s", nonnegative_number(normal.prepare_time)},
              {"layer_sequence_count", normal.layers_times.size()},
              {"layer_duration_s", layer_times.to_json()},
              {"layer_duration_sequence_s", std::move(layer_time_sequence)},
              {"role_time_s", std::move(role_times)}}},
            {"move_counts", counts.to_json()},
            {"seam_record_count", seam_record_count},
            {"seam_position_bounds_mm",
             {{"complete", seam_positions_complete},
              {"min", have_seam_bounds ? json::array({seam_min.x(), seam_min.y(), seam_min.z()}) : json(nullptr)},
              {"max", have_seam_bounds ? json::array({seam_max.x(), seam_max.y(), seam_max.z()}) : json(nullptr)}}},
            {"seam_positions_mm",
             {{"scope", "gcode_processor_preview_coordinates_in_move_order"},
              {"complete", seam_positions_complete}, {"positions", std::move(seam_positions)}}},
            {"travel",
             {{"segment_length_mm", travel_lengths.to_json()},
              {"path_length_mm", travel_lengths.invalid_count == 0 ? nonnegative_number(travel_length) : json(nullptr)}}},
            {"extrusion_ranges", ranges.to_json()},
            {"extrusion_by_role", std::move(extrusion_by_role)},
            {"extruders", std::move(extruders)},
            {"filament_by_role", std::move(by_role)}};
}

} // namespace

json make_slice_analysis_report(const Print& print, const GCodeProcessorResult& gcode_result)
{
    throw_if_canceled(print);
    LayerPathCache               paths{print};
    const PlateSupportProjection support = plate_support_projection(print, paths);
    PlateModelProjection         model   = plate_model_projection(print, paths);
    JointLowerPathSlabs          joint{print, paths};
    std::set<size_t>             source_ids;
    json                         groups = json::array();
    for (const PrintObject* object : print.objects()) {
        source_ids.insert(object->model_object()->id().id);
        groups.push_back(object_summary(print, *object, groups.size(), support, model, joint));
    }

    return {
        {"schema_version", 1},
        {"scope", "sliced_print_metrics"},
        {"objects",
         {{"source_object_count", source_ids.size()},
          {"slice_object_group_count", groups.size()},
          {"instance_count", print.num_object_instances()},
          {"groups", std::move(groups)}}},
        {"planned_plate_bed_footprint", planned_plate_bed_footprint(print)},
        {"gcode", gcode_summary(print, gcode_result)},
        {"configured_settings",
         selected_settings(print.config(), {"print_sequence", "spiral_mode", "nozzle_diameter", "initial_layer_print_height",
                                            "filament_max_volumetric_speed", "slow_down_for_layer_cooling", "slow_down_layer_time",
                                            "slow_down_min_speed", "fan_cooling_layer_time"})},
        {"limitations",
         json::array(
             {"Read-only metrics of the supplied sliced print and finalized G-code result; "
              "no parameter selection or printability verdict.",
              "Object counts cover this Print only. Source IDs are process-local; slice groups may share one source object and geometry.",
              "Model layers and support layers are separate collections, not the whole plate's G-code layer count; "
              "support layers may include raft.",
              "First model layer section area is per instance, excludes polygon holes, and is not deposited contact area "
              "or bed adhesion; raft or elevation may separate it from the bed.",
              "Planned model bed footprint is the first model layer's path area only when its bottom Z is zero within "
              "one coordinate unit; an elevated model gives zero and unavailable geometry gives null. It excludes raft, "
              "support, brim and skirt and does not measure physical adhesion or account for machine Z-offset calibration.",
              "Plate bed footprint separately unions model paths, support/raft paths and model/support brim paths whose nominal "
              "bottom (print Z minus each path's height) is zero within one coordinate unit. Their combined area is a geometric "
              "union, not the sum of category areas. Model/support paths use every instance's plate position; brim maps already "
              "contain those positions. It excludes skirt, wipe tower and custom G-code, does not read pre-merge raft backups, "
              "and does not establish physical contact, adhesion, strength or the final G-code footprint.",
              "Bed-footprint regions are the positive-area normalized polygons of each existing category union and their "
              "combined union. Areas exclude holes; material islands inside holes remain separate regions. XY bounds use "
              "source world placement coordinates, including plate-origin offsets, not bed-local machine coordinates. "
              "Region order is not a persistent feature identity. Bounds do not measure local minimum width, identify "
              "structural feet or establish slenderness, fusion or adhesion; no extra closing or geometry tolerance is applied.",
              "Planned layer coverage is a per-instance XY footprint of model perimeters and fills before G-code seam, "
              "scarf, spiral or other export transformations; it excludes support, raft, brim and skirt. It is not actual deposited "
              "material.",
              "Planned perimeter loops are complete, positive-flow closed model-wall centerlines, counted once per instance "
              "before seam clipping, scarf or spiral export. A loop may contain ordinary and overhang wall fragments. "
              "Open paths, gap fill, support and brim are excluded; disconnected or degenerate loops are unavailable. "
              "Adjacent duplicate points are removed; turning angles measure absolute changes of travel direction "
              "(0 straight, 180 reversal), not interior angles, curvature or physical corner quality. Configuration "
              "snapshots preserve zero/inherited and percentage values, not the processor's or machine's actual acceleration. "
              "Orca's small-perimeter comparison uses circumference 2*pi*small_perimeter_threshold, not that setting "
              "as a direct loop-length limit; no additional size threshold or quality score is applied here.",
              "Section area uses merged layer lslices; first-layer slices can precede elephant-foot compensation. "
              "Section uncovered area includes intentional sparse-infill gaps and is not a missing-detail or quality verdict.",
              "Region counts use positive-area normalized XY polygons, not three-dimensional connectivity or strength. "
              "A section island may contain multiple clipped planned regions with ordinary sparse infill; a full planned region "
              "may overlap multiple separate section islands without establishing physical fusion. Boundary-only contact is not "
              "positive-area overlap. Closed-void counts concern bounded section negative spaces after excluding nested material: "
              "their remaining regions do not verify surrounding printed walls, open slots or three-dimensional hole clearance.",
              "Minimum island gap is the XY boundary distance between different positive-area normalized retained section islands, "
              "including hole boundaries. Fewer than two islands gives null; point contact can give zero. It is not the width of "
              "an opening within one island, a neck width or a three-dimensional clearance. Retained elephant-foot-layer sections "
              "can precede compensation, so this is not a compensated path gap or a measured printed gap.",
              "Parallel-boundary clearance uses positive-length primary Voronoi edges between strictly parallel segment sites "
              "belonging to the same normalized section island. All islands participate in nearest-site construction. "
              "Material and empty-space widths are separate; empty space can be an open slot or a bounded hole. No applicable "
              "pair gives null, not zero; a square or curved neck may have no such pair. This is neither general minimum "
              "wall/neck thickness nor compensated-path clearance, three-dimensional fit or feature preservation. Invalid "
              "original diagrams or out-of-domain coordinates leave only this observation unknown; repaired diagrams are "
              "not used. A zero-length Voronoi feature is excluded.",
              "Planned enclosed exterior measures bounded empty space in the planned footprint that belonged to the source "
              "section's unbounded exterior. It excludes source material, original bounded voids and nested planned material. "
              "The region count counts each planned bounded void once, even if its source-exterior portion has several fragments. "
              "No artificial frame or gap-closing radius is used. These are not identified slots or measured clearances: zero "
              "also occurs when an opening is completely filled, and missing planned walls are not supplied by the source section.",
              "Cross-layer geometry compares adjacent model sections in the same transformed slice group, not deposited material "
              "or support. First layers have no previous model layer, so their derived comparison metrics are null and complete=false "
              "means not applicable. Invalid layer order, slicing errors or unavailable Z data also leave derived metrics null.",
              "Outside-previous-model area and islands without positive-area overlap are XY projection observations, not unsupported "
              "area or floating-print failures. Sections include sparse interiors and may have no printed paths. Support, raft, "
              "neighboring objects and physical anchoring are not evaluated by this adjacent-section comparison; "
              "the signed model-layer gap is reported separately.",
              "Planned support projection uses current support_fills, including raft, at all instances' plate positions. "
              "Its target is the first model section, or later section material outside the previous model section, intersected "
              "with current model planned paths. It is not a three-dimensional downward-facing surface. Target locations are "
              "assigned to the highest overlapping support print plane no higher than the model print plane; lower levels only "
              "receive still-unmatched locations. Areas are disjoint within a target and are reported separately for each instance. "
              "The signed gap is model layer bottom Z minus support print Z, not the actual gap between extrusion surfaces. "
              "Zero or negative gaps are retained. Unmatched area is not an unsupported-print verdict: physical model obstruction, "
              "bridge anchoring, printing order (including sequential objects), physical contact and strength are not evaluated. "
              "Retained print planes can differ from near-equal Z planes combined during G-code export. Invalid support paths "
              "or support Z data make this separate observation incomplete without invalidating model-only metrics.",
              "Each support level's intervening model projection measures its assigned area's overlap with model planned paths "
              "whose print planes lie strictly between that support print Z and the target model print Z. It uses all plate "
              "instances, counts repeated overlap once, and does not replace paths with solid sections. Equal-Z model planes are "
              "excluded; support at the target print Z makes only this nested observation unavailable. Non-finite model print Z "
              "cannot be located and makes all such observations unavailable; a known-Z invalid model layer or path affects only "
              "windows containing it. The original support assignment and completeness remain unchanged. Model path heights, "
              "three-dimensional blocking, physical contact and extrusion order are not evaluated: even with sequential-object "
              "printing, this is a whole-plate geometric observation, not proof that intervening material has already printed.",
              "Planned bridge paths separate external bridge infill, internal bridge infill and overhang perimeters. Counts are "
              "path fragments, not independent spans; lengths use the retained simplified polylines, not final G-code arc lengths. "
              "Footprint sums union within each role/layer before summing across Z. These roles do not establish endpoint support, "
              "bridge success or physical contact, and incomplete planned path data gives null aggregate measurements.",
              "External bridge projection follows open external-bridge infill centerlines over the previous model layer's planned "
              "footprint, without expanding it or substituting the full section. Uncovered runs are continuous lengths along each "
              "retained polyline, not straight spans or independent bridges. A two-sided run has positive-length interior coverage "
              "immediately before and after it within that same path fragment; boundary-only or point contact is not coverage. "
              "Fragment endpoints leave outside connections unobserved, not proven absent. No previous model layer, invalid layer "
              "order/geometry/footprints or closed external-bridge paths leaves this observation incomplete. Zero runs have null "
              "maximum lengths. Internal bridges, overhang walls, support, raft, neighboring objects and same-layer connections "
              "are excluded. The reported print planes and cross-layer metadata describe Z ordering, not physical contact: "
              "bridge path height can differ from model-layer height. No anchoring, bridge success or physical span is verified.",
              "Joint lower-path slab coverage groups current model path-width footprints by their quantized nominal bottom "
              "(source print Z minus each path's own height). Lower model and current support/raft paths from all plate instances "
              "contribute only when their raw source print Z is strictly lower and their quantized slab satisfies "
              "source bottom < target bottom <= source top. Overlaps are unioned, and different target bottoms stay separate. "
              "External-bridge runs use this joint mask with the same fragment and strict-interior conventions as the original "
              "bridge observation. A closed bridge makes only its bridge child unavailable; invalid target heights cannot be "
              "assigned a bottom plane, while invalid lower sources affect only potentially intersecting planes. This is "
              "nominal width-by-height geometry, not actual bead contact, fusion, anchoring, sag or strength. The bed is excluded. "
              "Same-source-Z paths are excluded; near-Z export grouping, same-layer order, sequential-object order and final "
              "G-code Z changes are not inferred. Lower source Z does not prove that material has already printed. Uncovered "
              "area or length is not a failure verdict, including intentional sparse fill and support gaps.",
              "Source-volume sampling uses model-part volumes only and float-transformed source Z bounds with retained model slice "
              "planes, not print Z or raft Z. Interior and exact-boundary planes are separate; neither proves a nonempty section. "
              "The three section/path counts require the existing pre-boolean volume cache and valid retained layers; unavailable "
              "cache data gives null, not zero. They measure positive-area overlap within a source volume's section, not which volume "
              "owns the final material. Negative volumes, later volume overrides and XY compensation can change these overlaps. "
              "A fused volume's internal details or disconnected mesh surfaces are not separate targets. A volume's Z range is "
              "not local relief height, and these counts are not a lost-detail or printability verdict.",
              "Source-surface layer alignment observes float-transformed model-part facets, not the final Boolean exterior or "
              "printed paths. Source layer bands are reconstructed as slice_z plus/minus half the saved layer height; the "
              "mapping plane uses float(slice_z), as passed to the slicer, not print Z or raft Z. For each non-horizontal "
              "facet/band overlap, the normal-offset reference is the maximum normal component of moving source points only "
              "vertically to that slice plane. Counts are facet/band pairs, not physical surfaces. Horizontal facets are "
              "separate: their offset is distance to the nearest retained source-band boundary, including faces outside the "
              "retained range. No applicable facets gives count zero and maximum null; invalid source geometry or layer bands "
              "gives null metrics independently of source-section caches and path validity. These values depend on the source "
              "faceted approximation, can include internal or subsequently removed faces, and do not prove sampling, path "
              "coverage, actual staircase error, surface roughness or printing accuracy. Float rounding can slightly exceed "
              "a nominal half-layer-height bound; no automatic quality comparison or parameter selection is performed.",
              "Solid/top/bottom targets use the configured fill surfaces after wall processing; they do not include every original "
              "thin detail or prove shell thickness. Lower top/bottom surface density deliberately leaves gaps in these targets; "
              "uncovered area is not automatically a defect. Hole intrusion is planned XY overlap with section holes, not a measured "
              "clearance "
              "or a claim that a hole is closed. Whole unprinted islands are layer sections, not disconnected three-dimensional parts.",
              "Solid-fill target depth uses fixed-XY uninterrupted unions of nominal solid fill-surface slabs. Each slab ends "
              "at print Z and uses surface.thickness when supplied (including combined solid infill), otherwise layer.height. "
              "Z events are rounded to z_resolution_mm, the native coordinate precision, without changing saved layer values. "
              "Overlapping slabs contribute their Z union, not summed heights; positive gaps of one coordinate unit remain gaps. "
              "Top targets query downward depth and bottom targets upward depth; target areas are unioned at each height and "
              "summed across heights per instance. No targets gives area zero and null extrema. These are configured fill "
              "targets, even with reduced/zero skin density or bridging flow; not actual deposited solid thickness, effective "
              "solid-layer counts, strength or bridge support. Walls, sparse/void fill, brim and support are excluded.",
              "Planned material depth uses the same fixed-XY continuity scan on positive-flow model perimeter/fill path footprints "
              "and their nominal [print Z - path.height, print Z] slabs, unioned at the native Z precision. Top/bottom fill surfaces "
              "supply the starting targets using their own surface thickness, not the path height. Starting target area without "
              "path coverage is counted explicitly and contributes zero to the depth range; entirely uncovered targets have "
              "zero minimum and maximum, while no target gives null extrema. Support, raft and brim are not included. This is "
              "a before-G-code width-offset envelope, not actual bead shape, material density, deposited solid thickness, "
              "effective solid-layer counts or a strength guarantee. Deliberately sparse skins can have zero minimum depth.",
              "Nominal target/path connectivity compares compensated current region sections extruded through their model-layer "
              "height with model-only width-offset paths extruded through each path's own height. Z endpoints use the reported "
              "polygon-coordinate resolution. Positive-area joining faces connect neighboring Z intervals; point/line-only "
              "contact does not. Target-internal path components use clipped material, while cross-target joins retain material "
              "outside the target. Multiple clipped components may still connect by a path outside that target. Missing slab "
              "regions retain actual uncovered volume and local XYZ bounds, not box-filled geometry; sparse infill also creates "
              "uncovered regions. Coordinates are PrintObject-local XY and print Z, and indices are report-local, not source-part "
              "or cross-candidate identities. Each group describes one instance without support or inter-instance connections. "
              "These are final nominal bodies, not source-STL topology, semantic stroke loss, deposited bead shape, print order "
              "or strength; a later connection does not establish that an earlier start was supported.",
              "Coverage uses the slicer's width-offset polygons with butt path ends, zero additional offset and polygon arithmetic "
              "precision, not a manufacturing tolerance or a physical bead model. "
              "Incomplete footprint data or invalid layer geometry gives null derived coverage metrics and region counts. "
              "Transformed bounds, local three-dimensional wall thickness, "
              "source-part topology and overhang/bridge safety are not evaluated.",
              "Normal layer times follow G-code layer-change order, may include preparation/support/raft and multiple objects, "
              "and are estimates, not measured times or local cooling intervals.",
              "Per-material layer-time observations compare that material's existing enabled slow-down threshold with final "
              "normal G-code layer-sequence times. Material membership is captured from positive non-custom Extrude records "
              "before their temporary sequence IDs become display durations; custom/unclassified extrusion is excluded, "
              "while model, support/raft, brim and other classified extrusion may participate. A material counts once per "
              "sequence, independently of other materials, Z or equal durations. The raw first sequence still includes its "
              "preparation time. Disabled or zero thresholds and unused materials are not applicable; missing membership "
              "is unknown. This is not the CoolingBuffer's possibly merged support/model interval, a slowdown trigger or "
              "failure rate, local cooling time, fan effectiveness or a print-quality verdict.",
              "Move counts count processor records, not physical segments; one arc may have several display interpolation points.",
              "Extrusion segment and path lengths use the processor's arc-aware distance only for Extrude records; "
              "they exclude travel moves. Seam records remain part of move_counts.other and are separately counted.",
              "Travel lengths separately sum the processor's XYZ displacement for Travel records, including arcs and Z lifts; "
              "retractions without XYZ movement, wipes and seam markers are not travel. This is not crossing-over-model "
              "distance or a stringing prediction. Seam bounds cover finite processor-detected marker positions in preview "
              "coordinates, not every physical seam or its visual quality; no markers gives null bounds.",
              "Seam positions retain every processor marker in move order, including repeated positions. They use the "
              "processor's preview coordinate system with its existing plate/extruder offsets and Z-offset handling, "
              "not source-model local coordinates or measured print positions. No markers gives an empty array; any "
              "invalid marker gives an unavailable whole array. No object, layer number, visible seam or stringing "
              "classification is inferred from these positions.",
              "Per-role configured flow comparison uses each extrusion's material cap and final commanded nominal flow. "
              "The ratio is length above that cap divided by the role's total extrusion length, not a count ratio or "
              "acceleration-resolved flow. Unknown caps or invalid lengths/flow leave the whole role comparison unknown; "
              "zero total length has no ratio. Custom priming, support and model roles remain separate. Rounding alone "
              "may place a command slightly above a cap; this is an observation, not a failure or material capability test.",
              "Estimated motion observes finalized normal-estimator blocks with positive classified extrusion, excluding custom "
              "and unclassified roles. Peak-speed ranges compare each block's reached peak, not its requested speed or the "
              "minimum speed throughout a move. Signed tangential acceleration includes only nonempty acceleration, cruise "
              "and deceleration phases. Retained queue blocks are not counted twice; a final unconsumed block leaves its role "
              "unknown. These are estimator results, not axis/centripetal accelerations, measured motion or vibration.",
              "Estimated peak flow snapshots each block's material and linear-E volume per path length with the estimator's "
              "current global M221 multiplier, then multiplies by that same finalized block's peak speed. It excludes custom "
              "and unclassified extrusion, unlike the existing all-role nominal flow. Zero M221 can produce a valid zero; "
              "encoded flow multipliers are not applied again. Unsupported volumetric-E commands, tool-specific M221 settings "
              "or inch mode leave subsequent new flow observations unknown without changing old parsing or estimation. "
              "A final pending block affects its material; no blocks gives null extrema, while invalid caps or non-finite "
              "ratios only suppress the cap comparison. This is supported linear-E planner evidence, not measured nozzle "
              "output, pressure advance, melting capacity or proof of print quality.",
              "Estimated flow-cap distance integrates the finalized normal-estimator acceleration, cruise and deceleration "
              "phases against the applied material cap. It counts only distance strictly above that cap, not the entire "
              "block when its peak exceeds it. The stored finalized peak is the speed upper bound; equality is not exceeding. "
              "Positive commanded extrusion with zero M221 still participates with zero above-cap distance. Missing or "
              "changed caps, unsupported linear-E metadata or unconsumed blocks leave this independent comparison unknown. "
              "Unused known materials have zero lengths and no ratio. This is planner evidence, not measured melt flow or "
              "proof that the material limit causes a print defect.",
              "Extrusion ranges include only Extrude records across all roles. Feedrate and volumetric rate are commanded/nominal, "
              "not measured melt flow or acceleration-resolved rates.",
              "Use extrusion_by_role to separate model/support paths from custom priming or unclassified extrusion; "
              "custom path height/width estimates are not model layer heights or wall dimensions.",
              "Extruder IDs are zero-based processor/material indices, not necessarily separate physical nozzles. "
              "Role consumption is aggregated across materials; mass depends on configured density and is null if any used "
              "material has missing, non-finite or non-positive density.",
              "Volumetric caps use the current print configuration and the exporter's material-index lookup, including its "
              "first-value fallback. Missing, non-positive or non-finite caps give null; max nominal flow/cap is null without "
              "complete extrusion flow data. The cap's configured-settings snapshot is null if its array contains a non-finite value. "
              "The ratio includes custom extrusion and is not a measured flow or quality verdict.",
              "The other volume bucket is the processor's non-support/non-wipe-tower bucket and may include brim, skirt or "
              "custom extrusion. Missing volume entries are null.",
              "Configured settings are serialized option values from print/object/region scopes, not measured temperature or "
              "proof that a feature produced paths; some regions may have no extrusion.",
              "Non-finite or negative nonnegative metrics are null or excluded with invalid_count, never replaced with zero. "
              "No physical strength, adhesion, temperature or printing-success claim is made."})}};
}

} // namespace Slic3r
