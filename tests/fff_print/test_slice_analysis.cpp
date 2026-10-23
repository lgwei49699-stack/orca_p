#include <catch2/catch.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/SliceAnalysis.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
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
    REQUIRE(gcode.at("extruders").empty());
    REQUIRE(gcode.at("normal_estimate").at("layer_sequence_count") == 0);
    REQUIRE(gcode.size() == 4);
    REQUIRE(gcode.at("small_perimeter_selection").at("complete") == false);
    REQUIRE(gcode.at("quality_paths").at("rows").empty());
    REQUIRE(gcode.at("normal_estimate").size() == 2);
    REQUIRE_FALSE(report.at("limitations").empty());
    REQUIRE(json::parse(report.dump()) == report);
}

TEST_CASE("Slice analysis keeps tuning metrics without retired geometry diagnostics", "[SliceAnalysis]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer* layer = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices = {square_with_hole()};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->fills.append(planned_path(0., 1., 10., 1.));
    const auto before = all_settings(print);
    const auto paths = region->fills.as_polylines();
    const json report = make_slice_analysis_report(print, InitializedResult{});
    const auto& group = report.at("objects").at("groups").at(0);
    for (const char* key : {"nominal_target_path_connectivity", "source_volume_sampling", "source_surface_layer_alignment",
                            "solid_fill_target_depth", "planned_material_depth", "joint_lower_path_slab_coverage",
                            "planned_support_projection", "cross_layer_geometry", "external_bridge_projection",
                            "planned_perimeter_loops", "planned_bridge_paths"})
        REQUIRE_FALSE(group.contains(key));
    const auto& coverage = group.at("planned_layer_coverage").at("layers").at(0);
    for (const char* key : {"section_min_island_gap_mm", "parallel_boundary_clearance", "completely_unprinted_island_count",
                            "hole_intrusion_area_mm2", "closed_void_fully_covered_count", "solid_fill_uncovered_area_mm2"})
        REQUIRE(coverage.contains(key));
    for (const char* key : {"section_uncovered_area_mm2", "section_islands_with_multiple_planned_regions_count",
                            "planned_regions_overlapping_multiple_section_islands_count",
                            "closed_voids_with_multiple_remaining_regions_count", "planned_enclosed_exterior_region_count",
                            "planned_enclosed_exterior_area_mm2", "section_covered_area_mm2",
                            "top_fill_target_area_mm2", "top_fill_uncovered_area_mm2",
                            "bottom_fill_target_area_mm2", "bottom_fill_uncovered_area_mm2"})
        REQUIRE_FALSE(coverage.contains(key));
    const auto& gcode = report.at("gcode");
    for (const char* key : {"move_counts", "travel", "seam_record_count", "extrusion_ranges", "extrusion_by_role", "filament_by_role"})
        REQUIRE_FALSE(gcode.contains(key));
    REQUIRE_FALSE(gcode.at("normal_estimate").contains("layer_duration_sequence_s"));
    const auto& bed = report.at("planned_plate_bed_footprint");
    REQUIRE(bed.size() == 6);
    REQUIRE(bed.at("complete") == true);
    REQUIRE(bed.at("model_area_mm2").get<double>() > 0.);
    REQUIRE(all_settings(print) == before);
    REQUIRE(region->fills.as_polylines() == paths);
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
    for (const char* key : {"section_island_count", "closed_void_region_count", "closed_void_fully_covered_count"})
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
    REQUIRE(gcode.at("normal_estimate").at("layer_sequence_count") == 2);
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
    REQUIRE(gcode.at("normal_estimate").at("total_time_s").is_null());
    REQUIRE(gcode.at("extruders").at(0).at("estimated_volume_mm3").at("total").is_null());
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
    REQUIRE(first.at("solid_fill_target_area_mm2") == 0.);
    REQUIRE(first.at("solid_fill_uncovered_area_mm2") == 0.);
    REQUIRE(first.at("completely_unprinted_island_count") == 1);
    REQUIRE(first.at("completely_unprinted_island_area_mm2").get<double>() == Approx(4.));
    const auto group = make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0);
    REQUIRE(group.at("instance_count") == 2);
}

