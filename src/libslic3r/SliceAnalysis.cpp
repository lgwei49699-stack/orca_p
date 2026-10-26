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

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <map>
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

struct PlannedFootprint
{
    const Print& print;
    Polygons     polygons;
    size_t       excluded_path_count = 0;
    size_t       invalid_path_count  = 0;
    // Optional category filter, used only when diagnosing one peak hole layer.
    int kind = 0;

    void add_path(const ExtrusionPath& path)
    {
        throw_if_canceled(print);
        if (path.is_force_no_extrusion() || path.mm3_per_mm == 0. || path.polyline.size() < 2) {
            ++excluded_path_count;
            return;
        }
        if (!std::isfinite(path.width) || path.width <= 0.f || !std::isfinite(path.mm3_per_mm) || path.mm3_per_mm < 0.) {
            ++invalid_path_count;
            return;
        }
        const int path_kind = is_bridge(path.role())                                             ? 3 :
                              (path.role() == erPerimeter || path.role() == erExternalPerimeter) ? 1 :
                              path.role() == erGapFill                                           ? 2 :
                                                                                                   4;
        if (kind != 0 && kind != path_kind)
            return;
        path.polygons_covered_by_width(polygons, 0.f);
    }

    void add(const ExtrusionEntity& entity)
    {
        throw_if_canceled(print);
        if (const auto* collection = dynamic_cast<const ExtrusionEntityCollection*>(&entity)) {
            for (const ExtrusionEntity* child : collection->entities)
                add(*child);
        } else if (const auto* path = dynamic_cast<const ExtrusionPath*>(&entity)) {
            add_path(*path);
        } else if (const auto* multipath = dynamic_cast<const ExtrusionMultiPath*>(&entity)) {
            for (const ExtrusionPath& path : multipath->paths)
                add_path(path);
        } else if (const auto* loop = dynamic_cast<const ExtrusionLoop*>(&entity)) {
            for (const ExtrusionPath& path : loop->paths)
                add_path(path);
        } else {
            ++invalid_path_count;
        }
    }
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
                   {"combined_area_mm2", nullptr}};
    if (!result["complete"].get<bool>())
        return result;
    Polygons   combined;
    const auto add_category = [&](const BedFootprint& category, const char* area_key) {
        throw_if_canceled(print);
        const ExPolygons merged = union_ex(category.polygons);
        result[area_key]        = nonnegative_number(area_mm2(merged));
        polygons_append(combined, to_polygons(merged));
    };
    add_category(model, "model_area_mm2");
    add_category(support, "support_and_raft_area_mm2");
    add_category(brim, "brim_area_mm2");
    throw_if_canceled(print);
    const ExPolygons merged     = union_ex(combined);
    result["combined_area_mm2"] = nonnegative_number(area_mm2(merged));
    return result;
}

json solid_fill_peak(
    const Print& print, const Layer* layer, size_t layer_index, const ExPolygons& uncovered, double uncovered_area, bool complete)
{
    json result = {{"scope", "largest_solid_uncovered_layer_exclusive_target_kinds"},
                   {"complete", complete},
                   {"layer_index", nullptr},
                   {"print_z_mm", nullptr},
                   {"uncovered_area_mm2", nullptr},
                   {"surface_area_mm2", nullptr},
                   {"internal_solid_area_mm2", nullptr},
                   {"bridge_area_mm2", nullptr},
                   {"mixed_area_mm2", nullptr}};
    if (!complete)
        return result;
    result["uncovered_area_mm2"] = nonnegative_number(uncovered_area);
    result["mixed_area_mm2"]     = 0.;
    const std::array<const char*, 3> keys{"surface_area_mm2", "internal_solid_area_mm2", "bridge_area_mm2"};
    for (const char* key : keys)
        result[key] = 0.;
    if (layer == nullptr)
        return result;

    result["layer_index"] = layer_index;
    result["print_z_mm"]  = finite_number(layer->print_z);
    std::array<ExPolygons, 3> targets;
    // Classify only the largest already-computed gap, not every layer again.
    // Bridge targets must never be presented as ordinary top/bottom gaps.
    for (const LayerRegion* region : layer->regions())
        for (const Surface& surface : region->fill_surfaces.surfaces) {
            throw_if_canceled(print);
            if (surface.is_solid())
                targets[surface.is_bridge() ? 2 : surface.is_external() ? 0 : 1].push_back(surface.expolygon);
        }
    for (ExPolygons& target : targets) {
        throw_if_canceled(print);
        target = intersection_ex(uncovered, union_ex(target));
    }
    ExPolygons overlaps;
    for (size_t i = 0; i < targets.size(); ++i)
        for (size_t j = i + 1; j < targets.size(); ++j) {
            throw_if_canceled(print);
            append(overlaps, intersection_ex(targets[i], targets[j]));
        }
    overlaps                 = union_ex(overlaps);
    result["mixed_area_mm2"] = nonnegative_number(area_mm2(overlaps));
    for (size_t i = 0; i < targets.size(); ++i) {
        throw_if_canceled(print);
        result[keys[i]] = nonnegative_number(area_mm2(diff_ex(targets[i], overlaps)));
    }
    return result;
}

