#include <catch2/catch.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/SliceAnalysis.hpp"
#include "libslic3r/SliceAnalysisGeometry.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

using namespace Slic3r;
using json = nlohmann::json;

namespace {

struct InitializedResult : GCodeProcessorResult
{
    InitializedResult()
    {
        // Production reports receive a finalized result, not this fixture.
        for (auto& mode : print_statistics.modes)
            mode.reset();
    }
};

ModelObject* add_box(Model& model, size_t instances)
{
    ModelObject* object = model.add_object();
    object->name        = "slice-analysis-box";
    object->add_volume(TriangleMesh(its_make_cube(10., 10., 2.)), ModelVolumeType::MODEL_PART, false);
    for (size_t index = 0; index < instances; ++index)
        object->add_instance()->set_offset(Vec3d(20. + 20. * index, 20., 0.));
    return object;
}

void apply_model(Print& print, Model& model)
{
    for (ModelObject* object : model.objects)
        print.auto_assign_extruders(object);
    print.set_status_silent();
    print.apply(model, DynamicPrintConfig::full_print_config());
}

ExPolygon square_with_hole()
{
    Polygon contour({Point(0, 0), Point(scaled(10.), 0), Point(scaled(10.), scaled(10.)), Point(0, scaled(10.))});
    Polygon hole(
        {Point(scaled(3.), scaled(3.)), Point(scaled(7.), scaled(3.)), Point(scaled(7.), scaled(7.)), Point(scaled(3.), scaled(7.))});
    contour.make_counter_clockwise();
    hole.make_clockwise();
    return ExPolygon(contour, hole);
}

ExPolygon rectangle(double x, double y, double width, double height)
{
    return ExPolygon(Polygon({Point(scaled(x), scaled(y)), Point(scaled(x + width), scaled(y)),
                              Point(scaled(x + width), scaled(y + height)), Point(scaled(x), scaled(y + height))}));
}

ExtrusionPath planned_path(double x0, double y0, double x1, double y1, float width = 0.4f, ExtrusionRole role = erExternalPerimeter)
{
    ExtrusionPath path(role, 0.08, width, 0.2f);
    path.polyline = Polyline({Point(scaled(x0), scaled(y0)), Point(scaled(x1), scaled(y1))});
    return path;
}

json coverage_layers(const Print& print)
{
    return make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0).at("planned_layer_coverage").at("layers");
}

json cross_layers(const Print& print)
{
    return make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0).at("cross_layer_geometry").at("layers");
}

json bridge_paths(const Print& print)
{
    return make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0).at("planned_bridge_paths");
}

json perimeter_loops(const Print& print)
{
    return make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0).at("planned_perimeter_loops");
}

json solid_target_depth(const Print& print)
{
    return make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0).at("solid_fill_target_depth");
}

json planned_material_depth(const Print& print)
{
    return make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0).at("planned_material_depth");
}

LayerRegion* add_fill_target_layer(PrintObject& object, double height, double print_z, const Surfaces& surfaces)
{
    Layer* layer = object.add_layer(int(object.layer_count()), height, print_z, print_z - height * 0.5);
    for (const Surface& surface : surfaces)
        layer->lslices.push_back(surface.expolygon);
    LayerRegion* region = layer->add_region(&object.printing_region(0));
    region->fill_surfaces.surfaces = surfaces;
    return region;
}

void require_target_depth(const json& metric, const char* side, double area, double minimum, double maximum)
{
    REQUIRE(metric.at("complete") == true);
    const json& target = metric.at(side);
    REQUIRE(target.at("target_area_sum_mm2").get<double>() == Approx(area).margin(1e-8));
    REQUIRE(target.at("continuous_depth_mm").at("min").get<double>() == Approx(minimum).margin(1e-9));
    REQUIRE(target.at("continuous_depth_mm").at("max").get<double>() == Approx(maximum).margin(1e-9));
}

ExtrusionLoop planned_loop(const Points& points, ExtrusionRole role = erExternalPerimeter)
{
    ExtrusionPath path(role, 0.08, 0.4f, 0.2f);
    path.polyline = Polyline(points);
    return ExtrusionLoop(std::move(path));
}

json external_bridge_layers(const Print& print)
{
    return make_slice_analysis_report(print, InitializedResult{})
        .at("objects").at("groups").at(0).at("external_bridge_projection").at("layers");
}

json volume_sampling(const Print& print, size_t group = 0)
{
    return make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(group).at("source_volume_sampling");
}

json surface_alignment(const Print& print, size_t group = 0)
{
    return make_slice_analysis_report(print, InitializedResult{})
        .at("objects").at("groups").at(group).at("source_surface_layer_alignment");
}

ModelVolume* add_source_facets(ModelObject& object, const std::vector<Vec3f>& vertices,
                              const std::vector<Vec3i32>& indices = {Vec3i32(0, 1, 2)})
{
    indexed_triangle_set mesh;
    mesh.vertices = vertices;
    mesh.indices  = indices;
    return object.add_volume(TriangleMesh(mesh), ModelVolumeType::MODEL_PART, false);
}

json support_projection(const Print& print, size_t group = 0)
{
    return make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(group).at("planned_support_projection");
}

using SettingsSnapshot = std::tuple<PrintConfig, std::vector<PrintObjectConfig>, std::vector<PrintRegionConfig>>;

SettingsSnapshot all_settings(const Print& print)
{
    // Compare complete typed configuration copies. Serializing every internal
    // option is unrelated to reporting and not valid for all default enums.
    SettingsSnapshot result{print.config(), {}, {}};
    for (const PrintObject* object : print.objects()) {
        std::get<1>(result).push_back(object->config());
        for (size_t index = 0; index < object->num_printing_regions(); ++index)
            std::get<2>(result).push_back(object->printing_region(index).config());
    }
    return result;
}

GCodeProcessorResult::MoveVertex extrusion(unsigned char extruder, float width, float height, float feedrate, float volume_per_mm)
{
    GCodeProcessorResult::MoveVertex result;
    result.type           = EMoveType::Extrude;
    result.extrusion_role = erExternalPerimeter;
    result.extruder_id    = extruder;
    result.width          = width;
    result.height         = height;
    result.feedrate       = feedrate;
    result.mm3_per_mm     = volume_per_mm;
    return result;
}

void require_empty_range(const json& range)
{
    REQUIRE(range.at("count") == 0);
    REQUIRE(range.at("min").is_null());
    REQUIRE(range.at("max").is_null());
}

// Independent test-only reference: retain the original full-XY depth-origin
// partition scan. Production material queries must not pay for this scan.
json full_run_material_depth_reference(const PrintObject& object)
{
    struct Slab { coord_t bottom, top; ExPolygons polygons; };
    std::vector<Slab> slabs;
    std::map<coord_t, ExPolygons> top_targets, bottom_targets;
    const auto z_event = [](double value) { return coord_t(std::round(static_cast<long double>(value) / SCALING_FACTOR)); };
    const auto area = [](const ExPolygons& polygons) {
        double result = 0.;
        for (const ExPolygon& polygon : polygons)
            result += polygon.area() * SCALING_FACTOR * SCALING_FACTOR;
        return result;
    };
    const auto collect = [&](auto&& self, const ExtrusionEntity& entity, std::map<float, Polygons>& by_height) -> void {
        const auto add = [&](const ExtrusionPath& path) {
            if (path.is_force_no_extrusion() || path.mm3_per_mm == 0. || path.polyline.size() < 2)
                return;
            REQUIRE(std::isfinite(path.height));
            REQUIRE(path.height > 0.f);
            REQUIRE(std::isfinite(path.mm3_per_mm));
            REQUIRE(path.mm3_per_mm > 0.);
            polygons_append(by_height[path.height], path.polygons_covered_by_width(0.f));
        };
        if (const auto* collection = dynamic_cast<const ExtrusionEntityCollection*>(&entity))
            for (const ExtrusionEntity* child : collection->entities)
                self(self, *child, by_height);
        else if (const auto* path = dynamic_cast<const ExtrusionPath*>(&entity))
            add(*path);
        else if (const auto* loop = dynamic_cast<const ExtrusionLoop*>(&entity))
            for (const ExtrusionPath& path : loop->paths)
                add(path);
        else if (const auto* multipath = dynamic_cast<const ExtrusionMultiPath*>(&entity))
            for (const ExtrusionPath& path : multipath->paths)
                add(path);
        else
            FAIL("unsupported reference fixture entity");
    };
    for (const Layer* layer : object.layers()) {
        std::map<float, Polygons> by_height;
        for (const LayerRegion* region : layer->regions()) {
            collect(collect, region->perimeters, by_height);
            collect(collect, region->fills, by_height);
            for (const Surface& surface : region->fill_surfaces.surfaces) {
                if (!surface.is_solid() || surface.empty() || surface.area() <= 0.)
                    continue;
                const double thickness = surface.thickness == -1. ? layer->height : surface.thickness;
                if (surface.is_top())
                    top_targets[z_event(layer->print_z)].push_back(surface.expolygon);
                if (surface.is_bottom())
                    bottom_targets[z_event(layer->print_z - thickness)].push_back(surface.expolygon);
            }
        }
        for (const auto& [height, polygons] : by_height)
            slabs.push_back({z_event(layer->print_z - double(height)), z_event(layer->print_z), union_ex(polygons)});
    }
    const auto measure = [&](bool downward) {
        struct Event { std::vector<size_t> starts, ends; ExPolygons targets; };
        std::map<coord_t, Event> events;
        for (size_t index = 0; index < slabs.size(); ++index) {
            const Slab& slab = slabs[index];
            events[downward ? slab.bottom : -slab.top].starts.push_back(index);
            events[downward ? slab.top : -slab.bottom].ends.push_back(index);
        }
        for (const auto& [z, seed] : downward ? top_targets : bottom_targets)
            events[downward ? z : -z].targets = seed;
        std::set<size_t> active;
        ExPolygons previous;
        std::map<coord_t, ExPolygons> runs;
        double area_sum = 0., uncovered_sum = 0., minimum = 0., maximum = 0.;
        bool have_depth = false;
        const auto record = [&](double depth) {
            minimum = have_depth ? std::min(minimum, depth) : depth;
            maximum = have_depth ? std::max(maximum, depth) : depth;
            have_depth = true;
        };
        for (const auto& [z, event] : events) {
            const ExPolygons targets = union_ex(event.targets);
            const double target_area = area(targets);
            if (target_area > 0.) {
                area_sum += target_area;
                const double uncovered = area(diff_ex(targets, previous));
                uncovered_sum += uncovered;
                if (uncovered > 0.)
                    record(0.);
                const auto record_first = [&, query_z = z](auto begin, auto end) {
                    for (auto it = begin; it != end; ++it)
                        if (area(intersection_ex(targets, it->second)) > 0.) {
                            record(double((static_cast<long double>(query_z) - it->first) * SCALING_FACTOR));
                            return;
                        }
                    REQUIRE(uncovered > 0.);
                };
                record_first(runs.begin(), runs.end());
                record_first(runs.rbegin(), runs.rend());
            }
            for (size_t index : event.ends)
                active.erase(index);
            for (size_t index : event.starts)
                active.insert(index);
            ExPolygons pieces;
            for (size_t index : active)
                append(pieces, slabs[index].polygons);
            ExPolygons current = union_ex(pieces);
            const ExPolygons lost = diff_ex(previous, current);
            if (!lost.empty())
                for (auto it = runs.begin(); it != runs.end();) {
                    it->second = diff_ex(it->second, lost);
                    if (it->second.empty())
                        it = runs.erase(it);
                    else
                        ++it;
                }
            ExPolygons added = diff_ex(current, previous);
            if (!added.empty())
                runs.emplace(z, std::move(added));
            previous = std::move(current);
        }
        return json{{"target_area_sum_mm2", area_sum}, {"uncovered_start_area_sum_mm2", uncovered_sum},
                    {"continuous_depth_mm", {{"min", have_depth ? json(minimum) : json(nullptr)},
                                             {"max", have_depth ? json(maximum) : json(nullptr)}}}};
    };
    return {{"scope", "per_instance_fixed_xy_model_path_slabs_before_gcode"}, {"z_resolution_mm", SCALING_FACTOR},
            {"complete", true}, {"top", measure(true)}, {"bottom", measure(false)}};
}

} // namespace

TEST_CASE("Slice analysis empty data remains explicitly absent", "[SliceAnalysis]")
{
    Print                   print;
    const InitializedResult result;
    const json              report = make_slice_analysis_report(print, result);

    REQUIRE(report.at("schema_version") == 1);
    REQUIRE(report.at("scope") == "sliced_print_metrics");
    REQUIRE(report.at("objects").at("source_object_count") == 0);
    REQUIRE(report.at("objects").at("slice_object_group_count") == 0);
    REQUIRE(report.at("objects").at("instance_count") == 0);
    REQUIRE(report.at("objects").at("groups").empty());
    const auto& gcode = report.at("gcode");
    REQUIRE(gcode.at("move_counts").at("total") == 0);
    REQUIRE(gcode.at("extruders").empty());
    REQUIRE(gcode.at("filament_by_role").empty());
    require_empty_range(gcode.at("normal_estimate").at("layer_duration_s"));
    for (const auto& range : gcode.at("extrusion_ranges"))
        if (range.is_object())
            require_empty_range(range);
    REQUIRE(gcode.at("extrusion_ranges").at("path_length_mm").is_null());
    REQUIRE(gcode.at("normal_estimate").at("layer_duration_sequence_s").empty());
    REQUIRE(gcode.at("seam_record_count") == 0);
    REQUIRE_FALSE(report.at("limitations").empty());
    REQUIRE(json::parse(report.dump()) == report);
}

TEST_CASE("Slice analysis distinguishes instances, model layers and support layers", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    REQUIRE(print.objects().size() == 1);
    PrintObject* object = print.get_object(0);
    // Elevated first model layer can sit above a raft; this is section area,
    // not a claim that any of it touches the build plate.
    object->add_layer(3, 0.2, 1.2, 1.1)->lslices = {square_with_hole()};
    object->add_layer(4, 0.3, 1.5, 1.35);
    object->add_support_layer(0, 0, 0.4, 0.4);
    const InitializedResult result;
    const json              report  = make_slice_analysis_report(print, result);
    const auto&             objects = report.at("objects");
    REQUIRE(objects.at("source_object_count") == 1);
    REQUIRE(objects.at("slice_object_group_count") == 1);
    REQUIRE(objects.at("instance_count") == 2);
    const auto& group = objects.at("groups").at(0);
    REQUIRE(group.at("instance_count") == 2);
    REQUIRE(group.at("model_layer_count") == 2);
    REQUIRE(group.at("support_layer_count") == 1);
    REQUIRE(group.at("first_model_print_z_mm").get<double>() == Approx(1.2));
    REQUIRE(group.at("last_model_print_z_mm").get<double>() == Approx(1.5));
    REQUIRE(group.at("model_layer_height_mm").at("min").get<double>() == Approx(0.2));
    REQUIRE(group.at("model_layer_height_mm").at("max").get<double>() == Approx(0.3));
    REQUIRE(group.at("model_layer_height_mm").at("count") == 2);
    REQUIRE(group.at("first_model_layer_section_area_mm2_per_instance").get<double>() == Approx(84.));
    REQUIRE(group.at("planned_model_bed_footprint_area_mm2_per_instance") == 0.);
    const auto& layers = group.at("planned_layer_coverage").at("layers");
    REQUIRE(layers.size() == 2);
    REQUIRE(layers.at(0).at("bottom_z_mm").get<double>() == Approx(1.0));
    REQUIRE(layers.at(0).at("height_mm").get<double>() == Approx(0.2));
    REQUIRE(layers.at(0).at("section_area_mm2").get<double>() == Approx(84.));
    REQUIRE(layers.at(0).at("hole_area_mm2").get<double>() == Approx(16.));
    REQUIRE(layers.at(0).at("completely_unprinted_island_count") == 1);
    REQUIRE(layers.at(1).at("section_area_mm2") == 0.);
    REQUIRE(layers.at(1).at("completely_unprinted_island_count") == 0);
    REQUIRE(layers.at(1).at("section_min_island_gap_mm").is_null());
    for (const char* key : {"section_island_count", "section_islands_with_multiple_planned_regions_count",
                            "planned_regions_overlapping_multiple_section_islands_count", "closed_void_region_count",
                            "closed_void_fully_covered_count", "closed_voids_with_multiple_remaining_regions_count",
                            "planned_enclosed_exterior_region_count", "planned_enclosed_exterior_area_mm2"})
        REQUIRE(layers.at(1).at(key) == 0);
}

TEST_CASE("Slice analysis source objects are deduplicated across transformed groups", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* object = add_box(model, 2);
    object->instances.back()->set_scaling_factor(Vec3d(2., 1., 1.));
    apply_model(print, model);
    REQUIRE(print.objects().size() == 2);

    const json report = make_slice_analysis_report(print, InitializedResult{});
    REQUIRE(report.at("objects").at("source_object_count") == 1);
    REQUIRE(report.at("objects").at("slice_object_group_count") == 2);
    REQUIRE(report.at("objects").at("instance_count") == 2);
    for (const auto& group : report.at("objects").at("groups")) {
        REQUIRE(group.at("instance_count") == 1);
        // PrintObject::size() may use the source object's first instance;
        // it is not an accurate bound for this differently scaled group.
        REQUIRE_FALSE(group.contains("slicing_bounds_size_mm"));
        REQUIRE(group.at("first_model_print_z_mm").is_null());
        REQUIRE(group.at("first_model_layer_section_area_mm2_per_instance").is_null());
        REQUIRE(group.at("planned_model_bed_footprint_area_mm2_per_instance").is_null());
        require_empty_range(group.at("model_layer_height_mm"));
    }
}

TEST_CASE("Slice analysis does not mutate settings or layer geometry", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 1);
    source->config.set_key_value("wall_generator", new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));
    apply_model(print, model);
    PrintObject* object              = print.get_object(0);
    Layer*       layer               = object->add_layer(0, 0.25, 0.25, 0.125);
    layer->lslices                   = {square_with_hole()};
    const auto              before   = all_settings(print);
    const ExPolygons        original = layer->lslices;
    const InitializedResult result;

    const json first  = make_slice_analysis_report(print, result);
    const json second = make_slice_analysis_report(print, result);

    REQUIRE(first == second);
    REQUIRE(all_settings(print) == before);
    REQUIRE(layer == object->get_layer(0));
    REQUIRE(layer->height == Approx(0.25));
    REQUIRE(layer->lslices.front().area() == original.front().area());
    REQUIRE(layer->lslices.front().contour.points == original.front().contour.points);
    REQUIRE(layer->lslices.front().holes.front().points == original.front().holes.front().points);
    const auto& settings = first.at("objects").at("groups").at(0).at("configured_settings");
    REQUIRE(settings.at("object").at("wall_generator") == "arachne");
    REQUIRE(settings.at("regions").size() == object->num_printing_regions());
    REQUIRE(settings.at("regions").at(0).at("settings").contains("bridge_speed"));
}

TEST_CASE("Slice analysis preserves processor units and material scopes", "[SliceAnalysis]")
{
    Print             print;
    InitializedResult result;
    result.filament_densities = {1.24f, 1.27f, 1.24f, 1.24f};
    result.moves.push_back(extrusion(0, 0.4f, 0.2f, 20.f, 0.08f));
    auto arc                 = extrusion(1, 0.6f, 0.3f, 30.f, 0.18f);
    arc.move_path_type       = EMovePathType::Arc_move_ccw;
    arc.interpolation_points = {Vec3f(1.f, 1.f, 0.f), Vec3f(2.f, 0.f, 0.f)};
    result.moves.push_back(arc);
    for (const auto type : {EMoveType::Travel, EMoveType::Retract, EMoveType::Unretract, EMoveType::Wipe, EMoveType::Noop}) {
        GCodeProcessorResult::MoveVertex move;
        move.type     = type;
        move.width    = 100.f;
        move.feedrate = 1000.f;
        result.moves.push_back(move);
    }
    auto& statistics                           = result.print_statistics;
    auto& normal                               = statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    normal.time                                = 60.f;
    normal.prepare_time                        = 10.f;
    normal.layers_times                        = {20.f, 40.f};
    normal.roles_times                         = {{erExternalPerimeter, 35.f}, {erSupportMaterial, 15.f}};
    statistics.total_volumes_per_extruder      = {{0, 12.5}, {1, 25.}, {3, 4.}};
    statistics.model_volumes_per_extruder      = {{0, 10.}, {1, 20.}};
    statistics.support_volumes_per_extruder    = {{0, 2.5}};
    statistics.wipe_tower_volumes_per_extruder = {{1, 5.}};
    statistics.flush_per_filament              = {{3, 4.}};
    statistics.used_filaments_per_role         = {{erExternalPerimeter, {1.25, 3.5}}, {erSupportMaterial, {0.25, 0.8}}};

    const json gcode = make_slice_analysis_report(print, result).at("gcode");
    REQUIRE(gcode.at("normal_estimate").at("total_time_s") == 60.);
    REQUIRE(gcode.at("normal_estimate").at("prepare_time_s") == 10.);
    REQUIRE(gcode.at("normal_estimate").at("layer_sequence_count") == 2);
    REQUIRE(gcode.at("normal_estimate").at("layer_duration_s").at("min") == 20.);
    REQUIRE(gcode.at("normal_estimate").at("layer_duration_s").at("max") == 40.);
    REQUIRE(gcode.at("normal_estimate").at("role_time_s").at(0).at("role") == "external_perimeter");
    REQUIRE(gcode.at("normal_estimate").at("role_time_s").at(0).at("estimated_time_s") == 35.);
    REQUIRE(gcode.at("move_counts").at("total") == 7);
    REQUIRE(gcode.at("move_counts").at("extrude") == 2);
    for (const auto* key : {"travel", "retract", "unretract", "wipe", "other"})
        REQUIRE(gcode.at("move_counts").at(key) == 1);
    const auto& ranges = gcode.at("extrusion_ranges");
    REQUIRE(ranges.at("width_mm").at("count") == 2);
    REQUIRE(ranges.at("width_mm").at("min").get<double>() == Approx(0.4));
    REQUIRE(ranges.at("width_mm").at("max").get<double>() == Approx(0.6));
    REQUIRE(ranges.at("height_mm").at("min").get<double>() == Approx(0.2));
    REQUIRE(ranges.at("commanded_feedrate_mm_s").at("max") == 30.);
    REQUIRE(ranges.at("nominal_volumetric_rate_mm3_s").at("min").get<double>() == Approx(1.6));
    REQUIRE(ranges.at("nominal_volumetric_rate_mm3_s").at("max").get<double>() == Approx(5.4));
    const auto& materials = gcode.at("extruders");
    REQUIRE(materials.size() == 3);
    REQUIRE(materials.at(0).at("extruder_id") == 0);
    REQUIRE(materials.at(1).at("extruder_id") == 1);
    REQUIRE(materials.at(2).at("extruder_id") == 3);
    REQUIRE(materials.at(0).at("estimated_volume_mm3").at("total") == 12.5);
    REQUIRE(materials.at(0).at("estimated_volume_mm3").at("other") == 10.);
    REQUIRE(materials.at(0).at("estimated_volume_mm3").at("support") == 2.5);
    REQUIRE(materials.at(1).at("estimated_volume_mm3").at("wipe_tower") == 5.);
    REQUIRE(materials.at(2).at("estimated_volume_mm3").at("flush") == 4.);
    REQUIRE(materials.at(2).at("estimated_volume_mm3").at("other").is_null());
    require_empty_range(materials.at(2).at("extrusion_ranges").at("width_mm"));
    REQUIRE(materials.at(0).at("extrusion_ranges").at("width_mm").at("count") == 1);
    REQUIRE(materials.at(1).at("extrusion_ranges").at("width_mm").at("min").get<double>() == Approx(0.6));
    REQUIRE(gcode.at("filament_by_role").at(0).at("role") == "external_perimeter");
    REQUIRE(gcode.at("filament_by_role").at(0).at("estimated_length_m") == 1.25);
    REQUIRE(gcode.at("filament_by_role").at(0).at("estimated_mass_g") == 3.5);
}

TEST_CASE("Slice analysis rejects invalid numbers without silently replacing them with zero", "[SliceAnalysis]")
{
    Print             print;
    InitializedResult result;
    const float       nan                 = std::numeric_limits<float>::quiet_NaN();
    const float       inf                 = std::numeric_limits<float>::infinity();
    result.moves                          = {extrusion(0, nan, inf, nan, 0.1f), extrusion(0, -0.1f, 0.2f, 10.f, inf)};
    auto& statistics                      = result.print_statistics;
    auto& normal                          = statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    normal.time                           = nan;
    normal.prepare_time                   = -1.f;
    normal.layers_times                   = {nan, inf, -1.f, 5.f};
    normal.roles_times                    = {{erExternalPerimeter, nan}};
    statistics.total_volumes_per_extruder = {{0, inf}};
    statistics.used_filaments_per_role    = {{erExternalPerimeter, {nan, -1.}}};

    const json  report = make_slice_analysis_report(print, result);
    const auto& gcode  = report.at("gcode");
    REQUIRE(gcode.at("move_counts").at("extrude") == 2);
    const auto& ranges = gcode.at("extrusion_ranges");
    require_empty_range(ranges.at("width_mm"));
    REQUIRE(ranges.at("width_mm").at("invalid_count") == 2);
    require_empty_range(ranges.at("nominal_volumetric_rate_mm3_s"));
    REQUIRE(ranges.at("nominal_volumetric_rate_mm3_s").at("invalid_count") == 2);
    REQUIRE(ranges.at("height_mm").at("invalid_count") == 1);
    REQUIRE(ranges.at("height_mm").at("count") == 1);
    REQUIRE(ranges.at("height_mm").at("min").get<double>() == Approx(0.2));
    REQUIRE(gcode.at("normal_estimate").at("total_time_s").is_null());
    REQUIRE(gcode.at("normal_estimate").at("prepare_time_s").is_null());
    REQUIRE(gcode.at("normal_estimate").at("role_time_s").at(0).at("estimated_time_s").is_null());
    REQUIRE(gcode.at("normal_estimate").at("layer_duration_s").at("invalid_count") == 3);
    REQUIRE(gcode.at("normal_estimate").at("layer_duration_s").at("min") == 5.);
    REQUIRE(gcode.at("normal_estimate").at("layer_duration_sequence_s") == json::array({nullptr, nullptr, nullptr, 5.}));
    REQUIRE(gcode.at("extruders").at(0).at("estimated_volume_mm3").at("total").is_null());
    REQUIRE(gcode.at("filament_by_role").at(0).at("estimated_length_m").is_null());
    REQUIRE(gcode.at("filament_by_role").at(0).at("estimated_mass_g").is_null());
    REQUIRE(json::parse(report.dump()) == report);
}

TEST_CASE("Slice analysis uses planned material widths without counting sparse interiors as solid targets", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {rectangle(0., 0., 10., 10.), rectangle(20., 0., 2., 2.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fill_surfaces.append({rectangle(0., 0., 10., 10.)}, stInternal);
    region->fill_surfaces.append({rectangle(20., 0., 2., 2.)}, stInternalVoid);
    const ExtrusionPath path = planned_path(1., 5., 9., 5.);
    region->perimeters.append(path);
    // A duplicate region/path and thin_fills must not double count area.
    layer->add_region(&object->printing_region(0))->fills.append(path);
    region->thin_fills.append(planned_path(20., 1., 22., 1., 2.f));

    const auto  layers = coverage_layers(print);
    const auto& first  = layers.at(0);
    REQUIRE(first.at("complete") == true);
    REQUIRE(first.at("section_area_mm2").get<double>() == Approx(104.));
    REQUIRE(first.at("planned_path_area_mm2").get<double>() == Approx(3.2).margin(1e-5));
    REQUIRE(first.at("section_covered_area_mm2") == first.at("planned_path_area_mm2"));
    REQUIRE(first.at("section_uncovered_area_mm2").get<double>() > 100.);
    REQUIRE(first.at("solid_fill_target_area_mm2") == 0.);
    REQUIRE(first.at("solid_fill_uncovered_area_mm2") == 0.);
    REQUIRE(first.at("completely_unprinted_island_count") == 1);
    REQUIRE(first.at("completely_unprinted_island_area_mm2").get<double>() == Approx(4.));
    const auto group = make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0);
    REQUIRE(group.at("instance_count") == 2);
    REQUIRE(group.at("planned_model_bed_footprint_area_mm2_per_instance").get<double>() == Approx(3.2).margin(1e-5));
}

TEST_CASE("Slice analysis planned bed footprint distinguishes model paths from the first section and from elevation", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {rectangle(0., 0., 10., 10.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->perimeters.append(planned_path(1., 5., 9., 5.));
    const auto footprint = [&print]() {
        return make_slice_analysis_report(print, InitializedResult{})
            .at("objects")
            .at("groups")
            .at(0)
            .at("planned_model_bed_footprint_area_mm2_per_instance");
    };

    REQUIRE(footprint().get<double>() == Approx(3.2).margin(1e-5));
    layer->print_z = 0.2 + SCALING_FACTOR * 0.5;
    REQUIRE(footprint().get<double>() == Approx(3.2).margin(1e-5));
    layer->print_z = 1.2;
    object->add_support_layer(0, 0, 0.4, 0.4);
    REQUIRE(footprint() == 0.);
    layer->print_z = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(footprint().is_null());
    layer->print_z = 0.1; // Invalid below-bed model bottom, not zero contact.
    REQUIRE(footprint().is_null());
    layer->print_z = 0.2;
    region->fills.append(planned_path(0., 5., 10., 5., std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(footprint().is_null());
}

TEST_CASE("Slice analysis excludes travel-only paths and preserves nested extrusion entities", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object        = print.get_object(0);
    Layer*       layer         = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices             = {rectangle(0., 0., 10., 10.)};
    LayerRegion*  region       = layer->add_region(&object->printing_region(0));
    ExtrusionPath no_extrusion = planned_path(0., 5., 10., 5., 10.f);
    no_extrusion.set_force_no_extrusion(true);
    region->perimeters.append(no_extrusion);
    ExtrusionPath zero_flow = planned_path(0., 5., 10., 5., 10.f);
    zero_flow.mm3_per_mm    = 0.;
    region->fills.append(zero_flow);
    REQUIRE(coverage_layers(print).at(0).at("planned_path_area_mm2") == 0.);
    REQUIRE(coverage_layers(print).at(0).at("excluded_path_count") == 2);

    auto* nested    = new ExtrusionEntityCollection;
    auto* multipath = new ExtrusionMultiPath;
    multipath->paths.push_back(planned_path(1., 1., 9., 1.));
    nested->entities.push_back(multipath);
    auto* loop = new ExtrusionLoop;
    loop->paths.push_back(planned_path(1., 1., 9., 1.));
    nested->entities.push_back(loop);
    region->fills.entities.push_back(nested);
    const auto first = coverage_layers(print).at(0);
    REQUIRE(first.at("planned_path_area_mm2").get<double>() == Approx(3.2).margin(1e-5));
    REQUIRE(first.at("completely_unprinted_island_count") == 0);
    REQUIRE(first.at("excluded_path_count") == 2);
}

TEST_CASE("Slice analysis measures hole intrusion but does not count nested model islands as holes", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {square_with_hole(), rectangle(4., 4., 2., 2.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->perimeters.append(planned_path(4.5, 5., 5.5, 5., 1.f));
    const auto first = coverage_layers(print).at(0);
    REQUIRE(first.at("section_area_mm2").get<double>() == Approx(88.));
    REQUIRE(first.at("hole_area_mm2").get<double>() == Approx(12.));
    REQUIRE(first.at("hole_intrusion_area_mm2") == 0.);
    REQUIRE(first.at("completely_unprinted_island_count") == 1);
    REQUIRE(first.at("completely_unprinted_island_area_mm2").get<double>() == Approx(84.));

    region->perimeters.append(planned_path(1., 5., 9., 5., 1.f));
    const auto crossing = coverage_layers(print).at(0);
    REQUIRE(crossing.at("hole_intrusion_area_mm2").get<double>() == Approx(2.));
    REQUIRE(crossing.at("completely_unprinted_island_count") == 0);
}

TEST_CASE("Slice analysis keeps configured solid targets separate and unions overlapping regions", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.3, 0.3, 0.15);
    layer->lslices      = {rectangle(0., 0., 10., 10.)};
    for (size_t index = 0; index < 2; ++index) {
        LayerRegion* region = layer->add_region(&object->printing_region(0));
        region->fill_surfaces.append({rectangle(0., 0., 2., 2.)}, stTop);
        region->fill_surfaces.append({rectangle(2., 0., 2., 2.)}, stBottomBridge);
        region->fill_surfaces.append({rectangle(4., 0., 2., 2.)}, stInternalSolid);
        region->fill_surfaces.append({rectangle(6., 0., 2., 2.)}, stInternalBridge);
        region->fill_surfaces.append({rectangle(0., 2., 10., 8.)}, stInternal);
        region->fills.append(planned_path(0., 1., 2., 1., 2.f));
    }
    const auto first = coverage_layers(print).at(0);
    REQUIRE(first.at("solid_fill_target_area_mm2").get<double>() == Approx(16.));
    REQUIRE(first.at("top_fill_target_area_mm2").get<double>() == Approx(4.));
    REQUIRE(first.at("bottom_fill_target_area_mm2").get<double>() == Approx(4.));
    REQUIRE(first.at("top_fill_uncovered_area_mm2") == 0.);
    REQUIRE(first.at("bottom_fill_uncovered_area_mm2").get<double>() == Approx(4.));
    REQUIRE(first.at("solid_fill_uncovered_area_mm2").get<double>() == Approx(12.));
    REQUIRE(first.at("section_uncovered_area_mm2").get<double>() > 90.);
}

TEST_CASE("Slice analysis does not turn invalid footprint data or cancellation into complete coverage", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {rectangle(0., 0., 10., 10.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fills.append(planned_path(0., 5., 10., 5., std::numeric_limits<float>::quiet_NaN()));
    const auto first = coverage_layers(print).at(0);
    REQUIRE(first.at("complete") == false);
    REQUIRE(first.at("invalid_path_count") == 1);
    REQUIRE(first.at("section_area_mm2").get<double>() == Approx(100.));
    for (const char* key :
         {"planned_path_area_mm2", "section_covered_area_mm2", "section_uncovered_area_mm2", "completely_unprinted_island_count",
          "completely_unprinted_island_area_mm2", "hole_intrusion_area_mm2", "solid_fill_uncovered_area_mm2", "top_fill_uncovered_area_mm2",
          "bottom_fill_uncovered_area_mm2", "section_island_count", "section_islands_with_multiple_planned_regions_count",
          "planned_regions_overlapping_multiple_section_islands_count", "closed_void_region_count", "closed_void_fully_covered_count",
          "closed_voids_with_multiple_remaining_regions_count", "planned_enclosed_exterior_region_count",
          "planned_enclosed_exterior_area_mm2", "section_min_island_gap_mm"})
        REQUIRE(first.at(key).is_null());
    print.cancel();
    REQUIRE_THROWS_AS(make_slice_analysis_report(print, InitializedResult{}), CanceledException);
}

TEST_CASE("Slice analysis sums arc-aware extrusion distances and keeps seam counts out of extrusion", "[SliceAnalysis]")
{
    Print             print;
    InitializedResult result;
    auto              line = extrusion(0, 0.4f, 0.2f, 20.f, 0.08f);
    line.travel_dist       = 2.f;
    auto arc               = line;
    arc.move_path_type     = EMovePathType::Arc_move_cw;
    arc.travel_dist        = 3.1415927f;
    arc.position           = Vec3f(2.f, 0.f, 0.2f);
    auto travel            = line;
    travel.type            = EMoveType::Travel;
    travel.travel_dist     = 100.f; // Travel must not contribute to extrusion totals.
    auto seam              = travel;
    seam.type              = EMoveType::Seam;
    result.moves           = {line, arc, travel, seam};
    const auto  gcode      = make_slice_analysis_report(print, result).at("gcode");
    const auto& ranges     = gcode.at("extrusion_ranges");
    REQUIRE(ranges.at("segment_length_mm").at("count") == 2);
    REQUIRE(ranges.at("segment_length_mm").at("min") == 2.);
    REQUIRE(ranges.at("path_length_mm").get<double>() == Approx(5.1415927));
    REQUIRE(gcode.at("extrusion_by_role").at(0).at("extrusion_ranges").at("path_length_mm") == ranges.at("path_length_mm"));
    REQUIRE(gcode.at("extruders").at(0).at("extrusion_ranges").at("path_length_mm") == ranges.at("path_length_mm"));
    REQUIRE(gcode.at("move_counts").at("total") == 4);
    REQUIRE(gcode.at("move_counts").at("extrude") == 2);
    REQUIRE(gcode.at("move_counts").at("travel") == 1);
    REQUIRE(gcode.at("move_counts").at("other") == 1);
    REQUIRE(gcode.at("seam_record_count") == 1);

    result.moves.front().travel_dist = std::numeric_limits<float>::infinity();
    const auto invalid               = make_slice_analysis_report(print, result).at("gcode").at("extrusion_ranges");
    REQUIRE(invalid.at("segment_length_mm").at("invalid_count") == 1);
    REQUIRE(invalid.at("path_length_mm").is_null());
}

TEST_CASE("Slice analysis reports travel separately and preserves signed seam bounds", "[SliceAnalysis]")
{
    Print print;
    InitializedResult result;
    auto move = extrusion(0, 0.4f, 0.2f, 20.f, 0.08f);
    move.travel_dist = 100.f;
    result.moves.push_back(move);
    move.type = EMoveType::Travel;
    move.travel_dist = 3.f;
    result.moves.push_back(move);
    move.move_path_type = EMovePathType::Arc_move_ccw;
    move.travel_dist = 6.f;
    result.moves.push_back(move);
    move.type = EMoveType::Wipe;
    move.travel_dist = 100.f;
    result.moves.push_back(move);
    move.type = EMoveType::Seam;
    move.position = Vec3f(-4.f, 8.f, 0.2f);
    result.moves.push_back(move);
    move.position = Vec3f(2.f, -1.f, 1.2f);
    result.moves.push_back(move);

    const auto metrics = [&]() { return make_slice_analysis_report(print, result).at("gcode"); };
    const auto report = metrics();
    REQUIRE(report.at("travel").at("path_length_mm") == 9.);
    REQUIRE(report.at("travel").at("segment_length_mm").at("count") == 2);
    REQUIRE(report.at("extrusion_ranges").at("path_length_mm") == 100.);
    REQUIRE(report.at("seam_record_count") == 2);
    const auto& bounds = report.at("seam_position_bounds_mm");
    REQUIRE(bounds.at("complete") == true);
    REQUIRE(bounds.at("min") == json::array({-4.f, -1.f, 0.2f}));
    REQUIRE(bounds.at("max") == json::array({2.f, 8.f, 1.2f}));
    const auto& seams = report.at("seam_positions_mm");
    REQUIRE(seams.at("scope") == "gcode_processor_preview_coordinates_in_move_order");
    REQUIRE(seams.at("complete") == true);
    REQUIRE(seams.at("positions") == json::array({json::array({-4.f, 8.f, 0.2f}), json::array({2.f, -1.f, 1.2f})}));

    result.moves[1].travel_dist = -1.f;
    result.moves.back().position.x() = std::numeric_limits<float>::quiet_NaN();
    const auto invalid = metrics();
    REQUIRE(invalid.at("travel").at("path_length_mm").is_null());
    REQUIRE(invalid.at("travel").at("segment_length_mm").at("invalid_count") == 1);
    REQUIRE(invalid.at("seam_position_bounds_mm").at("complete") == false);
    REQUIRE(invalid.at("seam_position_bounds_mm").at("min").is_null());
    REQUIRE(invalid.at("seam_position_bounds_mm").at("max").is_null());
    REQUIRE(invalid.at("seam_positions_mm").at("complete") == false);
    REQUIRE(invalid.at("seam_positions_mm").at("positions").is_null());
    REQUIRE(invalid.at("extrusion_ranges") == report.at("extrusion_ranges"));

    result.moves.clear();
    const auto empty = metrics();
    REQUIRE(empty.at("travel").at("path_length_mm") == 0.);
    REQUIRE(empty.at("seam_position_bounds_mm").at("complete") == true);
    REQUIRE(empty.at("seam_position_bounds_mm").at("min").is_null());
    REQUIRE(empty.at("seam_positions_mm").at("complete") == true);
    REQUIRE(empty.at("seam_positions_mm").at("positions") == json::array());
}

TEST_CASE("Slice analysis retains every seam marker in order or marks the entire position list unavailable", "[SliceAnalysis]")
{
    Print print;
    InitializedResult result;
    auto move = extrusion(0, 0.4f, 0.2f, 20.f, 0.08f);
    move.type = EMoveType::Seam;
    move.position = Vec3f(2.f, -1.f, -0.25f);
    result.moves.push_back(move);
    move.type = EMoveType::Travel;
    move.position = Vec3f(99.f, 99.f, 99.f);
    result.moves.push_back(move);
    move.type = EMoveType::Seam;
    move.position = Vec3f(-4.f, 8.f, 0.5f);
    result.moves.push_back(move);
    move.position = Vec3f(2.f, -1.f, -0.25f);
    result.moves.push_back(move);
    bool valid = true;
    SECTION("signed duplicate positions keep their original order") {}
    SECTION("an invalid first marker does not return the later valid subset") {
        result.moves.front().position.y() = std::numeric_limits<float>::infinity();
        valid = false;
    }
    SECTION("an invalid middle marker discards both earlier and later subsets") {
        result.moves[2].position.z() = std::numeric_limits<float>::quiet_NaN();
        valid = false;
    }
    const auto positions_before = [&]() {
        std::vector<Vec3f> points;
        for (const auto& record : result.moves)
            points.push_back(record.position);
        return points;
    }();
    const json report = make_slice_analysis_report(print, result);
    const json& gcode = report.at("gcode");
    REQUIRE(gcode.at("seam_record_count") == 3);
    REQUIRE(gcode.at("move_counts").at("other") == 3);
    REQUIRE(gcode.at("seam_positions_mm").at("complete") == valid);
    if (valid)
        REQUIRE(gcode.at("seam_positions_mm").at("positions") ==
                json::array({json::array({2.f, -1.f, -0.25f}), json::array({-4.f, 8.f, 0.5f}), json::array({2.f, -1.f, -0.25f})}));
    else
        REQUIRE(gcode.at("seam_positions_mm").at("positions").is_null());
    REQUIRE(make_slice_analysis_report(print, result) == report);
    for (size_t index = 0; index < result.moves.size(); ++index)
        for (int axis = 0; axis < 3; ++axis) {
            const float before = positions_before[index][axis];
            const float after = result.moves[index].position[axis];
            REQUIRE((before == after || (std::isnan(before) && std::isnan(after))));
        }
}

TEST_CASE("Gcode processor records G1 travel displacement rather than the preceding extrusion", "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    processor.apply_config(PrintConfig{});
    processor.initialize("slice-analysis-travel-regression.gcode");
    processor.process_buffer("G90\nM83\nT0\nG1 Z0.2 F1200\nG1 X10 E1\nG1 X13 Y4\nG1 E-1\nG1 E1\n"
                             "G1 X16 Y8\nG2 X14 Y8 I-1 J0\nG92 X100 Y100\nG91\nG1 X3 Y4\nG90\nG1 X100 Y100\n"
                             "G1 Z1.2 E-1\n");
    processor.finalize(false);
    std::vector<float> distances;
    for (const auto& move : processor.get_result().moves)
        if (move.type == EMoveType::Travel)
            distances.push_back(move.travel_dist);
    REQUIRE(distances.size() == 7);
    REQUIRE(distances[0] == Approx(0.2));
    REQUIRE(distances[1] == Approx(5.));
    REQUIRE(distances[2] == Approx(5.));
    REQUIRE(distances[3] == Approx(M_PI));
    REQUIRE(distances[4] == Approx(5.));
    REQUIRE(distances[5] == Approx(5.));
    REQUIRE(distances[6] == Approx(1.));
    Print print;
    const auto travel = make_slice_analysis_report(print, processor.get_result()).at("gcode").at("travel");
    REQUIRE(travel.at("path_length_mm").get<double>() == Approx(21.2 + M_PI));
}

TEST_CASE("Slice analysis exposes processor seam preview positions with plate and extruder offsets", "[SliceAnalysis][GCodeProcessor]")
{
    PrintConfig config;
    config.extruder_offset.values = {Vec2d(2., -3.)};
    GCodeProcessor processor(true);
    processor.apply_config(config);
    processor.initialize("slice-analysis-seam-positions.gcode");
    processor.set_xy_offset(100., -200.);
    processor.process_buffer("G90\nM83\nT0\nG1 Z0.2 F1200\nG1 X-4 Y8\n;" +
                             GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role) +
                             ExtrusionEntity::role_to_string(erExternalPerimeter) +
                             "\nG1 X0 Y8 E1\nG1 X0 Y11 E1\nG1 X-4 Y11 E1\nG1 X-4 Y8 E1\nG1 X-3 Y8\n");
    processor.finalize(false);
    json stored_positions = json::array();
    for (const auto& move : processor.get_result().moves)
        if (move.type == EMoveType::Seam)
            stored_positions.push_back(json::array({move.position.x(), move.position.y(), move.position.z()}));
    REQUIRE(stored_positions.size() == 1);
    REQUIRE(stored_positions.at(0) == json::array({98.f, -195.f, 0.2f}));
    Print print;
    const auto gcode = make_slice_analysis_report(print, processor.get_result()).at("gcode");
    REQUIRE(gcode.at("seam_positions_mm").at("complete") == true);
    REQUIRE(gcode.at("seam_positions_mm").at("positions") == stored_positions);
    REQUIRE(gcode.at("seam_position_bounds_mm").at("min") == stored_positions.front());
    REQUIRE(gcode.at("seam_position_bounds_mm").at("max") == stored_positions.front());
}

TEST_CASE("Slice analysis weights above-cap nominal flow by length and each material cap", "[SliceAnalysis]")
{
    Model model;
    Print print;
    auto config = DynamicPrintConfig::full_print_config();
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75}));
    config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats({5., 10.}));
    print.set_status_silent();
    print.apply(model, config);
    InitializedResult result;
    auto high = extrusion(0, 0.4f, 0.2f, 2.f, 4.f);
    high.travel_dist = 2.f;
    auto low = high;
    low.feedrate = 1.f;
    low.travel_dist = 8.f;
    auto other_material = high;
    other_material.extruder_id = 1;
    other_material.travel_dist = 10.f;
    auto priming = high;
    priming.extrusion_role = erCustom;
    priming.travel_dist = 40.f;
    result.moves = {high, low, other_material, priming};
    const auto role = [&](ExtrusionRole selected) {
        const auto roles = make_slice_analysis_report(print, result).at("gcode").at("extrusion_by_role");
        return *std::find_if(roles.begin(), roles.end(), [&](const auto& row) { return row.at("role_id") == selected; });
    };
    const auto wall = role(erExternalPerimeter);
    REQUIRE(wall.at("extrusion_ranges").at("path_length_mm") == 20.);
    REQUIRE(wall.at("configured_flow_comparison").at("complete") == true);
    REQUIRE(wall.at("configured_flow_comparison").at("above_cap_path_length_mm") == 2.);
    REQUIRE(wall.at("configured_flow_comparison").at("above_cap_path_length_ratio").get<double>() == Approx(0.1));
    REQUIRE(role(erCustom).at("configured_flow_comparison").at("above_cap_path_length_ratio") == 1.);

    SECTION("Unknown cap invalidates only the affected role") {
        config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats({5., 0.}));
        print.apply(model, config);
        const auto flow = role(erExternalPerimeter).at("configured_flow_comparison");
        REQUIRE(flow.at("complete") == false);
        REQUIRE(flow.at("above_cap_path_length_mm").is_null());
        REQUIRE(flow.at("above_cap_path_length_ratio").is_null());
        REQUIRE(role(erCustom).at("configured_flow_comparison").at("complete") == true);
    }
    SECTION("Fallback cap follows the same material lookup as export") {
        config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats({5.}));
        print.apply(model, config);
        REQUIRE(role(erExternalPerimeter).at("configured_flow_comparison").at("above_cap_path_length_ratio").get<double>() == Approx(0.6));
    }
    SECTION("Invalid nominal rate is not replaced with zero") {
        result.moves.front().feedrate = std::numeric_limits<float>::infinity();
        REQUIRE(role(erExternalPerimeter).at("configured_flow_comparison").at("complete") == false);
        REQUIRE(role(erExternalPerimeter).at("configured_flow_comparison").at("above_cap_path_length_ratio").is_null());
    }
    SECTION("Zero length has no applicable ratio") {
        for (auto& move : result.moves)
            move.travel_dist = 0.f;
        const auto flow = role(erExternalPerimeter).at("configured_flow_comparison");
        REQUIRE(flow.at("complete") == true);
        REQUIRE(flow.at("above_cap_path_length_mm") == 0.);
        REQUIRE(flow.at("above_cap_path_length_ratio").is_null());
    }
}

TEST_CASE("Slice analysis counts material-active layer sequences against each configured cooling threshold", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75}));
    config.set_key_value("slow_down_for_layer_cooling", new ConfigOptionBools({true, true}));
    config.set_key_value("slow_down_layer_time", new ConfigOptionFloats({5., 7.}));
    print.set_status_silent();
    print.apply(model, config);
    InitializedResult result;
    result.moves = {extrusion(0, 0.4f, 0.2f, 20.f, 0.08f), extrusion(1, 0.4f, 0.2f, 20.f, 0.08f)};
    auto& normal = result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    normal.layers_times = {2.f, 5.f, 10.f, 5.f};
    normal.prepare_time = 1.f; // The report uses the raw first sequence, without a new subtraction.
    result.layer_extruder_ids = {{0, 1}, {0}, {0, 1}, {1}};
    result.layer_extruder_ids_complete = true;
    const auto ids_before = result.layer_extruder_ids;
    const auto settings_before = all_settings(print);
    const json report = make_slice_analysis_report(print, result);
    const auto& materials = report.at("gcode").at("extruders");
    const auto& first = materials.at(0).at("layer_time_threshold_observation");
    REQUIRE(first.at("scope") == "final_gcode_layer_sequences_per_material");
    REQUIRE(first.at("complete") == true);
    REQUIRE(first.at("applicable") == true);
    REQUIRE(first.at("slow_down_enabled") == true);
    REQUIRE(first.at("configured_threshold_s") == 5.);
    REQUIRE(first.at("active_layer_sequence_count") == 3);
    REQUIRE(first.at("below_threshold_layer_sequence_count") == 1); // Exactly 5 seconds is not below 5.
    REQUIRE(first.at("below_threshold_layer_ratio").get<double>() == Approx(1. / 3.));
    const auto& second = materials.at(1).at("layer_time_threshold_observation");
    REQUIRE(second.at("configured_threshold_s") == 7.);
    REQUIRE(second.at("active_layer_sequence_count") == 3);
    REQUIRE(second.at("below_threshold_layer_sequence_count") == 2);
    REQUIRE(second.at("below_threshold_layer_ratio").get<double>() == Approx(2. / 3.));
    REQUIRE(report.at("gcode").at("normal_estimate").at("layer_duration_sequence_s") == json::array({2., 5., 10., 5.}));
    REQUIRE(report.at("configured_settings").at("slow_down_for_layer_cooling") == "1,1");
    REQUIRE(make_slice_analysis_report(print, result) == report);
    REQUIRE(result.layer_extruder_ids == ids_before);
    REQUIRE(all_settings(print) == settings_before);
}

TEST_CASE("Slice analysis keeps disabled zero and unused cooling thresholds not applicable", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    InitializedResult result;
    result.moves = {extrusion(0, 0.4f, 0.2f, 20.f, 0.08f)};
    result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].layers_times = {2.f};
    result.layer_extruder_ids = {{0}};
    result.layer_extruder_ids_complete = true;
    bool enabled = true;
    double threshold = 5.;
    size_t active = 1;
    SECTION("disabled slowdown is not a zero short-layer count") { enabled = false; }
    SECTION("zero threshold is disabled for this observation") { threshold = 0.; }
    SECTION("only startup consumption has no classified active sequence") {
        result.moves.front().extrusion_role = erCustom;
        result.layer_extruder_ids = {{}};
        active = 0;
    }
    config.set_key_value("slow_down_for_layer_cooling", new ConfigOptionBools({enabled}));
    config.set_key_value("slow_down_layer_time", new ConfigOptionFloats({threshold}));
    print.set_status_silent();
    print.apply(model, config);
    const json row = make_slice_analysis_report(print, result).at("gcode").at("extruders").at(0).at("layer_time_threshold_observation");
    REQUIRE(row.at("complete") == true);
    REQUIRE(row.at("applicable") == false);
    REQUIRE(row.at("slow_down_enabled") == enabled);
    REQUIRE(row.at("configured_threshold_s") == threshold);
    REQUIRE(row.at("active_layer_sequence_count") == active);
    REQUIRE(row.at("below_threshold_layer_sequence_count").is_null());
    REQUIRE(row.at("below_threshold_layer_ratio").is_null());
}

TEST_CASE("Slice analysis never substitutes zero for unknown layer material membership or cooling configuration", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("slow_down_for_layer_cooling", new ConfigOptionBools({true}));
    config.set_key_value("slow_down_layer_time", new ConfigOptionFloats({5.}));
    InitializedResult result;
    result.moves = {extrusion(0, 0.4f, 0.2f, 20.f, 0.08f)};
    auto& durations = result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].layers_times;
    durations = {2.f};
    result.layer_extruder_ids = {{0}};
    result.layer_extruder_ids_complete = true;
    SECTION("a result without retained metadata is unknown") { result.layer_extruder_ids_complete = false; }
    SECTION("a shortened sequence map is unknown") { result.layer_extruder_ids.clear(); }
    SECTION("a nonfinite active duration is unknown") { durations.front() = std::numeric_limits<float>::quiet_NaN(); }
    SECTION("a negative active duration is unknown") { durations.front() = -1.f; }
    SECTION("a negative threshold is invalid") { config.set_key_value("slow_down_layer_time", new ConfigOptionFloats({-1.})); }
    SECTION("a nonfinite threshold is invalid without losing other report fields") {
        config.set_key_value("slow_down_layer_time", new ConfigOptionFloats({std::numeric_limits<double>::quiet_NaN()}));
    }
    SECTION("a missing threshold is not replaced with a default") {
        config.set_key_value("slow_down_layer_time", new ConfigOptionFloats());
    }
    SECTION("a missing enabled flag is unknown") {
        config.set_key_value("slow_down_for_layer_cooling", new ConfigOptionBools());
    }
    print.set_status_silent();
    print.apply(model, config);
    const json report = make_slice_analysis_report(print, result);
    const auto& row = report.at("gcode").at("extruders").at(0).at("layer_time_threshold_observation");
    REQUIRE(row.at("complete") == false);
    for (const char* key :
         {"applicable", "active_layer_sequence_count", "below_threshold_layer_sequence_count", "below_threshold_layer_ratio"})
        REQUIRE(row.at(key).is_null());
    REQUIRE(report.at("gcode").at("move_counts").at("extrude") == 1);
    REQUIRE(json::parse(report.dump()) == report);
}

TEST_CASE("Slice analysis uses exporter material fallback and validates only each material's active durations", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75}));
    config.set_key_value("slow_down_for_layer_cooling", new ConfigOptionBools({true}));
    config.set_key_value("slow_down_layer_time", new ConfigOptionFloats({5.}));
    print.set_status_silent();
    print.apply(model, config);
    InitializedResult result;
    result.moves = {extrusion(0, 0.4f, 0.2f, 20.f, 0.08f), extrusion(1, 0.4f, 0.2f, 20.f, 0.08f)};
    result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].layers_times =
        {std::numeric_limits<float>::quiet_NaN(), 6.f, 6.f};
    result.layer_extruder_ids = {{0}, {1}, {1}};
    result.layer_extruder_ids_complete = true;
    const auto materials = make_slice_analysis_report(print, result).at("gcode").at("extruders");
    REQUIRE(materials.at(0).at("layer_time_threshold_observation").at("complete") == false);
    const auto& second = materials.at(1).at("layer_time_threshold_observation");
    REQUIRE(second.at("complete") == true);
    REQUIRE(second.at("slow_down_enabled") == true);
    REQUIRE(second.at("configured_threshold_s") == 5.);
    REQUIRE(second.at("active_layer_sequence_count") == 2); // Equal durations remain two distinct sequences.
    REQUIRE(second.at("below_threshold_layer_sequence_count") == 0);
    REQUIRE(second.at("below_threshold_layer_ratio") == 0.);
}

TEST_CASE("Gcode processor retains classified material sequences without changing display times", "[SliceAnalysis][GCodeProcessor]")
{
    bool finish_with_park = true;
    SECTION("a completed final interval retains all three sequences") {}
    SECTION("an unestimated final block keeps material membership incomplete") { finish_with_park = false; }
    PrintConfig config;
    config.filament_diameter.values = {1.75, 1.75};
    GCodeProcessor processor(true);
    processor.apply_config(config);
    processor.initialize("slice-analysis-layer-materials.gcode");
    const auto tag = [](GCodeProcessor::ETags value) { return ";" + GCodeProcessor::reserved_tag(value) + "\n"; };
    const auto role = [](ExtrusionRole value) {
        return ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role) + ExtrusionEntity::role_to_string(value) + "\n";
    };
    processor.process_buffer("G90\nM83\nT0\n" + role(erCustom) + "G1 X2 E1 F600\n" +
                             tag(GCodeProcessor::ETags::Layer_Change) + role(erExternalPerimeter) +
                             "G1 Z0.2 F1200\nG1 X10 E1\nG1 X20 E1\nT1\n" + role(erSupportMaterial) + "G1 X30 E1\n" +
                             tag(GCodeProcessor::ETags::Layer_Change) + "T0\n" + role(erNone) + "G1 X40 E1\nT1\n" +
                             role(erInternalInfill) + "G1 X50 E1\nG1 X60 E0\n" +
                             tag(GCodeProcessor::ETags::Layer_Change) + "T0\n" + role(erSkirt) +
                             // The tool change flushes the planner. A final park gives the last interval
                             // the two blocks required by the existing estimator; the counterexample omits it.
                             "G1 X70 E1\n" + (finish_with_park ? "G1 X80 F6000\nM400\n" : ""));
    const auto before = processor.get_result().moves;
    REQUIRE(processor.get_result().layer_extruder_ids_complete == false);
    for (const auto& move : before) {
        if (move.type != EMoveType::Extrude || move.extrusion_role == erCustom || move.extrusion_role == erNone)
            continue;
        CAPTURE(move.gcode_id, move.extrusion_role, move.extruder_id, move.layer_duration, move.mm3_per_mm, move.travel_dist);
        REQUIRE(move.layer_duration >= 1.f);
        REQUIRE(move.layer_duration <= 3.f);
        REQUIRE(std::isfinite(move.mm3_per_mm));
        REQUIRE(move.mm3_per_mm > 0.f);
        REQUIRE(move.travel_dist > 0.f);
    }
    processor.finalize(false);
    const auto& result = processor.get_result();
    const auto& normal = result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    if (!finish_with_park) {
        REQUIRE(normal.layers_times.size() == 2);
        REQUIRE(result.layer_extruder_ids_complete == false);
        REQUIRE((result.layer_extruder_ids == std::vector<std::vector<unsigned char>>{{0, 1}, {1}}));
        return; // The missing third estimate must not be manufactured from its Z or temporary ID.
    }
    REQUIRE(normal.layers_times.size() == 3);
    REQUIRE(result.layer_extruder_ids_complete == true);
    REQUIRE((result.layer_extruder_ids == std::vector<std::vector<unsigned char>>{{0, 1}, {1}, {0}}));
    REQUIRE(result.moves.size() == before.size());
    const auto custom = std::find_if(normal.roles_times.begin(), normal.roles_times.end(),
                                     [](const auto& item) { return item.first == erCustom; });
    const float prepare = custom == normal.roles_times.end() ? 0.f : custom->second;
    for (size_t index = 0; index < before.size(); ++index) {
        const auto& old_move = before[index];
        const auto& new_move = result.moves[index];
        REQUIRE(new_move.gcode_id == old_move.gcode_id);
        REQUIRE(new_move.type == old_move.type);
        REQUIRE(new_move.feedrate == old_move.feedrate);
        REQUIRE(new_move.travel_dist == old_move.travel_dist);
        REQUIRE(new_move.position == old_move.position);
        const size_t layer_id = size_t(old_move.layer_duration);
        const float expected = layer_id == 0 ? 0.f : layer_id == 1 ? std::max(0.f, normal.layers_times[0] - prepare)
                                                                                : normal.layers_times[layer_id - 1];
        REQUIRE(new_move.layer_duration == expected);
    }
    GCodeProcessorResult copied;
    copied = result;
    REQUIRE(copied.layer_extruder_ids_complete == true);
    REQUIRE(copied.layer_extruder_ids == result.layer_extruder_ids);
    copied.reset();
    REQUIRE(copied.layer_extruder_ids_complete == false);
    REQUIRE(copied.layer_extruder_ids.empty());
    REQUIRE(result.layer_extruder_ids_complete == true);
}

TEST_CASE("Gcode processor does not assign untagged model extrusion to an inferred layer", "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    processor.apply_config(PrintConfig{});
    processor.initialize("slice-analysis-untagged-model.gcode");
    processor.process_buffer("G90\nM83\nT0\n;" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role) +
                             ExtrusionEntity::role_to_string(erExternalPerimeter) + "\nG1 Z0.2 F600\nG1 X10 E1\n");
    processor.finalize(false);
    REQUIRE(processor.get_result().layer_extruder_ids_complete == false);
    REQUIRE((processor.get_result().layer_extruder_ids == std::vector<std::vector<unsigned char>>{{}}));
}

TEST_CASE("Slice analysis maps nominal flow caps by material ID and follows exporter configuration lookup", "[SliceAnalysis]")
{
    Model              model;
    Print              print;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75}));
    config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats({10., 20.}));
    print.set_status_silent();
    print.apply(model, config);
    InitializedResult result;
    result.extruders_count = 2;
    result.moves           = {extrusion(0, 0.4f, 0.2f, 40.f, 0.1f), extrusion(1, 0.4f, 0.2f, 60.f, 0.1f)};
    const auto materials   = [&]() { return make_slice_analysis_report(print, result).at("gcode").at("extruders"); };

    const auto distinct = materials();
    REQUIRE(make_slice_analysis_report(print, result).at("configured_settings").at("filament_max_volumetric_speed") ==
            config.option("filament_max_volumetric_speed")->serialize());
    REQUIRE(distinct.at(0).at("configured_filament_max_volumetric_speed_mm3_s") == 10.);
    REQUIRE(distinct.at(1).at("configured_filament_max_volumetric_speed_mm3_s") == 20.);
    REQUIRE(distinct.at(0).at("max_nominal_flow_to_cap_ratio").get<double>() == Approx(0.4));
    REQUIRE(distinct.at(1).at("max_nominal_flow_to_cap_ratio").get<double>() == Approx(0.3));

    config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats({10.}));
    print.apply(model, config);
    REQUIRE(materials().at(1).at("configured_filament_max_volumetric_speed_mm3_s") == 10.);
    REQUIRE(materials().at(1).at("max_nominal_flow_to_cap_ratio").get<double>() == Approx(0.6));

    result.moves.front().feedrate = std::numeric_limits<float>::infinity();
    REQUIRE(materials().at(0).at("configured_filament_max_volumetric_speed_mm3_s") == 10.);
    REQUIRE(materials().at(0).at("max_nominal_flow_to_cap_ratio").is_null());
    result.moves.clear();
    result.print_statistics.total_volumes_per_extruder = {{1, 5.}, {3, 5.}};
    const auto no_moves                                = materials();
    REQUIRE(no_moves.at(0).at("extruder_id") == 1);
    REQUIRE(no_moves.at(0).at("configured_filament_max_volumetric_speed_mm3_s") == 10.);
    REQUIRE(no_moves.at(0).at("max_nominal_flow_to_cap_ratio").is_null());
    REQUIRE(no_moves.at(1).at("extruder_id") == 3);
    REQUIRE(no_moves.at(1).at("configured_filament_max_volumetric_speed_mm3_s").is_null());
    REQUIRE(no_moves.at(1).at("max_nominal_flow_to_cap_ratio").is_null());
}

TEST_CASE("Slice analysis leaves unlimited or invalid volumetric caps unassessed", "[SliceAnalysis]")
{
    Model              model;
    Print              print;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    print.set_status_silent();
    InitializedResult result;
    result.moves = {extrusion(0, 0.4f, 0.2f, 40.f, 0.1f)};
    for (double cap : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        CAPTURE(cap);
        config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats({cap}));
        print.apply(model, config);
        const auto  report   = make_slice_analysis_report(print, result);
        const auto& material = report.at("gcode").at("extruders").at(0);
        REQUIRE(material.at("configured_filament_max_volumetric_speed_mm3_s").is_null());
        REQUIRE(material.at("max_nominal_flow_to_cap_ratio").is_null());
        if (std::isfinite(cap))
            REQUIRE(report.at("configured_settings").at("filament_max_volumetric_speed") ==
                    config.option("filament_max_volumetric_speed")->serialize());
        else
            REQUIRE(report.at("configured_settings").at("filament_max_volumetric_speed").is_null());
        REQUIRE(json::parse(report.dump()) == report);
    }
}

TEST_CASE("Slice analysis separates custom extrusion estimates from model role ranges", "[SliceAnalysis]")
{
    Print             print;
    InitializedResult result;
    auto              wall    = extrusion(0, 0.4f, 0.2f, 20.f, 0.08f);
    auto              priming = extrusion(0, 2.38f, 10.6f, 5.f, 2.f);
    priming.extrusion_role    = erCustom;
    result.moves              = {wall, priming};

    const json  gcode    = make_slice_analysis_report(print, result).at("gcode");
    const auto& combined = gcode.at("extrusion_ranges");
    REQUIRE(combined.at("height_mm").at("count") == 2);
    REQUIRE(combined.at("height_mm").at("max").get<double>() == Approx(10.6));
    const auto& by_role = gcode.at("extrusion_by_role");
    REQUIRE(by_role.size() == 2);
    for (const auto& role : by_role) {
        const auto& ranges = role.at("extrusion_ranges");
        REQUIRE(ranges.at("height_mm").at("count") == 1);
        if (role.at("role") == "external_perimeter") {
            REQUIRE(ranges.at("height_mm").at("max").get<double>() == Approx(0.2));
            REQUIRE(ranges.at("width_mm").at("max").get<double>() == Approx(0.4));
        } else {
            REQUIRE(role.at("role") == "custom");
            REQUIRE(ranges.at("height_mm").at("max").get<double>() == Approx(10.6));
            REQUIRE(ranges.at("width_mm").at("max").get<double>() == Approx(2.38));
        }
    }
}

TEST_CASE("Slice analysis does not claim a known role mass with missing or invalid material density", "[SliceAnalysis]")
{
    Print             print;
    InitializedResult result;
    result.moves                                    = {extrusion(0, 0.4f, 0.2f, 20.f, 0.08f), extrusion(1, 0.4f, 0.2f, 20.f, 0.08f)};
    result.print_statistics.used_filaments_per_role = {{erExternalPerimeter, {1., 0.}}, {erSupportMaterial, {0.5, 2.}}};

    SECTION("missing density for a used material") { result.filament_densities = {1.24f}; }
    SECTION("zero density") { result.filament_densities = {1.24f, 0.f}; }
    SECTION("negative density") { result.filament_densities = {1.24f, -1.f}; }
    SECTION("non-finite density") { result.filament_densities = {1.24f, std::numeric_limits<float>::infinity()}; }

    const json roles = make_slice_analysis_report(print, result).at("gcode").at("filament_by_role");
    REQUIRE(roles.at(0).at("estimated_length_m") == 1.);
    for (const auto& role : roles)
        REQUIRE(role.at("estimated_mass_g").is_null());
}

TEST_CASE("Slice analysis compares adjacent model sections using their actual variable layer coordinates", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    PrintObject* object                           = print.get_object(0);
    object->add_layer(7, 0.2, 0.2, 0.1)->lslices  = {rectangle(0., 0., 4., 4.)};
    object->add_layer(8, 0.3, 0.5, 0.35)->lslices = {rectangle(0., 0., 6., 4.)};

    const json layers = cross_layers(print);
    REQUIRE(layers.size() == 2);
    const auto& first = layers.at(0);
    REQUIRE_FALSE(first.at("complete").get<bool>());
    REQUIRE(first.at("section_area_mm2").get<double>() == Approx(16.));
    for (const char* key :
         {"previous_layer_index", "previous_slice_z_mm", "previous_print_z_mm", "previous_model_gap_mm", "previous_model_overlap_area_mm2",
          "outside_previous_model_area_mm2", "no_previous_overlap_island_count", "no_previous_overlap_island_area_mm2"})
        REQUIRE(first.at(key).is_null());
    const auto& second = layers.at(1);
    REQUIRE(second.at("complete") == true);
    REQUIRE(second.at("layer_index") == 1);
    REQUIRE(second.at("previous_layer_index") == 0);
    REQUIRE(second.at("slice_z_mm").get<double>() == Approx(0.35));
    REQUIRE(second.at("print_z_mm").get<double>() == Approx(0.5));
    REQUIRE(second.at("bottom_z_mm").get<double>() == Approx(0.2));
    REQUIRE(second.at("height_mm").get<double>() == Approx(0.3));
    REQUIRE(second.at("previous_slice_z_mm").get<double>() == Approx(0.1));
    REQUIRE(second.at("previous_print_z_mm").get<double>() == Approx(0.2));
    REQUIRE(second.at("previous_model_gap_mm").get<double>() == Approx(0.).margin(1e-12));
    REQUIRE(second.at("section_area_mm2").get<double>() == Approx(24.));
    REQUIRE(second.at("previous_model_overlap_area_mm2").get<double>() == Approx(16.));
    REQUIRE(second.at("outside_previous_model_area_mm2").get<double>() == Approx(8.));
    REQUIRE(second.at("no_previous_overlap_island_count") == 0);
    REQUIRE(second.at("no_previous_overlap_island_area_mm2") == 0.);
    REQUIRE(coverage_layers(print).at(1).at("slice_z_mm") == second.at("slice_z_mm"));
    // Geometry remains per-instance and does not require any generated path.
    REQUIRE(coverage_layers(print).at(1).at("completely_unprinted_island_count") == 1);
}

TEST_CASE("Slice analysis distinguishes new section islands, holes and boundary-only contact", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object   = print.get_object(0);
    Layer*       previous = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer*       current  = object->add_layer(1, 0.2, 0.4, 0.3);
    previous->lslices     = {rectangle(0., 0., 4., 4.)};
    current->lslices      = {rectangle(1., 1., 2., 2.), rectangle(10., 0., 2., 2.)};
    double overlap = 4., outside = 4., island_area = 4.;
    size_t island_count = 1;
    SECTION("one existing and one new island") {}
    SECTION("new island inside a lower-layer hole")
    {
        previous->lslices = {square_with_hole()};
        current->lslices  = {rectangle(4., 4., 2., 2.)};
        overlap           = 0.;
    }
    SECTION("a nested lower-layer material island is not a hole")
    {
        previous->lslices = {square_with_hole(), rectangle(4., 4., 2., 2.)};
        current->lslices  = {rectangle(4., 4., 2., 2.)};
        outside = island_area = 0.;
        island_count          = 0;
    }
    SECTION("edge-only contact has no positive-area overlap")
    {
        current->lslices = {rectangle(4., 0., 2., 2.)};
        overlap          = 0.;
    }
    SECTION("point-only contact has no positive-area overlap")
    {
        current->lslices = {rectangle(4., 4., 2., 2.)};
        overlap          = 0.;
    }
    SECTION("an empty previous model section does not act as material")
    {
        previous->lslices.clear();
        current->lslices = {rectangle(4., 4., 2., 2.)};
        overlap          = 0.;
    }
    SECTION("an empty current section has no new island")
    {
        current->lslices.clear();
        overlap = outside = island_area = 0.;
        island_count                    = 0;
    }
    const json second = cross_layers(print).at(1);
    REQUIRE(second.at("complete") == true);
    REQUIRE(second.at("section_area_mm2").get<double>() == Approx(overlap + outside));
    REQUIRE(second.at("previous_model_overlap_area_mm2").get<double>() == Approx(overlap));
    REQUIRE(second.at("outside_previous_model_area_mm2").get<double>() == Approx(outside));
    REQUIRE(second.at("no_previous_overlap_island_count") == island_count);
    REQUIRE(second.at("no_previous_overlap_island_area_mm2").get<double>() == Approx(island_area));
}

TEST_CASE("Slice analysis does not interpret raft elevation or intervening support as adjacent model contact", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object                          = print.get_object(0);
    object->add_layer(4, 0.2, 1.2, 0.1)->lslices = {rectangle(0., 0., 4., 4.)};
    object->add_layer(5, 0.2, 1.6, 0.3)->lslices = {rectangle(10., 0., 2., 2.)};
    const json    without_support                = cross_layers(print);
    SupportLayer* support                        = object->add_support_layer(2, 2, 0.2, 1.4);
    support->support_fills.append(planned_path(10., 1., 12., 1., 2.f));
    REQUIRE(cross_layers(print) == without_support);
    const auto& first  = without_support.at(0);
    const auto& second = without_support.at(1);
    REQUIRE_FALSE(first.at("complete").get<bool>());
    REQUIRE(first.at("previous_layer_index").is_null());
    REQUIRE(second.at("complete") == true);
    REQUIRE(second.at("previous_model_gap_mm").get<double>() == Approx(0.2));
    REQUIRE(second.at("no_previous_overlap_island_count") == 1);
    REQUIRE(second.at("no_previous_overlap_island_area_mm2").get<double>() == Approx(4.));
}

TEST_CASE("Slice analysis leaves cross-layer measurements unavailable for invalid layer geometry", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object   = print.get_object(0);
    Layer*       previous = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer*       current  = object->add_layer(1, 0.2, 0.4, 0.3);
    previous->lslices = current->lslices = {rectangle(0., 0., 4., 4.)};
    SECTION("previous slicing error") { previous->slicing_errors = true; }
    SECTION("current slicing error") { current->slicing_errors = true; }
    SECTION("repeated print Z") { current->print_z = previous->print_z; }
    SECTION("descending print Z") { current->print_z = 0.1; }
    SECTION("repeated slice Z") { current->slice_z = previous->slice_z; }
    SECTION("descending slice Z") { current->slice_z = 0.05; }
    SECTION("unavailable previous print Z") { previous->print_z = std::numeric_limits<double>::quiet_NaN(); }
    SECTION("unavailable current print Z") { current->print_z = std::numeric_limits<double>::infinity(); }
    SECTION("unavailable previous slice Z") { previous->slice_z = std::numeric_limits<double>::quiet_NaN(); }
    SECTION("unavailable current slice Z") { current->slice_z = std::numeric_limits<double>::infinity(); }
    SECTION("unavailable height") { current->height = std::numeric_limits<double>::quiet_NaN(); }
    SECTION("zero height") { current->height = 0.; }
    SECTION("negative height") { current->height = -0.2; }
    const json layers = cross_layers(print);
    REQUIRE(layers.at(1).at("complete") == false);
    for (const char* key : {"previous_model_overlap_area_mm2", "outside_previous_model_area_mm2", "no_previous_overlap_island_count",
                            "no_previous_overlap_island_area_mm2"})
        REQUIRE(layers.at(1).at(key).is_null());
    REQUIRE(json::parse(layers.dump()) == layers);
}

TEST_CASE("Slice analysis retains geometric evidence for intentional CuraV1 model-layer overlap", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    // The production layer schedule: 1.3 mm raft + 0.3 mm air gap,
    // model heights 0.3/0.2/0.2 mm, with 0.1 mm overlap after model L1.
    object->add_layer(5, 0.3, 1.9, 0.15)->lslices = {rectangle(0., 0., 4., 4.)};
    object->add_layer(6, 0.2, 2.0, 0.4)->lslices  = {rectangle(0., 0., 6., 4.)};
    object->add_layer(7, 0.2, 2.2, 0.6)->lslices  = {rectangle(0., 0., 6., 4.)};
    const json layers                             = cross_layers(print);
    REQUIRE(layers.at(0).at("complete") == false);
    const auto& overlapping = layers.at(1);
    REQUIRE(overlapping.at("complete") == true);
    REQUIRE(overlapping.at("previous_model_gap_mm").get<double>() == Approx(-0.1));
    REQUIRE(overlapping.at("previous_model_overlap_area_mm2").get<double>() == Approx(16.));
    REQUIRE(overlapping.at("outside_previous_model_area_mm2").get<double>() == Approx(8.));
    REQUIRE(overlapping.at("no_previous_overlap_island_count") == 0);
    REQUIRE(layers.at(2).at("complete") == true);
    REQUIRE(layers.at(2).at("previous_model_gap_mm").get<double>() == Approx(0.).margin(1e-12));
}

TEST_CASE("Slice analysis sums bridge-role fragments and layer footprints without merging different Z", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       first  = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer*       second = object->add_layer(1, 0.2, 0.4, 0.3);
    first->lslices = second->lslices  = {rectangle(0., 0., 10., 10.)};
    LayerRegion*        first_region  = first->add_region(&object->printing_region(0));
    LayerRegion*        second_region = second->add_region(&object->printing_region(0));
    const ExtrusionPath bridge        = planned_path(1., 1., 3., 1., 0.4f, erBridgeInfill);
    first_region->fills.append(bridge);
    first->add_region(&object->printing_region(0))->fills.append(bridge);
    second_region->fills.append(bridge);
    auto* nested    = new ExtrusionEntityCollection;
    auto* multipath = new ExtrusionMultiPath;
    multipath->paths.push_back(planned_path(1., 2., 4., 2., 0.4f, erInternalBridgeInfill));
    nested->entities.push_back(multipath);
    auto* loop = new ExtrusionLoop;
    loop->paths.push_back(planned_path(1., 3., 5., 3., 0.4f, erOverhangPerimeter));
    nested->entities.push_back(loop);
    first_region->perimeters.entities.push_back(nested);
    // Ordinary model walls and thin_fills are outside the bridge-role counts.
    first_region->perimeters.append(planned_path(1., 4., 9., 4.));
    first_region->thin_fills.append(bridge);
    ExtrusionPath no_extrusion = bridge;
    no_extrusion.set_force_no_extrusion(true);
    first_region->fills.append(no_extrusion);
    ExtrusionPath zero_flow = bridge;
    zero_flow.mm3_per_mm    = 0.;
    first_region->fills.append(zero_flow);
    ExtrusionPath short_path = bridge;
    short_path.polyline      = Polyline({Point(0, 0)});
    first_region->fills.append(short_path);

    const json report = bridge_paths(print);
    REQUIRE(report.at("scope") == "per_instance_model_paths_before_gcode");
    REQUIRE(report.at("complete") == true);
    const auto& roles = report.at("roles");
    REQUIRE(roles.size() == 3);
    REQUIRE(roles.at(0).at("role") == "bridge_infill");
    REQUIRE(roles.at(0).at("path_count") == 3);
    REQUIRE(roles.at(0).at("path_length_mm").get<double>() == Approx(6.));
    REQUIRE(roles.at(0).at("layer_footprint_area_sum_mm2").get<double>() == Approx(1.6).margin(1e-5));
    REQUIRE(roles.at(0).at("excluded_path_count") == 3);
    REQUIRE(roles.at(1).at("role") == "internal_bridge_infill");
    REQUIRE(roles.at(1).at("path_count") == 1);
    REQUIRE(roles.at(1).at("path_length_mm").get<double>() == Approx(3.));
    REQUIRE(roles.at(1).at("layer_footprint_area_sum_mm2").get<double>() == Approx(1.2).margin(1e-5));
    REQUIRE(roles.at(2).at("role") == "overhang_perimeter");
    REQUIRE(roles.at(2).at("path_count") == 1);
    REQUIRE(roles.at(2).at("path_length_mm").get<double>() == Approx(4.));
    REQUIRE(roles.at(2).at("layer_footprint_area_sum_mm2").get<double>() == Approx(1.6).margin(1e-5));
    for (const auto& role : roles)
        REQUIRE(role.at("invalid_path_count") == 0);
}

TEST_CASE("Slice analysis distinguishes zero bridge paths from unavailable bridge measurements", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object         = print.get_object(0);
    const json   without_layers = bridge_paths(print);
    REQUIRE(without_layers.at("complete") == false);
    for (const auto& role : without_layers.at("roles")) {
        REQUIRE(role.at("path_count").is_null());
        REQUIRE(role.at("path_length_mm").is_null());
        REQUIRE(role.at("layer_footprint_area_sum_mm2").is_null());
    }
    Layer* layer     = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices   = {rectangle(0., 0., 4., 4.)};
    const json empty = bridge_paths(print);
    REQUIRE(empty.at("complete") == true);
    for (const auto& role : empty.at("roles")) {
        REQUIRE(role.at("path_count") == 0);
        REQUIRE(role.at("path_length_mm") == 0.);
        REQUIRE(role.at("layer_footprint_area_sum_mm2") == 0.);
    }
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fills.append(planned_path(1., 1., 3., 1., std::numeric_limits<float>::quiet_NaN(), erBridgeInfill));
    const json invalid = bridge_paths(print);
    REQUIRE(invalid.at("complete") == false);
    REQUIRE(invalid.at("roles").at(0).at("invalid_path_count") == 1);
    for (const auto& role : invalid.at("roles")) {
        REQUIRE(role.at("path_count").is_null());
        REQUIRE(role.at("path_length_mm").is_null());
        REQUIRE(role.at("layer_footprint_area_sum_mm2").is_null());
    }
    REQUIRE(json::parse(invalid.dump()) == invalid);
}

TEST_CASE("Slice analysis measures continuous external bridge gaps within original path fragments", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* previous = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer* current  = object->add_layer(1, 0.2, 0.4, 0.3);
    previous->lslices = current->lslices = {rectangle(0., -1., 12., 2.)};
    LayerRegion* lower = previous->add_region(&object->printing_region(0));
    LayerRegion* upper = current->add_region(&object->printing_region(0));
    lower->fills.append(planned_path(0., 0., 2., 0., 2.f));
    lower->fills.append(planned_path(10., 0., 12., 0., 2.f));
    ExtrusionPath bridge = planned_path(1., 0., 11., 0., 0.4f, erBridgeInfill);
    size_t expected_count = 1, expected_two_sided = 1;
    double expected_maximum = 8.;
    bool split = false;
    SECTION("two pillars") {}
    SECTION("an intermediate island separates two three-millimeter gaps") {
        lower->fills.append(planned_path(5., 0., 7., 0., 2.f));
        expected_count = expected_two_sided = 2;
        expected_maximum = 3.;
    }
    SECTION("original segment boundaries do not split an uncovered run") {
        bridge.polyline = Polyline::new_scale({{1., 0.}, {4., 0.}, {8., 0.}, {11., 0.}});
    }
    SECTION("repeated points do not split an uncovered run") {
        bridge.polyline = Polyline::new_scale({{1., 0.}, {1., 0.}, {6., 0.}, {6., 0.}, {11., 0.}, {11., 0.}});
    }
    SECTION("reversed traversal has the same observation") { bridge.reverse(); }
    SECTION("rotated paths retain the same centerline observation") {
        for (ExtrusionEntity* entity : lower->fills.entities)
            dynamic_cast<ExtrusionPath&>(*entity).polyline.rotate(0., 1.);
        bridge.polyline.rotate(0., 1.);
    }
    SECTION("matching independent path fragments are not stitched") {
        auto* paths = new ExtrusionMultiPath;
        paths->paths.push_back(planned_path(1., 0., 6., 0., 0.4f, erBridgeInfill));
        paths->paths.push_back(planned_path(6., 0., 11., 0., 0.4f, erBridgeInfill));
        upper->fills.entities.push_back(paths);
        split = true;
        expected_count = 2;
        expected_two_sided = 0;
        expected_maximum = 4.;
    }
    if (!split)
        upper->fills.append(bridge);

    const json report = make_slice_analysis_report(print, InitializedResult{});
    const auto& projection = report.at("objects").at("groups").at(0).at("external_bridge_projection");
    REQUIRE(projection.at("scope") == "external_bridge_centerlines_over_previous_model_footprint_before_gcode");
    const auto& layers = projection.at("layers");
    REQUIRE(layers.size() == 2);
    REQUIRE(layers.at(0).at("complete") == false);
    REQUIRE(layers.at(0).at("previous_layer_index").is_null());
    REQUIRE(layers.at(0).at("previous_print_z_mm").is_null());
    const auto& measured = layers.at(1);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("layer_index") == 1);
    REQUIRE(measured.at("previous_layer_index") == 0);
    REQUIRE(measured.at("print_z_mm").get<double>() == Approx(0.4));
    REQUIRE(measured.at("previous_print_z_mm").get<double>() == Approx(0.2));
    REQUIRE(measured.at("uncovered_run_count") == expected_count);
    REQUIRE(measured.at("max_uncovered_run_mm").get<double>() == Approx(expected_maximum));
    REQUIRE(measured.at("two_sided_run_count") == expected_two_sided);
    if (expected_two_sided > 0)
        REQUIRE(measured.at("max_two_sided_run_mm").get<double>() == Approx(expected_maximum));
    else
        REQUIRE(measured.at("max_two_sided_run_mm").is_null());
}

TEST_CASE("Slice analysis distinguishes empty, fully covered and one-sided external bridge observations", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* previous = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer* current  = object->add_layer(1, 0.2, 0.4, 0.3);
    previous->lslices = current->lslices = {rectangle(0., -1., 12., 2.)};
    LayerRegion* lower = previous->add_region(&object->printing_region(0));
    LayerRegion* upper = current->add_region(&object->printing_region(0));
    ExtrusionPath bridge = planned_path(1., 0., 11., 0., 0.4f, erBridgeInfill);
    size_t expected_count = 1;
    double expected_maximum = 10.;
    SECTION("empty previous footprint is known, not replaced with its solid section") {}
    SECTION("only the start has positive coverage") {
        lower->fills.append(planned_path(0., 0., 2., 0., 2.f));
        expected_maximum = 9.;
    }
    SECTION("only the end has positive coverage") {
        lower->fills.append(planned_path(10., 0., 12., 0., 2.f));
        expected_maximum = 9.;
    }
    SECTION("entire centerline is covered") {
        lower->fills.append(planned_path(0., 0., 12., 0., 2.f));
        expected_count = 0;
    }
    SECTION("no external bridge path") {
        bridge.mm3_per_mm = 0.;
        expected_count = 0;
    }
    SECTION("force-no-extrusion path is excluded") {
        bridge.set_force_no_extrusion(true);
        expected_count = 0;
    }
    SECTION("zero-length repeated points do not create a closed bridge") {
        bridge.polyline = Polyline::new_scale({{1., 0.}, {1., 0.}, {1., 0.}});
        expected_count = 0;
    }
    upper->fills.append(bridge);
    const json measured = external_bridge_layers(print).at(1);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("uncovered_run_count") == expected_count);
    REQUIRE(measured.at("two_sided_run_count") == 0);
    REQUIRE(measured.at("max_two_sided_run_mm").is_null());
    if (expected_count == 0)
        REQUIRE(measured.at("max_uncovered_run_mm").is_null());
    else
        REQUIRE(measured.at("max_uncovered_run_mm").get<double>() == Approx(expected_maximum));
}

TEST_CASE("Slice analysis follows external bridge gaps through previous footprint holes and nested material", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* previous = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer* current  = object->add_layer(1, 0.2, 0.4, 0.3);
    previous->lslices = current->lslices = {rectangle(0., 0., 10., 10.)};
    LayerRegion* lower = previous->add_region(&object->printing_region(0));
    LayerRegion* upper = current->add_region(&object->printing_region(0));
    lower->fills.append(planned_path(0., 1., 10., 1., 2.f));
    lower->fills.append(planned_path(0., 9., 10., 9., 2.f));
    lower->fills.append(planned_path(1., 2., 1., 8., 2.f));
    lower->fills.append(planned_path(9., 2., 9., 8., 2.f));
    size_t expected_count = 1;
    double expected_maximum = 6., y = 5.;
    SECTION("empty hole") {}
    SECTION("material inside a hole is coverage") {
        lower->fills.append(planned_path(4., 5., 6., 5., 2.f));
        expected_count = 2;
        expected_maximum = 2.;
    }
    SECTION("positive-length hole boundary is not interior coverage") { y = 2.; }
    upper->fills.append(planned_path(1., y, 9., y, 0.4f, erBridgeInfill));
    const json measured = external_bridge_layers(print).at(1);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("uncovered_run_count") == expected_count);
    REQUIRE(measured.at("two_sided_run_count") == expected_count);
    REQUIRE(measured.at("max_uncovered_run_mm").get<double>() == Approx(expected_maximum));
    REQUIRE(measured.at("max_two_sided_run_mm").get<double>() == Approx(expected_maximum));
}

TEST_CASE("Slice analysis measures bent bridge centerline length rather than a straight span", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* previous = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer* current  = object->add_layer(1, 0.2, 0.4, 0.3);
    previous->lslices = current->lslices = {rectangle(0., -1., 12., 8.)};
    LayerRegion* lower = previous->add_region(&object->printing_region(0));
    LayerRegion* upper = current->add_region(&object->printing_region(0));
    lower->fills.append(planned_path(0., 0., 2., 0., 2.f));
    lower->fills.append(planned_path(10., 6., 12., 6., 2.f));
    ExtrusionPath bridge = planned_path(1., 0., 11., 6., 0.4f, erBridgeInfill);
    bridge.polyline = Polyline::new_scale({{1., 0.}, {6., 0.}, {6., 6.}, {11., 6.}});
    upper->fills.append(bridge);
    const json measured = external_bridge_layers(print).at(1);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("uncovered_run_count") == 1);
    REQUIRE(measured.at("two_sided_run_count") == 1);
    REQUIRE(measured.at("max_uncovered_run_mm").get<double>() == Approx(14.));
    REQUIRE(measured.at("max_two_sided_run_mm").get<double>() == Approx(14.));
}

TEST_CASE("Slice analysis does not use boundary or point contact to split an uncovered bridge run", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* previous = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer* current  = object->add_layer(1, 0.2, 0.4, 0.3);
    previous->lslices = current->lslices = {rectangle(0., 0., 10., 10.)};
    LayerRegion* lower = previous->add_region(&object->printing_region(0));
    LayerRegion* upper = current->add_region(&object->printing_region(0));
    ExtrusionPath bridge = planned_path(0., 0., 10., 0., 0.4f, erBridgeInfill);
    double expected_maximum = 10.;
    SECTION("coincident outer boundary") { lower->fills.append(planned_path(0., 1., 10., 1., 2.f)); }
    SECTION("a boundary piece in the middle is not coverage") { lower->fills.append(planned_path(4., 1., 6., 1., 2.f)); }
    SECTION("tangent polygon vertex within one path segment") {
        lower->fills.append(planned_path(2., 5., 4., 5., 2.f));
        bridge.polyline = Polyline::new_scale({{0., 0.}, {10., 10.}});
        expected_maximum = std::sqrt(200.);
    }
    SECTION("tangent point at an original path vertex") {
        lower->fills.append(planned_path(4., 6., 6., 6., 2.f));
        bridge.polyline = Polyline::new_scale({{0., 0.}, {5., 5.}, {10., 0.}});
        expected_maximum = std::sqrt(200.);
    }
    upper->fills.append(bridge);
    const json measured = external_bridge_layers(print).at(1);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("uncovered_run_count") == 1);
    REQUIRE(measured.at("two_sided_run_count") == 0);
    REQUIRE(measured.at("max_uncovered_run_mm").get<double>() == Approx(expected_maximum));
    REQUIRE(measured.at("max_two_sided_run_mm").is_null());
}

TEST_CASE("Slice analysis does not substitute other roles or same-layer or support material for previous model coverage", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* previous = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer* current  = object->add_layer(1, 0.2, 0.4, 0.3);
    previous->lslices = current->lslices = {rectangle(0., -1., 12., 2.)};
    LayerRegion* upper = current->add_region(&object->printing_region(0));
    upper->fills.append(planned_path(1., 0., 11., 0., 0.4f, erInternalBridgeInfill));
    upper->perimeters.append(planned_path(1., 0., 11., 0., 0.4f, erOverhangPerimeter));
    REQUIRE(external_bridge_layers(print).at(1).at("uncovered_run_count") == 0);
    upper->fills.append(planned_path(1., 0., 11., 0., 0.4f, erBridgeInfill));
    upper->fills.append(planned_path(0., 0., 12., 0., 2.f));
    object->add_support_layer(0, 0, 0.2, 0.2)->support_fills.append(planned_path(0., 0., 12., 0., 2.f, erSupportMaterial));
    const json measured = external_bridge_layers(print).at(1);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("uncovered_run_count") == 1);
    REQUIRE(measured.at("two_sided_run_count") == 0);
    REQUIRE(measured.at("max_uncovered_run_mm").get<double>() == Approx(10.));
}

TEST_CASE("Slice analysis leaves external bridge projection unknown for closed paths or unavailable adjacent layers", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* previous = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer* current  = object->add_layer(1, 0.2, 0.4, 0.3);
    previous->lslices = current->lslices = {rectangle(0., -1., 12., 2.)};
    LayerRegion* lower = previous->add_region(&object->printing_region(0));
    LayerRegion* upper = current->add_region(&object->printing_region(0));
    ExtrusionPath bridge = planned_path(1., 0., 11., 0., 0.4f, erBridgeInfill);
    SECTION("closed external bridge") { bridge.polyline = Polyline::new_scale({{1., 0.}, {11., 0.}, {6., 1.}, {1., 0.}}); }
    SECTION("previous slicing error") { previous->slicing_errors = true; }
    SECTION("current slicing error") { current->slicing_errors = true; }
    SECTION("non-increasing slice Z") { current->slice_z = previous->slice_z; }
    SECTION("non-increasing print Z") { current->print_z = previous->print_z; }
    SECTION("invalid current height") { current->height = 0.; }
    SECTION("unavailable previous print Z") { previous->print_z = std::numeric_limits<double>::quiet_NaN(); }
    SECTION("invalid previous footprint") { lower->fills.append(planned_path(0., 0., 12., 0., -1.f)); }
    SECTION("invalid current footprint") { upper->fills.append(planned_path(0., 0., 12., 0., -1.f)); }
    upper->fills.append(bridge);
    const json layers = external_bridge_layers(print);
    for (const auto& layer : layers) {
        REQUIRE(layer.at("complete") == false);
        for (const char* key : {"uncovered_run_count", "max_uncovered_run_mm", "two_sided_run_count", "max_two_sided_run_mm"})
            REQUIRE(layer.at(key).is_null());
    }
    REQUIRE(json::parse(layers.dump()) == layers);
}

TEST_CASE("Slice analysis external bridge observations retain Z metadata and do not change paths or settings", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    // A variable-height, elevated pair with valid negative nominal layer gap.
    Layer* previous = object->add_layer(5, 0.3, 1.9, 0.15);
    Layer* current  = object->add_layer(6, 0.2, 2.0, 0.4);
    previous->lslices = current->lslices = {rectangle(0., -1., 12., 2.)};
    LayerRegion* lower = previous->add_region(&object->printing_region(0));
    LayerRegion* upper = current->add_region(&object->printing_region(0));
    lower->fills.append(planned_path(0., 0., 2., 0., 2.f));
    lower->fills.append(planned_path(10., 0., 12., 0., 2.f));
    auto* nested = new ExtrusionEntityCollection;
    auto* paths = new ExtrusionMultiPath;
    paths->paths.push_back(planned_path(1., 0., 11., 0., 0.4f, erBridgeInfill));
    paths->paths.back().height = 0.4f; // Does not turn XY evidence into Z contact.
    nested->entities.push_back(paths);
    upper->fills.entities.push_back(nested);
    const auto settings = all_settings(print);
    const auto lower_before = lower->fills.as_polylines();
    const auto upper_before = upper->fills.as_polylines();
    const Polygons lower_footprint = lower->fills.polygons_covered_by_width(0.f);
    const Polygons upper_footprint = upper->fills.polygons_covered_by_width(0.f);
    const json report = make_slice_analysis_report(print, InitializedResult{});
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    const auto& observed = report.at("objects").at("groups").at(0).at("external_bridge_projection").at("layers").at(1);
    REQUIRE(observed.at("complete") == true);
    REQUIRE(observed.at("print_z_mm").get<double>() == Approx(2.0));
    REQUIRE(observed.at("previous_print_z_mm").get<double>() == Approx(1.9));
    REQUIRE(observed.at("uncovered_run_count") == 1);
    REQUIRE(observed.at("two_sided_run_count") == 1);
    REQUIRE(observed.at("max_uncovered_run_mm").get<double>() == Approx(8.));
    REQUIRE(all_settings(print) == settings);
    REQUIRE(lower->fills.as_polylines() == lower_before);
    REQUIRE(upper->fills.as_polylines() == upper_before);
    REQUIRE(lower->fills.polygons_covered_by_width(0.f) == lower_footprint);
    REQUIRE(upper->fills.polygons_covered_by_width(0.f) == upper_footprint);
    REQUIRE(paths->paths.front().height == 0.4f);
}

TEST_CASE("Slice analysis keeps coordinate-scale bridge gaps and recovers after an invalid preceding footprint", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* previous = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer* current  = object->add_layer(1, 0.2, 0.4, 0.3);
    previous->lslices = current->lslices = {rectangle(0., -1., 10., 2.)};
    LayerRegion* lower = previous->add_region(&object->printing_region(0));
    LayerRegion* upper = current->add_region(&object->printing_region(0));
    upper->fills.append(planned_path(1., 0., 9., 0., 0.4f, erBridgeInfill));
    SECTION("one coordinate unit is not merged by a new tolerance") {
        lower->fills.append(planned_path(0., 0., 5., 0., 2.f));
        ExtrusionPath right = planned_path(5., 0., 10., 0., 2.f);
        right.polyline.points.front().x() += 1;
        lower->fills.append(right);
        const json measured = external_bridge_layers(print).at(1);
        REQUIRE(measured.at("complete") == true);
        REQUIRE(measured.at("uncovered_run_count") == 1);
        REQUIRE(measured.at("two_sided_run_count") == 1);
        REQUIRE(measured.at("max_uncovered_run_mm").get<double>() == Approx(SCALING_FACTOR).margin(1e-12));
        REQUIRE(measured.at("max_two_sided_run_mm").get<double>() == Approx(SCALING_FACTOR).margin(1e-12));
    }
    SECTION("an invalid old layer does not poison later adjacent valid pairs") {
        lower->fills.append(planned_path(0., 0., 10., 0., -1.f));
        Layer* next = object->add_layer(2, 0.2, 0.6, 0.5);
        next->lslices = current->lslices;
        next->add_region(&object->printing_region(0))->fills.append(planned_path(1., 0., 9., 0., 0.4f, erBridgeInfill));
        const json layers = external_bridge_layers(print);
        REQUIRE(layers.at(1).at("complete") == false);
        REQUIRE(layers.at(2).at("complete") == true);
        REQUIRE(layers.at(2).at("previous_layer_index") == 1);
        REQUIRE(layers.at(2).at("uncovered_run_count") == 0);
        REQUIRE(layers.at(2).at("two_sided_run_count") == 0);
        REQUIRE(layers.at(2).at("max_uncovered_run_mm").is_null());
        REQUIRE(layers.at(2).at("max_two_sided_run_mm").is_null());
    }
}

TEST_CASE("Slice analysis distinguishes thin-volume sampling from layer thickness and exact-boundary planes", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source         = add_box(model, 1);
    ModelVolume* thin           = source->add_volume(TriangleMesh(its_make_cube(4., 2., 0.05)), ModelVolumeType::MODEL_PART, false);
    double       bottom         = 0.21;
    size_t       interior_count = 0, boundary_count = 0, section_count = 0;
    SECTION("thin volume between retained planes") {}
    SECTION("the same height intersects a retained plane")
    {
        bottom         = 0.28;
        interior_count = section_count = 1;
    }
    SECTION("an exact boundary is not an interior sample or a known positive-area slice")
    {
        bottom         = 0.3;
        boundary_count = 1;
    }
    thin->set_offset(Vec3d(20., 0., bottom));
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    // Variable layer heights and an elevated print Z do not shift source slice Z.
    object->add_layer(5, 0.2, 1.4, 0.1)->lslices  = {rectangle(0., 0., 10., 10.)};
    Layer* middle                                 = object->add_layer(6, 0.2, 1.6, 0.3);
    middle->lslices                               = {rectangle(0., 0., 10., 10.)};
    object->add_layer(7, 0.3, 1.9, 0.55)->lslices = {rectangle(0., 0., 10., 10.)};
    const ExPolygons thin_section                 = section_count == 0 ? ExPolygons{} : ExPolygons{rectangle(20., 0., 4., 2.)};
    object->firstLayerObjSliceMod().push_back({object->model_object()->volumes[1]->id(), {{}, thin_section, {}}});
    if (section_count != 0) {
        middle->lslices.push_back(thin_section.front());
        middle->add_region(&object->printing_region(0))->fills.append(planned_path(21., 1., 23., 1.));
    }
    const json sampling = volume_sampling(print);
    REQUIRE(sampling.at("scope") == "per_instance_source_model_part_on_retained_layers");
    const auto& volume = sampling.at("volumes").at(1);
    REQUIRE(volume.at("volume_index") == 1);
    REQUIRE(volume.at("z_min_mm").get<double>() == Approx(bottom).margin(1e-6));
    REQUIRE(volume.at("z_max_mm").get<double>() == Approx(bottom + 0.05).margin(1e-6));
    REQUIRE(volume.at("interior_plane_count") == interior_count);
    REQUIRE(volume.at("boundary_plane_count") == boundary_count);
    REQUIRE(volume.at("complete") == true);
    REQUIRE(volume.at("source_section_layer_count") == section_count);
    REQUIRE(volume.at("final_section_overlap_layer_count") == section_count);
    REQUIRE(volume.at("planned_path_overlap_layer_count") == section_count);
}

TEST_CASE("Slice analysis separates source-volume sections from final material and planned-path overlap", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 1);
    source->add_volume(TriangleMesh(its_make_cube(10., 10., 2.)), ModelVolumeType::NEGATIVE_VOLUME, false);
    source->add_volume(TriangleMesh(its_make_cube(10., 10., 2.)), ModelVolumeType::PARAMETER_MODIFIER, false);
    apply_model(print, model);
    PrintObject*     object = print.get_object(0);
    const ExPolygons section{rectangle(0., 0., 4., 4.)};
    for (size_t i = 0; i < 3; ++i) {
        Layer* layer = object->add_layer(int(i), 0.2, 0.2 * (i + 1), 0.1 + 0.2 * i);
        // Source exists at all three heights. At the middle height it has
        // been removed from final sections, as by a negative volume/override.
        if (i != 1)
            layer->lslices = section;
        LayerRegion* region = layer->add_region(&object->printing_region(0));
        region->fills.append(i == 0 ? planned_path(1., 1., 3., 1.) : planned_path(10., 1., 12., 1.));
    }
    for (const ModelVolume* volume : object->model_object()->volumes)
        object->firstLayerObjSliceMod().push_back({volume->id(), {section, section, section}});
    const json sampling = volume_sampling(print);
    REQUIRE(sampling.at("volumes").size() == 1);
    const auto& volume = sampling.at("volumes").at(0);
    REQUIRE(volume.at("volume_index") == 0);
    REQUIRE(volume.at("complete") == true);
    REQUIRE(volume.at("interior_plane_count") == 3);
    REQUIRE(volume.at("source_section_layer_count") == 3);
    REQUIRE(volume.at("final_section_overlap_layer_count") == 2);
    REQUIRE(volume.at("planned_path_overlap_layer_count") == 1);
}

TEST_CASE("Slice analysis does not replace missing or invalid source-volume cache data with zero", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject*     object = print.get_object(0);
    const ExPolygons section{rectangle(0., 0., 4., 4.)};
    Layer*           first  = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer*           second = object->add_layer(1, 0.2, 0.4, 0.3);
    first->lslices = second->lslices = section;
    bool complete                    = false;
    SECTION("missing cache, including loaded slice data") {}
    SECTION("cache shorter than retained layers")
    {
        object->firstLayerObjSliceMod().push_back({object->model_object()->volumes[0]->id(), {section}});
    }
    SECTION("exact-size cache")
    {
        complete = true;
        object->firstLayerObjSliceMod().push_back({object->model_object()->volumes[0]->id(), {section, section}});
    }
    SECTION("a trimmed empty top-layer suffix is permitted")
    {
        complete = true;
        object->firstLayerObjSliceMod().push_back({object->model_object()->volumes[0]->id(), {section, section, {}, {}}});
    }
    SECTION("a layer with slicing errors is not complete")
    {
        second->slicing_errors = true;
        object->firstLayerObjSliceMod().push_back({object->model_object()->volumes[0]->id(), {section, section}});
    }
    SECTION("invalid planned width is not known zero coverage")
    {
        second->add_region(&object->printing_region(0))->fills.append(planned_path(1., 1., 3., 1., std::numeric_limits<float>::quiet_NaN()));
        object->firstLayerObjSliceMod().push_back({object->model_object()->volumes[0]->id(), {section, section}});
    }
    const json volume = volume_sampling(print).at("volumes").at(0);
    REQUIRE(volume.at("z_min_mm") == 0.);
    REQUIRE(volume.at("z_max_mm") == 2.);
    REQUIRE(volume.at("interior_plane_count") == 2);
    REQUIRE(volume.at("complete") == complete);
    if (complete) {
        REQUIRE(volume.at("source_section_layer_count") == 2);
        REQUIRE(volume.at("final_section_overlap_layer_count") == 2);
        REQUIRE(volume.at("planned_path_overlap_layer_count") == 0);
    } else {
        REQUIRE(volume.at("source_section_layer_count").is_null());
        REQUIRE(volume.at("final_section_overlap_layer_count").is_null());
        REQUIRE(volume.at("planned_path_overlap_layer_count").is_null());
    }
}

TEST_CASE("Slice analysis keeps source-volume bounds but not invented samples without a valid layer schedule", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    SECTION("no retained layers") {}
    SECTION("nonfinite retained sampling plane") { object->add_layer(0, 0.2, 0.2, std::numeric_limits<double>::quiet_NaN()); }
    const json volume = volume_sampling(print).at("volumes").at(0);
    REQUIRE(volume.at("z_min_mm") == 0.);
    REQUIRE(volume.at("z_max_mm") == 2.);
    REQUIRE(volume.at("interior_plane_count").is_null());
    REQUIRE(volume.at("boundary_plane_count").is_null());
    REQUIRE(volume.at("complete") == false);
    REQUIRE(volume.at("source_section_layer_count").is_null());
    REQUIRE(volume.at("final_section_overlap_layer_count").is_null());
    REQUIRE(volume.at("planned_path_overlap_layer_count").is_null());
}

TEST_CASE("Slice analysis uses each group's volume and instance transforms for source Z bounds", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source = model.add_object();
    ModelVolume* volume = source->add_volume(TriangleMesh(its_make_cube(2., 3., 0.1)), ModelVolumeType::MODEL_PART, false);
    volume->set_rotation(Vec3d(0., 0.5 * PI, 0.));
    volume->set_offset(Vec3d(0., 0., 4.));
    source->add_instance()->set_scaling_factor(Vec3d(1., 1., 2.));
    source->instances.back()->set_offset(Vec3d(20., 20., 1.));
    source->add_instance()->set_scaling_factor(Vec3d(1., 1., 3.));
    source->instances.back()->set_offset(Vec3d(40., 20., 0.));
    apply_model(print, model);
    REQUIRE(print.objects().size() == 2);
    std::vector<double> minima, maxima;
    for (size_t i = 0; i < print.objects().size(); ++i) {
        print.get_object(i)->add_layer(0, 0.2, 8.1, 8.);
        const json result = volume_sampling(print, i).at("volumes").at(0);
        minima.push_back(result.at("z_min_mm").get<double>());
        maxima.push_back(result.at("z_max_mm").get<double>());
        REQUIRE(result.at("interior_plane_count") == 1);
        REQUIRE(result.at("boundary_plane_count") == 0);
    }
    std::sort(minima.begin(), minima.end());
    std::sort(maxima.begin(), maxima.end());
    REQUIRE(minima.at(0) == Approx(5.).margin(1e-5));
    REQUIRE(minima.at(1) == Approx(6.).margin(1e-5));
    REQUIRE(maxima.at(0) == Approx(9.).margin(1e-5));
    REQUIRE(maxima.at(1) == Approx(12.).margin(1e-5));
}

TEST_CASE("Slice analysis resolves shared source-volume cache IDs through the slicing owner", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 1);
    ModelObject* copy   = model.add_object(*source);
    copy->instances.front()->set_offset(Vec3d(60., 20., 0.));
    apply_model(print, model);
    REQUIRE(print.objects().size() == 2);
    PrintObject* owner  = print.get_object(0);
    PrintObject* shared = print.get_object(1);
    REQUIRE(owner->model_object()->volumes[0]->id() != shared->model_object()->volumes[0]->id());
    REQUIRE(owner->model_object()->volumes[0]->mesh_ptr() == shared->model_object()->volumes[0]->mesh_ptr());
    const ExPolygons section{rectangle(0., 0., 4., 4.)};
    Layer*           layer = owner->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices         = section;
    layer->add_region(&owner->printing_region(0))->fills.append(planned_path(1., 1., 3., 1.));
    owner->firstLayerObjSliceMod().push_back({owner->model_object()->volumes[0]->id(), {section}});
    shared->set_shared_object(owner);
    shared->copy_layers_from_shared_object();
    const json original = volume_sampling(print, 0);
    const json copied   = volume_sampling(print, 1);
    REQUIRE(copied == original);
    REQUIRE(copied.at("volumes").at(0).at("complete") == true);
    REQUIRE(copied.at("volumes").at(0).at("source_section_layer_count") == 1);
    REQUIRE(copied.at("volumes").at(0).at("planned_path_overlap_layer_count") == 1);
    REQUIRE(surface_alignment(print, 1) == surface_alignment(print, 0));
    REQUIRE(surface_alignment(print, 1).at("volumes").at(0).at("complete") == true);
}

TEST_CASE("Slice analysis normal projection preserves the maximum under coplanar facet subdivision", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 1);
    add_source_facets(*source, {{0.f, 0.f, 0.f}, {1.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 1.f, 0.f}},
                      {{0, 1, 2}, {0, 2, 3}});
    std::vector<Vec3f> vertices;
    std::vector<Vec3i32> indices;
    for (int strip = 0; strip < 8; ++strip) {
        const float a = float(strip) / 8.f;
        const float b = float(strip + 1) / 8.f;
        const int   i = int(vertices.size());
        vertices.insert(vertices.end(), {{a, 0.f, a}, {b, 0.f, b}, {b, 1.f, b}, {a, 1.f, a}});
        indices.emplace_back(i, i + 1, i + 2);
        indices.emplace_back(i, i + 2, i + 3);
    }
    add_source_facets(*source, vertices, indices);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    object->add_layer(0, 0.2, 0.4, 0.3);
    const auto settings          = all_settings(print);
    const auto source_vertices   = object->model_object()->volumes[2]->mesh().its.vertices;
    const auto source_indices    = object->model_object()->volumes[2]->mesh().its.indices;
    const json original_sampling = volume_sampling(print);
    const json alignment         = surface_alignment(print);
    REQUIRE(alignment.at("scope") == "source_model_part_facets_and_retained_source_layer_bands");
    const auto& coarse = alignment.at("volumes").at(1);
    const auto& fine   = alignment.at("volumes").at(2);
    const double sampling_z = double(float(0.3));
    const double expected = std::max(std::abs((0.3 - 0.1) - sampling_z), std::abs((0.3 + 0.1) - sampling_z)) / std::sqrt(2.);
    for (const json* value : {&coarse, &fine}) {
        REQUIRE(value->at("complete") == true);
        REQUIRE(value->at("max_vertical_projection_normal_offset_mm").get<double>() == Approx(0.1 / std::sqrt(2.)).margin(2e-8));
        REQUIRE(value->at("max_vertical_projection_normal_offset_mm").get<double>() == Approx(expected).epsilon(0.).margin(1e-12));
        REQUIRE(value->at("horizontal_facet_count") == 0);
        REQUIRE(value->at("max_horizontal_boundary_offset_mm").is_null());
    }
    REQUIRE(coarse.at("non_horizontal_facet_band_count") == 2);
    REQUIRE(fine.at("non_horizontal_facet_band_count") == 6);
    REQUIRE(coarse.at("max_vertical_projection_normal_offset_mm") == fine.at("max_vertical_projection_normal_offset_mm"));
    REQUIRE(surface_alignment(print) == alignment);
    REQUIRE(volume_sampling(print) == original_sampling);
    REQUIRE(all_settings(print) == settings);
    REQUIRE(object->model_object()->volumes[2]->mesh().its.vertices == source_vertices);
    REQUIRE(object->model_object()->volumes[2]->mesh().its.indices == source_indices);
    REQUIRE(object->layers().front()->slice_z == 0.3);
    REQUIRE(object->layers().front()->height == 0.2);
}

TEST_CASE("Slice analysis normal projection uses only a source facet's part inside each layer band", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 1);
    std::vector<Vec3f> vertices{{0.f, 0.f, 0.25f}, {0.f, 1.f, 0.25f}, {0.f, 0.f, 0.5f}};
    double            expected = 0.;
    size_t            count    = 1;
    SECTION("vertical source facets have a zero normal component") {}
    SECTION("a nearly horizontal source facet remains bounded without an angle cutoff")
    {
        const float delta = std::ldexp(1.f, -20);
        vertices = {{0.f, 0.f, 0.375f}, {1.f, 0.f, 0.375f + delta}, {0.f, 1.f, 0.375f}};
        expected = double(delta) / std::sqrt(1. + double(delta) * double(delta));
    }
    SECTION("a facet entirely above the sampling plane is not treated as sampled or as a full-height facet")
    {
        vertices = {{0.4375f, 0.f, 0.4375f}, {0.46875f, 0.f, 0.46875f}, {0.4375f, 1.f, 0.4375f}};
        expected = 0.09375 / std::sqrt(2.);
    }
    SECTION("a non-horizontal facet outside retained bands has no applicable pair")
    {
        vertices = {{0.f, 0.f, 1.f}, {0.f, 1.f, 1.f}, {0.f, 0.f, 1.5f}};
        count    = 0;
    }
    add_source_facets(*source, vertices);
    apply_model(print, model);
    print.get_object(0)->add_layer(0, 0.25, 0.5, 0.375);
    const json volume = surface_alignment(print).at("volumes").at(1);
    REQUIRE(volume.at("complete") == true);
    REQUIRE(volume.at("non_horizontal_facet_band_count") == count);
    REQUIRE(volume.at("horizontal_facet_count") == 0);
    REQUIRE(volume.at("max_horizontal_boundary_offset_mm").is_null());
    if (count == 0)
        REQUIRE(volume.at("max_vertical_projection_normal_offset_mm").is_null());
    else
        REQUIRE(volume.at("max_vertical_projection_normal_offset_mm").get<double>() == Approx(expected).margin(1e-12));
}

TEST_CASE("Slice analysis horizontal alignment is nearest retained source boundary rather than a print result", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 1);
    float  z        = 0.375f;
    double expected = 0.125;
    SECTION("between layer boundaries") {}
    SECTION("exactly on a boundary")
    {
        z = 0.5f;
        expected = 0.;
    }
    SECTION("above the last retained source layer")
    {
        z = 0.75f;
        expected = 0.25;
    }
    SECTION("below the first retained source layer")
    {
        z = 0.125f;
        expected = 0.125;
    }
    add_source_facets(*source, {{0.f, 0.f, z}, {1.f, 0.f, z}, {0.f, 1.f, z}});
    apply_model(print, model);
    print.get_object(0)->add_layer(0, 0.25, 1.5, 0.375);
    const json volume = surface_alignment(print).at("volumes").at(1);
    REQUIRE(volume.at("complete") == true);
    REQUIRE(volume.at("non_horizontal_facet_band_count") == 0);
    REQUIRE(volume.at("max_vertical_projection_normal_offset_mm").is_null());
    REQUIRE(volume.at("horizontal_facet_count") == 1);
    REQUIRE(volume.at("max_horizontal_boundary_offset_mm").get<double>() == Approx(expected));
}

TEST_CASE("Slice analysis source alignment follows variable layer bands independently of Cura print-plane overlap", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 1);
    add_source_facets(*source, {{0.f, 0.f, 0.f}, {1.f, 0.f, 1.f}, {0.f, 1.f, 0.f}});
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       first  = object->add_layer(0, 0.25, 0.25, 0.125);
    Layer*       second = object->add_layer(1, 0.5, 0.75, 0.5);
    Layer*       third  = object->add_layer(2, 0.25, 1., 0.875);
    const json original = surface_alignment(print);
    const auto& volume = original.at("volumes").at(1);
    REQUIRE(volume.at("complete") == true);
    REQUIRE(volume.at("non_horizontal_facet_band_count") == 3);
    REQUIRE(volume.at("max_vertical_projection_normal_offset_mm").get<double>() == Approx(0.25 / std::sqrt(2.)));
    first->print_z  += 1.;
    second->print_z += 0.9;
    third->print_z  += 0.9;
    REQUIRE(second->bottom_z() - first->print_z == Approx(-0.1));
    REQUIRE(surface_alignment(print) == original);
}

TEST_CASE("Slice analysis source alignment recomputes normals after volume and instance transformations", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 1);
    ModelVolume* volume = add_source_facets(*source, {{0.f, 0.f, 0.f}, {1.f, 0.f, 1.f}, {0.f, 1.f, 0.f}});
    volume->set_scaling_factor(Vec3d(1., 1., 2.));
    volume->set_rotation(Vec3d(0., 0., 0.5 * PI));
    source->instances.front()->set_scaling_factor(Vec3d(1., 1., 3.));
    apply_model(print, model);
    print.get_object(0)->add_layer(0, 0.25, 0.5, 0.375);
    const json transformed = surface_alignment(print).at("volumes").at(1);
    REQUIRE(transformed.at("complete") == true);
    REQUIRE(transformed.at("non_horizontal_facet_band_count") == 1);
    // The transformed surface is z = 6*y, whose unit normal has |nz|=1/sqrt(37).
    REQUIRE(transformed.at("max_vertical_projection_normal_offset_mm").get<double>() == Approx(0.125 / std::sqrt(37.)).margin(1e-9));
}

TEST_CASE("Slice analysis source alignment is independent of Boolean regions and source-cache validity", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 1);
    add_source_facets(*source, {{0.f, 0.f, 0.f}, {1.f, 0.f, 1.f}, {0.f, 1.f, 0.f}});
    source->add_volume(TriangleMesh(its_make_cube(10., 10., 2.)), ModelVolumeType::NEGATIVE_VOLUME, false);
    source->add_volume(TriangleMesh(its_make_cube(10., 10., 2.)), ModelVolumeType::PARAMETER_MODIFIER, false);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.25, 0.5, 0.375);
    // Empty final sections and a missing raw cache must not invent an exterior
    // surface or suppress this explicitly source-only observation.
    const json original = surface_alignment(print);
    REQUIRE(original.at("volumes").size() == 2);
    REQUIRE(original.at("volumes").at(1).at("volume_index") == 1);
    REQUIRE(original.at("volumes").at(1).at("complete") == true);
    REQUIRE(volume_sampling(print).at("volumes").at(1).at("complete") == false);
    layer->add_region(&object->printing_region(0))->fills.append(
        planned_path(0., 0., 1., 0., std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(surface_alignment(print) == original);
}

TEST_CASE("Slice analysis invalid source facets invalidate only the new surface observation", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    ModelVolume* volume = object->model_object()->volumes[0];
    Layer*       layer  = object->add_layer(0, 0.25, 0.5, 0.375);
    layer->lslices     = {rectangle(0., 0., 4., 4.)};
    object->firstLayerObjSliceMod().push_back({volume->id(), {layer->lslices}});
    const json previous_sampling = volume_sampling(print);
    REQUIRE(previous_sampling.at("volumes").at(0).at("complete") == true);
    indexed_triangle_set mesh = volume->mesh().its;
    const int            first = int(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), {{0.f, 0.f, 0.f}, {1.f, 0.f, 1.f}, {2.f, 0.f, 2.f}});
    mesh.indices.emplace_back(first, first + 1, first + 2);
    bool bounds_valid = true;
    SECTION("a finite degenerate source triangle leaves the previous sampling report unchanged") {}
    SECTION("a nonfinite vertex follows the previous float-transform Z validity rule")
    {
        mesh.vertices.back().x() = std::numeric_limits<float>::quiet_NaN();
        const Transform3f transform = (object->trafo_centered() * volume->get_matrix()).cast<float>();
        bounds_valid = std::isfinite((transform * mesh.vertices.back()).z());
    }
    volume->set_mesh(std::move(mesh));
    const json observed = surface_alignment(print).at("volumes").at(0);
    REQUIRE(observed.at("complete") == false);
    for (const char* key : {"non_horizontal_facet_band_count", "max_vertical_projection_normal_offset_mm",
                            "horizontal_facet_count", "max_horizontal_boundary_offset_mm"})
        REQUIRE(observed.at(key).is_null());
    json expected_sampling = previous_sampling;
    if (!bounds_valid)
        for (const char* key : {"z_min_mm", "z_max_mm", "interior_plane_count", "boundary_plane_count"})
            expected_sampling.at("volumes").at(0)[key] = nullptr;
    REQUIRE(volume_sampling(print) == expected_sampling);
}

TEST_CASE("Slice analysis source surface alignment does not invent a valid layer schedule", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    SECTION("no retained layer") {}
    SECTION("zero source layer height") { object->add_layer(0, 0., 0.5, 0.375); }
    SECTION("nonfinite source sampling plane") { object->add_layer(0, 0.25, 0.5, std::numeric_limits<double>::quiet_NaN()); }
    SECTION("unordered source layer planes")
    {
        object->add_layer(0, 0.25, 0.5, 0.375);
        object->add_layer(1, 0.25, 0.75, 0.125);
    }
    SECTION("layer slicing errors") { object->add_layer(0, 0.25, 0.5, 0.375)->slicing_errors = true; }
    SECTION("empty source mesh")
    {
        object->add_layer(0, 0.25, 0.5, 0.375);
        object->model_object()->volumes[0]->set_mesh(indexed_triangle_set{});
    }
    const json volume = surface_alignment(print).at("volumes").at(0);
    REQUIRE(volume.at("complete") == false);
    for (const char* key : {"non_horizontal_facet_band_count", "max_vertical_projection_normal_offset_mm",
                            "horizontal_facet_count", "max_horizontal_boundary_offset_mm"})
        REQUIRE(volume.at(key).is_null());
}

TEST_CASE("Slice analysis distinguishes a missing section neck from a wholly unprinted island", "[SliceAnalysis]")
{
    const bool connect_neck = GENERATE(false, true);
    Model      model;
    Print      print;
    add_box(model, 2);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {rectangle(0., 0., 4., 4.), rectangle(4., 1.8, 4., 0.4), rectangle(8., 0., 4., 4.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fills.append(planned_path(0., 2., 4., 2., 4.f));
    region->fills.append(planned_path(8., 2., 12., 2., 4.f));
    if (connect_neck)
        region->fills.append(planned_path(3.5, 2., 8.5, 2.));

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("section_island_count") == 1);
    REQUIRE(measured.at("section_area_mm2").get<double>() == Approx(33.6).margin(1e-5));
    REQUIRE(measured.at("section_covered_area_mm2").get<double>() == Approx(connect_neck ? 33.6 : 32.).margin(1e-5));
    REQUIRE(measured.at("section_uncovered_area_mm2").get<double>() == Approx(connect_neck ? 0. : 1.6).margin(1e-5));
    REQUIRE(measured.at("completely_unprinted_island_count") == 0);
    REQUIRE(measured.at("section_islands_with_multiple_planned_regions_count") == (connect_neck ? 0 : 1));
    REQUIRE(measured.at("planned_regions_overlapping_multiple_section_islands_count") == 0);
}

TEST_CASE("Slice analysis counts clipped planned regions without treating ordinary sparse gaps as defects", "[SliceAnalysis]")
{
    const bool connect_outside_section = GENERATE(false, true);
    Model      model;
    Print      print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {rectangle(0., 0., 10., 10.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fill_surfaces.append(layer->lslices, stInternal);
    region->fills.append(planned_path(-1., 2., 9., 2., 0.4f, erInternalInfill));
    region->fills.append(planned_path(-1., 8., 9., 8., 0.4f, erInternalInfill));
    if (connect_outside_section)
        region->fills.append(planned_path(-1., 2., -1., 8.));

    const auto measured = coverage_layers(print).at(0);
    // The outside connector makes one full footprint, but its intersection
    // with this target still consists of two regions. Sparse gaps are normal.
    REQUIRE(measured.at("section_island_count") == 1);
    REQUIRE(measured.at("section_islands_with_multiple_planned_regions_count") == 1);
    REQUIRE(measured.at("planned_regions_overlapping_multiple_section_islands_count") == 0);
    REQUIRE(measured.at("completely_unprinted_island_count") == 0);
    REQUIRE(measured.at("solid_fill_target_area_mm2") == 0.);
    REQUIRE(measured.at("solid_fill_uncovered_area_mm2") == 0.);
}

TEST_CASE("Slice analysis uses full footprints and positive area for cross-island overlaps", "[SliceAnalysis]")
{
    // Separate paths, a crossing footprint, edge contact and point contact.
    const int scenario = GENERATE(0, 1, 2, 3);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {rectangle(0., 0., 2., 2.), rectangle(3., scenario == 3 ? 2. : 0., 2., 2.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    if (scenario == 0) {
        region->fills.append(planned_path(0., 1., 2., 1., 1.f));
        region->fills.append(planned_path(3., 1., 5., 1., 1.f));
    } else {
        region->fills.append(planned_path(1., 1., scenario == 1 ? 4. : 3., 1., scenario == 3 ? 2.f : 1.f));
    }

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("section_island_count") == 2);
    REQUIRE(measured.at("section_islands_with_multiple_planned_regions_count") == 0);
    REQUIRE(measured.at("planned_regions_overlapping_multiple_section_islands_count") == (scenario == 1 ? 1 : 0));
    REQUIRE(measured.at("completely_unprinted_island_count") == (scenario >= 2 ? 1 : 0));
}

TEST_CASE("Slice analysis distinguishes untouched, invaded, divided and filled bounded voids", "[SliceAnalysis]")
{
    // No paths, internal intrusion, a cross-cut, full coverage, an inner
    // material island only, and a cross-cut through that inner material.
    const int scenario = GENERATE(0, 1, 2, 3, 4, 5);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {square_with_hole()};
    if (scenario >= 4)
        layer->lslices.push_back(rectangle(4., 4., 2., 2.));
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    if (scenario == 1)
        region->fills.append(planned_path(4., 5., 6., 5., 1.f));
    else if (scenario == 2 || scenario == 5)
        region->fills.append(planned_path(2., 5., 8., 5., 1.f));
    else if (scenario == 3)
        region->fills.append(planned_path(2., 5., 8., 5., 4.f));
    else if (scenario == 4)
        region->fills.append(planned_path(4.5, 5., 5.5, 5., 1.f));

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("closed_void_region_count") == 1);
    REQUIRE(measured.at("closed_void_fully_covered_count") == (scenario == 3 ? 1 : 0));
    REQUIRE(measured.at("closed_voids_with_multiple_remaining_regions_count") == (scenario == 2 || scenario == 5 ? 1 : 0));
    REQUIRE(measured.at("hole_area_mm2").get<double>() == Approx(scenario >= 4 ? 12. : 16.));
    const double expected_intrusion[] = {0., 2., 4., 16., 0., 2.};
    REQUIRE(measured.at("hole_intrusion_area_mm2").get<double>() == Approx(expected_intrusion[scenario]));
}

TEST_CASE("Slice analysis keeps vanished and split voids separate instead of cancelling their region counts", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    ExPolygon    shape  = rectangle(0., 0., 20., 10.);
    for (double x : {3., 13.}) {
        Polygon hole = rectangle(x, 3., 4., 4.).contour;
        hole.make_clockwise();
        shape.holes.push_back(std::move(hole));
    }
    layer->lslices      = {shape};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fills.append(planned_path(2., 5., 8., 5., 4.f));
    region->fills.append(planned_path(12., 5., 18., 5., 1.f));

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("closed_void_region_count") == 2);
    REQUIRE(measured.at("closed_void_fully_covered_count") == 1);
    REQUIRE(measured.at("closed_voids_with_multiple_remaining_regions_count") == 1);
    REQUIRE(measured.at("hole_area_mm2").get<double>() == Approx(32.));
    REQUIRE(measured.at("hole_intrusion_area_mm2").get<double>() == Approx(20.));
}

TEST_CASE("Slice analysis candidate pruning agrees with full clipping for distant islands and nested voids", "[SliceAnalysis]")
{
    const bool reverse_order = GENERATE(false, true);
    Model      model;
    Print      print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object   = print.get_object(0);
    Layer*       previous = object->add_layer(0, 0.2, 0.2, 0.1);
    Layer*       current  = object->add_layer(1, 0.2, 0.4, 0.3);
    LayerRegion* region   = current->add_region(&object->printing_region(0));
    ExtrusionPaths paths;
    constexpr int copies = 3;
    for (int index = 0; index < copies * 8; ++index) {
        const double x        = 30. * index;
        const int    scenario = index % 8;
        ExPolygon    ring     = square_with_hole();
        ring.translate(scaled(x), 0);
        current->lslices.push_back(ring);
        if (scenario == 6 || scenario == 7)
            ring.translate(0, scaled(scenario == 6 ? -10. : -20.));
        previous->lslices.push_back(ring);
        if (scenario == 4 || scenario == 5) {
            current->lslices.push_back(rectangle(x + 4., 4., 2., 2.));
            previous->lslices.push_back(current->lslices.back());
        }
        switch (scenario) {
        case 0: break; // A source island with no nearby footprint.
        case 1:
            paths.push_back(planned_path(x + 1., 1., x + 9., 1., 0.5f));
            paths.push_back(planned_path(x + 1., 2., x + 9., 2., 0.5f));
            break;
        case 2: paths.push_back(planned_path(x + 2., 5., x + 8., 5., 1.f)); break;
        case 3: paths.push_back(planned_path(x + 2., 5., x + 8., 5., 4.f)); break;
        case 4: paths.push_back(planned_path(x + 2., 5., x + 8., 5., 1.f)); break;
        case 5: paths.push_back(planned_path(x + 4., 5., x + 6., 5., 2.f)); break;
        case 6: paths.push_back(planned_path(x, -0.5, x + 10., -0.5, 1.f)); break; // Bounding boxes touch only.
        case 7: paths.push_back(planned_path(x, -2., x + 10., -2., 1.f)); break;
        }
    }
    if (reverse_order) {
        std::reverse(previous->lslices.begin(), previous->lslices.end());
        std::reverse(current->lslices.begin(), current->lslices.end());
        std::reverse(paths.begin(), paths.end());
    }
    Polygons footprint_polygons;
    for (const ExtrusionPath& path : paths) {
        region->fills.append(path);
        polygons_append(footprint_polygons, path.polygons_covered_by_width(0.f));
    }
    const ExPolygons section = union_ex(current->lslices);
    const ExPolygons lower   = union_ex(previous->lslices);
    const ExPolygons covered = union_ex(footprint_polygons);
    Polygons        hole_contours;
    for (const ExPolygon& island : section)
        for (Polygon hole : island.holes) {
            hole.make_counter_clockwise();
            hole_contours.push_back(std::move(hole));
        }
    const ExPolygons holes = diff_ex(union_ex(hole_contours), section);
    const auto area = [](const ExPolygons& polygons) {
        double value = 0.;
        for (const ExPolygon& polygon : polygons)
            value += polygon.area() * SCALING_FACTOR * SCALING_FACTOR;
        return value;
    };
    const auto count = [](const ExPolygons& polygons) {
        return std::count_if(polygons.begin(), polygons.end(), [](const ExPolygon& polygon) { return polygon.area() > 0.; });
    };
    // Reference deliberately clips the complete sets, without any bounds
    // preselection. It runs on the same Print as the optimized report.
    size_t unprinted = 0, split_islands = 0, crossed_islands = 0, filled_voids = 0, split_voids = 0, no_previous = 0;
    double unprinted_area = 0., no_previous_area = 0.;
    for (const ExPolygon& island : section) {
        const auto regions = count(intersection_ex(ExPolygons{island}, covered));
        if (regions == 0) {
            ++unprinted;
            unprinted_area += island.area() * SCALING_FACTOR * SCALING_FACTOR;
        }
        split_islands += regions > 1;
        if (intersection_ex(ExPolygons{island}, lower).empty()) {
            ++no_previous;
            no_previous_area += island.area() * SCALING_FACTOR * SCALING_FACTOR;
        }
    }
    for (const ExPolygon& footprint : covered) {
        size_t overlaps = 0;
        for (const ExPolygon& island : section)
            overlaps += area(intersection_ex(ExPolygons{footprint}, ExPolygons{island})) > 0.;
        crossed_islands += overlaps > 1;
    }
    for (const ExPolygon& hole : holes) {
        const auto remaining = count(diff_ex(ExPolygons{hole}, covered));
        filled_voids += remaining == 0;
        split_voids += remaining > 1;
    }
    const json expected{{"section_island_count", count(section)},
                        {"section_islands_with_multiple_planned_regions_count", split_islands},
                        {"planned_regions_overlapping_multiple_section_islands_count", crossed_islands},
                        {"completely_unprinted_island_count", unprinted},
                        {"closed_void_region_count", count(holes)},
                        {"closed_void_fully_covered_count", filled_voids},
                        {"closed_voids_with_multiple_remaining_regions_count", split_voids},
                        {"completely_unprinted_island_area_mm2", unprinted_area},
                        {"section_area_mm2", area(section)},
                        {"planned_path_area_mm2", area(covered)},
                        {"section_covered_area_mm2", area(intersection_ex(section, covered))},
                        {"section_uncovered_area_mm2", area(diff_ex(section, covered))},
                        {"hole_area_mm2", area(holes)},
                        {"hole_intrusion_area_mm2", area(intersection_ex(holes, covered))}};
    const auto before_sections = current->lslices;
    const auto before_paths    = region->fills.as_polylines();
    const json report          = make_slice_analysis_report(print, InitializedResult{});
    const json& group          = report.at("objects").at("groups").at(0);
    const json& measured       = group.at("planned_layer_coverage").at("layers").at(1);
    REQUIRE(measured.at("complete") == true);
    for (const auto& item : expected.items()) {
        INFO(item.key());
        REQUIRE(measured.at(item.key()) == item.value());
    }
    const json& cross = group.at("cross_layer_geometry").at("layers").at(1);
    REQUIRE(cross.at("complete") == true);
    REQUIRE(cross.at("no_previous_overlap_island_count") == no_previous);
    REQUIRE(cross.at("no_previous_overlap_island_area_mm2") == no_previous_area);
    REQUIRE(measured.at("section_island_count") == copies * 10);
    REQUIRE(measured.at("completely_unprinted_island_count") == copies * 4);
    REQUIRE(measured.at("section_islands_with_multiple_planned_regions_count") == copies * 4);
    REQUIRE(measured.at("planned_regions_overlapping_multiple_section_islands_count") == copies);
    REQUIRE(measured.at("closed_void_region_count") == copies * 8);
    REQUIRE(measured.at("closed_void_fully_covered_count") == copies);
    REQUIRE(measured.at("closed_voids_with_multiple_remaining_regions_count") == copies * 2);
    REQUIRE(measured.at("section_area_mm2").get<double>() == Approx(copies * 680.));
    REQUIRE(measured.at("section_covered_area_mm2").get<double>() == Approx(copies * 26.));
    REQUIRE(measured.at("hole_intrusion_area_mm2").get<double>() == Approx(copies * 22.));
    REQUIRE(cross.at("no_previous_overlap_island_count") == copies * 2);
    REQUIRE(cross.at("no_previous_overlap_island_area_mm2").get<double>() == Approx(copies * 168.));
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(current->lslices == before_sections);
    REQUIRE(region->fills.as_polylines() == before_paths);
}

TEST_CASE("Slice analysis does not invent a bounded-void target for an open slot", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    // A U-shaped section with an open slot, not an enclosed polygon hole.
    layer->lslices      = {rectangle(0., 0., 10., 3.), rectangle(0., 3., 4.75, 7.), rectangle(5.25, 3., 4.75, 7.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fills.append(planned_path(4.6, 7., 4.6, 9., 1.f));
    region->fills.append(planned_path(5.4, 7., 5.4, 9., 1.f));

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("section_island_count") == 1);
    REQUIRE(measured.at("closed_void_region_count") == 0);
    REQUIRE(measured.at("closed_void_fully_covered_count") == 0);
    REQUIRE(measured.at("closed_voids_with_multiple_remaining_regions_count") == 0);
    REQUIRE(measured.at("hole_area_mm2") == 0.);
    REQUIRE(measured.at("hole_intrusion_area_mm2") == 0.);
}

TEST_CASE("Slice analysis leaves planned coverage and region counts unknown for invalid layer geometry", "[SliceAnalysis]")
{
    const int scenario = GENERATE(0, 1, 2, 3, 4, 5, 6);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {square_with_hole()};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fills.append(planned_path(0., 1., 10., 1., 1.f));
    switch (scenario) {
    case 0: layer->slicing_errors = true; break;
    case 1: layer->height = 0.; break;
    case 2: layer->height = -0.2; break;
    case 3: layer->height = std::numeric_limits<double>::quiet_NaN(); break;
    case 4: layer->height = std::numeric_limits<double>::infinity(); break;
    case 5: layer->slice_z = std::numeric_limits<double>::quiet_NaN(); break;
    case 6: layer->print_z = std::numeric_limits<double>::quiet_NaN(); break;
    }

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("complete") == false);
    REQUIRE(measured.at("invalid_path_count") == 0);
    REQUIRE(measured.at("section_area_mm2").get<double>() == Approx(84.));
    REQUIRE(measured.at("hole_area_mm2").get<double>() == Approx(16.));
    for (const char* key :
         {"planned_path_area_mm2", "section_covered_area_mm2", "section_uncovered_area_mm2", "completely_unprinted_island_count",
          "completely_unprinted_island_area_mm2", "hole_intrusion_area_mm2", "solid_fill_uncovered_area_mm2", "top_fill_uncovered_area_mm2",
          "bottom_fill_uncovered_area_mm2", "section_island_count", "section_islands_with_multiple_planned_regions_count",
          "planned_regions_overlapping_multiple_section_islands_count", "closed_void_region_count", "closed_void_fully_covered_count",
          "closed_voids_with_multiple_remaining_regions_count", "planned_enclosed_exterior_region_count",
          "planned_enclosed_exterior_area_mm2", "section_min_island_gap_mm"})
        REQUIRE(measured.at(key).is_null());
}

TEST_CASE("Slice analysis measures exterior voids bounded by actual planned paths without supplying missing walls", "[SliceAnalysis]")
{
    // Open U, enclosed U, missing planned bottom, completely filled U,
    // no paths, and an otherwise closed U with a real gap in its top paths.
    const int scenario = GENERATE(0, 1, 2, 3, 4, 5);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {rectangle(0., 0., 3., 10.), rectangle(7., 0., 3., 10.), rectangle(0., 0., 10., 3.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    if (scenario < 3 || scenario == 5) {
        region->fills.append(planned_path(1.5, 0., 1.5, 10., 3.f));
        region->fills.append(planned_path(8.5, 0., 8.5, 10., 3.f));
        if (scenario != 2)
            region->fills.append(planned_path(0., 1.5, 10., 1.5, 3.f));
        if (scenario == 5) {
            region->fills.append(planned_path(0., 8.5, 4.9, 8.5, 3.f));
            region->fills.append(planned_path(5.1, 8.5, 10., 8.5, 3.f));
        } else if (scenario != 0)
            region->fills.append(planned_path(0., 8.5, 10., 8.5, 3.f));
    } else if (scenario == 3) {
        region->fills.append(planned_path(0., 5., 10., 5., 10.f));
    }

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("closed_void_region_count") == 0);
    REQUIRE(measured.at("planned_enclosed_exterior_region_count") == (scenario == 1 ? 1 : 0));
    REQUIRE(measured.at("planned_enclosed_exterior_area_mm2").get<double>() == Approx(scenario == 1 ? 16. : 0.));
    // Zero in the filled case describes absence of remaining empty space,
    // not preservation of the opening or a printability verdict.
}

TEST_CASE("Slice analysis excludes sparse interiors and original bounded voids from newly enclosed exterior", "[SliceAnalysis]")
{
    const bool source_has_hole = GENERATE(false, true);
    Model      model;
    Print      print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {source_has_hole ? square_with_hole() : rectangle(0., 0., 10., 10.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fill_surfaces.append(layer->lslices, stInternal);
    region->fills.append(planned_path(1.5, 0., 1.5, 10., 3.f));
    region->fills.append(planned_path(8.5, 0., 8.5, 10., 3.f));
    region->fills.append(planned_path(0., 1.5, 10., 1.5, 3.f));
    region->fills.append(planned_path(0., 8.5, 10., 8.5, 3.f));

    const ExPolygons           source_before = layer->lslices;
    std::vector<ExtrusionPath> paths_before;
    for (const ExtrusionEntity* entity : region->fills.entities)
        paths_before.push_back(dynamic_cast<const ExtrusionPath&>(*entity));
    const auto measured = coverage_layers(print).at(0);
    REQUIRE(coverage_layers(print).at(0) == measured);
    REQUIRE(layer->lslices == source_before);
    for (size_t index = 0; index < paths_before.size(); ++index) {
        const auto& after = dynamic_cast<const ExtrusionPath&>(*region->fills.entities[index]);
        REQUIRE(after.width == paths_before[index].width);
        REQUIRE(after.polyline.points == paths_before[index].polyline.points);
    }
    REQUIRE(measured.at("closed_void_region_count") == (source_has_hole ? 1 : 0));
    REQUIRE(measured.at("planned_enclosed_exterior_region_count") == 0);
    REQUIRE(measured.at("planned_enclosed_exterior_area_mm2") == 0.);
    REQUIRE(measured.at("solid_fill_target_area_mm2") == 0.);
}

TEST_CASE("Slice analysis excludes nested source and planned material from enclosed exterior empty space", "[SliceAnalysis]")
{
    // The central island is present in source only, paths only or both.
    // A nested planned ring also leaves its own bounded empty interior.
    const int scenario = GENERATE(0, 1, 2, 3);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {rectangle(0., 0., 3., 10.), rectangle(7., 0., 3., 10.), rectangle(0., 0., 10., 3.)};
    if (scenario == 0 || scenario == 2)
        layer->lslices.push_back(rectangle(4., 4., 2., 2.));
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fills.append(planned_path(1.5, 0., 1.5, 10., 3.f));
    region->fills.append(planned_path(8.5, 0., 8.5, 10., 3.f));
    region->fills.append(planned_path(0., 1.5, 10., 1.5, 3.f));
    region->fills.append(planned_path(0., 8.5, 10., 8.5, 3.f));
    if (scenario == 1 || scenario == 2)
        region->fills.append(planned_path(4., 5., 6., 5., 2.f));
    else if (scenario == 3) {
        region->fills.append(planned_path(4.25, 4., 4.25, 6., 0.5f));
        region->fills.append(planned_path(5.75, 4., 5.75, 6., 0.5f));
        region->fills.append(planned_path(4., 4.25, 6., 4.25, 0.5f));
        region->fills.append(planned_path(4., 5.75, 6., 5.75, 0.5f));
    }

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("planned_enclosed_exterior_region_count") == (scenario == 3 ? 2 : 1));
    REQUIRE(measured.at("planned_enclosed_exterior_area_mm2").get<double>() == Approx(scenario == 3 ? 13. : 12.));
}

TEST_CASE("Slice analysis counts each planned void once despite exterior fragments and boundary-only source contact", "[SliceAnalysis]")
{
    // A source stripe divides the exterior contribution into two fragments;
    // the other sources cover the planned void or touch only its edge/point.
    const int scenario = GENERATE(0, 1, 2, 3);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject*    object    = print.get_object(0);
    Layer*          layer     = object->add_layer(0, 0.2, 0.2, 0.1);
    const ExPolygon sources[] = {rectangle(4., 0., 2., 10.), rectangle(2., 2., 6., 6.), rectangle(4., 0., 2., 2.),
                                 rectangle(0., 0., 2., 2.)};
    layer->lslices            = {sources[scenario]};
    LayerRegion* region       = layer->add_region(&object->printing_region(0));
    region->fills.append(planned_path(1., 0., 1., 10., 2.f));
    region->fills.append(planned_path(9., 0., 9., 10., 2.f));
    region->fills.append(planned_path(0., 1., 10., 1., 2.f));
    region->fills.append(planned_path(0., 9., 10., 9., 2.f));

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("planned_enclosed_exterior_region_count") == (scenario == 1 ? 0 : 1));
    const double expected_area[] = {24., 0., 36., 36.};
    REQUIRE(measured.at("planned_enclosed_exterior_area_mm2").get<double>() == Approx(expected_area[scenario]));
}

TEST_CASE("Slice analysis measures the minimum between all retained island boundaries", "[SliceAnalysis]")
{
    const int scenario = GENERATE(0, 1, 2, 3, 4, 5, 6);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    switch (scenario) {
    case 0: layer->lslices = {rectangle(0., 0., 2., 2.), rectangle(2.5, 0., 2., 2.)}; break;
    case 1: layer->lslices = {rectangle(0., 0., 2., 2.), rectangle(3., 3., 2., 2.)}; break;
    // The short upper edge projects onto the middle of the lower long edge;
    // checking only the lower rectangle's vertices would overestimate the gap.
    case 2: layer->lslices = {rectangle(0., 0., 10., 1.), rectangle(4., 2., 2., 1.)}; break;
    // The closest edge is the first rectangle's final closing segment.
    case 3: layer->lslices = {rectangle(0., 0., 2., 2.), rectangle(-2., 0.5, 1., 1.)}; break;
    case 4: layer->lslices = {square_with_hole(), rectangle(4., 4., 2., 2.)}; break;
    case 5: layer->lslices = {rectangle(0., 0., 1., 1.), rectangle(10., 0., 1., 1.), rectangle(12.25, 0., 1., 1.)}; break;
    case 6: layer->lslices = {rectangle(0., 0., 2., 2.), rectangle(2., 2., 2., 2.)}; break;
    }

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("section_island_count") == (scenario == 5 ? 3 : 2));
    const double expected_gap[] = {0.5, std::sqrt(2.), 1., 1., 1., 1.25, 0.};
    REQUIRE(measured.at("section_min_island_gap_mm").get<double>() == Approx(expected_gap[scenario]).margin(1e-9));
}

TEST_CASE("Slice analysis does not label a single island's opening or empty section as an inter-island gap", "[SliceAnalysis]")
{
    const int scenario = GENERATE(0, 1, 2, 3, 4);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    switch (scenario) {
    case 0: break;
    case 1: layer->lslices = {square_with_hole()}; break;
    case 2: layer->lslices = {rectangle(0., 0., 3., 10.), rectangle(7., 0., 3., 10.), rectangle(0., 0., 10., 3.)}; break;
    case 3: layer->lslices = {rectangle(0., 0., 3., 10.), rectangle(0., 0., 10., 3.), rectangle(0., 7., 10., 3.)}; break;
    // A shared edge is merged into one normalized island, not a zero-gap pair.
    case 4: layer->lslices = {rectangle(0., 0., 2., 2.), rectangle(2., 0., 2., 2.)}; break;
    }

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("section_island_count") == (scenario == 0 ? 0 : 1));
    REQUIRE(measured.at("section_min_island_gap_mm").is_null());
}

TEST_CASE("Slice analysis measures same-island parallel material and open-slot boundaries", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 3., 10.), rectangle(7., 0., 3., 10.), rectangle(0., 0., 10., 3.)};
    const json first = coverage_layers(print).at(0).at("parallel_boundary_clearance");
    REQUIRE(first.at("scope") == "same_section_island_parallel_voronoi_sites");
    REQUIRE(first.at("complete") == true);
    REQUIRE(first.at("min_material_width_mm").get<double>() == Approx(3.).epsilon(0.).margin(1e-10));
    REQUIRE(first.at("min_void_width_mm").get<double>() == Approx(4.).epsilon(0.).margin(1e-10));
    REQUIRE(coverage_layers(print).at(0).at("section_min_island_gap_mm").is_null());

    // Planned material can occupy the slot without changing this source-section
    // measurement. It is not compensated-path clearance or actual fit tolerance.
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fills.append(planned_path(5., 3., 5., 10., 6.f));
    const ExPolygons sections = layer->lslices;
    const Polylines  paths    = region->fills.as_polylines();
    const auto       settings = all_settings(print);
    REQUIRE(coverage_layers(print).at(0).at("parallel_boundary_clearance") == first);
    REQUIRE(coverage_layers(print).at(0).at("parallel_boundary_clearance") == first);
    REQUIRE(layer->lslices == sections);
    REQUIRE(region->fills.as_polylines() == paths);
    REQUIRE(all_settings(print) == settings);
}

TEST_CASE("Slice analysis preserves a parallel section neck under rotation and large translation", "[SliceAnalysis]")
{
    const bool rotate = GENERATE(false, true);
    const bool shift  = GENERATE(false, true);
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    Layer* layer = print.get_object(0)->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 4., 4.), rectangle(4., 1.75, 4., 0.5), rectangle(8., 0., 4., 4.)};
    for (ExPolygon& polygon : layer->lslices)
        for (Point& point : polygon.contour.points) {
            if (rotate) {
                // Exact 3-4-5 rotation in integer geometry, not a near-parallel
                // angle tolerance introduced by the measurement.
                const coord_t x = point.x(), y = point.y();
                point = Point((3 * x - 4 * y) / 5, (4 * x + 3 * y) / 5);
            }
            if (shift)
                point += Point(scaled(100000.), scaled(-200000.));
        }
    const json value = coverage_layers(print).at(0).at("parallel_boundary_clearance");
    REQUIRE(value.at("complete") == true);
    REQUIRE(value.at("min_material_width_mm").get<double>() == Approx(0.5).epsilon(0.).margin(1e-10));
}

TEST_CASE("Slice analysis distinguishes a bounded parallel void from another island occupying it", "[SliceAnalysis]")
{
    const bool nested_material = GENERATE(false, true);
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    Layer* layer = print.get_object(0)->add_layer(0, 0.2, 0.2, 0.1);
    ExPolygon hollow = rectangle(0., 0., 14., 14.);
    Polygon hole = rectangle(4., 3., 6., 8.).contour;
    hole.make_clockwise();
    hollow.holes.push_back(hole);
    layer->lslices = {hollow};
    if (nested_material)
        layer->lslices.push_back(rectangle(6., 4., 2., 6.));
    const json value = coverage_layers(print).at(0).at("parallel_boundary_clearance");
    REQUIRE(value.at("complete") == true);
    if (nested_material) {
        // All islands must participate in nearest-site construction. Measuring
        // the outer island alone would report a six-mm void through this body.
        REQUIRE(value.at("min_void_width_mm").is_null());
        REQUIRE(value.at("min_material_width_mm").get<double>() == Approx(2.).epsilon(0.).margin(1e-10));
    } else {
        REQUIRE(value.at("min_void_width_mm").get<double>() == Approx(6.).epsilon(0.).margin(1e-10));
        REQUIRE(value.at("min_material_width_mm").get<double>() == Approx(3.).epsilon(0.).margin(1e-10));
    }
}

TEST_CASE("Slice analysis does not mix different-island gaps into same-island parallel void widths", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    Layer* layer = print.get_object(0)->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 2., 8.), rectangle(2.5, 0., 2., 8.)};
    const json result = coverage_layers(print).at(0);
    const auto& value = result.at("parallel_boundary_clearance");
    REQUIRE(value.at("complete") == true);
    REQUIRE(value.at("min_material_width_mm").get<double>() == Approx(2.).epsilon(0.).margin(1e-10));
    REQUIRE(value.at("min_void_width_mm").is_null());
    REQUIRE(result.at("section_min_island_gap_mm").get<double>() == Approx(0.5));
}

TEST_CASE("Slice analysis leaves absent parallel-boundary observations inapplicable rather than zero", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    Layer* layer = print.get_object(0)->add_layer(0, 0.2, 0.2, 0.1);
    SECTION("empty section") {}
    SECTION("a triangle has no parallel boundary pairs")
    {
        layer->lslices = {ExPolygon(Polygon({Point(0, 0), Point(scaled(5.), 0), Point(scaled(1.), scaled(3.))}))};
    }
    SECTION("a square's parallel sides meet only at a zero-length Voronoi feature")
    {
        layer->lslices = {rectangle(0., 0., 4., 4.)};
    }
    const json value = coverage_layers(print).at(0).at("parallel_boundary_clearance");
    REQUIRE(value.at("complete") == true);
    REQUIRE(value.at("min_material_width_mm").is_null());
    REQUIRE(value.at("min_void_width_mm").is_null());
}

TEST_CASE("Slice analysis parallel-boundary validity is explicit without invalidating existing coverage", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 2., 8.)};
    bool old_complete = false;
    SECTION("invalid layer geometry") { layer->slicing_errors = true; }
    SECTION("invalid planned-path coverage")
    {
        layer->add_region(&object->printing_region(0))->fills.append(
            planned_path(1., 1., 1., 7., std::numeric_limits<float>::quiet_NaN()));
    }
    SECTION("a section wider than the Voronoi integer domain remains valid old coverage")
    {
        old_complete = true;
        layer->lslices = {rectangle(0., 0., 5000., 2.)};
    }
    const json result = coverage_layers(print).at(0);
    REQUIRE(result.at("complete") == old_complete);
    const auto& value = result.at("parallel_boundary_clearance");
    REQUIRE(value.at("complete") == false);
    REQUIRE(value.at("min_material_width_mm").is_null());
    REQUIRE(value.at("min_void_width_mm").is_null());
}

TEST_CASE("Slice analysis measures repeated parallel comb features without short-branch filtering", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    Layer* layer = print.get_object(0)->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 199., 2.)};
    for (size_t tooth = 0; tooth < 100; ++tooth)
        layer->lslices.push_back(rectangle(2. * tooth, 2., 1., 8.));
    const json value = coverage_layers(print).at(0).at("parallel_boundary_clearance");
    REQUIRE(value.at("complete") == true);
    REQUIRE(value.at("min_material_width_mm").get<double>() == Approx(1.).epsilon(0.).margin(1e-10));
    REQUIRE(value.at("min_void_width_mm").get<double>() == Approx(1.).epsilon(0.).margin(1e-10));
}

TEST_CASE("Slice analysis preserves large-coordinate geometry and paths while measuring a per-instance island gap", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    PrintObject* object               = print.get_object(0);
    Layer*       layer                = object->add_layer(0, 0.2, 0.2, 0.1);
    const double x                    = 1000000.;
    const double y                    = -1000000.;
    layer->lslices                    = {rectangle(x, y, 2., 2.), rectangle(x + 2.5, y, 2., 2.)};
    LayerRegion*        region        = layer->add_region(&object->printing_region(0));
    const ExtrusionPath original_path = planned_path(x, y + 1., x + 2., y + 1., 1.f);
    region->fills.append(original_path);
    const ExPolygons source_before = layer->lslices;

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("section_min_island_gap_mm").get<double>() == Approx(0.5).margin(1e-9));
    REQUIRE(coverage_layers(print).at(0) == measured);
    REQUIRE(layer->lslices == source_before);
    const auto& path_after = dynamic_cast<const ExtrusionPath&>(*region->fills.entities.front());
    REQUIRE(path_after.polyline.points == original_path.polyline.points);
    REQUIRE(path_after.width == original_path.width);
    REQUIRE(object->instances().size() == 2);
}

TEST_CASE("Slice analysis withholds a known island gap when planned coverage is incomplete", "[SliceAnalysis]")
{
    const int scenario = GENERATE(0, 1, 2);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {rectangle(0., 0., 2., 2.), rectangle(2.5, 0., 2., 2.)};
    REQUIRE(coverage_layers(print).at(0).at("section_min_island_gap_mm").get<double>() == Approx(0.5));
    if (scenario == 0)
        layer->add_region(&object->printing_region(0))->fills.append(planned_path(0., 1., 2., 1., -1.f));
    else if (scenario == 1)
        layer->slicing_errors = true;
    else
        layer->height = 0.;

    const auto measured = coverage_layers(print).at(0);
    REQUIRE(measured.at("complete") == false);
    REQUIRE(measured.at("section_min_island_gap_mm").is_null());
}

TEST_CASE("Slice analysis finds the highest overlapping support plane rather than the highest plane anywhere", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&object->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    object->add_support_layer(0, 0, 0.2, 0.8)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    object->add_support_layer(1, 0, 0.2, 0.9)->support_fills.append(planned_path(10., 2., 14., 2., 4.f, erSupportMaterial));
    object->add_support_layer(2, 0, 0.2, 1.3)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));

    const json projection = support_projection(print);
    REQUIRE(projection.at("scope") == "per_instance_plate_support_projection_before_gcode");
    REQUIRE(projection.at("instances").size() == 1);
    REQUIRE(projection.at("instances").at(0).at("instance_index") == 0);
    const auto& measured = projection.at("instances").at(0).at("layers").at(0);
    REQUIRE(measured.at("layer_index") == 0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("model_bottom_z_mm").get<double>() == Approx(1.));
    REQUIRE(measured.at("model_print_z_mm").get<double>() == Approx(1.2));
    REQUIRE(measured.at("target_area_mm2").get<double>() == Approx(16.));
    REQUIRE(measured.at("unmatched_target_area_mm2").get<double>() == Approx(0.));
    const auto& levels = measured.at("support_levels");
    REQUIRE(levels.size() == 1);
    REQUIRE(levels.at(0).at("support_print_z_mm").get<double>() == Approx(0.8));
    REQUIRE(levels.at(0).at("model_layer_bottom_gap_mm").get<double>() == Approx(0.2));
    REQUIRE(levels.at(0).at("nearest_projection_area_mm2").get<double>() == Approx(16.));
}

TEST_CASE("Slice analysis partitions support projections by nearest height without double counting lower paths", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&object->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    object->add_support_layer(0, 0, 0.2, 0.6)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    object->add_support_layer(1, 0, 0.2, 0.8)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    SupportLayer* upper = object->add_support_layer(2, 0, 0.1, 0.9);
    upper->support_fills.append(planned_path(0., 1., 4., 1., 2.f, erSupportMaterialInterface));
    // Same print Z and duplicate paths are one support footprint. Different
    // path heights do not change the observed top plane.
    ExtrusionPath duplicate = planned_path(0., 1., 4., 1., 2.f, erSupportMaterial);
    duplicate.height = 0.4f;
    object->add_support_layer(3, 0, 0.4, 0.9)->support_fills.append(duplicate);

    const json measured = support_projection(print).at("instances").at(0).at("layers").at(0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("target_area_mm2").get<double>() == Approx(16.));
    REQUIRE(measured.at("unmatched_target_area_mm2").get<double>() == Approx(0.));
    const auto& levels = measured.at("support_levels");
    REQUIRE(levels.size() == 2);
    REQUIRE(levels.at(0).at("support_print_z_mm").get<double>() == Approx(0.9));
    REQUIRE(levels.at(1).at("support_print_z_mm").get<double>() == Approx(0.8));
    REQUIRE(levels.at(0).at("nearest_projection_area_mm2").get<double>() == Approx(8.));
    REQUIRE(levels.at(1).at("nearest_projection_area_mm2").get<double>() == Approx(8.));
}

TEST_CASE("Slice analysis retains zero and negative nominal support gaps including the same model print plane", "[SliceAnalysis]")
{
    const double support_z = GENERATE(0.8, 1.0, 1.1, 1.2);
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&object->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    object->add_support_layer(0, 0, 0.2, support_z)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));

    const json measured = support_projection(print).at("instances").at(0).at("layers").at(0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("support_levels").size() == 1);
    REQUIRE(measured.at("support_levels").at(0).at("model_layer_bottom_gap_mm").get<double>() == Approx(1. - support_z).margin(1e-9));
}

TEST_CASE("Slice analysis restricts support targets to new section material that has planned model paths", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* first = object->add_layer(0, 0.2, 0.4, 0.3);
    Layer* second = object->add_layer(1, 0.3, 1., 0.85);
    Layer* third = object->add_layer(2, 0.1, 1.1, 1.05);
    first->lslices = {rectangle(0., 0., 2., 4.)};
    second->lslices = third->lslices = {rectangle(0., 0., 4., 4.)};
    for (Layer* layer : object->layers())
        layer->add_region(&object->printing_region(0))->fills.append(planned_path(0., 1., 4., 1., 2.f));
    object->add_support_layer(0, 0, 0.2, 0.6)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));

    const json measured = support_projection(print).at("instances").at(0).at("layers");
    REQUIRE(measured.size() == 3);
    for (size_t index = 0; index < measured.size(); ++index) {
        REQUIRE(measured.at(index).at("layer_index") == index);
        REQUIRE(measured.at(index).at("complete") == true);
    }
    REQUIRE(measured.at(0).at("target_area_mm2").get<double>() == Approx(4.));
    REQUIRE(measured.at(0).at("unmatched_target_area_mm2").get<double>() == Approx(4.));
    REQUIRE(measured.at(0).at("support_levels").empty());
    REQUIRE(measured.at(1).at("target_area_mm2").get<double>() == Approx(4.));
    REQUIRE(measured.at(1).at("unmatched_target_area_mm2").get<double>() == Approx(0.));
    REQUIRE(measured.at(1).at("support_levels").at(0).at("nearest_projection_area_mm2").get<double>() == Approx(4.));
    REQUIRE(measured.at(1).at("support_levels").at(0).at("model_layer_bottom_gap_mm").get<double>() == Approx(0.1));
    REQUIRE(measured.at(2).at("target_area_mm2").get<double>() == Approx(0.));
    REQUIRE(measured.at(2).at("unmatched_target_area_mm2").get<double>() == Approx(0.));
    REQUIRE(measured.at(2).at("support_levels").empty());
}

TEST_CASE("Slice analysis distinguishes absent or excluded support paths from missing support observations", "[SliceAnalysis]")
{
    const int scenario = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    if (scenario != 5)
        layer->add_region(&object->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    if (scenario != 0) {
        SupportLayer* support = object->add_support_layer(0, 0, 0.2, 0.8);
        if (scenario > 1) {
            ExtrusionPath path = planned_path(0., 2., 4., 2., 4.f, erSupportMaterial);
            if (scenario == 2)
                path.mm3_per_mm = 0.;
            else if (scenario == 3)
                path.set_force_no_extrusion(true);
            else if (scenario == 4)
                path.polyline = Polyline({Point(0, 0)});
            else if (scenario == 6)
                path = planned_path(0., -2., 4., -2., 4.f, erSupportMaterial);
            else if (scenario == 7)
                path = planned_path(4., -1., 6., -1., 2.f, erSupportMaterial);
            support->support_fills.append(path);
        }
    }
    const json measured = support_projection(print).at("instances").at(0).at("layers").at(0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("target_area_mm2").get<double>() == Approx(scenario == 5 ? 0. : 16.));
    REQUIRE(measured.at("unmatched_target_area_mm2").get<double>() == Approx(scenario == 5 ? 0. : 16.));
    REQUIRE(measured.at("support_levels").is_array());
    REQUIRE(measured.at("support_levels").empty());
}

TEST_CASE("Slice analysis isolates invalid plate support data from model-only metrics", "[SliceAnalysis]")
{
    const int scenario = GENERATE(0, 1, 2, 3);
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&object->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    const json coverage = coverage_layers(print);
    const json cross = cross_layers(print);
    SupportLayer* support = object->add_support_layer(0, 0, 0.2, 0.8);
    ExtrusionPath path = planned_path(0., 2., 4., 2., 4.f, erSupportMaterial);
    if (scenario == 0)
        path.width = -1.f;
    else if (scenario == 1)
        path.mm3_per_mm = std::numeric_limits<double>::quiet_NaN();
    else if (scenario == 2)
        support->print_z = std::numeric_limits<double>::quiet_NaN();
    else
        support->print_z = std::numeric_limits<double>::infinity();
    support->support_fills.append(path);

    const json measured = support_projection(print).at("instances").at(0).at("layers").at(0);
    REQUIRE(measured.at("complete") == false);
    REQUIRE(measured.at("target_area_mm2").is_null());
    REQUIRE(measured.at("unmatched_target_area_mm2").is_null());
    REQUIRE(measured.at("support_levels").is_null());
    REQUIRE(measured.at("model_bottom_z_mm").get<double>() == Approx(1.));
    REQUIRE(measured.at("model_print_z_mm").get<double>() == Approx(1.2));
    REQUIRE(coverage_layers(print) == coverage);
    REQUIRE(cross_layers(print) == cross);
}

TEST_CASE("Slice analysis leaves support targets unknown when adjacent model geometry is unavailable", "[SliceAnalysis]")
{
    const int scenario = GENERATE(0, 1, 2, 3, 4);
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* first = object->add_layer(0, 0.2, 0.8, 0.7);
    Layer* second = object->add_layer(1, 0.2, 1.2, 1.1);
    first->lslices = {rectangle(0., 0., 2., 4.)};
    second->lslices = {rectangle(0., 0., 4., 4.)};
    LayerRegion* region = second->add_region(&object->printing_region(0));
    region->fills.append(planned_path(0., 2., 4., 2., 4.f));
    object->add_support_layer(0, 0, 0.2, 1.)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    if (scenario == 0)
        first->slicing_errors = true;
    else if (scenario == 1)
        second->height = 0.;
    else if (scenario == 2)
        second->slice_z = first->slice_z;
    else if (scenario == 3)
        second->print_z = std::numeric_limits<double>::quiet_NaN();
    else
        region->fills.append(planned_path(0., 2., 4., 2., -1.f));
    const json measured = support_projection(print).at("instances").at(0).at("layers").at(1);
    REQUIRE(measured.at("complete") == false);
    REQUIRE(measured.at("target_area_mm2").is_null());
    REQUIRE(measured.at("unmatched_target_area_mm2").is_null());
    REQUIRE(measured.at("support_levels").is_null());
    if (scenario == 3) {
        REQUIRE(measured.at("model_bottom_z_mm").is_null());
        REQUIRE(measured.at("model_print_z_mm").is_null());
    }
}

TEST_CASE("Slice analysis resolves current shared raft ownership per instance without reading backups", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 2);
    add_box(model, 1)->instances.front()->set_offset(Vec3d(80., 20., 0.));
    apply_model(print, model);
    REQUIRE(print.objects().size() == 2);
    PrintObject* object = print.get_object(0);
    PrintObject* owner  = print.get_object(1);
    REQUIRE(object->instances().size() == 2);
    Layer*              layer      = object->add_layer(0, 0.2, 1.2, 1.1);
    const ExtrusionPath model_path = planned_path(0., 2., 4., 2., 4.f);
    layer->lslices                 = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&object->printing_region(0))->fills.append(model_path);
    object->add_support_layer(0, 0, 0.2, 0.8)->raft_covered_by_shared_paths = true;
    SupportLayer* shared = owner->add_support_layer(0, 0, 0.2, 0.8);
    const Point   delta  = object->instances().at(0).shift - owner->instances().at(0).shift;
    const double  dx = unscale<double>(delta.x()), dy = unscale<double>(delta.y());
    const ExtrusionPath shared_path = planned_path(dx, dy + 2., dx + 4., dy + 2., 4.f, erSupportMaterialInterface);
    shared->support_fills.append(shared_path);
    shared->raft_merge_backup = std::make_unique<SupportLayer::RaftMergeBackup>();
    const Point backup_delta = object->instances().at(1).shift - owner->instances().at(0).shift;
    shared->raft_merge_backup->fills.append(planned_path(unscale<double>(backup_delta.x()), unscale<double>(backup_delta.y()) + 2.,
                                                       unscale<double>(backup_delta.x()) + 4., unscale<double>(backup_delta.y()) + 2.,
                                                       4.f, erSupportMaterialInterface));
    const ExPolygons       source   = layer->lslices;
    const SettingsSnapshot settings = all_settings(print);
    const json             before   = make_slice_analysis_report(print, InitializedResult{});
    const auto& projection = before.at("objects").at("groups").at(0).at("planned_support_projection");
    REQUIRE(projection.at("instances").size() == 2);
    for (size_t index = 0; index < 2; ++index) {
        const auto& instance = projection.at("instances").at(index);
        REQUIRE(instance.at("instance_index") == index);
        const auto& measured = instance.at("layers").at(0);
        REQUIRE(measured.at("complete") == true);
        REQUIRE(measured.at("target_area_mm2").get<double>() == Approx(16.));
        REQUIRE(measured.at("unmatched_target_area_mm2").get<double>() == Approx(index == 0 ? 0. : 16.));
        REQUIRE(measured.at("support_levels").size() == (index == 0 ? 1 : 0));
    }
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == before);
    REQUIRE(all_settings(print) == settings);
    REQUIRE(layer->lslices == source);
    const auto& model_after   = dynamic_cast<const ExtrusionPath&>(*layer->regions().front()->fills.entities.front());
    const auto& support_after = dynamic_cast<const ExtrusionPath&>(*shared->support_fills.entities.front());
    REQUIRE(model_after.polyline.points == model_path.polyline.points);
    REQUIRE(model_after.width == model_path.width);
    REQUIRE(support_after.polyline.points == shared_path.polyline.points);
    REQUIRE(support_after.width == shared_path.width);
    REQUIRE(shared->raft_merge_backup->fills.entities.size() == 1);
}

TEST_CASE("Slice analysis repeats shared support layer pointers at each instance's own position", "[SliceAnalysis]")
{
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 1);
    ModelObject* copy   = model.add_object(*source);
    copy->instances.front()->set_offset(Vec3d(60., 20., 0.));
    apply_model(print, model);
    REQUIRE(print.objects().size() == 2);
    PrintObject* owner  = print.get_object(0);
    PrintObject* shared = print.get_object(1);
    Layer*       layer  = owner->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices      = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&owner->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    owner->add_support_layer(0, 0, 0.2, 0.8)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    shared->set_shared_object(owner);
    shared->copy_layers_from_shared_object();
    REQUIRE(owner->support_layers().front() == shared->support_layers().front());
    const json original = support_projection(print, 0);
    REQUIRE(support_projection(print, 1) == original);
    const auto& measured = original.at("instances").at(0).at("layers").at(0);
    REQUIRE(measured.at("target_area_mm2").get<double>() == Approx(16.));
    REQUIRE(measured.at("unmatched_target_area_mm2").get<double>() == Approx(0.));
    REQUIRE(measured.at("support_levels").at(0).at("nearest_projection_area_mm2").get<double>() == Approx(16.));
}

TEST_CASE("Slice analysis observes intervening model paths without replacing sparse regions with solid sections", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    add_box(model, 1);
    apply_model(print, model);
    REQUIRE(print.objects().size() == 2);
    PrintObject* target = print.get_object(0);
    PrintObject* middle = print.get_object(1);
    Layer* layer = target->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&target->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    target->add_support_layer(0, 0, 0.2, 0.8)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    Layer* model_layer = middle->add_layer(0, 0.2, 1.0, 0.9);
    model_layer->lslices = {rectangle(0., 0., 4., 4.)};
    LayerRegion* region = model_layer->add_region(&middle->printing_region(0));
    double expected = 0.;
    SECTION("solid section without planned paths is zero") {}
    SECTION("one half-width planned stripe") {
        region->fills.append(planned_path(0., 1., 4., 1., 2.f));
        expected = 8.;
    }
    SECTION("repeated stripe across regions and layers is counted once") {
        region->fills.append(planned_path(0., 1., 4., 1., 2.f));
        model_layer->add_region(&middle->printing_region(0))->perimeters.append(planned_path(0., 1., 4., 1., 2.f));
        Layer* next = middle->add_layer(1, 0.1, 1.1, 1.05);
        next->lslices = model_layer->lslices;
        next->add_region(&middle->printing_region(0))->fills.append(planned_path(0., 1., 4., 1., 2.f));
        expected = 8.;
    }
    SECTION("complementary stripes across layers cover the full assigned area") {
        region->fills.append(planned_path(0., 1., 4., 1., 2.f));
        Layer* next = middle->add_layer(1, 0.1, 1.1, 1.05);
        next->lslices = model_layer->lslices;
        next->add_region(&middle->printing_region(0))->fills.append(planned_path(0., 3., 4., 3., 2.f));
        expected = 16.;
    }
    SECTION("planned footprint holes remain empty") {
        region->fills.append(planned_path(0., 0.5, 4., 0.5, 1.f));
        region->fills.append(planned_path(0., 3.5, 4., 3.5, 1.f));
        region->fills.append(planned_path(0.5, 1., 0.5, 3., 1.f));
        region->fills.append(planned_path(3.5, 1., 3.5, 3., 1.f));
        expected = 12.;
    }
    SECTION("zero-flow and travel connectors are excluded") {
        ExtrusionPath zero = planned_path(0., 2., 4., 2., 4.f);
        zero.mm3_per_mm = 0.;
        region->fills.append(zero);
        ExtrusionPath travel = planned_path(0., 2., 4., 2., 4.f);
        travel.set_force_no_extrusion(true);
        region->fills.append(travel);
    }
    SECTION("footprints elsewhere do not intersect") { region->fills.append(planned_path(10., 2., 14., 2., 4.f)); }
    SECTION("boundary-only contact has zero area") { region->fills.append(planned_path(4., 2., 8., 2., 4.f)); }
    const json measured = support_projection(print).at("instances").at(0).at("layers").at(0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("target_area_mm2").get<double>() == Approx(16.));
    REQUIRE(measured.at("unmatched_target_area_mm2").get<double>() == Approx(0.));
    REQUIRE(measured.at("support_levels").size() == 1);
    const auto& level = measured.at("support_levels").at(0);
    REQUIRE(level.at("nearest_projection_area_mm2").get<double>() == Approx(16.));
    REQUIRE(level.at("intervening_model_projection").at("complete") == true);
    REQUIRE(level.at("intervening_model_projection").at("area_mm2").get<double>() == Approx(expected));
}

TEST_CASE("Slice analysis uses strict print-plane windows for intervening model projections", "[SliceAnalysis]")
{
    const double model_z = GENERATE(0.6, 0.8, 1.0, 1.2, 1.4);
    Model model;
    Print print;
    add_box(model, 1);
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* target = print.get_object(0);
    PrintObject* middle = print.get_object(1);
    Layer* layer = target->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&target->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    target->add_support_layer(0, 0, 0.2, 0.8)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    Layer* model_layer = middle->add_layer(0, 0.2, model_z, model_z - 0.1);
    model_layer->lslices = {rectangle(0., 0., 4., 4.)};
    ExtrusionPath path = planned_path(0., 1., 4., 1., 2.f);
    path.height = 0.7f; // A thick path crossing the window is still not a print plane inside it.
    model_layer->add_region(&middle->printing_region(0))->fills.append(path);
    const json level = support_projection(print).at("instances").at(0).at("layers").at(0).at("support_levels").at(0);
    REQUIRE(level.at("nearest_projection_area_mm2").get<double>() == Approx(16.));
    REQUIRE(level.at("intervening_model_projection").at("complete") == true);
    REQUIRE(level.at("intervening_model_projection").at("area_mm2").get<double>() == Approx(model_z == 1.0 ? 8. : 0.));
}

TEST_CASE("Slice analysis keeps original support matching with zero negative and equal-plane gaps", "[SliceAnalysis]")
{
    const double support_z = GENERATE(1.0, 1.1, 1.2);
    Model model;
    Print print;
    add_box(model, 1);
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* target = print.get_object(0);
    PrintObject* middle = print.get_object(1);
    Layer* layer = target->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&target->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    target->add_support_layer(0, 0, 0.2, support_z)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    Layer* model_layer = middle->add_layer(0, 0.2, 1.15, 1.05);
    model_layer->lslices = {rectangle(0., 0., 4., 4.)};
    model_layer->add_region(&middle->printing_region(0))->fills.append(planned_path(0., 1., 4., 1., 2.f));
    const json measured = support_projection(print).at("instances").at(0).at("layers").at(0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("unmatched_target_area_mm2").get<double>() == Approx(0.));
    const auto& level = measured.at("support_levels").at(0);
    REQUIRE(level.at("nearest_projection_area_mm2").get<double>() == Approx(16.));
    REQUIRE(level.at("model_layer_bottom_gap_mm").get<double>() == Approx(1. - support_z).margin(1e-12));
    const auto& nested = level.at("intervening_model_projection");
    REQUIRE(nested.at("complete") == (support_z < 1.2));
    if (support_z < 1.2)
        REQUIRE(nested.at("area_mm2").get<double>() == Approx(8.));
    else
        REQUIRE(nested.at("area_mm2").is_null());
}

TEST_CASE("Slice analysis includes earlier paths from the same object beyond its retained section", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* previous = object->add_layer(0, 0.2, 0.8, 0.7);
    previous->lslices = {rectangle(0., 0., 2., 4.)};
    previous->add_region(&object->printing_region(0))->fills.append(planned_path(0., 2., 2.25, 2., 4.f));
    Layer* current = object->add_layer(1, 0.2, 1.2, 1.1);
    current->lslices = {rectangle(0., 0., 4., 4.)};
    current->add_region(&object->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    object->add_support_layer(0, 0, 0.2, 0.6)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    const json measured = support_projection(print).at("instances").at(0).at("layers").at(1);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("target_area_mm2").get<double>() == Approx(8.));
    const auto& level = measured.at("support_levels").at(0);
    REQUIRE(level.at("nearest_projection_area_mm2").get<double>() == Approx(8.));
    REQUIRE(level.at("intervening_model_projection").at("complete") == true);
    REQUIRE(level.at("intervening_model_projection").at("area_mm2").get<double>() == Approx(1.));
}

TEST_CASE("Slice analysis confines invalid model projection data to its applicable Z windows", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* target = print.get_object(0);
    PrintObject* middle = print.get_object(1);
    Layer* layer = target->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&target->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    target->add_support_layer(0, 0, 0.2, 0.8)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    Layer* earlier = middle->add_layer(0, 0.1, 0.9, 0.85);
    earlier->lslices = {rectangle(0., 0., 4., 4.)};
    Layer* invalid = middle->add_layer(1, 0.1, 1.0, 0.95);
    invalid->lslices = earlier->lslices;
    LayerRegion* region = invalid->add_region(&middle->printing_region(0));
    bool expected_complete = false;
    SECTION("invalid path in window") { region->fills.append(planned_path(0., 2., 4., 2., -1.f)); }
    SECTION("invalid geometry in window") { invalid->slicing_errors = true; }
    SECTION("invalid height in window") { invalid->height = 0.; }
    SECTION("unavailable model Z cannot be placed outside the window") {
        invalid->print_z = std::numeric_limits<double>::quiet_NaN();
    }
    SECTION("infinite model Z is not assumed outside the window") {
        invalid->print_z = std::numeric_limits<double>::infinity();
    }
    SECTION("invalid path above the window leaves this observation complete") {
        invalid->print_z = 1.4;
        region->fills.append(planned_path(0., 2., 4., 2., -1.f));
        expected_complete = true;
    }
    SECTION("invalid path on the support boundary is excluded") {
        invalid->print_z = 0.8;
        region->fills.append(planned_path(0., 2., 4., 2., -1.f));
        expected_complete = true;
    }
    SECTION("invalid geometry on the target boundary is excluded") {
        invalid->print_z = 1.2;
        invalid->slicing_errors = true;
        expected_complete = true;
    }
    SECTION("full coverage does not conceal an invalid later layer in the window") {
        earlier->add_region(&middle->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
        region->fills.append(planned_path(0., 2., 4., 2., -1.f));
    }
    const json measured = support_projection(print).at("instances").at(0).at("layers").at(0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("target_area_mm2").get<double>() == Approx(16.));
    REQUIRE(measured.at("unmatched_target_area_mm2").get<double>() == Approx(0.));
    const auto& level = measured.at("support_levels").at(0);
    REQUIRE(level.at("nearest_projection_area_mm2").get<double>() == Approx(16.));
    const auto& nested = level.at("intervening_model_projection");
    REQUIRE(nested.at("complete") == expected_complete);
    if (expected_complete)
        REQUIRE(nested.at("area_mm2").get<double>() == Approx(0.));
    else
        REQUIRE(nested.at("area_mm2").is_null());
    REQUIRE(json::parse(measured.dump()) == measured);
}

TEST_CASE("Slice analysis evaluates intervening model completeness independently for each assigned support level", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* target = print.get_object(0);
    PrintObject* middle = print.get_object(1);
    Layer* layer = target->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&target->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    target->add_support_layer(0, 0, 0.2, 0.8)->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    target->add_support_layer(1, 0, 0.2, 1.0)->support_fills.append(planned_path(0., 3., 4., 3., 2.f, erSupportMaterial));
    Layer* invalid = middle->add_layer(0, 0.2, 0.9, 0.8);
    invalid->lslices = {rectangle(0., 0., 4., 4.)};
    invalid->add_region(&middle->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., -1.f));
    Layer* valid = middle->add_layer(1, 0.2, 1.1, 1.0);
    valid->lslices = invalid->lslices;
    valid->add_region(&middle->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    const json measured = support_projection(print).at("instances").at(0).at("layers").at(0);
    REQUIRE(measured.at("complete") == true);
    REQUIRE(measured.at("unmatched_target_area_mm2").get<double>() == Approx(0.));
    const auto& levels = measured.at("support_levels");
    REQUIRE(levels.size() == 2);
    REQUIRE(levels.at(0).at("support_print_z_mm").get<double>() == Approx(1.));
    REQUIRE(levels.at(1).at("support_print_z_mm").get<double>() == Approx(0.8));
    for (const auto& assigned : levels)
        REQUIRE(assigned.at("nearest_projection_area_mm2").get<double>() == Approx(8.));
    REQUIRE(levels.at(0).at("intervening_model_projection").at("complete") == true);
    REQUIRE(levels.at(0).at("intervening_model_projection").at("area_mm2").get<double>() == Approx(8.));
    REQUIRE(levels.at(1).at("intervening_model_projection").at("complete") == false);
    REQUIRE(levels.at(1).at("intervening_model_projection").at("area_mm2").is_null());
}

TEST_CASE("Slice analysis projects cached shared model paths at every instance without changing print data", "[SliceAnalysis]")
{
    const PrintSequence sequence = GENERATE(PrintSequence::ByLayer, PrintSequence::ByObject);
    Model model;
    Print print;
    add_box(model, 2);
    ModelObject* owner_source = add_box(model, 1);
    ModelObject* copy_source = model.add_object(*owner_source);
    copy_source->instances.front()->set_offset(Vec3d(40., 20., 0.));
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(sequence));
    for (ModelObject* object : model.objects)
        print.auto_assign_extruders(object);
    print.set_status_silent();
    print.apply(model, config);
    REQUIRE(print.objects().size() == 3);
    PrintObject* target = print.get_object(0);
    PrintObject* owner = print.get_object(1);
    PrintObject* shared = print.get_object(2);
    REQUIRE(target->instances().size() == 2);
    Layer* layer = target->add_layer(0, 0.2, 1.2, 1.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    LayerRegion* target_region = layer->add_region(&target->printing_region(0));
    target_region->fills.append(planned_path(0., 2., 4., 2., 4.f));
    SupportLayer* support = target->add_support_layer(0, 0, 0.2, 0.8);
    support->support_fills.append(planned_path(0., 2., 4., 2., 4.f, erSupportMaterial));
    Layer* model_layer = owner->add_layer(0, 0.2, 1.0, 0.9);
    model_layer->lslices = layer->lslices;
    LayerRegion* model_region = model_layer->add_region(&owner->printing_region(0));
    model_region->fills.append(planned_path(0., 1., 4., 1., 2.f));
    shared->set_shared_object(owner);
    shared->copy_layers_from_shared_object();
    REQUIRE(owner->layers().front() == shared->layers().front());
    const auto settings = all_settings(print);
    const Polylines model_paths = model_region->fills.as_polylines();
    const Polylines target_paths = target_region->fills.as_polylines();
    const Polylines support_paths = support->support_fills.as_polylines();
    const Polygons model_footprint = model_region->fills.polygons_covered_by_width();
    const ExPolygons model_section = model_layer->lslices;
    const json report = make_slice_analysis_report(print, InitializedResult{});
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    const auto& instances = report.at("objects").at("groups").at(0).at("planned_support_projection").at("instances");
    REQUIRE(instances.size() == 2);
    for (const auto& instance : instances) {
        const auto& measured = instance.at("layers").at(0);
        REQUIRE(measured.at("complete") == true);
        REQUIRE(measured.at("target_area_mm2").get<double>() == Approx(16.));
        REQUIRE(measured.at("unmatched_target_area_mm2").get<double>() == Approx(0.));
        const auto& level = measured.at("support_levels").at(0);
        REQUIRE(level.at("nearest_projection_area_mm2").get<double>() == Approx(16.));
        REQUIRE(level.at("intervening_model_projection").at("complete") == true);
        REQUIRE(level.at("intervening_model_projection").at("area_mm2").get<double>() == Approx(8.));
    }
    REQUIRE(all_settings(print) == settings);
    REQUIRE(model_region->fills.as_polylines() == model_paths);
    REQUIRE(target_region->fills.as_polylines() == target_paths);
    REQUIRE(support->support_fills.as_polylines() == support_paths);
    REQUIRE(model_region->fills.polygons_covered_by_width() == model_footprint);
    REQUIRE(model_layer->lslices == model_section);
}

TEST_CASE("Slice analysis observes both objects after real shared raft generation without changing paths", "[SliceAnalysis][Raft]")
{
    // Same small, overlapping CuraV1 raft recipe as the support merger tests.
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {100., 0.}, {100., 100.}, {0., 100.}}));
    config.set_key_value("bed_exclude_area", new ConfigOptionPoints());
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stNormalAuto));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsGrid));
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btNoBrim));
    config.set("skirt_loops", 0);
    config.set("enable_support", false);
    config.set("before_layer_change_gcode", std::string("G92 E0"));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.3);
    config.set("raft_layers", 3);
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 1);
    config.set("raft_surface_layers", 1);
    config.set("raft_base_margin", 3.);
    config.set("raft_interface_margin", 2.5);
    config.set("raft_surface_margin", 2.);
    config.set("raft_base_line_width", 1.);
    config.set("raft_base_line_spacing", 2.);
    config.set("raft_interface_line_width", 0.6);
    config.set("raft_interface_line_spacing", 0.8);
    config.set("raft_surface_line_width", 0.5);
    config.set("raft_surface_line_spacing", 0.4);
    config.set("raft_first_layer_expansion", 3.);
    config.set("raft_expansion", 2.);

    Model model;
    Print print;
    for (const double x : {30., 41.}) {
        ModelObject* object = model.add_object();
        object->name        = "shared raft analysis cube";
        object->add_volume(TriangleMesh(its_make_cube(8., 8., 0.8)), ModelVolumeType::MODEL_PART, false);
        object->add_instance()->set_offset(Vec3d(x, 40., 0.));
        object->ensure_on_bed();
        print.auto_assign_extruders(object);
    }
    print.set_status_silent();
    print.apply(model, config);
    const StringObjectException validation = print.validate();
    INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
    REQUIRE(validation.string.empty());
    print.process();
    REQUIRE(print.objects().size() == 2);
    for (size_t index = 0; index < 3; ++index) {
        size_t owners = 0;
        for (const PrintObject* object : print.objects()) {
            REQUIRE(object->support_layers().size() > index);
            const SupportLayer* layer = object->support_layers()[index];
            REQUIRE(layer->raft_merge_backup != nullptr);
            if (!layer->support_fills.empty())
                ++owners;
            else
                REQUIRE(layer->raft_covered_by_shared_paths);
        }
        REQUIRE(owners == 1);
    }

    const auto paths_snapshot = [&print]() {
        std::vector<std::tuple<Polylines, Polygons, double>> result;
        const auto record = [&result](const ExtrusionEntityCollection& paths) {
            result.emplace_back(paths.as_polylines(), paths.polygons_covered_by_width(), paths.total_volume());
        };
        for (const PrintObject* object : print.objects()) {
            for (const Layer* layer : object->layers())
                for (const LayerRegion* region : layer->regions()) {
                    record(region->perimeters);
                    record(region->fills);
                }
            for (const SupportLayer* layer : object->support_layers()) {
                record(layer->support_fills);
                if (layer->raft_merge_backup)
                    record(layer->raft_merge_backup->fills);
            }
        }
        return result;
    };
    const auto settings = all_settings(print);
    const auto paths    = paths_snapshot();
    const json report   = make_slice_analysis_report(print, InitializedResult{});
    for (const auto& group : report.at("objects").at("groups")) {
        const auto& first = group.at("planned_support_projection").at("instances").at(0).at("layers").at(0);
        REQUIRE(first.at("complete") == true);
        REQUIRE(first.at("target_area_mm2").get<double>() > 0.);
        REQUIRE(first.at("unmatched_target_area_mm2").get<double>() < first.at("target_area_mm2").get<double>());
        REQUIRE_FALSE(first.at("support_levels").empty());
        for (const auto& level : first.at("support_levels")) {
            REQUIRE(level.at("nearest_projection_area_mm2").get<double>() > 0.);
            REQUIRE(level.at("intervening_model_projection").at("complete") == true);
            REQUIRE(level.at("intervening_model_projection").at("area_mm2").get<double>() == Approx(0.));
        }
    }
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(all_settings(print) == settings);
    REQUIRE(paths_snapshot() == paths);
    const auto& bed = report.at("planned_plate_bed_footprint");
    REQUIRE(bed.at("complete") == true);
    REQUIRE(bed.at("model_area_mm2") == 0.);
    REQUIRE(bed.at("brim_area_mm2") == 0.);
    REQUIRE(bed.at("support_and_raft_area_mm2").get<double>() > 0.);
    REQUIRE(bed.at("combined_area_mm2") == bed.at("support_and_raft_area_mm2"));
}

TEST_CASE("Slice analysis unions nominal plate bed categories without double counting their overlap", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fills.append(planned_path(0., 2., 4., 2., 4.f));
    SupportLayer* support = object->add_support_layer(0, 0, 0.2, 0.2);
    support->support_fills.append(planned_path(2., 2., 6., 2., 4.f, erSupportMaterial));
    ExtrusionPath brim = planned_path(4., 2., 8., 2., 4.f, erBrim);
    brim.polyline.translate(object->instances().front().shift);
    print.get_brimMap()[object->id()].append(brim);
    const auto settings = all_settings(print);
    const auto model_paths = region->fills.as_polylines();
    const auto support_paths = support->support_fills.as_polylines();
    const auto brim_paths = print.get_brimMap().at(object->id()).as_polylines();
    const json report = make_slice_analysis_report(print, InitializedResult{});
    const auto& bed = report.at("planned_plate_bed_footprint");
    REQUIRE(bed.at("scope") == "planned_path_nominal_bottom_on_bed_before_gcode");
    REQUIRE(bed.at("complete") == true);
    REQUIRE(bed.at("model_area_mm2").get<double>() == Approx(16.));
    REQUIRE(bed.at("support_and_raft_area_mm2").get<double>() == Approx(16.));
    REQUIRE(bed.at("brim_area_mm2").get<double>() == Approx(16.));
    REQUIRE(bed.at("combined_area_mm2").get<double>() == Approx(32.));
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(all_settings(print) == settings);
    REQUIRE(region->fills.as_polylines() == model_paths);
    REQUIRE(support->support_fills.as_polylines() == support_paths);
    REQUIRE(print.get_brimMap().at(object->id()).as_polylines() == brim_paths);
}

TEST_CASE("Slice analysis places model copies but does not place already positioned brim copies again", "[SliceAnalysis]")
{
    const bool coincident = GENERATE(false, true);
    const bool offset_plate = GENERATE(false, true);
    Model model;
    Print print;
    ModelObject* source = add_box(model, 2);
    if (coincident)
        source->instances.back()->set_offset(source->instances.front()->get_offset());
    if (offset_plate)
        print.set_plate_origin(Vec3d(300., 0., 0.));
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    REQUIRE(object->instances().size() == 2);
    Layer* layer = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&object->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    for (const PrintInstance& instance : object->instances()) {
        ExtrusionPath path = planned_path(4., 2., 8., 2., 4.f, erBrim);
        path.polyline.translate(instance.shift);
        print.get_brimMap()[object->id()].append(path);
    }
    const json bed = make_slice_analysis_report(print, InitializedResult{}).at("planned_plate_bed_footprint");
    const double multiplier = coincident ? 1. : 2.;
    REQUIRE(bed.at("complete") == true);
    REQUIRE(bed.at("model_area_mm2").get<double>() == Approx(16. * multiplier));
    REQUIRE(bed.at("brim_area_mm2").get<double>() == Approx(16. * multiplier));
    REQUIRE(bed.at("support_and_raft_area_mm2") == 0.);
    REQUIRE(bed.at("combined_area_mm2").get<double>() == Approx(32. * multiplier));
}

TEST_CASE("Slice analysis uses each path height and each brim owner's print plane for bed footprints", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* model_layer = object->add_layer(0, 0.2, 0.6, 0.1);
    model_layer->lslices = {rectangle(0., 0., 4., 4.)};
    model_layer->add_region(&object->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    SupportLayer* support = object->add_support_layer(0, 0, 0.1, 0.3);
    ExtrusionPath tall = planned_path(0., 2., 4., 2., 4.f, erSupportMaterial);
    tall.height = 0.3f;
    support->support_fills.append(tall);
    ExtrusionPath short_path = planned_path(10., 2., 14., 2., 4.f, erSupportMaterial);
    short_path.height = 0.1f;
    support->support_fills.append(short_path);
    ExtrusionPath brim = planned_path(4., 2., 8., 2., 4.f, erBrim);
    brim.height = 0.3f;
    brim.polyline.translate(object->instances().front().shift);
    // Fixture setup only: the production API exposes support brims read-only.
    auto& support_brims = const_cast<std::map<ObjectID, ExtrusionEntityCollection>&>(print.get_supportBrimMap());
    support_brims[object->id()].append(brim);
    print.get_brimMap()[object->id()].append(brim); // Same height is not on the model's 0.6-mm print plane.
    const auto before = support_brims.at(object->id()).as_polylines();
    const json report = make_slice_analysis_report(print, InitializedResult{});
    const auto& bed = report.at("planned_plate_bed_footprint");
    REQUIRE(bed.at("complete") == true);
    REQUIRE(bed.at("model_area_mm2") == 0.);
    REQUIRE(bed.at("support_and_raft_area_mm2").get<double>() == Approx(16.));
    REQUIRE(bed.at("brim_area_mm2").get<double>() == Approx(16.));
    REQUIRE(bed.at("combined_area_mm2").get<double>() == Approx(32.));
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(support_brims.at(object->id()).as_polylines() == before);
}

TEST_CASE("Slice analysis counts different first-layer heights and shared layer instances on the bed", "[SliceAnalysis]")
{
    const bool share_layers = GENERATE(false, true);
    Model model;
    Print print;
    ModelObject* source = add_box(model, 1);
    ModelObject* copy = model.add_object(*source);
    copy->instances.front()->set_offset(Vec3d(40., 20., 0.));
    apply_model(print, model);
    REQUIRE(print.objects().size() == 2);
    PrintObject* first = print.get_object(0);
    PrintObject* second = print.get_object(1);
    Layer* layer = first->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    layer->add_region(&first->printing_region(0))->fills.append(planned_path(0., 2., 4., 2., 4.f));
    if (share_layers) {
        second->set_shared_object(first);
        second->copy_layers_from_shared_object();
        REQUIRE(first->layers().front() == second->layers().front());
    } else {
        Layer* other = second->add_layer(0, 0.4, 0.4, 0.2);
        other->lslices = layer->lslices;
        ExtrusionPath path = planned_path(0., 2., 4., 2., 4.f);
        path.height = 0.4f;
        other->add_region(&second->printing_region(0))->fills.append(path);
    }
    const json bed = make_slice_analysis_report(print, InitializedResult{}).at("planned_plate_bed_footprint");
    REQUIRE(bed.at("complete") == true);
    REQUIRE(bed.at("model_area_mm2").get<double>() == Approx(32.));
    REQUIRE(bed.at("combined_area_mm2").get<double>() == Approx(32.));
}

TEST_CASE("Slice analysis excludes zero-flow, non-extruding and auxiliary paths from nominal bed area", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    ExtrusionPath zero_flow = planned_path(0., 2., 4., 2., 4.f);
    zero_flow.mm3_per_mm = 0.;
    zero_flow.height = std::numeric_limits<float>::quiet_NaN();
    region->fills.append(zero_flow);
    ExtrusionPath no_extrusion = planned_path(0., 2., 4., 2., 4.f);
    no_extrusion.set_force_no_extrusion(true);
    region->fills.append(no_extrusion);
    for (ExtrusionRole role : {erSkirt, erWipeTower, erCustom})
        region->fills.append(planned_path(0., 2., 4., 2., 4.f, role));
    const json bed = make_slice_analysis_report(print, InitializedResult{}).at("planned_plate_bed_footprint");
    REQUIRE(bed.at("complete") == true);
    for (const char* key : {"model_area_mm2", "support_and_raft_area_mm2", "brim_area_mm2", "combined_area_mm2"})
        REQUIRE(bed.at(key) == 0.);
    const json empty = make_slice_analysis_report(Print{}, InitializedResult{}).at("planned_plate_bed_footprint");
    REQUIRE(empty.at("complete") == true);
    REQUIRE(empty.at("combined_area_mm2") == 0.);
}

TEST_CASE("Slice analysis leaves unavailable bed footprints unknown instead of reporting partial areas", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 4., 4.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    ExtrusionPath path = planned_path(0., 2., 4., 2., 4.f);
    SECTION("invalid path height") { path.height = std::numeric_limits<float>::quiet_NaN(); }
    SECTION("zero path height") { path.height = 0.f; }
    SECTION("invalid bed-path width") { path.width = -1.f; }
    SECTION("invalid bed-path flow") { path.mm3_per_mm = std::numeric_limits<double>::infinity(); }
    SECTION("invalid model print plane") { layer->print_z = std::numeric_limits<double>::quiet_NaN(); }
    SECTION("slicing error") { layer->slicing_errors = true; }
    SECTION("orphaned brim owner") { print.get_brimMap()[model.add_object()->id()].append(path); }
    SECTION("invalid support print plane") {
        object->add_support_layer(0, 0, 0.2, std::numeric_limits<double>::quiet_NaN())->support_fills.append(path);
    }
    region->fills.append(path);
    const json bed = make_slice_analysis_report(print, InitializedResult{}).at("planned_plate_bed_footprint");
    REQUIRE(bed.at("complete") == false);
    for (const char* key : {"model_area_mm2", "support_and_raft_area_mm2", "brim_area_mm2", "combined_area_mm2"})
        REQUIRE(bed.at(key).is_null());
    REQUIRE(json::parse(bed.dump()) == bed);
}

TEST_CASE("Slice analysis retains real generated brim bed area without altering the paths", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {100., 0.}, {100., 100.}, {0., 100.}}));
    config.set_key_value("bed_exclude_area", new ConfigOptionPoints());
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    config.set("brim_width", 3.);
    config.set("brim_object_gap", 0.1);
    config.set("enable_support", false);
    config.set("skirt_loops", 0);
    config.set("before_layer_change_gcode", std::string("G92 E0"));
    config.set("initial_layer_print_height", 0.3);
    print.set_status_silent();
    print.auto_assign_extruders(model.objects.front());
    print.apply(model, config);
    const auto validation = print.validate();
    INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
    REQUIRE(validation.string.empty());
    print.process();
    const PrintObject* object = print.objects().front();
    const auto& brims = std::as_const(print).get_brimMap();
    REQUIRE(brims.count(object->id()) == 1);
    REQUIRE_FALSE(brims.at(object->id()).empty());
    const auto paths = brims.at(object->id()).as_polylines();
    const auto settings = all_settings(print);
    const json report = make_slice_analysis_report(print, InitializedResult{});
    const auto& bed = report.at("planned_plate_bed_footprint");
    REQUIRE(bed.at("complete") == true);
    REQUIRE(bed.at("model_area_mm2").get<double>() > 0.);
    REQUIRE(bed.at("brim_area_mm2").get<double>() > 0.);
    REQUIRE(bed.at("support_and_raft_area_mm2") == 0.);
    REQUIRE(bed.at("combined_area_mm2").get<double>() >= bed.at("model_area_mm2").get<double>());
    REQUIRE(bed.at("combined_area_mm2").get<double>() <=
            bed.at("model_area_mm2").get<double>() + bed.at("brim_area_mm2").get<double>());
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(brims.at(object->id()).as_polylines() == paths);
    REQUIRE(all_settings(print) == settings);
}

TEST_CASE("Slice analysis measures whole perimeter loops once per instance without changing paths", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 4., 3.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->perimeters.append(planned_loop(layer->lslices.front().contour.split_at_first_point().points));
    const Polylines paths = region->perimeters.as_polylines();
    const auto settings = all_settings(print);
    const json report = make_slice_analysis_report(print, InitializedResult{});
    const json& loops = report.at("objects").at("groups").at(0).at("planned_perimeter_loops");
    REQUIRE(loops.at("scope") == "per_instance_closed_model_perimeter_centerlines_before_gcode");
    REQUIRE(loops.at("complete") == true);
    REQUIRE(loops.at("loop_count") == 1);
    REQUIRE(loops.at("loop_length_mm").at("min").get<double>() == Approx(14.));
    REQUIRE(loops.at("loop_length_mm").at("max").get<double>() == Approx(14.));
    REQUIRE(loops.at("turn_angle_degrees").at("min").get<double>() == Approx(90.));
    REQUIRE(loops.at("turn_angle_degrees").at("max").get<double>() == Approx(90.));
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(region->perimeters.as_polylines() == paths);
    REQUIRE(all_settings(print) == settings);
}

TEST_CASE("Slice analysis joins only fragments of the same closed perimeter for turns", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 4., 3.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    ExtrusionLoop loop;
    ExtrusionPath first = planned_path(0., 0., 4., 3.);
    first.polyline = Polyline({Point(0, 0), Point(scaled(4.), 0), Point(scaled(4.), scaled(3.))});
    ExtrusionPath second = planned_path(4., 3., 0., 0., 0.4f, erOverhangPerimeter);
    second.polyline = Polyline({Point(scaled(4.), scaled(3.)), Point(0, scaled(3.)), Point(0, 0)});
    loop.paths = {first, second};
    double minimum_turn = 90.;
    SECTION("ordinary and overhang fragments remain one loop") {}
    SECTION("reversing all fragments preserves length and turn range") { loop.reverse(); }
    SECTION("duplicate junctions and consecutive points do not add turns") {
        loop.paths.front().polyline.points.insert(loop.paths.front().polyline.points.begin(), Point(0, 0));
        loop.paths.back().polyline.points.push_back(Point(0, 0));
    }
    SECTION("a straight intermediate vertex has zero directional turn") {
        loop.paths.front().polyline.points.insert(loop.paths.front().polyline.points.begin() + 1, Point(scaled(2.), 0));
        minimum_turn = 0.;
    }
    region->perimeters.append(loop);
    const json loops = perimeter_loops(print);
    REQUIRE(loops.at("complete") == true);
    REQUIRE(loops.at("loop_count") == 1);
    REQUIRE(loops.at("loop_length_mm").at("min").get<double>() == Approx(14.));
    REQUIRE(loops.at("turn_angle_degrees").at("min").get<double>() == Approx(minimum_turn));
    REQUIRE(loops.at("turn_angle_degrees").at("max").get<double>() == Approx(90.));
}

TEST_CASE("Slice analysis aggregates closed loop lengths and non-interior turn angles across model layers", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    for (size_t index = 0; index < 2; ++index) {
        Layer* layer = object->add_layer(int(index), 0.2, 0.2 * (index + 1), 0.1 + 0.2 * index);
        layer->lslices = {rectangle(0., 0., 4., 4.)};
        LayerRegion* region = layer->add_region(&object->printing_region(0));
        auto* nested = new ExtrusionEntityCollection;
        if (index == 0)
            nested->append(planned_loop(rectangle(0., 0., 1., 1.).contour.split_at_first_point().points));
        else
            nested->append(planned_loop({Point(0, 0), Point(scaled(4.), 0), Point(0, scaled(4.)), Point(0, 0)}));
        region->perimeters.entities.push_back(nested);
    }
    const json loops = perimeter_loops(print);
    REQUIRE(loops.at("complete") == true);
    REQUIRE(loops.at("loop_count") == 2);
    REQUIRE(loops.at("loop_length_mm").at("min").get<double>() == Approx(4.));
    REQUIRE(loops.at("loop_length_mm").at("max").get<double>() == Approx(8. + 4. * std::sqrt(2.)));
    REQUIRE(loops.at("turn_angle_degrees").at("min").get<double>() == Approx(90.));
    REQUIRE(loops.at("turn_angle_degrees").at("max").get<double>() == Approx(135.));
}

TEST_CASE("Slice analysis excludes open paths and non-model-wall loops from closed perimeter observations", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 4., 3.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    const Points points = layer->lslices.front().contour.split_at_first_point().points;
    const ExtrusionLoop wall = planned_loop(points);
    SECTION("valid layer without any paths") {}
    SECTION("non-loop and excluded-role collections") {
        region->perimeters.append(planned_path(0., 0., 4., 0.));
        region->perimeters.append(wall.paths.front()); // Closed coordinates alone do not establish an ExtrusionLoop.
        auto* multipath = new ExtrusionMultiPath;
        multipath->paths = wall.paths;
        region->perimeters.entities.push_back(multipath);
        for (const auto role : {erGapFill, erBrim, erSupportMaterial})
            region->perimeters.append(planned_loop(points, role));
        ExtrusionLoop zero_flow = wall;
        zero_flow.paths.front().mm3_per_mm = 0.;
        region->perimeters.append(zero_flow);
        ExtrusionLoop no_extrusion = wall;
        no_extrusion.paths.front().set_force_no_extrusion(true);
        region->perimeters.append(no_extrusion);
        region->fills.append(wall);
        object->add_support_layer(0, 0, 0.2, 0.2)->support_fills.append(wall);
        print.get_brimMap()[object->id()].append(wall);
    }
    const json loops = perimeter_loops(print);
    REQUIRE(loops.at("complete") == true);
    REQUIRE(loops.at("loop_count") == 0);
    for (const char* key : {"loop_length_mm", "turn_angle_degrees"}) {
        REQUIRE(loops.at(key).at("min").is_null());
        REQUIRE(loops.at(key).at("max").is_null());
    }
}

TEST_CASE("Slice analysis keeps malformed closed perimeters unknown without invalidating existing footprint reports", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {rectangle(0., 0., 4., 3.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    ExtrusionLoop loop = planned_loop(layer->lslices.front().contour.split_at_first_point().points);
    bool old_complete = true;
    SECTION("open loop") { loop.paths.front().polyline.points.pop_back(); }
    SECTION("disconnected fragments") { loop.paths.push_back(planned_path(10., 10., 11., 10.)); }
    SECTION("collinear closed loop") {
        loop = planned_loop({Point(0, 0), Point(scaled(1.), 0), Point(scaled(2.), 0), Point(0, 0)});
    }
    SECTION("one distinct point") { loop = planned_loop({Point(0, 0), Point(0, 0)}); }
    SECTION("empty loop") { loop.paths.clear(); }
    SECTION("invalid positive-flow width") {
        loop.paths.front().width = std::numeric_limits<float>::quiet_NaN();
        old_complete = false;
    }
    SECTION("invalid model layer") {
        layer->slicing_errors = true;
        old_complete = false;
    }
    region->perimeters.append(loop);
    const json report = make_slice_analysis_report(print, InitializedResult{});
    const json& group = report.at("objects").at("groups").at(0);
    const json& loops = group.at("planned_perimeter_loops");
    REQUIRE(loops.at("complete") == false);
    REQUIRE(loops.at("loop_count").is_null());
    for (const char* key : {"loop_length_mm", "turn_angle_degrees"}) {
        REQUIRE(loops.at(key).at("min").is_null());
        REQUIRE(loops.at(key).at("max").is_null());
    }
    REQUIRE(group.at("planned_layer_coverage").at("layers").at(0).at("complete") == old_complete);
    REQUIRE(json::parse(report.dump()) == report);
}

TEST_CASE("Slice analysis preserves effective perimeter motion settings as configuration rather than actual motion", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("default_acceleration", 0.);
    config.set("outer_wall_acceleration", 400.);
    config.set("inner_wall_acceleration", 800.);
    config.set("outer_wall_speed", 30.);
    config.set("inner_wall_speed", 50.);
    config.set("small_perimeter_threshold", 2.);
    config.set_key_value("bridge_acceleration", new ConfigOptionFloatOrPercent(75., true));
    config.set_key_value("small_perimeter_speed", new ConfigOptionFloatOrPercent(40., true));
    print.set_status_silent();
    print.auto_assign_extruders(model.objects.front());
    print.apply(model, config);
    const json group = make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0);
    REQUIRE(group.at("planned_perimeter_loops").at("complete") == false);
    REQUIRE(group.at("planned_perimeter_loops").at("loop_count").is_null());
    REQUIRE(group.at("solid_fill_target_depth").at("complete") == false);
    REQUIRE(group.at("solid_fill_target_depth").at("top").at("target_area_sum_mm2").is_null());
    const json& settings = group.at("configured_settings");
    const auto& object_settings = settings.at("object");
    REQUIRE(object_settings.at("default_acceleration") == "0");
    REQUIRE(object_settings.at("outer_wall_acceleration") == "400");
    REQUIRE(object_settings.at("inner_wall_acceleration") == "800");
    REQUIRE(object_settings.at("bridge_acceleration") == "75%");
    const json& region_settings = settings.at("regions").at(0).at("settings");
    REQUIRE(region_settings.at("outer_wall_speed") == "30");
    REQUIRE(region_settings.at("inner_wall_speed") == "50");
    REQUIRE(region_settings.at("small_perimeter_speed") == "40%");
    REQUIRE(region_settings.at("small_perimeter_threshold") == "2");
}

TEST_CASE("Slice analysis measures fixed-XY solid target columns without multiplying regions or instances", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    ExPolygon whole = rectangle(0., 0., 4., 2.);
    double area = 8., top_minimum = 0.6, top_area_multiplier = 1.;
    bool middle_top = false;
    SECTION("one top target height") {}
    SECTION("different top heights are separate targets") {
        middle_top = true;
        top_minimum = 0.4;
        top_area_multiplier = 2.;
    }
    SECTION("a through-hole is not part of the target columns") {
        whole = square_with_hole();
        area = 84.;
    }
    for (size_t index = 0; index < 3; ++index) {
        const SurfaceType type = index == 0 ? stBottom : index == 2 || middle_top ? stTop : stInternalSolid;
        LayerRegion* region = add_fill_target_layer(object, 0.2, 0.2 * (index + 1), {Surface(type, whole)});
        region->layer()->add_region(&object.printing_region(0))->fill_surfaces.surfaces = region->fill_surfaces.surfaces;
    }
    const auto settings = all_settings(print);
    const json report = make_slice_analysis_report(print, InitializedResult{});
    const json& depth = report.at("objects").at("groups").at(0).at("solid_fill_target_depth");
    REQUIRE(depth.at("scope") == "per_instance_continuous_solid_fill_surface_slabs");
    REQUIRE(depth.at("z_resolution_mm") == SCALING_FACTOR);
    require_target_depth(depth, "top", area * top_area_multiplier, top_minimum, 0.6);
    require_target_depth(depth, "bottom", area, 0.6, 0.6);
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(all_settings(print) == settings);
    for (const Layer* layer : object.layers())
        for (const LayerRegion* region : layer->regions()) {
            REQUIRE(region->fill_surfaces.surfaces.size() == 1);
            const Surface& surface = region->fill_surfaces.surfaces.front();
            REQUIRE(surface.expolygon.contour.points == whole.contour.points);
            REQUIRE(surface.thickness == -1.);
            REQUIRE(surface.thickness_layers == 1);
        }
}

TEST_CASE("Slice analysis does not turn adjacent solid overlaps into a wandering vertical column", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    double top_area = 4., maximum = 0.6;
    SECTION("only half of the top continues through both lower layers") {
        add_fill_target_layer(object, 0.2, 0.2, {Surface(stInternalSolid, rectangle(0., 0., 1., 2.))});
        add_fill_target_layer(object, 0.2, 0.4, {Surface(stInternalSolid, rectangle(0., 0., 1., 2.))});
        add_fill_target_layer(object, 0.2, 0.6, {Surface(stTop, rectangle(0., 0., 2., 2.))});
    }
    SECTION("pairwise overlaps have no common positive-area column across all three layers") {
        add_fill_target_layer(object, 0.2, 0.2, {Surface(stInternalSolid, rectangle(0., 0., 1., 1.))});
        add_fill_target_layer(object, 0.2, 0.4, {Surface(stInternalSolid, rectangle(0.5, 0., 1., 1.))});
        add_fill_target_layer(object, 0.2, 0.6, {Surface(stTop, rectangle(1., 0., 1., 1.))});
        top_area = 1.;
        maximum = 0.4;
    }
    require_target_depth(solid_target_depth(print), "top", top_area, 0.2, maximum);
}

TEST_CASE("Slice analysis uses actual slab heights and Z union rather than summing overlapping layers", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    const ExPolygon whole = rectangle(0., 0., 2., 2.);
    double expected = 0.;
    SECTION("adaptive layer heights") {
        add_fill_target_layer(object, 0.15, 0.15, {Surface(stBottom, whole)});
        add_fill_target_layer(object, 0.25, 0.4, {Surface(stInternalSolid, whole)});
        add_fill_target_layer(object, 0.1, 0.5, {Surface(stTop, whole)});
        expected = 0.5;
    }
    SECTION("Cura-style nominal overlap contributes only its Z union") {
        add_fill_target_layer(object, 0.2, 0.2, {Surface(stBottom, whole)});
        add_fill_target_layer(object, 0.2, 0.3, {Surface(stTop, whole)});
        expected = 0.3;
    }
    SECTION("a positive nominal gap interrupts both directions") {
        add_fill_target_layer(object, 0.2, 0.2, {Surface(stBottom, whole)});
        add_fill_target_layer(object, 0.2, 0.5, {Surface(stTop, whole)});
        expected = 0.2;
    }
    SECTION("a gap of one native coordinate unit is not bridged") {
        add_fill_target_layer(object, 0.2, 0.2, {Surface(stBottom, whole)});
        add_fill_target_layer(object, 0.2, 0.4 + SCALING_FACTOR, {Surface(stTop, whole)});
        expected = 0.2;
    }
    SECTION("ordinary decimal rounding is not a physical gap") {
        for (size_t index = 0; index < 4; ++index)
            add_fill_target_layer(object, 0.2, 0.2 * (index + 1),
                                  {Surface(index == 0 ? stBottom : index == 3 ? stTop : stInternalSolid, whole)});
        expected = 0.8;
    }
    const json depth = solid_target_depth(print);
    require_target_depth(depth, "top", 4., expected, expected);
    require_target_depth(depth, "bottom", 4., expected, expected);
}

TEST_CASE("Slice analysis preserves combined solid infill depth across layers marked void", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    const ExPolygon whole = rectangle(0., 0., 2., 2.);
    add_fill_target_layer(object, 0.2, 0.2, {Surface(stInternalVoid, whole)});
    Surface combined(stInternalSolid, whole);
    combined.thickness = 0.4;
    combined.thickness_layers = 99; // The observation must use the nominal slab, not this count.
    add_fill_target_layer(object, 0.2, 0.4, {combined});
    add_fill_target_layer(object, 0.2, 0.6, {Surface(stTop, whole)});
    const json depth = solid_target_depth(print);
    require_target_depth(depth, "top", 4., 0.6, 0.6);
    REQUIRE(depth.at("bottom").at("target_area_sum_mm2") == 0.);
    REQUIRE(depth.at("bottom").at("continuous_depth_mm").at("min").is_null());
    REQUIRE(depth.at("bottom").at("continuous_depth_mm").at("max").is_null());
}

TEST_CASE("Slice analysis distinguishes solid fill target depth from actual skin density and bridge flow", "[SliceAnalysis]")
{
    double density = 100.;
    SECTION("full skin density") {}
    SECTION("reduced skin density") { density = 25.; }
    SECTION("zero skin density") { density = 0.; }
    Model model;
    Print print;
    add_box(model, 1);
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionPercent>("top_surface_density")->value = density;
    config.option<ConfigOptionPercent>("bottom_surface_density")->value = density;
    print.set_status_silent();
    print.auto_assign_extruders(model.objects.front());
    print.apply(model, config);
    PrintObject& object = *print.get_object(0);
    const ExPolygon whole = rectangle(0., 0., 2., 2.);
    LayerRegion* region = add_fill_target_layer(object, 0.2, 0.2, {Surface(stBottomBridge, whole)});
    ExtrusionPath bridge = planned_path(0., 1., 2., 1., 0.4f, erBridgeInfill);
    bridge.height = 0.5f; // Actual bridge flow height is deliberately not the fill-target slab height.
    region->fills.append(bridge);
    add_fill_target_layer(object, 0.2, 0.4, {Surface(stInternalBridge, whole)});
    add_fill_target_layer(object, 0.2, 0.6, {Surface(stTop, whole)});
    const json depth = solid_target_depth(print);
    require_target_depth(depth, "top", 4., 0.6, 0.6);
    require_target_depth(depth, "bottom", 4., 0.6, 0.6);
}

TEST_CASE("Slice analysis leaves absent top or bottom fill targets not applicable", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    const ExPolygon whole = rectangle(0., 0., 2., 2.);
    Surface sparse(stInternal, whole);
    sparse.thickness = std::numeric_limits<double>::quiet_NaN(); // Not a solid slab or seed.
    LayerRegion* region = add_fill_target_layer(object, 0.2, 0.2, {sparse, Surface(stInternalVoid, whole)});
    region->perimeters.append(planned_loop(whole.contour.split_at_first_point().points));
    const json depth = solid_target_depth(print);
    REQUIRE(depth.at("complete") == true);
    for (const char* side : {"top", "bottom"}) {
        REQUIRE(depth.at(side).at("target_area_sum_mm2") == 0.);
        REQUIRE(depth.at(side).at("continuous_depth_mm").at("min").is_null());
        REQUIRE(depth.at(side).at("continuous_depth_mm").at("max").is_null());
    }
}

TEST_CASE("Slice analysis marks invalid solid slabs unknown independently of existing coverage", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    LayerRegion* region = add_fill_target_layer(object, 0.2, 0.2, {Surface(stTop, rectangle(0., 0., 2., 2.))});
    bool coverage_complete = true;
    SECTION("non-finite explicit thickness") { region->fill_surfaces.surfaces.front().thickness = std::numeric_limits<double>::quiet_NaN(); }
    SECTION("negative non-sentinel thickness") { region->fill_surfaces.surfaces.front().thickness = -0.2; }
    SECTION("zero thickness") { region->fill_surfaces.surfaces.front().thickness = 0.; }
    SECTION("positive thickness vanishing below native Z precision") { region->fill_surfaces.surfaces.front().thickness = SCALING_FACTOR * 0.1; }
    SECTION("slab endpoint outside coordinate domain") { region->fill_surfaces.surfaces.front().thickness = 1e30; }
    SECTION("invalid model geometry") {
        region->layer()->slicing_errors = true;
        coverage_complete = false;
    }
    const json report = make_slice_analysis_report(print, InitializedResult{});
    const json& group = report.at("objects").at("groups").at(0);
    const json& depth = group.at("solid_fill_target_depth");
    REQUIRE(depth.at("complete") == false);
    for (const char* side : {"top", "bottom"}) {
        REQUIRE(depth.at(side).at("target_area_sum_mm2").is_null());
        REQUIRE(depth.at(side).at("continuous_depth_mm").at("min").is_null());
        REQUIRE(depth.at(side).at("continuous_depth_mm").at("max").is_null());
    }
    REQUIRE(group.at("planned_layer_coverage").at("layers").at(0).at("complete") == coverage_complete);
    REQUIRE(json::parse(report.dump()) == report);
}

TEST_CASE("Slice analysis measures actual model path slabs independently of the solid target depth", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    std::vector<LayerRegion*> regions;
    for (size_t index = 0; index < 3; ++index)
        regions.push_back(add_fill_target_layer(object, 0.2, 0.2 * (index + 1),
                                               {Surface(index == 0 ? stBottom : index == 2 ? stTop : stInternalSolid,
                                                        rectangle(0., 0., 4., 2.))}));
    const json original_targets = solid_target_depth(print);
    double top_minimum = 0.6, bottom_minimum = 0.6, top_uncovered = 0.;
    SECTION("full-width paths and duplicates across regions do not multiply depth or target area") {}
    SECTION("half the middle path is missing although its fill target is unchanged") {
        top_minimum = bottom_minimum = 0.2;
    }
    SECTION("a quarter-covered top has an explicit zero minimum rather than the target thickness") {
        top_minimum = 0.;
        bottom_minimum = 0.4;
        top_uncovered = 6.;
    }
    for (size_t index = 0; index < regions.size(); ++index) {
        const bool half_middle = index == 1 && top_minimum == 0.2;
        const bool sparse_top  = index == 2 && top_uncovered > 0.;
        const ExtrusionPath path = planned_path(0., sparse_top ? 0.25 : 1., half_middle ? 2. : 4.,
                                                sparse_top ? 0.25 : 1., sparse_top ? 0.5f : 2.f, erInternalInfill);
        regions[index]->fills.append(path);
        LayerRegion* duplicate = regions[index]->layer()->add_region(&object.printing_region(0));
        duplicate->perimeters.append(path);
        duplicate->fill_surfaces.surfaces = regions[index]->fill_surfaces.surfaces;
    }
    const auto settings = all_settings(print);
    const json report = make_slice_analysis_report(print, InitializedResult{});
    const json& group = report.at("objects").at("groups").at(0);
    const json& depth = group.at("planned_material_depth");
    REQUIRE(depth.at("scope") == "per_instance_fixed_xy_model_path_slabs_before_gcode");
    REQUIRE(depth.at("z_resolution_mm") == SCALING_FACTOR);
    require_target_depth(depth, "top", 8., top_minimum, 0.6);
    require_target_depth(depth, "bottom", 8., bottom_minimum, 0.6);
    REQUIRE(depth.at("top").at("uncovered_start_area_sum_mm2") == top_uncovered);
    REQUIRE(depth.at("bottom").at("uncovered_start_area_sum_mm2") == 0.);
    REQUIRE(group.at("solid_fill_target_depth") == original_targets);
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(all_settings(print) == settings);
    for (const LayerRegion* region : regions) {
        REQUIRE(region->fills.entities.size() == 1);
        REQUIRE(dynamic_cast<const ExtrusionPath*>(region->fills.entities.front())->height == 0.2f);
    }
}

TEST_CASE("Slice analysis does not join laterally wandering planned path columns", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    for (size_t index = 0; index < 3; ++index) {
        const double x = 0.5 * index;
        LayerRegion* region = add_fill_target_layer(object, 0.2, 0.2 * (index + 1),
            {Surface(index == 0 ? stBottom : index == 2 ? stTop : stInternalSolid, rectangle(x, 0., 1., 1.))});
        region->fills.append(planned_path(x, 0.5, x + 1., 0.5, 1.f));
    }
    const json depth = planned_material_depth(print);
    for (const char* side : {"top", "bottom"}) {
        require_target_depth(depth, side, 1., 0.2, 0.4);
        REQUIRE(depth.at(side).at("uncovered_start_area_sum_mm2") == 0.);
    }
}

TEST_CASE("Slice analysis unions different model path heights at the same print plane", "[SliceAnalysis]")
{
    const bool same_footprint = GENERATE(false, true);
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    const ExPolygon whole = rectangle(0., 0., 4., 2.);
    LayerRegion* region = add_fill_target_layer(object, 0.2, 0.4, {Surface(stTop, whole), Surface(stBottom, whole)});
    const json targets = solid_target_depth(print);
    ExtrusionPath tall = planned_path(0., 1., same_footprint ? 4. : 2., 1., 2.f);
    tall.height = 0.4f;
    region->fills.append(tall);
    region->fills.append(planned_path(same_footprint ? 0. : 2., 1., 4., 1., 2.f));
    const json depth = planned_material_depth(print);
    require_target_depth(depth, "top", 8., same_footprint ? 0.4 : 0.2, 0.4);
    require_target_depth(depth, "bottom", 8., 0.2, 0.2);
    REQUIRE(depth.at("top").at("uncovered_start_area_sum_mm2") == 0.);
    REQUIRE(depth.at("bottom").at("uncovered_start_area_sum_mm2") == 0.);
    REQUIRE(solid_target_depth(print) == targets);
}

TEST_CASE("Slice analysis unions actual path slab Z intervals at the existing native precision", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    std::vector<std::pair<double, double>> layers;
    double expected = 0.;
    SECTION("variable actual path heights") {
        layers = {{0.15, 0.15}, {0.25, 0.4}, {0.1, 0.5}};
        expected = 0.5;
    }
    SECTION("nominal Cura-style overlap is not added twice") {
        layers = {{0.2, 0.2}, {0.2, 0.3}};
        expected = 0.3;
    }
    SECTION("a real one-unit gap stays disconnected") {
        layers = {{0.2, 0.2}, {0.2, 0.4 + SCALING_FACTOR}};
        expected = 0.2;
    }
    SECTION("a double ULP is not a manufactured gap") {
        layers = {{0.2, 0.2}, {0.2, std::nextafter(0.4, 1.)}};
        expected = 0.4;
    }
    for (size_t index = 0; index < layers.size(); ++index) {
        const auto [height, z] = layers[index];
        LayerRegion* region = add_fill_target_layer(object, height, z,
            {Surface(index == 0 ? stBottom : index == layers.size() - 1 ? stTop : stInternalSolid, rectangle(0., 0., 2., 2.))});
        ExtrusionPath path = planned_path(0., 1., 2., 1., 2.f);
        path.height = float(height);
        region->fills.append(path);
    }
    const json depth = planned_material_depth(print);
    for (const char* side : {"top", "bottom"}) {
        require_target_depth(depth, side, 4., expected, expected);
        REQUIRE(depth.at(side).at("uncovered_start_area_sum_mm2") == 0.);
    }
}

TEST_CASE("Slice analysis uses path height for combined and bridge material but surface thickness for its starting target", "[SliceAnalysis]")
{
    bool shorter_bridge = false;
    SECTION("combined infill bridges layers without any lower paths") {}
    SECTION("a different bridge flow height leaves the bottom target uncovered") { shorter_bridge = true; }
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    const ExPolygon whole = rectangle(0., 0., 2., 2.);
    add_fill_target_layer(object, 0.2, 0.2, {Surface(stInternalVoid, whole)});
    Surface bottom(stBottomBridge, whole);
    bottom.thickness = 0.4;
    bottom.thickness_layers = 99;
    LayerRegion* combined = add_fill_target_layer(object, 0.2, 0.4, {bottom});
    LayerRegion* upper = add_fill_target_layer(object, 0.2, 0.6, {Surface(stTop, whole)});
    const json targets = solid_target_depth(print);
    ExtrusionPath path = planned_path(0., 1., 2., 1., 2.f, erBridgeInfill);
    path.height = shorter_bridge ? 0.3f : 0.4f;
    combined->fills.append(path);
    upper->perimeters.append(planned_path(0., 1., 2., 1., 2.f));
    const json depth = planned_material_depth(print);
    require_target_depth(depth, "top", 4., shorter_bridge ? 0.5 : 0.6, shorter_bridge ? 0.5 : 0.6);
    require_target_depth(depth, "bottom", 4., shorter_bridge ? 0. : 0.6, shorter_bridge ? 0. : 0.6);
    REQUIRE(depth.at("top").at("uncovered_start_area_sum_mm2") == 0.);
    REQUIRE(depth.at("bottom").at("uncovered_start_area_sum_mm2") == (shorter_bridge ? 4. : 0.));
    REQUIRE(solid_target_depth(print) == targets);
}

TEST_CASE("Slice analysis separates absent material-depth targets from targets with no model paths", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    bool targets = true;
    SECTION("top and bottom targets without any model material") {}
    SECTION("non-model paths and zero-flow paths do not supply material") {
        object.add_support_layer(0, 0, 0.2, 0.2)->support_fills.append(planned_path(0., 1., 2., 1., 2.f));
        print.get_brimMap()[object.id()].append(planned_path(0., 1., 2., 1., 2.f));
    }
    SECTION("a sparse-fill target is not a top or bottom starting target") { targets = false; }
    for (size_t index = 0; index < 2; ++index) {
        LayerRegion* region = add_fill_target_layer(object, 0.2, 0.2 * (index + 1),
            {Surface(targets ? index == 0 ? stBottom : stTop : stInternal, rectangle(0., 0., 2., 2.))});
        ExtrusionPath zero = planned_path(0., 1., 2., 1., 2.f);
        zero.mm3_per_mm = 0.;
        zero.height = std::numeric_limits<float>::quiet_NaN(); // An intentionally inactive path has no slab.
        region->fills.append(zero);
        zero.mm3_per_mm = 0.1;
        zero.set_force_no_extrusion(true);
        region->fills.append(zero);
        region->thin_fills.append(planned_path(0., 1., 2., 1., 2.f)); // Not separately printed.
    }
    const json depth = planned_material_depth(print);
    REQUIRE(depth.at("complete") == true);
    for (const char* side : {"top", "bottom"}) {
        REQUIRE(depth.at(side).at("target_area_sum_mm2") == (targets ? 4. : 0.));
        REQUIRE(depth.at(side).at("uncovered_start_area_sum_mm2") == (targets ? 4. : 0.));
        if (targets)
            require_target_depth(depth, side, 4., 0., 0.);
        else {
            REQUIRE(depth.at(side).at("continuous_depth_mm").at("min").is_null());
            REQUIRE(depth.at(side).at("continuous_depth_mm").at("max").is_null());
        }
    }
}

TEST_CASE("Slice analysis keeps invalid path heights local to material depth", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    LayerRegion* region = add_fill_target_layer(object, 0.2, 0.2, {Surface(stTop, rectangle(0., 0., 2., 2.))});
    ExtrusionPath path = planned_path(0., 1., 2., 1., 2.f);
    region->fills.append(path);
    const json before = make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0);
    auto* stored = dynamic_cast<ExtrusionPath*>(region->fills.entities.front());
    bool old_coverage_unchanged = true;
    SECTION("zero path height") { stored->height = 0.; }
    SECTION("negative path height") { stored->height = -0.2f; }
    SECTION("nonfinite path height") { stored->height = std::numeric_limits<float>::quiet_NaN(); }
    SECTION("height below native event precision") { stored->height = float(SCALING_FACTOR * 0.1); }
    SECTION("height outside event coordinate domain") { stored->height = 1e30f; }
    SECTION("invalid positive-flow width") {
        stored->width = std::numeric_limits<float>::quiet_NaN();
        old_coverage_unchanged = false;
    }
    SECTION("invalid extrusion flow") {
        stored->mm3_per_mm = std::numeric_limits<double>::quiet_NaN();
        old_coverage_unchanged = false;
    }
    const json group = make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0);
    const json& depth = group.at("planned_material_depth");
    REQUIRE(depth.at("complete") == false);
    for (const char* side : {"top", "bottom"}) {
        REQUIRE(depth.at(side).at("target_area_sum_mm2").is_null());
        REQUIRE(depth.at(side).at("uncovered_start_area_sum_mm2").is_null());
        REQUIRE(depth.at(side).at("continuous_depth_mm").at("min").is_null());
        REQUIRE(depth.at(side).at("continuous_depth_mm").at("max").is_null());
    }
    REQUIRE(group.at("solid_fill_target_depth") == before.at("solid_fill_target_depth"));
    if (old_coverage_unchanged)
        REQUIRE(group.at("planned_layer_coverage") == before.at("planned_layer_coverage"));
    REQUIRE(json::parse(group.dump()) == group);
}

TEST_CASE("Slice analysis leaves material depth unknown without valid retained layers and starting targets", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    SECTION("no retained model layers") {}
    SECTION("invalid print plane") {
        object.add_layer(0, 0.2, std::numeric_limits<double>::quiet_NaN(), 0.1);
    }
    SECTION("invalid top starting thickness") {
        Surface top(stTop, rectangle(0., 0., 2., 2.));
        top.thickness = 0.;
        add_fill_target_layer(object, 0.2, 0.2, {top})->fills.append(planned_path(0., 1., 2., 1., 2.f));
    }
    const json depth = planned_material_depth(print);
    REQUIRE(depth.at("complete") == false);
    for (const char* side : {"top", "bottom"}) {
        REQUIRE(depth.at(side).at("target_area_sum_mm2").is_null());
        REQUIRE(depth.at(side).at("uncovered_start_area_sum_mm2").is_null());
        REQUIRE(depth.at(side).at("continuous_depth_mm").at("min").is_null());
        REQUIRE(depth.at(side).at("continuous_depth_mm").at("max").is_null());
    }
}

TEST_CASE("Slice analysis target-driven material depth matches the full-run reference through holes and sloped boundaries", "[SliceAnalysis]")
{
    const int scenario = GENERATE(0, 1, 2);
    json original;
    for (const bool reverse_z : {false, true}) {
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        PrintObject& object = *print.get_object(0);
        for (int position = 0; position < 8; ++position) {
            const int index = reverse_z ? 7 - position : position;
            const double height = scenario == 1 && index % 3 == 1 ? 0.5 : 0.25;
            const double z = 0.25 * (index + 1);
            ExPolygon target = square_with_hole();
            // Integer-coordinate shear preserves the hole while supplying
            // non-axis-aligned boundaries and negative XY coordinates.
            for (Polygon* polygon : {&target.contour, &target.holes.front()})
                for (Point& point : polygon->points) {
                    point.x() += point.y() / 2 - scaled(4.);
                    point.y() -= scaled(3.);
                }
            LayerRegion* region = add_fill_target_layer(object, height, reverse_z ? 2. - z + height : z,
                                                        {Surface(stTop, target), Surface(stBottom, target)});
            ExtrusionLoop loop = planned_loop(target.contour.split_at_first_point().points);
            loop.paths.front().height = float(height);
            region->perimeters.append(loop);
            if (scenario < 2) {
                for (int line = 0; line < 3; ++line) {
                    ExtrusionPath path = planned_path(-4., line * 2. + (index % 2) * 0.5, 10., line * 2. + 1., 1.f);
                    path.height = float(height);
                    region->fills.append(path);
                }
            } else {
                // Fixed-seed, repeatable slanted stripes, not randomized test
                // execution. Different layers retain different fixed-XY parts.
                unsigned state = 0x31415926u + unsigned(index);
                for (size_t line = 0; line < 12; ++line) {
                    state = 1664525u * state + 1013904223u;
                    const double y0 = -3. + double(state % 40u) * 0.25;
                    state = 1664525u * state + 1013904223u;
                    const double y1 = -3. + double(state % 40u) * 0.25;
                    ExtrusionPath path = planned_path(-4., y0, 10., y1, 0.5f);
                    path.height = float(height);
                    region->fills.append(path);
                }
            }
        }
        const json reference = full_run_material_depth_reference(object);
        const json measured = planned_material_depth(print);
        INFO("scenario=" << scenario << "; reverse_z=" << reverse_z);
        for (const char* side : {"top", "bottom"})
            for (const char* field : {"target_area_sum_mm2", "uncovered_start_area_sum_mm2", "continuous_depth_mm"}) {
                INFO(side << "." << field);
                REQUIRE(measured.at(side).at(field) == reference.at(side).at(field));
            }
        REQUIRE(measured == reference);
        if (!reverse_z)
            original = measured;
        else {
            REQUIRE(measured.at("top").at("continuous_depth_mm") == original.at("bottom").at("continuous_depth_mm"));
            REQUIRE(measured.at("bottom").at("continuous_depth_mm") == original.at("top").at("continuous_depth_mm"));
        }
        REQUIRE(planned_material_depth(print) == measured);
    }
}

TEST_CASE("Slice analysis observes generated sparse top skins rather than reusing their target depth", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("before_layer_change_gcode", std::string("G92 E0"));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.2);
    config.set("top_shell_layers", 3);
    config.set("bottom_shell_layers", 3);
    config.set("skirt_loops", 0);
    config.set("enable_support", false);
    config.option<ConfigOptionPercent>("top_surface_density")->value = 25.;
    print.set_status_silent();
    print.auto_assign_extruders(model.objects.front());
    print.apply(model, config);
    const StringObjectException validation = print.validate();
    INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
    REQUIRE(validation.string.empty());
    print.process();
    const auto settings = all_settings(print);
    const json report = make_slice_analysis_report(print, InitializedResult{});
    const json& group = report.at("objects").at("groups").at(0);
    const json& planned = group.at("planned_material_depth");
    const json& targets = group.at("solid_fill_target_depth");
    REQUIRE(planned == full_run_material_depth_reference(*print.get_object(0)));
    REQUIRE(planned.at("complete") == true);
    REQUIRE(targets.at("complete") == true);
    REQUIRE(planned.at("top").at("target_area_sum_mm2") == targets.at("top").at("target_area_sum_mm2"));
    REQUIRE(planned.at("top").at("uncovered_start_area_sum_mm2").get<double>() > 0.);
    REQUIRE(planned.at("top").at("uncovered_start_area_sum_mm2").get<double>() < planned.at("top").at("target_area_sum_mm2").get<double>());
    REQUIRE(planned.at("top").at("continuous_depth_mm").at("min") == 0.);
    REQUIRE(planned.at("top").at("continuous_depth_mm").at("max").get<double>() > 0.);
    REQUIRE(targets.at("top").at("continuous_depth_mm").at("min").get<double>() > 0.);
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(all_settings(print) == settings);
    print.cancel();
    REQUIRE_THROWS_AS(make_slice_analysis_report(print, InitializedResult{}), CanceledException);
}

TEST_CASE("Slice analysis measures long expanding solid slopes without a depth proxy", "[SliceAnalysis][slow]")
{
    for (size_t count : {size_t(100), size_t(400)}) {
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        PrintObject& object = *print.get_object(0);
        for (size_t index = 0; index < count; ++index)
            add_fill_target_layer(object, 0.2, 0.2 * (index + 1),
                                  {Surface(index == 0 ? stBottom : index == count - 1 ? stTop : stInternalSolid,
                                           rectangle(0., 0., 1. + 0.01 * index, 2.))});
        const auto started = std::chrono::steady_clock::now();
        const json depth = solid_target_depth(print);
        const double elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        std::cout << "solid-target-depth expanding slope: layers=" << count << " report_ms=" << elapsed_ms << std::endl;
        require_target_depth(depth, "top", 2. * (1. + 0.01 * (count - 1)), 0.2, 0.2 * count);
        require_target_depth(depth, "bottom", 2., 0.2 * count, 0.2 * count);
        print.cancel();
        REQUIRE_THROWS_AS(make_slice_analysis_report(print, InitializedResult{}), CanceledException);
    }
}

namespace {

void initialize_motion_processor(GCodeProcessor& processor)
{
    PrintConfig config;
    config.gcode_flavor.value                         = gcfMarlinFirmware;
    config.filament_diameter.values                   = {1.75, 1.75};
    config.machine_max_acceleration_extruding.values  = {1000., 1000.};
    config.machine_max_acceleration_travel.values     = {1000., 1000.};
    config.machine_max_acceleration_retracting.values = {1000., 1000.};
    processor.apply_config(config);
    processor.initialize("slice-analysis-estimated-motion.gcode");
}

std::string motion_role(ExtrusionRole role)
{
    return ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role) + ExtrusionEntity::role_to_string(role) + "\n";
}

std::string motion_preamble()
{
    return "G90\nM83\nM201 X1000 Y1000 Z1000 E1000\nM203 X1000 Y1000 Z1000 E1000\n"
           "M204 P100 T100 R100\nM205 X10 Y10 Z10 E1000 S0 T0\n;" +
           GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Layer_Change) + "\n";
}

json motion_report(const GCodeProcessorResult& result, ExtrusionRole role)
{
    Model model;
    Print print;
    print.set_status_silent();
    print.apply(model, DynamicPrintConfig::full_print_config());
    const auto rows = make_slice_analysis_report(print, result).at("gcode").at("extrusion_by_role");
    for (const auto& row : rows)
        if (row.at("role_id") == static_cast<unsigned int>(role))
            return row.at("estimated_motion");
    FAIL("Expected classified extrusion row in the public report");
    return nullptr;
}

void require_motion_unknown(const json& row)
{
    REQUIRE(row.at("scope") == "normal_estimator_finalized_extrusion_blocks");
    REQUIRE(row.at("complete") == false);
    REQUIRE(row.at("block_count").is_null());
    for (const char* field : {"peak_speed_mm_s", "tangential_acceleration_mm_s2"}) {
        REQUIRE(row.at(field).at("min").is_null());
        REQUIRE(row.at(field).at("max").is_null());
    }
}

} // namespace

TEST_CASE("Gcode estimated motion observes finalized short and limited blocks rather than requested speed",
          "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_motion_processor(processor);
    std::string overrides;
    std::string move                  = "G1 X1 E0.05 F6000\n";
    double      expected_peak         = std::sqrt(200.); // v_entry=v_exit=10, a=100, d=1: triangular profile.
    double      expected_acceleration = 100.;
    SECTION("a one millimeter line cannot reach its requested 100 mm per second") {}
    SECTION("a long line reaches its cruise speed")
    {
        move          = "G1 X100 E5 F6000\n";
        expected_peak = 100.;
    }
    SECTION("axis acceleration limits replace the requested acceleration")
    {
        overrides             = "M201 X25\n";
        expected_peak         = std::sqrt(125.);
        expected_acceleration = 25.;
    }
    SECTION("axis speed limits replace the requested feedrate")
    {
        overrides     = "M203 X15\n";
        move          = "G1 X100 E5 F6000\n";
        expected_peak = 15.;
    }
    processor.process_buffer(motion_preamble() + overrides + "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter) + move + "G4 P0\n");
    const auto before = processor.get_result().moves;
    processor.finalize(false);
    const auto& result = processor.get_result();
    const auto& motion = result.normal_estimated_motion[erExternalPerimeter];
    REQUIRE(motion.complete);
    REQUIRE(motion.block_count == 1);
    REQUIRE(motion.min_peak_speed_mm_s == Approx(expected_peak));
    REQUIRE(motion.max_peak_speed_mm_s == Approx(expected_peak));
    REQUIRE(motion.min_tangential_acceleration_mm_s2 == Approx(-expected_acceleration));
    REQUIRE(motion.max_tangential_acceleration_mm_s2 == Approx(expected_acceleration));
    const json row = motion_report(result, erExternalPerimeter);
    REQUIRE(row.at("scope") == "normal_estimator_finalized_extrusion_blocks");
    REQUIRE(row.at("complete") == true);
    REQUIRE(row.at("block_count") == 1);
    REQUIRE(row.at("peak_speed_mm_s").at("min").get<double>() == Approx(expected_peak));
    REQUIRE(row.at("peak_speed_mm_s").at("max").get<double>() == Approx(expected_peak));
    REQUIRE(row.at("tangential_acceleration_mm_s2").at("min").get<double>() == Approx(-expected_acceleration));
    REQUIRE(row.at("tangential_acceleration_mm_s2").at("max").get<double>() == Approx(expected_acceleration));
    REQUIRE(result.moves.size() == before.size());
    for (size_t index = 0; index < before.size(); ++index) {
        REQUIRE(result.moves[index].position == before[index].position);
        REQUIRE(result.moves[index].feedrate == before[index].feedrate);
        REQUIRE(result.moves[index].travel_dist == before[index].travel_dist);
        REQUIRE(result.moves[index].mm3_per_mm == before[index].mm3_per_mm);
    }
}

TEST_CASE("Gcode estimated motion excludes absent acceleration phases", "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_motion_processor(processor);
    processor.process_buffer(motion_preamble() + "M205 X1000 Y1000 Z1000 E1000\nG1 Z0.2 F1200\n" + motion_role(erExternalPerimeter) +
                             "G1 X10 E0.5 F1200\nG4 P0\n");
    processor.finalize(false);
    const auto& motion = processor.get_result().normal_estimated_motion[erExternalPerimeter];
    REQUIRE(motion.complete);
    REQUIRE(motion.block_count == 1);
    REQUIRE(motion.min_peak_speed_mm_s == Approx(20.));
    REQUIRE(motion.max_peak_speed_mm_s == Approx(20.));
    REQUIRE(motion.min_tangential_acceleration_mm_s2 == 0.f);
    REQUIRE(motion.max_tangential_acceleration_mm_s2 == 0.f);
}

TEST_CASE("Gcode estimated motion keeps the sign of one-sided acceleration phases", "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_motion_processor(processor);
    std::string moves;
    double expected_acceleration = 100.;
    SECTION("a short block before a long continuation only accelerates") {
        moves = motion_role(erExternalPerimeter) + "G1 X1.5 E0.075 F6000\n" + motion_role(erSupportMaterial) +
                "G1 X101.5 E5 F6000\n";
    }
    SECTION("a short block after a long approach only decelerates") {
        moves = motion_role(erSupportMaterial) + "G1 X100 E5 F6000\n" + motion_role(erExternalPerimeter) +
                "G1 X101.5 E0.075 F6000\n";
        expected_acceleration = -100.;
    }
    // 10 -> 20 mm/s across 1.5 mm at 100 mm/s^2 has no cruise or opposite-sign phase.
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F6000\n" + moves + "G4 P0\n");
    processor.finalize(false);
    const auto& motion = processor.get_result().normal_estimated_motion[erExternalPerimeter];
    REQUIRE(motion.complete);
    REQUIRE(motion.block_count == 1);
    REQUIRE(motion.min_peak_speed_mm_s == Approx(20.));
    REQUIRE(motion.max_peak_speed_mm_s == Approx(20.));
    REQUIRE(motion.min_tangential_acceleration_mm_s2 == expected_acceleration);
    REQUIRE(motion.max_tangential_acceleration_mm_s2 == expected_acceleration);
}

TEST_CASE("Gcode estimated motion consumes long planner queues once and is independent of parse chunks", "[SliceAnalysis][GCodeProcessor]")
{
    std::vector<std::string> lines;
    for (size_t index = 1; index <= 400; ++index)
        lines.push_back("G1 X" + std::to_string(index) + " E0.05 F6000\n");
    GCodeProcessor whole(true);
    GCodeProcessor chunked(true);
    initialize_motion_processor(whole);
    initialize_motion_processor(chunked);
    const std::string prefix = motion_preamble() + "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter);
    std::string       all    = prefix;
    for (const auto& line : lines)
        all += line;
    whole.process_buffer(all + "G4 P0\n");
    chunked.process_buffer(prefix);
    for (size_t start = 0; start < lines.size(); start += 100) {
        std::string part;
        for (size_t index = start; index < start + 100; ++index)
            part += lines[index];
        chunked.process_buffer(part);
    }
    chunked.process_buffer("G4 P0\n");
    whole.finalize(false);
    chunked.finalize(false);
    const auto& a      = whole.get_result();
    const auto& b      = chunked.get_result();
    const auto& motion = a.normal_estimated_motion[erExternalPerimeter];
    REQUIRE(motion.complete);
    REQUIRE(motion.block_count == 400);
    REQUIRE(motion.min_peak_speed_mm_s > 0.f);
    REQUIRE(motion.max_peak_speed_mm_s == Approx(100.));
    REQUIRE(motion_report(a, erExternalPerimeter) == motion_report(b, erExternalPerimeter));
    const auto mode = static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE(a.print_statistics.modes[mode].time == b.print_statistics.modes[mode].time);
    REQUIRE(a.print_statistics.modes[mode].layers_times == b.print_statistics.modes[mode].layers_times);
    REQUIRE(a.print_statistics.modes[mode].roles_times == b.print_statistics.modes[mode].roles_times);
    REQUIRE(a.moves.size() == b.moves.size());
    for (size_t index = 0; index < a.moves.size(); ++index) {
        REQUIRE(a.moves[index].position == b.moves[index].position);
        REQUIRE(a.moves[index].feedrate == b.moves[index].feedrate);
        REQUIRE(a.moves[index].time == b.moves[index].time);
        REQUIRE(a.moves[index].layer_duration == b.moves[index].layer_duration);
    }
}

TEST_CASE("Gcode estimated motion classifies native arc blocks and excludes negative extrusion", "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_motion_processor(processor);
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erExternalPerimeter) +
                             "G2 X10 Y0 I5 J0 E0.5 F600\nG3 X20 Y0 I5 J0 E-0.5 F600\nG4 P0\n");
    processor.finalize(false);
    const auto& result    = processor.get_result();
    size_t      arc_count = 0;
    const GCodeProcessorResult::MoveVertex* positive_arc = nullptr;
    for (const auto& move : result.moves)
        if (move.is_arc_move()) {
            ++arc_count;
            REQUIRE(move.interpolation_points.size() > 1);
            REQUIRE(move.type == EMoveType::Extrude);
            REQUIRE(move.extrusion_role == erExternalPerimeter);
            if (move.delta_extruder > 0.f) {
                REQUIRE(positive_arc == nullptr);
                positive_arc = &move;
            }
        }
    REQUIRE(arc_count == 2);
    REQUIRE(positive_arc != nullptr);
    REQUIRE(positive_arc->delta_extruder == 0.5f);
    // The existing parser computes F * float(1/60), so F600 is one float ULP above 10.
    REQUIRE(positive_arc->feedrate == 600.f * (1.f / 60.f));
    const auto& motion = result.normal_estimated_motion[erExternalPerimeter];
    REQUIRE(motion.complete);
    REQUIRE(motion.block_count == 1); // Not interpolation-point count and not the negative-E arc.
    REQUIRE(std::isfinite(motion.min_peak_speed_mm_s));
    REQUIRE(motion.min_peak_speed_mm_s > 0.f);
    REQUIRE(motion.max_peak_speed_mm_s <= positive_arc->feedrate);
}

TEST_CASE("Gcode estimated motion keeps roles separate and excludes custom unclassified and nonextruding moves",
          "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_motion_processor(processor);
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erCustom) + "G1 X1 E0.05\n" + motion_role(erNone) +
                             "G1 X2 E0.05\n" + motion_role(erExternalPerimeter) +
                             "G1 X3 E0.05\nG1 X4 E0\nG1 E0.2\nG1 E-0.2\nG1 X5 E-0.05\n" + motion_role(erSupportMaterial) +
                             "G1 X6 E0.05\nG4 P0\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    for (ExtrusionRole role : {erExternalPerimeter, erSupportMaterial}) {
        REQUIRE(result.normal_estimated_motion[role].complete);
        REQUIRE(result.normal_estimated_motion[role].block_count == 1);
    }
    for (ExtrusionRole role : {erCustom, erNone}) {
        const auto row = motion_report(result, role);
        REQUIRE(row.at("complete") == true);
        REQUIRE(row.at("block_count") == 0);
        for (const char* field : {"peak_speed_mm_s", "tangential_acceleration_mm_s2"}) {
            REQUIRE(row.at(field).at("min").is_null());
            REQUIRE(row.at(field).at("max").is_null());
        }
    }
}

TEST_CASE("Gcode estimated motion distinguishes final residual blocks from temporarily retained single blocks",
          "[SliceAnalysis][GCodeProcessor]")
{
    bool add_second_block = false;
    SECTION("a final single extrusion block is unknown") {}
    SECTION("a later block makes the temporarily retained extrusion estimable") { add_second_block = true; }
    GCodeProcessor processor(true);
    initialize_motion_processor(processor);
    processor.process_buffer(motion_preamble() + motion_role(erExternalPerimeter) + "G1 X1 E0.05 F6000\nG4 P0\nT1\n");
    if (add_second_block)
        processor.process_buffer("G1 X2 F6000\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    const auto& motion = result.normal_estimated_motion[erExternalPerimeter];
    REQUIRE(result.normal_estimated_motion[erSupportMaterial].complete);
    REQUIRE(result.normal_estimated_motion[erSupportMaterial].block_count == 0);
    if (add_second_block) {
        REQUIRE(motion.complete);
        REQUIRE(motion.block_count == 1);
        REQUIRE(motion_report(result, erExternalPerimeter).at("complete") == true);
    } else {
        REQUIRE_FALSE(motion.complete);
        require_motion_unknown(motion_report(result, erExternalPerimeter));
    }
}

TEST_CASE("Gcode estimated motion marks invalid acceleration and old metadata unknown without changing other roles",
          "[SliceAnalysis][GCodeProcessor]")
{
    SECTION("negative finalized acceleration is not a signed deceleration phase")
    {
        GCodeProcessor processor(true);
        initialize_motion_processor(processor);
        processor.process_buffer(motion_preamble() + "G1 Z0.2 F600\nM204 P-1\n" + motion_role(erExternalPerimeter) +
                                 "G1 X10 E0.5 F600\nG4 P0\n");
        processor.finalize(false);
        const auto& result = processor.get_result();
        REQUIRE_FALSE(result.normal_estimated_motion[erExternalPerimeter].complete);
        REQUIRE(result.normal_estimated_motion[erSupportMaterial].complete);
        require_motion_unknown(motion_report(result, erExternalPerimeter));
    }
    SECTION("legacy results have no finalized estimator metadata")
    {
        InitializedResult result;
        result.moves                        = {extrusion(0, 0.4f, 0.2f, 20.f, 0.08f)};
        result.moves.front().extrusion_role = erExternalPerimeter;
        require_motion_unknown(motion_report(result, erExternalPerimeter));
    }
}

TEST_CASE("Gcode estimated motion survives result copying and resets between files", "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_motion_processor(processor);
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erExternalPerimeter) + "G1 X10 E0.5 F600\n");
    processor.finalize(false);
    const auto&          original = processor.get_result();
    GCodeProcessorResult copied;
    for (const auto& row : copied.normal_estimated_motion)
        REQUIRE_FALSE(row.complete);
    copied = original;
    REQUIRE(motion_report(copied, erExternalPerimeter) == motion_report(original, erExternalPerimeter));
    copied.reset();
    for (const auto& row : copied.normal_estimated_motion) {
        REQUIRE_FALSE(row.complete);
        REQUIRE(row.block_count == 0);
    }
    REQUIRE(original.normal_estimated_motion[erExternalPerimeter].block_count == 1);
    processor.reset();
    initialize_motion_processor(processor);
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erSupportMaterial) + "G1 X10 E0.5 F600\n");
    processor.finalize(false);
    const auto& reset = processor.get_result();
    REQUIRE(reset.normal_estimated_motion[erExternalPerimeter].complete);
    REQUIRE(reset.normal_estimated_motion[erExternalPerimeter].block_count == 0);
    REQUIRE(reset.normal_estimated_motion[erSupportMaterial].complete);
    REQUIRE(reset.normal_estimated_motion[erSupportMaterial].block_count == 1);
}

namespace {

void initialize_flow_processor(GCodeProcessor& processor, const std::vector<double>& diameters = {1.75, 2.85})
{
    PrintConfig config;
    config.gcode_flavor.value                         = gcfMarlinFirmware;
    config.filament_diameter.values                   = diameters;
    config.machine_max_acceleration_extruding.values  = {1000., 1000.};
    config.machine_max_acceleration_travel.values     = {1000., 1000.};
    config.machine_max_acceleration_retracting.values = {1000., 1000.};
    processor.apply_config(config);
    processor.initialize("slice-analysis-estimated-peak-flow.gcode");
}

json peak_flow_report(const GCodeProcessorResult& result, size_t material, double cap = 10.)
{
    Model              model;
    Print              print;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 2.85}));
    config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats({cap}));
    print.set_status_silent();
    print.apply(model, config);
    const auto rows = make_slice_analysis_report(print, result).at("gcode").at("extruders");
    for (const auto& row : rows)
        if (row.at("extruder_id") == material)
            return row.at("estimated_peak_flow");
    FAIL("Expected material row in the public report");
    return nullptr;
}

void require_peak_flow_unknown(const json& row)
{
    REQUIRE(row.at("scope") == "normal_estimator_finalized_linear_e_blocks");
    REQUIRE(row.at("complete") == false);
    REQUIRE(row.at("block_count").is_null());
    REQUIRE(row.at("peak_volumetric_rate_mm3_s").at("min").is_null());
    REQUIRE(row.at("peak_volumetric_rate_mm3_s").at("max").is_null());
    REQUIRE(row.at("max_to_configured_cap_ratio").is_null());
}

std::vector<const GCodeProcessorResult::MoveVertex*> positive_model_moves(const GCodeProcessorResult& result)
{
    std::vector<const GCodeProcessorResult::MoveVertex*> moves;
    for (const auto& move : result.moves)
        if (move.type == EMoveType::Extrude && move.delta_extruder > 0.f && move.extrusion_role != erCustom && move.extrusion_role != erNone)
            moves.push_back(&move);
    return moves;
}

} // namespace

TEST_CASE("Gcode estimated peak flow uses the same finalized block speed and linear E volume", "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_flow_processor(processor);
    std::string move          = "G1 X1 E0.05 F6000\n";
    double      expected_peak = std::sqrt(200.);
    SECTION("short extrusion never borrows the requested cruise peak") {}
    SECTION("long extrusion reaches the requested cruise peak")
    {
        move          = "G1 X100 E5 F6000\n";
        expected_peak = 100.;
    }
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter) + move + "G4 P0\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    REQUIRE(result.normal_estimated_peak_flow_available);
    const auto& flow  = result.normal_estimated_peak_flow.at(0);
    const auto  moves = positive_model_moves(result);
    REQUIRE(moves.size() == 1);
    REQUIRE(flow.complete);
    REQUIRE(flow.block_count == 1);
    const double analytical_flow = expected_peak * M_PI * 0.875 * 0.875 * 0.05;
    REQUIRE(flow.min_peak_volumetric_rate_mm3_s == Approx(analytical_flow));
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s == Approx(analytical_flow));
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s ==
            double(result.normal_estimated_motion[erExternalPerimeter].max_peak_speed_mm_s) * double(moves.front()->mm3_per_mm));
    const auto row = peak_flow_report(result, 0);
    REQUIRE(row.at("scope") == "normal_estimator_finalized_linear_e_blocks");
    REQUIRE(row.at("complete") == true);
    REQUIRE(row.at("block_count") == 1);
    REQUIRE(row.at("peak_volumetric_rate_mm3_s").at("max") == flow.max_peak_volumetric_rate_mm3_s);
    REQUIRE(row.at("max_to_configured_cap_ratio") == flow.max_peak_volumetric_rate_mm3_s / 10.);
    for (double cap : {0., -1., std::numeric_limits<double>::quiet_NaN()}) {
        const auto no_cap = peak_flow_report(result, 0, cap);
        REQUIRE(no_cap.at("complete") == true);
        REQUIRE(no_cap.at("peak_volumetric_rate_mm3_s") == row.at("peak_volumetric_rate_mm3_s"));
        REQUIRE(no_cap.at("max_to_configured_cap_ratio").is_null());
    }
}

TEST_CASE("Gcode estimated peak flow never multiplies maxima from different blocks", "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_flow_processor(processor);
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter) +
                             "G1 X100 E1 F6000\nG4 P0\nG1 Y10 F6000\nG1 X101 E0.2 F6000\nG4 P0\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    const auto  moves  = positive_model_moves(result);
    REQUIRE(moves.size() == 2);
    const auto& flow = result.normal_estimated_peak_flow.at(0);
    REQUIRE(flow.complete);
    REQUIRE(flow.block_count == 2);
    const double long_flow  = 100. * moves[0]->mm3_per_mm;
    const double short_flow = std::sqrt(200.) * moves[1]->mm3_per_mm;
    REQUIRE(flow.min_peak_volumetric_rate_mm3_s == Approx(long_flow));
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s == Approx(short_flow));
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s <
            double(result.normal_estimated_motion[erExternalPerimeter].max_peak_speed_mm_s) * moves[1]->mm3_per_mm);
}

TEST_CASE("Gcode estimated peak flow snapshots M221 before queued blocks are consumed and keeps zero valid",
          "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_flow_processor(processor);
    bool only_zero = false;
    SECTION("different queued multipliers are not replaced with the final multiplier") {}
    SECTION("zero flow remains a known numeric zero rather than no observation") { only_zero = true; }
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter) +
                             (only_zero ? "M221 S0\n" : "M221 S150\n") + "G1 X100 E1 F6000\n" + (only_zero ? "M221 S0\n" : "M221 S50\n") +
                             "G1 X200 E1 F6000\nM221 S0\nG1 X300 E1 F6000\n"
                             "M221 S200\nG4 P0\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    const auto  moves  = positive_model_moves(result);
    REQUIRE(moves.size() == 3);
    const auto& flow = result.normal_estimated_peak_flow.at(0);
    REQUIRE(flow.complete);
    REQUIRE(flow.block_count == 3);
    REQUIRE(flow.min_peak_volumetric_rate_mm3_s == 0.);
    // Keep the processor's float mm/min conversion, not a rounded 100 mm/s.
    const float parsed_peak = 6000.f * (1.f / 60.f);
    REQUIRE(moves.front()->feedrate == parsed_peak);
    REQUIRE(result.normal_estimated_motion[erExternalPerimeter].max_peak_speed_mm_s == parsed_peak);
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s ==
            (only_zero ? 0. : double(parsed_peak) * (double(moves.front()->mm3_per_mm) * 1.5)));
    const auto row = peak_flow_report(result, 0);
    REQUIRE(row.at("complete") == true);
    REQUIRE(row.at("block_count") == 3);
    REQUIRE(row.at("peak_volumetric_rate_mm3_s").at("min") == 0.);
    REQUIRE(row.at("max_to_configured_cap_ratio") == flow.max_peak_volumetric_rate_mm3_s / 10.);
}

TEST_CASE("Gcode estimated peak flow snapshots tool identity and diameter before tool-change synchronization",
          "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_flow_processor(processor);
    // The first T0 block is still alone when T1 synchronizes; it must retain material 0.
    processor.process_buffer(motion_preamble() + motion_role(erExternalPerimeter) + "G1 X1 E0.05 F6000\nT1\n" +
                             motion_role(erSupportMaterial) + "G1 X2 E0.05 F6000\nG4 P0\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    const auto  moves  = positive_model_moves(result);
    REQUIRE(moves.size() == 2);
    REQUIRE(moves[0]->extruder_id == 0);
    REQUIRE(moves[1]->extruder_id == 1);
    REQUIRE(result.normal_estimated_peak_flow.size() == 2);
    for (size_t material : {size_t(0), size_t(1)}) {
        const auto& flow = result.normal_estimated_peak_flow.at(material);
        REQUIRE(flow.complete);
        REQUIRE(flow.block_count == 1);
        const auto&  move   = *moves[material];
        const double radius = result.filament_diameters[material] / 2.;
        REQUIRE(move.mm3_per_mm == Approx(M_PI * radius * radius * move.delta_extruder / move.travel_dist));
        const double same_block_peak = result.normal_estimated_motion[move.extrusion_role].max_peak_speed_mm_s;
        REQUIRE(flow.max_peak_volumetric_rate_mm3_s == same_block_peak * double(move.mm3_per_mm));
        REQUIRE(peak_flow_report(result, material).at("peak_volumetric_rate_mm3_s").at("max") == flow.max_peak_volumetric_rate_mm3_s);
    }
    REQUIRE(moves[1]->mm3_per_mm > moves[0]->mm3_per_mm);
}

TEST_CASE("Gcode estimated peak flow keeps bad material independent of other material and role motion state",
          "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_flow_processor(processor);
    std::string invalid = "M221 S-100\n";
    SECTION("negative multiplier") {}
    SECTION("nonfinite multiplier") { invalid = "M221 Snan\n"; }
    SECTION("bad finalized acceleration in the same role") { invalid = "M204 P-1\n"; }
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erExternalPerimeter) + invalid +
                             "G1 X10 E0.5 F600\nG4 P0\nM221 S100\nM204 P100\nT1\nG1 X20 E0.5 F600\nG1 X30 F600\nG4 P0\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    REQUIRE_FALSE(result.normal_estimated_peak_flow.at(0).complete);
    require_peak_flow_unknown(peak_flow_report(result, 0));
    REQUIRE(result.normal_estimated_peak_flow.at(1).complete);
    REQUIRE(result.normal_estimated_peak_flow.at(1).block_count == 1);
    REQUIRE(peak_flow_report(result, 1).at("complete") == true);
}

TEST_CASE("Gcode estimated peak flow rejects invalid source filament diameters without changing legacy volume",
          "[SliceAnalysis][GCodeProcessor]")
{
    for (double diameter : {0., -1.75, std::numeric_limits<double>::quiet_NaN()}) {
        CAPTURE(diameter);
        GCodeProcessor processor(true);
        initialize_flow_processor(processor, {diameter, 2.85});
        processor.process_buffer(motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erExternalPerimeter) + "G1 X10 E0.5 F600\nG4 P0\n");
        processor.finalize(false);
        const auto& result = processor.get_result();
        REQUIRE_FALSE(result.normal_estimated_peak_flow.at(0).complete);
        require_peak_flow_unknown(peak_flow_report(result, 0));
        const auto moves = positive_model_moves(result);
        REQUIRE(moves.size() == 1);
        if (diameter == 0.)
            REQUIRE(moves.front()->mm3_per_mm == 0.f);
        else if (diameter < 0.)
            REQUIRE(moves.front()->mm3_per_mm > 0.f); // The old squared-diameter value is not silently changed.
    }
}

TEST_CASE("Gcode estimated peak flow does not pretend unsupported E or unit modes are measured", "[SliceAnalysis][GCodeProcessor]")
{
    std::string mode      = "M200 D1.75\n";
    bool        supported = false;
    SECTION("explicit M200 D0 keeps linear E supported")
    {
        mode      = "M200 D0\n";
        supported = true;
    }
    SECTION("a query without S is not a tool-specific multiplier change")
    {
        mode      = "M221 T1\n";
        supported = true;
    }
    SECTION("volumetric E is not implemented") {}
    SECTION("M200 without an explicit disabled mode remains unknown") { mode = "M200\n"; }
    SECTION("later D0 does not reinterpret an earlier unsupported mode") { mode = "M200 D1.75\nM200 D0\n"; }
    SECTION("tool-specific M221 is not the global multiplier") { mode = "M221 S150 T0\nM221 S100\n"; }
    SECTION("G21 cannot repair a previously inherited inch feedrate") { mode = "G20\nG21\n"; }
    GCodeProcessor processor(true);
    initialize_flow_processor(processor);
    processor.process_buffer(motion_preamble() + mode + "G1 Z0.2 F600\n" + motion_role(erExternalPerimeter) + "G1 X10 E0.5 F600\nG4 P0\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    REQUIRE(result.normal_estimated_motion[erExternalPerimeter].complete);
    REQUIRE(result.normal_estimated_peak_flow.at(0).complete == supported);
    if (supported) {
        REQUIRE(peak_flow_report(result, 0).at("complete") == true);
        REQUIRE(result.normal_estimated_peak_flow.at(0).block_count == 1);
    } else
        require_peak_flow_unknown(peak_flow_report(result, 0));
}

TEST_CASE("Gcode estimated peak flow unsupported state does not retroactively invalidate buffered earlier blocks",
          "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_flow_processor(processor);
    // M200 is observed while the old block is pending. Its already captured linear-E volume remains valid.
    processor.process_buffer(motion_preamble() + motion_role(erExternalPerimeter) + "G1 X1 E0.05 F600\nM200 D1.75\nT1\n" +
                             "G1 X2 E0.05 F600\nG4 P0\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    REQUIRE(result.normal_estimated_peak_flow.at(0).complete);
    REQUIRE(result.normal_estimated_peak_flow.at(0).block_count == 1);
    REQUIRE_FALSE(result.normal_estimated_peak_flow.at(1).complete);
    REQUIRE(peak_flow_report(result, 0).at("complete") == true);
    require_peak_flow_unknown(peak_flow_report(result, 1));
}

TEST_CASE("Gcode estimated peak flow uses true arc and helical distance and excludes negative E", "[SliceAnalysis][GCodeProcessor]")
{
    bool helical = false;
    SECTION("planar half-circle") {}
    SECTION("half-circle rising one millimeter") { helical = true; }
    GCodeProcessor processor(true);
    initialize_flow_processor(processor);
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F600\nM221 S150\n" + motion_role(erExternalPerimeter) +
                             (helical ? "G2 X10 Y0 Z1.2 I5 J0 E0.5 F600\n" : "G2 X10 Y0 I5 J0 E0.5 F600\n") +
                             "G3 X20 Y0 I5 J0 E-0.5 F600\nG4 P0\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    const auto  moves  = positive_model_moves(result);
    REQUIRE(moves.size() == 1);
    const auto& move = *moves.front();
    REQUIRE(move.is_arc_move());
    REQUIRE(move.travel_dist == Approx(std::sqrt(std::pow(5. * M_PI, 2) + (helical ? 1. : 0.))));
    REQUIRE(move.mm3_per_mm == Approx(M_PI * 0.875 * 0.875 * 0.5 / move.travel_dist));
    const auto& flow = result.normal_estimated_peak_flow.at(0);
    REQUIRE(flow.complete);
    REQUIRE(flow.block_count == 1);
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s ==
            double(result.normal_estimated_motion[erExternalPerimeter].max_peak_speed_mm_s) * (double(move.mm3_per_mm) * 1.5));
    REQUIRE(peak_flow_report(result, 0).at("block_count") == 1);
}

TEST_CASE("Gcode estimated peak flow follows absolute relative E and G92 without changing nominal consumption",
          "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor baseline(true);
    GCodeProcessor scaled_flow(true);
    initialize_flow_processor(baseline);
    initialize_flow_processor(scaled_flow);
    const std::string moves = "G1 Z0.2 F6000\nM82\n" + motion_role(erExternalPerimeter) +
                              "G1 X100 E1 F6000\nG92 E0\nG1 X200 E1 F6000\nM83\nG1 X300 E1 F6000\nG4 P0\n";
    baseline.process_buffer(motion_preamble() + "M221 S100\n" + moves);
    scaled_flow.process_buffer(motion_preamble() + "M221 S150\n" + moves);
    baseline.finalize(false);
    scaled_flow.finalize(false);
    const auto& a = baseline.get_result();
    const auto& b = scaled_flow.get_result();
    REQUIRE(a.normal_estimated_peak_flow.at(0).complete);
    REQUIRE(b.normal_estimated_peak_flow.at(0).complete);
    REQUIRE(a.normal_estimated_peak_flow.at(0).block_count == 3);
    REQUIRE(b.normal_estimated_peak_flow.at(0).block_count == 3);
    REQUIRE(b.normal_estimated_peak_flow.at(0).max_peak_volumetric_rate_mm3_s ==
            a.normal_estimated_peak_flow.at(0).max_peak_volumetric_rate_mm3_s * 1.5);
    REQUIRE(a.print_statistics.total_volumes_per_extruder == b.print_statistics.total_volumes_per_extruder);
    const auto mode = static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE(a.print_statistics.modes[mode].time == b.print_statistics.modes[mode].time);
    REQUIRE(a.moves.size() == b.moves.size());
    for (size_t index = 0; index < a.moves.size(); ++index) {
        REQUIRE(a.moves[index].position == b.moves[index].position);
        REQUIRE(a.moves[index].feedrate == b.moves[index].feedrate);
        REQUIRE(a.moves[index].mm3_per_mm == b.moves[index].mm3_per_mm);
    }
    const auto parsed_moves = positive_model_moves(b);
    REQUIRE(parsed_moves.size() == 3);
    for (const auto* move : parsed_moves)
        REQUIRE(move->delta_extruder == 1.f);
}

TEST_CASE("Gcode estimated peak flow consumes a refreshed planner queue only once", "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_flow_processor(processor);
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter));
    for (size_t index = 1; index <= 400; ++index)
        processor.process_buffer("G1 X" + std::to_string(index * 100) + " E1 F6000\n");
    processor.process_buffer("G4 P0\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    const auto& flow   = result.normal_estimated_peak_flow.at(0);
    REQUIRE(flow.complete);
    REQUIRE(flow.block_count == 400);
    REQUIRE(flow.min_peak_volumetric_rate_mm3_s == flow.max_peak_volumetric_rate_mm3_s);
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s == Approx(M_PI * 0.875 * 0.875));
    REQUIRE(peak_flow_report(result, 0).at("block_count") == 400);
}

TEST_CASE("Gcode estimated peak flow separates no eligible blocks from missing metadata and final residuals",
          "[SliceAnalysis][GCodeProcessor]")
{
    SECTION("custom and unclassified extrusion are known inapplicable observations")
    {
        GCodeProcessor processor(true);
        initialize_flow_processor(processor);
        processor.process_buffer(motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erCustom) + "G1 X10 E0.5 F600\n" + motion_role(erNone) +
                                 "G1 X20 E0.5 F600\nG4 P0\n");
        processor.finalize(false);
        const auto& result = processor.get_result();
        REQUIRE(result.normal_estimated_peak_flow_available);
        REQUIRE(result.normal_estimated_peak_flow.empty());
        const auto row = peak_flow_report(result, 0);
        REQUIRE(row.at("complete") == true);
        REQUIRE(row.at("block_count") == 0);
        REQUIRE(row.at("peak_volumetric_rate_mm3_s").at("min").is_null());
        REQUIRE(row.at("peak_volumetric_rate_mm3_s").at("max").is_null());
        REQUIRE(row.at("max_to_configured_cap_ratio").is_null());
    }
    SECTION("legacy results have no estimator flow metadata")
    {
        InitializedResult result;
        result.moves = {extrusion(0, 0.4f, 0.2f, 20.f, 0.08f)};
        require_peak_flow_unknown(peak_flow_report(result, 0));
    }
    SECTION("a final residual only invalidates its own material")
    {
        GCodeProcessor processor(true);
        initialize_flow_processor(processor);
        processor.process_buffer(motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erExternalPerimeter) +
                                 "G1 X10 E0.5 F600\nT1\nG1 X20 E0.5 F600\n");
        processor.finalize(false);
        const auto& result = processor.get_result();
        REQUIRE(result.normal_estimated_peak_flow_available);
        REQUIRE(result.normal_estimated_peak_flow.at(0).complete);
        REQUIRE(result.normal_estimated_peak_flow.at(0).block_count == 1);
        REQUIRE_FALSE(result.normal_estimated_peak_flow.at(1).complete);
        REQUIRE(peak_flow_report(result, 0).at("complete") == true);
        require_peak_flow_unknown(peak_flow_report(result, 1));
    }
}

TEST_CASE("Gcode estimated peak flow copies metadata and resets support state between files", "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_flow_processor(processor);
    processor.process_buffer(motion_preamble() + "M200 D1.75\nG1 Z0.2 F600\n" + motion_role(erExternalPerimeter) +
                             "G1 X10 E0.5 F600\nG4 P0\n");
    processor.finalize(false);
    GCodeProcessorResult copied;
    REQUIRE_FALSE(copied.normal_estimated_peak_flow_available);
    copied = processor.get_result();
    REQUIRE(copied.normal_estimated_peak_flow_available);
    require_peak_flow_unknown(peak_flow_report(copied, 0));
    processor.reset();
    initialize_flow_processor(processor);
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erExternalPerimeter) + "G1 X10 E0.5 F600\nG4 P0\n");
    processor.finalize(false);
    const auto& result = processor.get_result();
    REQUIRE(result.normal_estimated_peak_flow.at(0).complete);
    REQUIRE(result.normal_estimated_peak_flow.at(0).block_count == 1);
    copied = result;
    REQUIRE(peak_flow_report(copied, 0) == peak_flow_report(result, 0));
    copied.reset();
    REQUIRE_FALSE(copied.normal_estimated_peak_flow_available);
    REQUIRE(copied.normal_estimated_peak_flow.empty());
    REQUIRE(result.normal_estimated_peak_flow_available);
    REQUIRE(result.normal_estimated_peak_flow.at(0).block_count == 1);
}

TEST_CASE("Slice analysis observes cancellation while collecting one region of fill paths", "[SliceAnalysis][Cancellation]")
{
    struct CancelingPath : ExtrusionPath
    {
        Print&  print;
        size_t& visited;

        CancelingPath(const ExtrusionPath& path, Print& print, size_t& visited)
            : ExtrusionPath(path), print(print), visited(visited)
        {}

        void polygons_covered_by_width(Polygons& out, float epsilon) const override
        {
            ExtrusionPath::polygons_covered_by_width(out, epsilon);
            if (++visited == 17)
                print.cancel();
        }
    };

    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 1.2, 0.1);
    layer->lslices = {rectangle(0., 0., 10., 5.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    ExtrusionEntityCollection* fills = &region->fills;
    SECTION("direct fill paths") {}
    SECTION("nested fill paths") {
        auto* nested = new ExtrusionEntityCollection;
        region->fills.entities.push_back(nested);
        fills = nested;
    }
    size_t visited = 0;
    for (size_t index = 0; index < 4096; ++index) {
        const double y = 0.5 + 0.001 * index;
        // Direct ownership preserves the virtual offset hook; append() would
        // clone this as an ordinary ExtrusionPath and remove the test hook.
        fills->entities.push_back(new CancelingPath(planned_path(0., y, 10., y, 0.4f, erInternalInfill), print, visited));
    }
    REQUIRE(object->support_layers().empty());
    REQUIRE(layer->regions().size() == 1);
    REQUIRE(visited == 0);
    REQUIRE_FALSE(print.canceled());

    // The public report first visits this object's model paths. Cancellation
    // starts inside the seventeenth actual offset, not before report execution.
    REQUIRE_THROWS_AS(make_slice_analysis_report(print, InitializedResult{}), CanceledException);
    REQUIRE(print.canceled());
    REQUIRE(visited == 17);
}

TEST_CASE("Slice analysis measures dense external bridges over a single ladder footprint", "[SliceAnalysis][slow]")
{
    for (size_t count : {size_t(32), size_t(128)}) {
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        PrintObject* object = print.get_object(0);
        Layer* previous = object->add_layer(0, 0.2, 0.2, 0.1);
        Layer* current  = object->add_layer(1, 0.2, 0.4, 0.3);
        const double width = 2. * count - 1., height = count + 2.;
        previous->lslices = current->lslices = {rectangle(0., 0., width, height)};
        LayerRegion* lower = previous->add_region(&object->printing_region(0));
        LayerRegion* upper = current->add_region(&object->printing_region(0));
        // One connected footprint, with count - 1 rectangular holes. Every
        // bridge crosses all holes, exercising one complex clipping candidate.
        for (size_t index = 0; index < count; ++index) {
            const double x = 2. * index + 0.5;
            lower->fills.append(planned_path(x, 0., x, height, 1.f, erInternalInfill));
            const double y = 1.5 + index;
            upper->fills.append(planned_path(0.5, y, width - 0.5, y, 0.4f, erBridgeInfill));
        }
        lower->fills.append(planned_path(0., 0.5, width, 0.5, 1.f, erInternalInfill));
        lower->fills.append(planned_path(0., height - 0.5, width, height - 0.5, 1.f, erInternalInfill));
        const auto settings = all_settings(print);
        const auto lower_before = lower->fills.as_polylines();
        const auto upper_before = upper->fills.as_polylines();
        const Polygons lower_footprint = lower->fills.polygons_covered_by_width(0.f);
        const Polygons upper_footprint = upper->fills.polygons_covered_by_width(0.f);
        const ExPolygons ladder = union_ex(lower_footprint);
        REQUIRE(ladder.size() == 1);
        REQUIRE(ladder.front().holes.size() == count - 1);

        const auto started = std::chrono::steady_clock::now();
        const json report = make_slice_analysis_report(print, InitializedResult{});
        const double elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        const auto repeated_started = std::chrono::steady_clock::now();
        const json repeated = make_slice_analysis_report(print, InitializedResult{});
        const double repeated_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - repeated_started).count();
        std::cout << "dense external bridges: columns=" << count << " bridge_paths=" << count << " holes=" << count - 1
                  << " report_ms=" << elapsed_ms << " repeated_report_ms=" << repeated_ms << std::endl;

        const auto& measured = report.at("objects").at("groups").at(0).at("external_bridge_projection").at("layers").at(1);
        REQUIRE(measured.at("complete") == true);
        REQUIRE(measured.at("uncovered_run_count") == count * (count - 1));
        REQUIRE(measured.at("two_sided_run_count") == count * (count - 1));
        REQUIRE(measured.at("max_uncovered_run_mm").get<double>() == Approx(1.).epsilon(0.).margin(1e-12));
        REQUIRE(measured.at("max_two_sided_run_mm").get<double>() == Approx(1.).epsilon(0.).margin(1e-12));
        REQUIRE(repeated == report);
        REQUIRE(all_settings(print) == settings);
        REQUIRE(lower->fills.as_polylines() == lower_before);
        REQUIRE(upper->fills.as_polylines() == upper_before);
        REQUIRE(lower->fills.polygons_covered_by_width(0.f) == lower_footprint);
        REQUIRE(upper->fills.polygons_covered_by_width(0.f) == upper_footprint);
    }
}

namespace {

LayerRegion* joint_model_layer(PrintObject& object, double print_z, double height = 0.2)
{
    Layer* layer   = object.add_layer(int(object.layer_count()), height, print_z, print_z - height * 0.5);
    layer->lslices = {rectangle(-2., -2., 20., 8.)};
    return layer->add_region(&object.printing_region(0));
}

json joint_layers(const Print& print, size_t group = 0, size_t instance = 0)
{
    return make_slice_analysis_report(print, InitializedResult{})
        .at("objects")
        .at("groups")
        .at(group)
        .at("joint_lower_path_slab_coverage")
        .at("instances")
        .at(instance)
        .at("layers");
}

void require_joint_plane(const json& plane, double bottom, double area, double uncovered)
{
    REQUIRE(plane.at("complete") == true);
    REQUIRE(plane.at("bottom_z_mm").get<double>() == Approx(bottom).epsilon(0.).margin(1e-12));
    REQUIRE(plane.at("target_area_mm2").get<double>() == Approx(area).epsilon(0.).margin(1e-10));
    REQUIRE(plane.at("uncovered_area_mm2").get<double>() == Approx(uncovered).epsilon(0.).margin(1e-10));
    REQUIRE(plane.at("uncovered_area_mm2").get<double>() <= plane.at("target_area_mm2").get<double>());
}

void require_joint_bridge(const json& plane, size_t count, double maximum, size_t two_sided)
{
    const auto& bridge = plane.at("external_bridge");
    REQUIRE(bridge.at("complete") == true);
    REQUIRE(bridge.at("uncovered_run_count") == count);
    REQUIRE(bridge.at("two_sided_run_count") == two_sided);
    if (count == 0)
        REQUIRE(bridge.at("max_uncovered_run_mm").is_null());
    else
        REQUIRE(bridge.at("max_uncovered_run_mm").get<double>() == Approx(maximum).epsilon(0.).margin(1e-12));
    if (two_sided == 0)
        REQUIRE(bridge.at("max_two_sided_run_mm").is_null());
    else
        REQUIRE(bridge.at("max_two_sided_run_mm").get<double>() == Approx(maximum).epsilon(0.).margin(1e-12));
}

} // namespace

TEST_CASE("Joint lower path slabs distinguish contact, true gaps, overlap and same source plane", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    joint_model_layer(*object, 1.2)->fills.append(planned_path(0., 0., 4., 0., 2.f));
    double support_z = 1., uncovered = 0.;
    float  support_height = 0.2f;
    SECTION("top exactly touches target bottom") {}
    SECTION("one coordinate unit of real gap")
    {
        support_z -= SCALING_FACTOR;
        uncovered = 8.;
    }
    SECTION("positive nominal overlap") { support_z = 1.1; }
    SECTION("source begins at target bottom and has no thickness below it")
    {
        support_z      = 1.1;
        support_height = 0.1f;
        uncovered      = 8.;
    }
    SECTION("same source print plane is excluded")
    {
        support_z = 1.2;
        uncovered = 8.;
    }
    SECTION("source raw print Z is strictly lower even when quantized tops coincide")
    {
        support_z      = std::nextafter(1.2, 0.);
        support_height = 0.4f;
    }
    SECTION("a remote projected support does not contact this bottom")
    {
        support_z = 0.8;
        uncovered = 8.;
    }
    auto source   = planned_path(0., 0., 4., 0., 2.f, erSupportMaterialInterface);
    source.height = support_height;
    object->add_support_layer(0, 0, 0.2, support_z)->support_fills.append(source);
    const auto  report = make_slice_analysis_report(print, InitializedResult{});
    const auto& group  = report.at("objects").at("groups").at(0);
    const auto& metric = group.at("joint_lower_path_slab_coverage");
    REQUIRE(metric.at("scope") == "per_instance_lower_source_print_z_nominal_path_slabs_before_gcode");
    REQUIRE(metric.at("z_resolution_mm") == SCALING_FACTOR);
    const auto& layer = metric.at("instances").at(0).at("layers").at(0);
    REQUIRE(layer.at("complete") == true);
    REQUIRE(layer.at("bottom_planes").size() == 1);
    require_joint_plane(layer.at("bottom_planes").at(0), 1., 8., uncovered);
    require_joint_bridge(layer.at("bottom_planes").at(0), 0, 0., 0);
    // The old XY-only projection intentionally still matches the remote source.
    REQUIRE(group.at("planned_support_projection").at("instances").at(0).at("layers").at(0).at("unmatched_target_area_mm2") == 0.);
}

TEST_CASE("Joint lower path slabs union model and support without double counting", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    joint_model_layer(*object, 0.2)->fills.append(planned_path(0., 0., 2.5, 0., 2.f));
    joint_model_layer(*object, 0.4)->fills.append(planned_path(0., 0., 4., 0., 2.f, erBridgeInfill));
    SupportLayer* support = object->add_support_layer(0, 0, 0.2, 0.2);
    support->support_fills.append(planned_path(1.5, 0., 4., 0., 2.f, erSupportMaterial));
    support->support_fills.append(planned_path(1.5, 0., 4., 0., 2.f, erSupportMaterialInterface));
    const auto settings       = all_settings(print);
    const auto model_before   = object->layers().back()->regions().front()->fills.as_polylines();
    const auto support_before = support->support_fills.as_polylines();
    const auto layers         = joint_layers(print);
    require_joint_plane(layers.at(0).at("bottom_planes").at(0), 0., 5., 5.); // Bed is outside this scope.
    require_joint_plane(layers.at(1).at("bottom_planes").at(0), 0.2, 8., 0.);
    require_joint_bridge(layers.at(1).at("bottom_planes").at(0), 0, 0., 0);
    const auto old = external_bridge_layers(print).at(1);
    REQUIRE(old.at("uncovered_run_count") == 1);
    REQUIRE(old.at("max_uncovered_run_mm").get<double>() == Approx(1.5));
    REQUIRE(joint_layers(print) == layers);
    REQUIRE(all_settings(print) == settings);
    REQUIRE(object->layers().back()->regions().front()->fills.as_polylines() == model_before);
    REQUIRE(support->support_fills.as_polylines() == support_before);
}

TEST_CASE("Joint lower path slabs measure original bridge runs over combined lower material", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    auto*        lower  = joint_model_layer(*object, 0.2);
    lower->fills.append(planned_path(0., 0., 2., 0., 2.f));
    auto*         upper   = joint_model_layer(*object, 0.4);
    SupportLayer* support = object->add_support_layer(0, 0, 0.2, 0.2);
    support->support_fills.append(planned_path(10., 0., 12., 0., 2.f, erSupportMaterial));
    auto   bridge = planned_path(1., 0., 11., 0., 0.5f, erBridgeInfill);
    size_t count = 1, two_sided = 1;
    double maximum = 8., uncovered = 4.;
    bool   split = false;
    SECTION("model at one end and support at the other") {}
    SECTION("an intermediate support island divides the run")
    {
        support->support_fills.append(planned_path(5., 0., 7., 0., 2.f, erSupportMaterial));
        count = two_sided = 2;
        maximum           = 3.;
        uncovered         = 3.;
    }
    SECTION("polyline vertices do not split a continuous run")
    {
        bridge.polyline = Polyline::new_scale({{1., 0.}, {4., 0.}, {8., 0.}, {11., 0.}});
    }
    SECTION("original path fragments are not stitched")
    {
        upper->fills.append(planned_path(1., 0., 6., 0., 0.5f, erBridgeInfill));
        upper->fills.append(planned_path(6., 0., 11., 0., 0.5f, erBridgeInfill));
        split     = true;
        count     = 2;
        two_sided = 0;
        maximum   = 4.;
    }
    SECTION("centerline on the candidate boundary is not interior coverage")
    {
        bridge.polyline.translate(Point(0, scaled(1.)));
        two_sided = 0;
        maximum   = 10.;
        uncovered = 4.5;
    }
    if (!split)
        upper->fills.append(bridge);
    const auto plane = joint_layers(print).at(1).at("bottom_planes").at(0);
    require_joint_plane(plane, 0.2, 5., uncovered);
    require_joint_bridge(plane, count, maximum, two_sided);
}

TEST_CASE("Joint lower path slabs separate actual heights and merge identical quantized bottoms", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    auto*        upper  = joint_model_layer(*object, 1.2, 0.2);
    for (float height : {0.4f, 0.2f, std::nextafter(0.2f, 1.f)}) {
        auto path   = planned_path(0., 0., 4., 0., 2.f, erBridgeInfill);
        path.height = height;
        upper->fills.append(path);
    }
    object->add_support_layer(0, 0, 0.2, 0.8)->support_fills.append(planned_path(0., 0., 4., 0., 2.f, erSupportMaterial));
    const auto layer = joint_layers(print).at(0);
    REQUIRE(layer.at("complete") == true);
    const auto& planes = layer.at("bottom_planes");
    REQUIRE(planes.size() == 2);
    require_joint_plane(planes.at(0), 0.8, 8., 0.);
    require_joint_bridge(planes.at(0), 0, 0., 0);
    require_joint_plane(planes.at(1), 1., 8., 8.);
    require_joint_bridge(planes.at(1), 2, 4., 0);
}

TEST_CASE("Joint lower path slabs keep invalid target heights unknown without changing old coverage", "[SliceAnalysis]")
{
    const float height = GENERATE(0.f, -0.2f, 1.e-9f, std::numeric_limits<float>::quiet_NaN());
    Model       model;
    Print       print;
    add_box(model, 1);
    apply_model(print, model);
    auto* region = joint_model_layer(*print.get_object(0), 0.4);
    auto  path   = planned_path(0., 0., 4., 0., 2.f);
    path.height  = height;
    region->fills.append(path);
    const auto layer = joint_layers(print).at(0);
    REQUIRE(layer.at("complete") == false);
    REQUIRE(layer.at("bottom_planes").is_null());
    REQUIRE(coverage_layers(print).at(0).at("complete") == true);
}

TEST_CASE("Joint lower path slabs isolate an unknown source to possibly affected bottom planes", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    auto*        upper  = joint_model_layer(*object, 1.2);
    for (float height : {0.2f, 0.4f}) {
        auto path   = planned_path(0., 0., 4., 0., 2.f);
        path.height = height;
        upper->fills.append(path);
    }
    auto invalid   = planned_path(0., 0., 4., 0., 2.f, erSupportMaterial);
    invalid.height = std::numeric_limits<float>::quiet_NaN();
    object->add_support_layer(0, 0, 0.2, 0.8)->support_fills.append(invalid);
    const auto layer = joint_layers(print).at(0);
    REQUIRE(layer.at("complete") == false);
    REQUIRE(layer.at("bottom_planes").size() == 2);
    const auto& unknown = layer.at("bottom_planes").at(0);
    REQUIRE(unknown.at("complete") == false);
    REQUIRE(unknown.at("target_area_mm2").is_null());
    REQUIRE(unknown.at("uncovered_area_mm2").is_null());
    REQUIRE(unknown.at("external_bridge").at("complete") == false);
    for (const char* key : {"uncovered_run_count", "max_uncovered_run_mm", "two_sided_run_count", "max_two_sided_run_mm"})
        REQUIRE(unknown.at("external_bridge").at(key).is_null());
    require_joint_plane(layer.at("bottom_planes").at(1), 1., 8., 8.);
    REQUIRE(make_slice_analysis_report(print, InitializedResult{})
                .at("objects")
                .at("groups")
                .at(0)
                .at("planned_support_projection")
                .at("instances")
                .at(0)
                .at("layers")
                .at(0)
                .at("complete") == true);
}

TEST_CASE("Joint lower path slabs distinguish empty targets, closed bridges and invalid source Z", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    auto*        region = joint_model_layer(*object, 0.4);
    SECTION("empty target is measured and not an unknown or zero-sized plane")
    {
        const auto layer = joint_layers(print).at(0);
        REQUIRE(layer.at("complete") == true);
        REQUIRE(layer.at("bottom_planes") == json::array());
    }
    SECTION("closed bridge affects only the bridge child")
    {
        auto path     = planned_path(0., 0., 4., 0., 1.f, erBridgeInfill);
        path.polyline = Polyline::new_scale({{0., 0.}, {4., 0.}, {4., 4.}, {0., 0.}});
        region->fills.append(path);
        const auto layer = joint_layers(print).at(0);
        REQUIRE(layer.at("complete") == true);
        const auto& plane = layer.at("bottom_planes").at(0);
        REQUIRE(plane.at("complete") == true);
        REQUIRE(plane.at("target_area_mm2") == plane.at("uncovered_area_mm2"));
        REQUIRE(plane.at("external_bridge").at("complete") == false);
        REQUIRE(plane.at("external_bridge").at("uncovered_run_count").is_null());
    }
    SECTION("source with no orderable Z is not silently ignored")
    {
        region->fills.append(planned_path(0., 0., 4., 0., 2.f));
        object->add_support_layer(0, 0, 0.2, std::numeric_limits<double>::quiet_NaN())
            ->support_fills.append(planned_path(0., 0., 4., 0., 2.f, erSupportMaterial));
        const auto layer = joint_layers(print).at(0);
        REQUIRE(layer.at("complete") == false);
        REQUIRE(layer.at("bottom_planes").at(0).at("target_area_mm2").is_null());
    }
}

TEST_CASE("Joint lower path slabs reuse current shared raft at each actual instance location", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 2);
    add_box(model, 1)->instances.front()->set_offset(Vec3d(80., 20., 0.));
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    PrintObject* owner  = print.get_object(1);
    joint_model_layer(*object, 1.2)->fills.append(planned_path(0., 0., 4., 0., 2.f));
    object->add_support_layer(0, 0, 0.2, 1.)->raft_covered_by_shared_paths = true;
    SupportLayer* support                                                  = owner->add_support_layer(0, 0, 0.2, 1.);
    const Point   delta = object->instances().at(0).shift - owner->instances().at(0).shift;
    support->support_fills.append(planned_path(unscale<double>(delta.x()), unscale<double>(delta.y()), unscale<double>(delta.x()) + 4.,
                                               unscale<double>(delta.y()), 2.f, erSupportMaterialInterface));
    support->raft_merge_backup = std::make_unique<SupportLayer::RaftMergeBackup>();
    const Point backup_delta   = object->instances().at(1).shift - owner->instances().at(0).shift;
    support->raft_merge_backup->fills.append(planned_path(unscale<double>(backup_delta.x()), unscale<double>(backup_delta.y()),
                                                          unscale<double>(backup_delta.x()) + 4., unscale<double>(backup_delta.y()), 2.f,
                                                          erSupportMaterialInterface));
    for (size_t instance : {size_t(0), size_t(1)})
        require_joint_plane(joint_layers(print, 0, instance).at(0).at("bottom_planes").at(0), 1., 8., instance == 0 ? 0. : 8.);
    REQUIRE(support->raft_merge_backup->fills.entities.size() == 1);
}

TEST_CASE("Joint lower path slabs share width expansion with existing metrics and multiple instances", "[SliceAnalysis]")
{
    struct CountingPath : ExtrusionPath
    {
        size_t& visits;
        CountingPath(const ExtrusionPath& path, size_t& visits) : ExtrusionPath(path), visits(visits) {}
        void polygons_covered_by_width(Polygons& out, float epsilon) const override
        {
            ++visits;
            ExtrusionPath::polygons_covered_by_width(out, epsilon);
        }
    };
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    size_t       visits = 0;
    joint_model_layer(*object, 0.8)->fills.entities.push_back(new CountingPath(planned_path(0., 0., 2., 0., 2.f), visits));
    joint_model_layer(*object, 1.)->fills.entities.push_back(new CountingPath(planned_path(0., 0., 4., 0., 2.f, erBridgeInfill), visits));
    object->add_support_layer(0, 0, 0.2, 0.8)
        ->support_fills.entities.push_back(new CountingPath(planned_path(2., 0., 4., 0., 2.f, erSupportMaterial), visits));
    const auto report = make_slice_analysis_report(print, InitializedResult{});
    REQUIRE(visits == 3);
    const auto& metric = report.at("objects").at("groups").at(0).at("joint_lower_path_slab_coverage");
    REQUIRE(metric.at("instances").size() == 2);
    for (const auto& instance : metric.at("instances"))
        require_joint_plane(instance.at("layers").at(1).at("bottom_planes").at(0), 0.8, 8., 0.);
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(visits == 6); // Cache lifetime is one report, never persisted in Print.
}

namespace {

PrintConfig cap_distance_config(const std::vector<double>& caps)
{
    PrintConfig config;
    config.gcode_flavor.value                         = gcfMarlinFirmware;
    config.filament_diameter.values                   = {1.75, 2.85};
    config.filament_max_volumetric_speed.values       = caps;
    config.machine_max_acceleration_extruding.values  = {1000., 1000.};
    config.machine_max_acceleration_travel.values     = {1000., 1000.};
    config.machine_max_acceleration_retracting.values = {1000., 1000.};
    return config;
}

void process_cap_distance(GCodeProcessor& processor, const std::string& gcode, const std::vector<double>& caps)
{
    processor.apply_config(cap_distance_config(caps));
    processor.initialize("slice-analysis-flow-cap-distance.gcode");
    processor.process_buffer(gcode);
    processor.finalize(false);
}

void import_cap_distance(GCodeProcessor& processor, const std::string& gcode, const PrintConfig* config)
{
    struct TemporaryGcode
    {
        boost::filesystem::path path = boost::filesystem::temp_directory_path() /
                                       boost::filesystem::unique_path("orca-flow-cap-distance-%%%%-%%%%.gcode");
        ~TemporaryGcode()
        {
            boost::system::error_code ignored;
            boost::filesystem::remove(path, ignored);
        }
    } temporary;
    boost::nowide::ofstream file(temporary.path.string());
    REQUIRE(file.good());
    if (config != nullptr) {
        // Exercise the viewer's public import path, including dynamic config
        // deserialization. Its loader requires a complete-looking config block.
        REQUIRE(config->keys().size() >= 80);
        file << "; generated by OrcaSlicer\n; CONFIG_BLOCK_START\n";
        for (const auto& key : config->keys()) {
            file << "; " << key << " = ";
            const auto* enums = dynamic_cast<const ConfigOptionEnumsGeneric*>(config->option(key));
            if (enums != nullptr && enums->keys_map == nullptr) {
                // Raw static defaults carry values without generic enum maps;
                // prepare the same definitions used by export fixtures/presets.
                const auto* definition = print_config_def.get(key);
                REQUIRE(definition != nullptr);
                REQUIRE(definition->enum_keys_map != nullptr);
                ConfigOptionEnumsGeneric mapped(definition->enum_keys_map);
                mapped.values = enums->values;
                file << mapped.serialize();
            } else
                file << config->opt_serialize(key);
            file << '\n';
        }
        file << "; CONFIG_BLOCK_END\n";
    }
    file << gcode;
    file.close();
    REQUIRE(file.good());
    processor.process_file(temporary.path.string());
}

json cap_distance_report(const GCodeProcessorResult& result, size_t material, const std::vector<double>& caps)
{
    Model              model;
    Print              print;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 2.85}));
    config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats(caps));
    print.set_status_silent();
    print.apply(model, config);
    const auto rows = make_slice_analysis_report(print, result).at("gcode").at("extruders");
    for (const auto& row : rows)
        if (row.at("extruder_id") == material)
            return row.at("estimated_flow_cap_distance");
    FAIL("Expected material row for finalized phase-distance observation");
    return nullptr;
}

void require_cap_distance_unknown(const json& row)
{
    REQUIRE(row.at("scope") == "normal_estimator_finalized_linear_e_block_phases");
    REQUIRE(row.at("complete") == false);
    for (const char* field : {"participating_path_length_mm", "above_cap_path_length_mm", "above_cap_path_length_ratio"})
        REQUIRE(row.at(field).is_null());
}

void require_cap_distance(const json& row, double total, double above)
{
    REQUIRE(row.at("scope") == "normal_estimator_finalized_linear_e_block_phases");
    REQUIRE(row.at("complete") == true);
    REQUIRE(row.at("participating_path_length_mm") == total);
    REQUIRE(row.at("above_cap_path_length_mm") == above);
    if (total > 0.)
        REQUIRE(row.at("above_cap_path_length_ratio") == above / total);
    else
        REQUIRE(row.at("above_cap_path_length_ratio").is_null());
}

std::string cap_phase_program(const std::string& overrides, const std::string& moves)
{
    return motion_preamble() + "M205 X0 Y0 Z0 E1000\n" + overrides + "G1 Z0.2 F6000\n" + moves + "G4 P0\n";
}

void require_no_analysis_metadata(const GCodeProcessorResult& result)
{
    REQUIRE(result.layer_extruder_ids.empty());
    REQUIRE_FALSE(result.layer_extruder_ids_complete);
    REQUIRE_FALSE(result.normal_estimated_peak_flow_available);
    REQUIRE(result.normal_estimated_peak_flow.empty());
    REQUIRE(result.normal_estimated_flow_cap_distance.empty());
    for (const auto& motion : result.normal_estimated_motion) {
        REQUIRE_FALSE(motion.complete);
        REQUIRE(motion.block_count == 0);
        REQUIRE(motion.min_peak_speed_mm_s == 0.f);
        REQUIRE(motion.max_peak_speed_mm_s == 0.f);
        REQUIRE(motion.min_tangential_acceleration_mm_s2 == 0.f);
        REQUIRE(motion.max_tangential_acceleration_mm_s2 == 0.f);
    }
}

void require_same_legacy_gcode_observations(const GCodeProcessorResult& actual, const GCodeProcessorResult& expected)
{
    const auto& a = actual.print_statistics;
    const auto& b = expected.print_statistics;
    REQUIRE(a.volumes_per_color_change == b.volumes_per_color_change);
    REQUIRE(a.model_volumes_per_extruder == b.model_volumes_per_extruder);
    REQUIRE(a.wipe_tower_volumes_per_extruder == b.wipe_tower_volumes_per_extruder);
    REQUIRE(a.support_volumes_per_extruder == b.support_volumes_per_extruder);
    REQUIRE(a.total_volumes_per_extruder == b.total_volumes_per_extruder);
    REQUIRE(a.flush_per_filament == b.flush_per_filament);
    REQUIRE(a.used_filaments_per_role == b.used_filaments_per_role);
    REQUIRE(a.total_filamentchanges == b.total_filamentchanges);
    for (size_t index = 0; index < a.modes.size(); ++index) {
        REQUIRE(a.modes[index].time == b.modes[index].time);
        REQUIRE(a.modes[index].prepare_time == b.modes[index].prepare_time);
        REQUIRE(a.modes[index].custom_gcode_times == b.modes[index].custom_gcode_times);
        REQUIRE(a.modes[index].moves_times == b.modes[index].moves_times);
        REQUIRE(a.modes[index].roles_times == b.modes[index].roles_times);
        REQUIRE(a.modes[index].layers_times == b.modes[index].layers_times);
    }
    REQUIRE(actual.moves.size() == expected.moves.size());
    for (size_t index = 0; index < actual.moves.size(); ++index) {
        const auto& x = actual.moves[index];
        const auto& y = expected.moves[index];
        REQUIRE(x.gcode_id == y.gcode_id);
        REQUIRE(x.type == y.type);
        REQUIRE(x.extrusion_role == y.extrusion_role);
        REQUIRE(x.extruder_id == y.extruder_id);
        REQUIRE(x.cp_color_id == y.cp_color_id);
        REQUIRE(x.position == y.position);
        REQUIRE(x.delta_extruder == y.delta_extruder);
        REQUIRE(x.feedrate == y.feedrate);
        REQUIRE(x.width == y.width);
        REQUIRE(x.height == y.height);
        REQUIRE(x.mm3_per_mm == y.mm3_per_mm);
        // Travel distance is new analysis metadata. Existing preview consumers
        // use this field only for extrusion, whose original value must not change.
        if (x.type == EMoveType::Extrude)
            REQUIRE(x.travel_dist == y.travel_dist);
        REQUIRE(x.fan_speed == y.fan_speed);
        REQUIRE(x.temperature == y.temperature);
        REQUIRE(x.time == y.time);
        REQUIRE(x.layer_duration == y.layer_duration);
        REQUIRE(x.move_path_type == y.move_path_type);
        REQUIRE(x.arc_center_position == y.arc_center_position);
        REQUIRE(x.interpolation_points == y.interpolation_points);
    }
}

} // namespace

TEST_CASE("Gcode analysis collection is opt in and leaves ordinary preview time and material observations unchanged",
          "[SliceAnalysis][GCodeProcessor][AnalysisCollection]")
{
    bool import_file = false;
    bool long_queue  = false;
    SECTION("streaming export with linear and native arc paths") {}
    SECTION("file import obeys the constructor option") { import_file = true; }
    SECTION("long planner queues retain their original estimates without analysis") { long_queue = true; }
    const auto  config  = cap_distance_config({1., 2.});
    std::string program = cap_phase_program("", motion_role(erExternalPerimeter) +
                                                    "G1 X10 E0.5 F600\nG1 X13 Y4 F1200\nG2 X15 Y4 I1 J0 E0.1 F600\n");
    program += "T1\n" + motion_role(erSupportMaterial) + "G1 X25 E0.5 F1200\n";
    if (long_queue)
        for (size_t index = 26; index <= 425; ++index)
            program += "G1 X" + std::to_string(index) + " E0.05 F6000\n";
    program += "G1 X430 F6000\nM400\n";
    GCodeProcessor default_processor;
    GCodeProcessor disabled(false);
    GCodeProcessor enabled(true);
    for (auto* processor : {&default_processor, &disabled, &enabled}) {
        if (import_file)
            import_cap_distance(*processor, program, &config);
        else
            process_cap_distance(*processor, program, {1., 2.});
    }
    const auto& observed = enabled.get_result();
    REQUIRE(observed.normal_estimated_peak_flow_available);
    REQUIRE(observed.normal_estimated_motion[erExternalPerimeter].block_count > 0);
    REQUIRE(observed.normal_estimated_peak_flow.at(0).block_count > 0);
    REQUIRE(observed.normal_estimated_flow_cap_distance.at(0).participating_path_length_mm > 0.);
    REQUIRE_FALSE(observed.layer_extruder_ids.empty());
    for (const auto* processor : {&default_processor, &disabled}) {
        require_no_analysis_metadata(processor->get_result());
        require_same_legacy_gcode_observations(processor->get_result(), observed);
    }
}

TEST_CASE("Gcode analysis collection survives reset without leaking previous file metadata",
          "[SliceAnalysis][GCodeProcessor][AnalysisCollection]")
{
    GCodeProcessor    default_processor;
    GCodeProcessor    disabled(false);
    GCodeProcessor    enabled(true);
    const std::string first  = cap_phase_program("", "T1\n" + motion_role(erSupportMaterial) + "G1 X10 E0.5 F600\nG1 X20 F1200\n");
    const std::string second = cap_phase_program("", "T0\n" + motion_role(erExternalPerimeter) + "G1 X2 E0.1 F600\nG1 X3 F1200\n");
    for (auto* processor : {&default_processor, &disabled, &enabled}) {
        process_cap_distance(*processor, first, {1., 2.});
        if (processor == &enabled) {
            REQUIRE(processor->get_result().normal_estimated_motion[erSupportMaterial].block_count == 1);
            REQUIRE(processor->get_result().normal_estimated_peak_flow.at(1).block_count == 1);
        } else
            require_no_analysis_metadata(processor->get_result());
        processor->reset();
        require_no_analysis_metadata(processor->get_result());
        process_cap_distance(*processor, second, {1000., 2000.});
    }
    const auto& observed = enabled.get_result();
    REQUIRE(observed.normal_estimated_peak_flow_available);
    REQUIRE(observed.normal_estimated_motion[erExternalPerimeter].block_count == 1);
    REQUIRE(observed.normal_estimated_motion[erSupportMaterial].block_count == 0);
    REQUIRE(observed.normal_estimated_peak_flow.size() == 1);
    REQUIRE(observed.normal_estimated_peak_flow.at(0).block_count == 1);
    REQUIRE(observed.normal_estimated_flow_cap_distance.at(0).configured_cap_mm3_s == 1000.);
    REQUIRE(observed.normal_estimated_flow_cap_distance.at(0).participating_path_length_mm == 2.);
    REQUIRE(observed.normal_estimated_flow_cap_distance.at(1).participating_path_length_mm == 0.);
    for (const auto& ids : observed.layer_extruder_ids)
        REQUIRE(std::find(ids.begin(), ids.end(), 1) == ids.end());
    for (const auto* processor : {&default_processor, &disabled}) {
        require_no_analysis_metadata(processor->get_result());
        require_same_legacy_gcode_observations(processor->get_result(), observed);
    }
}

TEST_CASE("Gcode flow-cap distance integrates triangle and trapezoid portions rather than flagging whole blocks",
          "[SliceAnalysis][GCodeProcessor]")
{
    std::string overrides = "M204 P1\nM203 X2\n";
    std::string move      = "G1 X4 E0.2 F120\n";
    double      peak = 2., threshold_speed = 1., total = 4., above = 3.;
    SECTION("triangle accelerates and decelerates over two millimeters each") {}
    SECTION("trapezoid has four millimeter ramps and two millimeters of cruise")
    {
        overrides       = "M204 P2\nM203 X4\n";
        move            = "G1 X10 E0.5 F240\n";
        peak            = 4.;
        threshold_speed = 2.;
        total           = 10.;
        above           = 8.;
    }
    const std::string gcode = cap_phase_program(overrides, motion_role(erExternalPerimeter) + move);
    GCodeProcessor    reference(true);
    process_cap_distance(reference, gcode, {1000.});
    const auto& original = reference.get_result();
    const auto  moves    = positive_model_moves(original);
    REQUIRE(moves.size() == 1);
    REQUIRE(original.normal_estimated_motion[erExternalPerimeter].max_peak_speed_mm_s == peak);
    const double   cap = double(moves[0]->mm3_per_mm) * threshold_speed;
    GCodeProcessor processor(true);
    process_cap_distance(processor, gcode, {cap});
    const auto& result = processor.get_result();
    require_cap_distance(cap_distance_report(result, 0, {cap}), total, above);
    REQUIRE(above < total);
    REQUIRE(result.normal_estimated_peak_flow.at(0).max_peak_volumetric_rate_mm3_s ==
            original.normal_estimated_peak_flow.at(0).max_peak_volumetric_rate_mm3_s);
    REQUIRE(result.moves.size() == original.moves.size());
    for (size_t index = 0; index < result.moves.size(); ++index) {
        REQUIRE(result.moves[index].position == original.moves[index].position);
        REQUIRE(result.moves[index].feedrate == original.moves[index].feedrate);
        REQUIRE(result.moves[index].mm3_per_mm == original.moves[index].mm3_per_mm);
    }
    const auto mode = static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE(result.print_statistics.modes[mode].time == original.print_statistics.modes[mode].time);
    REQUIRE(result.print_statistics.total_volumes_per_extruder == original.print_statistics.total_volumes_per_extruder);
}

TEST_CASE("Gcode flow-cap distance equality uses the finalized rounded peak as its strict upper bound", "[SliceAnalysis][GCodeProcessor]")
{
    const std::string gcode = motion_preamble() + "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter) + "G1 X1 E0.05 F6000\nG4 P0\n";
    GCodeProcessor    reference(true);
    process_cap_distance(reference, gcode, {1000.});
    const auto& result = reference.get_result();
    REQUIRE(result.normal_estimated_motion[erExternalPerimeter].max_peak_speed_mm_s == std::sqrt(200.f));
    const double peak_flow = result.normal_estimated_peak_flow.at(0).max_peak_volumetric_rate_mm3_s;
    for (double cap : {peak_flow, peak_flow * 2.}) {
        GCodeProcessor processor(true);
        process_cap_distance(processor, gcode, {cap});
        require_cap_distance(cap_distance_report(processor.get_result(), 0, {cap}), 1., 0.);
    }
    const double   cap = peak_flow * 0.9;
    GCodeProcessor below(true);
    process_cap_distance(below, gcode, {cap});
    const auto row = cap_distance_report(below.get_result(), 0, {cap});
    REQUIRE(row.at("complete") == true);
    REQUIRE(row.at("above_cap_path_length_mm").get<double>() > 0.);
    REQUIRE(row.at("above_cap_path_length_mm").get<double>() < 1.);
}

TEST_CASE("Gcode flow-cap distance treats isolated acceleration and deceleration phases independently", "[SliceAnalysis][GCodeProcessor]")
{
    std::string moves;
    float       acceleration = 2.f;
    SECTION("a one millimeter initial block only accelerates from zero to two")
    {
        moves = motion_role(erExternalPerimeter) + "G1 X1 E0.05 F240\n" + motion_role(erCustom) + "G1 X11 E0.5 F240\n";
    }
    SECTION("a one millimeter final block only decelerates from two to zero")
    {
        moves        = motion_role(erCustom) + "G1 X10 E0.5 F240\n" + motion_role(erExternalPerimeter) + "G1 X11 E0.05 F240\n";
        acceleration = -2.f;
    }
    const std::string gcode = cap_phase_program("M204 P2\nM203 X4\n", moves);
    GCodeProcessor    reference(true);
    process_cap_distance(reference, gcode, {1000.});
    const auto& motion = reference.get_result().normal_estimated_motion[erExternalPerimeter];
    REQUIRE(motion.block_count == 1);
    REQUIRE(motion.min_peak_speed_mm_s == 2.f);
    REQUIRE(motion.min_tangential_acceleration_mm_s2 == acceleration);
    REQUIRE(motion.max_tangential_acceleration_mm_s2 == acceleration);
    const auto positive = positive_model_moves(reference.get_result());
    REQUIRE(positive.size() == 1);
    const double   cap = positive.front()->mm3_per_mm; // speed threshold exactly 1 mm/s.
    GCodeProcessor processor(true);
    process_cap_distance(processor, gcode, {cap});
    require_cap_distance(cap_distance_report(processor.get_result(), 0, {cap}), 1., 0.75);
}

TEST_CASE("Gcode flow-cap distance constant speed and zero multiplier remain numeric observations", "[SliceAnalysis][GCodeProcessor]")
{
    bool zero_flow         = false;
    bool zero_acceleration = false;
    SECTION("constant cruise above the cap participates in full") {}
    SECTION("zero acceleration constant cruise does not divide by zero") { zero_acceleration = true; }
    SECTION("zero M221 participates in the denominator without exceeding") { zero_flow = true; }
    const std::string gcode = motion_preamble() + "M205 X1000 Y1000 Z1000 E1000\nM203 X4\n" + (zero_acceleration ? "M201 X0\n" : "") +
                              "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter) + (zero_flow ? "M221 S0\n" : "") +
                              "G1 X10 E0.5 F240\nG4 P0\n";
    GCodeProcessor processor(true);
    process_cap_distance(processor, gcode, {0.01});
    const auto& motion = processor.get_result().normal_estimated_motion[erExternalPerimeter];
    REQUIRE(motion.min_peak_speed_mm_s == 4.f);
    REQUIRE(motion.min_tangential_acceleration_mm_s2 == 0.f);
    REQUIRE(motion.max_tangential_acceleration_mm_s2 == 0.f);
    require_cap_distance(cap_distance_report(processor.get_result(), 0, {0.01}), 10., zero_flow ? 0. : 10.);
}

TEST_CASE("Gcode flow-cap distance applies each material cap and captured multiplier without mixing blocks",
          "[SliceAnalysis][GCodeProcessor]")
{
    const std::string gcode = motion_preamble() + "M205 X1000 Y1000 Z1000 E1000\nM203 X4\nG1 Z0.2 F6000\n" +
                              motion_role(erExternalPerimeter) +
                              "M221 S150\nG1 X10 E0.5 F240\nM221 S0\nG1 X20 E0.5 F240\n"
                              "M221 S50\nG1 X30 E0.5 F240\nT1\nM221 S100\nG1 X40 E0.5 F240\nG1 Y1 F240\nG4 P0\n";
    GCodeProcessor reference(true);
    process_cap_distance(reference, gcode, {1000.});
    const auto moves = positive_model_moves(reference.get_result());
    REQUIRE(moves.size() == 4);
    const double cap0 = 4. * double(moves[0]->mm3_per_mm);
    const double cap1 = 4. * double(moves[3]->mm3_per_mm);
    REQUIRE(cap1 > cap0);
    GCodeProcessor processor(true);
    process_cap_distance(processor, gcode, {cap0, cap1});
    require_cap_distance(cap_distance_report(processor.get_result(), 0, {cap0, cap1}), 30., 10.);
    require_cap_distance(cap_distance_report(processor.get_result(), 1, {cap0, cap1}), 10., 0.);
}

TEST_CASE("Gcode flow-cap distance shares first-cap fallback between static dynamic and report configurations",
          "[SliceAnalysis][GCodeProcessor]")
{
    const std::string gcode = motion_preamble() + "G1 Z0.2 F6000\nT1\n" + motion_role(erSupportMaterial) +
                              "G1 X10 E0.5 F6000\nG1 Y1 F6000\nG4 P0\n";
    const auto     config = cap_distance_config({0.01});
    GCodeProcessor from_static(true);
    process_cap_distance(from_static, gcode, {0.01});
    GCodeProcessor from_dynamic(true);
    import_cap_distance(from_dynamic, gcode, &config);
    REQUIRE(from_static.get_result().normal_estimated_flow_cap_distance.at(1).configured_cap_mm3_s == 0.01);
    REQUIRE(from_dynamic.get_result().normal_estimated_flow_cap_distance.at(1).configured_cap_mm3_s == 0.01);
    REQUIRE(cap_distance_report(from_static.get_result(), 1, {0.01}) == cap_distance_report(from_dynamic.get_result(), 1, {0.01}));
    REQUIRE(cap_distance_report(from_static.get_result(), 1, {0.01}).at("complete") == true);
    require_cap_distance_unknown(cap_distance_report(from_static.get_result(), 1, {0.02}));
    REQUIRE(peak_flow_report(from_static.get_result(), 1, 0.02).at("complete") == true);
}

TEST_CASE("Gcode flow-cap distance leaves missing invalid and changed cap metadata unknown independently of raw peak",
          "[SliceAnalysis][GCodeProcessor]")
{
    const std::string gcode = motion_preamble() + "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter) + "G1 X10 E0.5 F6000\nG4 P0\n";
    for (const auto& caps : std::vector<std::vector<double>>{{}, {0.}, {-1.}, {std::numeric_limits<double>::quiet_NaN()}}) {
        GCodeProcessor processor(true);
        process_cap_distance(processor, gcode, caps);
        require_cap_distance_unknown(cap_distance_report(processor.get_result(), 0, {10.}));
        REQUIRE(processor.get_result().normal_estimated_peak_flow.at(0).complete);
    }
    GCodeProcessor missing(true);
    // A generic G-code has no configured cap. An Orca config block instead
    // inherits full defaults, so omitting one key there would not mean unknown.
    import_cap_distance(missing, gcode, nullptr);
    require_cap_distance_unknown(cap_distance_report(missing.get_result(), 0, {10.}));
    REQUIRE(missing.get_result().normal_estimated_peak_flow.at(0).complete);
    InitializedResult legacy;
    legacy.moves = {extrusion(0, 0.4f, 0.2f, 20.f, 0.08f)};
    require_cap_distance_unknown(cap_distance_report(legacy, 0, {10.}));
    GCodeProcessor valid(true);
    process_cap_distance(valid, gcode, {10.});
    REQUIRE(cap_distance_report(valid.get_result(), 0, {10.}).at("complete") == true);
    require_cap_distance_unknown(cap_distance_report(valid.get_result(), 0, {20.}));
    require_cap_distance_unknown(cap_distance_report(valid.get_result(), 0, {}));
}

TEST_CASE("Gcode flow-cap distance separates unused material from unsupported volume and final pending material",
          "[SliceAnalysis][GCodeProcessor]")
{
    SECTION("custom unclassified and negative E do not enter the denominator")
    {
        GCodeProcessor processor(true);
        process_cap_distance(processor,
                             motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erCustom) + "G1 X10 E0.5 F600\n" + motion_role(erNone) +
                                 "G1 X20 E0.5 F600\n" + motion_role(erExternalPerimeter) + "G1 X30 E-0.5 F600\nG4 P0\n",
                             {10.});
        require_cap_distance(cap_distance_report(processor.get_result(), 0, {10.}), 0., 0.);
        REQUIRE(processor.get_result().normal_estimated_flow_cap_distance.at(1).complete);
        REQUIRE(processor.get_result().normal_estimated_flow_cap_distance.at(1).participating_path_length_mm == 0.);
    }
    SECTION("unsupported later material does not contaminate an earlier buffered material")
    {
        GCodeProcessor processor(true);
        process_cap_distance(processor,
                             motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erExternalPerimeter) +
                                 "G1 X10 E0.5 F600\nT1\nM200 D1.75\nG1 X20 E0.5 F600\nG1 Y1 F600\nG4 P0\n",
                             {10.});
        REQUIRE(cap_distance_report(processor.get_result(), 0, {10.}).at("complete") == true);
        require_cap_distance_unknown(cap_distance_report(processor.get_result(), 1, {10.}));
    }
    SECTION("final pending block only invalidates its own material")
    {
        GCodeProcessor processor(true);
        process_cap_distance(processor,
                             motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erExternalPerimeter) +
                                 "G1 X10 E0.5 F600\nT1\nG1 X20 E0.5 F600\n",
                             {10.});
        REQUIRE(cap_distance_report(processor.get_result(), 0, {10.}).at("complete") == true);
        require_cap_distance_unknown(cap_distance_report(processor.get_result(), 1, {10.}));
    }
}

TEST_CASE("Gcode flow-cap distance uses arc-aware participating distance and strict phase bounds", "[SliceAnalysis][GCodeProcessor]")
{
    std::string arc = "G3 X10 Y10 I0 J10 E0.5 F600\n";
    SECTION("planar quarter circle") {}
    SECTION("helical quarter circle") { arc = "G3 X10 Y10 Z2.2 I0 J10 E0.5 F600\n"; }
    GCodeProcessor processor(true);
    process_cap_distance(processor, motion_preamble() + "G1 Z0.2 F600\n" + motion_role(erExternalPerimeter) + arc + "G1 X20 F600\nG4 P0\n",
                         {1000.});
    const auto moves = positive_model_moves(processor.get_result());
    REQUIRE(moves.size() == 1);
    REQUIRE(moves.front()->is_arc_move());
    REQUIRE(moves.front()->travel_dist > std::sqrt(200.f));
    require_cap_distance(cap_distance_report(processor.get_result(), 0, {1000.}), moves.front()->travel_dist, 0.);
}

TEST_CASE("Gcode flow-cap distance consumes long planner queues exactly once and survives copying and reset",
          "[SliceAnalysis][GCodeProcessor]")
{
    const std::string prefix = motion_preamble() + "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter);
    std::string       moves;
    for (size_t index = 1; index <= 400; ++index)
        moves += "G1 X" + std::to_string(index) + " E0.05 F6000\n";
    GCodeProcessor whole(true);
    process_cap_distance(whole, prefix + moves + "G4 P0\n", {1.});
    GCodeProcessor chunked(true);
    chunked.apply_config(cap_distance_config({1.}));
    chunked.initialize("slice-analysis-flow-cap-chunks.gcode");
    chunked.process_buffer(prefix);
    for (size_t index = 1; index <= 400; ++index)
        chunked.process_buffer("G1 X" + std::to_string(index) + " E0.05 F6000\n");
    chunked.process_buffer("G4 P0\n");
    chunked.finalize(false);
    const auto row = cap_distance_report(whole.get_result(), 0, {1.});
    REQUIRE(row.at("complete") == true);
    REQUIRE(row.at("participating_path_length_mm") == 400.);
    REQUIRE(row.at("above_cap_path_length_mm").get<double>() > 0.);
    REQUIRE(row.at("above_cap_path_length_mm").get<double>() <= 400.);
    REQUIRE(row == cap_distance_report(chunked.get_result(), 0, {1.}));
    const auto mode = static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE(whole.get_result().print_statistics.modes[mode].time == chunked.get_result().print_statistics.modes[mode].time);
    GCodeProcessorResult copied;
    REQUIRE(copied.normal_estimated_flow_cap_distance.empty());
    copied = whole.get_result();
    REQUIRE(cap_distance_report(copied, 0, {1.}) == row);
    copied.reset();
    REQUIRE(copied.normal_estimated_flow_cap_distance.empty());
    whole.reset();
    REQUIRE(whole.get_result().normal_estimated_flow_cap_distance.empty());
    process_cap_distance(whole, prefix + "G1 X2 E0.1 F6000\nG4 P0\n", {1000.});
    require_cap_distance(cap_distance_report(whole.get_result(), 0, {1000.}), 2., 0.);
}

namespace {

LayerRegion* bed_region_layer(PrintObject& object, double print_z = 0.2)
{
    Layer* layer   = object.add_layer(0, 0.2, print_z, 0.1);
    layer->lslices = {rectangle(0., 0., 10., 10.)};
    return layer->add_region(&object.printing_region(0));
}

void require_bed_region(const json& region, double area, const Point& shift, double x0, double y0, double x1, double y1)
{
    REQUIRE(region.size() == 2);
    REQUIRE(region.at("area_mm2").get<double>() == Approx(area));
    const Point minimum = shift + Point(scaled(x0), scaled(y0));
    const Point maximum = shift + Point(scaled(x1), scaled(y1));
    REQUIRE(region.at("bounds_mm").size() == 2);
    REQUIRE(region.at("bounds_mm").at("min") == json::array({unscale<double>(minimum.x()), unscale<double>(minimum.y())}));
    REQUIRE(region.at("bounds_mm").at("max") == json::array({unscale<double>(maximum.x()), unscale<double>(maximum.y())}));
}

void require_bed_region_sums(const json& bed)
{
    REQUIRE(bed.at("scope") == "planned_path_nominal_bottom_on_bed_before_gcode");
    for (const auto& keys :
         {std::make_pair("model_regions", "model_area_mm2"), std::make_pair("support_and_raft_regions", "support_and_raft_area_mm2"),
          std::make_pair("brim_regions", "brim_area_mm2"), std::make_pair("combined_regions", "combined_area_mm2")}) {
        if (!bed.at("complete").get<bool>()) {
            REQUIRE(bed.at(keys.first).is_null());
            REQUIRE(bed.at(keys.second).is_null());
            continue;
        }
        REQUIRE(bed.at(keys.first).is_array());
        double area = 0.;
        for (const auto& region : bed.at(keys.first)) {
            REQUIRE(region.at("area_mm2").get<double>() > 0.);
            area += region.at("area_mm2").get<double>();
            for (size_t axis = 0; axis < 2; ++axis) {
                REQUIRE(std::isfinite(region.at("bounds_mm").at("min").at(axis).get<double>()));
                REQUIRE(region.at("bounds_mm").at("min").at(axis).get<double>() < region.at("bounds_mm").at("max").at(axis).get<double>());
            }
        }
        REQUIRE(area == bed.at(keys.second).get<double>());
    }
}

} // namespace

TEST_CASE("Bed footprint regions reuse each category union and keep overlap out of the combined area", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    bed_region_layer(object)->fills.append(planned_path(0., 2., 4., 2., 4.f));
    object.add_support_layer(0, 0, 0.2, 0.2)->support_fills.append(planned_path(2., 2., 6., 2., 4.f, erSupportMaterial));
    ExtrusionPath brim  = planned_path(4., 2., 8., 2., 4.f, erBrim);
    const Point   shift = object.instances().front().shift;
    brim.polyline.translate(shift);
    print.get_brimMap()[object.id()].append(brim);
    const json bed = make_slice_analysis_report(print, InitializedResult{}).at("planned_plate_bed_footprint");
    REQUIRE(bed.at("complete") == true);
    for (const char* key : {"model_regions", "support_and_raft_regions", "brim_regions", "combined_regions"})
        REQUIRE(bed.at(key).size() == 1);
    require_bed_region(bed.at("model_regions").at(0), 16., shift, 0., 0., 4., 4.);
    require_bed_region(bed.at("support_and_raft_regions").at(0), 16., shift, 2., 0., 6., 4.);
    require_bed_region(bed.at("brim_regions").at(0), 16., shift, 4., 0., 8., 4.);
    require_bed_region(bed.at("combined_regions").at(0), 32., shift, 0., 0., 8., 4.);
    require_bed_region_sums(bed);
}

TEST_CASE("Bed footprint region area excludes holes and keeps nested material islands separate", "[SliceAnalysis]")
{
    const bool inner_island = GENERATE(false, true);
    Model      model;
    Print      print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    auto&        fills  = bed_region_layer(object)->fills;
    // Outer 10x10 square minus [3,7]x[3,7], made from four exact butt-ended strips.
    fills.append(planned_path(0., 1.5, 10., 1.5, 3.f));
    fills.append(planned_path(0., 8.5, 10., 8.5, 3.f));
    fills.append(planned_path(1.5, 3., 1.5, 7., 3.f));
    fills.append(planned_path(8.5, 3., 8.5, 7., 3.f));
    if (inner_island)
        fills.append(planned_path(4., 5., 6., 5., 2.f));
    const json bed = make_slice_analysis_report(print, InitializedResult{}).at("planned_plate_bed_footprint");
    REQUIRE(bed.at("model_regions").size() == (inner_island ? 2 : 1));
    REQUIRE(bed.at("combined_regions") == bed.at("model_regions"));
    const Point shift = object.instances().front().shift;
    for (const auto& region : bed.at("model_regions")) {
        if (region.at("area_mm2").get<double>() > 50.)
            require_bed_region(region, 84., shift, 0., 0., 10., 10.);
        else {
            REQUIRE(inner_island);
            require_bed_region(region, 4., shift, 4., 4., 6., 6.);
        }
    }
    REQUIRE(bed.at("model_area_mm2").get<double>() == Approx(inner_island ? 88. : 84.));
    require_bed_region_sums(bed);
}

TEST_CASE("Bed footprint regions distinguish separate instances from coincident copies", "[SliceAnalysis]")
{
    const bool   coincident = GENERATE(false, true);
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 2);
    if (coincident)
        source->instances.back()->set_offset(source->instances.front()->get_offset());
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    bed_region_layer(object)->fills.append(planned_path(0., 2., 4., 2., 4.f));
    const json bed = make_slice_analysis_report(print, InitializedResult{}).at("planned_plate_bed_footprint");
    REQUIRE(bed.at("model_regions").size() == (coincident ? 1 : 2));
    REQUIRE(bed.at("combined_regions") == bed.at("model_regions"));
    for (const auto& region : bed.at("model_regions")) {
        const auto first_min = json::array(
            {unscale<double>(object.instances().front().shift.x()), unscale<double>(object.instances().front().shift.y())});
        const Point shift = region.at("bounds_mm").at("min") == first_min ? object.instances().front().shift :
                                                                            object.instances().back().shift;
        require_bed_region(region, 16., shift, 0., 0., 4., 4.);
    }
    require_bed_region_sums(bed);
}

TEST_CASE("Bed footprint bounds retain negative world placement without a second brim or plate transform", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1)->instances.front()->set_offset(Vec3d(-40., -30., 0.));
    print.set_plate_origin(Vec3d(300., 200., 0.));
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    const Point  shift  = object.instances().front().shift;
    REQUIRE(shift.x() < 0);
    REQUIRE(shift.y() < 0);
    REQUIRE(object.instances().front().shift_without_plate_offset() != shift);
    bed_region_layer(object)->fills.append(planned_path(0., 2., 4., 2., 4.f));
    ExtrusionPath brim = planned_path(4., 2., 8., 2., 4.f, erBrim);
    brim.polyline.translate(shift);
    print.get_brimMap()[object.id()].append(brim);
    const json bed = make_slice_analysis_report(print, InitializedResult{}).at("planned_plate_bed_footprint");
    REQUIRE(bed.at("model_regions").size() == 1);
    REQUIRE(bed.at("brim_regions").size() == 1);
    REQUIRE(bed.at("combined_regions").size() == 1);
    require_bed_region(bed.at("model_regions").at(0), 16., shift, 0., 0., 4., 4.);
    require_bed_region(bed.at("brim_regions").at(0), 16., shift, 4., 0., 8., 4.);
    require_bed_region(bed.at("combined_regions").at(0), 32., shift, 0., 0., 8., 4.);
    require_bed_region_sums(bed);
}

TEST_CASE("Bed footprint regions use actual path heights and current shared raft rather than backups", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    add_box(model, 1)->instances.front()->set_offset(Vec3d(80., 20., 0.));
    apply_model(print, model);
    PrintObject& covered = *print.get_object(0);
    PrintObject& owner   = *print.get_object(1);
    bed_region_layer(covered, 0.6)->fills.append(planned_path(0., 2., 4., 2., 4.f));
    bed_region_layer(owner, 0.6);
    covered.add_support_layer(0, 0, 0.1, 0.3)->raft_covered_by_shared_paths = true;
    SupportLayer* support                                                   = owner.add_support_layer(0, 0, 0.1, 0.3);
    const Point   delta = covered.instances().front().shift - owner.instances().front().shift;
    ExtrusionPath tall  = planned_path(0., 2., 4., 2., 4.f, erSupportMaterial);
    tall.height         = 0.3f;
    tall.polyline.translate(delta);
    support->support_fills.append(tall);
    ExtrusionPath short_path = planned_path(10., 2., 14., 2., 4.f, erSupportMaterial);
    short_path.height        = 0.1f;
    support->support_fills.append(short_path);
    support->raft_merge_backup = std::make_unique<SupportLayer::RaftMergeBackup>();
    support->raft_merge_backup->fills.append(planned_path(20., 2., 24., 2., 4.f, erSupportMaterial));
    const auto backup = support->raft_merge_backup->fills.as_polylines();
    const json bed    = make_slice_analysis_report(print, InitializedResult{}).at("planned_plate_bed_footprint");
    REQUIRE(bed.at("model_regions").empty());
    REQUIRE(bed.at("brim_regions").empty());
    REQUIRE(bed.at("support_and_raft_regions").size() == 1);
    REQUIRE(bed.at("combined_regions") == bed.at("support_and_raft_regions"));
    require_bed_region(bed.at("support_and_raft_regions").at(0), 16., covered.instances().front().shift, 0., 0., 4., 4.);
    REQUIRE(support->raft_merge_backup->fills.as_polylines() == backup);
    require_bed_region_sums(bed);
}

TEST_CASE("Bed footprint regions preserve known empty and whole-observation unknown", "[SliceAnalysis]")
{
    SECTION("no objects is known empty")
    {
        const json bed = make_slice_analysis_report(Print{}, InitializedResult{}).at("planned_plate_bed_footprint");
        REQUIRE(bed.at("complete") == true);
        for (const char* key : {"model_regions", "support_and_raft_regions", "brim_regions", "combined_regions"})
            REQUIRE(bed.at(key) == json::array());
        require_bed_region_sums(bed);
    }
    SECTION("missing centerline, zero flow and elevated paths do not manufacture positive regions")
    {
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        auto&         fills         = bed_region_layer(*print.get_object(0))->fills;
        ExtrusionPath no_centerline = planned_path(0., 0., 1., 0., 4.f);
        no_centerline.polyline.points.resize(1);
        fills.append(no_centerline);
        ExtrusionPath zero = planned_path(0., 2., 4., 2., 4.f);
        zero.mm3_per_mm    = 0.;
        fills.append(zero);
        ExtrusionPath elevated = planned_path(10., 2., 14., 2., 4.f);
        elevated.height        = 0.1f;
        fills.append(elevated);
        const json bed = make_slice_analysis_report(print, InitializedResult{}).at("planned_plate_bed_footprint");
        REQUIRE(bed.at("complete") == true);
        for (const char* key : {"model_regions", "support_and_raft_regions", "brim_regions", "combined_regions"})
            REQUIRE(bed.at(key) == json::array());
        require_bed_region_sums(bed);
    }
    SECTION("one invalid category keeps all four new arrays unknown")
    {
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        PrintObject& object = *print.get_object(0);
        bed_region_layer(object)->fills.append(planned_path(0., 2., 4., 2., 4.f));
        ExtrusionPath invalid = planned_path(10., 2., 14., 2., -1.f, erSupportMaterial);
        object.add_support_layer(0, 0, 0.2, 0.2)->support_fills.append(invalid);
        const json bed = make_slice_analysis_report(print, InitializedResult{}).at("planned_plate_bed_footprint");
        REQUIRE(bed.at("complete") == false);
        require_bed_region_sums(bed);
        REQUIRE(json::parse(bed.dump()) == bed);
    }
}

TEST_CASE("Bed footprint region serialization adds no width expansion and leaves paths and settings unchanged", "[SliceAnalysis]")
{
    struct CountingPath : ExtrusionPath
    {
        size_t& visits;
        CountingPath(const ExtrusionPath& path, size_t& visits) : ExtrusionPath(path), visits(visits) {}
        void polygons_covered_by_width(Polygons& out, float epsilon) const override
        {
            ++visits;
            ExtrusionPath::polygons_covered_by_width(out, epsilon);
        }
    };
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    size_t visits = 0;
    auto&  fills  = bed_region_layer(*print.get_object(0))->fills;
    fills.entities.push_back(new CountingPath(planned_path(0., 2., 4., 2., 4.f), visits));
    const auto paths    = fills.as_polylines();
    const auto settings = all_settings(print);
    const json report   = make_slice_analysis_report(print, InitializedResult{});
    // Existing model-layer cache once plus existing bed collector once, not per
    // instance and not again for either category/combined region serialization.
    REQUIRE(visits == 2);
    REQUIRE(report.at("planned_plate_bed_footprint").at("model_regions").size() == 2);
    require_bed_region_sums(report.at("planned_plate_bed_footprint"));
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(visits == 4);
    REQUIRE(fills.as_polylines() == paths);
    REQUIRE(all_settings(print) == settings);
    print.cancel();
    REQUIRE_THROWS_AS(make_slice_analysis_report(print, InitializedResult{}), CanceledException);
}

namespace {

LayerRegion* connectivity_layer(PrintObject& object, double bottom, double top, const ExPolygons& targets)
{
    Layer* layer        = object.add_layer(object.layer_count(), top - bottom, top, 0.5 * (bottom + top));
    layer->lslices      = targets;
    LayerRegion* region = layer->add_region(&object.printing_region(0));
    region->slices.set(targets, stInternal);
    return region;
}

void connectivity_rectangle(LayerRegion& region, double x, double y, double width, double height, float path_height)
{
    ExtrusionPath path = planned_path(x, y + height * 0.5, x + width, y + height * 0.5, float(height));
    path.height        = path_height;
    region.fills.append(path);
}

json nominal_connectivity(const Print& print, size_t group = 0)
{
    return make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(group).at("nominal_target_path_connectivity");
}

void require_connectivity_contract(const json& observed)
{
    REQUIRE(observed.at("scope") == "per_instance_compensated_region_section_and_model_path_slabs");
    REQUIRE(observed.at("coordinate_frame") == "print_object_local_xy_and_print_z");
    REQUIRE(observed.at("z_resolution_mm") == SCALING_FACTOR);
    if (!observed.at("complete").get<bool>()) {
        REQUIRE(observed.at("target_components").is_null());
        REQUIRE(observed.at("cross_target_joins").is_null());
        return;
    }
    const auto& components = observed.at("target_components");
    for (size_t index = 0; index < components.size(); ++index) {
        const json& component = components.at(index);
        REQUIRE(component.at("component_index") == index);
        const double target  = component.at("target_volume_mm3").get<double>();
        const double missing = component.at("uncovered_volume_mm3").get<double>();
        REQUIRE(std::isfinite(target));
        REQUIRE(target > 0.);
        REQUIRE(std::isfinite(missing));
        REQUIRE(missing >= 0.);
        REQUIRE(missing <= target);
        REQUIRE(component.at("missing_slab_regions").empty() == (missing == 0.));
        const auto& bounds = component.at("bounds_mm");
        for (size_t axis = 0; axis < 3; ++axis) {
            REQUIRE(std::isfinite(bounds.at("min").at(axis).get<double>()));
            REQUIRE(std::isfinite(bounds.at("max").at(axis).get<double>()));
            REQUIRE(bounds.at("min").at(axis).get<double>() < bounds.at("max").at(axis).get<double>());
        }
        double missing_sum = 0.;
        for (const auto& region : component.at("missing_slab_regions")) {
            const double volume = region.at("volume_mm3").get<double>();
            REQUIRE(std::isfinite(volume));
            REQUIRE(volume > 0.);
            missing_sum += volume;
            for (size_t axis = 0; axis < 3; ++axis) {
                REQUIRE(region.at("bounds_mm").at("min").at(axis).get<double>() >= bounds.at("min").at(axis).get<double>());
                REQUIRE(region.at("bounds_mm").at("max").at(axis).get<double>() <= bounds.at("max").at(axis).get<double>());
                REQUIRE(region.at("bounds_mm").at("min").at(axis).get<double>() < region.at("bounds_mm").at("max").at(axis).get<double>());
            }
        }
        REQUIRE(missing_sum == Approx(missing)); // Summation order is not a serialized identity.
    }
    std::set<size_t> material_indices;
    for (const auto& join : observed.at("cross_target_joins")) {
        REQUIRE(material_indices.insert(join.at("path_component_index").get<size_t>()).second);
        const auto references = join.at("target_component_indices").get<std::vector<size_t>>();
        REQUIRE(references.size() >= 2);
        REQUIRE(std::set<size_t>(references.begin(), references.end()).size() == references.size());
        for (size_t reference : references) {
            REQUIRE(reference < components.size());
            REQUIRE(components.at(reference).at("clipped_path_component_count").get<size_t>() > 0);
        }
    }
}

} // namespace

TEST_CASE("Nominal connectivity locates a missing neck while preserving the two covered ends", "[SliceAnalysis][Connectivity]")
{
    const bool include_neck = GENERATE(false, true);
    Model      model;
    Print      print;
    add_box(model, 1);
    apply_model(print, model);
    LayerRegion* region = connectivity_layer(*print.get_object(0), 0., 1.,
                                             {rectangle(0., 0., 4., 4.), rectangle(8., 0., 4., 4.), rectangle(4., 1.75, 4., 0.5)});
    connectivity_rectangle(*region, 0., 0., 4., 4., 1.f);
    connectivity_rectangle(*region, 8., 0., 4., 4., 1.f);
    if (include_neck)
        connectivity_rectangle(*region, 4., 1.75, 4., 0.5, 1.f);
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("complete") == true);
    REQUIRE(observed.at("target_components").size() == 1);
    const auto& target = observed.at("target_components").at(0);
    REQUIRE(target.at("target_volume_mm3").get<double>() == Approx(34.));
    REQUIRE(target.at("clipped_path_component_count") == (include_neck ? 1 : 2));
    REQUIRE(target.at("uncovered_volume_mm3").get<double>() == Approx(include_neck ? 0. : 2.));
    REQUIRE(observed.at("cross_target_joins").empty());
    if (!include_neck) {
        REQUIRE(target.at("missing_slab_regions").size() == 1);
        REQUIRE(target.at("missing_slab_regions").at(0).at("bounds_mm").at("min") == json::array({4., 1.75, 0.}));
        REQUIRE(target.at("missing_slab_regions").at(0).at("bounds_mm").at("max") == json::array({8., 2.25, 1.}));
    }
}

TEST_CASE("Nominal connectivity distinguishes partial branch loss from a wholly uncovered target", "[SliceAnalysis][Connectivity]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    LayerRegion* region = connectivity_layer(*print.get_object(0), 0., 1.,
                                             {rectangle(0., 0., 4., 4.), rectangle(4., 1.5, 4., 1.), rectangle(20., 0., 2., 2.)});
    connectivity_rectangle(*region, 0., 0., 4., 4., 1.f);
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("target_components").size() == 2);
    for (const auto& target : observed.at("target_components")) {
        REQUIRE(target.at("uncovered_volume_mm3").get<double>() == Approx(4.));
        REQUIRE(target.at("missing_slab_regions").size() == 1);
        if (target.at("target_volume_mm3").get<double>() > 10.) {
            REQUIRE(target.at("target_volume_mm3").get<double>() == Approx(20.));
            REQUIRE(target.at("clipped_path_component_count") == 1);
            REQUIRE(target.at("missing_slab_regions").at(0).at("bounds_mm").at("min") == json::array({4., 1.5, 0.}));
        } else {
            REQUIRE(target.at("target_volume_mm3").get<double>() == Approx(4.));
            REQUIRE(target.at("clipped_path_component_count") == 0);
        }
    }
}

TEST_CASE("Nominal connectivity keeps material outside targets and above their final layer for cross-target joins",
          "[SliceAnalysis][Connectivity]")
{
    const int bridge = GENERATE(0, 1, 2);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    LayerRegion* region = connectivity_layer(object, 0., 1., {rectangle(0., 0., 2., 2.), rectangle(3., 0., 2., 2.)});
    connectivity_rectangle(*region, 0., 0., 2., 2., 1.f);
    connectivity_rectangle(*region, 3., 0., 2., 2., 1.f);
    if (bridge == 1)
        connectivity_rectangle(*region, 2., 0., 1., 2., 1.f);
    else if (bridge == 2) {
        LayerRegion* later = connectivity_layer(object, 1., 2., {});
        connectivity_rectangle(*later, 0., 0., 5., 2., 1.f);
    }
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("target_components").size() == 2);
    for (const auto& target : observed.at("target_components")) {
        REQUIRE(target.at("target_volume_mm3").get<double>() == Approx(4.));
        REQUIRE(target.at("uncovered_volume_mm3") == 0.);
        REQUIRE(target.at("clipped_path_component_count") == 1);
    }
    REQUIRE(observed.at("cross_target_joins").size() == (bridge == 0 ? 0 : 1));
    if (bridge != 0)
        REQUIRE(observed.at("cross_target_joins").at(0).at("target_component_indices") == json::array({0, 1}));
}

TEST_CASE("Target-internal split does not claim material is disconnected outside the target", "[SliceAnalysis][Connectivity]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    LayerRegion* region = connectivity_layer(*print.get_object(0), 0., 1.,
                                             {rectangle(0., 0., 4., 4.), rectangle(8., 0., 4., 4.), rectangle(4., 1.75, 4., 0.5)});
    connectivity_rectangle(*region, 0., 0., 4., 4., 1.f);
    connectivity_rectangle(*region, 8., 0., 4., 4., 1.f);
    connectivity_rectangle(*region, 0., -1., 12., 1., 1.f); // Outside T, joined by vertical faces to both ends.
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("target_components").size() == 1);
    REQUIRE(observed.at("target_components").at(0).at("clipped_path_component_count") == 2);
    REQUIRE(observed.at("target_components").at(0).at("uncovered_volume_mm3").get<double>() == Approx(2.));
    REQUIRE(observed.at("cross_target_joins").empty()); // Only one target, irrespective of its internal split.
}

TEST_CASE("Nominal connectivity separates point touches but joins positive vertical faces", "[SliceAnalysis][Connectivity]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    ExPolygons targets;
    size_t     expected        = 2;
    double     expected_volume = 8.;
    SECTION("two squares share a vertical face")
    {
        targets  = {rectangle(0., 0., 2., 2.), rectangle(2., 0., 2., 2.)};
        expected = 1;
    }
    SECTION("two squares only share a vertical edge") { targets = {rectangle(0., 0., 2., 2.), rectangle(2., 2., 2., 2.)}; }
    SECTION("one weak contour contains two point-touching squares")
    {
        targets = {ExPolygon(
            Polygon({Point(0, 0), Point(scaled(2.), 0), Point(scaled(2.), scaled(2.)), Point(scaled(4.), scaled(2.)),
                     Point(scaled(4.), scaled(4.)), Point(scaled(2.), scaled(4.)), Point(scaled(2.), scaled(2.)), Point(0, scaled(2.))}))};
    }
    SECTION("a triangle vertex touches the middle of a square edge")
    {
        targets         = {rectangle(0., 0., 2., 2.),
                           ExPolygon(Polygon({Point(scaled(2.), scaled(1.)), Point(scaled(3.), scaled(0.5)), Point(scaled(3.), scaled(1.5))}))};
        expected_volume = 4.5;
    }
    connectivity_layer(*print.get_object(0), 0., 1., targets);
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("target_components").size() == expected);
    double volume = 0.;
    for (const auto& target : observed.at("target_components")) {
        volume += target.at("target_volume_mm3").get<double>();
        REQUIRE(target.at("clipped_path_component_count") == 0);
        REQUIRE(target.at("uncovered_volume_mm3") == target.at("target_volume_mm3"));
    }
    REQUIRE(volume == Approx(expected_volume));
}

TEST_CASE("Nominal connectivity requires positive horizontal interfaces and does not bridge empty Z intervals",
          "[SliceAnalysis][Connectivity]")
{
    const int scenario = GENERATE(0, 1, 2, 3, 4);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    double       x      = scenario == 0 ? 1. : scenario == 4 ? 2. - SCALING_FACTOR : 2.;
    double       y      = scenario == 2 ? 2. : 0.;
    double       gap    = scenario == 3 ? SCALING_FACTOR : 0.;
    if (scenario == 3)
        x = 0.;
    LayerRegion* first = connectivity_layer(object, 0., 1., {rectangle(0., 0., 2., 2.)});
    connectivity_rectangle(*first, 0., 0., 2., 2., 1.f);
    LayerRegion* second = connectivity_layer(object, 1. + gap, 2. + gap, {rectangle(x, y, 2., 2.)});
    connectivity_rectangle(*second, x, y, 2., 2., 1.f);
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("target_components").size() == (scenario == 0 || scenario == 4 ? 1 : 2));
    for (const auto& target : observed.at("target_components")) {
        REQUIRE(target.at("clipped_path_component_count") == 1);
        REQUIRE(target.at("uncovered_volume_mm3") == 0.);
    }
}

TEST_CASE("Nominal connectivity does not count boundary-only coverage or join corner-touching path cells", "[SliceAnalysis][Connectivity]")
{
    const int scenario = GENERATE(0, 1, 2);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    LayerRegion* region = connectivity_layer(*print.get_object(0), 0., 1., {rectangle(0., 0., 4., 4.)});
    if (scenario == 0)
        connectivity_rectangle(*region, 4., 0., 2., 2., 1.f); // Only a target boundary is touched.
    else {
        connectivity_rectangle(*region, 0., 0., 2., 2., 1.f);
        connectivity_rectangle(*region, 2., scenario == 1 ? 2. : 0., 2., 2., 1.f);
    }
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("complete") == true);
    REQUIRE(observed.at("target_components").size() == 1);
    const auto& target = observed.at("target_components").at(0);
    REQUIRE(target.at("target_volume_mm3").get<double>() == Approx(16.));
    REQUIRE(target.at("uncovered_volume_mm3").get<double>() == Approx(scenario == 0 ? 16. : 8.));
    REQUIRE(target.at("clipped_path_component_count") == (scenario == 0 ? 0 : scenario == 1 ? 2 : 1));
    REQUIRE(observed.at("cross_target_joins").empty());
}

TEST_CASE("Three-dimensional connectivity permits an overlapping lateral chain unlike fixed-XY depth", "[SliceAnalysis][Connectivity]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    for (size_t index = 0; index < 3; ++index) {
        LayerRegion* region = connectivity_layer(*print.get_object(0), double(index), double(index + 1),
                                                 {rectangle(double(index), 0., 2., 1.)});
        connectivity_rectangle(*region, double(index), 0., 2., 1., 1.f);
    }
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("target_components").size() == 1);
    REQUIRE(observed.at("target_components").at(0).at("clipped_path_component_count") == 1);
    REQUIRE(observed.at("target_components").at(0).at("target_volume_mm3").get<double>() == Approx(6.));
    REQUIRE(observed.at("target_components").at(0).at("uncovered_volume_mm3") == 0.);
}

TEST_CASE("Nominal connectivity unions overlapping slabs and uses actual path height", "[SliceAnalysis][Connectivity]")
{
    const int scenario = GENERATE(0, 1, 2, 3);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object          = *print.get_object(0);
    double       expected_target = 4., expected_missing = 0.;
    if (scenario == 0) {
        LayerRegion* region = connectivity_layer(object, 0., 1., {rectangle(0., 0., 2., 2.)});
        connectivity_rectangle(*region, 0., 0., 2., 2., 0.5f);
        expected_missing = 2.;
    } else if (scenario == 1) {
        // Target exists only in the second retained layer, but older actual
        // path height covers the lower half of its nominal target slab.
        connectivity_rectangle(*connectivity_layer(object, 0., 0.5, {}), 0., 0., 2., 2., 0.5f);
        connectivity_rectangle(*connectivity_layer(object, 0., 1., {rectangle(0., 0., 2., 2.)}), 0., 0., 2., 2., 0.5f);
    } else if (scenario == 2) {
        connectivity_rectangle(*connectivity_layer(object, 0., 1., {rectangle(0., 0., 2., 2.)}), 0., 0., 2., 2., 1.f);
        connectivity_rectangle(*connectivity_layer(object, 0.5, 1.5, {rectangle(0., 0., 2., 2.)}), 0., 0., 2., 2., 1.f);
        expected_target = 6.; // Overlap is not added twice.
    } else {
        for (const auto interval : {std::pair<double, double>{0., 0.25}, {0.25, 0.75}, {0.75, 1.}})
            connectivity_rectangle(*connectivity_layer(object, interval.first, interval.second, {rectangle(0., 0., 2., 2.)}), 0., 0., 2.,
                                   2., float(interval.second - interval.first));
    }
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("target_components").size() == 1);
    const auto& target = observed.at("target_components").at(0);
    REQUIRE(target.at("target_volume_mm3").get<double>() == Approx(expected_target));
    REQUIRE(target.at("uncovered_volume_mm3").get<double>() == Approx(expected_missing));
    REQUIRE(target.at("clipped_path_component_count") == 1);
    if (scenario == 0) {
        REQUIRE(target.at("missing_slab_regions").size() == 1);
        REQUIRE(target.at("missing_slab_regions").at(0).at("bounds_mm").at("max").at(2) == 0.5);
    }
}

TEST_CASE("Nominal connectivity counts overlapping regions once and leaves the old section report unchanged",
          "[SliceAnalysis][Connectivity]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject& object = *print.get_object(0);
    LayerRegion* region = connectivity_layer(object, 0., 1., {rectangle(0., 0., 2., 2.)});
    connectivity_rectangle(*region, 0., 0., 3., 2., 1.f);
    LayerRegion* other = region->layer()->add_region(&object.printing_region(0));
    other->slices.set({rectangle(1., 0., 2., 2.)}, stInternal);
    json       original = make_slice_analysis_report(print, InitializedResult{});
    const json observed = original.at("objects").at("groups").at(0).at("nominal_target_path_connectivity");
    require_connectivity_contract(observed);
    REQUIRE(observed.at("target_components").size() == 1);
    REQUIRE(observed.at("target_components").at(0).at("target_volume_mm3").get<double>() == Approx(6.));
    REQUIRE(observed.at("target_components").at(0).at("uncovered_volume_mm3") == 0.);
    other->slices.set({rectangle(0., 0., 2., 2.)}, stInternal);
    json changed = make_slice_analysis_report(print, InitializedResult{});
    REQUIRE(changed.at("objects")
                .at("groups")
                .at(0)
                .at("nominal_target_path_connectivity")
                .at("target_components")
                .at(0)
                .at("target_volume_mm3")
                .get<double>() == Approx(4.));
    original["objects"]["groups"][0].erase("nominal_target_path_connectivity");
    changed["objects"]["groups"][0].erase("nominal_target_path_connectivity");
    REQUIRE(changed == original);
}

TEST_CASE("Nominal connectivity subtracts holes and distinguishes a nested target from material crossing its gap",
          "[SliceAnalysis][Connectivity]")
{
    const bool bridge = GENERATE(false, true);
    Model      model;
    Print      print;
    add_box(model, 1);
    apply_model(print, model);
    LayerRegion* region = connectivity_layer(*print.get_object(0), 0., 1., {square_with_hole(), rectangle(4., 4., 2., 2.)});
    connectivity_rectangle(*region, 0., 0., 10., 3., 1.f);
    connectivity_rectangle(*region, 0., 7., 10., 3., 1.f);
    connectivity_rectangle(*region, 0., 3., 3., 4., 1.f);
    connectivity_rectangle(*region, 7., 3., 3., 4., 1.f);
    connectivity_rectangle(*region, 4., 4., 2., 2., 1.f);
    if (bridge)
        connectivity_rectangle(*region, 0., 4., 6., 2., 1.f);
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("target_components").size() == 2);
    double total = 0.;
    for (const auto& target : observed.at("target_components")) {
        total += target.at("target_volume_mm3").get<double>();
        REQUIRE(target.at("uncovered_volume_mm3") == 0.);
        REQUIRE(target.at("clipped_path_component_count") == 1);
    }
    REQUIRE(total == Approx(88.));
    REQUIRE(observed.at("cross_target_joins").size() == (bridge ? 1 : 0));
}

TEST_CASE("Nominal connectivity keeps valid empty targets separate from invalid geometry or paths", "[SliceAnalysis][Connectivity]")
{
    const int scenario = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    Model     model;
    Print     print;
    add_box(model, 1);
    apply_model(print, model);
    if (scenario != 0) {
        LayerRegion*  region = connectivity_layer(*print.get_object(0), 0., 1.,
                                                 scenario == 1 ? ExPolygons{} : ExPolygons{rectangle(0., 0., 2., 2.)});
        ExtrusionPath path   = planned_path(0., 1., 2., 1., 2.f);
        path.height          = 1.f;
        if (scenario == 2 || scenario == 3)
            path.height = std::numeric_limits<float>::quiet_NaN();
        if (scenario == 3)
            path.mm3_per_mm = 0.; // Excluded paths do not turn an otherwise valid observation unknown.
        if (scenario == 4)
            path.width = -1.f;
        if (scenario == 5)
            region->layer()->slicing_errors = true;
        if (scenario == 6)
            region->layer()->height = 0.;
        if (scenario == 7)
            path.height = float(SCALING_FACTOR * 0.25); // No positive quantized slab.
        if (scenario == 8)
            region->layer()->print_z = std::numeric_limits<double>::quiet_NaN();
        region->fills.append(path);
    }
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("complete") == (scenario == 1 || scenario == 3));
    if (scenario == 1)
        REQUIRE(observed.at("target_components").empty());
    if (scenario == 3) {
        REQUIRE(observed.at("target_components").size() == 1);
        REQUIRE(observed.at("target_components").at(0).at("clipped_path_component_count") == 0);
        REQUIRE(observed.at("target_components").at(0).at("uncovered_volume_mm3").get<double>() == Approx(4.));
    }
}

TEST_CASE("Nominal connectivity is per group instance and does not expand paths again", "[SliceAnalysis][Connectivity]")
{
    struct CountingPath : ExtrusionPath
    {
        size_t& visits;
        CountingPath(const ExtrusionPath& path, size_t& visits) : ExtrusionPath(path), visits(visits) {}
        void polygons_covered_by_width(Polygons& out, float epsilon) const override
        {
            ++visits;
            ExtrusionPath::polygons_covered_by_width(out, epsilon);
        }
    };
    Model model;
    Print print;
    add_box(model, 2);
    apply_model(print, model);
    size_t        visits = 0;
    LayerRegion*  region = connectivity_layer(*print.get_object(0), 0., 1., {rectangle(-3., -2., 2., 2.)});
    ExtrusionPath path   = planned_path(-3., -1., -1., -1., 2.f);
    path.height          = 1.f;
    region->fills.entities.push_back(new CountingPath(path, visits));
    const auto paths    = region->fills.as_polylines();
    const auto targets  = region->slices.surfaces;
    const auto settings = all_settings(print);
    const json report   = make_slice_analysis_report(print, InitializedResult{});
    REQUIRE(visits == 2); // Existing model cache and existing bed collector only.
    const auto& observed = report.at("objects").at("groups").at(0).at("nominal_target_path_connectivity");
    require_connectivity_contract(observed);
    REQUIRE(observed.at("target_components").size() == 1);
    REQUIRE(observed.at("target_components").at(0).at("target_volume_mm3").get<double>() == Approx(4.));
    REQUIRE(observed.at("target_components").at(0).at("bounds_mm").at("min") == json::array({-3., -2., 0.}));
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(visits == 4);
    REQUIRE(region->fills.as_polylines() == paths);
    REQUIRE(to_polygons(region->slices.surfaces) == to_polygons(targets));
    REQUIRE(all_settings(print) == settings);
    print.cancel();
    REQUIRE_THROWS_AS(make_slice_analysis_report(print, InitializedResult{}), CanceledException);
}

TEST_CASE("Generated compensated region targets include negative cuts and modifier partitions without raw-slice substitution",
          "[SliceAnalysis][Connectivity]")
{
    Model        model;
    Print        print;
    ModelObject* source = add_box(model, 1);
    ModelVolume* cut    = source->add_volume(TriangleMesh(its_make_cube(2., 2., 2.)), ModelVolumeType::NEGATIVE_VOLUME, false);
    cut->set_offset(Vec3d(4., 4., 0.));
    ModelVolume* modifier = source->add_volume(TriangleMesh(its_make_cube(5., 10., 2.)), ModelVolumeType::PARAMETER_MODIFIER, false);
    modifier->config.set_key_value("wall_loops", new ConfigOptionInt(3));
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("before_layer_change_gcode", std::string("G92 E0"));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.2);
    config.set("elefant_foot_compensation", 0.2);
    config.set("elefant_foot_compensation_layers", 1);
    config.set("skirt_loops", 0);
    config.set("enable_support", false);
    print.set_status_silent();
    print.auto_assign_extruders(source);
    print.apply(model, config);
    const StringObjectException validation = print.validate();
    INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
    REQUIRE(validation.string.empty());
    print.process();
    REQUIRE(print.objects().size() == 1);
    const PrintObject& object = *print.get_object(0);
    REQUIRE(object.num_printing_regions() > 1);
    double expected = 0.;
    bool   first    = true;
    for (const Layer* layer : object.layers()) {
        Polygons polygons;
        for (const LayerRegion* region : layer->regions())
            polygons_append(polygons, to_polygons(region->slices.surfaces));
        const ExPolygons section = union_ex(polygons);
        const double     area    = std::accumulate(section.begin(), section.end(), 0., [](double sum, const ExPolygon& polygon) {
            return sum + polygon.area() * SCALING_FACTOR * SCALING_FACTOR;
        });
        const double     low     = std::round(layer->bottom_z() / SCALING_FACTOR) * SCALING_FACTOR;
        const double     high    = std::round(layer->print_z / SCALING_FACTOR) * SCALING_FACTOR;
        expected += area * (high - low);
        if (first) {
            double raw_area = 0.;
            for (const ExPolygon& polygon : layer->lslices)
                raw_area += polygon.area() * SCALING_FACTOR * SCALING_FACTOR;
            REQUIRE(area < raw_area);
            REQUIRE(section.size() == 1);
            REQUIRE(section.front().holes.size() == 1);
            first = false;
        }
    }
    const auto  settings = all_settings(print);
    const json  report   = make_slice_analysis_report(print, InitializedResult{});
    const auto& observed = report.at("objects").at("groups").at(0).at("nominal_target_path_connectivity");
    require_connectivity_contract(observed);
    REQUIRE(observed.at("complete") == true);
    REQUIRE(observed.at("target_components").size() == 1);
    REQUIRE(observed.at("target_components").at(0).at("target_volume_mm3").get<double>() == Approx(expected).epsilon(0.).margin(1e-9));
    REQUIRE(make_slice_analysis_report(print, InitializedResult{}) == report);
    REQUIRE(all_settings(print) == settings);
}

TEST_CASE("Nominal connectivity keeps distant islands separate through many adjacent layers", "[SliceAnalysis][Connectivity][slow]")
{
    for (size_t count : {size_t(32), size_t(128)}) {
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        ExPolygons targets;
        for (size_t index = 0; index < count; ++index)
            targets.push_back(rectangle(3. * index, 0., 1., 1.));
        for (size_t layer = 0; layer < 4; ++layer) {
            LayerRegion* region = connectivity_layer(*print.get_object(0), 0.25 * layer, 0.25 * (layer + 1), targets);
            for (size_t index = 0; index < count; ++index)
                connectivity_rectangle(*region, 3. * index, 0., 1., 1., 0.25f);
        }
        const auto start    = std::chrono::steady_clock::now();
        const json observed = nominal_connectivity(print);
        std::cout << "nominal connectivity: islands=" << count
                  << " layers=4 report_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
                  << std::endl;
        require_connectivity_contract(observed);
        REQUIRE(observed.at("target_components").size() == count);
        for (const auto& component : observed.at("target_components")) {
            REQUIRE(component.at("target_volume_mm3").get<double>() == Approx(1.));
            REQUIRE(component.at("clipped_path_component_count") == 1);
            REQUIRE(component.at("uncovered_volume_mm3") == 0.);
        }
        REQUIRE(observed.at("cross_target_joins").empty());
    }
}

namespace {

json canonical_connectivity_geometry(const json& observed)
{
    require_connectivity_contract(observed);
    REQUIRE(observed.at("complete") == true);
    std::vector<json> targets, joins;
    for (json target : observed.at("target_components")) {
        target.erase("component_index");
        auto missing = target.at("missing_slab_regions").get<std::vector<json>>();
        std::sort(missing.begin(), missing.end(), [](const json& a, const json& b) { return a.dump() < b.dump(); });
        target["missing_slab_regions"] = std::move(missing);
        targets.push_back(std::move(target));
    }
    for (const auto& join : observed.at("cross_target_joins")) {
        std::vector<json> bounds;
        for (size_t index : join.at("target_component_indices").get<std::vector<size_t>>())
            bounds.push_back(observed.at("target_components").at(index).at("bounds_mm"));
        std::sort(bounds.begin(), bounds.end(), [](const json& a, const json& b) { return a.dump() < b.dump(); });
        joins.push_back(bounds);
    }
    const auto order = [](const json& a, const json& b) { return a.dump() < b.dump(); };
    std::sort(targets.begin(), targets.end(), order);
    std::sort(joins.begin(), joins.end(), order);
    return {{"targets", targets}, {"joins", joins}};
}

} // namespace

TEST_CASE("Fragmented and overlapping region targets preserve holes and point-touching connectivity", "[SliceAnalysis][Connectivity][slow]")
{
    ExPolygon main = rectangle(0., 0., 32., 32.);
    for (double x : {8., 20.}) {
        Polygon hole = rectangle(x, 8., 4., 4.).contour;
        hole.make_clockwise();
        main.holes.push_back(std::move(hole));
    }
    const ExPolygon  triangle(Polygon({Point(scaled(32.), scaled(4.)), Point(scaled(33.), scaled(3.5)), Point(scaled(33.), scaled(4.5))}));
    const ExPolygons standard{main, rectangle(9., 9., 2., 2.), rectangle(32., 32., 2., 2.), triangle};
    ExPolygons       fragments;
    for (size_t y = 0; y < 32; ++y)
        for (size_t x = 0; x < 32; ++x) {
            if (y >= 8 && y < 12 && ((x >= 8 && x < 12) || (x >= 20 && x < 24)))
                continue;
            fragments.push_back(rectangle(double(x), double(y), 1., 1.));
        }
    for (size_t y = 0; y < 2; ++y)
        for (size_t x = 0; x < 2; ++x) {
            fragments.push_back(rectangle(9. + x, 9. + y, 1., 1.));
            fragments.push_back(rectangle(32. + x, 32. + y, 1., 1.));
        }
    fragments.push_back(
        ExPolygon(Polygon({Point(scaled(32.), scaled(4.)), Point(scaled(33.), scaled(3.5)), Point(scaled(33.), scaled(4.))})));
    fragments.push_back(
        ExPolygon(Polygon({Point(scaled(32.), scaled(4.)), Point(scaled(33.), scaled(4.)), Point(scaled(33.), scaled(4.5))})));
    json reference;
    for (bool fragmented : {false, true}) {
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        PrintObject& object = *print.get_object(0);
        LayerRegion* region = connectivity_layer(object, 0., 1., standard);
        if (fragmented) {
            region->slices.set(fragments, stInternal);
            LayerRegion* overlap = region->layer()->add_region(&object.printing_region(0));
            // Adjacent seams, duplicate edges and genuine overlapping areas
            // are different normal inputs, not a reason to drop strict cells.
            overlap->slices.set({rectangle(0., 0., 32., 1.), rectangle(0.25, 0.25, 31.5, 0.5), rectangle(9., 9., 1.5, 1.5)}, stInternal);
        }
        connectivity_rectangle(*region, 0., 0., 32., 32., 1.f);
        connectivity_rectangle(*region, 32., 32., 2., 2., 1.f);
        const auto start    = std::chrono::steady_clock::now();
        const json observed = nominal_connectivity(print);
        std::cout << "nominal connectivity fragmented=" << fragmented
                  << " source_surfaces=" << (fragmented ? fragments.size() + 3 : standard.size())
                  << " report_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
                  << std::endl;
        const json geometry = canonical_connectivity_geometry(observed);
        REQUIRE(observed.at("target_components").size() == 4);
        std::vector<double> volumes;
        for (const auto& target : observed.at("target_components")) {
            const double volume = target.at("target_volume_mm3").get<double>();
            volumes.push_back(volume);
            const bool triangle_component = volume < 1.;
            REQUIRE(target.at("clipped_path_component_count") == (triangle_component ? 0 : 1));
            REQUIRE(target.at("uncovered_volume_mm3").get<double>() == Approx(triangle_component ? 0.5 : 0.).epsilon(0.).margin(1e-12));
        }
        std::sort(volumes.begin(), volumes.end());
        const std::array<double, 4> exact_volumes{0.5, 4., 4., 992.};
        for (size_t index = 0; index < exact_volumes.size(); ++index)
            REQUIRE(volumes.at(index) == Approx(exact_volumes[index]).epsilon(0.).margin(1e-12));
        REQUIRE(observed.at("cross_target_joins").size() == 1);
        REQUIRE(observed.at("cross_target_joins").at(0).at("target_component_indices").size() == 2);
        if (fragmented)
            REQUIRE(geometry == reference);
        else
            reference = geometry;
    }
}

TEST_CASE("Slanted fragmented targets preserve exact covered and missing geometry", "[SliceAnalysis][Connectivity]")
{
    const auto triangle = [](double ax, double ay, double bx, double by, double cx, double cy) {
        return ExPolygon(Polygon({Point(scaled(ax), scaled(ay)), Point(scaled(bx), scaled(by)), Point(scaled(cx), scaled(cy))}));
    };
    const ExPolygons standard{triangle(0., 0., 4., 0., 4., 4.)};
    const ExPolygons fragments{triangle(0., 0., 2., 0., 2., 2.), triangle(2., 0., 4., 0., 4., 4.), triangle(2., 0., 4., 4., 2., 2.)};
    json             reference;
    for (bool fragmented : {false, true}) {
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        PrintObject& object = *print.get_object(0);
        LayerRegion* region = connectivity_layer(object, 0., 1., standard);
        if (fragmented) {
            region->slices.set(fragments, stInternal);
            region->layer()->add_region(&object.printing_region(0))->slices.set({triangle(0.5, 0.25, 3., 0.25, 3., 2.5)}, stInternal);
        }
        connectivity_rectangle(*region, 0., 0., 2., 4., 1.f);
        const json observed = nominal_connectivity(print);
        const json geometry = canonical_connectivity_geometry(observed);
        REQUIRE(observed.at("target_components").size() == 1);
        const auto& target = observed.at("target_components").at(0);
        REQUIRE(target.at("target_volume_mm3").get<double>() == Approx(8.).epsilon(0.).margin(1e-12));
        REQUIRE(target.at("uncovered_volume_mm3").get<double>() == Approx(6.).epsilon(0.).margin(1e-12));
        REQUIRE(target.at("clipped_path_component_count") == 1);
        REQUIRE(target.at("missing_slab_regions").size() == 1);
        REQUIRE(target.at("missing_slab_regions").at(0).at("bounds_mm").at("min") == json::array({2., 0., 0.}));
        REQUIRE(target.at("missing_slab_regions").at(0).at("bounds_mm").at("max") == json::array({4., 4., 1.}));
        REQUIRE(observed.at("cross_target_joins").empty());
        if (fragmented)
            REQUIRE(geometry == reference);
        else
            reference = geometry;
    }
}

TEST_CASE("Nominal connectivity preserves microscopic positive interfaces after a large translation", "[SliceAnalysis][Connectivity]")
{
    json reference_volumes;
    for (coord_t base : {coord_t(0), scaled(100.), scaled(1000.)}) {
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        // The exact triangle area is half of one scaled coordinate unit squared.
        // Absolute-coordinate double cross products can cancel this area at a
        // large translation; neither its volume nor the joining face is zero.
        const ExPolygons target{ExPolygon(Polygon({Point(base, base), Point(base + 1, base), Point(base, base + 1)}))};
        for (size_t layer = 0; layer < 2; ++layer)
            connectivity_layer(*print.get_object(0), double(layer), double(layer + 1), target);
        const json observed = nominal_connectivity(print);
        INFO("scaled XY translation: " << base);
        require_connectivity_contract(observed);
        REQUIRE(observed.at("complete") == true);
        REQUIRE(observed.at("target_components").size() == 1);
        const auto& component = observed.at("target_components").at(0);
        REQUIRE(component.at("clipped_path_component_count") == 0);
        REQUIRE(component.at("target_volume_mm3").get<double>() == Approx(1e-12).epsilon(0.).margin(1e-27));
        REQUIRE(component.at("uncovered_volume_mm3") == component.at("target_volume_mm3"));
        REQUIRE(component.at("bounds_mm").at("min") == json::array({unscale<double>(base), unscale<double>(base), 0.}));
        REQUIRE(component.at("bounds_mm").at("max") == json::array({unscale<double>(base + 1), unscale<double>(base + 1), 2.}));
        REQUIRE(component.at("missing_slab_regions").size() == 2);
        json volumes = json::array({component.at("target_volume_mm3"), component.at("uncovered_volume_mm3")});
        for (const auto& missing : component.at("missing_slab_regions")) {
            REQUIRE(missing.at("volume_mm3").get<double>() == Approx(5e-13).epsilon(0.).margin(1e-27));
            volumes.push_back(missing.at("volume_mm3"));
        }
        REQUIRE(observed.at("cross_target_joins").empty());
        if (base == 0)
            reference_volumes = volumes;
        else
            REQUIRE(volumes == reference_volumes);
    }
}

TEST_CASE("Nominal connectivity preserves the measured thin triangle in either input direction", "[SliceAnalysis][Connectivity]")
{
    json reference_volumes;
    for (const Point& shift : {Point(0, 0), Point(32662900, -18505151), Point(scaled(1000.), scaled(-1000.))}) {
        for (bool reverse : {false, true}) {
            Model model;
            Print print;
            add_box(model, 1);
            apply_model(print, model);
            // This shape was observed as a clockwise outer ring in a real
            // StrictlySimple result. Feeding it as an input here verifies the
            // geometry contract, not a reproduction of that internal split.
            Polygon contour({Point(0, 0), Point(3, 0), Point(25387, -201)}); // Exact twice-area: -603.
            if (reverse)
                contour.reverse();
            contour.translate(shift);
            const ExPolygons targets{ExPolygon(contour)};
            for (size_t layer = 0; layer < 2; ++layer)
                connectivity_layer(*print.get_object(0), double(layer), double(layer + 1), targets);
            const json observed = nominal_connectivity(print);
            INFO("scaled shift: " << shift.x() << ',' << shift.y() << "; reversed: " << reverse);
            require_connectivity_contract(observed);
            REQUIRE(observed.at("complete") == true);
            REQUIRE(observed.at("target_components").size() == 1);
            const auto& component = observed.at("target_components").at(0);
            REQUIRE(component.at("target_volume_mm3").get<double>() == Approx(6.03e-10).epsilon(0.).margin(1e-24));
            REQUIRE(component.at("uncovered_volume_mm3") == component.at("target_volume_mm3"));
            REQUIRE(component.at("clipped_path_component_count") == 0);
            REQUIRE(component.at("missing_slab_regions").size() == 2);
            REQUIRE(component.at("bounds_mm").at("min") == json::array({unscale<double>(shift.x()), unscale<double>(shift.y() - 201), 0.}));
            REQUIRE(component.at("bounds_mm").at("max") ==
                    json::array({unscale<double>(shift.x() + 25387), unscale<double>(shift.y()), 2.}));
            json volumes = json::array({component.at("target_volume_mm3"), component.at("uncovered_volume_mm3")});
            for (const auto& missing : component.at("missing_slab_regions")) {
                REQUIRE(missing.at("volume_mm3").get<double>() == Approx(3.015e-10).epsilon(0.).margin(1e-24));
                volumes.push_back(missing.at("volume_mm3"));
            }
            REQUIRE(observed.at("cross_target_joins").empty());
            if (reference_volumes.is_null())
                reference_volumes = volumes;
            else
                REQUIRE(volumes == reference_volumes);
        }
    }
}

TEST_CASE("Nominal connectivity subtracts the thin triangle hole without losing its nested island", "[SliceAnalysis][Connectivity]")
{
    json reference_volumes;
    for (const Point& shift : {Point(0, 0), Point(32662900, -18505151), Point(scaled(1000.), scaled(-1000.))}) {
        for (bool reverse : {false, true}) {
            Model model;
            Print print;
            add_box(model, 1);
            apply_model(print, model);
            Polygon outer({Point(-1, -202), Point(25388, -202), Point(25388, 1), Point(-1, 1)}); // Area 5153967 scaled².
            Polygon hole({Point(0, 0), Point(3, 0), Point(25387, -201)});                        // Area -301.5 scaled².
            Polygon island({Point(127, -1), Point(254, -2), Point(128, -1)});                    // Area 0.5 scaled², strictly inside hole.
            if (reverse) {
                // Reversing every ring preserves nonzero winding semantics:
                // the hole must still subtract, not become an extra outer.
                outer.reverse();
                hole.reverse();
                island.reverse();
            }
            outer.translate(shift);
            hole.translate(shift);
            island.translate(shift);
            ExPolygon surround(outer);
            surround.holes.push_back(hole);
            const ExPolygons targets{surround, ExPolygon(island)};
            for (size_t layer = 0; layer < 2; ++layer)
                connectivity_layer(*print.get_object(0), double(layer), double(layer + 1), targets);
            const json observed = nominal_connectivity(print);
            INFO("scaled shift: " << shift.x() << ',' << shift.y() << "; reversed: " << reverse);
            require_connectivity_contract(observed);
            REQUIRE(observed.at("complete") == true);
            REQUIRE(observed.at("target_components").size() == 2);
            std::vector<double> volumes;
            for (const auto& component : observed.at("target_components")) {
                REQUIRE(component.at("clipped_path_component_count") == 0);
                REQUIRE(component.at("uncovered_volume_mm3") == component.at("target_volume_mm3"));
                REQUIRE(component.at("missing_slab_regions").size() == 2);
                volumes.push_back(component.at("target_volume_mm3").get<double>());
            }
            std::sort(volumes.begin(), volumes.end());
            REQUIRE(volumes.at(0) == Approx(1e-12).epsilon(0.).margin(1e-27));
            REQUIRE(volumes.at(1) == Approx(1.0307331e-5).epsilon(0.).margin(1e-20));
            REQUIRE(observed.at("cross_target_joins").empty());
            if (reference_volumes.is_null())
                reference_volumes = volumes;
            else
                REQUIRE(json(volumes) == reference_volumes);
        }
    }
}

TEST_CASE("Nominal connectivity preserves the exact connected area of a narrow crossing contour", "[SliceAnalysis][Connectivity]")
{
    // Reduced from an actual region slice. Integer Strict Paths yielded two
    // rounded rings, but the original NonZero regions overlap and form one
    // component. An independent rational vertical-strip integration supplies
    // the exact filled area; neither the signed area nor the rounded rings do.
    const Polygon contour({Point(22115162, -44925549), Point(22143365, -44882442), Point(22184154, -44820100), Point(22222535, -44750472),
                           Point(22252394, -44696305), Point(22115162, -44925548), Point(22184153, -44820100), Point(22143365, -44882442),
                           Point(22115160, -44925551)});
    using Rational                 = SliceAnalysisGeometry::Rational;
    const Rational expected_area2  = Rational("20576139159975682215906") / Rational("15297781005667");
    const Rational expected_volume = expected_area2 / Rational("2000000000000"); // 0.5 * 1e-12 mm² * 1 mm.
    const double   volume          = expected_volume.convert_to<double>();
    json           reference_volumes;
    for (const Point& shift : {Point(0, 0), Point(scaled(100.), scaled(-100.)), Point(scaled(1000.), scaled(-1000.))}) {
        for (bool reverse : {false, true}) {
            Polygon source = contour;
            if (reverse)
                source.reverse();
            source.translate(shift);
            Model model;
            Print print;
            add_box(model, 1);
            apply_model(print, model);
            connectivity_layer(*print.get_object(0), 0., 1., {ExPolygon(source)});
            const json observed = nominal_connectivity(print);
            INFO("scaled shift: " << shift.x() << ',' << shift.y() << "; reversed: " << reverse);
            require_connectivity_contract(observed);
            REQUIRE(observed.at("complete") == true);
            REQUIRE(observed.at("target_components").size() == 1);
            const auto& component = observed.at("target_components").at(0);
            REQUIRE(component.at("target_volume_mm3").get<double>() == Approx(volume).epsilon(0.).margin(1e-18));
            REQUIRE(component.at("uncovered_volume_mm3") == component.at("target_volume_mm3"));
            REQUIRE(component.at("clipped_path_component_count") == 0);
            REQUIRE(component.at("missing_slab_regions").size() == 1);
            REQUIRE(component.at("missing_slab_regions").at(0).at("volume_mm3") == component.at("target_volume_mm3"));
            REQUIRE(component.at("bounds_mm").at("min") ==
                    json::array({unscale<double>(22115160 + shift.x()), unscale<double>(-44925551 + shift.y()), 0.}));
            REQUIRE(component.at("bounds_mm").at("max") ==
                    json::array({unscale<double>(22252394 + shift.x()), unscale<double>(-44696305 + shift.y()), 1.}));
            REQUIRE(component.at("missing_slab_regions").at(0).at("bounds_mm") == component.at("bounds_mm"));
            REQUIRE(observed.at("cross_target_joins").empty());
            const json volumes = json::array({component.at("target_volume_mm3"), component.at("uncovered_volume_mm3")});
            if (reference_volumes.is_null())
                reference_volumes = volumes;
            else
                REQUIRE(volumes == reference_volumes);
        }
    }
}

TEST_CASE("Nominal missing regions do not grow when only a path bounding box overlaps", "[SliceAnalysis][Connectivity]")
{
    const bool    weak    = GENERATE(false, true);
    const Polygon contour = weak ? Polygon({Point(22115162, -44925549), Point(22143365, -44882442), Point(22184154, -44820100),
                                            Point(22222535, -44750472), Point(22252394, -44696305), Point(22115162, -44925548),
                                            Point(22184153, -44820100), Point(22143365, -44882442), Point(22115160, -44925551)}) :
                                   Polygon({Point(0, 0), Point(3, 0), Point(25387, -201)});
    ExtrusionPath path(erExternalPerimeter, 0.08, float(2. * SCALING_FACTOR), 1.f);
    path.polyline = weak ? Polyline({Point(22115160, -44882450), Point(22115163, -44882450)}) : Polyline({Point(0, -100), Point(3, -100)});
    Polygons footprint;
    path.polygons_covered_by_width(footprint, 0.f);
    REQUIRE_FALSE(footprint.empty());
    REQUIRE(get_extents(contour).overlap(get_extents(footprint)));
    REQUIRE(intersection_ex(Polygons{contour}, footprint).empty());
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    LayerRegion* region       = connectivity_layer(*print.get_object(0), 0., 1., {ExPolygon(contour)});
    const json   without_path = nominal_connectivity(print);
    require_connectivity_contract(without_path);
    REQUIRE(without_path.at("complete") == true);
    region->fills.append(path);
    const json with_path = nominal_connectivity(print);
    require_connectivity_contract(with_path);
    REQUIRE(with_path == without_path);
}

namespace {

using MissingRational = SliceAnalysisGeometry::Rational;

Polygon captured_missing_target()
{
    return Polygon({Point(-24131302, 30311565), Point(-24123292, 30317535), Point(-24123293, 30317535), Point(-24198118, 30261764)});
}

Polygon captured_missing_material()
{
    return Polygon({Point(-24969000, 28917460), Point(-25038989, 29046132), Point(-25021766, 29034534), Point(-24944359, 28933459),
                    Point(-24577259, 29214597), Point(-24568761, 29221944), Point(-24234618, 29547408), Point(-24229329, 29552996),
                    Point(-23878967, 29954679), Point(-23859039, 29991373), Point(-23793861, 30219944), Point(-23988740, 30377329),
                    Point(-24177191, 30276847), Point(-24196075, 30263604), Point(-24315080, 30156399), Point(-24317823, 30153827),
                    Point(-24856885, 29627945), Point(-24860836, 29623851), Point(-25147444, 29308482), Point(-25155916, 29297681),
                    Point(-25318946, 29055589), Point(-25119044, 28835846)});
}

MissingRational captured_missing_twice_area()
{
    // Independently integrated using rational vertical events of the captured
    // source edges, not by rounding CGAL output or by accepting D <= T alone.
    return MissingRational("4822040503204547183733148122929167423") / MissingRational("118542619892253068605990391621940");
}

SliceAnalysisGeometry::Regions exact_missing(const ExPolygon& target, const ExPolygons& material, const std::function<void()>& cancel)
{
    const auto                     targets = SliceAnalysisGeometry::region_from_expolygon(target, cancel);
    SliceAnalysisGeometry::Regions materials, result;
    for (const ExPolygon& polygon : material) {
        const auto regions = SliceAnalysisGeometry::region_from_expolygon(polygon, cancel);
        materials.insert(materials.end(), regions.begin(), regions.end());
    }
    for (const auto& region : targets) {
        const auto missing = SliceAnalysisGeometry::subtract_regions(region, materials, cancel);
        result.insert(result.end(), missing.begin(), missing.end());
    }
    return result;
}

MissingRational missing_twice_area(const SliceAnalysisGeometry::Regions& regions)
{
    MissingRational result = 0;
    for (const auto& region : regions) {
        REQUIRE(region.shape != nullptr);
        REQUIRE(region.twice_area > 0);
        REQUIRE(region.min[0] < region.max[0]);
        REQUIRE(region.min[1] < region.max[1]);
        result += region.twice_area;
    }
    return result;
}

ExPolygon grid_rectangle(coord_t x0, coord_t y0, coord_t x1, coord_t y1)
{
    return ExPolygon(Polygon({Point(x0, y0), Point(x1, y0), Point(x1, y1), Point(x0, y1)}));
}

struct ExactRetainedFootprintPath : ExtrusionPath
{
    Polygon footprint;
    explicit ExactRetainedFootprintPath(const Polygon& polygon, float height = 0.2f)
        : ExtrusionPath(erExternalPerimeter, 0.08, 0.4f, height), footprint(polygon)
    {
        polyline = Polyline({polygon.points.at(0), polygon.points.at(1)});
    }
    void polygons_covered_by_width(Polygons& out, float) const override { out.push_back(footprint); }
};

} // namespace

TEST_CASE("Nominal missing volume stays inside a narrow target cut by a retained footprint", "[SliceAnalysis][Connectivity]")
{
    // Captured from the real report's normalized T/M input, not reconstructed
    // from a guessed stroke width. The virtual path supplies that retained
    // footprint to the public report so its following Boolean stages are used.
    const Polygon target   = captured_missing_target();
    const Polygon material = captured_missing_material();
    const auto    area2    = [](const Polygons& polygons) {
        int64_t result = 0;
        for (const Polygon& polygon : polygons) {
            const Point origin   = polygon.points.front();
            Point       previous = polygon.points.back();
            for (const Point& point : polygon.points) {
                // This captured fixture spans less than 2 mm; relative
                // products fit int64_t and retain exact integer-grid area.
                result += (int64_t(previous.x()) - origin.x()) * (int64_t(point.y()) - origin.y()) -
                          (int64_t(previous.y()) - origin.y()) * (int64_t(point.x()) - origin.x());
                previous = point;
            }
        }
        return result;
    };
    REQUIRE(area2(Polygons{target}) == 41281);
    const Polygons raw      = to_polygons(diff_ex(Polygons{target}, Polygons{material}));
    const Polygons disjoint = union_(raw);
    const Polygons split    = simplify_polygons(disjoint);
    const Polygons rebuilt  = to_polygons(union_ex(split));
    INFO("twice scaled areas, target/raw/union/strict/rebuilt: " << area2(Polygons{target}) << '/' << area2(raw) << '/' << area2(disjoint)
                                                                 << '/' << area2(split) << '/' << area2(rebuilt));
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    LayerRegion* region = connectivity_layer(*print.get_object(0), 0.2, 0.4, {ExPolygon(target)});
    region->fills.entities.push_back(new ExactRetainedFootprintPath(material));
    const json observed = nominal_connectivity(print);
    REQUIRE(observed.at("complete") == true);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("target_components").size() == 1);
    REQUIRE(observed.at("target_components").at(0).at("target_volume_mm3").get<double>() == Approx(4.1281e-9).epsilon(0.).margin(1e-23));
    const MissingRational expected_missing_mm3 = captured_missing_twice_area() / MissingRational("10000000000000");
    const double          expected_missing     = expected_missing_mm3.convert_to<double>();
    REQUIRE(observed.at("target_components").at(0).at("uncovered_volume_mm3").get<double>() ==
            Approx(expected_missing).epsilon(0.).margin(1e-23)); // area2 * 0.5 * 1e-12 mm² * 0.2 mm.
}

TEST_CASE("Exact missing regions preserve the captured rational difference under translation", "[SliceAnalysis][Connectivity][ExactMissing]")
{
    const MissingRational expected = captured_missing_twice_area();
    REQUIRE(expected > 0);
    REQUIRE(expected < 41281);
    const auto reference = exact_missing(ExPolygon(captured_missing_target()), {ExPolygon(captured_missing_material())}, [] {});
    REQUIRE(missing_twice_area(reference) == expected);
    for (const Point& shift : {Point(0, 0), Point(scaled(100.), scaled(-100.)), Point(scaled(-1000.), scaled(1000.))}) {
        Polygon target = captured_missing_target(), material = captured_missing_material();
        target.translate(shift);
        material.translate(shift);
        const auto observed = exact_missing(ExPolygon(target), {ExPolygon(material)}, [] {});
        INFO("scaled shift: " << shift.x() << ',' << shift.y());
        REQUIRE(missing_twice_area(observed) == expected);
        REQUIRE(observed.size() == reference.size());
        // Region order is not an identity: match exact translated geometry.
        for (const auto& region : reference) {
            const auto found = std::find_if(observed.begin(), observed.end(), [&](const auto& shifted) {
                return shifted.twice_area == region.twice_area && shifted.min[0] == region.min[0] + shift.x() &&
                       shifted.min[1] == region.min[1] + shift.y() && shifted.max[0] == region.max[0] + shift.x() &&
                       shifted.max[1] == region.max[1] + shift.y();
            });
            REQUIRE(found != observed.end());
        }
    }
}

TEST_CASE("Exact missing regions keep a fractional cut even when integer difference does not exceed target",
          "[SliceAnalysis][Connectivity][ExactMissing]")
{
    const ExPolygon target = grid_rectangle(0, 0, 1, 1);
    const ExPolygon material(Polygon({Point(0, 0), Point(3, 0), Point(0, 1)}));
    // Line y = 1 - x/3 leaves the triangle (0,1), (1,2/3), (1,1).
    // Its area is 1/6 scaled²; no integer-coordinate polygon can retain it.
    const auto observed = exact_missing(target, {material}, [] {});
    REQUIRE(observed.size() == 1);
    REQUIRE(observed.front().twice_area == MissingRational(1) / 3);
    REQUIRE(observed.front().min[0] == 0);
    REQUIRE(observed.front().min[1] == MissingRational(2) / 3);
    REQUIRE(observed.front().max[0] == 1);
    REQUIRE(observed.front().max[1] == 1);
    const double rounded_area = area(diff_ex(target, ExPolygons{material}));
    REQUIRE(rounded_area <= 1.);
    REQUIRE(rounded_area != Approx(1. / 6.).epsilon(0.).margin(1e-12));

    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    LayerRegion* region = connectivity_layer(*print.get_object(0), 0., 1., {target});
    region->fills.entities.push_back(new ExactRetainedFootprintPath(material.contour, 1.f));
    const json report = nominal_connectivity(print);
    require_connectivity_contract(report);
    REQUIRE(report.at("complete") == true);
    REQUIRE(report.at("target_components").size() == 1);
    const auto& component = report.at("target_components").at(0);
    REQUIRE(component.at("target_volume_mm3").get<double>() == Approx(1e-12).epsilon(0.).margin(1e-27));
    REQUIRE(component.at("uncovered_volume_mm3").get<double>() == Approx(1e-12 / 6.).epsilon(0.).margin(1e-27));
    REQUIRE(component.at("missing_slab_regions").size() == 1);
    const auto& missing = component.at("missing_slab_regions").at(0);
    REQUIRE(missing.at("bounds_mm").at("min").at(1).get<double>() == Approx(2e-6 / 3.).epsilon(0.).margin(1e-21));
}

TEST_CASE("Exact missing regions subtract holes and material islands without counting the bounding box",
          "[SliceAnalysis][Connectivity][ExactMissing]")
{
    ExPolygon target        = grid_rectangle(0, 0, 10, 10);
    ExPolygon material      = target;
    Polygon   material_hole = grid_rectangle(2, 2, 8, 8).contour;
    material_hole.make_clockwise();
    material.holes.push_back(material_hole);
    SECTION("target hole remains excluded from missing volume")
    {
        Polygon target_hole = grid_rectangle(3, 3, 7, 7).contour;
        target_hole.make_clockwise();
        target.holes.push_back(target_hole);
        const auto observed = exact_missing(target, {material}, [] {});
        REQUIRE(observed.size() == 1);
        REQUIRE(observed.front().twice_area == 40); // 2 * (6*6 - 4*4).
        REQUIRE(observed.front().min[0] == 2);
        REQUIRE(observed.front().min[1] == 2);
        REQUIRE(observed.front().max[0] == 8);
        REQUIRE(observed.front().max[1] == 8);
    }
    SECTION("material island inside its hole is joined before difference")
    {
        const auto observed = exact_missing(target, {material, grid_rectangle(4, 4, 6, 6)}, [] {});
        REQUIRE(observed.size() == 1);
        REQUIRE(observed.front().twice_area == 64); // 2 * (6*6 - 2*2), not the bbox's 72.
    }
}

TEST_CASE("Exact missing regions preserve coordinates beyond double integer precision in both directions",
          "[SliceAnalysis][Connectivity][ExactMissing]")
{
    const coord_t large = coord_t(9007199254741009LL); // 2^53 + 17, not exactly representable as double.
    for (const Point& shift : {Point(large, -large), Point(-large, large)}) {
        ExPolygon target = grid_rectangle(0, 0, 1, 1);
        ExPolygon material(Polygon({Point(0, 0), Point(3, 0), Point(0, 1)}));
        target.translate(shift);
        material.translate(shift);
        const auto observed = exact_missing(target, {material}, [] {});
        INFO("scaled shift: " << shift.x() << ',' << shift.y());
        REQUIRE(observed.size() == 1);
        const auto& region = observed.front();
        REQUIRE(region.twice_area == MissingRational(1) / 3);
        REQUIRE(region.min[0] == MissingRational(shift.x()));
        REQUIRE(region.min[1] == MissingRational(shift.y()) + MissingRational(2) / 3);
        REQUIRE(region.max[0] == MissingRational(shift.x()) + 1);
        REQUIRE(region.max[1] == MissingRational(shift.y()) + 1);
    }
}

TEST_CASE("NonZero missing regions preserve set area at hole point contacts and shared boundary edges",
          "[SliceAnalysis][Connectivity][ExactMissing]")
{
    ExPolygon target = grid_rectangle(0, 0, 10, 10);
    Polygon   point_touch({Point(0, 5), Point(2, 3), Point(4, 5), Point(2, 7)});
    point_touch.make_clockwise();
    target.holes.push_back(point_touch);
    const auto observed = exact_missing(target, {}, [] {});
    REQUIRE(missing_twice_area(observed) == 184); // 2 * (100 - 8), point contact has zero area.

    target.holes.front() = grid_rectangle(0, 3, 4, 7).contour;
    target.holes.front().make_clockwise();
    REQUIRE(missing_twice_area(exact_missing(target, {}, [] {})) == 168); // 2 * (100 - 4*4).
}

TEST_CASE("Nominal missing volume accumulates exact rational cuts across different Z intervals",
          "[SliceAnalysis][Connectivity][ExactMissing]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    const ExPolygon target = grid_rectangle(0, 0, 1, 1);
    const Polygon   first_material({Point(0, 0), Point(3, 0), Point(0, 1)});
    const Polygon   second_material({Point(0, 0), Point(2, 0), Point(0, 1)});
    LayerRegion*    first = connectivity_layer(*print.get_object(0), 0., 1., {target});
    first->fills.entities.push_back(new ExactRetainedFootprintPath(first_material, 1.f));
    LayerRegion* second = connectivity_layer(*print.get_object(0), 1., 3., {target});
    second->fills.entities.push_back(new ExactRetainedFootprintPath(second_material, 2.f));
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("complete") == true);
    REQUIRE(observed.at("target_components").size() == 1);
    const auto& component = observed.at("target_components").at(0);
    REQUIRE(component.at("target_volume_mm3").get<double>() == Approx(3e-12).epsilon(0.).margin(1e-27));
    // (1/6 scaled² * 1 mm) + (1/4 scaled² * 2 mm) = 2/3 * 1e-12 mm³.
    REQUIRE(component.at("uncovered_volume_mm3").get<double>() == Approx(2e-12 / 3.).epsilon(0.).margin(1e-27));
    const auto& missing = component.at("missing_slab_regions");
    REQUIRE(missing.size() == 2);
    for (const auto& region : missing) {
        const double bottom = region.at("bounds_mm").at("min").at(2).get<double>();
        const double top    = region.at("bounds_mm").at("max").at(2).get<double>();
        if (bottom == 0.) {
            REQUIRE(top == 1.);
            REQUIRE(region.at("volume_mm3").get<double>() == Approx(1e-12 / 6.).epsilon(0.).margin(1e-27));
        } else {
            REQUIRE(bottom == 1.);
            REQUIRE(top == 3.);
            REQUIRE(region.at("volume_mm3").get<double>() == Approx(1e-12 / 2.).epsilon(0.).margin(1e-27));
        }
    }
}

TEST_CASE("Exact missing regions preserve point separated components and ignore mere boundary contact",
          "[SliceAnalysis][Connectivity][ExactMissing]")
{
    const ExPolygon target = grid_rectangle(0, 0, 4, 4);
    SECTION("four missing corners meet only at boundary points")
    {
        const ExPolygon diamond(Polygon({Point(0, 2), Point(2, 0), Point(4, 2), Point(2, 4)}));
        const auto      observed = exact_missing(target, {diamond}, [] {});
        REQUIRE(observed.size() == 4);
        REQUIRE(missing_twice_area(observed) == 16);
        for (const auto& region : observed)
            REQUIRE(region.twice_area == 4);
    }
    SECTION("outside material touching at an edge or point removes no positive area")
    {
        for (const ExPolygon& material : {grid_rectangle(4, 0, 6, 4), grid_rectangle(4, 4, 6, 6)}) {
            const auto observed = exact_missing(target, {material}, [] {});
            REQUIRE(observed.size() == 1);
            REQUIRE(observed.front().twice_area == 32);
        }
    }
}

TEST_CASE("Exact missing regions preserve known empty sets and do not mutate ring directions", "[SliceAnalysis][Connectivity][ExactMissing]")
{
    const ExPolygon target      = grid_rectangle(0, 0, 10, 10);
    const auto      no_material = exact_missing(target, {}, [] {});
    REQUIRE(no_material.size() == 1);
    REQUIRE(no_material.front().twice_area == 200);
    const auto all_covered = exact_missing(target, {target, target}, [] {});
    REQUIRE(all_covered.empty());

    ExPolygon with_hole = target;
    with_hole.holes.push_back(grid_rectangle(3, 3, 7, 7).contour); // Deliberately noncanonical direction.
    with_hole.contour.reverse();
    const ExPolygon before     = with_hole;
    const auto      normalized = exact_missing(with_hole, {}, [] {});
    REQUIRE(normalized.size() == 1);
    REQUIRE(normalized.front().twice_area == 168);
    REQUIRE(with_hole.contour.points == before.contour.points);
    REQUIRE(with_hole.holes.front().points == before.holes.front().points);
}

TEST_CASE("NonZero missing measurements use explicit set values for crossing degenerate and overlapping hole inputs",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    const ExPolygon                                    valid = grid_rectangle(0, 0, 10, 10);
    std::vector<std::pair<ExPolygon, MissingRational>> cases;
    cases.emplace_back(ExPolygon{}, MissingRational(0));
    cases.emplace_back(ExPolygon(Polygon({Point(0, 0), Point(1, 0), Point(2, 0)})), MissingRational(0));
    cases.emplace_back(ExPolygon(Polygon({Point(0, 0), Point(4, 4), Point(0, 4), Point(4, 0)})), MissingRational(16));
    cases.emplace_back(ExPolygon(Polygon({Point(0, 0), Point(4, 4), Point(0, 4), Point(2, 0)})), MissingRational(40) / 3);
    cases.emplace_back(ExPolygon(Polygon({Point(0, 0), Point(4, 0), Point(4, 0), Point(4, 4), Point(0, 4)})), MissingRational(32));
    ExPolygon external_hole = valid;
    external_hole.holes.push_back(grid_rectangle(11, 11, 12, 12).contour);
    cases.emplace_back(external_hole, MissingRational(200)); // A hole outside the outer set subtracts nothing.
    ExPolygon crossing_hole = valid;
    crossing_hole.holes.push_back(grid_rectangle(9, 9, 11, 11).contour);
    cases.emplace_back(crossing_hole, MissingRational(198)); // Only the 1x1 intersection is subtracted.
    ExPolygon overlapping_holes = valid;
    overlapping_holes.holes.push_back(grid_rectangle(1, 1, 5, 5).contour);
    overlapping_holes.holes.push_back(grid_rectangle(4, 4, 8, 8).contour);
    cases.emplace_back(overlapping_holes, MissingRational(138)); // 2 * (100 - (16+16-1)).
    ExPolygon nested_holes = valid;
    nested_holes.holes.push_back(grid_rectangle(1, 1, 9, 9).contour);
    nested_holes.holes.push_back(grid_rectangle(2, 2, 3, 3).contour);
    cases.emplace_back(nested_holes, MissingRational(72)); // The nested hole is not subtracted a second time.
    for (size_t index = 0; index < cases.size(); ++index) {
        INFO("NonZero fixture " << index);
        const auto& [source, twice_area] = cases[index];
        REQUIRE(missing_twice_area(exact_missing(source, {}, [] {})) == twice_area);
        REQUIRE(missing_twice_area(exact_missing(valid, {source}, [] {})) == 200 - twice_area);
        REQUIRE(exact_missing(valid, {valid, source}, [] {}).empty());
    }
}

TEST_CASE("Exact missing regions propagate cancellation at entry and during geometry processing",
          "[SliceAnalysis][Connectivity][ExactMissing]")
{
    const ExPolygon  target = grid_rectangle(0, 0, 10, 10);
    const ExPolygons materials{grid_rectangle(1, 1, 2, 2), grid_rectangle(3, 3, 4, 4), grid_rectangle(5, 5, 6, 6)};
    REQUIRE_THROWS_AS(exact_missing(target, materials, [] { throw CanceledException(); }), CanceledException);
    size_t     callbacks = 0;
    const auto cancel    = [&] {
        if (++callbacks == 3)
            throw CanceledException();
    };
    REQUIRE_THROWS_AS(exact_missing(target, materials, cancel), CanceledException);
    REQUIRE(callbacks == 3);
}

TEST_CASE("Nominal missing regions reject a collapsed exported bound while retaining exact positive geometry",
          "[SliceAnalysis][Connectivity][ExactMissing]")
{
    const coord_t scale = 1000000000;
    const Point   shift(scale, scale);
    ExPolygon     target = grid_rectangle(0, 0, 1, 1);
    ExPolygon     material(Polygon({Point(0, 0), Point(scale, 0), Point(0, 1)}));
    target.translate(shift);
    material.translate(shift);
    const auto exact = exact_missing(target, {material}, [] {});
    REQUIRE(exact.size() == 1);
    const auto& missing = exact.front();
    // The triangle above y = 1 - x/1e9 retains positive rational area and
    // height. Near 1e9 scaled units its Y bounds cannot remain distinct in
    // the report's double millimetre representation.
    REQUIRE(missing.twice_area == MissingRational(1) / scale);
    REQUIRE(missing.min[0] == MissingRational(scale));
    REQUIRE(missing.max[0] == MissingRational(scale) + 1);
    REQUIRE(missing.min[1] == MissingRational(scale) + 1 - MissingRational(1) / scale);
    REQUIRE(missing.max[1] == MissingRational(scale) + 1);
    REQUIRE(missing.min[1] < missing.max[1]);
    const MissingRational min_y_mm = missing.min[1] / 1000000;
    const MissingRational max_y_mm = missing.max[1] / 1000000;
    REQUIRE(min_y_mm.convert_to<double>() == max_y_mm.convert_to<double>());

    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    LayerRegion* region = connectivity_layer(*print.get_object(0), 0., 1., {target});
    region->fills.entities.push_back(new ExactRetainedFootprintPath(material.contour, 1.f));
    const json observed = nominal_connectivity(print);
    REQUIRE(observed.at("complete") == false);
    require_connectivity_contract(observed); // Unknown arrays, not a dropped region or widened bbox.
}

TEST_CASE("Exact missing regions accept a weak outer ring winding around a point touching hole",
          "[SliceAnalysis][Connectivity][ExactMissing]")
{
    for (const Point& shift : {Point(0, 0), Point(scaled(100.), scaled(-100.))}) {
        for (bool reverse : {false, true}) {
            // The repeated P=(0,2) is a contact, not a crossing. P->A->B->P
            // winds clockwise around a 1 scaled-square-unit triangular hole
            // inside the 4x4 counter-clockwise outer boundary.
            Polygon contour({Point(0, 0), Point(4, 0), Point(4, 4), Point(0, 4), Point(0, 2), Point(1, 3), Point(1, 1), Point(0, 2)});
            if (reverse)
                contour.reverse();
            contour.translate(shift);
            const ExPolygon target(contour);
            ExPolygon       material = grid_rectangle(2, 0, 4, 1);
            material.translate(shift);
            INFO("scaled shift: " << shift.x() << ',' << shift.y() << "; reversed: " << reverse);
            const auto whole = exact_missing(target, {}, [] {});
            REQUIRE(missing_twice_area(whole) == 30); // 2 * (16 - 1).
            const auto cut = exact_missing(target, {material}, [] {});
            REQUIRE(missing_twice_area(cut) == 26); // Subtract the separate 2x1 material corner.

            Model model;
            Print print;
            add_box(model, 1);
            apply_model(print, model);
            LayerRegion* region = connectivity_layer(*print.get_object(0), 0., 1., {target});
            region->fills.entities.push_back(new ExactRetainedFootprintPath(material.contour, 1.f));
            const json observed = nominal_connectivity(print);
            require_connectivity_contract(observed);
            REQUIRE(observed.at("complete") == true);
            REQUIRE(observed.at("target_components").size() == 1);
            const auto& component = observed.at("target_components").at(0);
            REQUIRE(component.at("target_volume_mm3").get<double>() == Approx(15e-12).epsilon(0.).margin(1e-26));
            REQUIRE(component.at("uncovered_volume_mm3").get<double>() == Approx(13e-12).epsilon(0.).margin(1e-26));
        }
    }
}

TEST_CASE("NonZero missing measurements preserve captured crossings and inputs under reversal and translation",
          "[SliceAnalysis][Connectivity][ExactMissing]")
{
    const std::array<Point, 3>   far_vertices{Point(3223, -4659), Point(390, -402), Point(15224, -21288)};
    const std::array<int64_t, 3> expected_twice_areas{1435, 11, 6063};
    const coord_t                large       = coord_t(9007199254741009LL);
    const auto                   determinant = [](const Point& a, const Point& b, const Point& c) {
        // Differences remain below 25000, even after the large translation.
        // These integer products do not depend on double area cancellation.
        const int64_t bx = int64_t(b.x()) - int64_t(a.x()), by = int64_t(b.y()) - int64_t(a.y());
        const int64_t cx = int64_t(c.x()) - int64_t(a.x()), cy = int64_t(c.y()) - int64_t(a.y());
        return bx * cy - by * cx;
    };
    for (size_t fixture = 0; fixture < far_vertices.size(); ++fixture) {
        for (const Point& shift : {Point(0, 0), Point(scaled(1000.), scaled(-1000.)), Point(large, -large)}) {
            for (bool reverse : {false, true}) {
                Polygon contour({Point(0, 0), far_vertices[fixture], Point(1, -1), Point(0, -1)});
                if (reverse)
                    contour.reverse();
                contour.translate(shift);
                const Points& points = contour.points;
                INFO("captured fixture " << fixture << "; scaled shift: " << shift.x() << ',' << shift.y() << "; reversed: " << reverse);
                const int64_t abc = determinant(points[0], points[1], points[2]);
                const int64_t abd = determinant(points[0], points[1], points[3]);
                const int64_t cda = determinant(points[2], points[3], points[0]);
                const int64_t cdb = determinant(points[2], points[3], points[1]);
                REQUIRE(abc != 0);
                REQUIRE(abd != 0);
                REQUIRE(cda != 0);
                REQUIRE(cdb != 0);
                REQUIRE((abc > 0) != (abd > 0));
                REQUIRE((cda > 0) != (cdb > 0)); // Edges 0 and 2 strictly cross, not a point/edge touch.
                const int64_t twice_area = abc + determinant(points[0], points[2], points[3]);
                REQUIRE(twice_area == expected_twice_areas[fixture] * (reverse ? -1 : 1));

                const ExPolygon crossing(contour);
                ExPolygon       valid = grid_rectangle(-1, -25000, 20000, 1);
                valid.translate(shift);
                const Points          crossing_before = crossing.contour.points, valid_before = valid.contour.points;
                ExPolygons            material{crossing}, after_covered{valid, crossing};
                const MissingRational small_leaf  = MissingRational(far_vertices[fixture].x()) / (-far_vertices[fixture].y());
                const MissingRational filled_area = MissingRational(expected_twice_areas[fixture]) + 2 * small_leaf;
                REQUIRE(missing_twice_area(exact_missing(crossing, {}, [] {})) == filled_area);
                REQUIRE(missing_twice_area(exact_missing(valid, material, [] {})) == MissingRational(2) * 20001 * 25001 - filled_area);
                REQUIRE(exact_missing(valid, after_covered, [] {}).empty());
                REQUIRE(crossing.contour.points == crossing_before);
                REQUIRE(valid.contour.points == valid_before);
                REQUIRE(material.front().contour.points == crossing_before);
                REQUIRE(after_covered.front().contour.points == valid_before);
                REQUIRE(after_covered.back().contour.points == crossing_before);
            }
        }
    }
}

namespace {

struct ExactRetainedFootprintGroupPath : ExtrusionPath
{
    Polygons footprint;
    explicit ExactRetainedFootprintGroupPath(Polygons polygons)
        : ExtrusionPath(erExternalPerimeter, 0.08, 0.4f, 1.f), footprint(std::move(polygons))
    {
        polyline = Polyline({footprint.front().points.at(0), footprint.front().points.at(1)});
    }
    void polygons_covered_by_width(Polygons& out, float) const override { out.insert(out.end(), footprint.begin(), footprint.end()); }
};

} // namespace

TEST_CASE("NonZero exact regions retain both rational leaves of captured crossing walks", "[SliceAnalysis][Connectivity][ExactRegions]")
{
    const std::array<Point, 3>           far_vertices{Point(3223, -4659), Point(390, -402), Point(15224, -21288)};
    const std::array<MissingRational, 3> expected_areas{MissingRational(6692111) / 4659, MissingRational(867) / 67,
                                                        MissingRational(16137449) / 2661};
    const coord_t                        large = coord_t(9007199254741009LL);
    for (size_t fixture = 0; fixture < far_vertices.size(); ++fixture) {
        const coord_t n = far_vertices[fixture].x(), m = -far_vertices[fixture].y();
        // The crossing is I=(n/m,-1). The upper and lower leaves have twice
        // areas n/m and (m-n-1)+n/m. NonZero fills both, not their signed sum.
        const MissingRational small_leaf = MissingRational(n) / m;
        const MissingRational large_leaf = MissingRational(m - n - 1) + small_leaf;
        REQUIRE(small_leaf + large_leaf == expected_areas[fixture]);
        for (const Point& shift : {Point(0, 0), Point(scaled(1000.), scaled(-1000.)), Point(large, -large)}) {
            for (bool reverse : {false, true}) {
                Polygon walk({Point(0, 0), far_vertices[fixture], Point(1, -1), Point(0, -1)});
                if (reverse)
                    walk.reverse();
                walk.translate(shift);
                const Points before = walk.points;
                INFO("NonZero fixture " << fixture << "; scaled shift: " << shift.x() << ',' << shift.y() << "; reversed: " << reverse);
                const auto observed = SliceAnalysisGeometry::regions_from_nonzero({walk}, [] {});
                REQUIRE(observed.size() == 2); // Only a point connects the two leaves.
                REQUIRE(missing_twice_area(observed) == expected_areas[fixture]);
                std::vector<MissingRational> areas;
                for (const auto& region : observed)
                    areas.push_back(region.twice_area);
                std::sort(areas.begin(), areas.end());
                REQUIRE(areas.at(0) == small_leaf);
                REQUIRE(areas.at(1) == large_leaf);
                const auto as_source = SliceAnalysisGeometry::region_from_expolygon(ExPolygon(walk), [] {});
                REQUIRE(as_source.size() == 2);
                REQUIRE(missing_twice_area(as_source) == expected_areas[fixture]);
                REQUIRE(walk.points == before);
            }
        }
    }
    // Algebraic area zero is not an empty NonZero set.
    const Polygon equal_bowtie({Point(0, 0), Point(4, 4), Point(0, 4), Point(4, 0)});
    const auto    equal_leaves = SliceAnalysisGeometry::regions_from_nonzero({equal_bowtie}, [] {});
    REQUIRE(equal_leaves.size() == 2);
    REQUIRE(missing_twice_area(equal_leaves) == 16);
}

TEST_CASE("NonZero source regions subtract each hole before unioning independent source owners",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    for (bool reverse : {false, true}) {
        ExPolygon source = grid_rectangle(0, 0, 4, 4);
        source.holes.push_back(grid_rectangle(1, 1, 3, 3).contour);
        source.holes.push_back(grid_rectangle(2, 1, 4, 3).contour);
        if (reverse) {
            source.contour.reverse();
            source.holes.front().reverse();
        } else {
            source.holes.back().reverse();
        }
        // The two holes' union has area 6, irrespective of their directions.
        const auto cut = SliceAnalysisGeometry::region_from_expolygon(source, [] {});
        REQUIRE(missing_twice_area(cut) == 20); // 2 * (16 - 6).
        auto filled = SliceAnalysisGeometry::region_from_expolygon(grid_rectangle(1, 1, 4, 3), [] {});
        filled.insert(filled.end(), cut.begin(), cut.end());
        const auto whole = SliceAnalysisGeometry::union_regions(filled, [] {});
        REQUIRE(whole.size() == 1);
        REQUIRE(missing_twice_area(whole) == 32);
        std::reverse(filled.begin(), filled.end());
        REQUIRE(missing_twice_area(SliceAnalysisGeometry::union_regions(filled, [] {})) == 32);
    }
}

TEST_CASE("NonZero path groups do not cancel oppositely directed independent path owners", "[SliceAnalysis][Connectivity][ExactRegions]")
{
    for (bool reverse : {false, true}) {
        for (bool swap : {false, true}) {
            Polygon a = grid_rectangle(0, 0, 4, 4).contour;
            Polygon b = grid_rectangle(2, 0, 6, 4).contour;
            b.reverse();
            if (reverse) {
                a.reverse();
                b.reverse();
            }
            if (swap)
                std::swap(a, b);
            const auto same_path = SliceAnalysisGeometry::regions_from_nonzero({a, b}, [] {});
            REQUIRE(same_path.size() == 2);
            REQUIRE(missing_twice_area(same_path) == 32); // Opposite winding cancels the 2x4 overlap only within one path.
            auto       separate = SliceAnalysisGeometry::regions_from_nonzero({a}, [] {});
            const auto second   = SliceAnalysisGeometry::regions_from_nonzero({b}, [] {});
            separate.insert(separate.end(), second.begin(), second.end());
            const auto independent_paths = SliceAnalysisGeometry::union_regions(separate, [] {});
            REQUIRE(independent_paths.size() == 1);
            REQUIRE(missing_twice_area(independent_paths) == 48); // Independent owners cover the entire 6x4 rectangle.

            for (bool independent : {false, true}) {
                Model model;
                Print print;
                add_box(model, 1);
                apply_model(print, model);
                LayerRegion* region = connectivity_layer(*print.get_object(0), 0., 1., {grid_rectangle(0, 0, 6, 4)});
                if (independent) {
                    region->fills.entities.push_back(new ExactRetainedFootprintGroupPath({a}));
                    region->fills.entities.push_back(new ExactRetainedFootprintGroupPath({b}));
                } else {
                    region->fills.entities.push_back(new ExactRetainedFootprintGroupPath({a, b}));
                }
                const json observed = nominal_connectivity(print);
                require_connectivity_contract(observed);
                REQUIRE(observed.at("complete") == true);
                REQUIRE(observed.at("target_components").size() == 1);
                const auto& component = observed.at("target_components").at(0);
                REQUIRE(component.at("target_volume_mm3").get<double>() == Approx(24e-12).epsilon(0.).margin(1e-26));
                REQUIRE(component.at("clipped_path_component_count") == (independent ? 1 : 2));
                REQUIRE(component.at("uncovered_volume_mm3").get<double>() == Approx(independent ? 0. : 8e-12).epsilon(0.).margin(1e-26));
            }
        }
    }
    Polygon positive = grid_rectangle(0, 0, 4, 4).contour, negative = positive;
    negative.reverse();
    REQUIRE(SliceAnalysisGeometry::regions_from_nonzero({positive, negative}, [] {}).empty());
    auto       owners    = SliceAnalysisGeometry::regions_from_nonzero({positive}, [] {});
    const auto duplicate = SliceAnalysisGeometry::regions_from_nonzero({negative}, [] {});
    owners.insert(owners.end(), duplicate.begin(), duplicate.end());
    REQUIRE(missing_twice_area(SliceAnalysisGeometry::union_regions(owners, [] {})) == 32);
}

TEST_CASE("Exact cross Z connectivity retains rational positive overlap but not edge or point contact",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    const ExPolygon lower(Polygon({Point(0, 0), Point(3, 0), Point(0, 1)}));
    const ExPolygon upper(Polygon({Point(0, 1), Point(2, 0), Point(2, 1)}));
    const auto      low  = SliceAnalysisGeometry::region_from_expolygon(lower, [] {});
    const auto      high = SliceAnalysisGeometry::region_from_expolygon(upper, [] {});
    REQUIRE(low.size() == 1);
    REQUIRE(high.size() == 1);
    const auto overlap = SliceAnalysisGeometry::intersect_regions(low.front(), high.front(), [] {});
    // Triangle (0,1),(2,0),(2,1/3): positive area 1/3 scaled² must connect across Z.
    REQUIRE(missing_twice_area(overlap) == MissingRational(2) / 3);
    {
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        connectivity_layer(*print.get_object(0), 0., 1., {lower});
        connectivity_layer(*print.get_object(0), 1., 2., {upper});
        const json observed = nominal_connectivity(print);
        require_connectivity_contract(observed);
        REQUIRE(observed.at("complete") == true);
        REQUIRE(observed.at("target_components").size() == 1);
        const auto& component = observed.at("target_components").at(0);
        REQUIRE(component.at("target_volume_mm3").get<double>() == Approx(2.5e-12).epsilon(0.).margin(1e-27));
        REQUIRE(component.at("uncovered_volume_mm3") == component.at("target_volume_mm3"));
    }
    const ExPolygon square         = grid_rectangle(0, 0, 1, 1);
    const auto      square_regions = SliceAnalysisGeometry::region_from_expolygon(square, [] {});
    for (const ExPolygon& touching : {grid_rectangle(1, 0, 2, 1), grid_rectangle(1, 1, 2, 2)}) {
        const auto other = SliceAnalysisGeometry::region_from_expolygon(touching, [] {});
        REQUIRE(SliceAnalysisGeometry::intersect_regions(square_regions.front(), other.front(), [] {}).empty());
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        connectivity_layer(*print.get_object(0), 0., 1., {square});
        connectivity_layer(*print.get_object(0), 1., 2., {touching});
        const json observed = nominal_connectivity(print);
        require_connectivity_contract(observed);
        REQUIRE(observed.at("complete") == true);
        REQUIRE(observed.at("target_components").size() == 2);
        for (const auto& component : observed.at("target_components"))
            REQUIRE(component.at("target_volume_mm3").get<double>() == Approx(1e-12).epsilon(0.).margin(1e-27));
    }
}

TEST_CASE("Exact region operations preserve rational shape through union intersection difference and cancellation",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    const auto whole = SliceAnalysisGeometry::region_from_expolygon(grid_rectangle(0, 0, 1, 1), [] {});
    const auto cut   = SliceAnalysisGeometry::regions_from_nonzero({Polygon({Point(0, 0), Point(3, 0), Point(0, 1)})}, [] {});
    REQUIRE(whole.size() == 1);
    REQUIRE(cut.size() == 1);
    const auto remainder = SliceAnalysisGeometry::subtract_regions(whole.front(), cut, [] {});
    REQUIRE(remainder.size() == 1);
    REQUIRE(remainder.front().twice_area == MissingRational(1) / 3);
    REQUIRE(remainder.front().min[1] == MissingRational(2) / 3);
    const auto covered = SliceAnalysisGeometry::intersect_regions(whole.front(), cut.front(), [] {});
    REQUIRE(missing_twice_area(covered) == MissingRational(5) / 3);
    auto pieces = covered;
    pieces.insert(pieces.end(), remainder.begin(), remainder.end());
    const auto rejoined = SliceAnalysisGeometry::union_regions(pieces, [] {});
    REQUIRE(rejoined.size() == 1);
    REQUIRE(rejoined.front().twice_area == 2);
    REQUIRE(SliceAnalysisGeometry::subtract_regions(whole.front(), whole, [] {}).empty());
    REQUIRE(SliceAnalysisGeometry::regions_from_nonzero({}, [] {}).empty());
    REQUIRE(SliceAnalysisGeometry::union_regions({}, [] {}).empty());
    REQUIRE(missing_twice_area(SliceAnalysisGeometry::subtract_regions(whole.front(), {}, [] {})) == 2);
    REQUIRE(remainder.front().twice_area == MissingRational(1) / 3); // Operations must not replace a retained opaque shape.
    REQUIRE(remainder.front().min[1] == MissingRational(2) / 3);
    const auto cancel = [] { throw CanceledException(); };
    REQUIRE_THROWS_AS(SliceAnalysisGeometry::region_from_expolygon(grid_rectangle(0, 0, 1, 1), cancel), CanceledException);
    REQUIRE_THROWS_AS(SliceAnalysisGeometry::regions_from_nonzero({grid_rectangle(0, 0, 1, 1).contour}, cancel), CanceledException);
    REQUIRE_THROWS_AS(SliceAnalysisGeometry::union_regions(pieces, cancel), CanceledException);
    REQUIRE_THROWS_AS(SliceAnalysisGeometry::intersect_regions(whole.front(), cut.front(), cancel), CanceledException);
    REQUIRE_THROWS_AS(SliceAnalysisGeometry::subtract_regions(whole.front(), cut, cancel), CanceledException);
}

TEST_CASE("Nominal connectivity uses NonZero crossing source regions and exact complete path coverage",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    for (bool reverse : {false, true}) {
        Polygon walk({Point(0, 0), Point(390, -402), Point(1, -1), Point(0, -1)});
        if (reverse)
            walk.reverse();
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        LayerRegion* region = connectivity_layer(*print.get_object(0), 0., 1., {ExPolygon(walk)});
        region->fills.entities.push_back(new ExactRetainedFootprintPath(grid_rectangle(-1, -403, 391, 1).contour, 1.f));
        const json observed = nominal_connectivity(print);
        require_connectivity_contract(observed);
        REQUIRE(observed.at("complete") == true);
        REQUIRE(observed.at("target_components").size() == 2); // Filled leaves only share the crossing point.
        std::vector<double> volumes;
        for (const auto& component : observed.at("target_components")) {
            volumes.push_back(component.at("target_volume_mm3").get<double>());
            REQUIRE(component.at("uncovered_volume_mm3") == 0.);
            REQUIRE(component.at("missing_slab_regions").empty());
            REQUIRE(component.at("clipped_path_component_count") == 1);
        }
        std::sort(volumes.begin(), volumes.end());
        REQUIRE(volumes.at(0) == Approx(65. / 67. * 0.5e-12).epsilon(0.).margin(1e-27));
        REQUIRE(volumes.at(1) == Approx(802. / 67. * 0.5e-12).epsilon(0.).margin(1e-26));
        REQUIRE(observed.at("cross_target_joins").size() == 1); // The covering path connects outside the two targets.
        REQUIRE(observed.at("cross_target_joins").at(0).at("target_component_indices") == json::array({0, 1}));
    }
}

TEST_CASE("Exact positive overlap agrees with full intersection across contacts holes and rational slivers",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    struct Fixture
    {
        ExPolygon       left, right;
        MissingRational twice_overlap;
    };
    ExPolygon holed = grid_rectangle(0, 0, 5, 5);
    holed.holes.push_back(grid_rectangle(1, 1, 4, 4).contour);
    const std::vector<Fixture> fixtures{{grid_rectangle(0, 0, 1, 1), grid_rectangle(1, 0, 2, 1), 0},
                                        {grid_rectangle(0, 0, 1, 1), grid_rectangle(1, 1, 2, 2), 0},
                                        {grid_rectangle(0, 0, 4, 4), grid_rectangle(1, 1, 3, 3), 8},
                                        {grid_rectangle(0, 0, 1, 1), grid_rectangle(0, 0, 1, 1), 2},
                                        {ExPolygon(Polygon({Point(0, 0), Point(3, 0), Point(0, 1)})),
                                         ExPolygon(Polygon({Point(0, 1), Point(2, 0), Point(2, 1)})), MissingRational(2) / 3},
                                        {holed, grid_rectangle(2, 2, 3, 3), 0}};
    const coord_t              large = coord_t(9007199254741009LL);
    for (size_t index = 0; index < fixtures.size(); ++index) {
        for (bool reverse : {false, true}) {
            for (const Point& shift : {Point(0, 0), Point(large, -large), Point(-large, large)}) {
                ExPolygon left = fixtures[index].left, right = fixtures[index].right;
                if (reverse) {
                    left.contour.reverse();
                    right.contour.reverse();
                    for (Polygon& hole : left.holes)
                        hole.reverse();
                }
                left.translate(shift);
                right.translate(shift);
                const auto a = SliceAnalysisGeometry::region_from_expolygon(left, [] {});
                const auto b = SliceAnalysisGeometry::region_from_expolygon(right, [] {});
                INFO("overlap fixture " << index << "; reverse " << reverse << "; scaled shift " << shift.x() << ',' << shift.y());
                REQUIRE(a.size() == 1);
                REQUIRE(b.size() == 1);
                const auto intersection = SliceAnalysisGeometry::intersect_regions(a.front(), b.front(), [] {});
                REQUIRE(missing_twice_area(intersection) == fixtures[index].twice_overlap);
                const bool overlap = SliceAnalysisGeometry::positive_area_overlap(a.front(), b.front(), [] {});
                REQUIRE(overlap == !intersection.empty());
                REQUIRE(overlap == (fixtures[index].twice_overlap > 0));
                REQUIRE(SliceAnalysisGeometry::positive_area_overlap(b.front(), a.front(), [] {}) == overlap);
                REQUIRE_THROWS_AS(SliceAnalysisGeometry::positive_area_overlap(a.front(), b.front(), [] { throw CanceledException(); }),
                                  CanceledException);
            }
        }
    }
}

TEST_CASE("Balanced exact union retains odd independent owners holes and contact topology in any order",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    ExPolygon frame = grid_rectangle(0, 0, 6, 6);
    frame.holes.push_back(grid_rectangle(1, 1, 5, 5).contour);
    ExPolygon extended(Polygon({Point(0, 0), Point(8, 0), Point(8, 2), Point(6, 2), Point(6, 6), Point(0, 6)}));
    extended.holes         = frame.holes;
    const ExPolygon island = grid_rectangle(2, 2, 3, 3), corner = grid_rectangle(8, 2, 9, 3);
    auto            expected = SliceAnalysisGeometry::region_from_expolygon(extended, [] {});
    for (const ExPolygon& source : {island, corner}) {
        const auto regions = SliceAnalysisGeometry::region_from_expolygon(source, [] {});
        expected.insert(expected.end(), regions.begin(), regions.end());
    }
    REQUIRE(expected.size() == 3);
    REQUIRE(missing_twice_area(expected) == 52); // Frame+edge extension:24; hole island:1; point-only corner:1.
    const auto require_same_set = [&](const SliceAnalysisGeometry::Regions& observed) {
        REQUIRE(observed.size() == 3);
        REQUIRE(missing_twice_area(observed) == 52);
        for (const auto& region : observed)
            REQUIRE(SliceAnalysisGeometry::subtract_regions(region, expected, [] {}).empty());
        for (const auto& region : expected)
            REQUIRE(SliceAnalysisGeometry::subtract_regions(region, observed, [] {}).empty());
    };
    for (bool reverse : {false, true}) {
        std::vector<ExPolygon> sources{frame, island, grid_rectangle(6, 0, 8, 2), corner, frame}; // Five leaves, including duplicate owner.
        SliceAnalysisGeometry::Regions owners;
        for (size_t index = 0; index < sources.size(); ++index) {
            if ((index % 2 == 0) == reverse) {
                sources[index].contour.reverse();
                for (Polygon& hole : sources[index].holes)
                    hole.reverse();
            }
            const auto regions = SliceAnalysisGeometry::region_from_expolygon(sources[index], [] {});
            REQUIRE(regions.size() == 1);
            owners.insert(owners.end(), regions.begin(), regions.end());
        }
        REQUIRE(owners.size() == 5);
        require_same_set(SliceAnalysisGeometry::union_regions(owners, [] {}));
        std::reverse(owners.begin(), owners.end());
        require_same_set(SliceAnalysisGeometry::union_regions(owners, [] {}));
        std::rotate(owners.begin(), owners.begin() + 2, owners.end());
        require_same_set(SliceAnalysisGeometry::union_regions(owners, [] {}));
        REQUIRE_THROWS_AS(SliceAnalysisGeometry::union_regions(owners, [] { throw CanceledException(); }), CanceledException);
    }
}

TEST_CASE("Exact empty and single region fast paths share immutable geometry without losing cancellation",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    const auto square = SliceAnalysisGeometry::region_from_expolygon(grid_rectangle(0, 0, 1, 1), [] {});
    const auto cutter = SliceAnalysisGeometry::regions_from_nonzero({Polygon({Point(0, 0), Point(3, 0), Point(0, 1)})}, [] {});
    const auto source = SliceAnalysisGeometry::subtract_regions(square.front(), cutter, [] {});
    REQUIRE(source.size() == 1);
    const auto shape   = source.front().shape;
    const auto minimum = source.front().min, maximum = source.front().max;
    REQUIRE(source.front().twice_area == MissingRational(1) / 3);
    REQUIRE(SliceAnalysisGeometry::union_regions({}, [] {}).empty());
    auto       single    = SliceAnalysisGeometry::union_regions(source, [] {});
    const auto unchanged = SliceAnalysisGeometry::subtract_regions(source.front(), {}, [] {});
    REQUIRE(single.size() == 1);
    REQUIRE(unchanged.size() == 1);
    REQUIRE(single.front().shape == shape);
    REQUIRE(unchanged.front().shape == shape);
    REQUIRE(single.front().twice_area == source.front().twice_area);
    REQUIRE(unchanged.front().twice_area == source.front().twice_area);
    REQUIRE(single.front().min == minimum);
    REQUIRE(unchanged.front().max == maximum);
    REQUIRE(SliceAnalysisGeometry::subtract_regions(single.front(), source, [] {}).empty());
    REQUIRE(missing_twice_area(SliceAnalysisGeometry::intersect_regions(unchanged.front(), square.front(), [] {})) ==
            MissingRational(1) / 3);
    // Region metadata is a value copy; the opaque shape is shared and const.
    single.front().min[0]     = -1;
    single.front().twice_area = 9;
    REQUIRE(source.front().shape == shape);
    REQUIRE(source.front().min == minimum);
    REQUIRE(source.front().max == maximum);
    REQUIRE(source.front().twice_area == MissingRational(1) / 3);
    const auto cancel = [] { throw CanceledException(); };
    REQUIRE_THROWS_AS(SliceAnalysisGeometry::union_regions({}, cancel), CanceledException);
    REQUIRE_THROWS_AS(SliceAnalysisGeometry::union_regions(source, cancel), CanceledException);
    REQUIRE_THROWS_AS(SliceAnalysisGeometry::subtract_regions(source.front(), {}, cancel), CanceledException);
}

TEST_CASE("Original five and seven point source contours retain three exact point touching components",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    struct Expected
    {
        MissingRational                twice_area;
        std::array<MissingRational, 2> min, max;
    };
    struct Fixture
    {
        Polygon                 contour;
        Point                   origin;
        MissingRational         total_twice_area;
        std::array<Expected, 3> components;
    };
    const auto fraction = [](const char* numerator, const char* denominator) -> MissingRational {
        return MissingRational(numerator) / MissingRational(denominator);
    };
    // Independent Fraction vertical-strip integration of the original source
    // walks, before any integer Boolean normalization. Each walk has three
    // positive-area NonZero components which meet only at crossing points.
    const std::vector<Fixture> fixtures{
        {Polygon({Point(0, 0), Point(-70183, 78100), Point(-2, 2), Point(227792, -195931), Point(227789, -195928)}),
         Point(14726273, -4696551),
         fraction("1992952276206463", "20197728305"),
         {{{fraction("63961157022126", "4039545661"),
            {-70183, fraction("4976688200", "4039545661")},
            {fraction("-4472201126", "4039545661"), 78100}},
           {fraction("514810812349744", "20197728305"),
            {fraction("-4472201126", "4039545661"), fraction("-391856", "5")},
            {fraction("455578", "5"), fraction("4976688200", "4039545661")}},
           {fraction("286749", "5"), {fraction("455578", "5"), -195931}, {227792, fraction("-391856", "5")}}}}},
        {Polygon({Point(0, 0), Point(-265791, 121254), Point(-415154, 227067), Point(-265795, 121256), Point(0, -1), Point(90941, -62235),
                  Point(90935, -62230)}),
         Point(20161777, -4869655),
         fraction("138107389864917515563", "260642577541059"),
         {{{fraction("1809082818464470547", "260642577541059"),
            {fraction("-1330720278810905", "5006628522"), fraction("10742902310", "104119")},
            {fraction("-23548639615", "104119"), fraction("22484303638483", "185430686")}},
           {fraction("311726262120383", "2503314261"),
            {-415154, fraction("22484303638483", "185430686")},
            {fraction("-1330720278810905", "5006628522"), 227067}},
           {fraction("41481679699", "104119"), {fraction("-23548639615", "104119"), -62235}, {90941, fraction("10742902310", "104119")}}}}}};
    const coord_t large = coord_t(9007199254741009LL);
    for (const Fixture& fixture : fixtures) {
        for (const Point& shift : {Point(0, 0), fixture.origin, Point(large, -large)}) {
            for (bool reverse : {false, true}) {
                Polygon walk = fixture.contour;
                walk.translate(shift);
                if (reverse)
                    walk.reverse();
                const ExPolygon input(walk);
                const Points    before  = input.contour.points;
                auto            regions = SliceAnalysisGeometry::region_from_expolygon(input, [] {});
                INFO("original source points " << before.size() << "; reverse " << reverse << "; scaled shift " << shift.x() << ','
                                               << shift.y());
                REQUIRE(input.contour.points == before);
                REQUIRE(regions.size() == 3);
                REQUIRE(missing_twice_area(regions) == fixture.total_twice_area);
                // Areas are distinct; sorting is only for truth comparison,
                // not a claim that component indices identify the same shape.
                std::sort(regions.begin(), regions.end(), [](const auto& a, const auto& b) { return a.twice_area < b.twice_area; });
                for (size_t index = 0; index < regions.size(); ++index) {
                    REQUIRE(regions[index].twice_area == fixture.components[index].twice_area);
                    for (size_t axis = 0; axis < 2; ++axis) {
                        REQUIRE(regions[index].min[axis] == fixture.components[index].min[axis] + shift[axis]);
                        REQUIRE(regions[index].max[axis] == fixture.components[index].max[axis] + shift[axis]);
                    }
                }
            }
        }
        // Exercise the public source-region route at its captured position;
        // the very large shifts above test exact geometry, not double JSON.
        Polygon source = fixture.contour;
        source.translate(fixture.origin);
        Model model;
        Print print;
        add_box(model, 1);
        apply_model(print, model);
        connectivity_layer(*print.get_object(0), 0., 1., {ExPolygon(source)});
        const json observed = nominal_connectivity(print);
        require_connectivity_contract(observed);
        REQUIRE(observed.at("complete") == true);
        REQUIRE(observed.at("target_components").size() == 3);
        REQUIRE(observed.at("cross_target_joins").empty());
        std::vector<double> volumes;
        double              total_volume = 0.;
        for (const auto& component : observed.at("target_components")) {
            const double volume = component.at("target_volume_mm3").get<double>();
            volumes.push_back(volume);
            total_volume += volume;
            REQUIRE(component.at("uncovered_volume_mm3") == component.at("target_volume_mm3"));
            REQUIRE(component.at("clipped_path_component_count") == 0);
            REQUIRE(component.at("missing_slab_regions").size() == 1);
            REQUIRE(component.at("missing_slab_regions").at(0).at("volume_mm3") == component.at("target_volume_mm3"));
        }
        std::sort(volumes.begin(), volumes.end());
        for (size_t index = 0; index < volumes.size(); ++index) {
            const MissingRational exact_volume = fixture.components[index].twice_area / MissingRational("2000000000000");
            REQUIRE(volumes[index] == Approx(exact_volume.convert_to<double>()).epsilon(8 * std::numeric_limits<double>::epsilon()));
        }
        const MissingRational exact_total = fixture.total_twice_area / MissingRational("2000000000000");
        REQUIRE(total_volume == Approx(exact_total.convert_to<double>()).epsilon(8 * std::numeric_limits<double>::epsilon()));
    }
}

TEST_CASE("Exact rational remnants retain overlap and point contact when combined again",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    const auto square = SliceAnalysisGeometry::region_from_expolygon(grid_rectangle(0, 0, 1, 1), [] {});
    const auto first_cut = SliceAnalysisGeometry::regions_from_nonzero({Polygon({Point(0, 0), Point(3, 0), Point(0, 1)})}, [] {});
    const auto second_cut = SliceAnalysisGeometry::regions_from_nonzero({Polygon({Point(1, 0), Point(-2, 0), Point(1, 1)})}, [] {});
    REQUIRE(square.size() == 1);
    const auto left  = SliceAnalysisGeometry::subtract_regions(square.front(), first_cut, [] {});
    const auto right = SliceAnalysisGeometry::subtract_regions(square.front(), second_cut, [] {});
    REQUIRE(left.size() == 1);
    REQUIRE(right.size() == 1);
    REQUIRE(missing_twice_area(left) == MissingRational(1) / 3);
    REQUIRE(missing_twice_area(right) == MissingRational(1) / 3);
    // The lower edges y=1-x/3 and y=2/3+x/3 cross at (1/2,5/6).
    // Neither remnant has a vertex strictly inside the other: the positive
    // overlap must still be found when an interior-witness shortcut cannot.
    const auto overlap = SliceAnalysisGeometry::intersect_regions(left.front(), right.front(), [] {});
    REQUIRE(overlap.size() == 1);
    REQUIRE(missing_twice_area(overlap) == MissingRational(1) / 6);
    REQUIRE(overlap.front().min[0] == 0);
    REQUIRE(overlap.front().min[1] == MissingRational(5) / 6);
    REQUIRE(overlap.front().max[0] == 1);
    REQUIRE(overlap.front().max[1] == 1);
    REQUIRE(SliceAnalysisGeometry::positive_area_overlap(left.front(), right.front(), [] {}));
    REQUIRE(SliceAnalysisGeometry::positive_area_overlap(right.front(), left.front(), [] {}));
    auto both = left;
    both.insert(both.end(), right.begin(), right.end());
    const auto joined = SliceAnalysisGeometry::union_regions(both, [] {});
    REQUIRE(joined.size() == 1);
    REQUIRE(missing_twice_area(joined) == MissingRational(1) / 2);

    const auto first_only  = SliceAnalysisGeometry::subtract_regions(left.front(), right, [] {});
    const auto second_only = SliceAnalysisGeometry::subtract_regions(right.front(), left, [] {});
    REQUIRE(first_only.size() == 1);
    REQUIRE(second_only.size() == 1);
    REQUIRE(missing_twice_area(first_only) == MissingRational(1) / 6);
    REQUIRE(missing_twice_area(second_only) == MissingRational(1) / 6);
    REQUIRE(first_only.front().min[0] == MissingRational(1) / 2);
    REQUIRE(second_only.front().max[0] == MissingRational(1) / 2);
    REQUIRE(SliceAnalysisGeometry::intersect_regions(first_only.front(), second_only.front(), [] {}).empty());
    REQUIRE_FALSE(SliceAnalysisGeometry::positive_area_overlap(first_only.front(), second_only.front(), [] {}));
    REQUIRE_FALSE(SliceAnalysisGeometry::positive_area_overlap(second_only.front(), first_only.front(), [] {}));
    auto point_touching = first_only;
    point_touching.insert(point_touching.end(), second_only.begin(), second_only.end());
    const auto separate = SliceAnalysisGeometry::union_regions(point_touching, [] {});
    REQUIRE(separate.size() == 2); // Only (1/2,5/6) is shared, not a positive-length boundary.
    REQUIRE(missing_twice_area(separate) == MissingRational(1) / 3);
    for (const auto& region : separate)
        REQUIRE(region.twice_area == MissingRational(1) / 6);
}

TEST_CASE("Batched exact owner construction matches separately built source and path sets",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    const auto require_equivalent = [](const SliceAnalysisGeometry::Regions& batch, const SliceAnalysisGeometry::Regions& separate,
                                       const MissingRational& expected_area, size_t expected_count) {
        REQUIRE(batch.size() == expected_count);
        REQUIRE(batch.size() == separate.size());
        REQUIRE(missing_twice_area(batch) == expected_area);
        REQUIRE(missing_twice_area(separate) == expected_area);
        for (const auto& region : batch) {
            const auto match = std::find_if(separate.begin(), separate.end(), [&](const auto& other) {
                return region.twice_area == other.twice_area && region.min == other.min && region.max == other.max;
            });
            REQUIRE(match != separate.end());
            REQUIRE(SliceAnalysisGeometry::subtract_regions(region, separate, [] {}).empty());
        }
        for (const auto& region : separate)
            REQUIRE(SliceAnalysisGeometry::subtract_regions(region, batch, [] {}).empty());
    };
    const Polygon         crossing({Point(0, 0), Point(390, -402), Point(1, -1), Point(0, -1)});
    const MissingRational crossing_area = MissingRational(867) / 67;

    SECTION("each source subtracts its own holes before independent owners are unioned")
    {
        ExPolygon source = grid_rectangle(0, 0, 6, 6);
        source.holes.push_back(grid_rectangle(1, 1, 4, 4).contour);
        source.holes.push_back(grid_rectangle(3, 1, 5, 4).contour);
        ExPolygon reversed = source;
        reversed.contour.reverse();
        for (Polygon& hole : reversed.holes)
            hole.reverse();
        const ExPolygons               sources{source, reversed, ExPolygon(crossing), ExPolygon{}};
        const ExPolygons               before = sources;
        std::vector<const ExPolygon*>  pointers;
        SliceAnalysisGeometry::Regions individually_built;
        for (const ExPolygon& owner : sources) {
            pointers.push_back(&owner);
            const auto regions = SliceAnalysisGeometry::region_from_expolygon(owner, [] {});
            individually_built.insert(individually_built.end(), regions.begin(), regions.end());
        }
        const auto separate = SliceAnalysisGeometry::union_regions(individually_built, [] {});
        // The overlapping holes remove 9+6-3=12 from the 6x6 owner.
        // Its reversed duplicate does not cancel it; the crossing leaves
        // touch that owner only at (0,0), so three components remain.
        const MissingRational expected = 48 + crossing_area;
        require_equivalent(SliceAnalysisGeometry::union_expolygons(pointers, [] {}), separate, expected, 3);
        std::reverse(pointers.begin(), pointers.end());
        require_equivalent(SliceAnalysisGeometry::union_expolygons(pointers, [] {}), separate, expected, 3);
        for (size_t index = 0; index < sources.size(); ++index) {
            REQUIRE(sources[index].contour.points == before[index].contour.points);
            REQUIRE(sources[index].holes.size() == before[index].holes.size());
            for (size_t hole = 0; hole < sources[index].holes.size(); ++hole)
                REQUIRE(sources[index].holes[hole].points == before[index].holes[hole].points);
        }
        size_t callbacks = 0;
        REQUIRE_THROWS_AS(SliceAnalysisGeometry::union_expolygons(pointers,
                                                                  [&] {
                                                                      if (++callbacks == 3)
                                                                          throw CanceledException();
                                                                  }),
                          CanceledException);
        REQUIRE(callbacks == 3);
    }

    SECTION("path owner grouping preserves directed multiplicity without cross owner cancellation")
    {
        Polygon a = grid_rectangle(0, 0, 4, 4).contour, negative_a = a;
        Polygon negative_b = grid_rectangle(2, 0, 6, 4).contour;
        negative_a.reverse();
        negative_b.reverse();
        struct Fixture
        {
            std::vector<Polygons> owners;
            MissingRational       twice_area;
            size_t                components;
        };
        const std::vector<Fixture> fixtures{{{{a, a, negative_a}},
                                             32,
                                             1}, // Winding 1+1-1 is one, not zero after a premature ring deduplication.
                                            {{{a, negative_a}}, 0, 0},
                                            {{{a}, {negative_a}}, 32, 1}, // Two independent opposite owners still cover the same square.
                                            {{{a, a, negative_a}, {negative_b}, {}, {crossing}}, 48 + crossing_area, 3}};
        for (const Fixture& fixture : fixtures) {
            auto                           owners = fixture.owners;
            const auto                     before = owners;
            SliceAnalysisGeometry::Regions individually_built;
            for (const Polygons& owner : owners) {
                const auto regions = SliceAnalysisGeometry::regions_from_nonzero(owner, [] {});
                individually_built.insert(individually_built.end(), regions.begin(), regions.end());
            }
            const auto separate = SliceAnalysisGeometry::union_regions(individually_built, [] {});
            require_equivalent(SliceAnalysisGeometry::union_nonzero_groups(owners, [] {}), separate, fixture.twice_area, fixture.components);
            for (size_t owner = 0; owner < owners.size(); ++owner) {
                REQUIRE(owners[owner].size() == before[owner].size());
                for (size_t ring = 0; ring < owners[owner].size(); ++ring)
                    REQUIRE(owners[owner][ring].points == before[owner][ring].points);
            }
            std::reverse(owners.begin(), owners.end());
            require_equivalent(SliceAnalysisGeometry::union_nonzero_groups(owners, [] {}), separate, fixture.twice_area, fixture.components);
        }
        size_t callbacks = 0;
        REQUIRE_THROWS_AS(SliceAnalysisGeometry::union_nonzero_groups(fixtures.back().owners,
                                                                      [&] {
                                                                          if (++callbacks == 3)
                                                                              throw CanceledException();
                                                                      }),
                          CanceledException);
        REQUIRE(callbacks == 3);
    }

    SECTION("empty batches remain empty and structural errors and cancellation propagate")
    {
        const ExPolygon empty;
        REQUIRE(SliceAnalysisGeometry::union_expolygons({}, [] {}).empty());
        REQUIRE(SliceAnalysisGeometry::union_expolygons({&empty}, [] {}).empty());
        REQUIRE(SliceAnalysisGeometry::union_nonzero_groups({}, [] {}).empty());
        REQUIRE(SliceAnalysisGeometry::union_nonzero_groups({{}}, [] {}).empty());
        REQUIRE_THROWS_AS(SliceAnalysisGeometry::union_expolygons({nullptr}, [] {}), std::runtime_error);
        const auto cancel = [] { throw CanceledException(); };
        REQUIRE_THROWS_AS(SliceAnalysisGeometry::union_expolygons({}, cancel), CanceledException);
        REQUIRE_THROWS_AS(SliceAnalysisGeometry::union_nonzero_groups({}, cancel), CanceledException);
    }
}

TEST_CASE("Nominal missing totals retain multiple rational cells when later target layers merge roots",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject&  object = *print.get_object(0);
    LayerRegion*  first  = connectivity_layer(object, 0., 1.,
                                              {grid_rectangle(0, 0, 1, 1), grid_rectangle(4, 0, 5, 1), grid_rectangle(10, 0, 11, 1)});
    const Polygon band({Point(-2, 0), Point(0, 0), Point(3, 1), Point(1, 1)});
    for (coord_t x : {coord_t(0), coord_t(4)}) {
        Polygon footprint = band;
        footprint.translate(Point(x, 0));
        first->fills.entities.push_back(new ExactRetainedFootprintPath(footprint, 1.f));
    }
    // At each unit target the band leaves two triangles, each area 1/6:
    // below y=x/3 and above y=(x+2)/3, in local scaled coordinates.
    // Spacing the targets by four avoids the other band's outside extension.
    // The next 2 mm slab joins both target roots but contains no material.
    connectivity_layer(object, 1., 3., {grid_rectangle(0, 0, 5, 1)});
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("complete") == true);
    REQUIRE(observed.at("target_components").size() == 2);
    REQUIRE(observed.at("cross_target_joins").empty());

    struct ExpectedMissing
    {
        std::array<double, 3> min, max;
        MissingRational       volume_mm3;
    };
    const MissingRational              unit_volume = MissingRational(1) / MissingRational("1000000000000");
    const std::vector<ExpectedMissing> merged_missing{{{0., 0., 0.}, {1e-6, 1e-6 / 3., 1.}, unit_volume / 6},
                                                      {{0., 2e-6 / 3., 0.}, {1e-6, 1e-6, 1.}, unit_volume / 6},
                                                      {{4e-6, 0., 0.}, {5e-6, 1e-6 / 3., 1.}, unit_volume / 6},
                                                      {{4e-6, 2e-6 / 3., 0.}, {5e-6, 1e-6, 1.}, unit_volume / 6},
                                                      {{0., 0., 1.}, {5e-6, 1e-6, 3.}, 10 * unit_volume}};
    const std::vector<ExpectedMissing> isolated_missing{{{10e-6, 0., 0.}, {11e-6, 1e-6, 1.}, unit_volume}};
    const auto json_approx = [](double expected) { return Approx(expected).epsilon(8 * std::numeric_limits<double>::epsilon()); };
    for (const auto& component : observed.at("target_components")) {
        const bool            isolated       = component.at("bounds_mm").at("min").at(0).get<double>() > 5e-6;
        const MissingRational target_volume  = (isolated ? 1 : 12) * unit_volume;
        const MissingRational missing_volume = isolated ? unit_volume : MissingRational(32) / 3 * unit_volume;
        // Merged target: 1+1+(5*2)=12 scaled²·mm; missing:
        // four 1/6 triangles plus the empty upper slab gives 32/3.
        REQUIRE(component.at("target_volume_mm3").get<double>() == json_approx(target_volume.convert_to<double>()));
        REQUIRE(component.at("uncovered_volume_mm3").get<double>() == json_approx(missing_volume.convert_to<double>()));
        REQUIRE(component.at("clipped_path_component_count") == (isolated ? 0 : 2));
        auto       missing  = component.at("missing_slab_regions").get<std::vector<json>>();
        const auto position = [](const json& cell) {
            const auto& minimum = cell.at("bounds_mm").at("min");
            return std::array<double, 3>{minimum.at(2).get<double>(), minimum.at(0).get<double>(), minimum.at(1).get<double>()};
        };
        std::sort(missing.begin(), missing.end(), [&](const json& a, const json& b) { return position(a) < position(b); });
        const auto& expected = isolated ? isolated_missing : merged_missing;
        REQUIRE(missing.size() == expected.size());
        for (size_t index = 0; index < expected.size(); ++index) {
            REQUIRE(missing[index].at("volume_mm3").get<double>() == json_approx(expected[index].volume_mm3.convert_to<double>()));
            for (size_t axis = 0; axis < 3; ++axis) {
                REQUIRE(missing[index].at("bounds_mm").at("min").at(axis).get<double>() == json_approx(expected[index].min[axis]));
                REQUIRE(missing[index].at("bounds_mm").at("max").at(axis).get<double>() == json_approx(expected[index].max[axis]));
            }
        }
    }
}

TEST_CASE("Nominal missing totals with interleaved denominators equal the serial rational reference exactly",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    const std::array<std::array<coord_t, 7>, 2> denominators{{{3, 5, 7, 11, 13, 17, 19}, {23, 29, 31, 37, 41, 43, 47}}};
    const std::array<coord_t, 2>                offsets{0, 100};
    std::array<MissingRational, 2>              serial_twice_volumes{0, 0};
    Model                                       model;
    Print                                       print;
    add_box(model, 1);
    apply_model(print, model);
    for (size_t layer = 0; layer < denominators.front().size(); ++layer) {
        LayerRegion* region = connectivity_layer(*print.get_object(0), double(layer), double(layer + 1),
                                                 {grid_rectangle(0, 0, 1, 1), grid_rectangle(100, 0, 101, 1)});
        for (size_t target = 0; target < offsets.size(); ++target) {
            const coord_t denominator = denominators[target][layer];
            Polygon       footprint({Point(0, 0), Point(denominator, 0), Point(0, 1)});
            footprint.translate(Point(offsets[target], 0));
            region->fills.entities.push_back(new ExactRetainedFootprintPath(footprint, 1.f));
            // The line y=1-x/n leaves a unit-width triangle with twice-area
            // 1/n. One millimetre is 1,000,000 scaled Z units. Accumulate
            // serially and interleave targets, independently of the reducer.
            serial_twice_volumes[target] += MissingRational(1000000) / denominator;
        }
    }
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("complete") == true);
    REQUIRE(observed.at("target_components").size() == 2);
    REQUIRE(observed.at("cross_target_joins").empty());
    const auto original_double_conversion = [](const MissingRational& twice_volume) {
        return twice_volume.convert_to<double>() * 0.5 * SCALING_FACTOR * SCALING_FACTOR * SCALING_FACTOR;
    };
    std::array<bool, 2> seen{false, false};
    for (const auto& component : observed.at("target_components")) {
        const size_t target = component.at("bounds_mm").at("min").at(0).get<double>() > 50e-6 ? 1 : 0;
        REQUIRE_FALSE(seen[target]);
        seen[target] = true;
        REQUIRE(component.at("target_volume_mm3").get<double>() == original_double_conversion(MissingRational(14000000)));
        REQUIRE(component.at("uncovered_volume_mm3").get<double>() == original_double_conversion(serial_twice_volumes[target]));
        REQUIRE(component.at("clipped_path_component_count") == 1);
        auto missing = component.at("missing_slab_regions").get<std::vector<json>>();
        REQUIRE(missing.size() == 7); // Each reduction has an odd, non-power-of-two number of rational terms.
        std::sort(missing.begin(), missing.end(), [](const json& a, const json& b) {
            return a.at("bounds_mm").at("min").at(2).get<double>() < b.at("bounds_mm").at("min").at(2).get<double>();
        });
        for (size_t layer = 0; layer < missing.size(); ++layer) {
            const MissingRational twice_volume = MissingRational(1000000) / denominators[target][layer];
            REQUIRE(missing[layer].at("volume_mm3").get<double>() == original_double_conversion(twice_volume));
            REQUIRE(missing[layer].at("bounds_mm").at("min").at(2).get<double>() == double(layer));
            REQUIRE(missing[layer].at("bounds_mm").at("max").at(2).get<double>() == double(layer + 1));
        }
    }
    REQUIRE(seen[0]);
    REQUIRE(seen[1]);
}

TEST_CASE("Clipped cross Z connectivity uses parent roots only as a necessary condition", "[SliceAnalysis][Connectivity][ExactRegions]")
{
    const ExPolygon square = rectangle(0., 0., 2., 2.);
    Polygon         a({Point(0, 0), Point(scaled(2.), 0), Point(0, scaled(2.))});
    Polygon         b({Point(scaled(2.), scaled(2.)), Point(scaled(2.), scaled(1.)), Point(scaled(1.), scaled(2.))});
    a.make_counter_clockwise();
    b.make_counter_clockwise();
    REQUIRE(get_extents(a).overlap(get_extents(b)));
    // A has x+y<=2 and area 2; B has x+y>=3 and area 1/2.
    // Their boxes overlap, but their actual positive-area intersection is empty.
    std::array<ExPolygons, 2> targets{{{square}, {square}}};
    std::array<Polygons, 2>   materials{{{a}, {b}}};
    std::vector<double>       expected_volumes{8.};
    double                    expected_missing = 5.5;
    size_t                    expected_clipped = 2, expected_joins = 0;

    SECTION("same target root but different material roots") {}
    SECTION("same material root but different target roots")
    {
        targets          = {{{ExPolygon(a)}, {ExPolygon(b)}}};
        materials        = {{{square.contour}, {square.contour}}};
        expected_volumes = {0.5, 2.};
        expected_missing = 0.;
        expected_clipped = 1;
        expected_joins   = 1;
    }
    SECTION("both parent roots match but the clipped regions do not overlap")
    {
        Polygon outer = rectangle(-1., -1., 4., 4.).contour;
        Polygon hole  = square.contour;
        outer.make_counter_clockwise();
        hole.make_clockwise();
        // The same exterior frame connects M across Z, but lies outside T.
        // Therefore both parent roots match while C is still A below and B above.
        materials = {{{outer, hole, a}, {outer, hole, b}}};
    }

    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    for (size_t layer = 0; layer < targets.size(); ++layer) {
        LayerRegion* region = connectivity_layer(*print.get_object(0), double(layer), double(layer + 1), targets[layer]);
        region->fills.entities.push_back(new ExactRetainedFootprintGroupPath(materials[layer]));
    }
    const json observed = nominal_connectivity(print);
    require_connectivity_contract(observed);
    REQUIRE(observed.at("complete") == true);
    REQUIRE(observed.at("target_components").size() == expected_volumes.size());
    REQUIRE(observed.at("cross_target_joins").size() == expected_joins);
    if (expected_joins != 0)
        REQUIRE(observed.at("cross_target_joins").at(0).at("target_component_indices").size() == 2);
    std::vector<double> volumes;
    for (const auto& component : observed.at("target_components")) {
        volumes.push_back(component.at("target_volume_mm3").get<double>());
        REQUIRE(component.at("uncovered_volume_mm3").get<double>() ==
                Approx(expected_missing).epsilon(8 * std::numeric_limits<double>::epsilon()));
        REQUIRE(component.at("clipped_path_component_count") == expected_clipped);
    }
    std::sort(volumes.begin(), volumes.end());
    for (size_t index = 0; index < expected_volumes.size(); ++index)
        REQUIRE(volumes[index] == Approx(expected_volumes[index]).epsilon(8 * std::numeric_limits<double>::epsilon()));
}

TEST_CASE("Missing subtraction retains true coverage while ignoring empty bounding box edge and point candidates",
          "[SliceAnalysis][Connectivity][ExactRegions]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    const ExPolygon target = rectangle(0., 0., 2., 2.);
    LayerRegion*    region = connectivity_layer(*print.get_object(0), 0., 1., {target});
    region->fills.entities.push_back(new ExactRetainedFootprintPath(rectangle(0., 0., 1., 2.).contour, 1.f));
    const json baseline = nominal_connectivity(print);
    require_connectivity_contract(baseline);
    REQUIRE(baseline.at("complete") == true);
    REQUIRE(baseline.at("target_components").size() == 1);
    const auto& component = baseline.at("target_components").at(0);
    REQUIRE(component.at("target_volume_mm3").get<double>() == Approx(4.).epsilon(8 * std::numeric_limits<double>::epsilon()));
    REQUIRE(component.at("uncovered_volume_mm3").get<double>() == Approx(2.).epsilon(8 * std::numeric_limits<double>::epsilon()));
    REQUIRE(component.at("clipped_path_component_count") == 1);
    REQUIRE(component.at("missing_slab_regions").size() == 1);
    REQUIRE(baseline.at("cross_target_joins").empty());

    const Polygon box_only({Point(scaled(1.), scaled(4.)), Point(scaled(4.), scaled(1.)), Point(scaled(4.), scaled(4.))});
    REQUIRE(get_extents(target).overlap(get_extents(box_only)));
    // The triangle has x+y>=5, outside the target's x+y<=4, despite overlapping boxes.
    const Polygons irrelevant{box_only, rectangle(2., 0., 1., 1.).contour, rectangle(2., 2., 1., 1.).contour};
    for (size_t candidate = 0; candidate < irrelevant.size(); ++candidate) {
        INFO("added irrelevant material candidate " << candidate);
        region->fills.entities.push_back(new ExactRetainedFootprintPath(irrelevant[candidate], 1.f));
        const json observed = nominal_connectivity(print);
        require_connectivity_contract(observed);
        REQUIRE(observed == baseline); // Every field and Missing record remains identical, not just its total volume.
    }
}