TEST_CASE("Slice analysis plate bed footprint distinguishes model paths from the first section and from elevation", "[SliceAnalysis]")
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
            .at("planned_plate_bed_footprint")
            .at("model_area_mm2");
    };

    REQUIRE(footprint().get<double>() == Approx(3.2).margin(1e-5));
    layer->print_z = 0.2 + SCALING_FACTOR * 0.5;
    REQUIRE(footprint().get<double>() == Approx(3.2).margin(1e-5));
    layer->print_z = 1.2;
    object->add_support_layer(0, 0, 0.4, 0.4);
    REQUIRE(footprint() == 0.);
    layer->print_z = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(footprint().is_null());
    layer->print_z = 0.1; // A path bottom below the bed is not a bed-contact footprint.
    REQUIRE(footprint() == 0.);
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
    REQUIRE(first.at("solid_fill_uncovered_area_mm2").get<double>() == Approx(12.));
}

TEST_CASE("Slice analysis attributes only the peak solid gap and separates overlapping target kinds", "[SliceAnalysis][SolidGapPeak]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    for (size_t index = 0; index < 2; ++index) {
        Layer* layer        = object->add_layer(index, 0.2, 0.2 * (index + 1), 0.1 + 0.2 * index);
        layer->lslices      = {rectangle(0., 0., 10., 10.)};
        LayerRegion* region = layer->add_region(&object->printing_region(0));
        region->fill_surfaces.append({rectangle(0., 0., 2., 2.)}, stTop);
        if (index == 1) {
            region->fill_surfaces.append({rectangle(0., 0., 2., 2.)}, stBottom); // Same category: no double count.
            region->fill_surfaces.append({rectangle(2., 0., 2., 2.)}, stInternalSolid);
            region->fill_surfaces.append({rectangle(4., 0., 2., 2.)}, stBottomBridge);
            region->fill_surfaces.append({rectangle(6., 0., 2., 2.)}, stInternalBridge);
            region->fill_surfaces.append({rectangle(6., 0., 2., 2.)}, stTop);       // Mixed category: separate, not attributed twice.
            region->fill_surfaces.append({rectangle(0., 2., 10., 8.)}, stInternal); // Sparse infill is not a solid target.
            region->fills.append(planned_path(0., 1., 2., 1., 2.f));
        }
    }
    const auto peak = [&]() {
        return make_slice_analysis_report(print, InitializedResult{})
            .at("objects")
            .at("groups")
            .at(0)
            .at("planned_layer_coverage")
            .at("solid_fill_peak");
    };
    const auto result = peak();
    REQUIRE(result.at("scope") == "largest_solid_uncovered_layer_exclusive_target_kinds");
    REQUIRE(result.at("complete") == true);
    REQUIRE(result.at("layer_index") == 1);
    REQUIRE(result.at("print_z_mm").get<double>() == Approx(0.4));
    REQUIRE(result.at("uncovered_area_mm2").get<double>() == Approx(12.));
    REQUIRE(result.at("surface_area_mm2") == 0.);
    REQUIRE(result.at("internal_solid_area_mm2").get<double>() == Approx(4.));
    REQUIRE(result.at("bridge_area_mm2").get<double>() == Approx(4.));
    REQUIRE(result.at("mixed_area_mm2").get<double>() == Approx(4.));

    object->layers().front()->regions().front()->fills.append(planned_path(0., 1., 2., 1., std::numeric_limits<float>::quiet_NaN()));
    const auto invalid = peak();
    REQUIRE(invalid.at("complete") == false); // An incomplete non-peak layer invalidates the maximum too.
    for (const char* key : {"layer_index", "print_z_mm", "uncovered_area_mm2", "surface_area_mm2", "internal_solid_area_mm2",
                            "bridge_area_mm2", "mixed_area_mm2"})
        REQUIRE(invalid.at(key).is_null());
}