json residual_peak(const Print& print, const Layer* layer, size_t index, const ExPolygons& residual, double area, bool complete, bool hole)
{
    const std::vector<const char*> keys  = hole ? std::vector<const char*>{"wall_area_mm2", "gap_fill_area_mm2", "bridge_area_mm2",
                                                                           "other_fill_area_mm2", "mixed_area_mm2"} :
                                                  std::vector<const char*>{"retained_region_area_mm2", "absent_region_area_mm2"};
    const char*                    total = hole ? "intrusion_area_mm2" : "unprinted_area_mm2";
    json out = {{"scope", hole ? "largest_hole_intrusion_layer_exclusive_path_kinds" : "largest_unprinted_layer_region_slices"},
                {"complete", complete},
                {"layer_index", nullptr},
                {"print_z_mm", nullptr},
                {total, nullptr}};
    for (const char* key : keys)
        out[key] = nullptr;
    if (!complete)
        return out;
    out[total] = nonnegative_number(area);
    for (const char* key : keys)
        out[key] = 0.;
    if (layer == nullptr)
        return out;
    out["layer_index"] = index;
    out["print_z_mm"]  = finite_number(layer->print_z);
    if (!hole) {
        ExPolygons retained;
        for (const LayerRegion* region : layer->regions())
            for (const Surface& surface : region->slices.surfaces) {
                throw_if_canceled(print);
                retained.push_back(surface.expolygon);
            }
        retained     = union_ex(retained);
        out[keys[0]] = nonnegative_number(area_mm2(intersection_ex(residual, retained)));
        out[keys[1]] = nonnegative_number(area_mm2(diff_ex(residual, retained)));
        return out;
    }
    // Only this maximum layer is traversed again. No per-role/per-layer cache.
    std::array<ExPolygons, 4> kinds;
    for (size_t k = 0; k < kinds.size(); ++k) {
        PlannedFootprint footprint{print};
        footprint.kind = int(k + 1);
        for (const LayerRegion* region : layer->regions()) {
            footprint.add(region->perimeters);
            footprint.add(region->fills);
        }
        kinds[k] = intersection_ex(residual, union_ex(footprint.polygons));
    }
    ExPolygons overlaps;
    for (size_t i = 0; i < kinds.size(); ++i)
        for (size_t j = i + 1; j < kinds.size(); ++j) {
            throw_if_canceled(print);
            append(overlaps, intersection_ex(kinds[i], kinds[j]));
        }
    overlaps         = union_ex(overlaps);
    out[keys.back()] = nonnegative_number(area_mm2(overlaps));
    for (size_t k = 0; k < kinds.size(); ++k) {
        throw_if_canceled(print);
        out[keys[k]] = nonnegative_number(area_mm2(diff_ex(kinds[k], overlaps)));
    }
    return out;
}