TEST_CASE("Slice analysis peak gap distinguishes empty, fully covered and absent layers", "[SliceAnalysis][SolidGapPeak]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    const auto   peak   = [&]() {
        return make_slice_analysis_report(print, InitializedResult{})
            .at("objects")
            .at("groups")
            .at(0)
            .at("planned_layer_coverage")
            .at("solid_fill_peak");
    };
    REQUIRE(peak().at("complete") == false);
    Layer* layer        = object->add_layer(0, 0.2, 0.2, 0.1);
    layer->lslices      = {rectangle(0., 0., 2., 2.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    REQUIRE(peak().at("complete") == true);
    REQUIRE(peak().at("layer_index").is_null());
    REQUIRE(peak().at("uncovered_area_mm2") == 0.);
    region->fill_surfaces.append({rectangle(0., 0., 2., 2.)}, stTop);
    region->fills.append(planned_path(0., 1., 2., 1., 2.f));
    const auto covered = peak();
    REQUIRE(covered.at("complete") == true);
    REQUIRE(covered.at("layer_index").is_null());
    REQUIRE(covered.at("surface_area_mm2") == 0.);
    REQUIRE(covered.at("uncovered_area_mm2") == 0.);
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
         {"planned_path_area_mm2", "completely_unprinted_island_count",
          "completely_unprinted_island_area_mm2", "hole_intrusion_area_mm2", "solid_fill_uncovered_area_mm2", "section_island_count", "closed_void_region_count", "closed_void_fully_covered_count",
          "section_min_island_gap_mm"})
        REQUIRE(first.at(key).is_null());
    print.cancel();
    REQUIRE_THROWS_AS(make_slice_analysis_report(print, InitializedResult{}), CanceledException);
}

TEST_CASE("Gcode analysis leaves native distances and time unchanged through travel coordinate modes",
          "[SliceAnalysis][GCodeProcessor][AnalysisCollection]")
{
    GCodeProcessor baseline;
    GCodeProcessor processor(true);
    for (auto* parser : {&baseline, &processor}) {
        parser->apply_config(PrintConfig{});
        parser->initialize("slice-analysis-travel-regression.gcode");
        parser->process_buffer("G90\nM83\nT0\nG1 Z0.2 F1200\nG1 X10 E1\nG1 X13 Y4\nG1 E-1\nG1 E1\n"
                               "G1 X16 Y8\nG2 X14 Y8 I-1 J0\nG92 X100 Y100\nG91\nG1 X3 Y4\nG90\nG1 X100 Y100\n"
                               "G1 Z1.2 E-1\n");
        parser->finalize(false);
    }
    const auto& actual   = processor.get_result();
    const auto& expected = baseline.get_result();
    REQUIRE(actual.moves.size() == expected.moves.size());
    size_t travel_count = 0;
    for (size_t index = 0; index < actual.moves.size(); ++index) {
        const auto& move = actual.moves[index];
        REQUIRE(move.type == expected.moves[index].type);
        REQUIRE(move.position == expected.moves[index].position);
        REQUIRE(move.travel_dist == expected.moves[index].travel_dist);
        if (move.type == EMoveType::Travel)
            ++travel_count;
        if (move.is_arc_move())
            REQUIRE(move.travel_dist == Approx(M_PI));
        if (move.type == EMoveType::Extrude)
            REQUIRE(move.travel_dist == Approx(10.));
    }
    REQUIRE(travel_count == 7);
    const auto normal = static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE(actual.print_statistics.modes[normal].time == expected.print_statistics.modes[normal].time);
    REQUIRE(actual.print_statistics.total_volumes_per_extruder == expected.print_statistics.total_volumes_per_extruder);
    Print print;
    REQUIRE_FALSE(make_slice_analysis_report(print, actual).at("gcode").contains("travel"));
}

TEST_CASE("Slice analysis omits seam coordinates without changing processor preview positions", "[SliceAnalysis][GCodeProcessor]")
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
    REQUIRE_FALSE(gcode.contains("seam_positions_mm"));
    REQUIRE_FALSE(gcode.contains("seam_position_bounds_mm"));
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

    config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats({10.}));
    print.apply(model, config);
    REQUIRE(materials().at(1).at("configured_filament_max_volumetric_speed_mm3_s") == 10.);

    result.moves.front().feedrate = std::numeric_limits<float>::infinity();
    REQUIRE(materials().at(0).at("configured_filament_max_volumetric_speed_mm3_s") == 10.);
    result.moves.clear();
    result.print_statistics.total_volumes_per_extruder = {{1, 5.}, {3, 5.}};
    const auto no_moves                                = materials();
    REQUIRE(no_moves.at(0).at("extruder_id") == 1);
    REQUIRE(no_moves.at(0).at("configured_filament_max_volumetric_speed_mm3_s") == 10.);
    REQUIRE(no_moves.at(1).at("extruder_id") == 3);
    REQUIRE(no_moves.at(1).at("configured_filament_max_volumetric_speed_mm3_s").is_null());
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
        if (std::isfinite(cap))
            REQUIRE(report.at("configured_settings").at("filament_max_volumetric_speed") ==
                    config.option("filament_max_volumetric_speed")->serialize());
        else
            REQUIRE(report.at("configured_settings").at("filament_max_volumetric_speed").is_null());
        REQUIRE(json::parse(report.dump()) == report);
    }
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
    REQUIRE(measured.at("completely_unprinted_island_count") == 0);
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
    size_t unprinted = 0, filled_voids = 0;
    double unprinted_area = 0.;
    for (const ExPolygon& island : section) {
        const auto regions = count(intersection_ex(ExPolygons{island}, covered));
        if (regions == 0) {
            ++unprinted;
            unprinted_area += island.area() * SCALING_FACTOR * SCALING_FACTOR;
        }
    }
    for (const ExPolygon& hole : holes) {
        const auto remaining = count(diff_ex(ExPolygons{hole}, covered));
        filled_voids += remaining == 0;
    }
    const json expected{{"section_island_count", count(section)},
                        {"completely_unprinted_island_count", unprinted},
                        {"closed_void_region_count", count(holes)},
                        {"closed_void_fully_covered_count", filled_voids},
                        {"completely_unprinted_island_area_mm2", unprinted_area},
                        {"section_area_mm2", area(section)},
                        {"planned_path_area_mm2", area(covered)},
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
    REQUIRE(measured.at("section_island_count") == copies * 10);
    REQUIRE(measured.at("completely_unprinted_island_count") == copies * 4);
    REQUIRE(measured.at("closed_void_region_count") == copies * 8);
    REQUIRE(measured.at("closed_void_fully_covered_count") == copies);
    REQUIRE(measured.at("section_area_mm2").get<double>() == Approx(copies * 680.));
    REQUIRE(measured.at("hole_intrusion_area_mm2").get<double>() == Approx(copies * 22.));
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
         {"planned_path_area_mm2", "completely_unprinted_island_count",
          "completely_unprinted_island_area_mm2", "hole_intrusion_area_mm2", "solid_fill_uncovered_area_mm2", "section_island_count", "closed_void_region_count", "closed_void_fully_covered_count",
          "section_min_island_gap_mm"})
        REQUIRE(measured.at(key).is_null());
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

namespace {

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
    std::string overrides;
    std::string move          = "G1 X1 E0.05 F6000\n";
    float       expected_peak = std::sqrt(200.f);
    SECTION("short extrusion never borrows the requested cruise peak") {}
    SECTION("long extrusion reaches the requested cruise peak")
    {
        move          = "G1 X100 E5 F6000\n";
        expected_peak = 6000.f * (1.f / 60.f);
    }
    SECTION("axis acceleration limits replace the requested acceleration")
    {
        overrides     = "M201 X25\n";
        expected_peak = std::sqrt(125.f);
    }
    SECTION("axis speed limits replace the requested feedrate")
    {
        overrides     = "M203 X15\n";
        move          = "G1 X100 E5 F6000\n";
        expected_peak = 15.f;
    }
    processor.process_buffer(motion_preamble() + overrides + "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter) + move + "G4 P0\n");
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
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s == double(expected_peak) * double(moves.front()->mm3_per_mm));
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
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s < double(6000.f * (1.f / 60.f)) * moves[1]->mm3_per_mm);
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
        // Two consecutive 1 mm blocks enter/exit at 10 mm/s. At a=100,
        // their shared junction is sqrt(10^2 + 2*100*1) mm/s.
        const double same_block_peak = std::sqrt(300.f);
        REQUIRE(flow.max_peak_volumetric_rate_mm3_s == same_block_peak * double(move.mm3_per_mm));
        REQUIRE(peak_flow_report(result, material).at("peak_volumetric_rate_mm3_s").at("max") == flow.max_peak_volumetric_rate_mm3_s);
    }
    REQUIRE(moves[1]->mm3_per_mm > moves[0]->mm3_per_mm);
}