json analyze_model_layers(const Print& print, const PrintObject& object)
{
    json         layers     = json::array();
    const Layer* peak_layer = nullptr;
    size_t       peak_index = 0;
    double       peak_area  = 0.;
    ExPolygons   peak_uncovered;
    const Layer* missing_layer = nullptr;
    const Layer* hole_layer    = nullptr;
    size_t       missing_index = 0, hole_index = 0;
    double       missing_area = 0., hole_area = 0.;
    ExPolygons   missing_regions, hole_regions;
    bool         all_complete = !object.layers().empty();
    for (const Layer* layer : object.layers()) {
        throw_if_canceled(print);
        // Keep geometry local to one layer. The business report does not need
        // a second model-wide path cache or three-dimensional material slabs.
        PlannedFootprint footprint{print};
        ExPolygons       solid_target;
        for (const LayerRegion* region : layer->regions()) {
            footprint.add(region->perimeters);
            footprint.add(region->fills);
            // thin_fills are already copied into fills. Sparse infill is not
            // a solid target, whereas deliberately sparse skins still are.
            for (const Surface& surface : region->fill_surfaces.surfaces) {
                throw_if_canceled(print);
                if (surface.is_solid())
                    solid_target.push_back(surface.expolygon);
            }
        }
        throw_if_canceled(print);
        const ExPolygons section         = union_ex(layer->lslices);
        const ExPolygons covered         = union_ex(footprint.polygons);
        const auto       covered_bounds  = get_extents_vector(covered);
        const ExPolygons holes           = bounded_voids(section);
        const bool       complete        = valid_model_layer_geometry(*layer) && footprint.invalid_path_count == 0;
        size_t           unprinted_count = 0, fully_covered_voids = 0;
        double           unprinted_area = 0.;
        ExPolygons       unprinted;
        if (complete) {
            for (const ExPolygon& island : section) {
                throw_if_canceled(print);
                if (positive_region_count(
                        intersection_ex(ExPolygons{island}, bounding_box_candidates(covered, covered_bounds, get_extents(island)))) == 0) {
                    ++unprinted_count;
                    unprinted_area += island.area() * SCALING_FACTOR * SCALING_FACTOR;
                    unprinted.push_back(island);
                }
            }
            for (const ExPolygon& hole : holes) {
                throw_if_canceled(print);
                if (hole.area() > 0. && positive_region_count(diff_ex(ExPolygons{hole}, bounding_box_candidates(covered, covered_bounds,
                                                                                                                get_extents(hole)))) == 0)
                    ++fully_covered_voids;
            }
        }
        solid_target                = union_ex(solid_target);
        ExPolygons   uncovered      = diff_ex(solid_target, covered);
        const double uncovered_area = area_mm2(uncovered);
        all_complete                = all_complete && complete;
        ExPolygons   intrusion      = intersection_ex(holes, covered);
        const double intrusion_area = area_mm2(intrusion);
        if (complete && unprinted_area > missing_area) {
            missing_layer   = layer;
            missing_index   = layers.size();
            missing_area    = unprinted_area;
            missing_regions = std::move(unprinted);
        }
        if (complete && intrusion_area > hole_area) {
            hole_layer   = layer;
            hole_index   = layers.size();
            hole_area    = intrusion_area;
            hole_regions = std::move(intrusion);
        }
        if (complete && uncovered_area > peak_area) {
            peak_layer     = layer;
            peak_index     = layers.size();
            peak_area      = uncovered_area;
            peak_uncovered = std::move(uncovered);
        }
        const auto coverage_area = [complete](double area) { return complete ? nonnegative_number(area) : json(nullptr); };
        layers.push_back({{"layer_index", layers.size()},
                          {"slice_z_mm", finite_number(layer->slice_z)},
                          {"print_z_mm", finite_number(layer->print_z)},
                          {"bottom_z_mm", finite_number(layer->bottom_z())},
                          {"height_mm", nonnegative_number(layer->height)},
                          {"complete", complete},
                          {"excluded_path_count", footprint.excluded_path_count},
                          {"invalid_path_count", footprint.invalid_path_count},
                          {"section_area_mm2", nonnegative_number(area_mm2(section))},
                          {"planned_path_area_mm2", coverage_area(area_mm2(covered))},
                          {"section_island_count", complete ? json(positive_region_count(section)) : json(nullptr)},
                          {"section_min_island_gap_mm", complete ? section_min_island_gap(print, section) : json(nullptr)},
                          {"parallel_boundary_clearance", parallel_boundary_clearance(print, section, complete)},
                          {"completely_unprinted_island_count", complete ? json(unprinted_count) : json(nullptr)},
                          {"completely_unprinted_island_area_mm2", coverage_area(unprinted_area)},
                          {"hole_area_mm2", nonnegative_number(area_mm2(holes))},
                          {"hole_intrusion_area_mm2", coverage_area(intrusion_area)},
                          {"closed_void_region_count", complete ? json(positive_region_count(holes)) : json(nullptr)},
                          {"closed_void_fully_covered_count", complete ? json(fully_covered_voids) : json(nullptr)},
                          {"solid_fill_target_area_mm2", nonnegative_number(area_mm2(solid_target))},
                          {"solid_fill_uncovered_area_mm2", coverage_area(uncovered_area)}});
    }
    return {{"scope", "per_instance_model_paths_before_gcode"},
            {"layers", std::move(layers)},
            {"unprinted_peak", residual_peak(print, missing_layer, missing_index, missing_regions, missing_area, all_complete, false)},
            {"hole_intrusion_peak", residual_peak(print, hole_layer, hole_index, hole_regions, hole_area, all_complete, true)},
            {"solid_fill_peak", solid_fill_peak(print, peak_layer, peak_index, peak_uncovered, peak_area, all_complete)}};
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

json object_summary(const Print& print, const PrintObject& object, size_t index)
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
    json layer_analysis        = analyze_model_layers(print, object);
    return {{"group_index", index},
            {"source_object_id", object.model_object()->id().id},
            {"instance_count", object.instances().size()},
            {"model_layer_count", object.layer_count()},
            {"support_layer_count", object.support_layer_count()},
            {"model_layer_height_mm", layer_heights.to_json()},
            {"first_model_print_z_mm", first == nullptr ? json(nullptr) : finite_number(first->print_z)},
            {"last_model_print_z_mm", last == nullptr ? json(nullptr) : finite_number(last->print_z)},
            {"first_model_layer_section_area_mm2_per_instance", std::move(first_section_area)},
            {"planned_layer_coverage", std::move(layer_analysis)},
            {"configured_settings", object_settings(object)}};
}