TEST_CASE("Gcode estimated peak flow keeps bad material independent of other material",
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
    // The half-circle is long enough to reach its parsed F600 speed.
    REQUIRE(move.feedrate == 600.f * (1.f / 60.f));
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s == double(move.feedrate) * (double(move.mm3_per_mm) * 1.5));
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
            REQUIRE(processor->get_result().normal_estimated_peak_flow.at(1).block_count == 1);
        } else
            require_no_analysis_metadata(processor->get_result());
        processor->reset();
        require_no_analysis_metadata(processor->get_result());
        process_cap_distance(*processor, second, {1000., 2000.});
    }
    const auto& observed = enabled.get_result();
    REQUIRE(observed.normal_estimated_peak_flow_available);
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
    REQUIRE(original.normal_estimated_peak_flow.at(0).max_peak_volumetric_rate_mm3_s == peak * double(moves[0]->mm3_per_mm));
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

TEST_CASE("Small perimeter selection remains distinct from final speed and survives result copy", "[SliceAnalysis][SmallPerimeterSelection]")
{
    Print print;
    InitializedResult result;
    const auto read = [&]() { return make_slice_analysis_report(print, result).at("gcode").at("small_perimeter_selection"); };
    REQUIRE(read().at("complete") == false);
    result.small_perimeter_selection.available = true;
    REQUIRE(read().at("complete") == true);
    REQUIRE(read().at("selected_loop_count") == 0);
    REQUIRE(read().at("requested_speed_mm_s").at("min").is_null());
    result.small_perimeter_selection = {true, 2, 3, 12., 15., 20.};
    REQUIRE(read().at("selected_path_length_mm") == 12.);
    REQUIRE(read().at("requested_speed_mm_s").at("min") == 15.);
    REQUIRE(read().at("scope") == "native_small_perimeter_selection_before_speed_overrides");
    InitializedResult copy;
    static_cast<GCodeProcessorResult&>(copy) = result;
    REQUIRE(copy.small_perimeter_selection.loop_count == 2);
    copy.reset();
    REQUIRE_FALSE(copy.small_perimeter_selection.available);
    REQUIRE(copy.small_perimeter_selection.path_count == 0);
    result.small_perimeter_selection.path_count = 1;
    REQUIRE(read().at("complete") == false);
    REQUIRE(read().at("selected_loop_count").is_null());
    result.small_perimeter_selection.path_count = 3;
    result.small_perimeter_selection.path_length_mm = std::numeric_limits<double>::infinity();
    REQUIRE(read().at("complete") == false);
}

TEST_CASE("Quality path evidence separates actual roles and materials without counting travel or custom extrusion", "[SliceAnalysis]")
{
    Print             print;
    InitializedResult result;
    auto              outer = extrusion(0, .42f, .2f, 30.f, .08f);
    outer.delta_extruder    = .5f;
    outer.travel_dist       = 10.f;
    outer.fan_speed         = 30.f;
    auto bridge             = outer;
    bridge.extrusion_role   = erBridgeInfill;
    bridge.extruder_id      = 1;
    bridge.feedrate         = 15.f;
    bridge.fan_speed        = 100.f;
    auto travel             = outer;
    travel.type             = EMoveType::Travel;
    auto custom             = outer;
    custom.extrusion_role   = erCustom;
    auto zero_e             = outer;
    zero_e.delta_extruder   = 0.f;
    result.moves            = {outer, bridge, travel, custom, zero_e};
    const auto before       = all_settings(print);
    const auto rows         = make_slice_analysis_report(print, result).at("gcode").at("quality_paths");
    REQUIRE(rows.at("scope") == "final_gcode_positive_extrusion_moves");
    REQUIRE(rows.at("rows").size() == 2);
    const auto& a = rows.at("rows").at(0);
    REQUIRE(a.at("role") == "outer_wall");
    REQUIRE(a.at("extruder_id") == 0);
    REQUIRE(a.at("complete") == true);
    REQUIRE(a.at("move_count") == 1);
    REQUIRE(a.at("path_length_mm") == 10.);
    REQUIRE(a.at("commanded_speed_mm_s").at("max") == 30.);
    REQUIRE(a.at("fan_speed_percent").at("min") == 30.);
    REQUIRE(a.at("nominal_width_mm").at("min").get<double>() == Approx(.42));
    REQUIRE(rows.at("rows").at(1).at("role") == "bridge");
    REQUIRE(rows.at("rows").at(1).at("fan_speed_percent").at("min") == 100.);
    REQUIRE(all_settings(print) == before);

    auto invalid     = bridge;
    invalid.feedrate = std::numeric_limits<float>::quiet_NaN();
    result.moves.push_back(invalid);
    const auto mixed = make_slice_analysis_report(print, result).at("gcode").at("quality_paths").at("rows");
    REQUIRE(mixed.at(0).at("complete") == true);
    REQUIRE(mixed.at(1).at("complete") == false);
    REQUIRE(mixed.at(1).at("path_length_mm").is_null());
    REQUIRE(mixed.at(1).at("commanded_speed_mm_s").at("max").is_null());
}

TEST_CASE("Quality path evidence reads final Gcode fan commands and native arc length", "[SliceAnalysis][GCodeProcessor]")
{
    GCodeProcessor processor(true);
    initialize_flow_processor(processor);
    processor.process_buffer(motion_preamble() + "G1 Z0.2 F6000\nM106 S255\n" + motion_role(erBridgeInfill) +
                             "G1 X10 E.5 F900\nG2 X12 Y0 I1 J0 E.2\nG4 P0\n");
    processor.finalize(false);
    Print      print;
    const auto rows = make_slice_analysis_report(print, processor.get_result()).at("gcode").at("quality_paths").at("rows");
    REQUIRE(rows.size() == 1);
    REQUIRE(rows.at(0).at("role") == "bridge");
    REQUIRE(rows.at(0).at("complete") == true);
    REQUIRE(rows.at(0).at("path_length_mm").get<double>() == Approx(10. + M_PI));
    REQUIRE(rows.at(0).at("commanded_speed_mm_s").at("min").get<double>() == Approx(15.));
    REQUIRE(rows.at(0).at("fan_speed_percent").at("min") == 100.);
}

TEST_CASE("Quality path slender beads use paired width and height, never independent extrema", "[SliceAnalysis][ResidualRisk]")
{
    Print             print;
    InitializedResult result;
    auto              a = extrusion(0, .065f, .08f, 25.f, .004f);
    a.extrusion_role    = erGapFill;
    a.delta_extruder    = .1f;
    a.travel_dist       = 2.f;
    a.fan_speed         = 100.f;
    a.position          = Vec3f(1.f, 2.f, 3.f);
    auto b              = a;
    b.width             = .4f;
    b.height            = .32f;
    b.travel_dist       = 10.f;
    auto c              = a;
    c.width = c.height = .08f;
    c.travel_dist      = 3.f;
    result.moves       = {a, b, c};
    const auto sample  = [&]() { return make_slice_analysis_report(print, result).at("gcode").at("quality_paths").at("rows").at(0); };
    const auto bead    = sample().at("width_height_observation");
    REQUIRE(bead.at("scope") == "same_move_nominal_width_height_non_bridge");
    REQUIRE(bead.at("complete") == true);
    REQUIRE(bead.at("applicable") == true);
    REQUIRE(bead.at("below_height_move_count") == 1);
    REQUIRE(bead.at("below_height_path_length_mm") == 2.);
    REQUIRE(bead.at("min_ratio").get<double>() == Approx(.065 / .08));
    REQUIRE(bead.at("width_at_min_mm").get<double>() == Approx(.065));
    REQUIRE(bead.at("height_at_min_mm").get<double>() == Approx(.08));
    REQUIRE(bead.at("print_z_at_min_mm") == 3.);
    result.moves.front().width = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(sample().at("width_height_observation").at("complete") == false);
    REQUIRE(sample().at("width_height_observation").at("below_height_path_length_mm").is_null());
    result.moves                        = {a};
    result.moves.front().extrusion_role = erBridgeInfill;
    REQUIRE(sample().at("width_height_observation").at("applicable") == false);
    REQUIRE(sample().at("width_height_observation").at("min_ratio").is_null());
}