json small_perimeter_selection(const GCodeProcessorResult& result)
{
    const auto& value = result.small_perimeter_selection;
    const bool selected = value.loop_count > 0;
    const bool complete = value.available && value.path_count >= value.loop_count &&
        std::isfinite(value.path_length_mm) && (selected ? value.path_length_mm > 0 : value.path_length_mm == 0) &&
        (selected ? std::isfinite(value.min_speed_mm_s) && std::isfinite(value.max_speed_mm_s) &&
                        value.min_speed_mm_s > 0 && value.max_speed_mm_s >= value.min_speed_mm_s : value.path_count == 0);
    return {{"scope", "native_small_perimeter_selection_before_speed_overrides"},
            {"complete", complete},
            {"selected_loop_count", complete ? json(value.loop_count) : json(nullptr)},
            {"selected_path_count", complete ? json(value.path_count) : json(nullptr)},
            {"selected_path_length_mm", complete ? json(value.path_length_mm) : json(nullptr)},
            {"requested_speed_mm_s", {{"min", complete && selected ? json(value.min_speed_mm_s) : json(nullptr)},
                                       {"max", complete && selected ? json(value.max_speed_mm_s) : json(nullptr)}}}};
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

// Stable keys for quality-relevant exported roles, not localized GUI labels.
const char* quality_role(ExtrusionRole role)
{
    switch (role) {
    case erPerimeter: return "inner_wall";
    case erExternalPerimeter: return "outer_wall";
    case erOverhangPerimeter: return "overhang_wall";
    case erBridgeInfill: return "bridge";
    case erInternalBridgeInfill: return "internal_bridge";
    case erSolidInfill: return "internal_solid_infill";
    case erTopSolidInfill: return "top_surface";
    case erBottomSurface: return "bottom_surface";
    case erGapFill: return "gap_infill";
    case erSupportMaterial: return "support";
    case erSupportMaterialInterface: return "support_interface";
    case erSupportTransition: return "support_transition";
    default: return nullptr;
    }
}

struct QualityPathObservation
{
    size_t       move_count = 0;
    bool         complete   = true;
    double       length_mm  = 0.;
    NumericRange speed, fan, width, height;

    size_t below_height_count  = 0;
    double below_height_length = 0.;
    double min_ratio           = std::numeric_limits<double>::infinity();
    double min_width = 0., min_height = 0., min_z = 0.;

    void add(const GCodeProcessorResult::MoveVertex& move)
    {
        ++move_count;
        // A bad observation invalidates this material/role only. Never turn
        // a missing feedrate or width into a successful zero-risk measurement.
        if (!std::isfinite(move.delta_extruder) || !std::isfinite(move.travel_dist) || move.travel_dist <= 0.f ||
            !std::isfinite(move.feedrate) || move.feedrate <= 0.f || !std::isfinite(move.fan_speed) || move.fan_speed < 0.f ||
            move.fan_speed > 100.f || !std::isfinite(move.width) || move.width <= 0.f || !std::isfinite(move.height) ||
            move.height <= 0.f) {
            complete = false;
            return;
        }
        length_mm += move.travel_dist; // Includes native arcs' length, not their endpoint chord.
        complete &= std::isfinite(length_mm);
        speed.add(move.feedrate);
        fan.add(move.fan_speed);
        width.add(move.width);
        height.add(move.height);
        // Compare metadata from the SAME extrusion move. Dividing global min
        // width by global max height can invent an impossible thin bead.
        if (!is_bridge(move.extrusion_role)) {
            const double ratio = double(move.width) / double(move.height);
            if (ratio < min_ratio) {
                min_ratio  = ratio;
                min_width  = move.width;
                min_height = move.height;
                min_z      = move.position.z();
            }
            if (move.width < move.height) {
                ++below_height_count;
                below_height_length += move.travel_dist;
            }
        }
    }

    json to_json(size_t material, const std::string& role) const
    {
        const auto range = [this](const NumericRange& value) -> json {
            return {{"min", complete ? json(value.minimum) : json(nullptr)}, {"max", complete ? json(value.maximum) : json(nullptr)}};
        };
        const bool applicable = role != "bridge" && role != "internal_bridge" && role != "overhang_wall";
        const bool measured   = complete && applicable;
        json       bead       = {{"scope", "same_move_nominal_width_height_non_bridge"},
                                 {"complete", complete},
                                 {"applicable", complete ? json(applicable) : json(nullptr)},
                                 {"below_height_move_count", measured ? json(below_height_count) : json(nullptr)},
                                 {"below_height_path_length_mm", measured ? nonnegative_number(below_height_length) : json(nullptr)},
                                 {"min_ratio", measured ? nonnegative_number(min_ratio) : json(nullptr)},
                                 {"width_at_min_mm", measured ? nonnegative_number(min_width) : json(nullptr)},
                                 {"height_at_min_mm", measured ? nonnegative_number(min_height) : json(nullptr)},
                                 {"print_z_at_min_mm", measured ? finite_number(min_z) : json(nullptr)}};
        return {{"extruder_id", material},
                {"role", role},
                {"complete", complete},
                {"move_count", move_count},
                {"path_length_mm", complete ? json(length_mm) : json(nullptr)},
                {"commanded_speed_mm_s", range(speed)},
                {"fan_speed_percent", range(fan)},
                {"nominal_width_mm", range(width)},
                {"nominal_height_mm", range(height)},
                {"width_height_observation", std::move(bead)}};
    }
};

json gcode_summary(const Print& print, const GCodeProcessorResult& result)
{
    const auto&      config     = print.config();
    const auto&      statistics = result.print_statistics;
    const auto&      normal     = statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    std::set<size_t> material_ids;
    // Reuse the existing report traversal. Bounded summaries answer whether
    // quality-relevant paths actually exist and what commands they received.
    // No per-move coordinates, second parser, or planner changes are introduced.
    std::map<std::pair<size_t, ExtrusionRole>, QualityPathObservation> quality_paths;
    size_t move_index = 0;
    for (const auto& move : result.moves) {
        if (move_index++ % 4096 == 0)
            throw_if_canceled(print);
        if (move.type == EMoveType::Extrude)
            material_ids.insert(move.extruder_id);
        if (move.type == EMoveType::Extrude && !(move.delta_extruder <= 0.f))
            if (quality_role(move.extrusion_role) != nullptr)
                quality_paths[{move.extruder_id, move.extrusion_role}].add(move);
    }
    // A material can have consumption without a drawable move (for example flush).
    const auto include_materials = [&material_ids](const auto& volumes) {
        for (const auto& entry : volumes)
            material_ids.insert(entry.first);
    };
    include_materials(statistics.total_volumes_per_extruder);
    include_materials(statistics.model_volumes_per_extruder);
    include_materials(statistics.support_volumes_per_extruder);
    include_materials(statistics.wipe_tower_volumes_per_extruder);
    include_materials(statistics.flush_per_filament);

    json extruders = json::array();
    for (const size_t id : material_ids) {
        throw_if_canceled(print);
        const auto cap = configured_flow_cap(config, id);
        extruders.push_back({{"extruder_id", id},
                             {"configured_filament_max_volumetric_speed_mm3_s", cap ? json(*cap) : json(nullptr)},
                             {"estimated_peak_flow", estimated_peak_flow(config, result, id)},
                             {"estimated_flow_cap_distance", estimated_flow_cap_distance(config, result, id)},
                             {"layer_time_threshold_observation", layer_time_threshold_observation(print, result, id)},
                             {"estimated_volume_mm3",
                              {{"total", volume_for(statistics.total_volumes_per_extruder, id)},
                               {"other", volume_for(statistics.model_volumes_per_extruder, id)},
                               {"support", volume_for(statistics.support_volumes_per_extruder, id)},
                               {"wipe_tower", volume_for(statistics.wipe_tower_volumes_per_extruder, id)},
                               {"flush", volume_for(statistics.flush_per_filament, id)}}}});
    }
    json path_rows = json::array();
    for (const auto& [identity, observation] : quality_paths)
        path_rows.push_back(observation.to_json(identity.first, quality_role(identity.second)));
    return {{"quality_paths", {{"scope", "final_gcode_positive_extrusion_moves"}, {"rows", std::move(path_rows)}}},
            {"small_perimeter_selection", small_perimeter_selection(result)},
            {"normal_estimate",
             {{"total_time_s", nonnegative_number(normal.time)},
              // Internal denominator for per-material cooling completeness,
              // not a model-layer count or a quality score.
              {"layer_sequence_count", normal.layers_times.size()}}},
            {"extruders", std::move(extruders)}};
}

} // namespace