TEST_CASE("Residual peaks localize missing contour stage and hole path sources without altering paths", "[SliceAnalysis][ResidualRisk]")
{
    Model model;
    Print print;
    add_box(model, 1);
    apply_model(print, model);
    PrintObject* object = print.get_object(0);
    Layer*       layer  = object->add_layer(0, .2, .2, .1);
    layer->lslices      = {square_with_hole(), rectangle(12., 0., 1., 1.)};
    LayerRegion* region = layer->add_region(&object->printing_region(0));
    region->slices.append({square_with_hole(), rectangle(12., 0., .5, 1.)}, stInternal);
    region->perimeters.append(planned_path(0., 1., 10., 1.));
    region->perimeters.append(planned_path(3.5, 4., 6.5, 4., .4f));
    region->fills.append(planned_path(3.5, 5., 6.5, 5., .2f, erGapFill));
    region->fills.append(planned_path(3.5, 6., 6.5, 6., .6f, erBridgeInfill));
    const auto original = region->fills.as_polylines();
    const auto read     = [&]() {
        return make_slice_analysis_report(print, InitializedResult{}).at("objects").at("groups").at(0).at("planned_layer_coverage");
    };
    const auto coverage = read();
    const auto missing  = coverage.at("unprinted_peak");
    REQUIRE(missing.at("complete") == true);
    REQUIRE(missing.at("layer_index") == 0);
    REQUIRE(missing.at("unprinted_area_mm2").get<double>() == Approx(1.));
    REQUIRE(missing.at("retained_region_area_mm2").get<double>() == Approx(.5));
    REQUIRE(missing.at("absent_region_area_mm2").get<double>() == Approx(.5));
    const auto hole = coverage.at("hole_intrusion_peak");
    REQUIRE(hole.at("complete") == true);
    REQUIRE(hole.at("wall_area_mm2").get<double>() > 1.);
    REQUIRE(hole.at("gap_fill_area_mm2").get<double>() == Approx(.6));
    REQUIRE(hole.at("bridge_area_mm2").get<double>() == Approx(1.8));
    REQUIRE(hole.at("other_fill_area_mm2") == 0.);
    REQUIRE(hole.at("mixed_area_mm2") == 0.);
    REQUIRE(hole.at("intrusion_area_mm2").get<double>() ==
            Approx(hole.at("wall_area_mm2").get<double>() + hole.at("gap_fill_area_mm2").get<double>() +
                   hole.at("bridge_area_mm2").get<double>()));
    REQUIRE(region->fills.as_polylines() == original);
    region->fills.append(planned_path(3.5, 4., 6.5, 4., .4f, erSolidInfill));
    REQUIRE(read().at("hole_intrusion_peak").at("mixed_area_mm2").get<double>() > 1.);
    Layer* invalid          = object->add_layer(1, .2, .4, .3);
    invalid->slicing_errors = true;
    REQUIRE(read().at("unprinted_peak").at("complete") == false);
    REQUIRE(read().at("hole_intrusion_peak").at("intrusion_area_mm2").is_null());
}

TEST_CASE("Gcode flow observations accept float-rounded phase endpoints without changing motion", "[SliceAnalysis][GCodeProcessor]")
{
    // Reduced from a real support path. (distance - ramp) + ramp rounds one
    // float ULP above distance; it is not an invalid or unfinished movement.
    const std::string gcode = motion_preamble() +
                              "M204 P300\nM205 X8 Y8\nG1 Z0.24 F1500\n"
                              "G92 X51.556 Y31.053\n" +
                              motion_role(erCustom) + "G1 X52.444 Y31.025 E.03975 F1500\n" + motion_role(erSupportMaterial) +
                              "G1 X52.433 Y36.031 E.22405\n" + motion_role(erCustom) + "G1 X51.869 Y38.697 E.12194\nG4 P0\n";
    GCodeProcessor observed(true);
    GCodeProcessor disabled;
    const double   cap = GENERATE(1., 12., 1.e-12);
    process_cap_distance(observed, gcode, {cap});
    process_cap_distance(disabled, gcode, {cap});
    const auto& result = observed.get_result();
    const auto  moves  = positive_model_moves(result);
    REQUIRE(moves.size() == 1);
    const auto& peak = result.normal_estimated_peak_flow.at(0);
    REQUIRE(peak.complete);
    REQUIRE(peak.block_count == 1);
    const double peak_speed = double(1500.f * (1.f / 60.f));
    REQUIRE(peak.max_peak_volumetric_rate_mm3_s == peak_speed * moves.front()->mm3_per_mm);
    const auto& distance = result.normal_estimated_flow_cap_distance.at(0);
    REQUIRE(distance.complete);
    REQUIRE(distance.participating_path_length_mm == double(moves.front()->travel_dist));
    const double threshold_speed = cap / moves.front()->mm3_per_mm;
    const double below           = std::max(0., (threshold_speed * threshold_speed - 8. * 8.) / (2. * 300.));
    const double expected_above  = threshold_speed >= peak_speed ? 0. : distance.participating_path_length_mm - below;
    REQUIRE(distance.above_cap_path_length_mm == Approx(expected_above));
    REQUIRE(distance.above_cap_path_length_mm <= distance.participating_path_length_mm);
    REQUIRE(peak_flow_report(result, 0).at("complete") == true);
    REQUIRE(cap_distance_report(result, 0, {cap}).at("complete") == true);
    require_no_analysis_metadata(disabled.get_result());
    require_same_legacy_gcode_observations(result, disabled.get_result());
}

TEST_CASE("Gcode flow-cap distance equality uses the finalized rounded peak as its strict upper bound", "[SliceAnalysis][GCodeProcessor]")
{
    const std::string gcode = motion_preamble() + "G1 Z0.2 F6000\n" + motion_role(erExternalPerimeter) + "G1 X1 E0.05 F6000\nG4 P0\n";
    GCodeProcessor    reference(true);
    process_cap_distance(reference, gcode, {1000.});
    const auto&  result    = reference.get_result();
    const double peak_flow = result.normal_estimated_peak_flow.at(0).max_peak_volumetric_rate_mm3_s;
    const auto   moves     = positive_model_moves(result);
    REQUIRE(moves.size() == 1);
    REQUIRE(peak_flow == double(std::sqrt(200.f)) * double(moves[0]->mm3_per_mm));
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
    SECTION("a one millimeter initial block only accelerates from zero to two")
    {
        moves = motion_role(erExternalPerimeter) + "G1 X1 E0.05 F240\n" + motion_role(erCustom) + "G1 X11 E0.5 F240\n";
    }
    SECTION("a one millimeter final block only decelerates from two to zero")
    {
        moves = motion_role(erCustom) + "G1 X10 E0.5 F240\n" + motion_role(erExternalPerimeter) + "G1 X11 E0.05 F240\n";
    }
    const std::string gcode = cap_phase_program("M204 P2\nM203 X4\n", moves);
    GCodeProcessor    reference(true);
    process_cap_distance(reference, gcode, {1000.});
    const auto positive = positive_model_moves(reference.get_result());
    REQUIRE(positive.size() == 1);
    const auto& flow = reference.get_result().normal_estimated_peak_flow.at(0);
    REQUIRE(flow.complete);
    REQUIRE(flow.block_count == 1);
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s == 2. * double(positive.front()->mm3_per_mm));
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
    const auto positive = positive_model_moves(processor.get_result());
    REQUIRE(positive.size() == 1);
    const auto& flow = processor.get_result().normal_estimated_peak_flow.at(0);
    REQUIRE(flow.complete);
    REQUIRE(flow.block_count == 1);
    REQUIRE(flow.max_peak_volumetric_rate_mm3_s == (zero_flow ? 0. : 4. * double(positive.front()->mm3_per_mm)));
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

namespace {

LayerRegion* bed_region_layer(PrintObject& object, double print_z = 0.2)
{
    Layer* layer   = object.add_layer(0, 0.2, print_z, 0.1);
    layer->lslices = {rectangle(0., 0., 10., 10.)};
    return layer->add_region(&object.printing_region(0));
}

} // namespace

TEST_CASE("Bed footprint area excludes holes and includes nested material islands", "[SliceAnalysis]")
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
    REQUIRE(bed.at("complete") == true);
    REQUIRE(bed.at("model_area_mm2").get<double>() == Approx(inner_island ? 88. : 84.));
    REQUIRE(bed.at("support_and_raft_area_mm2") == 0.);
    REQUIRE(bed.at("brim_area_mm2") == 0.);
    REQUIRE(bed.at("combined_area_mm2") == bed.at("model_area_mm2"));
}

TEST_CASE("Bed footprint area uses actual path heights and current shared raft rather than backups", "[SliceAnalysis]")
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
    REQUIRE(bed.at("complete") == true);
    REQUIRE(bed.at("model_area_mm2") == 0.);
    REQUIRE(bed.at("brim_area_mm2") == 0.);
    REQUIRE(bed.at("support_and_raft_area_mm2").get<double>() == Approx(16.));
    REQUIRE(bed.at("combined_area_mm2") == bed.at("support_and_raft_area_mm2"));
    REQUIRE(support->raft_merge_backup->fills.as_polylines() == backup);
}