json make_slice_analysis_report(const Print& print, const GCodeProcessorResult& gcode_result)
{
    throw_if_canceled(print);
    std::set<size_t>             source_ids;
    json                         groups = json::array();
    for (const PrintObject* object : print.objects()) {
        source_ids.insert(object->model_object()->id().id);
        groups.push_back(object_summary(print, *object, groups.size()));
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
        {"limitations", json::array({
            "Read-only observations of this sliced Print and finalized G-code; no parameter selection or printability verdict.",
            "Coverage is per instance before seam, scarf and spiral export transformations, not deposited material. "
            "Section geometry can precede elephant-foot compensation. Sparse infill is not a solid-fill target; "
            "deliberately sparse top/bottom skins may still leave target area uncovered.",
            "Completely unprinted islands and fully covered closed voids describe positive-area XY sections, not "
            "three-dimensional missing parts, blocked holes, connectivity or strength. Nested material is excluded from voids.",
            "Island gap is the boundary distance between separate retained section islands. Parallel-boundary widths "
            "measure only applicable parallel Voronoi sites within a section island, not general minimum wall or slot width. "
            "No applicable measurement is null, not zero.",
            "Bed areas union model, support/raft and brim paths with nominal bottom at the bed. They exclude skirt, "
            "wipe tower and custom G-code and do not prove physical adhesion. Elevated models may have zero model footprint.",
            "Costs, movement speed and flow are estimates. Flow-cap comparisons use configured material limits; "
            "missing limits or unsupported data remain unknown. Layer times are not local cooling intervals.",
            "Quality path roles summarize positive extrusion moves in final G-code. Speeds and fan percentages are commands, "
            "not measured machine motion or cooling. Widths/heights are nominal metadata; small perimeters are not a separate "
            "role. Missing bridge roles do not prove no geometrical bridge or certify support contact.",
            "Source IDs are process-local. Model layers, support layers and G-code layer sequences are different counts. "
            "Settings preserve object and region overrides, including inherited and percentage values.",
            "No surface sample thickness, per-fragment coordinates or exact three-dimensional target/path connectivity "
            "is calculated by this report. Incomplete evidence must not be interpreted as passed."
        })}
    };
}

} // namespace Slic3r
