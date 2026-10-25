#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>
#include <boost/log/core.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/make_shared.hpp>

#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/GCode/ConflictChecker.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Fill/FillRectilinear.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Support/RaftPlan.hpp"
#include "libslic3r/Support/RaftMerge.hpp"
#include "libslic3r/Support/RaftGeometry.hpp"
#include "libslic3r/Support/SupportCommon.hpp"
#include "libslic3r/Support/SupportParameters.hpp"
#include "libslic3r/Utils.hpp"

#include "test_data.hpp" // get access to init_print, etc

using namespace Slic3r::Test;
using namespace Slic3r;

TEST_CASE("Floating warnings require actual extrusion but retain small printed regions", "[SupportMaterial][FloatingWarning]")
{
    const int mode = GENERATE(0, 1, 2, 3);
    INFO("case=" << mode);
    Print              print;
    Model              model;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_support", false);
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {200., 0.}, {200., 200.}, {0., 200.}}));
    config.set_key_value("bed_exclude_area", new ConfigOptionPoints());
    ModelObject* cube = model.add_object();
    cube->name = "floating warning test";
    cube->add_volume(make_cube(20., 20., 2.));
    cube->add_instance()->set_offset(Vec3d(80., 80., 0.));
    cube->ensure_on_bed();
    print.auto_assign_extruders(cube);
    print.apply(model, config);
    print.set_status_silent();
    print.process();
    PrintObject* object = print.get_object(0);
    REQUIRE(object->is_step_done(posPerimeters));
    REQUIRE(object->is_step_done(posInfill));
    Layer* layer = object->get_layer(1);
    // A disconnected slice contour, far from the cube, has no printable path
    // unless the case below explicitly supplies one. No area-based exemption.
    layer->lslices.emplace_back(
        Polygon{Points{Point::new_scale(50., 50.), Point::new_scale(51., 50.), Point::new_scale(51., 51.), Point::new_scale(50., 51.)}});
    if (mode >= 1 && mode <= 3) {
        auto* path = new ExtrusionPath(mode == 2 ? erGapFill : erExternalPerimeter, 0.08, 0.4, 0.2);
        // Case 3 has its centerline outside the section; the line's width
        // overlaps it and must still be treated as real deposited material.
        const double x        = mode == 3 ? 49.9 : 50.5;
        path->polyline.points = {Point::new_scale(x, 50.25), Point::new_scale(x, 50.75)};
        auto* island = new ExtrusionEntityCollection();
        island->entities.push_back(path);
        if (mode == 2)
            layer->regions().front()->fills.entities.push_back(island);
        else
            layer->regions().front()->perimeters.entities.push_back(island);
    }
    bool warned = false;
    print.set_status_callback([&](const PrintBase::SlicingStatus& status) {
        if (status.warning_step != -1 && status.text.find("floating regions") != std::string::npos)
            warned = true;
    });
    config.set("support_threshold_angle", 31);
    print.apply(model, config); // Rerun support checks without regenerating the test paths.
    REQUIRE(object->is_step_done(posPerimeters));
    REQUIRE(object->is_step_done(posInfill));
    REQUIRE_FALSE(object->is_step_done(posSupportMaterial));
    print.process();
    REQUIRE(warned == (mode != 0));
}

namespace {
DynamicPrintConfig raft_bed_test_config()
{
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
    return config;
}

ModelObject* add_raft_bed_test_cube(Model& model, double size, const Vec3d& offset)
{
    ModelObject* object = model.add_object();
    object->name        = "raft bed boundary cube";
    object->add_volume(make_cube(size, size, 0.8));
    object->add_instance()->set_offset(offset);
    object->ensure_on_bed();
    return object;
}

void process_raft_bed_test(Print& print, Model& model, const DynamicPrintConfig& config)
{
    for (ModelObject* object : model.objects)
        print.auto_assign_extruders(object);
    print.apply(model, config);
    const StringObjectException validation = print.validate();
    INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
    REQUIRE(validation.string.empty());
    print.set_status_silent();
    print.process();
}

// Check the deposited footprint, not just the path vertices or raft polygons.
void require_raft_inside_bed(const Print& print)
{
    const Polygons bed         = diff(Polygons{Polygon(get_bed_shape(print.config()))}, get_bed_excluded_area(print.config()));
    const Vec3d    origin      = print.get_plate_origin();
    const Point    plate_shift = Point::new_scale(origin.x(), origin.y());
    for (const PrintObject* object : print.objects()) {
        size_t raft_layers = 0;
        for (const SupportLayer* layer : object->support_layers()) {
            if (layer->print_z > object->slicing_parameters().raft_contact_top_z + EPSILON)
                continue;
            ++raft_layers;
            INFO("raft layer z=" << layer->print_z);
            REQUIRE(layer->has_extrusions());
            const Polygons footprint = layer->support_fills.polygons_covered_by_width();
            REQUIRE_FALSE(footprint.empty());
            for (const PrintInstance& instance : object->instances()) {
                Polygons placed = footprint;
                for (Polygon& polygon : placed)
                    polygon.translate(instance.shift - plate_shift);
                // Allow only polygon integer rounding, not a hidden bed margin.
                REQUIRE(diff(placed, offset(bed, scaled<float>(0.0001))).empty());
            }
        }
        REQUIRE(raft_layers == object->slicing_parameters().raft_layers());
    }
}

Polygons require_brim_inside_bed(Print& print)
{
    const Polygons bed    = diff(Polygons{Polygon(get_bed_shape(print.config()))}, get_bed_excluded_area(print.config()));
    const Vec3d    origin = print.get_plate_origin();
    const Point    shift  = Point::new_scale(-origin.x(), -origin.y());
    Polygons       footprint;
    double         volume = 0.;
    for (const auto& entry : print.get_brimMap()) {
        append(footprint, entry.second.polygons_covered_by_width());
        volume += entry.second.total_volume();
    }
    REQUIRE(volume > 0.);
    REQUIRE_FALSE(footprint.empty());
    for (Polygon& polygon : footprint)
        polygon.translate(shift);
    REQUIRE(diff(footprint, offset(bed, scaled<float>(0.0001))).empty());
    return footprint;
}

double raft_volume(const PrintObject& object)
{
    double volume = 0.;
    for (const SupportLayer* layer : object.support_layers())
        if (layer->print_z <= object.slicing_parameters().raft_contact_top_z + EPSILON)
            volume += layer->support_fills.total_volume();
    return volume;
}
} // namespace

TEST_CASE("Raft extrusion stays on a 100 mm bed around a 98 mm object", "[SupportMaterial][Raft][BedBoundary]")
{
    const auto mode  = GENERATE(RaftMode::Legacy, RaftMode::CuraV1);
    const auto style = GENERATE(smsGrid, smsSnug, smsTreeOrganic, smsTreeSlim, smsTreeStrong, smsTreeHybrid);
    const int  walls = GENERATE(0, 2);
    INFO("mode=" << int(mode) << ", style=" << int(style) << ", walls=" << walls);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(mode));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(style));
    config.set_key_value("support_type",
                         new ConfigOptionEnum<SupportType>(style == smsGrid || style == smsSnug ? stNormalAuto : stTreeAuto));
    config.set("raft_base_wall_count", walls);
    config.set("raft_interface_wall_count", walls);
    config.set("raft_surface_wall_count", walls);

    Model model;
    add_raft_bed_test_cube(model, 98., Vec3d(1., 1., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    require_raft_inside_bed(print);
    // Clipping the raft must neither resize nor relocate the model.
    const BoundingBoxf3 model_box = model.objects.front()->instance_bounding_box(0);
    REQUIRE(model_box.min.x() == Approx(1.));
    REQUIRE(model_box.max.x() == Approx(99.));
    REQUIRE(model_box.size().y() == Approx(98.));
}

TEST_CASE("Raft clipping respects curved beds and excluded regions on another plate", "[SupportMaterial][Raft][BedBoundary]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("enable_arc_fitting", GENERATE(false, true));
    const Vec3d origin(150., -120., 0.);
    double      size = 20.;
    Vec3d       offset(45., 40., 0.);
    SECTION("an interior excluded region is kept as a hole")
    {
        config.set_key_value("bed_exclude_area", new ConfigOptionPoints({{66., 35.}, {74., 35.}, {74., 65.}, {66., 65.}}));
    }
    SECTION("a circular bed clips the corners of the expanded raft")
    {
        Pointfs circle;
        for (size_t i = 0; i < 64; ++i) {
            const double angle = 2. * PI * double(i) / 64.;
            circle.emplace_back(50. + 50. * std::cos(angle), 50. + 50. * std::sin(angle));
        }
        config.set_key_value("printable_area", new ConfigOptionPoints(circle));
        config.set("raft_base_margin", 20.);
        config.set("raft_interface_margin", 19.);
        config.set("raft_surface_margin", 18.);
        size   = 40.;
        offset = Vec3d(30., 30., 0.);
    }
    Model model;
    add_raft_bed_test_cube(model, size, origin + offset);
    Print print;
    print.set_plate_origin(origin);
    process_raft_bed_test(print, model, config);
    require_raft_inside_bed(print);
}

TEST_CASE("Moving an object and changing the bed regenerates its raft", "[SupportMaterial][Raft][BedBoundary]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    Model              model;
    ModelObject*       model_object = add_raft_bed_test_cube(model, 20., Vec3d(40., 40., 0.));
    Print              print;
    process_raft_bed_test(print, model, config);
    const double center_volume = raft_volume(*print.objects().front());
    REQUIRE(center_volume > 0.);
    for (const Vec3d& position :
         {Vec3d(1., 40., 0.), Vec3d(79., 40., 0.), Vec3d(40., 1., 0.), Vec3d(40., 79., 0.), Vec3d(1., 1., 0.), Vec3d(79., 79., 0.)}) {
        model_object->instances.front()->set_offset(position);
        model_object->invalidate_bounding_box();
        print.apply(model, config);
        REQUIRE_FALSE(print.is_step_done(posSupportMaterial));
        print.process();
        require_raft_inside_bed(print);
        REQUIRE(raft_volume(*print.objects().front()) < center_volume);
    }
    model_object->instances.front()->set_offset(Vec3d(40., 40., 0.));
    model_object->invalidate_bounding_box();
    process_raft_bed_test(print, model, config);
    REQUIRE(raft_volume(*print.objects().front()) == Approx(center_volume));

    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {61., 0.}, {61., 100.}, {0., 100.}}));
    print.apply(model, config);
    REQUIRE_FALSE(print.is_step_done(posSupportMaterial));
    print.process();
    require_raft_inside_bed(print);
    REQUIRE(raft_volume(*print.objects().front()) < center_volume);

    const Vec3d origin(150., 120., 0.);
    print.set_plate_origin(origin);
    REQUIRE_FALSE(print.is_step_done(posSupportMaterial));
    model_object->instances.front()->set_offset(origin + Vec3d(40., 40., 0.));
    model_object->invalidate_bounding_box();
    process_raft_bed_test(print, model, config);
    require_raft_inside_bed(print);

    config.set_key_value("bed_exclude_area", new ConfigOptionPoints({{30., 30.}, {39., 30.}, {39., 70.}, {30., 70.}}));
    print.apply(model, config);
    REQUIRE_FALSE(print.is_step_done(posSupportMaterial));
    // Cached model layers must not bypass raft regeneration.
    print.process(nullptr, true);
    require_raft_inside_bed(print);
}

TEST_CASE("Raft clipping accounts for every instance and separately placed mesh copies", "[SupportMaterial][Raft][BedBoundary]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    Model              model;
    ModelObject*       object = add_raft_bed_test_cube(model, 20., Vec3d(1., 40., 0.));
    SECTION("instances at opposite bed edges share only printable toolpaths")
    {
        object->add_instance()->set_offset(Vec3d(79., 40., 0.));
        Print print;
        process_raft_bed_test(print, model, config);
        REQUIRE(print.objects().size() == 1);
        REQUIRE(print.objects().front()->instances().size() == 2);
        require_raft_inside_bed(print);
    }
    SECTION("a centered copy must retain its full raft")
    {
        ModelObject* copy = model.add_object(*object);
        REQUIRE(copy->volumes.front()->mesh_ptr() == object->volumes.front()->mesh_ptr());
        copy->instances.front()->set_offset(Vec3d(40., 40., 0.));
        copy->invalidate_bounding_box();
        Print print;
        process_raft_bed_test(print, model, config);
        REQUIRE(print.objects().size() == 2);
        require_raft_inside_bed(print);
        for (const PrintObject* printed : print.objects())
            REQUIRE(printed->get_shared_object() == nullptr);
        REQUIRE(raft_volume(*print.objects().front()) < raft_volume(*print.objects().back()));
    }
}

TEST_CASE("A raft entirely outside the bed reports an error", "[SupportMaterial][Raft][BedBoundary]")
{
    const auto         style  = GENERATE(smsGrid, smsTreeSlim);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(style));
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(style == smsGrid ? stNormalAuto : stTreeAuto));
    Model model;
    add_raft_bed_test_cube(model, 20., Vec3d(40., 40., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    // Exercise clipping's own failure instead of passing a model-placement error
    // from Print::validate() off as successful boundary protection.
    print.set_plate_origin(Vec3d(1000., 0., 0.));
    // apply() refreshes invalid slicing parameters through the public workflow.
    print.apply(model, config);
    REQUIRE_THROWS_WITH(print.process(), Catch::Contains("The raft has no printable area on the bed."));
}

TEST_CASE("Tree support without a raft keeps its existing generation path", "[SupportMaterial][Raft][BedBoundary]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    config.set("raft_layers", 0);
    config.set("enable_support", true);
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTreeAuto));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsTreeSlim));
    config.set("support_threshold_angle", 60);
    Model        model;
    ModelObject* object = model.add_object();
    TriangleMesh tilted = make_cube(20., 20., 20.);
    tilted.rotate_x(float(Geometry::deg2rad(30.)));
    object->add_volume(std::move(tilted));
    object->add_instance()->set_offset(Vec3d(40., 40., 0.));
    object->ensure_on_bed();
    Print print;
    process_raft_bed_test(print, model, config);
    PrintObject& printed = *print.get_object(size_t(0));
    REQUIRE_FALSE(printed.has_raft());
    REQUIRE_FALSE(printed.support_layers().empty());
    double volume = 0.;
    for (const SupportLayer* layer : printed.support_layers())
        volume += layer->support_fills.total_volume();
    REQUIRE(volume > 0.);
    clip_raft_toolpaths_to_bed(printed);
    double unchanged_volume = 0.;
    for (const SupportLayer* layer : printed.support_layers())
        unchanged_volume += layer->support_fills.total_volume();
    REQUIRE(unchanged_volume == volume);
}

TEST_CASE("SupportMaterial: Three raft layers created", "[SupportMaterial]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({{"support_material", 1}, {"raft_layers", 3}});

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->name = "legacy raft integration cube";
    model_object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
    ModelInstance *instance = model_object->add_instance();
    instance->set_offset(Vec3d(100., 100., 0.));
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    print.process();
    REQUIRE(print.objects().front()->support_layers().size() == 3);
}

TEST_CASE("SlicingParameters characterizes the legacy three-layer raft layout", "[SupportMaterial][Raft][Legacy]")
{
    PrintConfig print_config;
    print_config.nozzle_diameter.values             = {0.4};
    print_config.min_layer_height.values             = {0.08};
    print_config.max_layer_height.values             = {0.32};
    print_config.initial_layer_print_height.value     = 0.4;

    PrintObjectConfig object_config;
    object_config.layer_height.value                 = 0.2;
    object_config.raft_layers.value                   = 3;
    object_config.support_filament.value              = 1;
    object_config.support_interface_filament.value    = 1;
    object_config.support_top_z_distance.value        = 0.2;
    object_config.support_bottom_z_distance.value     = 0.2;

    REQUIRE(object_config.raft_mode.value == RaftMode::Legacy);

    auto slicing_parameters = [&](double contact_distance, bool independent_support_layer_height) {
        object_config.raft_contact_distance.value               = contact_distance;
        print_config.independent_support_layer_height.value     = independent_support_layer_height;
        return SlicingParameters::create_from_config(print_config, object_config, 10.1, {1}, Vec3d::Ones());
    };

    SECTION("three layers split into one base and two interface layers")
    {
        const SlicingParameters params = slicing_parameters(0.1, true);

        REQUIRE(params.valid);
        REQUIRE(params.raft_layers() == 3);
        REQUIRE(params.base_raft_layers == 1);
        REQUIRE(params.interface_raft_layers == 2);
        REQUIRE(params.first_print_layer_height == Approx(0.4));
        REQUIRE(params.base_raft_layer_height == Approx(0.3));
        REQUIRE(params.interface_raft_layer_height == Approx(0.3));
        REQUIRE(params.contact_raft_layer_height == Approx(0.3));
        REQUIRE(params.raft_base_top_z == Approx(0.4));
        REQUIRE(params.raft_interface_top_z == Approx(0.7));
        REQUIRE(params.raft_contact_top_z == Approx(1.0));

        // Legacy behavior: the model over a raft loses the configured 0.4 mm
        // initial-layer height and uses the regular object layer height.
        REQUIRE_FALSE(params.first_object_layer_bridging);
        REQUIRE(params.first_object_layer_height == Approx(0.2));
    }

    SECTION("independent support layers preserve the configured raft contact distance")
    {
        const SlicingParameters gap_01 = slicing_parameters(0.1, true);
        const SlicingParameters gap_03 = slicing_parameters(0.3, true);

        REQUIRE(gap_01.gap_raft_object == Approx(0.1));
        REQUIRE(gap_01.object_print_z_min == Approx(1.1));
        REQUIRE(gap_03.gap_raft_object == Approx(0.3));
        REQUIRE(gap_03.object_print_z_min == Approx(1.3));
    }

    SECTION("coupled support layers quantize the raft gap to the object layer height")
    {
        const SlicingParameters gap_01 = slicing_parameters(0.1, false);
        const SlicingParameters gap_03 = slicing_parameters(0.3, false);

        REQUIRE(gap_01.gap_raft_object == Approx(0.2));
        REQUIRE(gap_01.object_print_z_min == Approx(1.2));
        REQUIRE(gap_03.gap_raft_object == Approx(0.4));
        REQUIRE(gap_03.object_print_z_min == Approx(1.4));
    }
}

TEST_CASE("Cura V1 fills every raft layer beneath an off-centre surviving thin plate", "[SupportMaterial][Raft][CuraV1][OffsetFill]")
{
    const auto support_type = GENERATE(stNormalAuto, stTreeAuto);
    const int  walls        = GENERATE(0, 1);
    const bool mirrored     = GENERATE(false, true);
    const bool segmented    = GENERATE(false, true);
    CAPTURE(support_type, walls, mirrored, segmented);

    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(support_type));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsDefault));
    config.set("initial_layer_print_height", 0.24);
    config.set("raft_layers", 0);
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("raft_base_layer_height", 0.32);
    config.set("raft_interface_layer_height", 0.3);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_surface_line_width", 0.8);
    config.set("raft_surface_line_spacing", 0.);
    config.set("raft_interface_wall_count", walls);
    config.set("raft_surface_wall_count", walls);
    config.set("raft_airgap", 0.22);
    config.set("raft_segmented", segmented);
    config.set("raft_chunk_size", 15.);

    // One STL/object with two distant sheets: the 0.10 mm sheet disappears at
    // the 0.12 mm sampling plane, leaving the raft far from the object origin.
    TriangleMesh mesh  = make_cube(30., 30., 0.10);
    TriangleMesh thick = make_cube(30., 30., 0.25);
    mesh.translate(0.5f, 1.5f, 0.f);
    thick.translate(69.5f, 69.5f, 0.f);
    mesh.merge(thick);
    mesh.translate(-50.f, -50.f, 0.f);
    if (mirrored)
        mesh.rotate_z(float(M_PI));
    Model        model;
    ModelObject* object = model.add_object();
    object->name        = "offset thin plates";
    object->add_volume(std::move(mesh));
    object->add_instance()->set_offset(Vec3d(50., 50., 0.));
    object->ensure_on_bed();

    Print print;
    process_raft_bed_test(print, model, config);
    const PrintObject& printed = *print.objects().front();
    REQUIRE(printed.layers().size() == 1);
    REQUIRE(printed.layers().front()->lslices.size() == 1);
    REQUIRE(printed.layers().front()->print_z == Approx(1.78));
    REQUIRE(printed.support_layers().size() == 5);
    const std::array<double, 5> expected_z{0.32, 0.62, 0.92, 1.12, 1.32};
    for (size_t i = 0; i < expected_z.size(); ++i) {
        const SupportLayer& layer = *printed.support_layers()[i];
        CAPTURE(i, layer.print_z);
        REQUIRE(layer.print_z == Approx(expected_z[i]));
        REQUIRE(layer.has_extrusions());
        const Polygons    coverage = layer.support_fills.polygons_covered_by_width();
        const BoundingBox paths    = get_extents(coverage);
        const BoundingBox area     = get_extents(layer.raft_outline);
        // A nonempty perimeter or a few clipped hatch lines are not enough:
        // the deposited paths must reach both sides of the raft footprint.
        REQUIRE(paths.defined);
        REQUIRE(paths.min.x() <= area.min.x() + scale_(2.));
        REQUIRE(paths.min.y() <= area.min.y() + scale_(2.));
        REQUIRE(paths.max.x() >= area.max.x() - scale_(2.));
        REQUIRE(paths.max.y() >= area.max.y() - scale_(2.));
        if (i > 0) {
            // Walls alone can span the outline while the hatch is still gone.
            const double covered_area = Slic3r::area(intersection_ex(coverage, to_polygons(layer.raft_outline)));
            REQUIRE(covered_area >= (i < 3 ? 0.5 : 0.7) * Slic3r::area(layer.raft_outline));
        }
    }
    require_raft_inside_bed(print);
}

TEST_CASE("Cura V1 raft plan preserves the three physical phases", "[SupportMaterial][Raft][CuraV1]")
{
    ConfigOptionEnum<RaftMode> serialized_mode(RaftMode::CuraV1);
    REQUIRE(serialized_mode.serialize() == "cura_v1");
    REQUIRE(serialized_mode.deserialize("legacy"));
    REQUIRE(serialized_mode.value == RaftMode::Legacy);

    RaftPlanConfig config;
    config.first_base_layer_height = 0.3;
    config.airgap                  = 0.27;
    config.overlap                 = 0.1;
    config.surface_angle           = 110.;
    config.base_config             = {1, 0.3, 0.6, 1.5, 1.05, 10., 0., 4, 3.};
    config.interface_config        = {2, 0.3, 0.6, 1.5, 0.95, 25., 0., 0, 1.};
    config.surface_config          = {2, 0.2, 0.42, 0.42, 1., 60., 0., 0, 1.};

    const RaftPhasePlan plan = build_cura_raft_phase_plan(config);

    REQUIRE(plan.validate());
    REQUIRE(plan.mode == RaftPlanMode::CuraV1);
    REQUIRE(plan.layers.size() == 5);
    REQUIRE(plan.phase_layer_count(RaftPhase::Base) == 1);
    REQUIRE(plan.phase_layer_count(RaftPhase::Interface) == 2);
    REQUIRE(plan.phase_layer_count(RaftPhase::Surface) == 2);
    REQUIRE(plan.layers[0].print_z == Approx(0.3));
    REQUIRE(plan.layers[1].print_z == Approx(0.6));
    REQUIRE(plan.layers[2].print_z == Approx(0.9));
    REQUIRE(plan.layers[3].print_z == Approx(1.1));
    REQUIRE(plan.layers[4].print_z == Approx(1.3));
    REQUIRE(plan.layers[0].angle == Approx(110.));
    REQUIRE(plan.layers[1].angle == Approx(20.));
    REQUIRE(plan.layers[2].angle == Approx(110.));
    REQUIRE(plan.layers[3].angle == Approx(20.));
    REQUIRE(plan.layers[4].angle == Approx(110.));
    REQUIRE(plan.layers[0].line_width == Approx(0.6));
    REQUIRE(plan.layers[0].line_spacing == Approx(1.5));
    REQUIRE(plan.layers[0].flow_ratio == Approx(1.05));
    REQUIRE(plan.layers[0].speed == Approx(10.));
    REQUIRE(plan.layers[0].fan_speed == Approx(0.));
    REQUIRE(plan.layers[0].wall_count == 4);
    REQUIRE(plan.layers[0].margin == Approx(3.));
    REQUIRE(plan.layers[1].flow_ratio == Approx(0.95));
    REQUIRE(plan.layers[1].speed == Approx(25.));
    REQUIRE(plan.layers[1].wall_count == 0);
    REQUIRE(plan.layers[1].margin == Approx(1.));
    REQUIRE(plan.layers[3].line_width == Approx(0.42));
    REQUIRE(plan.layers[3].line_spacing == Approx(0.42));
    REQUIRE(plan.layers[3].flow_ratio == Approx(1.));
    REQUIRE(plan.layers[3].speed == Approx(60.));
    REQUIRE(plan.layers[3].wall_count == 0);
    REQUIRE(plan.layers[3].margin == Approx(1.));

    // The model-contacting Surface stays anchored when the total physical
    // layer count changes from odd to even; only the preceding schedule moves.
    RaftPlanConfig even_config = config;
    even_config.interface_config.layer_count = 1;
    const RaftPhasePlan even_plan = build_cura_raft_phase_plan(even_config);
    REQUIRE(even_plan.validate());
    REQUIRE(even_plan.layers.size() == 4);
    REQUIRE(even_plan.layers[0].angle == Approx(20.));
    REQUIRE(even_plan.layers[1].angle == Approx(110.));
    REQUIRE(even_plan.layers[2].angle == Approx(20.));
    REQUIRE(even_plan.layers[3].angle == Approx(110.));
    REQUIRE(even_plan.layers.back().phase == RaftPhase::Surface);
}

TEST_CASE("SlicingParameters resolves Cura V1 airgap and model first layer independently", "[SupportMaterial][Raft][CuraV1]")
{
    PrintConfig print_config;
    print_config.nozzle_diameter.values                  = {0.4};
    print_config.min_layer_height.values                 = {0.08};
    print_config.max_layer_height.values                 = {0.32};
    print_config.initial_layer_print_height.value        = 0.42;
    print_config.independent_support_layer_height.value  = false;

    PrintObjectConfig object_config;
    object_config.layer_height.value                  = 0.2;
    object_config.raft_mode.value                     = RaftMode::CuraV1;
    object_config.raft_airgap.value                   = 0.27;
    object_config.raft_layer_0_z_overlap.value        = 0.1;
    object_config.raft_base_layers.value              = 1;
    object_config.raft_interface_layers.value         = 2;
    object_config.raft_surface_layers.value           = 2;
    object_config.raft_base_layer_height.value        = 0.3;
    object_config.raft_interface_layer_height.value   = 0.3;
    object_config.raft_surface_layer_height.value     = 0.2;
    object_config.raft_base_line_width.value          = 0.6;
    object_config.raft_interface_line_width.value     = 0.6;
    object_config.raft_surface_line_width.value       = 0.42;
    object_config.raft_base_line_spacing.value        = 1.5;
    object_config.raft_interface_line_spacing.value   = 1.5;
    object_config.raft_surface_line_spacing.value     = 0.42;
    object_config.support_filament.value              = 1;
    object_config.support_interface_filament.value    = 1;
    object_config.support_top_z_distance.value        = 0.2;
    object_config.support_bottom_z_distance.value     = 0.2;

    const SlicingParameters params =
        SlicingParameters::create_from_config(print_config, object_config, 10.1, {1}, Vec3d::Ones());

    REQUIRE(params.valid);
    REQUIRE(params.cura_raft_mode);
    REQUIRE(params.raft_layers() == 5);
    REQUIRE(params.base_raft_layers == 1);
    REQUIRE(params.interface_phase_raft_layers() == 2);
    REQUIRE(params.surface_raft_layers == 2);
    REQUIRE(params.first_print_layer_height == Approx(0.3));
    REQUIRE(params.first_object_layer_height == Approx(0.42));
    REQUIRE(params.gap_raft_object == Approx(0.27));
    REQUIRE(params.raft_layer_0_z_overlap == Approx(0.1));
    REQUIRE(params.raft_base_top_z == Approx(0.3));
    REQUIRE(params.raft_phase_interface_top_z == Approx(0.9));
    REQUIRE(params.raft_interface_top_z == Approx(1.1));
    REQUIRE(params.raft_contact_top_z == Approx(1.3));
    REQUIRE(params.object_print_z_min == Approx(1.57));
    REQUIRE(params.object_print_z_max == Approx(11.67));
    REQUIRE(params.first_object_layer_height_fixed());
}

TEST_CASE("Cura V1 raft validates phase geometry against its active nozzle", "[SupportMaterial][Raft][CuraV1][Validation]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("raft_base_layer_height", 0.3);
    config.set("raft_interface_layer_height", 0.3);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_base_line_width", 0.6);
    config.set("raft_interface_line_width", 0.6);
    config.set("raft_surface_line_width", 0.42);

    const char *expected_key = nullptr;
    SECTION("interface layer height cannot exceed interface nozzle")
    {
        config.set("raft_interface_layer_height", 0.5);
        expected_key = "raft_interface_layer_height";
    }
    SECTION("base layer height error points to the raft Base setting")
    {
        config.set("raft_base_layer_height", 0.5);
        expected_key = "raft_base_layer_height";
    }
    SECTION("surface line width must be larger than its layer height")
    {
        config.set("raft_surface_line_width", 0.19);
        expected_key = "raft_surface_line_width";
    }
    SECTION("base line width cannot exceed the nozzle width limit")
    {
        config.set("raft_base_line_width", 3.0);
        expected_key = "raft_base_line_width";
    }
    SECTION("model initial layer height cannot exceed its nozzle")
    {
        config.set("initial_layer_print_height", 0.8);
        expected_key = "initial_layer_print_height";
    }
    SECTION("model initial line width must exceed the model initial layer height")
    {
        config.set("initial_layer_print_height", 0.3);
        config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.2, false));
        expected_key = "initial_layer_line_width";
    }
    SECTION("normal raft rejects a Base margin that cannot be represented exactly")
    {
        config.set("raft_base_margin", 1.4);
        config.set("raft_surface_margin", 1.0);
        expected_key = "raft_base_margin";
    }
    SECTION("base line spacing below resolver epsilon is not silently treated as Auto")
    {
        config.set("raft_base_line_spacing", EPSILON * 0.5);
        expected_key = "raft_base_line_spacing";
    }
    SECTION("interface line spacing below path resolution is rejected")
    {
        config.set("raft_interface_line_spacing", RESOLUTION * 0.5);
        expected_key = "raft_interface_line_spacing";
    }
    SECTION("surface line spacing below path resolution is rejected")
    {
        config.set("raft_surface_line_spacing", RESOLUTION * 0.5);
        expected_key = "raft_surface_line_spacing";
    }

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
    model_object->add_instance();
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);
    const StringObjectException error = print.validate();
    REQUIRE_FALSE(error.string.empty());
    REQUIRE(error.opt_key == expected_key);
}

TEST_CASE("Cura V1 rejects invalid phase counts before defensive resolution", "[SupportMaterial][Raft][CuraV1][Validation]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));

    const char *expected_key = nullptr;
    SECTION("all zero counts still mean an enabled but invalid Cura V1 raft")
    {
        config.set("raft_base_layers", 0);
        config.set("raft_interface_layers", 0);
        config.set("raft_surface_layers", 0);
        expected_key = "raft_base_layers";
    }
    SECTION("Base is mandatory")
    {
        config.set("raft_base_layers", 0);
        expected_key = "raft_base_layers";
    }
    SECTION("Middle may be zero but not negative")
    {
        config.set("raft_interface_layers", -1);
        expected_key = "raft_interface_layers";
    }
    SECTION("Top is mandatory in the current Orca Cura V1 implementation")
    {
        config.set("raft_surface_layers", 0);
        expected_key = "raft_surface_layers";
    }

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
    model_object->add_instance();
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);

    REQUIRE(print.objects().front()->has_raft());
    const StringObjectException error = print.validate();
    INFO("validation error: " << error.string << "; key: " << error.opt_key);
    REQUIRE_FALSE(error.string.empty());
    REQUIRE(error.opt_key == expected_key);
}

TEST_CASE("Legacy zero layers remains unrafted after Cura V1 mode validation", "[SupportMaterial][Raft][Legacy][Validation]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    config.set("raft_layers", 0);

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
    model_object->add_instance();
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);

    REQUIRE_FALSE(print.objects().front()->has_raft());
}

TEST_CASE("Cura V1 raft accepts safe explicit spacing and representable margins", "[SupportMaterial][Raft][CuraV1][Validation]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("raft_base_layer_height", 0.3);
    config.set("raft_interface_layer_height", 0.3);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_base_line_width", 0.6);
    config.set("raft_interface_line_width", 0.6);
    config.set("raft_surface_line_width", 0.42);
    config.set("before_layer_change_gcode", std::string("G92 E0"));

    SECTION("zero remains Auto")
    {
        config.set("raft_base_line_spacing", 0.);
        config.set("raft_interface_line_spacing", 0.);
        config.set("raft_surface_line_spacing", 0.);
    }
    SECTION("zero Middle layers remains valid")
    {
        config.set("raft_interface_layers", 0);
    }
    SECTION("path resolution is an accepted explicit boundary")
    {
        config.set("raft_base_line_spacing", RESOLUTION);
        config.set("raft_interface_line_spacing", RESOLUTION);
        config.set("raft_surface_line_spacing", RESOLUTION);
    }
    SECTION("business Surface spacing remains accepted")
    {
        config.set("raft_surface_line_spacing", 0.3);
    }
    SECTION("normal Base margin accepts the exact representable boundary")
    {
        config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stNormal));
        config.set("raft_base_margin", 1.5);
        config.set("raft_surface_margin", 1.0);
    }
    SECTION("tree raft accepts a Base margin smaller than Surface")
    {
        config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTree));
        config.set("raft_base_margin", 0.5);
        config.set("raft_surface_margin", 1.0);
    }

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
    model_object->add_instance();
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);
    const StringObjectException validation = print.validate();
    INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
    REQUIRE(validation.string.empty());
}

TEST_CASE("Cura V1 current-tool raft phases require equal nozzle diameters", "[SupportMaterial][Raft][CuraV1][Validation]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("raft_base_layer_height", 0.3);
    config.set("raft_interface_layer_height", 0.3);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_base_line_width", 0.6);
    config.set("raft_interface_line_width", 0.6);
    config.set("raft_surface_line_width", 0.42);
    config.set("before_layer_change_gcode", std::string("G92 E0"));

    const char *expected_key = nullptr;
    SECTION("Base current tool is ambiguous with mixed nozzle diameters")
    {
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.6}));
        config.set("support_filament", 0);
        config.set("support_interface_filament", 1);
        expected_key = "support_filament";
    }
    SECTION("Interface current tool is ambiguous with mixed nozzle diameters")
    {
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.6}));
        config.set("support_filament", 1);
        config.set("support_interface_filament", 0);
        expected_key = "support_interface_filament";
    }
    SECTION("explicit phase tools remain valid with mixed nozzle diameters")
    {
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.6}));
        config.set("support_filament", 1);
        config.set("support_interface_filament", 1);
    }
    SECTION("current phase tools remain valid with equal nozzle diameters")
    {
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.4}));
        config.set("support_filament", 0);
        config.set("support_interface_filament", 0);
    }

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
    model_object->add_instance();
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);
    const StringObjectException error = print.validate();
    INFO("validation error: " << error.string << "; key: " << error.opt_key);
    if (expected_key == nullptr) {
        REQUIRE(error.string.empty());
    } else {
        REQUIRE_FALSE(error.string.empty());
        REQUIRE(error.opt_key == expected_key);
    }
}

TEST_CASE("Mixed object-level raft settings prefer the first valid CuraV1 configuration",
          "[SupportMaterial][Raft][CuraV1][Compatibility]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("raft_airgap", 0.27);
    config.set("raft_base_layer_height", 0.3);
    config.set("raft_interface_layer_height", 0.3);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_base_line_width", 0.6);
    config.set("raft_interface_line_width", 0.6);
    config.set("raft_surface_line_width", 0.42);
    config.set("raft_surface_speed", 60.);
    config.set("before_layer_change_gcode", std::string("G92 E0"));

    Model model;
    const auto add_cube = [&model](const char *name, double x) -> ModelObject * {
        ModelObject *object = model.add_object();
        object->name = name;
        object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
        ModelInstance *instance = object->add_instance();
        instance->set_offset(Vec3d(x, 100., 0.));
        object->ensure_on_bed();
        return object;
    };
    ModelObject *active_legacy = add_cube("active Legacy raft object", 40.);
    ModelObject *disabled_legacy = add_cube("disabled Legacy raft object", 80.);
    ModelObject *first_cura = add_cube("first valid CuraV1 raft object", 120.);
    ModelObject *second_cura = add_cube("second valid CuraV1 raft object", 160.);

    active_legacy->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    active_legacy->config.set("raft_layers", 3);
    active_legacy->config.set("raft_contact_distance", 0.11);
    disabled_legacy->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    disabled_legacy->config.set("raft_layers", 0);
    first_cura->config.set("raft_surface_speed", 41.);
    second_cura->config.set("raft_surface_speed", 52.);

    Print print;
    for (ModelObject *object : model.objects)
        print.auto_assign_extruders(object);
    print.apply(model, config);
    const StringObjectException error = print.validate();
    INFO("validation error: " << error.string << "; key: " << error.opt_key);
    REQUIRE(error.string.empty());
    REQUIRE(print.objects().size() == 4);

    const PrintObjectConfig &converted_config = print.objects()[0]->config();
    const PrintObjectConfig &disabled_config = print.objects()[1]->config();
    const PrintObjectConfig &donor_config = print.objects()[2]->config();
    const PrintObjectConfig &other_cura_config = print.objects()[3]->config();

    REQUIRE(converted_config.raft_mode.value == RaftMode::CuraV1);
    REQUIRE(disabled_config.raft_mode.value == RaftMode::Legacy);
    REQUIRE(disabled_config.raft_layers.value == 0);
    REQUIRE(other_cura_config.raft_mode.value == RaftMode::CuraV1);
    REQUIRE(donor_config.raft_surface_speed.value == Approx(41.));
    REQUIRE(other_cura_config.raft_surface_speed.value == Approx(52.));

    // The active Legacy object receives the donor's complete runtime raft
    // configuration. A later CuraV1 object keeps its own settings.
    for (const t_config_option_key &key : donor_config.keys()) {
        if (key.compare(0, 5, "raft_") != 0)
            continue;
        INFO("runtime CuraV1 donor key: " << key);
        REQUIRE(*converted_config.option(key) == *donor_config.option(key));
    }

    // Runtime compatibility must not rewrite the source Model/3MF/JSON data.
    REQUIRE(active_legacy->config.get().opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(active_legacy->config.get().opt_int("raft_layers") == 3);
    REQUIRE(active_legacy->config.get().opt_float("raft_contact_distance") == Approx(0.11));
    REQUIRE(disabled_legacy->config.get().opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(disabled_legacy->config.get().opt_int("raft_layers") == 0);

    REQUIRE(print.objects()[0]->slicing_parameters().raft_layers() == 5);
    REQUIRE(print.objects()[1]->slicing_parameters().raft_layers() == 0);
    REQUIRE(print.objects()[2]->slicing_parameters().raft_layers() == 5);
    REQUIRE(print.objects()[3]->slicing_parameters().raft_layers() == 5);
    REQUIRE(print.apply(model, config) == Print::APPLY_STATUS_UNCHANGED);

    print.set_status_silent();
    print.process();
    REQUIRE(print.objects()[0]->support_layers().size() == 5);
    REQUIRE(print.objects()[1]->support_layers().empty());

    // If all valid CuraV1 donors disappear, a subsequent apply reconstructs
    // the original Legacy settings instead of retaining stale runtime values.
    first_cura->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    first_cura->config.set("raft_layers", 0);
    second_cura->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    second_cura->config.set("raft_layers", 0);
    print.apply(model, config);
    REQUIRE(print.validate().string.empty());
    REQUIRE(print.objects()[0]->config().raft_mode.value == RaftMode::Legacy);
    REQUIRE(print.objects()[0]->config().raft_layers.value == 3);
    REQUIRE(print.objects()[0]->config().raft_contact_distance.value == Approx(0.11));
    REQUIRE(print.objects()[1]->config().raft_mode.value == RaftMode::Legacy);
    REQUIRE(print.objects()[1]->config().raft_layers.value == 0);
    print.process();
    REQUIRE(print.objects()[0]->support_layers().size() == 3);
    REQUIRE(print.objects()[1]->support_layers().empty());
    REQUIRE(print.objects()[2]->support_layers().empty());
    REQUIRE(print.objects()[3]->support_layers().empty());
}

TEST_CASE("CLI process JSON preserves CuraV1 compatibility across global and object scopes",
          "[SupportMaterial][Raft][CuraV1][Compatibility][CLIConfig]")
{
    const auto load_process_json = [](const char *prefix, const nlohmann::json &document) {
        const boost::filesystem::path path = boost::filesystem::temp_directory_path() /
                                             boost::filesystem::unique_path(std::string(prefix) + "-%%%%-%%%%.json");
        {
            std::ofstream output(path.string());
            output << document.dump(2);
        }

        DynamicPrintConfig                 config;
        std::map<std::string, std::string> key_values;
        std::string                        reason;
        config.load_from_json(path.string(), ForwardCompatibilitySubstitutionRule::Enable, key_values, reason);
        boost::filesystem::remove(path);

        REQUIRE(reason.empty());
        REQUIRE(key_values.at("type") == "process");
        REQUIRE(key_values.at("from") == "user");
        return config;
    };

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    const DynamicPrintConfig global_process = load_process_json(
        "orca-cli-global-process",
        {{"name", "global process"},
         {"type", "process"},
         {"from", "user"},
         {"raft_mode", "legacy"},
         {"raft_layers", "0"},
         {"layer_height", "0.18"},
         {"before_layer_change_gcode", "G92 E0"}});
    config.apply(global_process, true);
    config.normalize_fdm();

    const DynamicPrintConfig legacy_process = load_process_json(
        "orca-cli-object-legacy-process",
        {{"name", "active Legacy object process"},
         {"type", "process"},
         {"from", "user"},
         {"raft_mode", "legacy"},
         {"raft_layers", "3"},
         {"raft_contact_distance", "0.11"},
         {"layer_height", "0.17"},
         {"raft_future_optional_bias", "19"}});
    const DynamicPrintConfig cura_process = load_process_json(
        "orca-cli-object-cura-process",
        {{"name", "CuraV1 object process"},
         {"type", "process"},
         {"from", "user"},
         {"raft_mode", "cura_v1"},
         {"raft_base_layers", "1"},
         {"raft_interface_layers", "2"},
         {"raft_surface_layers", "2"},
         {"raft_airgap", "0.29"},
         {"raft_surface_speed", "41"},
         {"layer_height", "0.21"},
         {"raft_future_surface_bias", "23"}});

    REQUIRE_FALSE(legacy_process.has("raft_future_optional_bias"));
    REQUIRE_FALSE(cura_process.has("raft_future_surface_bias"));

    Model model;
    const auto add_cube = [&model](const char *name, double x) -> ModelObject * {
        ModelObject *object = model.add_object();
        object->name = name;
        object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
        ModelInstance *instance = object->add_instance();
        instance->set_offset(Vec3d(x, 100., 0.));
        object->ensure_on_bed();
        return object;
    };

    ModelObject *active_legacy   = add_cube("active Legacy JSON object", 60.);
    ModelObject *cura            = add_cube("CuraV1 JSON object", 100.);
    ModelObject *global_disabled = add_cube("global disabled Legacy object", 140.);
    active_legacy->config.apply(legacy_process, true);
    cura->config.apply(cura_process, true);

    Print print;
    for (ModelObject *object : model.objects)
        print.auto_assign_extruders(object);
    print.apply(model, config);
    const StringObjectException error = print.validate();
    INFO("validation error: " << error.string << "; key: " << error.opt_key);
    REQUIRE(error.string.empty());
    REQUIRE(print.objects().size() == 3);

    const PrintObjectConfig &converted_legacy = print.objects()[0]->config();
    const PrintObjectConfig &cura_config       = print.objects()[1]->config();
    const PrintObjectConfig &disabled_config   = print.objects()[2]->config();

    REQUIRE(converted_legacy.raft_mode.value == RaftMode::CuraV1);
    REQUIRE(converted_legacy.raft_airgap.value == Approx(0.29));
    REQUIRE(converted_legacy.raft_surface_speed.value == Approx(41.));
    REQUIRE(converted_legacy.layer_height.value == Approx(0.17));
    REQUIRE(cura_config.raft_mode.value == RaftMode::CuraV1);
    REQUIRE(cura_config.layer_height.value == Approx(0.21));
    REQUIRE(disabled_config.raft_mode.value == RaftMode::Legacy);
    REQUIRE(disabled_config.raft_layers.value == 0);
    REQUIRE(disabled_config.layer_height.value == Approx(0.18));

    // The compatibility conversion is runtime-only: source JSON/model data
    // remains intact for saving, reloading, and a later apply without CuraV1.
    REQUIRE(active_legacy->config.get().opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(active_legacy->config.get().opt_int("raft_layers") == 3);
    REQUIRE(active_legacy->config.get().opt_float("raft_contact_distance") == Approx(0.11));
    REQUIRE(cura->config.get().opt_enum<RaftMode>("raft_mode") == RaftMode::CuraV1);
    REQUIRE(global_disabled->config.empty());
}

TEST_CASE("Mixed unrafted brim and CuraV1 raft keep their physical first-layer flows",
          "[SupportMaterial][Raft][CuraV1][Compatibility][Brim]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    config.set("raft_layers", 0);
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set("initial_layer_print_height", 0.24);
    config.set("initial_layer_line_width", 0.42);
    config.set_key_value("nozzle_temperature_initial_layer", new ConfigOptionInts({215}));
    config.set_key_value("nozzle_temperature", new ConfigOptionInts({190}));
    config.set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(btPEI));
    config.set_key_value("hot_plate_temp_initial_layer", new ConfigOptionInts({60}));
    config.set_key_value("hot_plate_temp", new ConfigOptionInts({45}));
    config.set_key_value("gcode_flavor", new ConfigOptionEnum<GCodeFlavor>(gcfMarlinFirmware));
    config.set("machine_start_gcode", std::string("G90\nM83\nG92 E0"));
    config.set("before_layer_change_gcode", std::string("G92 E0"));

    for (const std::string &key : config.keys()) {
        const auto *enum_option = dynamic_cast<const ConfigOptionEnumsGeneric *>(config.option(key));
        const ConfigOptionDef *option_def = print_config_def.get(key);
        if (enum_option != nullptr && enum_option->keys_map == nullptr && option_def != nullptr && option_def->enum_keys_map != nullptr) {
            auto *mapped_option = new ConfigOptionEnumsGeneric(option_def->enum_keys_map);
            mapped_option->values = enum_option->values;
            config.set_key_value(key, mapped_option);
        }
    }

    const auto run_order = [&config](bool cura_first) {
        Model model;
        const auto add_cube = [&model](const char *name, double x) -> ModelObject * {
            ModelObject *object = model.add_object();
            object->name = name;
            object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
            ModelInstance *instance = object->add_instance();
            instance->set_offset(Vec3d(x, 100., 0.));
            object->ensure_on_bed();
            return object;
        };
        const auto configure_cura_raft = [](ModelObject *object) {
            object->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
            object->config.set("raft_base_layers", 1);
            object->config.set("raft_interface_layers", 2);
            object->config.set("raft_surface_layers", 2);
            object->config.set("raft_base_layer_height", 0.3);
            object->config.set("raft_interface_layer_height", 0.3);
            object->config.set("raft_surface_layer_height", 0.2);
        };
        const auto configure_unrafted_brim = [](ModelObject *object) {
            object->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
            object->config.set("raft_layers", 0);
            object->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(BrimType::btOuterOnly));
            object->config.set("brim_width", 3.);
        };

        ModelObject *first  = add_cube(cura_first ? "CuraV1 raft object" : "unrafted brim object", 80.);
        ModelObject *second = add_cube(cura_first ? "unrafted brim object" : "CuraV1 raft object", 120.);
        configure_cura_raft(cura_first ? first : second);
        configure_unrafted_brim(cura_first ? second : first);

        Print print;
        for (ModelObject *object : model.objects)
            print.auto_assign_extruders(object);
        print.apply(model, config);
        REQUIRE(print.validate().string.empty());
        REQUIRE(print.objects().size() == 2);

        const PrintObject *cura_print_object      = print.objects()[cura_first ? 0 : 1];
        const PrintObject *unrafted_print_object = print.objects()[cura_first ? 1 : 0];
        REQUIRE(cura_print_object->slicing_parameters().first_print_layer_height == Approx(0.3));
        REQUIRE(unrafted_print_object->slicing_parameters().first_print_layer_height == Approx(0.24));
        REQUIRE(print.skirt_first_layer_height() == Approx(0.24));
        REQUIRE(print.brim_flow().height() == Approx(0.24));

        print.set_status_silent();
        print.process();

        REQUIRE(unrafted_print_object->layers().front()->height == Approx(0.24));
        const auto brim_it = print.get_brimMap().find(unrafted_print_object->id());
        REQUIRE(brim_it != print.get_brimMap().end());
        REQUIRE_FALSE(brim_it->second.empty());

        const Flow expected_flow = print.brim_flow(unrafted_print_object->layers().front()->height);
        size_t     brim_path_count = 0;
        const auto inspect_path = [&expected_flow, &brim_path_count](const ExtrusionPath &path) {
            ++brim_path_count;
            REQUIRE(path.height == Approx(0.24));
            REQUIRE(path.width == Approx(expected_flow.width()));
            REQUIRE(path.mm3_per_mm == Approx(expected_flow.mm3_per_mm()));
        };
        for (const ExtrusionEntity *entity : brim_it->second.entities) {
            if (const auto *path = dynamic_cast<const ExtrusionPath *>(entity)) {
                inspect_path(*path);
            } else if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(entity)) {
                for (const ExtrusionPath &path : multipath->paths)
                    inspect_path(path);
            } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(entity)) {
                for (const ExtrusionPath &path : loop->paths)
                    inspect_path(path);
            }
        }
        REQUIRE(brim_path_count > 0);

        const boost::filesystem::path gcode_path =
            boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orca-mixed-first-layer-temp-%%%%-%%%%.gcode");
        set_resources_dir((boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path() / "resources").string());
        GCodeProcessorResult gcode_result;
        print.export_gcode(gcode_path.string(), &gcode_result, nullptr);
        std::ifstream gcode_stream(gcode_path.string());
        const std::string gcode((std::istreambuf_iterator<char>(gcode_stream)), std::istreambuf_iterator<char>());

        size_t cura_base_marker = gcode.find(";Z:0.3");
        if (cura_base_marker == std::string::npos)
            cura_base_marker = gcode.find("; Z_HEIGHT: 0.3");
        REQUIRE(cura_base_marker != std::string::npos);

        const size_t normal_nozzle_temperature = gcode.find("M104 S190");
        const size_t normal_bed_temperature = gcode.find("M140 S45");
        REQUIRE(normal_nozzle_temperature != std::string::npos);
        REQUIRE(normal_bed_temperature != std::string::npos);
        REQUIRE(normal_nozzle_temperature > cura_base_marker);
        REQUIRE(normal_bed_temperature > cura_base_marker);
        boost::filesystem::remove(gcode_path);
    };

    run_order(true);
    run_order(false);
}

TEST_CASE("Mixed same-Z CuraV1 and unrafted paths keep object-local layer context",
          "[SupportMaterial][Raft][CuraV1][Compatibility][GCode]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.3);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set("initial_layer_speed", 7.);
    config.set("initial_layer_infill_speed", 11.);
    config.set_key_value("initial_layer_travel_speed", new ConfigOptionFloatOrPercent(3., false));
    config.set("travel_speed", 100.);
    // 1.3 mm Raft top + 0.3 mm air gap + 0.3 mm model L1 = 1.9 mm,
    // exactly matching an ordinary 0.2 mm layer of the unrafted object.
    config.set("raft_airgap", 0.3);
    config.set("raft_layer_0_z_overlap", 0.1);
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("raft_base_layer_height", 0.3);
    config.set("raft_interface_layer_height", 0.3);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_base_line_width", 0.6);
    config.set("raft_interface_line_width", 0.6);
    config.set("raft_surface_line_width", 0.42);
    config.set("raft_base_line_spacing", 1.5);
    config.set("raft_interface_line_spacing", 1.5);
    config.set("raft_surface_line_spacing", 0.42);
    config.set_key_value("raft_base_fan_speed", new ConfigOptionPercent(73.));
    config.set_key_value("close_fan_the_first_x_layers", new ConfigOptionInts({1}));
    config.set_key_value("full_fan_speed_layer", new ConfigOptionInts({3}));
    config.set_key_value("gcode_flavor", new ConfigOptionEnum<GCodeFlavor>(gcfMarlinFirmware));
    config.set("gcode_label_objects", true);
    config.set("machine_start_gcode", std::string("G90\nM83\nG92 E0"));
    config.set("before_layer_change_gcode", std::string("G92 E0"));
    config.option<ConfigOptionBools>("slow_down_for_layer_cooling")->values = {false};

    // Synthetic full configs contain generic enum-vector defaults without
    // their serialization maps. Normal presets already provide these maps.
    for (const std::string &key : config.keys()) {
        const auto *enum_option = dynamic_cast<const ConfigOptionEnumsGeneric *>(config.option(key));
        const ConfigOptionDef *option_def = print_config_def.get(key);
        if (enum_option != nullptr && enum_option->keys_map == nullptr && option_def != nullptr && option_def->enum_keys_map != nullptr) {
            auto *mapped_option = new ConfigOptionEnumsGeneric(option_def->enum_keys_map);
            mapped_option->values = enum_option->values;
            config.set_key_value(key, mapped_option);
        }
    }

    Model model;
    const auto add_cube = [&model](const char *name, double x) {
        ModelObject *object = model.add_object();
        object->name = name;
        object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
        ModelInstance *instance = object->add_instance();
        instance->set_offset(Vec3d(x, 100., 0.));
        object->ensure_on_bed();
        return object;
    };
    ModelObject *unrafted = add_cube("mixed unrafted object", 80.);
    ModelObject *cura_raft = add_cube("mixed CuraV1 raft object", 120.);
    unrafted->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    unrafted->config.set("raft_layers", 0);

    Print print;
    print.auto_assign_extruders(unrafted);
    print.auto_assign_extruders(cura_raft);
    print.apply(model, config);
    const StringObjectException validation = print.validate();
    INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
    REQUIRE(validation.string.empty());
    REQUIRE(print.objects()[0]->config().raft_mode.value == RaftMode::Legacy);
    REQUIRE(print.objects()[0]->slicing_parameters().raft_layers() == 0);
    REQUIRE(print.objects()[1]->config().raft_mode.value == RaftMode::CuraV1);
    REQUIRE(print.objects()[1]->slicing_parameters().raft_layers() == 5);
    print.set_status_silent();
    print.process();

    const PrintObject *unrafted_object = print.objects()[0];
    const PrintObject *cura_object = print.objects()[1];
    const double cura_model_first_z = cura_object->layers().front()->print_z;
    const auto closest_unrafted_layer = std::min_element(
        unrafted_object->layers().begin(), unrafted_object->layers().end(), [cura_model_first_z](const Layer *lhs, const Layer *rhs) {
            return std::abs(lhs->print_z - cura_model_first_z) < std::abs(rhs->print_z - cura_model_first_z);
        });
    REQUIRE(closest_unrafted_layer != unrafted_object->layers().end());
    CAPTURE(cura_model_first_z, (*closest_unrafted_layer)->print_z,
            std::abs((*closest_unrafted_layer)->print_z - cura_model_first_z));
    REQUIRE(std::any_of(unrafted_object->layers().begin(), unrafted_object->layers().end(), [cura_model_first_z](const Layer *layer) {
        return is_approx(layer->print_z, cura_model_first_z);
    }));

    const boost::filesystem::path gcode_path =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orca-mixed-layer-context-%%%%-%%%%.gcode");
    set_resources_dir((boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path() / "resources").string());
    GCodeProcessorResult gcode_result;
    print.export_gcode(gcode_path.string(), &gcode_result, nullptr);
    std::ifstream gcode_stream(gcode_path.string());
    const std::string gcode((std::istreambuf_iterator<char>(gcode_stream)), std::istreambuf_iterator<char>());

    const auto layer_marker_position = [&gcode](double print_z) {
        std::ostringstream compact_marker;
        compact_marker << ";Z:" << print_z;
        size_t position = gcode.find(compact_marker.str());
        if (position == std::string::npos) {
            std::ostringstream verbose_marker;
            verbose_marker << "; Z_HEIGHT: " << print_z;
            position = gcode.find(verbose_marker.str());
        }
        return position;
    };
    const auto layer_section = [&gcode, &layer_marker_position](double print_z) {
        const size_t begin = layer_marker_position(print_z);
        REQUIRE(begin != std::string::npos);
        const size_t end = gcode.find(";LAYER_CHANGE", begin + 1);
        return gcode.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    };

    // At Z0.3 the unrafted model L1 and CuraV1 Base share one physical
    // group. Raft paths use their 73% phase fan and then restore the regular
    // first-model-layer fan instead of leaking either value across paths.
    const std::string mixed_base_section = layer_section(0.3);
    const size_t raft_object_begin = mixed_base_section.find("; printing object mixed CuraV1 raft object id:");
    REQUIRE(raft_object_begin != std::string::npos);
    const size_t raft_fan_position = mixed_base_section.find("M106 S186");
    REQUIRE(raft_fan_position != std::string::npos);
    REQUIRE(raft_fan_position > raft_object_begin);
    // A plate-level skirt/support brim sharing this SupportLayer must not
    // activate the Raft phase fan before the Raft object itself starts.
    const size_t first_raft_extrusion = mixed_base_section.find(" E", raft_fan_position);
    REQUIRE(first_raft_extrusion != std::string::npos);
    const size_t regular_fan_restore = mixed_base_section.find("M106 S0", first_raft_extrusion);
    REQUIRE(regular_fan_restore != std::string::npos);

    // Later, CuraV1 model L1 shares a Z with an ordinary higher layer of the
    // unrafted object. Its first travel must still use 3 mm/s (F180), even
    // though the first object in the group is not on its initial layer.
    const std::string mixed_model_section = layer_section(cura_model_first_z);
    const std::string object_start = "; printing object mixed CuraV1 raft object id:";
    const size_t object_begin = mixed_model_section.find(object_start);
    REQUIRE(object_begin != std::string::npos);
    const size_t object_end = mixed_model_section.find("; stop printing object mixed CuraV1 raft object id:", object_begin);
    REQUIRE(object_end != std::string::npos);
    const std::string cura_object_section = mixed_model_section.substr(object_begin, object_end - object_begin);
    REQUIRE(cura_object_section.find("F180") != std::string::npos);
    boost::filesystem::remove(gcode_path);
}

TEST_CASE("ByObject restarts the physical first-layer transition for each object",
          "[SupportMaterial][Raft][CuraV1][Compatibility][GCode][ByObject]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByObject));
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("initial_layer_print_height", 0.3);
    config.set("raft_airgap", 0.3);
    config.set("raft_layer_0_z_overlap", 0.1);
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("raft_base_layer_height", 0.3);
    config.set("raft_interface_layer_height", 0.3);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_base_line_width", 0.6);
    config.set("raft_interface_line_width", 0.6);
    config.set("raft_surface_line_width", 0.42);
    config.set("raft_base_line_spacing", 1.5);
    config.set("raft_interface_line_spacing", 1.5);
    config.set("raft_surface_line_spacing", 0.42);
    config.set("gcode_label_objects", true);
    config.set("machine_start_gcode", std::string("G90\nM83\nG92 E0"));
    config.set("before_layer_change_gcode", std::string("G92 E0"));

    for (const std::string &key : config.keys()) {
        const auto *enum_option = dynamic_cast<const ConfigOptionEnumsGeneric *>(config.option(key));
        const ConfigOptionDef *option_def = print_config_def.get(key);
        if (enum_option != nullptr && enum_option->keys_map == nullptr && option_def != nullptr && option_def->enum_keys_map != nullptr) {
            auto *mapped_option = new ConfigOptionEnumsGeneric(option_def->enum_keys_map);
            mapped_option->values = enum_option->values;
            config.set_key_value(key, mapped_option);
        }
    }

    Model model;
    const auto add_cube = [&model](const char *name, double x) {
        ModelObject *object = model.add_object();
        object->name = name;
        object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
        ModelInstance *instance = object->add_instance();
        instance->set_offset(Vec3d(x, 100., 0.));
        object->ensure_on_bed();
        return object;
    };
    add_cube("sequential CuraV1 object", 60.);
    ModelObject *unrafted = add_cube("sequential unrafted object", 180.);
    unrafted->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    unrafted->config.set("raft_layers", 0);

    Print print;
    for (ModelObject *object : model.objects)
        print.auto_assign_extruders(object);
    print.apply(model, config);
    const StringObjectException validation = print.validate();
    INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
    REQUIRE(validation.string.empty());
    print.set_status_silent();
    print.process();

    const boost::filesystem::path gcode_path =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orca-by-object-first-layer-%%%%-%%%%.gcode");
    set_resources_dir((boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path() / "resources").string());
    GCodeProcessorResult gcode_result;
    print.export_gcode(gcode_path.string(), &gcode_result, nullptr);
    std::ifstream gcode_stream(gcode_path.string());
    const std::string gcode((std::istreambuf_iterator<char>(gcode_stream)), std::istreambuf_iterator<char>());

    size_t first_height_count = 0;
    const auto count_first_height = [&gcode, &first_height_count](const std::string &marker) {
        size_t height_position = 0;
        while ((height_position = gcode.find(marker, height_position)) != std::string::npos) {
            height_position += marker.size();
            const double height = std::stod(gcode.substr(height_position));
            CAPTURE(marker, height);
            REQUIRE(height > 0.);
            if (height == Approx(0.3))
                ++first_height_count;
        }
    };
    // Different G-code flavors expose the same layer transition with either
    // the legacy HEIGHT marker or Orca's Z_HEIGHT marker.
    count_first_height(";HEIGHT:");
    count_first_height("; Z_HEIGHT:");
    REQUIRE(first_height_count >= 2);
    boost::filesystem::remove(gcode_path);
}

TEST_CASE("Pure Legacy object-level raft settings retain their existing per-object behavior",
          "[SupportMaterial][Raft][Legacy][Compatibility]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    config.set("raft_layers", 3);
    config.set("raft_contact_distance", 0.1);
    config.set("before_layer_change_gcode", std::string("G92 E0"));

    Model model;
    for (size_t object_idx = 0; object_idx < 2; ++object_idx) {
        ModelObject *object = model.add_object();
        object->name = "Legacy raft object";
        object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
        ModelInstance *instance = object->add_instance();
        instance->set_offset(Vec3d(80. + 40. * object_idx, 100., 0.));
        object->ensure_on_bed();
        if (object_idx == 1) {
            object->config.set("raft_layers", 7);
            object->config.set("raft_contact_distance", 0.35);
        }
    }

    Print print;
    for (ModelObject *object : model.objects)
        print.auto_assign_extruders(object);
    print.apply(model, config);
    REQUIRE(print.validate().string.empty());
    REQUIRE(print.objects().size() == 2);
    REQUIRE(print.objects()[0]->config().raft_mode.value == RaftMode::Legacy);
    REQUIRE(print.objects()[0]->config().raft_layers.value == 3);
    REQUIRE(print.objects()[0]->config().raft_contact_distance.value == Approx(0.1));
    REQUIRE(print.objects()[1]->config().raft_mode.value == RaftMode::Legacy);
    REQUIRE(print.objects()[1]->config().raft_layers.value == 7);
    REQUIRE(print.objects()[1]->config().raft_contact_distance.value == Approx(0.35));
}

TEST_CASE("Mixed raft compatibility skips CuraV1 donors with invalid mandatory phase counts",
          "[SupportMaterial][Raft][CuraV1][Compatibility][Validation]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("before_layer_change_gcode", std::string("G92 E0"));

    Model model;
    const auto add_cube = [&model](double x) -> ModelObject * {
        ModelObject *object = model.add_object();
        object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
        ModelInstance *instance = object->add_instance();
        instance->set_offset(Vec3d(x, 100., 0.));
        object->ensure_on_bed();
        return object;
    };
    ModelObject *active_legacy = add_cube(70.);
    ModelObject *invalid_cura = add_cube(110.);
    ModelObject *valid_cura = add_cube(150.);
    active_legacy->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    active_legacy->config.set("raft_layers", 3);
    invalid_cura->config.set("raft_base_layers", 0);
    valid_cura->config.set("raft_surface_speed", 43.);

    Print print;
    for (ModelObject *object : model.objects)
        print.auto_assign_extruders(object);
    print.apply(model, config);

    REQUIRE(print.objects().size() == 3);
    REQUIRE(print.objects()[0]->config().raft_mode.value == RaftMode::CuraV1);
    REQUIRE(print.objects()[0]->config().raft_surface_speed.value == Approx(43.));
    REQUIRE(print.objects()[1]->config().raft_base_layers.value == 0);
    const StringObjectException error = print.validate();
    REQUIRE_FALSE(error.string.empty());
    REQUIRE(error.object == print.objects()[1]);
    REQUIRE(error.opt_key == "raft_base_layers");
}

TEST_CASE("Cura V1 overlap stays below a thin second model layer", "[SupportMaterial][Raft][CuraV1][VariableLayer]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.6}));
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.42);
    config.set("raft_layer_0_z_overlap", 0.1);
    config.set("raft_base_layer_height", 0.3);
    config.set("raft_interface_layer_height", 0.3);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_base_line_width", 0.6);
    config.set("raft_interface_line_width", 0.6);
    config.set("raft_surface_line_width", 0.42);
    config.set_key_value("min_layer_height", new ConfigOptionFloats({0.04}));
    config.set("before_layer_change_gcode", std::string("G92 E0"));

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
    model_object->add_instance();
    DynamicPrintConfig thin_second_layer;
    thin_second_layer.set_key_value("layer_height", new ConfigOptionFloat(0.08));
    model_object->layer_config_ranges[{0.42, 0.50}].assign_config(std::move(thin_second_layer));
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);
    REQUIRE(print.validate().string.empty());
    print.set_status_silent();
    print.process();

    const auto layers = print.objects().front()->layers();
    REQUIRE(layers.size() >= 2);
    REQUIRE(layers[1]->height == Approx(0.08));
    REQUIRE(layers[1]->print_z > layers[0]->print_z);
    REQUIRE(layers[1]->print_z - layers[0]->print_z >=
            print.objects().front()->slicing_parameters().min_layer_height - EPSILON);
}

TEST_CASE("Cura V1 model features use model-local layer ids above the raft", "[SupportMaterial][Raft][CuraV1][LayerId]")
{
    REQUIRE(SlicingParameters::model_layer_id(5, true, 5) == 0);
    REQUIRE(SlicingParameters::model_layer_id(6, true, 5) == 1);
    REQUIRE(SlicingParameters::model_layer_id(5, false, 5) == 5);

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.6}));
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.3);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.6, false));
    config.set("inner_wall_line_width", 0.42);
    config.set("outer_wall_line_width", 0.42);
    config.set("raft_airgap", 0.2);
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 1);
    config.set("raft_surface_layers", 1);
    config.set("raft_base_layer_height", 0.2);
    config.set("raft_interface_layer_height", 0.2);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_base_line_width", 0.4);
    config.set("raft_interface_line_width", 0.4);
    config.set("raft_surface_line_width", 0.4);
    config.set("raft_base_line_spacing", 0.8);
    config.set("raft_interface_line_spacing", 0.8);
    config.set("raft_surface_line_spacing", 0.4);
    config.set("support_filament", 1);
    config.set("support_interface_filament", 1);
    config.set("support_top_z_distance", 0.2);
    config.set("wall_loops", 1);
    config.set("bottom_shell_layers", 0);
    config.set("top_shell_layers", 0);
    config.set_key_value("sparse_infill_density", new ConfigOptionPercent(20.));
    config.set("infill_combination", true);
    config.set_key_value("infill_combination_max_layer_height", new ConfigOptionFloatOrPercent(0.6, false));
    // The template evaluator indexes PrintObject::layers(). A physical raft
    // offset used here would walk past this intentionally short model.
    config.set("sparse_infill_rotate_template", std::string("0,90!"));
    config.set_key_value("seam_slope_type", new ConfigOptionEnum<SeamScarfType>(SeamScarfType::All));
    config.set("seam_slope_conditional", false);
    config.set("seam_slope_min_length", 5.);
    config.set("seam_slope_steps", 5);
    config.set("machine_start_gcode", std::string("G90\nM83\nG92 E0"));
    config.set("before_layer_change_gcode", std::string("G92 E0"));

    // Synthetic full configs contain enum-vector defaults without their
    // serialization maps. Normal presets provide these maps.
    for (const std::string &key : config.keys()) {
        const auto *enum_option = dynamic_cast<const ConfigOptionEnumsGeneric *>(config.option(key));
        const ConfigOptionDef *option_def = print_config_def.get(key);
        if (enum_option != nullptr && enum_option->keys_map == nullptr && option_def != nullptr && option_def->enum_keys_map != nullptr) {
            auto *mapped_option = new ConfigOptionEnumsGeneric(option_def->enum_keys_map);
            mapped_option->values = enum_option->values;
            config.set_key_value(key, mapped_option);
        }
    }

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->name = "short Cura raft model-layer semantics cube";
    model_object->add_volume(make_cube(20., 20., 0.8));
    ModelInstance *instance = model_object->add_instance();
    instance->set_offset(Vec3d(100., 100., 0.));
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);
    const StringObjectException validation = print.validate();
    INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
    REQUIRE(validation.string.empty());
    print.set_status_silent();
    print.process();

    const PrintObject *object = print.objects().front();
    REQUIRE(object->slicing_parameters().raft_layers() == 3);
    REQUIRE(object->layers().size() >= 3);
    REQUIRE(object->slicing_parameters().model_layer_id(object->layers()[0]->id()) == 0);
    REQUIRE(object->slicing_parameters().model_layer_id(object->layers()[1]->id()) == 1);

    // Combined sparse infill must not consume the first model layer.
    const ExtrusionEntityCollection &first_layer_fills = object->layers()[0]->get_region(0)->fills;
    REQUIRE(std::any_of(first_layer_fills.entities.begin(), first_layer_fills.entities.end(),
                        [](const ExtrusionEntity *entity) { return entity->role() == erInternalInfill; }));

    const boost::filesystem::path gcode_path =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orca-raft-layer-id-%%%%-%%%%.gcode");
    set_resources_dir((boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path() / "resources").string());
    GCodeProcessorResult gcode_result;
    print.export_gcode(gcode_path.string(), &gcode_result, nullptr);
    std::ifstream gcode_stream(gcode_path.string());
    const std::string gcode((std::istreambuf_iterator<char>(gcode_stream)), std::istreambuf_iterator<char>());

    const auto layer_marker_position = [&gcode](double print_z, size_t offset = 0) {
        std::ostringstream compact_marker;
        compact_marker << ";Z:" << print_z;
        size_t position = gcode.find(compact_marker.str(), offset);
        if (position == std::string::npos) {
            std::ostringstream verbose_marker;
            verbose_marker << "; Z_HEIGHT: " << print_z;
            position = gcode.find(verbose_marker.str(), offset);
        }
        return position;
    };
    const auto layer_section = [&gcode, &layer_marker_position](double print_z, double next_print_z) {
        const size_t begin = layer_marker_position(print_z);
        REQUIRE(begin != std::string::npos);
        const size_t end = layer_marker_position(next_print_z, begin + 1);
        REQUIRE(end != std::string::npos);
        return gcode.substr(begin, end - begin);
    };
    const auto has_sloped_extrusion = [](const std::string &section) {
        std::istringstream lines(section);
        for (std::string line; std::getline(lines, line);)
            if (line.rfind("G1 ", 0) == 0 && line.find('E') != std::string::npos && line.find('Z') != std::string::npos)
                return true;
        return false;
    };

    const std::string first_model_layer_gcode =
        layer_section(object->layers()[0]->print_z, object->layers()[1]->print_z);
    const std::string second_model_layer_gcode =
        layer_section(object->layers()[1]->print_z, object->layers()[2]->print_z);
    REQUIRE_FALSE(has_sloped_extrusion(first_model_layer_gcode));
    REQUIRE(has_sloped_extrusion(second_model_layer_gcode));
    boost::filesystem::remove(gcode_path);
}

TEST_CASE("Cura V1 raft reaches support toolpaths and G-code phase controls", "[SupportMaterial][Raft][CuraV1][GCode]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    // The ordinary support interface is deliberately Grid.  CuraV1 Raft
    // Surface must keep its own phase topology instead of inheriting this
    // unrelated support setting.
    config.set_key_value("support_interface_pattern",
                         new ConfigOptionEnum<SupportMaterialInterfacePattern>(smipGrid));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.28);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.58, false));
    config.set("inner_wall_line_width", 0.42);
    config.set("outer_wall_line_width", 0.42);
    config.set_key_value("bottom_surface_pattern", new ConfigOptionEnum<InfillPattern>(ipMonotonic));
    config.set("solid_infill_direction", 20.);
    config.set("initial_layer_speed", 7.);
    config.set("initial_layer_infill_speed", 11.);
    config.set_key_value("initial_layer_travel_speed", new ConfigOptionFloatOrPercent(3., false));
    config.set("travel_speed", 100.);
    config.set("raft_airgap", 0.27);
    config.set("raft_layer_0_z_overlap", 0.1);
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("raft_base_layer_height", 0.3);
    config.set("raft_interface_layer_height", 0.3);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_base_line_width", 0.6);
    config.set("raft_interface_line_width", 0.6);
    config.set("raft_surface_line_width", 0.42);
    config.set("raft_base_line_spacing", 1.5);
    config.set("raft_interface_line_spacing", 1.5);
    // Exercise an overlapping center spacing smaller than the nominal bead
    // spacing; it must not alter the configured 0.42 mm visual line width.
    config.set("raft_surface_line_spacing", 0.3);
    config.set_key_value("raft_surface_flow", new ConfigOptionPercent(150.));
    config.set("raft_base_speed", 10.);
    config.set("raft_interface_speed", 25.);
    config.set("raft_surface_speed", 60.);
    config.set("default_acceleration", 500.);
    config.set("initial_layer_acceleration", 300.);
    config.set("raft_base_acceleration", 600.);
    config.set("raft_interface_acceleration", 2000.);
    config.set("raft_surface_acceleration", 800.);
    config.set_key_value("gcode_flavor", new ConfigOptionEnum<GCodeFlavor>(gcfMarlinFirmware));
    config.set_key_value("machine_max_acceleration_extruding", new ConfigOptionFloats({5000.}));
    config.set_key_value("raft_base_fan_speed", new ConfigOptionPercent(0.));
    config.set_key_value("raft_interface_fan_speed", new ConfigOptionPercent(20.));
    config.set_key_value("raft_surface_fan_speed", new ConfigOptionPercent(40.));
    config.set_key_value("close_fan_the_first_x_layers", new ConfigOptionInts({1}));
    config.set_key_value("full_fan_speed_layer", new ConfigOptionInts({3}));
    config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats({100.}));
    config.set("slow_down_layers", 8);
    config.set("machine_start_gcode", std::string("G90\nM83\nG1 Z0.28 F1200\nG1 X5 Y5 F3000\nG1 X85 E8 F2400\nG92 E0"));
    config.set("before_layer_change_gcode", std::string("G92 E0"));
    config.option<ConfigOptionBools>("slow_down_for_layer_cooling")->values = {false};
    // full_print_config() contains a few generic enum-vector defaults without
    // their serialization maps. Normal presets populate those maps; repair the
    // synthetic test config so G-code export can append its CONFIG_BLOCK.
    for (const std::string &key : config.keys()) {
        const auto *enum_option = dynamic_cast<const ConfigOptionEnumsGeneric *>(config.option(key));
        const ConfigOptionDef *option_def = print_config_def.get(key);
        if (enum_option != nullptr && enum_option->keys_map == nullptr && option_def != nullptr && option_def->enum_keys_map != nullptr) {
            auto *mapped_option = new ConfigOptionEnumsGeneric(option_def->enum_keys_map);
            mapped_option->values = enum_option->values;
            config.set_key_value(key, mapped_option);
        }
    }

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->name = "Cura-style raft integration cube";
    model_object->add_volume(Slic3r::Test::mesh(TestMesh::cube_20x20x20));
    ModelInstance *instance = model_object->add_instance();
    instance->set_offset(Vec3d(100., 100., 0.));
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);
    const StringObjectException validation = print.validate();
    INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
    REQUIRE(validation.string.empty());
    print.set_status_silent();
    print.process();

    const PrintObject *object = print.objects().front();
    REQUIRE(object->slicing_parameters().raft_layers() == 5);
    REQUIRE(object->slicing_parameters().first_print_layer_height == Approx(0.3));
    REQUIRE(object->slicing_parameters().first_object_layer_height == Approx(0.28));
    REQUIRE(object->support_layers().size() == 5);
    const SupportParameters support_params(*object);
    const Flow nominal_base_flow(0.6f, 0.3f, 0.4f);
    const Flow nominal_interface_flow(0.6f, 0.3f, 0.4f);
    const Flow nominal_surface_flow(0.42f, 0.2f, 0.4f);
    REQUIRE(support_params.raft_flow(RaftPhase::Base).width() == Approx(0.6));
    REQUIRE(support_params.raft_flow(RaftPhase::Interface).width() == Approx(0.6));
    REQUIRE(support_params.raft_flow(RaftPhase::Surface).width() == Approx(0.42));
    REQUIRE(support_params.raft_flow(RaftPhase::Base).mm3_per_mm() == Approx(nominal_base_flow.mm3_per_mm()));
    REQUIRE(support_params.raft_flow(RaftPhase::Interface).mm3_per_mm() == Approx(nominal_interface_flow.mm3_per_mm()));
    REQUIRE(support_params.raft_flow(RaftPhase::Surface).mm3_per_mm() == Approx(nominal_surface_flow.mm3_per_mm()));
    REQUIRE(support_params.raft_flow_ratio(RaftPhase::Base) == Approx(1.05));
    REQUIRE(support_params.raft_flow_ratio(RaftPhase::Interface) == Approx(0.95));
    REQUIRE(support_params.raft_flow_ratio(RaftPhase::Surface) == Approx(1.5));
    REQUIRE(support_params.raft_pattern_spacing(RaftPhase::Base) / support_params.raft_density(RaftPhase::Base) == Approx(1.5));
    REQUIRE(support_params.raft_pattern_spacing(RaftPhase::Interface) / support_params.raft_density(RaftPhase::Interface) == Approx(1.5));
    REQUIRE(support_params.raft_pattern_spacing(RaftPhase::Surface) / support_params.raft_density(RaftPhase::Surface) == Approx(0.3));
    REQUIRE(support_params.raft_wall_count(RaftPhase::Base) == 4);
    REQUIRE(support_params.raft_wall_count(RaftPhase::Interface) == 0);
    REQUIRE(support_params.raft_wall_count(RaftPhase::Surface) == 0);
    REQUIRE(support_params.raft_fill_pattern(RaftPhase::Base) == ipRectilinear);
    REQUIRE(support_params.raft_fill_pattern(RaftPhase::Interface) == ipZigZag);
    REQUIRE(support_params.raft_fill_pattern(RaftPhase::Surface) == ipZigZag);
    REQUIRE(support_params.raft_layer_angle(0) == Approx(Geometry::deg2rad(110.f)));
    REQUIRE(support_params.raft_layer_angle(1) == Approx(Geometry::deg2rad(20.f)));
    REQUIRE(support_params.raft_layer_angle(4) == Approx(Geometry::deg2rad(110.f)));

    // A non-directional model bottom has no meaningful perpendicular line.
    // Keep Cura's stable 135 degree Surface fallback instead of deriving a
    // pseudo-angle from the sparse infill direction or polygon geometry.
    DynamicPrintConfig non_directional_config = config;
    non_directional_config.set_key_value("bottom_surface_pattern", new ConfigOptionEnum<InfillPattern>(ipConcentric));
    Print non_directional_print;
    non_directional_print.auto_assign_extruders(model_object);
    non_directional_print.apply(model, non_directional_config);
    const StringObjectException non_directional_validation = non_directional_print.validate();
    INFO("non-directional validation error: " << non_directional_validation.string << "; key: " << non_directional_validation.opt_key);
    REQUIRE(non_directional_validation.string.empty());
    non_directional_print.set_status_silent();
    non_directional_print.process();
    const SupportParameters non_directional_support_params(*non_directional_print.objects().front());
    REQUIRE(non_directional_support_params.raft_layer_angle(0) == Approx(Geometry::deg2rad(135.f)));
    REQUIRE(non_directional_support_params.raft_layer_angle(1) == Approx(Geometry::deg2rad(45.f)));
    REQUIRE(non_directional_support_params.raft_layer_angle(4) == Approx(Geometry::deg2rad(135.f)));

    // Random template operators cannot be evaluated twice while preserving
    // the actual model Bottom direction because they consume global rand().
    // Treat them as non-unique and use the deterministic Surface fallback.
    DynamicPrintConfig randomized_direction_config = config;
    randomized_direction_config.set("solid_infill_rotate_template", std::string("90~1"));
    Print randomized_direction_print;
    randomized_direction_print.auto_assign_extruders(model_object);
    randomized_direction_print.apply(model, randomized_direction_config);
    const StringObjectException randomized_direction_validation = randomized_direction_print.validate();
    INFO("randomized direction validation error: " << randomized_direction_validation.string
                                                     << "; key: " << randomized_direction_validation.opt_key);
    REQUIRE(randomized_direction_validation.string.empty());
    randomized_direction_print.set_status_silent();
    randomized_direction_print.process();
    const SupportParameters randomized_direction_support_params(*randomized_direction_print.objects().front());
    REQUIRE(randomized_direction_support_params.raft_layer_angle(0) == Approx(Geometry::deg2rad(135.f)));
    REQUIRE(randomized_direction_support_params.raft_layer_angle(4) == Approx(Geometry::deg2rad(135.f)));

    const std::array<double, 5> expected_print_z {0.3, 0.6, 0.9, 1.1, 1.3};
    for (size_t layer_id = 0; layer_id < expected_print_z.size(); ++layer_id) {
        INFO("raft layer " << layer_id);
        REQUIRE(object->support_layers()[layer_id]->print_z == Approx(expected_print_z[layer_id]));
        REQUIRE_FALSE(object->support_layers()[layer_id]->support_fills.empty());
        const RaftPhase phase = object->slicing_parameters().raft_phase(layer_id);
        const double expected_mm3_per_mm =
            support_params.raft_flow(phase).mm3_per_mm() * support_params.raft_flow_ratio(phase);
        const double expected_width = support_params.raft_flow(phase).width();
        REQUIRE(object->support_layers()[layer_id]->support_fills.min_mm3_per_mm() == Approx(expected_mm3_per_mm));
        const ExtrusionEntityCollection flattened = object->support_layers()[layer_id]->support_fills.flatten();
        const auto check_path = [&](const ExtrusionPath &path) {
            REQUIRE(path.mm3_per_mm == Approx(expected_mm3_per_mm));
            REQUIRE(path.width == Approx(expected_width));
        };
        for (const ExtrusionEntity *entity : flattened.entities) {
            if (const auto *path = dynamic_cast<const ExtrusionPath *>(entity)) {
                check_path(*path);
            } else if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(entity)) {
                for (const ExtrusionPath &path : multipath->paths)
                    check_path(path);
            } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(entity)) {
                for (const ExtrusionPath &path : loop->paths)
                    check_path(path);
            }
        }
    }

    // The last Surface is a single-direction ZigZag with short end-piece
    // turns.  If it accidentally inherits the Grid support pattern, long
    // extrusions appear in two perpendicular directions.
    const ExtrusionEntityCollection final_surface = object->support_layers().back()->support_fills.flatten();
    CAPTURE(final_surface.entities.size());
    REQUIRE(final_surface.entities.size() == 1);
    std::optional<double> primary_long_angle;
    size_t long_segment_count = 0;
    size_t perpendicular_long_segment_count = 0;
    std::vector<double> hatch_offsets;
    const auto angle_distance = [](double lhs, double rhs) {
        double distance = std::fmod(std::abs(lhs - rhs), M_PI);
        return std::min(distance, M_PI - distance);
    };
    const auto inspect_polyline = [&](const Polyline &polyline) {
        for (size_t point_id = 1; point_id < polyline.points.size(); ++point_id) {
            const Point &from = polyline.points[point_id - 1];
            const Point &to = polyline.points[point_id];
            const double dx = unscale<double>(to.x() - from.x());
            const double dy = unscale<double>(to.y() - from.y());
            if (std::hypot(dx, dy) < 5.)
                continue;
            const double angle = std::atan2(dy, dx);
            if (!primary_long_angle)
                primary_long_angle = angle;
            ++long_segment_count;
            const double distance_from_primary = angle_distance(angle, *primary_long_angle);
            if (std::abs(distance_from_primary - M_PI_2) < Geometry::deg2rad(10.)) {
                ++perpendicular_long_segment_count;
            } else if (distance_from_primary < Geometry::deg2rad(10.)) {
                const double midpoint_x = 0.5 * unscale<double>(from.x() + to.x());
                const double midpoint_y = 0.5 * unscale<double>(from.y() + to.y());
                hatch_offsets.emplace_back(-midpoint_x * std::sin(*primary_long_angle) +
                                           midpoint_y * std::cos(*primary_long_angle));
            }
        }
    };
    for (const ExtrusionEntity *entity : final_surface.entities) {
        if (const auto *path = dynamic_cast<const ExtrusionPath *>(entity)) {
            inspect_polyline(path->polyline);
        } else if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(entity)) {
            for (const ExtrusionPath &path : multipath->paths)
                inspect_polyline(path.polyline);
        } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(entity)) {
            for (const ExtrusionPath &path : loop->paths)
                inspect_polyline(path.polyline);
        }
    }
    REQUIRE(long_segment_count > 2);
    REQUIRE(primary_long_angle.has_value());
    // Verify emitted geometry, not just the resolved plan values. The model
    // Bottom hatch follows its configured 20 degree direction, while the
    // model-contacting Raft Surface must emit at 110 degrees.
    size_t model_bottom_parallel_segments = 0;
    size_t model_bottom_perpendicular_segments = 0;
    const auto inspect_model_bottom_path = [&](const ExtrusionPath &path) {
        if (path.role() != erBottomSurface)
            return;
        for (size_t point_id = 1; point_id < path.polyline.points.size(); ++point_id) {
            const Point &from = path.polyline.points[point_id - 1];
            const Point &to = path.polyline.points[point_id];
            const double dx = unscale<double>(to.x() - from.x());
            const double dy = unscale<double>(to.y() - from.y());
            if (std::hypot(dx, dy) < 5.)
                continue;
            const double angle = std::atan2(dy, dx);
            if (angle_distance(angle, Geometry::deg2rad(20.)) < Geometry::deg2rad(5.))
                ++model_bottom_parallel_segments;
            if (angle_distance(angle, Geometry::deg2rad(110.)) < Geometry::deg2rad(5.))
                ++model_bottom_perpendicular_segments;
        }
    };
    const ExtrusionEntityCollection model_bottom = object->layers().front()->get_region(0)->fills.flatten();
    for (const ExtrusionEntity *entity : model_bottom.entities) {
        if (const auto *path = dynamic_cast<const ExtrusionPath *>(entity)) {
            inspect_model_bottom_path(*path);
        } else if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(entity)) {
            for (const ExtrusionPath &path : multipath->paths)
                inspect_model_bottom_path(path);
        } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(entity)) {
            for (const ExtrusionPath &path : loop->paths)
                inspect_model_bottom_path(path);
        }
    }
    REQUIRE(model_bottom_parallel_segments > 2);
    REQUIRE(model_bottom_parallel_segments > model_bottom_perpendicular_segments);
    REQUIRE(angle_distance(*primary_long_angle, Geometry::deg2rad(110.)) < Geometry::deg2rad(5.));
    REQUIRE(std::abs(angle_distance(*primary_long_angle, Geometry::deg2rad(20.)) - M_PI_2) < Geometry::deg2rad(5.));
    REQUIRE(perpendicular_long_segment_count == 0);
    std::sort(hatch_offsets.begin(), hatch_offsets.end());
    double minimum_hatch_spacing = std::numeric_limits<double>::max();
    for (size_t offset_id = 1; offset_id < hatch_offsets.size(); ++offset_id) {
        const double spacing = hatch_offsets[offset_id] - hatch_offsets[offset_id - 1];
        if (spacing > 0.05)
            minimum_hatch_spacing = std::min(minimum_hatch_spacing, spacing);
    }
    REQUIRE(minimum_hatch_spacing == Approx(0.3).margin(0.03));
    REQUIRE(object->layers().size() >= 2);
    REQUIRE(object->layers()[0]->id() == 5);
    REQUIRE(object->layers()[0]->height == Approx(0.28));
    REQUIRE(object->layers()[0]->print_z == Approx(1.85));
    REQUIRE(object->layers()[1]->height == Approx(0.2));
    REQUIRE(object->layers()[1]->print_z == Approx(1.95));
    // Overlap changes print Z only; the mesh must still be sliced through its
    // full 20 mm height instead of cropping the top by the overlap amount.
    REQUIRE(object->layers().back()->slice_z + 0.5 * object->layers().back()->height >= 20.0 - EPSILON);
    REQUIRE(object->layers()[0]->get_region(0)->flow(frPerimeter).width() == Approx(0.58));
    REQUIRE(object->layers()[1]->get_region(0)->flow(frPerimeter).width() == Approx(0.42));
    REQUIRE(print.skirt_first_layer_height() == Approx(0.3));
    REQUIRE(print.skirt_flow().height() == Approx(0.3));
    REQUIRE(print.brim_flow().height() == Approx(0.3));

    const boost::filesystem::path gcode_path =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orca-raft-%%%%-%%%%.gcode");
    set_resources_dir((boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path() / "resources").string());
    GCodeProcessorResult gcode_result;
    print.export_gcode(gcode_path.string(), &gcode_result, nullptr);
    for (const PrintStateBase::Warning &warning : print.step_state_with_warnings(psGCodeExport).warnings)
        REQUIRE(warning.message_id != PrintStateBase::SlicingEmptyGcodeLayers);
    std::ifstream gcode_stream(gcode_path.string());
    const std::string gcode((std::istreambuf_iterator<char>(gcode_stream)), std::istreambuf_iterator<char>());
    REQUIRE(gcode.find("; FIRST_PRINT_LAYER_HEIGHT: 0.300") != std::string::npos);
    const auto first_start_gcode_extrusion = std::find_if(
        gcode_result.moves.begin(), gcode_result.moves.end(), [](const GCodeProcessorResult::MoveVertex &move) {
            return move.type == EMoveType::Extrude && move.extrusion_role == erCustom;
        });
    REQUIRE(first_start_gcode_extrusion != gcode_result.moves.end());
    REQUIRE(first_start_gcode_extrusion->position.z() == Approx(0.3));
    const auto layer_marker_position = [&gcode](double print_z, size_t offset = 0) {
        std::ostringstream compact_marker;
        compact_marker << ";Z:" << print_z;
        size_t position = gcode.find(compact_marker.str(), offset);
        if (position == std::string::npos) {
            std::ostringstream verbose_marker;
            verbose_marker << "; Z_HEIGHT: " << print_z;
            position = gcode.find(verbose_marker.str(), offset);
        }
        return position;
    };
    const auto layer_section = [&gcode, &layer_marker_position](double print_z, double next_print_z) {
        const size_t begin = layer_marker_position(print_z);
        REQUIRE(begin != std::string::npos);
        const size_t end = layer_marker_position(next_print_z, begin + 1);
        REQUIRE(end != std::string::npos);
        return gcode.substr(begin, end - begin);
    };
    const std::string base_gcode = layer_section(0.3, 0.6);
    const std::string interface_gcode = layer_section(0.6, 0.9);
    const std::string surface_gcode = layer_section(1.1, 1.3);
    const std::string first_model_layer_gcode = layer_section(1.85, 1.95);
    const auto fan_command_count = [](const std::string &section) {
        size_t count = 0;
        for (size_t position = 0; (position = section.find("M106 S", position)) != std::string::npos; ++count)
            position += 6;
        return count;
    };
    REQUIRE(base_gcode.find("F600") != std::string::npos);
    REQUIRE(interface_gcode.find("F1500") != std::string::npos);
    REQUIRE(surface_gcode.find("F3600") != std::string::npos);
    REQUIRE(base_gcode.find("M204 P600") != std::string::npos);
    REQUIRE(interface_gcode.find("M204 P2000") != std::string::npos);
    REQUIRE(surface_gcode.find("M204 P800") != std::string::npos);
    REQUIRE(interface_gcode.find("M106 S51") != std::string::npos);
    REQUIRE(surface_gcode.find("M106 S102") != std::string::npos);
    REQUIRE(fan_command_count(base_gcode) == 1);
    REQUIRE(fan_command_count(interface_gcode) == 1);
    REQUIRE(fan_command_count(surface_gcode) == 1);
    // CoolingBuffer prepends the automatic fan command before the layer
    // marker. The post-raft model layer must therefore reset the Surface fan
    // to the model-local layer-0 value between the last raft marker and the
    // first model marker, instead of treating physical layer 5 as fully cooled.
    const size_t first_model_marker = layer_marker_position(1.85);
    const size_t last_raft_marker = layer_marker_position(1.3);
    const size_t model_fan_reset = gcode.rfind("M106 S0", first_model_marker);
    REQUIRE(model_fan_reset != std::string::npos);
    REQUIRE(model_fan_reset > last_raft_marker);
    REQUIRE(first_model_layer_gcode.find("F420") != std::string::npos);
    REQUIRE(first_model_layer_gcode.find("F660") != std::string::npos);
    REQUIRE(first_model_layer_gcode.find("F180") != std::string::npos);
    REQUIRE(first_model_layer_gcode.find("M204 P300") != std::string::npos);
    boost::filesystem::remove(gcode_path);

    const auto export_reapplied_config = [&](const DynamicPrintConfig &updated_config) {
        Print updated_print;
        updated_print.auto_assign_extruders(model_object);
        updated_print.apply(model, updated_config);
        const StringObjectException updated_validation = updated_print.validate();
        INFO("updated validation error: " << updated_validation.string << "; key: " << updated_validation.opt_key);
        REQUIRE(updated_validation.string.empty());
        updated_print.set_status_silent();
        updated_print.process();

        const boost::filesystem::path updated_gcode_path =
            boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orca-raft-motion-%%%%-%%%%.gcode");
        GCodeProcessorResult updated_result;
        updated_print.export_gcode(updated_gcode_path.string(), &updated_result, nullptr);
        std::ifstream updated_stream(updated_gcode_path.string());
        const std::string updated_gcode((std::istreambuf_iterator<char>(updated_stream)), std::istreambuf_iterator<char>());
        boost::filesystem::remove(updated_gcode_path);
        return updated_gcode;
    };

    // Zero means inherit the existing initial/default acceleration schedule,
    // not a literal M204 P0 and not the Cura reference values.
    DynamicPrintConfig inherited_acceleration_config = config;
    inherited_acceleration_config.set("raft_base_acceleration", 0.);
    inherited_acceleration_config.set("raft_interface_acceleration", 0.);
    inherited_acceleration_config.set("raft_surface_acceleration", 0.);
    const std::string inherited_acceleration_gcode = export_reapplied_config(inherited_acceleration_config);
    REQUIRE(inherited_acceleration_gcode.find("M204 P300") != std::string::npos);
    REQUIRE(inherited_acceleration_gcode.find("M204 P500") != std::string::npos);
    REQUIRE(inherited_acceleration_gcode.find("M204 P600") == std::string::npos);
    REQUIRE(inherited_acceleration_gcode.find("M204 P2000") == std::string::npos);
    REQUIRE(inherited_acceleration_gcode.find("M204 P800") == std::string::npos);
    REQUIRE(inherited_acceleration_gcode.find("M204 P0") == std::string::npos);

    // A zero normal acceleration delegates motion control to the firmware. In
    // that mode a phase override is intentionally suppressed because standard
    // G-code cannot restore the unknown firmware acceleration after the Raft.
    DynamicPrintConfig firmware_acceleration_config = config;
    firmware_acceleration_config.set("default_acceleration", 0.);
    const std::string firmware_acceleration_gcode = export_reapplied_config(firmware_acceleration_config);
    REQUIRE(firmware_acceleration_gcode.find("M204 P600") == std::string::npos);
    REQUIRE(firmware_acceleration_gcode.find("M204 P2000") == std::string::npos);
    REQUIRE(firmware_acceleration_gcode.find("M204 P800") == std::string::npos);

    // Dormant CuraV1 values remain saveable while Legacy Raft keeps its
    // existing initial/default acceleration behavior.
    DynamicPrintConfig legacy_acceleration_config = config;
    legacy_acceleration_config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    legacy_acceleration_config.set("raft_layers", 3);
    const std::string legacy_acceleration_gcode = export_reapplied_config(legacy_acceleration_config);
    REQUIRE(legacy_acceleration_gcode.find("M204 P300") != std::string::npos);
    REQUIRE(legacy_acceleration_gcode.find("M204 P500") != std::string::npos);
    REQUIRE(legacy_acceleration_gcode.find("M204 P600") == std::string::npos);
    REQUIRE(legacy_acceleration_gcode.find("M204 P2000") == std::string::npos);
    REQUIRE(legacy_acceleration_gcode.find("M204 P800") == std::string::npos);

    // Region-only changes do not carry raft_mode in their old/new resolvers.
    // Updating the model L1 direction on an existing Print must nevertheless
    // invalidate and regenerate its CuraV1 raft instead of retaining stale
    // Surface toolpaths until the project is reopened.
    REQUIRE(print.is_step_done(posSupportMaterial));
    DynamicPrintConfig updated_direction_config = config;
    updated_direction_config.set("solid_infill_direction", 35.);
    print.apply(model, updated_direction_config);
    REQUIRE_FALSE(print.is_step_done(posSupportMaterial));
    print.set_status_silent();
    print.process();
    const SupportParameters updated_direction_support_params(*print.objects().front());
    REQUIRE(updated_direction_support_params.raft_layer_angle(0) == Approx(Geometry::deg2rad(125.f)));
    REQUIRE(updated_direction_support_params.raft_layer_angle(4) == Approx(Geometry::deg2rad(125.f)));

    // A region can still touch L1 when bottom shells are disabled, but it no
    // longer emits a model Bottom hatch. The Raft direction must then use the
    // stable fallback, and the same-Print update must invalidate the existing
    // support toolpaths.
    REQUIRE(print.is_step_done(posSupportMaterial));
    DynamicPrintConfig no_bottom_config = updated_direction_config;
    no_bottom_config.set("bottom_shell_layers", 0);
    print.apply(model, no_bottom_config);
    REQUIRE_FALSE(print.is_step_done(posSupportMaterial));
    print.set_status_silent();
    print.process();
    const SupportParameters no_bottom_support_params(*print.objects().front());
    REQUIRE(no_bottom_support_params.raft_layer_angle(0) == Approx(Geometry::deg2rad(135.f)));
    REQUIRE(no_bottom_support_params.raft_layer_angle(4) == Approx(Geometry::deg2rad(135.f)));
}

TEST_CASE("Cura V1 raft reaches organic tree support layers", "[SupportMaterial][Raft][CuraV1][TreeSupport]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.3);
    config.set("raft_airgap", 0.27);
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("enable_support", true);
    config.set_key_value("raft_base_flow", new ConfigOptionPercent(105.));
    config.set_key_value("raft_interface_flow", new ConfigOptionPercent(95.));
    config.set_key_value("raft_surface_flow", new ConfigOptionPercent(80.));
    config.set("raft_interface_margin", 1.3);
    config.set("raft_surface_margin", 1.0);
    config.set("raft_interface_line_width", 0.72);
    config.set("raft_interface_line_spacing", 0.72);
    config.set("raft_surface_line_width", 0.72);
    config.set("raft_surface_line_spacing", 0.72);
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTreeAuto));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsTreeOrganic));
    config.set_key_value("support_line_width", new ConfigOptionFloatOrPercent(0.44, false));
    config.set_key_value("bottom_surface_pattern", new ConfigOptionEnum<InfillPattern>(ipMonotonic));
    config.set("solid_infill_rotate_template", std::string("30,120"));
    config.set("align_infill_direction_to_model", true);
    config.set("support_threshold_angle", 30);
    config.set("support_top_z_distance", 0.2);
    config.set("before_layer_change_gcode", std::string("G92 E0"));

    TriangleMesh tilted_cube = Slic3r::Test::mesh(TestMesh::cube_20x20x20);
    tilted_cube.rotate_x(float(Geometry::deg2rad(15.)));
    Model model;
    ModelObject *model_object = model.add_object();
    model_object->name = "Cura-style raft organic tree cube";
    model_object->add_volume(std::move(tilted_cube));
    ModelInstance *instance = model_object->add_instance();
    instance->set_offset(Vec3d(100., 100., 0.));
    instance->set_rotation(Z, Geometry::deg2rad(20.));
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);
    const StringObjectException validation = print.validate();
    INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
    REQUIRE(validation.string.empty());
    print.set_status_silent();
    print.process();

    const PrintObject *object = print.objects().front();
    const SupportParameters support_params(*object);
    REQUIRE(object->slicing_parameters().raft_layers() == 5);
    REQUIRE(object->support_layers().size() > 5);
    REQUIRE(support_params.raft_fill_pattern(RaftPhase::Base) == ipRectilinear);
    REQUIRE(support_params.raft_fill_pattern(RaftPhase::Interface) == ipZigZag);
    REQUIRE(support_params.raft_fill_pattern(RaftPhase::Surface) == ipZigZag);
    REQUIRE(support_params.raft_surface_flow.width() == Approx(0.72));
    REQUIRE(support_params.support_material_flow.width() == Approx(0.44));
    REQUIRE(support_params.support_material_interface_flow.width() == Approx(0.44));
    REQUIRE(support_params.raft_layer_angle(0) == Approx(Geometry::deg2rad(140.f)));
    REQUIRE(support_params.raft_layer_angle(1) == Approx(Geometry::deg2rad(50.f)));
    REQUIRE(support_params.raft_layer_angle(4) == Approx(Geometry::deg2rad(140.f)));
    const std::array<double, 5> expected_print_z {0.3, 0.6, 0.9, 1.1, 1.3};
    for (size_t layer_id = 0; layer_id < expected_print_z.size(); ++layer_id) {
        INFO("tree raft layer " << layer_id);
        REQUIRE(object->support_layers()[layer_id]->print_z == Approx(expected_print_z[layer_id]));
        REQUIRE(object->support_layers()[layer_id]->has_extrusions());
        const RaftPhase phase = object->slicing_parameters().raft_phase(layer_id);
        const double expected_mm3_per_mm =
            support_params.raft_flow(phase).mm3_per_mm() * support_params.raft_flow_ratio(phase);
        const double expected_width = support_params.raft_flow(phase).width();
        bool found_phase_path = false;
        const ExtrusionEntityCollection flattened = object->support_layers()[layer_id]->support_fills.flatten();
        const auto check_path = [&](const ExtrusionPath &path) {
            if (path.mm3_per_mm == Approx(expected_mm3_per_mm) && path.width == Approx(expected_width))
                found_phase_path = true;
        };
        for (const ExtrusionEntity *entity : flattened.entities) {
            if (const auto *path = dynamic_cast<const ExtrusionPath *>(entity)) {
                check_path(*path);
            } else if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(entity)) {
                for (const ExtrusionPath &path : multipath->paths)
                    check_path(path);
            } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(entity)) {
                for (const ExtrusionPath &path : loop->paths)
                    check_path(path);
            }
        }
        REQUIRE(found_phase_path);
    }

    // Interface layer 1 and Surface layer 1 use the same direction and line
    // spacing but deliberately have different margins. Their hatch lattice
    // must still share one raft-wide origin instead of shifting at the phase
    // boundary.
    const auto hatch_offsets = [&](size_t layer_id, std::optional<double> reference_angle = std::nullopt) {
        std::vector<double> offsets;
        const ExtrusionEntityCollection flattened = object->support_layers()[layer_id]->support_fills.flatten();
        const auto inspect_polyline = [&](const Polyline &polyline) {
            for (size_t point_id = 1; point_id < polyline.points.size(); ++point_id) {
                const Point &from = polyline.points[point_id - 1];
                const Point &to = polyline.points[point_id];
                const double dx = unscale<double>(to.x() - from.x());
                const double dy = unscale<double>(to.y() - from.y());
                if (std::hypot(dx, dy) < 5.)
                    continue;
                const double angle = std::atan2(dy, dx);
                if (!reference_angle)
                    reference_angle = angle;
                double angle_delta = std::fmod(std::abs(angle - *reference_angle), M_PI);
                angle_delta = std::min(angle_delta, M_PI - angle_delta);
                if (angle_delta >= Geometry::deg2rad(10.))
                    continue;
                const double midpoint_x = 0.5 * unscale<double>(from.x() + to.x());
                const double midpoint_y = 0.5 * unscale<double>(from.y() + to.y());
                offsets.emplace_back(-midpoint_x * std::sin(*reference_angle) +
                                     midpoint_y * std::cos(*reference_angle));
            }
        };
        for (const ExtrusionEntity *entity : flattened.entities) {
            if (const auto *path = dynamic_cast<const ExtrusionPath *>(entity)) {
                inspect_polyline(path->polyline);
            } else if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(entity)) {
                for (const ExtrusionPath &path : multipath->paths)
                    inspect_polyline(path.polyline);
            } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(entity)) {
                for (const ExtrusionPath &path : loop->paths)
                    inspect_polyline(path.polyline);
            }
        }
        std::sort(offsets.begin(), offsets.end());
        return std::make_pair(std::move(offsets), reference_angle);
    };
    const auto interface_hatch = hatch_offsets(1);
    REQUIRE(interface_hatch.second.has_value());
    const auto surface_hatch = hatch_offsets(3, interface_hatch.second);
    REQUIRE_FALSE(interface_hatch.first.empty());
    REQUIRE_FALSE(surface_hatch.first.empty());
    REQUIRE(std::abs(std::remainder(surface_hatch.first.front() - interface_hatch.first.front(), 0.72)) < 0.03);

    const double surface_mm3_per_mm =
        support_params.raft_surface_flow.mm3_per_mm() * support_params.raft_flow_ratio(RaftPhase::Surface);
    bool found_ordinary_tree_support_path = false;
    for (size_t layer_id = object->slicing_parameters().raft_layers(); layer_id < object->support_layers().size(); ++layer_id) {
        const ExtrusionEntityCollection flattened = object->support_layers()[layer_id]->support_fills.flatten();
        const auto check_path = [&](const ExtrusionPath &path) {
            if (path.role() != erSupportMaterial && path.role() != erSupportMaterialInterface)
                return;
            found_ordinary_tree_support_path = true;
            CAPTURE(layer_id, path.width, path.height, path.mm3_per_mm);
            REQUIRE_FALSE(path.width == Approx(support_params.raft_surface_flow.width()));
            REQUIRE_FALSE(path.mm3_per_mm == Approx(surface_mm3_per_mm));
        };
        for (const ExtrusionEntity *entity : flattened.entities) {
            if (const auto *path = dynamic_cast<const ExtrusionPath *>(entity)) {
                check_path(*path);
            } else if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(entity)) {
                for (const ExtrusionPath &path : multipath->paths)
                    check_path(path);
            } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(entity)) {
                for (const ExtrusionPath &path : loop->paths)
                    check_path(path);
            }
        }
    }
    REQUIRE(found_ordinary_tree_support_path);
}

TEST_CASE("Cura V1 raft keeps normal Grid and Snug support controls independent", "[SupportMaterial][Raft][CuraV1][NormalSupport]")
{
    for (const SupportMaterialStyle support_style : {smsGrid, smsSnug}) {
        CAPTURE(support_style);

        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
        config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
        config.set("layer_height", 0.2);
        config.set("initial_layer_print_height", 0.28);
        config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.58, false));
        config.set("raft_airgap", 0.27);
        config.set("raft_base_layers", 1);
        config.set("raft_interface_layers", 2);
        config.set("raft_surface_layers", 2);
        config.set("raft_base_layer_height", 0.3);
        config.set("raft_interface_layer_height", 0.3);
        config.set("raft_surface_layer_height", 0.2);
        config.set("raft_base_line_width", 0.6);
        config.set("raft_interface_line_width", 0.6);
        config.set("raft_surface_line_width", 0.72);
        config.set("raft_base_line_spacing", 1.5);
        config.set("raft_interface_line_spacing", 1.5);
        config.set("raft_surface_line_spacing", 0.72);
        config.set("enable_support", true);
        config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stNormalAuto));
        config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(support_style));
        config.set_key_value("support_line_width", new ConfigOptionFloatOrPercent(0.44, false));
        config.set("support_threshold_angle", 30);
        config.set("support_top_z_distance", 0.4);
        config.set("independent_support_layer_height", true);
        config.set("before_layer_change_gcode", "G92 E0");

        TriangleMesh tilted_cube = Slic3r::Test::mesh(TestMesh::cube_20x20x20);
        tilted_cube.rotate_x(float(Geometry::deg2rad(15.)));
        Model model;
        ModelObject *model_object = model.add_object();
        model_object->name = support_style == smsGrid ? "Cura-style raft Grid support cube" : "Cura-style raft Snug support cube";
        model_object->add_volume(std::move(tilted_cube));
        ModelInstance *instance = model_object->add_instance();
        instance->set_offset(Vec3d(100., 100., 0.));
        model_object->ensure_on_bed();

        Print print;
        print.auto_assign_extruders(model_object);
        print.apply(model, config);
        const StringObjectException validation = print.validate();
        INFO("validation error: " << validation.string << "; key: " << validation.opt_key);
        REQUIRE(validation.string.empty());
        print.set_status_silent();
        print.process();

        const PrintObject *object = print.objects().front();
        const SlicingParameters &slicing_params = object->slicing_parameters();
        const SupportParameters support_params(*object);
        REQUIRE(slicing_params.raft_layers() == 5);
        REQUIRE(slicing_params.first_print_layer_height == Approx(0.3));
        REQUIRE(slicing_params.first_object_layer_height == Approx(0.28));
        REQUIRE(slicing_params.gap_raft_object == Approx(0.27));
        REQUIRE(support_params.support_style == support_style);
        REQUIRE(support_params.raft_surface_flow.width() == Approx(0.72));
        REQUIRE(support_params.support_material_flow.width() == Approx(0.44));
        REQUIRE(support_params.support_material_interface_flow.width() == Approx(0.44));
        REQUIRE(object->support_layers().size() > slicing_params.raft_layers());
        REQUIRE_FALSE(object->layers().empty());
        REQUIRE(object->layers().front()->id() == slicing_params.raft_layers());
        REQUIRE(object->layers().front()->height == Approx(0.28));
        REQUIRE(object->layers().front()->get_region(0)->flow(frPerimeter).width() == Approx(0.58));

        bool found_normal_support_path = false;
        for (size_t layer_id = slicing_params.raft_layers(); layer_id < object->support_layers().size(); ++layer_id) {
            const ExtrusionEntityCollection flattened = object->support_layers()[layer_id]->support_fills.flatten();
            const auto check_path = [&](const ExtrusionPath &path) {
                if (path.role() != erSupportMaterial && path.role() != erSupportMaterialInterface)
                    return;
                found_normal_support_path = true;
                CAPTURE(layer_id, path.width, path.height, path.mm3_per_mm);
                REQUIRE_FALSE(path.width == Approx(support_params.raft_surface_flow.width()));
            };
            for (const ExtrusionEntity *entity : flattened.entities) {
                if (const auto *path = dynamic_cast<const ExtrusionPath *>(entity)) {
                    check_path(*path);
                } else if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(entity)) {
                    for (const ExtrusionPath &path : multipath->paths)
                        check_path(path);
                } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(entity)) {
                    for (const ExtrusionPath &path : loop->paths)
                        check_path(path);
                }
            }
        }
        REQUIRE(found_normal_support_path);
    }
}

TEST_CASE("Tree support prints a tilted object without an empty initial layer", "[SupportMaterial][TreeSupport]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_support", true);
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTreeAuto));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsTreeOrganic));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.2);
    config.set("independent_support_layer_height", true);
    config.set("support_threshold_angle", 30);
    config.set("support_remove_small_overhang", true);
    config.set("support_top_z_distance", 0.2);

    for (int top_interface_layers : {0, 2}) {
        config.set("support_interface_top_layers", top_interface_layers);
        for (double z_rotation : {0.0, 37.0, 122.0}) {
            INFO("Top interface layers: " << top_interface_layers << ", Z rotation: " << z_rotation);
            TriangleMesh tilted_cube = Slic3r::Test::mesh(TestMesh::cube_20x20x20);
            tilted_cube.rotate_x(float(Geometry::deg2rad(15.0)));
            tilted_cube.rotate_z(float(Geometry::deg2rad(z_rotation)));

            Model model;
            ModelObject *model_object = model.add_object();
            model_object->name = "tilted tree support cube";
            model_object->add_volume(std::move(tilted_cube));
            ModelInstance *instance = model_object->add_instance();
            instance->set_offset(Vec3d(100.0, 100.0, 0.0));
            model_object->ensure_on_bed();

            Print print;
            print.auto_assign_extruders(model_object);
            print.apply(model, config);
            print.validate();
            print.set_status_silent();
            print.process();

            ConstSupportLayerPtrsAdaptor support_layers = print.objects().front()->support_layers();
            REQUIRE_FALSE(support_layers.empty());
            REQUIRE(support_layers.front()->print_z == Approx(0.2));
            REQUIRE(support_layers.front()->has_extrusions());
            REQUIRE(std::any_of(support_layers.begin(), support_layers.end(), [](const SupportLayer *layer) {
                return layer->height > EPSILON && layer->has_extrusions();
            }));
            REQUIRE((print.objects().front()->layers().front()->has_extrusions() ||
                     print.objects().front()->support_layers().front()->has_extrusions()));
        }
    }
}

TEST_CASE("Tree support replaces an unextrudable object first layer with a build plate contact", "[SupportMaterial][TreeSupport]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_support", true);
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTreeAuto));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsDefault));
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.36);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.8, false));
    config.set_key_value("line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set("independent_support_layer_height", true);
    config.set("support_threshold_angle", 30);
    config.set("support_remove_small_overhang", true);
    config.set("support_top_z_distance", 0.2);
    config.set_key_value("wall_generator", new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));
    config.set_key_value("initial_layer_min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_feature_size", new ConfigOptionPercent(75.0));
    config.set("min_length_factor", 2.0);

    const std::array<std::array<double, 9>, 9> orientations{{
        {{0.932623355, 0., 0.360851323, 0., 1., 0., -0.360851323, 0., 0.932623355}},
        {{1.11022302e-16, 0.939692621, 0.342020143, -1., 1.11022302e-16, -2.55708605e-33, -5.55111512e-17, -0.342020143, 0.939692621}},
        {{-6.72514462e-33, 0.307348196, 0.951597124, -1., 4.71364137e-34, -5.55111512e-17, -5.55111512e-17, -0.951597124, 0.307348196}},
        {{-6.58978231e-16, -0.939692621, 0.342020143, 0.711141528, -0.240456878, -0.660649844, 0.703048879, 0.243224727, 0.668254447}},
        {{-0.516378815, -0.804648755, 0.29307559, 0.580505767, -0.580505747, -0.57098698, 0.629576027, -0.12471351, 0.766864112}},
        {{-0.707106781, -0.707106781, 1.11022302e-16, 0.498428932, -0.498428932, -0.709321647, 0.501566147, -0.501566147, 0.704884956}},
        {{-0.857894124, -0.513826501, -1.11022302e-16, 0.368441802, -0.615157171, -0.69701671, 0.358145657, -0.597966539, 0.717054884}},
        {{-0.707106781, -0.707106781, 5.55111512e-17, 0.491682622, -0.491682622, -0.718676838, 0.508181266, -0.508181266, 0.695344233}},
        {{1., 0., 0., 0., 1., 0., 0., 0., 1.}},
    }};

    for (size_t orientation_idx = 0; orientation_idx < orientations.size(); ++orientation_idx) {
        INFO("Orientation index: " << orientation_idx);
        TriangleMesh tilted_cube = make_cube(10.1, 10.1, 10.1);
        Transform3d  transform    = Transform3d::Identity();
        for (size_t row = 0; row < 3; ++row)
            for (size_t column = 0; column < 3; ++column)
                transform.linear()(row, column) = orientations[orientation_idx][3 * row + column];
        tilted_cube.transform(transform);

        Model model;
        ModelObject *model_object = model.add_object();
        model_object->name = "cube resting on an unextrudable edge";
        model_object->add_volume(std::move(tilted_cube));
        ModelInstance *instance = model_object->add_instance();
        instance->set_offset(Vec3d(100.0, 100.0, 0.0));
        model_object->ensure_on_bed();

        Print print;
        print.auto_assign_extruders(model_object);
        print.apply(model, config);
        print.validate();
        print.set_status_silent();
        print.process();

        const PrintObject *object = print.objects().front();
        REQUIRE_FALSE(object->layers().empty());
        if (!object->layers().front()->has_extrusions()) {
            REQUIRE_FALSE(object->support_layers().empty());
            REQUIRE(object->support_layers().front()->print_z == Approx(0.36));
            REQUIRE(object->support_layers().front()->has_extrusions());
        }
        REQUIRE((object->layers().front()->has_extrusions() ||
                 (!object->support_layers().empty() && object->support_layers().front()->has_extrusions())));
    }
}

TEST_CASE("Normal support does not treat an empty extrusion layer as bearing geometry", "[SupportMaterial][NormalSupport]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_support", true);
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stNormalAuto));
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.36);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.8, false));
    config.set_key_value("line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set_key_value("outer_wall_line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set_key_value("inner_wall_line_width", new ConfigOptionFloatOrPercent(0.45, false));
    config.set("independent_support_layer_height", true);
    config.set("support_threshold_angle", 30);
    config.set("support_remove_small_overhang", true);
    config.set("support_top_z_distance", 0.2);
    config.set_key_value("wall_generator", new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));
    config.set_key_value("initial_layer_min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_feature_size", new ConfigOptionPercent(75.0));
    config.set("min_length_factor", 2.0);
    config.set("wall_loops", 2);
    config.set_key_value("sparse_infill_density", new ConfigOptionPercent(15.0));

    const std::array<std::array<double, 9>, 9> orientations{{
        {{0.932623355, 0., 0.360851323, 0., 1., 0., -0.360851323, 0., 0.932623355}},
        {{1.11022302e-16, 0.939692621, 0.342020143, -1., 1.11022302e-16, -2.55708605e-33, -5.55111512e-17, -0.342020143, 0.939692621}},
        {{-6.72514462e-33, 0.307348196, 0.951597124, -1., 4.71364137e-34, -5.55111512e-17, -5.55111512e-17, -0.951597124, 0.307348196}},
        {{-6.58978231e-16, -0.939692621, 0.342020143, 0.711141528, -0.240456878, -0.660649844, 0.703048879, 0.243224727, 0.668254447}},
        {{-0.516378815, -0.804648755, 0.29307559, 0.580505767, -0.580505747, -0.57098698, 0.629576027, -0.12471351, 0.766864112}},
        {{-0.707106781, -0.707106781, 1.11022302e-16, 0.498428932, -0.498428932, -0.709321647, 0.501566147, -0.501566147, 0.704884956}},
        {{-0.857894124, -0.513826501, -1.11022302e-16, 0.368441802, -0.615157171, -0.69701671, 0.358145657, -0.597966539, 0.717054884}},
        {{-0.707106781, -0.707106781, 5.55111512e-17, 0.491682622, -0.491682622, -0.718676838, 0.508181266, -0.508181266, 0.695344233}},
        {{1., 0., 0., 0., 1., 0., 0., 0., 1.}},
    }};

    for (size_t orientation_idx = 0; orientation_idx < orientations.size(); ++orientation_idx) {
        TriangleMesh tilted_cube = make_cube(10.1, 10.1, 10.1);
        Transform3d  transform    = Transform3d::Identity();
        for (size_t row = 0; row < 3; ++row)
            for (size_t column = 0; column < 3; ++column)
                transform.linear()(row, column) = orientations[orientation_idx][3 * row + column];
        tilted_cube.transform(transform);

        Model model;
        ModelObject *model_object = model.add_object();
        model_object->name = "tilted cube";
        model_object->add_volume(std::move(tilted_cube));
        model_object->add_instance();
        model_object->ensure_on_bed();

        Print print;
        print.auto_assign_extruders(model_object);
        print.apply(model, config);
        print.validate();
        print.set_status_silent();
        print.process();

        const PrintObject *object = print.objects().front();
        INFO("Orientation index: " << orientation_idx);
        REQUIRE_FALSE(object->layers().empty());
        INFO("Object first layer has extrusions: " << object->layers().front()->has_extrusions());
        INFO("Support layer count: " << object->support_layers().size());
        if (!object->layers().front()->has_extrusions()) {
            ConstSupportLayerPtrsAdaptor support_layers = object->support_layers();
            REQUIRE_FALSE(support_layers.empty());
            REQUIRE(support_layers.front()->has_extrusions());
        }
        REQUIRE((object->layers().front()->has_extrusions() ||
                 (!object->support_layers().empty() && object->support_layers().front()->has_extrusions())));
    }
}

TEST_CASE("Normal support uses extrusion coverage as the bearing surface", "[SupportMaterial][NormalSupport]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_support", true);
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stNormalAuto));
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.2);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set_key_value("line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set("independent_support_layer_height", true);
    config.set("support_threshold_angle", 15);
    config.set("support_remove_small_overhang", true);
    config.set("support_top_z_distance", 0.2);
    config.set("support_object_xy_distance", 0.1);
    config.set_key_value("wall_generator", new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));
    config.set_key_value("initial_layer_min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_feature_size", new ConfigOptionPercent(75.0));

    // The 0.1 mm ribs are geometric slices but are below the 0.3 mm minimum
    // feature size, so they produce no extrusion. Their 1.2 mm pitch is small
    // enough that expanding the raw geometry by the overhang threshold makes
    // the 10x10 mm platform above look fully supported.
    TriangleMesh mesh;
    for (size_t rib_idx = 0; rib_idx <= 10; ++rib_idx) {
        TriangleMesh rib = make_cube(12.0, 0.1, 1.0);
        rib.translate(0.0, float(rib_idx) * 1.2f, 0.0);
        mesh.merge(rib);
    }
    TriangleMesh platform = make_cube(10.0, 10.0, 10.0);
    platform.translate(1.0, 1.0, 1.0);
    mesh.merge(platform);

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->name = "platform over unextrudable ribs";
    model_object->add_volume(std::move(mesh));
    model_object->add_instance();
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    print.process();

    const PrintObject *object = print.objects().front();
    REQUIRE_FALSE(object->layers().empty());
    REQUIRE_FALSE(object->layers().front()->has_extrusions());
    REQUIRE_FALSE(object->support_layers().empty());
    REQUIRE(object->support_layers().front()->has_extrusions());
    REQUIRE(object->support_layers().front()->print_z == Approx(0.2));
}

SCENARIO("SupportMaterial: support_layers_z and contact_distance", "[SupportMaterial]")
{
    // Box h = 20mm, hole bottom at 5mm, hole height 10mm (top edge at 15mm).
    TriangleMesh mesh = Slic3r::Test::mesh(Slic3r::Test::TestMesh::cube_with_hole);
    mesh.rotate_x(float(M_PI / 2));
//    mesh.write_binary("d:\\temp\\cube_with_hole.stl");

    auto check = [](Slic3r::Print &print, bool &first_support_layer_height_ok, bool &layer_height_minimum_ok,
                    bool &layer_height_maximum_ok, bool &top_spacing_ok)
	{
        ConstSupportLayerPtrsAdaptor support_layers = print.objects().front()->support_layers();

		first_support_layer_height_ok = support_layers.front()->print_z == print.config().initial_layer_print_height.value;

		layer_height_minimum_ok = true;
		layer_height_maximum_ok = true;
		double min_layer_height = print.config().min_layer_height.values.front();
		double max_layer_height = print.config().nozzle_diameter.values.front();
		if (print.config().max_layer_height.values.front() > EPSILON)
			max_layer_height = std::min(max_layer_height, print.config().max_layer_height.values.front());
		for (size_t i = 1; i < support_layers.size(); ++ i) {
			if (support_layers[i]->print_z - support_layers[i - 1]->print_z < min_layer_height - EPSILON)
				layer_height_minimum_ok = false;
			if (support_layers[i]->print_z - support_layers[i - 1]->print_z > max_layer_height + EPSILON)
				layer_height_maximum_ok = false;
		}

#if 0
		double expected_top_spacing = print.default_object_config().layer_height + print.config().nozzle_diameter.get_at(0);
		bool wrong_top_spacing = 0;
        std::vector<coordf_t> top_z { 1.1 };
		for (coordf_t top_z_el : top_z) {
			// find layer index of this top surface.
			size_t layer_id = -1;
			for (size_t i = 0; i < support_z.size(); ++ i) {
				if (abs(support_z[i] - top_z_el) < EPSILON) {
					layer_id = i;
					i = static_cast<int>(support_z.size());
				}
			}

			// check that first support layer above this top surface (or the next one) is spaced with nozzle diameter
			if (abs(support_z[layer_id + 1] - support_z[layer_id] - expected_top_spacing) > EPSILON && 
				abs(support_z[layer_id + 2] - support_z[layer_id] - expected_top_spacing) > EPSILON) {
				wrong_top_spacing = 1;
			}
		}
		d = ! wrong_top_spacing;
#else
		top_spacing_ok = true;
#endif
	};

    GIVEN("A print object having one modelObject") {
        WHEN("First layer height = 0.4") {
			Slic3r::Print print;
			Slic3r::Test::init_and_process_print({ mesh }, print, {
				{ "support_material",	1 },
				{ "layer_height",		0.2 },
				{ "first_layer_height", 0.4 },
                { "dont_support_bridges", false },
			});
			bool a, b, c, d;
            check(print, a, b, c, d);
            THEN("First layer height is honored")					{ REQUIRE(a == true); }
            THEN("No null or negative support layers")				{ REQUIRE(b == true); }
            THEN("No layers thicker than nozzle diameter")			{ REQUIRE(c == true); }
//            THEN("Layers above top surfaces are spaced correctly")	{ REQUIRE(d == true); }
        }
        WHEN("Layer height = 0.2 and, first layer height = 0.3") {
			Slic3r::Print print;
			Slic3r::Test::init_and_process_print({ mesh }, print, {
				{ "support_material",	1 },
				{ "layer_height",		0.2 },
				{ "first_layer_height", 0.3 },
                { "dont_support_bridges", false },
            });
            bool a, b, c, d;
            check(print, a, b, c, d);
            THEN("First layer height is honored")					{ REQUIRE(a == true); }
            THEN("No null or negative support layers")				{ REQUIRE(b == true); }
            THEN("No layers thicker than nozzle diameter")			{ REQUIRE(c == true); }
//            THEN("Layers above top surfaces are spaced correctly")	{ REQUIRE(d == true); }
        }
        WHEN("Layer height = nozzle_diameter[0]") {
			Slic3r::Print print;
			Slic3r::Test::init_and_process_print({ mesh }, print, {
				{ "support_material",	1 },
				{ "layer_height",		0.2 },
				{ "first_layer_height", 0.3 },
                { "dont_support_bridges", false },
            });
            bool a, b, c, d;
            check(print, a, b, c, d);
            THEN("First layer height is honored")					{ REQUIRE(a == true); }
            THEN("No null or negative support layers")				{ REQUIRE(b == true); }
            THEN("No layers thicker than nozzle diameter")			{ REQUIRE(c == true); }
//            THEN("Layers above top surfaces are spaced correctly")	{ REQUIRE(d == true); }
        }
    }
}

#if 0
// Test 8.
TEST_CASE("SupportMaterial: forced support is generated", "[SupportMaterial]")
{
    // Create a mesh & modelObject.
    TriangleMesh mesh = TriangleMesh::make_cube(20, 20, 20);

    Model model = Model();
    ModelObject *object = model.add_object();
    object->add_volume(mesh);
    model.add_default_instances();
    model.align_instances_to_origin();

    Print print = Print();

    std::vector<coordf_t> contact_z = {1.9};
    std::vector<coordf_t> top_z = {1.1};
    print.default_object_config.support_material_enforce_layers = 100;
    print.default_object_config.support_material = 0;
    print.default_object_config.layer_height = 0.2;
    print.default_object_config.set_deserialize("first_layer_height", "0.3");

    print.add_model_object(model.objects[0]);
    print.objects.front()->_slice();

    SupportMaterial *support = print.objects.front()->_support_material();
    auto support_z = support->support_layers_z(contact_z, top_z, print.default_object_config.layer_height);

    bool check = true;
    for (size_t i = 1; i < support_z.size(); i++) {
        if (support_z[i] - support_z[i - 1] <= 0)
            check = false;
    }

    REQUIRE(check == true);
}

// TODO
bool test_6_checks(Print& print)
{
	bool has_bridge_speed = true;

	// Pre-Processing.
	PrintObject* print_object = print.objects.front();
	print_object->infill();
	SupportMaterial* support_material = print.objects.front()->_support_material();
	support_material->generate(print_object);
	// TODO but not needed in test 6 (make brims and make skirts).

	// Exporting gcode.
	// TODO validation found in Simple.pm


	return has_bridge_speed;
}

// Test 6.
SCENARIO("SupportMaterial: Checking bridge speed", "[SupportMaterial]")
{
    GIVEN("Print object") {
        // Create a mesh & modelObject.
        TriangleMesh mesh = TriangleMesh::make_cube(20, 20, 20);

        Model model = Model();
        ModelObject *object = model.add_object();
        object->add_volume(mesh);
        model.add_default_instances();
        model.align_instances_to_origin();

        Print print = Print();
        print.config.brim_width = 0;
        print.config.skirts = 0;
        print.config.skirts = 0;
        print.default_object_config.support_material = 1;
        print.default_region_config.top_solid_layers = 0; // so that we don't have the internal bridge over infill.
        print.default_region_config.bridge_speed = 99;
        print.config.cooling = 0;
        print.config.set_deserialize("first_layer_speed", "100%");

        WHEN("support_material_contact_distance = 0.2") {
            print.default_object_config.support_material_contact_distance = 0.2;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);
            REQUIRE(check == true); // bridge speed is used.
        }

        WHEN("support_material_contact_distance = 0") {
            print.default_object_config.support_material_contact_distance = 0;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);
            REQUIRE(check == true); // bridge speed is not used.
        }

        WHEN("support_material_contact_distance = 0.2 & raft_layers = 5") {
            print.default_object_config.support_material_contact_distance = 0.2;
            print.default_object_config.raft_layers = 5;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);
            REQUIRE(check == true); // bridge speed is used.
        }

        WHEN("support_material_contact_distance = 0 & raft_layers = 5") {
            print.default_object_config.support_material_contact_distance = 0;
            print.default_object_config.raft_layers = 5;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);

            REQUIRE(check == true); // bridge speed is not used.
        }
    }
}

#endif

TEST_CASE("Cura V1 raft air gap is independent from ordinary support Top Z distance", "[SupportMaterial][Raft][CuraV1][Airgap]")
{
    const ConfigOptionDef *airgap_def = print_config_def.get("raft_airgap");
    REQUIRE(airgap_def != nullptr);
    REQUIRE(airgap_def->mode == comAdvanced);
    REQUIRE(airgap_def->tooltip.find("independent of the ordinary support Top Z distance") != std::string::npos);

    PrintConfig print_config;
    print_config.nozzle_diameter.values                 = {0.4};
    print_config.min_layer_height.values                = {0.08};
    print_config.max_layer_height.values                = {0.32};
    print_config.initial_layer_print_height.value       = 0.42;
    print_config.independent_support_layer_height.value = false;

    PrintObjectConfig object_config;
    object_config.layer_height.value                = 0.2;
    object_config.raft_mode.value                   = RaftMode::CuraV1;
    object_config.raft_airgap.value                 = 0.27;
    object_config.raft_layer_0_z_overlap.value      = 0.1;
    object_config.raft_base_layers.value            = 1;
    object_config.raft_interface_layers.value       = 2;
    object_config.raft_surface_layers.value         = 2;
    object_config.raft_base_layer_height.value      = 0.3;
    object_config.raft_interface_layer_height.value = 0.3;
    object_config.raft_surface_layer_height.value   = 0.2;
    object_config.raft_base_line_width.value        = 0.6;
    object_config.raft_interface_line_width.value   = 0.6;
    object_config.raft_surface_line_width.value     = 0.42;
    object_config.support_filament.value            = 1;
    object_config.support_interface_filament.value  = 1;

    object_config.support_top_z_distance.value = 0.2;
    const SlicingParameters detachable =
        SlicingParameters::create_from_config(print_config, object_config, 10.1, {1}, Vec3d::Ones());
    REQUIRE(detachable.valid);
    REQUIRE_FALSE(detachable.soluble_interface);
    REQUIRE(detachable.gap_raft_object == Approx(0.27));

    object_config.support_top_z_distance.value = 0.;
    const SlicingParameters soluble =
        SlicingParameters::create_from_config(print_config, object_config, 10.1, {1}, Vec3d::Ones());
    REQUIRE(soluble.valid);
    REQUIRE(soluble.soluble_interface);
    REQUIRE(soluble.gap_raft_object == Approx(0.27));
    REQUIRE(soluble.object_print_z_min == Approx(1.57));
    REQUIRE(object_config.raft_airgap.value == Approx(0.27));

    object_config.support_top_z_distance.value = 0.2;
    object_config.raft_airgap.value = 0.;
    const SlicingParameters explicit_zero_gap =
        SlicingParameters::create_from_config(print_config, object_config, 10.1, {1}, Vec3d::Ones());
    REQUIRE(explicit_zero_gap.valid);
    REQUIRE_FALSE(explicit_zero_gap.soluble_interface);
    REQUIRE(explicit_zero_gap.gap_raft_object == Approx(0.));
    REQUIRE(explicit_zero_gap.object_print_z_min == Approx(1.3));

    object_config.raft_mode.value              = RaftMode::Legacy;
    object_config.raft_airgap.value            = 0.27;
    object_config.raft_layers.value            = 3;
    object_config.raft_contact_distance.value  = 0.4;
    object_config.support_top_z_distance.value = 0.2;
    const SlicingParameters legacy =
        SlicingParameters::create_from_config(print_config, object_config, 10.1, {1}, Vec3d::Ones());
    REQUIRE(legacy.valid);
    REQUIRE_FALSE(legacy.cura_raft_mode);
    REQUIRE(legacy.gap_raft_object == Approx(0.4));
    REQUIRE(object_config.raft_airgap.value == Approx(0.27));

    object_config.raft_mode.value = RaftMode::CuraV1;
    const SlicingParameters cura_again =
        SlicingParameters::create_from_config(print_config, object_config, 10.1, {1}, Vec3d::Ones());
    REQUIRE(cura_again.valid);
    REQUIRE(cura_again.cura_raft_mode);
    REQUIRE(cura_again.gap_raft_object == Approx(0.27));
}

TEST_CASE("Cura V1 exposes all dedicated Raft controls in Advanced mode", "[SupportMaterial][Raft][CuraV1][ConfigMode]")
{
    for (const char *key : {"raft_mode", "raft_airgap", "raft_layer_0_z_overlap", "raft_base_layers", "raft_interface_layers",
                            "raft_surface_layers", "raft_base_layer_height",
                            "raft_base_line_width", "raft_base_line_spacing", "raft_base_flow", "raft_base_speed",
                            "raft_base_acceleration", "raft_base_fan_speed", "raft_base_wall_count", "raft_base_margin",
                            "raft_interface_layer_height", "raft_interface_line_width", "raft_interface_line_spacing",
                            "raft_interface_flow", "raft_interface_speed", "raft_interface_acceleration",
                            "raft_interface_fan_speed", "raft_interface_wall_count", "raft_interface_margin",
                            "raft_surface_layer_height", "raft_surface_line_width", "raft_surface_line_spacing",
                            "raft_surface_flow", "raft_surface_speed", "raft_surface_acceleration", "raft_surface_fan_speed",
                            "raft_surface_wall_count", "raft_surface_margin"}) {
        const ConfigOptionDef *def = print_config_def.get(key);
        CAPTURE(key);
        REQUIRE(def != nullptr);
        REQUIRE(def->mode == comAdvanced);
    }

    REQUIRE(print_config_def.get("raft_angle") == nullptr);
    REQUIRE(print_config_def.get("raft_angle_increment") == nullptr);

    for (const char *key : {"raft_base_layer_height", "raft_base_line_width", "raft_base_line_spacing",
                            "raft_interface_layer_height", "raft_interface_line_width", "raft_interface_line_spacing",
                            "raft_surface_layer_height", "raft_surface_line_width", "raft_surface_line_spacing"}) {
        const ConfigOptionDef *def = print_config_def.get(key);
        CAPTURE(key);
        REQUIRE(def != nullptr);
        REQUIRE(def->gui_type == ConfigOptionDef::GUIType::f_enum_open);
        REQUIRE(def->enum_values == std::vector<std::string>{"0"});
        REQUIRE(def->enum_labels.size() == 1);
        REQUIRE(def->enum_labels.front() == "Auto");
    }

    for (const char *key : {"raft_base_acceleration", "raft_interface_acceleration", "raft_surface_acceleration"}) {
        const ConfigOptionDef *def = print_config_def.get(key);
        CAPTURE(key);
        REQUIRE(def != nullptr);
        REQUIRE(def->mode == comAdvanced);
        REQUIRE(def->gui_type == ConfigOptionDef::GUIType::f_enum_open);
        REQUIRE(def->enum_values == std::vector<std::string>{"0"});
        REQUIRE(def->enum_labels.size() == 1);
        REQUIRE(def->enum_labels.front() == "Inherit");
        REQUIRE(def->default_value->getFloat() == Approx(0.));
    }

    for (const char *key : {"raft_airgap", "raft_layer_0_z_overlap", "raft_base_flow", "raft_base_speed",
                            "raft_base_fan_speed", "raft_base_wall_count", "raft_base_margin", "raft_interface_flow",
                            "raft_interface_speed", "raft_interface_fan_speed", "raft_interface_wall_count",
                            "raft_interface_margin", "raft_surface_flow", "raft_surface_speed", "raft_surface_fan_speed",
                            "raft_surface_wall_count", "raft_surface_margin", "raft_base_layers", "raft_interface_layers",
                            "raft_surface_layers"}) {
        const ConfigOptionDef *def = print_config_def.get(key);
        CAPTURE(key);
        REQUIRE(def != nullptr);
        REQUIRE(def->gui_type != ConfigOptionDef::GUIType::f_enum_open);
    }
}

TEST_CASE("Cura V1 resolves only phase geometry zero values as Auto", "[SupportMaterial][Raft][CuraV1][Auto]")
{
    PrintConfig print_config;
    PrintObjectConfig object_config;
    object_config.layer_height.value = 0.2;
    object_config.line_width.value = 0.;
    object_config.raft_base_layer_height.value = 0.;
    object_config.raft_base_line_width.value = 0.;
    object_config.raft_base_line_spacing.value = 0.;
    object_config.raft_interface_layer_height.value = 0.;
    object_config.raft_interface_line_width.value = 0.;
    object_config.raft_interface_line_spacing.value = 0.;
    object_config.raft_surface_layer_height.value = 0.;
    object_config.raft_surface_line_width.value = 0.;
    object_config.raft_surface_line_spacing.value = 0.;

    const RaftPlanConfig automatic = resolve_cura_raft_plan_config(print_config, object_config, 0.4, 0.4);
    REQUIRE(automatic.base_config.layer_height == Approx(0.3));
    REQUIRE(automatic.base_config.line_width == Approx(0.6));
    REQUIRE(automatic.base_config.line_spacing == Approx(1.5));
    REQUIRE(automatic.interface_config.layer_height == Approx(0.3));
    REQUIRE(automatic.interface_config.line_width == Approx(0.6));
    REQUIRE(automatic.interface_config.line_spacing == Approx(1.5));
    REQUIRE(automatic.surface_config.layer_height == Approx(0.2));
    REQUIRE(automatic.surface_config.line_width == Approx(0.42));
    REQUIRE(automatic.surface_config.line_spacing == Approx(0.42));

    SECTION("Surface Auto uses an absolute default line width")
    {
        object_config.line_width.value = 0.5;
        object_config.line_width.percent = false;
        const RaftPlanConfig absolute_width = resolve_cura_raft_plan_config(print_config, object_config, 0.4, 0.4);
        REQUIRE(absolute_width.surface_config.line_width == Approx(0.5));
        REQUIRE(absolute_width.surface_config.line_spacing == Approx(0.5));
    }

    SECTION("Surface Auto resolves a percentage default line width for the supported 0.4 mm nozzle")
    {
        object_config.line_width.value = 120.;
        object_config.line_width.percent = true;
        const RaftPlanConfig percentage_width = resolve_cura_raft_plan_config(print_config, object_config, 0.4, 0.4);
        REQUIRE(percentage_width.surface_config.line_width == Approx(0.48));
        REQUIRE(percentage_width.surface_config.line_spacing == Approx(0.48));
    }

    SECTION("explicit Surface geometry overrides the percentage default line width")
    {
        object_config.line_width.value = 120.;
        object_config.line_width.percent = true;
        object_config.raft_surface_layer_height.value = 0.23;
        object_config.raft_surface_line_width.value = 0.53;
        object_config.raft_surface_line_spacing.value = 0.54;
        const RaftPlanConfig explicit_surface = resolve_cura_raft_plan_config(print_config, object_config, 0.4, 0.4);
        REQUIRE(explicit_surface.surface_config.layer_height == Approx(0.23));
        REQUIRE(explicit_surface.surface_config.line_width == Approx(0.53));
        REQUIRE(explicit_surface.surface_config.line_spacing == Approx(0.54));
    }

    object_config.raft_base_layer_height.value = 0.21;
    object_config.raft_base_line_width.value = 0.51;
    object_config.raft_base_line_spacing.value = 1.21;
    object_config.raft_interface_layer_height.value = 0.22;
    object_config.raft_interface_line_width.value = 0.52;
    object_config.raft_interface_line_spacing.value = 1.22;
    object_config.raft_surface_layer_height.value = 0.23;
    object_config.raft_surface_line_width.value = 0.53;
    object_config.raft_surface_line_spacing.value = 0.54;

    const RaftPlanConfig explicit_values = resolve_cura_raft_plan_config(print_config, object_config, 0.4, 0.4);
    REQUIRE(explicit_values.base_config.layer_height == Approx(0.21));
    REQUIRE(explicit_values.base_config.line_width == Approx(0.51));
    REQUIRE(explicit_values.base_config.line_spacing == Approx(1.21));
    REQUIRE(explicit_values.interface_config.layer_height == Approx(0.22));
    REQUIRE(explicit_values.interface_config.line_width == Approx(0.52));
    REQUIRE(explicit_values.interface_config.line_spacing == Approx(1.22));
    REQUIRE(explicit_values.surface_config.layer_height == Approx(0.23));
    REQUIRE(explicit_values.surface_config.line_width == Approx(0.53));
    REQUIRE(explicit_values.surface_config.line_spacing == Approx(0.54));
}

namespace {
// Check actual deposited regions from all owners, allowing covered recipients
// to have no paths. The model objects and their layer plans remain independent.
void require_shared_rafts_are_disjoint(const Print& print)
{
    REQUIRE_FALSE(print.get_conflict_result().has_value());
    struct Footprint
    {
        const SupportLayer* layer;
        Polygons            polygons;
    };
    std::vector<Footprint> footprints;
    Polygons               bed    = diff(Polygons{Polygon(get_bed_shape(print.config()))}, get_bed_excluded_area(print.config()));
    const Vec3d            origin = print.get_plate_origin();
    for (Polygon& polygon : bed)
        polygon.translate(Point::new_scale(origin.x(), origin.y()));
    for (const PrintObject* object : print.objects())
        for (const SupportLayer* layer : object->support_layers()) {
            if (layer->print_z > object->slicing_parameters().raft_contact_top_z + EPSILON)
                continue;
            REQUIRE((layer->has_extrusions() || layer->raft_covered_by_shared_paths));
            Polygons area = layer->support_fills.polygons_covered_by_width();
            for (Polygon& polygon : area)
                polygon.translate(object->instances().front().shift);
            REQUIRE(diff(area, offset(bed, scaled<float>(0.0001))).empty());
            for (const auto& previous : footprints) {
                if (std::min(layer->print_z, previous.layer->print_z) <= std::max(layer->bottom_z(), previous.layer->bottom_z()) + EPSILON)
                    continue;
                REQUIRE(intersection(area, previous.polygons).empty());
            }
            footprints.push_back({layer, std::move(area)});
        }
}

size_t raft_path_owners(const Print& print, size_t index)
{
    size_t result = 0;
    for (const PrintObject* object : print.objects()) {
        REQUIRE(object->support_layers().size() > index);
        result += !object->support_layers()[index]->support_fills.empty();
    }
    return result;
}

double all_raft_volume(const Print& print)
{
    double volume = 0.;
    for (const PrintObject* object : print.objects())
        volume += raft_volume(*object);
    return volume;
}

struct RaftMergeTemporaryPath
{
    boost::filesystem::path path;
    explicit RaftMergeTemporaryPath(const std::string& suffix = "")
        : path(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orca-raft-merge-%%%%-%%%%" + suffix))
    {}
    ~RaftMergeTemporaryPath()
    {
        boost::system::error_code ignored;
        boost::filesystem::remove_all(path, ignored);
    }
};
} // namespace

TEST_CASE("Four independent legs share overlapping rafts while a distant tabletop stays independent", "[SupportMaterial][Raft][Merge]")
{
    const auto style = GENERATE(smsGrid, smsSnug, smsTreeOrganic, smsTreeSlim, smsTreeStrong, smsTreeHybrid);
    const int  walls = GENERATE(0, 2);
    INFO("style=" << int(style) << ", walls=" << walls);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(style));
    config.set_key_value("support_type",
                         new ConfigOptionEnum<SupportType>(style == smsGrid || style == smsSnug ? stNormalAuto : stTreeAuto));
    config.set("raft_base_wall_count", walls);
    config.set("raft_interface_wall_count", walls);
    config.set("raft_surface_wall_count", walls);
    Model model;
    for (const Vec3d& offset : {Vec3d(30., 30., 0.), Vec3d(41., 30., 0.), Vec3d(30., 41., 0.), Vec3d(41., 41., 0.)})
        add_raft_bed_test_cube(model, 8., offset);
    add_raft_bed_test_cube(model, 15., Vec3d(70., 70., 0.))->name = "separate tabletop";
    Print print;
    process_raft_bed_test(print, model, config);
    REQUIRE(print.objects().size() == 5);
    for (size_t index = 0; index < 3; ++index)
        REQUIRE(raft_path_owners(print, index) == 2);
    double original_volume = 0.;
    for (size_t i = 0; i < print.objects().size(); ++i) {
        const PrintObject& object = *print.objects()[i];
        REQUIRE(object.model_object()->id() == model.objects[i]->id());
        REQUIRE(object.instances().front().model_instance->get_offset().isApprox(model.objects[i]->instances.front()->get_offset()));
        REQUIRE_FALSE(object.layers().empty());
        for (const SupportLayer* layer : object.support_layers()) {
            if (layer->print_z > object.slicing_parameters().raft_contact_top_z + EPSILON)
                continue;
            if (i < 4) {
                REQUIRE(layer->raft_merge_backup != nullptr);
                original_volume += layer->raft_merge_backup->fills.total_volume();
            } else {
                REQUIRE(layer->raft_merge_backup == nullptr);
                original_volume += layer->support_fills.total_volume();
            }
        }
    }
    REQUIRE(all_raft_volume(print) > 0.);
    REQUIRE(all_raft_volume(print) < original_volume);
    require_shared_rafts_are_disjoint(print);
    Polygons top_material;
    for (const PrintObject* object : print.objects()) {
        Polygons footprint = object->support_layers()[2]->support_fills.polygons_covered_by_width();
        for (Polygon& polygon : footprint)
            polygon.translate(object->instances().front().shift);
        polygons_append(top_material, footprint);
    }
    // Permit normal line-spacing gaps, but no missing raft underneath a leg.
    const ExPolygons supported = offset_ex(union_ex(top_material), scale_(0.4));
    for (const PrintObject* object : print.objects()) {
        ExPolygons bottom = object->layers().front()->lslices;
        translate(bottom, object->instances().front().shift);
        REQUIRE(diff_ex(bottom, supported).empty());
    }
}

TEST_CASE("Automatic and explicit equivalent raft values merge despite different margins and model gaps", "[SupportMaterial][Raft][Merge]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set("line_width", 0.5);
    for (const std::string phase : {"base", "interface", "surface"})
        for (const std::string suffix : {"layer_height", "line_width", "line_spacing"})
            config.set("raft_" + phase + "_" + suffix, 0.);
    Model model;
    add_raft_bed_test_cube(model, 8., Vec3d(30., 40., 0.));
    ModelObject* second = add_raft_bed_test_cube(model, 8., Vec3d(41., 40., 0.));
    for (const std::string phase : {"base", "interface", "surface"}) {
        second->config.set("raft_" + phase + "_layer_height", phase == "surface" ? 0.2 : 0.3);
        second->config.set("raft_" + phase + "_line_width", phase == "surface" ? 0.5 : 0.6);
        second->config.set("raft_" + phase + "_line_spacing", phase == "surface" ? 0.5 : 1.5);
    }
    second->config.set("raft_base_margin", 4.);
    second->config.set("raft_interface_margin", 3.);
    second->config.set("raft_surface_margin", 2.5);
    second->config.set("raft_airgap", 0.31);
    second->config.set("raft_layer_0_z_overlap", 0.08);
    second->config.set("wall_loops", 3);
    Print print;
    process_raft_bed_test(print, model, config);
    for (size_t index = 0; index < 3; ++index)
        REQUIRE(raft_path_owners(print, index) == 1);
    REQUIRE(print.objects()[1]->config().raft_airgap.value == Approx(0.31));
    REQUIRE(print.objects()[1]->config().raft_layer_0_z_overlap.value == Approx(0.08));
    REQUIRE(print.objects()[1]->layers().front()->print_z > print.objects()[0]->layers().front()->print_z);
    require_shared_rafts_are_disjoint(print);
}

TEST_CASE("Different raft extrusion recipes partition overlap without warnings or changing their settings", "[SupportMaterial][Raft][Merge]")
{
    const std::string key   = GENERATE(std::string("raft_surface_line_width"), std::string("raft_surface_flow"),
                                       std::string("raft_surface_speed"), std::string("solid_infill_direction"));
    const double      value = key == "raft_surface_line_width" ? 0.7 :
                              key == "raft_surface_flow"       ? 95. :
                              key == "raft_surface_speed"      ? 35. :
                                                                 13.;
    INFO(key);
    DynamicPrintConfig config = raft_bed_test_config();
    Model              model;
    add_raft_bed_test_cube(model, 8., Vec3d(30., 40., 0.));
    ModelObject* second = add_raft_bed_test_cube(model, 8., Vec3d(41., 40., 0.));
    if (key == "raft_surface_flow")
        second->config.set_key_value(key, new ConfigOptionPercent(value));
    else
        second->config.set(key, value);
    Print print;
    process_raft_bed_test(print, model, config);
    REQUIRE(raft_path_owners(print, 2) == 2);
    REQUIRE(second->config.get().opt_float(key) == Approx(value));
    require_shared_rafts_are_disjoint(print);
    for (const PrintObject* object : print.objects()) {
        SupportParameters parameters(*object);
        ExtrusionPaths    paths;
        getExtrusionPathsFromEntity(&object->support_layers()[2]->support_fills, paths);
        REQUIRE_FALSE(paths.empty());
        for (const ExtrusionPath& path : paths) {
            REQUIRE(path.width == Approx(parameters.raft_flow(RaftPhase::Surface).width()));
            REQUIRE(path.mm3_per_mm ==
                    Approx(parameters.raft_flow(RaftPhase::Surface).mm3_per_mm() * parameters.raft_flow_ratio(RaftPhase::Surface)));
        }
    }
}

TEST_CASE("Shared raft paths are restored after repeated slicing, moving and deleting neighbours", "[SupportMaterial][Raft][Merge]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    Model              model;
    ModelObject*       first  = add_raft_bed_test_cube(model, 8., Vec3d(30., 40., 0.));
    ModelObject*       second = add_raft_bed_test_cube(model, 8., Vec3d(41., 40., 0.));
    Print              print;
    process_raft_bed_test(print, model, config);
    const double merged_volume = all_raft_volume(print);
    print.process();
    REQUIRE(all_raft_volume(print) == Approx(merged_volume));
    require_shared_rafts_are_disjoint(print);
    second->instances.front()->set_offset(Vec3d(70., 40., 0.));
    second->invalidate_bounding_box();
    process_raft_bed_test(print, model, config);
    REQUIRE(all_raft_volume(print) > merged_volume);
    for (const PrintObject* object : print.objects())
        for (const SupportLayer* layer : object->support_layers()) {
            REQUIRE(layer->raft_merge_backup == nullptr);
            REQUIRE(layer->has_extrusions());
        }
    second->instances.front()->set_offset(Vec3d(41., 40., 0.));
    second->invalidate_bounding_box();
    process_raft_bed_test(print, model, config);
    REQUIRE(all_raft_volume(print) == Approx(merged_volume));
    REQUIRE(model.delete_object(first));
    process_raft_bed_test(print, model, config);
    REQUIRE(print.objects().size() == 1);
    for (const SupportLayer* layer : print.objects().front()->support_layers()) {
        REQUIRE(layer->raft_merge_backup == nullptr);
        REQUIRE(layer->has_extrusions());
    }
    Print fresh;
    process_raft_bed_test(fresh, model, config);
    REQUIRE(all_raft_volume(print) == Approx(all_raft_volume(fresh)));
}

TEST_CASE("Raft merge cache stores independent paths and can merge again after reload", "[SupportMaterial][Raft][Merge]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    Model              model;
    add_raft_bed_test_cube(model, 8., Vec3d(30., 40., 0.));
    add_raft_bed_test_cube(model, 8., Vec3d(41., 40., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    const double                 merged_volume = all_raft_volume(print);
    const RaftMergeTemporaryPath cache;
    REQUIRE(print.export_cached_data(cache.path.string()) == 0);
    Print loaded;
    loaded.apply(model, config);
    REQUIRE(loaded.validate().string.empty());
    loaded.set_status_silent();
    REQUIRE(loaded.load_cached_data(cache.path.string()) == 0);
    for (const PrintObject* object : loaded.objects())
        for (const SupportLayer* layer : object->support_layers()) {
            REQUIRE(layer->raft_merge_backup == nullptr);
            REQUIRE(layer->has_extrusions());
            REQUIRE_FALSE(layer->raft_outline.empty());
        }
    REQUIRE(all_raft_volume(loaded) > merged_volume);
    // Verify the cache contents directly, without relying on regeneration to hide
    // a cache that accidentally saved the aggregate/suppressed object paths.
    merge_overlapping_rafts(loaded);
    REQUIRE(all_raft_volume(loaded) == Approx(merged_volume));
    require_shared_rafts_are_disjoint(loaded);
    loaded.process(nullptr, true);
    REQUIRE(all_raft_volume(loaded) == Approx(merged_volume));
}

TEST_CASE("Merged rafts retain bed clipping, excluded holes and a translated plate origin", "[SupportMaterial][Raft][Merge]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("bed_exclude_area", new ConfigOptionPoints({{8.8, 31.}, {10.2, 31.}, {10.2, 35.}, {8.8, 35.}}));
    const Vec3d origin(150., 120., 0.);
    Model       model;
    add_raft_bed_test_cube(model, 8., origin + Vec3d(0., 30., 0.));
    add_raft_bed_test_cube(model, 8., origin + Vec3d(11., 30., 0.));
    Print print;
    print.set_plate_origin(origin);
    process_raft_bed_test(print, model, config);
    REQUIRE(raft_path_owners(print, 0) == 1);
    require_shared_rafts_are_disjoint(print);
}

TEST_CASE("G-code emits shared rafts without assigning their cancellation to a single leg", "[SupportMaterial][Raft][Merge][GCode]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("gcode_flavor", new ConfigOptionEnum<GCodeFlavor>(gcfKlipper));
    config.set("machine_start_gcode", std::string("G90\nM83\nG92 E0"));
    config.set("gcode_label_objects", true);
    config.set("exclude_object", true);
    for (const std::string& key : config.keys()) {
        const auto* option     = dynamic_cast<const ConfigOptionEnumsGeneric*>(config.option(key));
        const auto* definition = print_config_def.get(key);
        if (option && !option->keys_map && definition && definition->enum_keys_map) {
            auto* mapped   = new ConfigOptionEnumsGeneric(definition->enum_keys_map);
            mapped->values = option->values;
            config.set_key_value(key, mapped);
        }
    }
    Model model;
    add_raft_bed_test_cube(model, 8., Vec3d(30., 40., 0.))->name = "left leg";
    add_raft_bed_test_cube(model, 8., Vec3d(41., 40., 0.))->name = "right leg";
    Print print;
    process_raft_bed_test(print, model, config);
    const RaftMergeTemporaryPath output(".gcode");
    set_resources_dir((boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path() / "resources").string());
    GCodeProcessorResult result;
    print.export_gcode(output.path.string(), &result, nullptr);
    std::ifstream     stream(output.path.string());
    const std::string gcode((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const size_t      first_model = gcode.find("; printing object ");
    REQUIRE(first_model != std::string::npos);
    REQUIRE(gcode.find("; printing object left leg") != std::string::npos);
    REQUIRE(gcode.find("; printing object right leg") != std::string::npos);
    const std::string raft_gcode = gcode.substr(0, first_model);
    REQUIRE((raft_gcode.find(";TYPE:Support") != std::string::npos || raft_gcode.find("; FEATURE: Support") != std::string::npos));
    REQUIRE(raft_gcode.find("EXCLUDE_OBJECT_START") == std::string::npos);
    REQUIRE(gcode.find("EXCLUDE_OBJECT_START") != std::string::npos);
    REQUIRE(gcode.find("EXCLUDE_OBJECT_START") > first_model);
    require_shared_rafts_are_disjoint(print);
}

TEST_CASE("Different explicit raft materials keep their tools and partition overlapping regions", "[SupportMaterial][Raft][Merge]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("enable_prime_tower", false);
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.4}));
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75}));
    config.set("support_filament", 1);
    config.set("support_interface_filament", 1);
    Model model;
    add_raft_bed_test_cube(model, 8., Vec3d(30., 40., 0.));
    ModelObject* second = add_raft_bed_test_cube(model, 8., Vec3d(41., 40., 0.));
    second->config.set("support_interface_filament", 2);
    Print print;
    process_raft_bed_test(print, model, config);
    REQUIRE(print.objects()[0]->config().support_interface_filament.value == 1);
    REQUIRE(print.objects()[1]->config().support_interface_filament.value == 2);
    REQUIRE(raft_path_owners(print, 0) == 1);
    REQUIRE(raft_path_owners(print, 1) == 2);
    REQUIRE(raft_path_owners(print, 2) == 2);
    require_shared_rafts_are_disjoint(print);
}

TEST_CASE("Shared raft XY crossings are conflicts only when their physical Z intervals overlap", "[SupportMaterial][Raft][Merge]")
{
    const int        owner = 1, neighbour = 2;
    const LineWithID raft(Line(Point::new_scale(-5., 0.), Point::new_scale(5., 0.)), &owner, erSupportMaterial, 0., 0.3);
    const Line       crossing(Point::new_scale(0., -5.), Point::new_scale(0., 5.));
    REQUIRE_FALSE(ConflictChecker::line_intersect(raft, LineWithID(crossing, &neighbour, erPerimeter, 1.0, 1.2)).has_value());
    REQUIRE_FALSE(ConflictChecker::line_intersect(raft, LineWithID(crossing, &neighbour, erPerimeter, 0.3, 0.5)).has_value());
    REQUIRE(ConflictChecker::line_intersect(raft, LineWithID(crossing, &neighbour, erPerimeter, 0.2, 0.4)).has_value());
}

namespace {
void disable_test_raft(ModelObject& object)
{
    object.config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    object.config.set("raft_layers", 0);
}

ExPolygon clearance_rectangle(double x0, double y0, double x1, double y1)
{
    return ExPolygon(
        Polygon(Points{Point::new_scale(x0, y0), Point::new_scale(x1, y0), Point::new_scale(x1, y1), Point::new_scale(x0, y1)}));
}

void append_clearance_path(ExtrusionEntityCollection& fills,
                           const Point&               from,
                           const Point&               to,
                           float                      width  = 1.f,
                           float                      height = 0.3f,
                           ExtrusionRole              role   = erSupportMaterial)
{
    ExtrusionPath path(role, 0.1, width, height);
    path.polyline.points = {from, to};
    fills.entities.push_back(new ExtrusionPath(std::move(path)));
}

struct RaftClearanceFixture
{
    Model model;
    Print print;

    explicit RaftClearanceFixture(bool multiple_unrafted_instances = false, Vec3d origin = Vec3d::Zero())
    {
        add_raft_bed_test_cube(model, 8., origin + Vec3d(30., 40., 0.))->name = "rafted A";
        ModelObject* other = add_raft_bed_test_cube(model, 8., origin + Vec3d(70., 70., 0.));
        other->name        = "unrafted B";
        disable_test_raft(*other);
        if (multiple_unrafted_instances)
            other->add_instance()->set_offset(origin + Vec3d(10., 10., 0.));
        print.set_plate_origin(origin);
        print.apply(model, raft_bed_test_config());
        REQUIRE(print.objects().size() == 2);
        REQUIRE(rafted().has_raft());
        REQUIRE_FALSE(unrafted().has_raft());
    }

    PrintObject& rafted() { return *print.get_object(0); }
    PrintObject& unrafted() { return *print.get_object(1); }

    SupportLayer& raft_line(const Point& from, const Point& to, double print_z = 0.3, float height = 0.3f)
    {
        auto*       layer = rafted().add_support_layer(0, 0, 0.05, print_z);
        const Point shift = rafted().instances().front().shift;
        append_clearance_path(layer->support_fills, from - shift, to - shift, 1.f, height);
        return *layer;
    }

    Layer& model_area(ExPolygon area, double print_z = 0.3, double height = 0.3)
    {
        auto* layer = unrafted().add_layer(0, height, print_z, print_z - height / 2.);
        area.translate(-unrafted().instances().front().shift);
        layer->lslices.push_back(std::move(area));
        return *layer;
    }
};
} // namespace

TEST_CASE("One raft cannot intrude into an unrafted object and moving it away permits reslicing", "[SupportMaterial][Raft][Clearance]")
{
    const auto         mode   = GENERATE(RaftMode::Legacy, RaftMode::CuraV1);
    const auto         style  = GENERATE(smsGrid, smsTreeOrganic, smsTreeSlim);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(mode));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(style));
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(style == smsGrid ? stNormalAuto : stTreeAuto));
    Model model;
    add_raft_bed_test_cube(model, 8., Vec3d(30., 40., 0.))->name = "rafted A";
    ModelObject* off                                             = add_raft_bed_test_cube(model, 2., Vec3d(38.5, 42., 0.));
    off->name                                                    = "unrafted B";
    disable_test_raft(*off);
    // The model meshes are separated by 0.5 mm; only the expanded raft collides.
    REQUIRE(model.objects[0]->instance_bounding_box(0).max.x() < off->instance_bounding_box(0).min.x());
    Print print;
    REQUIRE_THROWS_WITH(process_raft_bed_test(print, model, config), Catch::Matchers::Contains("has raft disabled"));
    REQUIRE(print.objects()[0]->has_raft());
    REQUIRE_FALSE(print.objects()[1]->has_raft());
    REQUIRE(off->config.opt_int("raft_layers") == 0);
    off->instances.front()->set_offset(Vec3d(65., 42., 0.));
    off->invalidate_bounding_box();
    REQUIRE_NOTHROW(process_raft_bed_test(print, model, config));
    REQUIRE_FALSE(print.objects()[1]->has_raft());
    // A previous successful slice must not suppress the check after another move.
    off->instances.front()->set_offset(Vec3d(38.5, 42., 0.));
    off->invalidate_bounding_box();
    REQUIRE_THROWS_WITH(process_raft_bed_test(print, model, config), Catch::Matchers::Contains("has raft disabled"));
}

TEST_CASE("Merged raft ownership does not hide a conflict with an unrafted neighbour", "[SupportMaterial][Raft][Clearance]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("raft_base_margin", 5.);
    config.set("raft_interface_margin", 4.);
    config.set("raft_surface_margin", 3.);
    Model model;
    add_raft_bed_test_cube(model, 8., Vec3d(30., 40., 0.));
    add_raft_bed_test_cube(model, 8., Vec3d(41., 40., 0.));
    ModelObject* off = add_raft_bed_test_cube(model, 2., Vec3d(38.8, 43., 0.));
    disable_test_raft(*off);
    Print print;
    REQUIRE_THROWS_WITH(process_raft_bed_test(print, model, config), Catch::Matchers::Contains("has raft disabled"));
    REQUIRE(print.objects()[0]->support_layers().front()->raft_merge_backup != nullptr);
    REQUIRE(print.objects()[1]->support_layers().front()->raft_merge_backup != nullptr);
    REQUIRE((!print.objects()[0]->support_layers().front()->support_fills.empty() +
             !print.objects()[1]->support_layers().front()->support_fills.empty()) == 1);
}

TEST_CASE("Arranging mixed rafts reserves generated contacts and the unrafted object's brim before slicing",
          "[SupportMaterial][Raft][Clearance][Arrange]")
{
    // Individual scenarios keep runtime bounded while exercising the real
    // arrangement, contact generation, optional merging and clearance check.
    const int scenario = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    CAPTURE(scenario);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("initial_layer_print_height", 0.24);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.5, false));
    config.set_key_value("support_line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("raft_interface_margin", 1.);
    config.set("raft_surface_margin", 1.);
    config.set("raft_base_wall_count", 4);
    for (const char* key : {"raft_base_line_width", "raft_interface_line_width", "raft_surface_line_width", "raft_base_line_spacing",
                            "raft_interface_line_spacing", "raft_surface_line_spacing"})
        config.set(key, 0.);
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(scenario == 1 ? btNoBrim : btOuterAndInner));
    config.set("brim_width", 2.);
    config.set("brim_object_gap", 0.);
    if (scenario == 2)
        config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsSnug));
    if (scenario == 3 || scenario == 4) {
        config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTreeAuto));
        config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(scenario == 3 ? smsTreeOrganic : smsTreeSlim));
    }
    if (scenario == 5) {
        config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {180., 0.}, {180., 180.}, {0., 180.}}));
        config.set("support_angle", 45.);
        config.set("support_base_pattern_spacing", 4.);
        config.set_key_value("support_line_width", new ConfigOptionFloatOrPercent(150., true));
        config.set("raft_base_line_width", 0.8);
    }
    if (scenario == 11) {
        config.set_key_value("skirt_type", new ConfigOptionEnum<SkirtType>(stPerObject));
        config.set("skirt_loops", 1);
        config.set("skirt_distance", 2.);
    }

    Model                      model;
    const std::array<Vec3d, 3> positions{Vec3d(56.55, 58.55, 5.05), Vec3d(56.55, 42.45, 5.05), Vec3d(42.45, 51.588883, 5.05)};
    for (size_t i = 0; i < positions.size(); ++i) {
        ModelObject* object = model.add_object();
        object->name        = "mixed arrange cube " + std::to_string(i);
        TriangleMesh mesh   = make_cube(10.1, 10.1, 10.1);
        mesh.translate(-5.05f, -5.05f, -5.05f);
        object->add_volume(std::move(mesh));
        object->add_instance()->set_offset(positions[i]);
        object->ensure_on_bed();
    }
    ModelObject* off = model.objects.back();
    disable_test_raft(*off);
    if (scenario == 8) {
        off->add_instance(*off->instances.front())->set_offset(Vec3d(70., 70., off->instances.front()->get_offset().z()));
    }
    if (scenario == 9) {
        model.objects.front()->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
        model.objects.front()->config.set("raft_layers", 3);
    }
    if (scenario == 10) {
        // A real cantilever on the unrafted object generates bed support and
        // a support brim, rather than only setting an unused support flag.
        TriangleMesh ledge = make_cube(24., 10., 2.);
        ledge.translate(-12.f, -5.f, 4.f);
        off->add_volume(std::move(ledge));
        off->config.set("enable_support", true);
        off->config.set("support_threshold_angle", 45);
        off->config.set("brim_width", 3.);
    }
    if (scenario == 0) {
        // Full default configs contain a few enum vectors without their string
        // maps; export serializes these options, unlike a process-only test.
        for (const std::string& key : config.keys()) {
            const auto* option     = dynamic_cast<const ConfigOptionEnumsGeneric*>(config.option(key));
            const auto* definition = print_config_def.get(key);
            if (option && !option->keys_map && definition && definition->enum_keys_map) {
                auto* mapped   = new ConfigOptionEnumsGeneric(definition->enum_keys_map);
                mapped->values = option->values;
                config.set_key_value(key, mapped);
            }
        }
        Print before;
        // Original GUI layout: nominal margins leave 4 mm but generated
        // normal-grid raft / brim still overlap. No segmentation is needed.
        REQUIRE_THROWS_WITH(process_raft_bed_test(before, model, config), Catch::Matchers::Contains("has raft disabled"));
    }

    ArrangePolygons selected, fixed;
    for (size_t i = 0; i < model.objects.size(); ++i) {
        for (ModelInstance* instance : model.objects[i]->instances) {
            auto       item     = get_instance_arrange_poly(instance, config);
            const bool is_fixed = (scenario == 6 && i == 2) || ((scenario == 7 || scenario == 12) && i == 0);
            (is_fixed ? fixed : selected).push_back(std::move(item));
        }
    }
    Vec3d fixed_offset = Vec3d::Zero();
    if (!fixed.empty())
        fixed_offset = fixed.front().model_instance->get_offset();
    ArrangeParams params;
    params.parallel         = false;
    params.allow_rotations  = scenario == 5;
    params.min_obj_distance = scaled(0.2); // must not override physical clearance
    params.progressind      = [](unsigned, std::string) {};
    if (scenario == 12)
        params.align_center = Vec2d(0.25, 0.75);
    update_arrange_params(params, &config, selected);
    REQUIRE(prepare_mixed_raft_clearance(selected, fixed, config, params));
    update_selected_items_inflation(selected, &config, params);
    update_unselected_items_inflation(fixed, &config, params);
    arrangement::arrange(selected, fixed, get_shrink_bedpts(&config, params), params);
    for (ArrangePolygon& item : selected) {
        REQUIRE(item.bed_idx == 0);
        item.apply();
    }
    if (!fixed.empty())
        REQUIRE(fixed.front().model_instance->get_offset().isApprox(fixed_offset));
    REQUIRE(off->config.opt_int("raft_layers") == 0);

    Print print;
    REQUIRE_NOTHROW(process_raft_bed_test(print, model, config));
    REQUIRE_NOTHROW(print.validate_raft_clearance());
    REQUIRE(print.objects().size() == 3);
    REQUIRE(print.objects().front()->has_raft());
    REQUIRE_FALSE(print.objects().back()->has_raft());
    if (scenario == 10)
        REQUIRE_FALSE(print.objects().back()->support_layers().empty());

    // Exercise the independent export gate as well as Print::process().
    if (scenario == 0) {
        const RaftMergeTemporaryPath output(".gcode");
        set_resources_dir((boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path() / "resources").string());
        GCodeProcessorResult result;
        REQUIRE_NOTHROW(print.export_gcode(output.path.string(), &result, nullptr));
        REQUIRE(boost::filesystem::file_size(output.path) > 0);
    }
}

TEST_CASE("Compact raft arrangement feeds one shared raft per physical layer on both printer sizes",
          "[SupportMaterial][Raft][Merge][Arrange][Compact]")
{
    const double bed_size  = GENERATE(100., 180.);
    const bool   segmented = GENERATE(false, true);
    CAPTURE(bed_size, segmented);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {bed_size, 0.}, {bed_size, bed_size}, {0., bed_size}}));
    config.set("raft_base_margin", 5.);
    config.set("raft_interface_margin", 4.);
    config.set("raft_surface_margin", 3.);
    config.set("raft_segmented", segmented);
    config.set("raft_smoothing", segmented ? 1. : 0.);
    Model model;
    for (int i = 0; i < 4; ++i)
        add_raft_bed_test_cube(model, (bed_size - 8.) / 2., Vec3d(200. + i * bed_size, 200., 0.));
    ArrangePolygons selected, fixed;
    for (ModelObject* object : model.objects)
        selected.push_back(get_instance_arrange_poly(object->instances.front(), config));
    ArrangeParams params;
    params.parallel    = false;
    params.progressind = [](unsigned, std::string) {};
    update_arrange_params(params, &config, selected);
    REQUIRE(prepare_compact_raft_arrangement(selected, fixed, config, params));
    update_selected_items_inflation(selected, &config, params);
    const Points bed = get_shrink_bedpts(&config, params);
    arrangement::arrange(selected, fixed, bed, params);
    for (ArrangePolygon& item : selected) {
        REQUIRE(item.bed_idx == 0);
        REQUIRE(BoundingBox(bed).contains(get_extents(item.transformed_poly())));
        item.apply();
    }
    Print print;
    REQUIRE_NOTHROW(process_raft_bed_test(print, model, config));
    REQUIRE(print.objects().size() == 4);
    require_shared_rafts_are_disjoint(print);
    for (size_t layer = 0; layer < 3; ++layer)
        REQUIRE(raft_path_owners(print, layer) == 1);
    REQUIRE(all_raft_volume(print) > 0.);
    for (const PrintObject* object : print.objects()) {
        REQUIRE(object->instances().size() == 1);
        REQUIRE_FALSE(object->layers().empty());
        REQUIRE(object->layers().front()->has_extrusions());
        REQUIRE(object->support_layers().front()->raft_merge_backup != nullptr);
    }
    // Replaying the same source must not duplicate the merged deposition.
    const double volume = all_raft_volume(print);
    merge_overlapping_rafts(print);
    REQUIRE(all_raft_volume(print) == Approx(volume));
    require_shared_rafts_are_disjoint(print);

    // A raft-off override switches the next arrange back to mixed clearance.
    disable_test_raft(*model.objects.back());
    selected.clear();
    for (ModelObject* object : model.objects)
        selected.push_back(get_instance_arrange_poly(object->instances.front(), config));
    REQUIRE(prepare_mixed_raft_clearance(selected, fixed, config, params));
    REQUIRE_FALSE(prepare_compact_raft_arrangement(selected, fixed, config, params));
    for (const auto& item : selected)
        REQUIRE(item.compact_raft_obstacle_margin == 0.);
}

TEST_CASE("Large rafted models arrange and slice with clipped raft paths on both printer sizes", "[SupportMaterial][Raft][BedClip][Arrange]")
{
    const double bed_size  = GENERATE(100., 180.);
    const bool   segmented = GENERATE(false, true);
    CAPTURE(bed_size, segmented);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {bed_size, 0.}, {bed_size, bed_size}, {0., bed_size}}));
    config.set("raft_segmented", segmented);
    Model           model;
    ModelObject*    object = add_raft_bed_test_cube(model, bed_size - 2., Vec3d(200., 200., 0.));
    ArrangePolygons items{get_instance_arrange_poly(object->instances.front(), config)};
    ArrangeParams   params;
    params.parallel    = false;
    params.progressind = [](unsigned, std::string) {};
    update_arrange_params(params, &config, items);
    update_selected_items_inflation(items, &config, params);
    const Points bed = get_shrink_bedpts(&config, params);
    arrangement::arrange(items, {}, bed, params);
    REQUIRE(items.front().bed_idx == 0);
    REQUIRE(BoundingBox(bed).contains(get_extents(items.front().transformed_poly())));
    items.front().apply();
    Print print;
    REQUIRE_NOTHROW(process_raft_bed_test(print, model, config));
    REQUIRE(print.objects().size() == 1);
    REQUIRE(print.objects().front()->has_raft());
    REQUIRE(raft_volume(*print.objects().front()) > 0.);
    require_raft_inside_bed(print);
}

TEST_CASE("Unrafted models filling the usable bed arrange and slice without clipping model paths",
          "[SupportMaterial][Raft][BedClip][Arrange]")
{
    const double bed_size = GENERATE(100., 101., 180.);
    const bool   with_brim = GENERATE(false, true);
    CAPTURE(bed_size, with_brim);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("printer_technology", new ConfigOptionEnum<PrinterTechnology>(ptFFF));
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    config.set("raft_layers", 0);
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(with_brim ? btOuterAndInner : btNoBrim));
    config.set("brim_width", 2.);
    config.set("brim_object_gap", 0.);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.5, false));
    config.set("initial_layer_print_height", 0.24);
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {bed_size, 0.}, {bed_size, bed_size}, {0., bed_size}}));
    Model           model;
    ModelObject*    object = add_raft_bed_test_cube(model, bed_size - 2., Vec3d(200., 200., 0.));
    ArrangePolygons items{get_instance_arrange_poly(object->instances.front(), config)};
    ArrangeParams   params;
    params.parallel    = false;
    params.progressind = [](unsigned, std::string) {};
    update_arrange_params(params, &config, items);
    update_selected_items_inflation(items, &config, params);
    const Points bed = get_shrink_bedpts(&config, params);
    arrangement::arrange(items, {}, bed, params);
    REQUIRE(items.front().bed_idx == 0);
    REQUIRE(items.front().inflation == scaled(1.));
    REQUIRE(BoundingBox(bed).contains(get_extents(items.front().transformed_poly())));
    items.front().apply();
    Print print;
    REQUIRE_NOTHROW(process_raft_bed_test(print, model, config));
    REQUIRE(print.objects().size() == 1);
    const PrintObject& printed = *print.objects().front();
    REQUIRE_FALSE(printed.has_raft());
    REQUIRE(printed.support_layers().empty());
    REQUIRE_FALSE(printed.layers().empty());
    const Polygons printable_bed{Polygon(get_bed_shape(print.config()))};
    double         volume = 0.;
    for (const Layer* layer : printed.layers()) {
        Polygons footprint;
        for (const LayerRegion* region : layer->regions()) {
            Polygons perimeters = region->perimeters.polygons_covered_by_width();
            Polygons fills      = region->fills.polygons_covered_by_width();
            footprint.insert(footprint.end(), perimeters.begin(), perimeters.end());
            footprint.insert(footprint.end(), fills.begin(), fills.end());
            volume += region->perimeters.total_volume() + region->fills.total_volume();
        }
        REQUIRE_FALSE(footprint.empty());
        for (const PrintInstance& instance : printed.instances()) {
            Polygons placed = footprint;
            for (Polygon& polygon : placed)
                polygon.translate(instance.shift);
            REQUIRE(diff(placed, offset(printable_bed, scaled<float>(0.0001))).empty());
        }
    }
    REQUIRE(volume > 0.);
    if (with_brim) {
        const Polygons    footprint = require_brim_inside_bed(print);
        const BoundingBox bounds    = get_extents(footprint);
        // The boundary loop must survive near each bed edge, not just vanish
        // so that the remaining inner loop appears to pass a containment check.
        REQUIRE(unscale<double>(bounds.min.x()) < 0.01);
        REQUIRE(unscale<double>(bounds.min.y()) < 0.01);
        REQUIRE(unscale<double>(bounds.max.x()) > bed_size - 0.01);
        REQUIRE(unscale<double>(bounds.max.y()) > bed_size - 0.01);
        const auto& brim = print.get_brimMap().at(printed.id());
        REQUIRE(std::any_of(brim.entities.begin(), brim.entities.end(), [bed_size](const ExtrusionEntity* entity) {
            if (dynamic_cast<const ExtrusionLoop*>(entity) == nullptr)
                return false;
            const BoundingBox loop_bounds = get_extents(entity->polygons_covered_by_width());
            return unscale<double>(loop_bounds.min.x()) < 0.01 && unscale<double>(loop_bounds.min.y()) < 0.01 &&
                   unscale<double>(loop_bounds.max.x()) > bed_size - 0.01 && unscale<double>(loop_bounds.max.y()) > bed_size - 0.01;
        }));
    } else {
        REQUIRE(print.get_brimMap().empty());
    }
}

TEST_CASE("Automatic brim at the usable bed size can resolve to zero or printable clipped paths",
          "[SupportMaterial][Raft][Brim][BedClip][Arrange]")
{
    const double bed_size   = GENERATE(100., 101., 180.);
    const bool   needs_brim = GENERATE(false, true);
    CAPTURE(bed_size, needs_brim);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("printer_technology", new ConfigOptionEnum<PrinterTechnology>(ptFFF));
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    config.set("raft_layers", 0);
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btAutoBrim));
    // Auto does not use the explicit-width setting. Resolve the actual width
    // through the normal slicer, including its tall/narrow adhesion rule.
    config.set("brim_width", 0.);
    config.set("brim_object_gap", 0.);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.5, false));
    config.set("initial_layer_print_height", 0.24);
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {bed_size, 0.}, {bed_size, bed_size}, {0., bed_size}}));
    Model        model;
    ModelObject* object = model.add_object();
    object->add_volume(make_cube(bed_size - 2., needs_brim ? 2. : bed_size - 2., needs_brim ? 12. : 0.8));
    object->add_instance()->set_offset(Vec3d(200., 200., 0.));
    object->ensure_on_bed();
    ArrangePolygons items{get_instance_arrange_poly(object->instances.front(), config)};
    ArrangeParams   params;
    params.parallel        = false;
    params.allow_rotations = false;
    params.progressind     = [](unsigned, std::string) {};
    update_arrange_params(params, &config, items);
    update_selected_items_inflation(items, &config, params);
    const Points bed = get_shrink_bedpts(&config, params);
    REQUIRE(items.front().allow_brim_bed_clipping);
    REQUIRE_FALSE(items.front().allow_unrafted_bed_fit);
    REQUIRE(items.front().brim_bed_clipping_margin > 18.);
    arrangement::arrange(items, {}, bed, params);
    REQUIRE(items.front().bed_idx == 0);
    REQUIRE(items.front().inflation == scaled(1.));
    REQUIRE(BoundingBox(bed).contains(get_extents(items.front().transformed_poly())));
    items.front().apply();
    Print print;
    REQUIRE_NOTHROW(process_raft_bed_test(print, model, config));
    REQUIRE(print.objects().size() == 1);
    const PrintObject& printed = *print.objects().front();
    REQUIRE_FALSE(printed.has_raft());
    REQUIRE(printed.support_layers().empty());
    REQUIRE_FALSE(printed.layers().empty());
    REQUIRE(printed.config().brim_type.value == btAutoBrim);
    REQUIRE(printed.config().brim_width.value == 0.);
    if (needs_brim) {
        const BoundingBox footprint = get_extents(require_brim_inside_bed(print));
        // Both ends of the long axis reach the bed edge without losing the brim.
        REQUIRE(unscale<double>(footprint.min.x()) < 0.01);
        REQUIRE(unscale<double>(footprint.max.x()) > bed_size - 0.01);
    } else {
        REQUIRE(print.get_brimMap().empty());
    }
}

TEST_CASE("Brim clipping respects nonrectangular beds, exclusions and translated plates", "[SupportMaterial][Raft][Brim][BedClip]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    config.set("raft_layers", 0);
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    config.set("brim_width", 12.);
    config.set("brim_object_gap", 0.);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(GENERATE(0.5, 0.8), false));
    config.set("initial_layer_print_height", 0.24);
    SECTION("an exclusion intersects the brim but not the model")
    {
        config.set_key_value("bed_exclude_area", new ConfigOptionPoints({{72., 40.}, {76., 40.}, {76., 60.}, {72., 60.}}));
    }
    SECTION("a polygonal circular bed clips expanded corners")
    {
        Pointfs circle;
        for (size_t i = 0; i < 64; ++i) {
            const double angle = 2. * PI * double(i) / 64.;
            circle.emplace_back(50. + 35. * std::cos(angle), 50. + 35. * std::sin(angle));
        }
        config.set_key_value("printable_area", new ConfigOptionPoints(circle));
    }
    SECTION("a concave bed edge intersects the brim")
    {
        config.set_key_value("printable_area",
                             new ConfigOptionPoints({{0., 0.}, {100., 0.}, {100., 100.}, {74., 100.}, {74., 74.}, {0., 74.}}));
    }
    const Vec3d origin(150., -120., 0.);
    Model       model;
    add_raft_bed_test_cube(model, 40., origin + Vec3d(30., 30., 0.));
    Print print;
    print.set_plate_origin(origin);
    REQUIRE_NOTHROW(process_raft_bed_test(print, model, config));
    require_brim_inside_bed(print);
}

TEST_CASE("Changing the printable area invalidates cached unrafted brim paths", "[SupportMaterial][Raft][Brim][BedClip]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    config.set("raft_layers", 0);
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    config.set("brim_width", 10.);
    config.set("brim_object_gap", 0.);
    Model model;
    add_raft_bed_test_cube(model, 20., Vec3d(40., 40., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    const double original_area = area(union_ex(require_brim_inside_bed(print)));
    REQUIRE(print.is_step_done(psSkirtBrim));
    SECTION("bed size changes without moving the model")
    {
        config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {65., 0.}, {65., 100.}, {0., 100.}}));
    }
    SECTION("bed exclusions change without moving the model")
    {
        config.set_key_value("bed_exclude_area", new ConfigOptionPoints({{62., 40.}, {68., 40.}, {68., 60.}, {62., 60.}}));
    }
    print.apply(model, config);
    REQUIRE_FALSE(print.is_step_done(psSkirtBrim));
    print.process(nullptr, true);
    REQUIRE(area(union_ex(require_brim_inside_bed(print))) < original_area);
    // A plate-origin change also invalidates absolute brim coordinates.
    print.set_plate_origin(Vec3d(150., -120., 0.));
    REQUIRE_FALSE(print.is_step_done(psSkirtBrim));
}

TEST_CASE("Raft clearance uses deposited width and full path height rather than intersecting centre lines",
          "[SupportMaterial][Raft][Clearance]")
{
    RaftClearanceFixture f;
    f.raft_line(Point::new_scale(40., 40.), Point::new_scale(50., 40.));
    const Point shift = f.unrafted().instances().front().shift;
    double      y = 40.8, z = 0.3;
    float       height    = 0.3f;
    bool        collision = true;
    SECTION("parallel centre lines still overlap through their widths") {}
    SECTION("a positive XY gap is permitted")
    {
        y         = 41.1;
        collision = false;
    }
    SECTION("touching Z intervals are not a collision")
    {
        z         = 0.5;
        height    = 0.2f;
        collision = false;
    }
    SECTION("different top heights can occupy the same physical Z interval")
    {
        z      = 0.4;
        height = 0.2f;
    }
    auto* support = f.unrafted().add_support_layer(0, 0, 0.05, z);
    append_clearance_path(support->support_fills, Point::new_scale(42., y) - shift, Point::new_scale(48., y) - shift, 1.f, height);
    if (collision)
        REQUIRE_THROWS_AS(f.print.validate_raft_clearance(), SlicingError);
    else
        REQUIRE_NOTHROW(f.print.validate_raft_clearance());
}

TEST_CASE("Raft clearance respects holes, empty paths and physical Z when checking the model", "[SupportMaterial][Raft][Clearance]")
{
    RaftClearanceFixture f;
    SupportLayer&        raft      = f.raft_line(Point::new_scale(40., 40.), Point::new_scale(50., 40.));
    ExPolygon            area      = clearance_rectangle(39., 38., 51., 42.);
    bool                 collision = true;
    double               z = 0.3, height = 0.3;
    SECTION("a raft fully contained in a solid model is a collision") {}
    SECTION("a real model hole stays clear")
    {
        Polygon hole = clearance_rectangle(39.5, 39., 50.5, 41.).contour;
        hole.reverse();
        area.holes.push_back(std::move(hole));
        collision = false;
    }
    SECTION("the model is above the raft")
    {
        z         = 0.5;
        height    = 0.2;
        collision = false;
    }
    SECTION("outline metadata without emitted paths is not a collision")
    {
        raft.raft_outline = {area};
        raft.support_fills.clear();
        collision = false;
    }
    SECTION("a non-extruding move is not a collision")
    {
        dynamic_cast<ExtrusionPath*>(raft.support_fills.entities.front())->set_force_no_extrusion(true);
        collision = false;
    }
    f.model_area(std::move(area), z, height);
    if (collision)
        REQUIRE_THROWS_WITH(f.print.validate_raft_clearance(), Catch::Matchers::Contains("unrafted B"));
    else
        REQUIRE_NOTHROW(f.print.validate_raft_clearance());
}

TEST_CASE("Unrafted brim clearance uses actual first-layer Z and applies the plate offset only once", "[SupportMaterial][Raft][Clearance]")
{
    const Vec3d          origin(150., -120., 0.);
    const Point          plate_shift = Point::new_scale(origin.x(), origin.y());
    RaftClearanceFixture f(false, origin);
    f.raft_line(Point::new_scale(40., 40.) + plate_shift, Point::new_scale(50., 40.) + plate_shift);
    const double z = GENERATE(0.3, 0.6);
    f.model_area(clearance_rectangle(70. + origin.x(), 70. + origin.y(), 78. + origin.x(), 78. + origin.y()), z, 0.2);
    // The model stays far away. Only its already positioned brim reaches the raft.
    append_clearance_path(f.print.get_brimMap()[f.unrafted().id()], Point::new_scale(42., 40.8) + plate_shift,
                          Point::new_scale(48., 40.8) + plate_shift, 1.f, 0.2f, erBrim);
    if (z < 0.4)
        REQUIRE_THROWS_AS(f.print.validate_raft_clearance(), SlicingError);
    else
        REQUIRE_NOTHROW(f.print.validate_raft_clearance());
}

TEST_CASE("Raft clearance checks every unrafted instance without changing the multi-instance merge policy",
          "[SupportMaterial][Raft][Clearance]")
{
    RaftClearanceFixture f(true);
    REQUIRE(f.unrafted().instances().size() == 2);
    const Point second = f.unrafted().instances()[1].shift;
    f.raft_line(second + Point::new_scale(-3., 0.), second + Point::new_scale(3., 0.));
    auto* layer    = f.unrafted().add_layer(0, 0.3, 0.3, 0.15);
    layer->lslices = {clearance_rectangle(-2., -2., 2., 2.)};
    REQUIRE_THROWS_AS(f.print.validate_raft_clearance(), SlicingError);
}

TEST_CASE("An unrafted per-object skirt is checked even when its model stays outside the raft", "[SupportMaterial][Raft][Clearance]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("skirt_loops", 1);
    config.set("skirt_height", 1);
    config.set("skirt_distance", 4.);
    config.set_key_value("skirt_type", new ConfigOptionEnum<SkirtType>(stPerObject));
    Model model;
    add_raft_bed_test_cube(model, 8., Vec3d(20., 20., 0.));
    ModelObject* off = add_raft_bed_test_cube(model, 8., Vec3d(65., 65., 0.));
    disable_test_raft(*off);
    Print print;
    process_raft_bed_test(print, model, config);
    PrintObject*   rafted   = print.get_object(0);
    PrintObject*   unrafted = print.get_object(1);
    ExtrusionPaths skirt;
    getExtrusionPathsFromEntity(&unrafted->object_skirt(), skirt);
    REQUIRE_FALSE(skirt.empty());
    REQUIRE(skirt.front().polyline.points.size() >= 2);
    for (SupportLayer* layer : rafted->support_layers())
        layer->support_fills.clear();
    const Point shift = unrafted->instances().front().shift - rafted->instances().front().shift;
    append_clearance_path(rafted->support_layers().front()->support_fills, skirt.front().polyline.points[0] + shift,
                          skirt.front().polyline.points[1] + shift);
    REQUIRE_THROWS_AS(print.validate_raft_clearance(), SlicingError);
}

TEST_CASE("Raft clearance leaves all-off and print-by-object policies unchanged and honours cancellation",
          "[SupportMaterial][Raft][Clearance]")
{
    RaftClearanceFixture f;
    SECTION("all objects without a raft do not enter the checker")
    {
        disable_test_raft(*f.model.objects[0]);
        f.print.apply(f.model, raft_bed_test_config());
        REQUIRE_NOTHROW(f.print.validate_raft_clearance());
    }
    SECTION("print-by-object stays on its existing validation path")
    {
        DynamicPrintConfig config = raft_bed_test_config();
        config.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByObject));
        f.print.apply(f.model, config);
        f.raft_line(Point::new_scale(40., 40.), Point::new_scale(50., 40.));
        f.model_area(clearance_rectangle(39., 38., 51., 42.));
        REQUIRE_NOTHROW(f.print.validate_raft_clearance());
    }
    SECTION("cancellation interrupts footprint construction")
    {
        f.raft_line(Point::new_scale(40., 40.), Point::new_scale(50., 40.));
        f.print.cancel();
        REQUIRE_THROWS_AS(f.print.validate_raft_clearance(), CanceledException);
    }
    SECTION("the optional line-check flag does not bypass physical raft clearance")
    {
        f.raft_line(Point::new_scale(40., 40.), Point::new_scale(50., 40.));
        f.model_area(clearance_rectangle(39., 38., 51., 42.));
        f.print.set_no_check_flag(true);
        REQUIRE_THROWS_AS(f.print.validate_raft_clearance(), SlicingError);
    }
}

TEST_CASE("A raft collision is rejected before direct G-code export creates a file", "[SupportMaterial][Raft][Clearance]")
{
    RaftClearanceFixture f;
    f.raft_line(Point::new_scale(40., 40.), Point::new_scale(50., 40.));
    f.model_area(clearance_rectangle(39., 38., 51., 42.));
    const RaftMergeTemporaryPath output(".gcode");
    GCodeProcessorResult         result;
    REQUIRE_THROWS_AS(f.print.export_gcode(output.path.string(), &result, nullptr), SlicingError);
    REQUIRE_FALSE(boost::filesystem::exists(output.path));
}

TEST_CASE("Raft geometry follows X-PRINT size and aspect-ratio probes", "[SupportMaterial][Raft][Geometry]")
{
    std::ifstream stream((boost::filesystem::path(TEST_DATA_DIR) / "raft_geometry/reference-shapes.json").string());
    REQUIRE(stream.good());
    const nlohmann::json      fixture = nlohmann::json::parse(stream);
    const Polygons            bed{clearance_rectangle(0., 0., 180., 180.).contour};
    const RaftGeometryOptions options{true, fixture.at("chunk_size_mm").get<double>(), fixture.at("chunk_gap_mm").get<double>(), 0.};
    REQUIRE(fixture.at("cases").size() == 6);
    for (const auto& entry : fixture.at("cases")) {
        const std::string name  = entry.at("name").get<std::string>();
        const double      width = entry.at("width_mm").get<double>(), height = entry.at("height_mm").get<double>();
        const size_t      sections = entry.at("sections").get<size_t>();
        CAPTURE(name, width, height);
        const auto result = plan_raft_geometry({{clearance_rectangle(20., 20., 20. + width, 20. + height)}}, {0.}, bed, options, 0.4);
        REQUIRE(result.layers.front().size() == sections);
        REQUIRE((result.layer_cuts.front() == 0) == (sections == 1));
        REQUIRE(diff_ex(result.layers.front(), bed).empty());
    }
}

TEST_CASE("Raft phases classify their own contours after the phase inset", "[SupportMaterial][Raft][Geometry]")
{
    const Polygons            bed{clearance_rectangle(0., 0., 100., 100.).contour};
    const ExPolygons          outline{clearance_rectangle(20., 20., 60.5, 60.5)};
    const RaftGeometryOptions options{true, 40., 0.8, 0.};
    const auto                result = plan_raft_geometry({outline, outline, outline}, {0., 0.4, 0.25}, bed, options, 0.4);
    REQUIRE(result.layers[0].size() == 8);
    REQUIRE(result.layers[1].size() == 1);
    REQUIRE(result.layers[2].size() == 1);
    const std::vector<size_t> expected_cuts{4, 0, 0};
    REQUIRE(result.layer_cuts == expected_cuts);
    const ExPolygons center{clearance_rectangle(40.15, 40.15, 40.35, 40.35)};
    REQUIRE(intersection_ex(result.layers[0], center).empty());
    REQUIRE_FALSE(intersection_ex(result.layers[2], center).empty());
}

TEST_CASE("Separated small raft islands stay small while joined islands are chunked", "[SupportMaterial][Raft][Geometry]")
{
    const Polygons            bed{clearance_rectangle(0., 0., 180., 180.).contour};
    const RaftGeometryOptions options{true, 40., 0.8, 0.};
    const ExPolygons          separate{clearance_rectangle(10., 10., 30., 30.), clearance_rectangle(130., 130., 150., 150.)};
    const auto                apart = plan_raft_geometry({separate}, {0.}, bed, options, 0.4);
    REQUIRE(apart.layers.front().size() == 2);
    REQUIRE(apart.layer_cuts.front() == 0);
    const ExPolygons joined{clearance_rectangle(10., 10., 40., 40.), clearance_rectangle(32., 10., 62., 40.)};
    const auto       shared = plan_raft_geometry({joined}, {0.}, bed, options, 0.4);
    REQUIRE(shared.layers.front().size() == 2);
    REQUIRE(shared.layer_cuts.front() > 0);
    REQUIRE(intersection_ex(shared.layers.front(), ExPolygons{clearance_rectangle(35.8, 10., 36.2, 40.)}).empty());
}

TEST_CASE("Raft closing fills small holes but never printer exclusions", "[SupportMaterial][Raft][Geometry]")
{
    const ExPolygon     outer = clearance_rectangle(20., 20., 50., 50.);
    const ExPolygons    hole{clearance_rectangle(33., 33., 37., 37.)};
    const ExPolygons    ring = diff_ex(ExPolygons{outer}, hole);
    const Polygons      bed{clearance_rectangle(0., 0., 100., 100.).contour};
    RaftGeometryOptions options{true, 40., 0.8, 0.};
    const auto          unsmoothed = plan_raft_geometry({ring}, {0.}, bed, options, 0.4);
    REQUIRE(unsmoothed.layers.front().size() == 1);
    REQUIRE(unsmoothed.layers.front().front().holes.size() == 1);
    REQUIRE(std::abs(unsmoothed.layers.front().front().holes.front().area()) / 1e12 == Approx(16.).margin(0.02));
    // Round offsets can leave sub-micron clipping residues at a hole corner.
    // Verify the hole survives and its interior stays clear within the combined
    // precision of the two one-micron arc offsets.
    REQUIRE(intersection_ex(unsmoothed.layers.front(), offset_ex(hole, -scale_(0.002))).empty());
    options.smoothing   = 5.;
    const auto smoothed = plan_raft_geometry({ring}, {0.}, bed, options, 0.4);
    REQUIRE(diff_ex(hole, smoothed.layers.front()).empty());
    const Polygons excluded_bed = diff(bed, to_polygons(hole));
    const auto     excluded     = plan_raft_geometry({ring}, {0.}, excluded_bed, options, 0.4);
    REQUIRE(intersection_ex(excluded.layers.front(), hole).empty());
}

TEST_CASE("Raft containment uses round beads without square corner overreach", "[SupportMaterial][Raft][Geometry]")
{
    ExtrusionPath path(erSupportMaterial, 0.4, 2.f, 0.2f);
    path.polyline            = Polyline{Point::new_scale(20., 20.), Point::new_scale(40., 20.), Point::new_scale(40., 40.)};
    const ExPolygons rounded = raft_extrusion_coverage(ExtrusionPaths{path});
    // This patch is farther than the 1 mm bead radius from the turn but inside
    // the square join's added corner. Its coordinates are an independent oracle.
    const ExPolygons corner{clearance_rectangle(40.39, 19.005, 40.41, 19.015)};
    REQUIRE_FALSE(intersection_ex(path.polygons_covered_by_width(), to_polygons(corner)).empty());
    REQUIRE(intersection_ex(rounded, corner).empty());
    REQUIRE_FALSE(intersection_ex(rounded, ExPolygons{clearance_rectangle(39.99, 19.05, 40.01, 19.1)}).empty());
    // Rounded ends also count as material: a real bead crossing a boundary must
    // remain detectable even when its centerline ends exactly on that boundary.
    REQUIRE_FALSE(intersection_ex(rounded, ExPolygons{clearance_rectangle(19.05, 19.99, 19.1, 20.01)}).empty());
    const Polygons too_small{clearance_rectangle(20., 19., 41., 41.).contour};
    REQUIRE_FALSE(diff_ex(rounded, too_small).empty());
    REQUIRE_THROWS_AS(raft_extrusion_coverage(ExtrusionPaths{path}, [] { throw CanceledException(); }), CanceledException);
}

TEST_CASE("Raft geometry disabled input remains exact and planning honors cancellation", "[SupportMaterial][Raft][Geometry]")
{
    const Polygons                bed{clearance_rectangle(0., 0., 100., 100.).contour};
    const std::vector<ExPolygons> input{{clearance_rectangle(10., 10., 90., 90.)}};
    RaftGeometryOptions           options;
    REQUIRE(plan_raft_geometry(input, {0.}, bed, options, 0.4).layers == input);
    options.segmented = true;
    const auto cancel = [] { throw CanceledException(); };
    REQUIRE_THROWS_AS(plan_raft_geometry(input, {0.}, bed, options, 0.4, cancel), CanceledException);
    options.chunk_size = 0.;
    REQUIRE_FALSE(plan_raft_geometry(input, {0.}, bed, options, 0.4).changed);
    REQUIRE_FALSE(plan_raft_geometry(input, {}, bed, options, 0.4).changed);
    REQUIRE(input.front().front() == clearance_rectangle(10., 10., 90., 90.));
}

TEST_CASE("Raft path preparation omits collapsed cut remnants but preserves original small islands", "[SupportMaterial][Raft][Geometry]")
{
    const ExPolygon  main        = clearance_rectangle(10., 10., 20., 20.);
    const ExPolygon  remnant     = clearance_rectangle(25., 10., 25.75, 10.75);
    const ExPolygon  small_model = clearance_rectangle(35., 10., 35.75, 10.75);
    const ExPolygon  narrow      = clearance_rectangle(40., 10., 50., 10.8);
    const ExPolygons sections{main, remnant, small_model, narrow};
    const ExPolygons uncut{clearance_rectangle(0., 0., 30., 30.), small_model, clearance_rectangle(39., 9., 51., 12.)};
    const auto       prepared = prepare_raft_path_regions(sections, uncut, 0.6);
    REQUIRE(prepared.omitted_regions == 1);
    REQUIRE(intersection_ex(prepared.fill_regions, ExPolygons{remnant}).empty());
    REQUIRE_FALSE(intersection_ex(prepared.fill_regions, ExPolygons{small_model}).empty());
    REQUIRE_FALSE(intersection_ex(prepared.fill_regions, ExPolygons{narrow}).empty());
    REQUIRE_FALSE(intersection_ex(prepared.wall_centerlines, ExPolygons{main}).empty());
    const ExPolygons expected{main, small_model, narrow};
    REQUIRE(prepared.fill_regions == expected);
    REQUIRE(diff_ex(prepared.fill_regions, sections).empty());
    // No reconstruction across the gap between distinct assigned sections.
    REQUIRE(intersection_ex(prepared.fill_regions, ExPolygons{clearance_rectangle(20., 10., 25., 20.)}).empty());
    REQUIRE_THROWS_AS(prepare_raft_path_regions(sections, uncut, 0.6, [] { throw CanceledException(); }), CanceledException);
    REQUIRE(prepare_raft_path_regions(sections, uncut, 0.).fill_regions.empty());
    REQUIRE(prepare_raft_path_regions(sections, uncut, std::numeric_limits<double>::infinity()).fill_regions.empty());
    REQUIRE(sections[1] == remnant);
}

TEST_CASE("Raft fragment cleanup preserves long thin sections and only reconstructs affected fill", "[SupportMaterial][Raft][Geometry]")
{
    const ExPolygons uncut{clearance_rectangle(0., 0., 30., 30.)};
    SECTION("A long narrow section survives in different orientations")
    {
        const double angle  = GENERATE(0., PI / 4., PI / 2.);
        ExPolygon    narrow = clearance_rectangle(10., 10., 20., 10.605);
        narrow.rotate(angle, Point::new_scale(15., 10.));
        // At 0.6 mm line width this ten-millimetre strip has less than
        // 0.01 mm² of inset area. It is a long region, not a bead-sized remnant.
        const ExPolygons sections{narrow};
        const auto       prepared = prepare_raft_path_regions(sections, uncut, 0.6);
        REQUIRE(prepared.omitted_regions == 0);
        REQUIRE_FALSE(prepared.wall_centerlines.empty());
        REQUIRE(prepared.fill_regions == sections);
    }
    SECTION("A removed lobe does not return through infill or fill a hole")
    {
        const ExPolygon  main = clearance_rectangle(10., 10., 20., 20.);
        const ExPolygon  lobe = clearance_rectangle(22., 14.6, 22.75, 15.35);
        const ExPolygon  neck = clearance_rectangle(19.9, 14.9, 22.1, 15.1);
        const ExPolygons hole{clearance_rectangle(12., 12., 14., 14.)};
        const ExPolygons sections = diff_ex(union_ex(ExPolygons{main, lobe, neck}), hole);
        REQUIRE(sections.size() == 1);
        const auto prepared = prepare_raft_path_regions(sections, uncut, 0.6);
        REQUIRE(prepared.omitted_regions == 1);
        REQUIRE_FALSE(prepared.fill_regions.empty());
        REQUIRE(intersection_ex(prepared.fill_regions, ExPolygons{lobe}).empty());
        REQUIRE(intersection_ex(prepared.wall_centerlines, ExPolygons{lobe}).empty());
        REQUIRE(intersection_ex(prepared.fill_regions, hole).empty());
        REQUIRE(diff_ex(prepared.fill_regions, sections).empty());
        REQUIRE(diff_ex(ExPolygons{clearance_rectangle(15., 12., 18., 18.)}, prepared.fill_regions).empty());
    }
}

TEST_CASE("Trial raft clipping removes newly cut stubs and keeps existing short extrusion paths", "[SupportMaterial][Raft][Geometry]")
{
    const bool                cleanup = GENERATE(false, true);
    const Polygons            area{clearance_rectangle(0., 0., 10., 10.).contour};
    ExtrusionEntityCollection collection;
    extrusion_entities_append_paths(collection.entities,
                                    Polylines{Polyline{Point::new_scale(-1., 5.), Point::new_scale(0.45, 5.)},
                                              Polyline{Point::new_scale(3., 3.), Point::new_scale(3.015, 3.)},
                                              Polyline{Point::new_scale(-1., 8.), Point::new_scale(8., 8.)}},
                                    erSupportMaterialInterface, 0.12, 0.6f, 0.2f);
    clip_raft_paths_to_area(collection, area, cleanup);
    ExtrusionPaths paths;
    getExtrusionPathsFromEntity(&collection, paths);
    REQUIRE(paths.size() == (cleanup ? 2 : 3));
    bool preserved_short = false, preserved_long = false;
    for (const ExtrusionPath& path : paths) {
        REQUIRE(path.width == Approx(0.6));
        REQUIRE(path.height == Approx(0.2));
        REQUIRE(path.mm3_per_mm == Approx(0.12));
        REQUIRE(path.role() == erSupportMaterialInterface);
        if (path.first_point() == Point::new_scale(3., 3.)) {
            REQUIRE(path.polyline.length() / 1e6 == Approx(0.015));
            preserved_short = true;
        }
        // Open-path clipping may reverse the polyline; its orientation is not
        // part of this containment/flow-preservation contract.
        if (path.first_point() == Point::new_scale(8., 8.) || path.last_point() == Point::new_scale(8., 8.)) {
            REQUIRE(path.polyline.length() / 1e6 == Approx(7.699).margin(0.00001));
            preserved_long = true;
        }
    }
    REQUIRE(preserved_short);
    REQUIRE(preserved_long);
    REQUIRE(diff_ex(raft_extrusion_coverage(paths), area).empty());
}

TEST_CASE("Captured dragon raft walls remain continuous through the final width-aware clip", "[SupportMaterial][Raft][Geometry]")
{
    std::ifstream stream((boost::filesystem::path(TEST_DATA_DIR) / "raft_geometry/dragon-first-raft-outline.json").string());
    REQUIRE(stream.good());
    const auto fixture = nlohmann::json::parse(stream);
    const auto polygon = [](const nlohmann::json& points) {
        Points result;
        for (const auto& point : points)
            result.emplace_back(point[0].get<coord_t>(), point[1].get<coord_t>());
        return Polygon(std::move(result));
    };
    ExPolygons input;
    for (const auto& entry : fixture.at("outlines")) {
        ExPolygon p(polygon(entry.at("contour")));
        for (const auto& hole : entry.at("holes"))
            p.holes.push_back(polygon(hole));
        input.push_back(std::move(p));
    }
    Polygons bed;
    for (const auto& points : fixture.at("bed"))
        bed.push_back(polygon(points));
    const auto plan = plan_raft_geometry({input}, {0.}, bed, RaftGeometryOptions{true, 40., 0.8, 0.}, 0.4);
    REQUIRE(plan.changed);
    REQUIRE(plan.uncut_layers.size() == 1);
    const auto prepared = prepare_raft_path_regions(plan.layers.front(), plan.uncut_layers.front(), 0.6f);
    REQUIRE(prepared.omitted_regions >= 3);
    REQUIRE_FALSE(prepared.wall_centerlines.empty());

    DynamicPrintConfig config = raft_bed_test_config();
    config.set("raft_base_line_width", 0.6);
    Model model;
    add_raft_bed_test_cube(model, 4., Vec3d(40., 40., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    const SupportParameters params(*print.objects().front());
    const Flow&             flow = params.raft_flow(RaftPhase::Base);
    std::unique_ptr<Fill>   filler(Fill::new_from_type(ipRectilinear));
    filler->set_bounding_box(get_extents(input));
    filler->spacing = flow.spacing();
    ExtrusionEntityCollection walls;
    // Isolate the four perimeter loops from legitimate short hatch lines. Zero
    // density with explicit centerlines must not route to the tree generator.
    fill_expolygons_with_sheath_generate_paths(walls.entities, to_polygons(prepared.fill_regions), filler.get(), 0.f, erSupportMaterial,
                                               flow, params, 4, true, 1., ipRectilinear, &prepared.wall_centerlines);
    ExtrusionPaths before, after;
    getExtrusionPathsFromEntity(&walls, before);
    REQUIRE(before.size() >= 8);
    // Use the strict legacy clip here: no short-piece filter can hide a broken
    // loop or make this continuity assertion pass.
    clip_raft_paths_to_area(walls, to_polygons(plan.layers.front()));
    getExtrusionPathsFromEntity(&walls, after);
    REQUIRE(after.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i)
        REQUIRE(after[i].polyline == before[i].polyline);
    REQUIRE(diff_ex(raft_extrusion_coverage(after), offset_ex(plan.layers.front(), scale_(0.002))).empty());
}

namespace {
ModelObject* add_geometry_test_cube(Model& model, double size, const Vec3d& center)
{
    // make_cube uses 0..size coordinates. Position these new fixtures by their
    // actual XY centers, so a seam assertion cannot accidentally check an edge.
    TriangleMesh mesh = make_cube(size, size, 0.8);
    mesh.translate(float(-0.5 * size), float(-0.5 * size), 0.f);
    ModelObject* object = model.add_object();
    object->name        = "raft geometry cube";
    object->add_volume(std::move(mesh));
    object->add_instance()->set_offset(center);
    object->ensure_on_bed();
    return object;
}

ExPolygons geometry_layer_material(const Print& print, size_t index)
{
    Polygons result;
    for (const PrintObject* object : print.objects()) {
        if (object->support_layers().size() <= index)
            continue;
        for (const PrintInstance& instance : object->instances()) {
            Polygons paths = object->support_layers()[index]->support_fills.polygons_covered_by_width();
            for (Polygon& path : paths)
                path.translate(instance.shift);
            polygons_append(result, std::move(paths));
        }
    }
    return union_ex(result);
}

void require_same_geometry_material(const ExPolygons& actual, const ExPolygons& expected)
{
    REQUIRE(diff_ex(actual, expected).empty());
    REQUIRE(diff_ex(expected, actual).empty());
}

ExPolygon geometry_wedge_outline()
{
    // A hole just above the cut intersection creates ~0.76 mm² triangular
    // fragments after diagonal cuts, like the dragon's central raft opening.
    ExPolygon outline = clearance_rectangle(10., 10., 90., 90.);
    Polygon   hole    = clearance_rectangle(20., 52.2, 80., 80.).contour;
    hole.reverse();
    outline.holes.push_back(std::move(hole));
    return outline;
}

std::vector<ExPolygons> geometry_dragon_outlines()
{
    const auto read = [](const char* name) {
        std::ifstream stream((boost::filesystem::path(TEST_DATA_DIR) / "raft_geometry" / name).string());
        REQUIRE(stream.good());
        return nlohmann::json::parse(stream);
    };
    const auto base  = read("dragon-first-raft-outline.json");
    const auto upper = read("dragon-upper-raft-outlines.json");
    REQUIRE(base.at("source_capture_sha256") == upper.at("source_capture_sha256"));
    auto layers = upper.at("upper_layers");
    layers.insert(layers.begin(), base.at("outlines"));
    const auto polygon = [](const nlohmann::json& points) {
        Points result;
        for (const auto& point : points)
            result.emplace_back(point[0].get<coord_t>(), point[1].get<coord_t>());
        return Polygon(std::move(result));
    };
    std::vector<ExPolygons> result;
    for (const auto& layer : layers) {
        ExPolygons outlines;
        for (const auto& entry : layer) {
            ExPolygon p(polygon(entry.at("contour")));
            for (const auto& hole : entry.at("holes"))
                p.holes.push_back(polygon(hole));
            outlines.push_back(std::move(p));
        }
        result.push_back(std::move(outlines));
    }
    return result;
}

void require_connected_raft_zigzags(const SupportLayer& layer)
{
    ExtrusionPaths paths;
    getExtrusionPathsFromEntity(&layer.support_fills, paths);
    REQUIRE_FALSE(paths.empty());
    REQUIRE_FALSE(layer.support_islands.empty());
    double total_length     = 0.;
    double connected_length = 0.;
    for (const ExtrusionPath& path : paths) {
        bool          has_turn = false;
        const Points& points   = path.polyline.points;
        for (size_t i = 2; i < points.size(); ++i) {
            const Vec2d a = (points[i - 1] - points[i - 2]).cast<double>();
            const Vec2d b = (points[i] - points[i - 1]).cast<double>();
            if (std::abs(a.x() * b.y() - a.y() * b.x()) > 0.1 * a.norm() * b.norm())
                has_turn = true;
        }
        total_length += path.polyline.length();
        if (has_turn)
            connected_length += path.polyline.length();

        // A continuous path must stay in ONE chunk, including its swept width.
        // Continuity alone could otherwise be satisfied by bridging the seams.
        const ExPolygons material = raft_extrusion_coverage(ExtrusionPaths{path});
        REQUIRE_FALSE(material.empty());
        REQUIRE(std::any_of(layer.support_islands.begin(), layer.support_islands.end(), [&](const ExPolygon& island) {
            return diff_ex(material, offset_ex(ExPolygons{island}, scale_(0.002))).empty();
        }));
    }
    REQUIRE(total_length > 0.);
    // Allow short end stubs and legitimate disconnected corners; most of the
    // fill must still belong to connected zigzags rather than individual lines.
    REQUIRE(connected_length > 0.9 * total_length);
}

void require_raft_gcode_completes_each_chunk(Print& print)
{
    const PrintObject&             object = *print.objects().front();
    const size_t                   count  = object.slicing_parameters().raft_layers();
    std::vector<std::vector<bool>> visited(count);
    std::vector<size_t>            previous(count, size_t(-1));
    for (size_t i = 0; i < count; ++i)
        visited[i].resize(object.support_layers()[i]->support_islands.size(), false);
    const RaftMergeTemporaryPath output(".gcode");
    struct ResourceGuard
    {
        std::string previous = resources_dir();
        ~ResourceGuard() { set_resources_dir(previous); }
    } resources;
    set_resources_dir((boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path() / "resources").string());
    GCodeProcessorResult result;
    print.export_gcode(output.path.string(), &result, nullptr);
    for (const auto& move : result.moves) {
        if (move.type != EMoveType::Extrude ||
            (move.extrusion_role != erSupportMaterial && move.extrusion_role != erSupportMaterialInterface))
            continue;
        for (size_t i = 0; i < count; ++i) {
            const SupportLayer& layer = *object.support_layers()[i];
            if (std::abs(double(move.position.z()) - layer.print_z) > 0.0001)
                continue;
            const Point point = Point::new_scale(move.position.x(), move.position.y()) - object.instances().front().shift;
            const auto  owner = std::find_if(layer.support_islands.begin(), layer.support_islands.end(),
                                             [&](const ExPolygon& island) { return island.contains(point); });
            REQUIRE(owner != layer.support_islands.end());
            const size_t index = size_t(owner - layer.support_islands.begin());
            CAPTURE(i, index, previous[i]);
            if (previous[i] != index) {
                // Observe actual exported moves, not the in-memory collection
                // order: the exporter is allowed to reorder and reverse paths.
                REQUIRE_FALSE(visited[i][index]);
                visited[i][index] = true;
                previous[i]       = index;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        CAPTURE(i);
        REQUIRE(visited[i].size() > 1);
        REQUIRE(std::all_of(visited[i].begin(), visited[i].end(), [](bool seen) { return seen; }));
    }
}
} // namespace

TEST_CASE("Enhanced raft zigzags retain connected turns and restore when disabled", "[SupportMaterial][Raft][Geometry][Continuity]")
{
    const bool   segmented     = GENERATE(false, true);
    const double surface_width = GENERATE(0., 0.8);
    CAPTURE(segmented, surface_width);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("raft_interface_line_width", 0.);
    config.set("raft_interface_line_spacing", 0.);
    config.set("raft_surface_line_width", surface_width);
    config.set("raft_surface_line_spacing", 0.);
    config.set("raft_interface_wall_count", 0);
    config.set("raft_surface_wall_count", 0);
    Model model;
    add_geometry_test_cube(model, 60., Vec3d(50., 50., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    REQUIRE(print.objects().front()->support_layers().size() == 5);
    std::vector<ExPolygons> baseline;
    for (size_t i = 0; i < 5; ++i)
        baseline.push_back(geometry_layer_material(print, i));
    const double model_z = print.objects().front()->layers().front()->print_z;

    config.set("raft_segmented", segmented);
    config.set("raft_smoothing", segmented ? 0. : 3.);
    process_raft_bed_test(print, model, config);
    REQUIRE(print.objects().front()->support_layers().size() == 5);
    for (size_t i = 1; i < 5; ++i) {
        const SupportLayer& layer = *print.objects().front()->support_layers()[i];
        CAPTURE(i, layer.print_z);
        REQUIRE(layer.raft_merge_backup != nullptr);
        if (segmented)
            REQUIRE(layer.support_islands.size() > 1);
        require_connected_raft_zigzags(layer);
        // These convex chunks need only a few continuous paths each, not one
        // extrusion/retraction cycle per hatch line as in the Benchy failure.
        REQUIRE(layer.support_fills.items_count() <= 2 * layer.support_islands.size());
    }
    REQUIRE(print.objects().front()->layers().front()->print_z == Approx(model_z));
    require_raft_inside_bed(print);

    config.set("raft_segmented", false);
    config.set("raft_smoothing", 0.);
    process_raft_bed_test(print, model, config);
    REQUIRE(print.objects().front()->support_layers().size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(print.objects().front()->support_layers()[i]->raft_merge_backup == nullptr);
        require_same_geometry_material(geometry_layer_material(print, i), baseline[i]);
    }
}

TEST_CASE("Connected raft turns preserve holes cuts bed limits and exported chunk order",
          "[SupportMaterial][Raft][Geometry][Continuity][Ordering][GCode]")
{
    const double bed_size = GENERATE(100., 180.);
    CAPTURE(bed_size);
    const double       origin = 0.5 * (bed_size - 100.);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {bed_size, 0.}, {bed_size, bed_size}, {0., bed_size}}));
    const ExPolygon exclusion = clearance_rectangle(origin + 88., origin + 15., origin + 100., origin + 35.);
    config.set_key_value("bed_exclude_area", new ConfigOptionPoints({{origin + 88., origin + 15.},
                                                                     {origin + 100., origin + 15.},
                                                                     {origin + 100., origin + 35.},
                                                                     {origin + 88., origin + 35.}}));
    config.set("raft_segmented", true);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("raft_interface_line_width", 0.);
    config.set("raft_interface_line_spacing", 0.);
    config.set("raft_surface_line_width", 0.);
    config.set("raft_surface_line_spacing", 0.);
    config.set("raft_interface_wall_count", 0);
    config.set("raft_surface_wall_count", 0);
    config.set("machine_start_gcode", std::string("G90\nM83\nG92 E0"));
    config.set("enable_arc_fitting", false);
    for (const std::string& key : config.keys()) {
        const auto* option     = dynamic_cast<const ConfigOptionEnumsGeneric*>(config.option(key));
        const auto* definition = print_config_def.get(key);
        if (option && !option->keys_map && definition && definition->enum_keys_map) {
            auto* mapped   = new ConfigOptionEnumsGeneric(definition->enum_keys_map);
            mapped->values = option->values;
            config.set_key_value(key, mapped);
        }
    }
    Model model;
    add_geometry_test_cube(model, 40., Vec3d(0.5 * bed_size, 0.5 * bed_size, 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    restore_unmerged_rafts(print);
    PrintObject&            object = *print.get_object(0);
    const SupportParameters params(object);
    const Point             shift   = object.instances().front().shift;
    ExPolygon               outline = clearance_rectangle(origin + 4., origin + 20., origin + 96., origin + 80.);
    const ExPolygon         hole    = clearance_rectangle(origin + 43., origin + 43., origin + 57., origin + 57.);
    outline.holes.push_back(hole.contour);
    outline.holes.back().reverse();
    // Supply a deterministic ring at the same post-support boundary used by
    // existing geometry tests; no support-grid rounding changes the fixture.
    REQUIRE(object.support_layers().size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        SupportLayer&   layer = *object.support_layers()[i];
        const RaftPhase phase = object.slicing_parameters().raft_phase(i);
        const Flow&     flow  = params.raft_flow(phase);
        layer.raft_outline    = {outline};
        translate(layer.raft_outline, -shift);
        layer.support_fills.clear();
        extrusion_entities_append_paths(layer.support_fills.entities,
                                        Polylines{Polyline{Point::new_scale(origin + 15., origin + 25.) - shift,
                                                           Point::new_scale(origin + 20., origin + 25.) - shift}},
                                        phase == RaftPhase::Base ? erSupportMaterial : erSupportMaterialInterface,
                                        flow.mm3_per_mm() * params.raft_flow_ratio(phase), flow.width(), flow.height());
    }
    merge_overlapping_rafts(print);
    for (size_t i = 1; i < 5; ++i) {
        const SupportLayer& layer = *object.support_layers()[i];
        CAPTURE(i, layer.print_z);
        REQUIRE(layer.raft_merge_backup != nullptr);
        REQUIRE(layer.support_islands.size() > 1);
        require_connected_raft_zigzags(layer);
        ExtrusionPaths paths;
        getExtrusionPathsFromEntity(&layer.support_fills, paths);
        ExPolygons material = raft_extrusion_coverage(paths);
        translate(material, shift);
        REQUIRE(intersection_ex(material, ExPolygons{hole, exclusion}).empty());
    }
    require_raft_inside_bed(print);
    REQUIRE(std::any_of(object.support_layers().begin(), object.support_layers().end(),
                        [](const SupportLayer* layer) { return layer->support_fills.items_count() > layer->support_islands.size(); }));
    require_raft_gcode_completes_each_chunk(print);
}

TEST_CASE("Standalone raft zigzags stay connected through stepped centerline notches",
          "[SupportMaterial][Raft][Geometry][Continuity][EndpointOverlap]")
{
    std::ifstream input((boost::filesystem::path(TEST_DATA_DIR) / "raft_geometry" / "benchy-stepped-raft.json").string());
    REQUIRE(input.good());
    const auto fixture = nlohmann::json::parse(input);
    const auto polygon = [](const nlohmann::json& points) {
        Points result;
        for (const auto& point : points)
            result.emplace_back(point[0].get<coord_t>(), point[1].get<coord_t>());
        return Polygon(std::move(result));
    };
    size_t legacy_splits = 0;
    for (const auto& layer : fixture.at("layers")) {
        ExPolygons areas;
        for (const auto& entry : layer.at("areas")) {
            ExPolygon area(polygon(entry.at("contour")));
            for (const auto& hole : entry.at("holes"))
                area.holes.push_back(polygon(hole));
            areas.push_back(std::move(area));
        }
        const Point origin = get_extents(areas).center();
        translate(areas, -origin);
        const double width = layer.at("width");
        // Test both hatch directions, multiple widths and densities, not only
        // the one phase/parity visible in the original GUI screenshot.
        for (double angle : {0., PI / 4., PI / 2., 3. * PI / 4.}) {
            FillZigZag filler;
            filler.set_bounding_box(get_extents(areas));
            filler.angle   = float(angle);
            filler.spacing = layer.at("spacing");
            filler.overlap = std::min(0., 0.5 * filler.spacing - (0.5 * width + 0.003));
            FillParams params;
            params.density      = layer.at("density");
            params.dont_adjust  = true;
            params.pattern      = ipZigZag;
            const auto generate = [&] {
                Polylines paths;
                for (const ExPolygon& area : closing_ex(areas, float(SCALED_EPSILON))) {
                    const Surface surface(stInternal, area);
                    append(paths, filler.fill_surface(&surface, params));
                }
                return paths;
            };
            const Polylines  legacy = generate();
            const ExPolygons bounds = offset_ex(areas, -scale_(0.5 * width + 0.001));
            legacy_splits += intersection_pl(legacy, bounds).size() > legacy.size();
            filler.max_endpoint_overlap = 0.001;
            Polylines paths             = generate();
            CAPTURE(width, angle, params.density, paths.size());
            REQUIRE_FALSE(paths.empty());
            REQUIRE(diff_pl(paths, bounds).empty());
            const double              height = width > 0.5 ? 0.3 : 0.2;
            ExtrusionEntityCollection collection;
            extrusion_entities_append_paths(collection.entities, std::move(paths), erSupportMaterialInterface, 0.1, float(width),
                                            float(height));
            ExtrusionPaths before;
            getExtrusionPathsFromEntity(&collection, before);
            clip_raft_paths_to_area(collection, to_polygons(areas), true);
            ExtrusionPaths after;
            getExtrusionPathsFromEntity(&collection, after);
            REQUIRE(after.size() == before.size());
            for (size_t i = 0; i < before.size(); ++i) {
                REQUIRE(after[i].polyline == before[i].polyline);
                REQUIRE(after[i].width == before[i].width);
                REQUIRE(after[i].height == before[i].height);
                REQUIRE(after[i].mm3_per_mm == before[i].mm3_per_mm);
            }
            // This is an opt-in property: reusing the ordinary filler restores
            // its original output, without changing all support/model infill.
            filler.max_endpoint_overlap = -1.;
            REQUIRE(generate() == legacy);
        }
    }
    // The fixture must expose the old failure, not just be a passing rectangle.
    REQUIRE(legacy_splits > 0);
}

TEST_CASE("Raft base cleanup distinguishes detached remnants from useful narrow and original regions",
          "[SupportMaterial][Raft][Geometry][Bearing]")
{
    const ExPolygon body  = clearance_rectangle(0., 0., 40., 10.);
    const ExPolygon strip = clearance_rectangle(5., 11., 31.7, 12.8);
    ExPolygons      base{body, strip};
    ExPolygons      parents{clearance_rectangle(0., 0., 40., 30.)};
    ExPolygons      demand{clearance_rectangle(2., 2., 10., 8.)};
    bool            remove_strip = true;
    SECTION("only the detached strip is removed") {}
    SECTION("original independent small island stays")
    {
        const ExPolygon small = clearance_rectangle(50., 5., 51.2, 6.2);
        base.push_back(small);
        parents.push_back(small);
    }
    SECTION("nominal upper demand protects sparse infill gaps")
    {
        demand.push_back(clearance_rectangle(15., 11.2, 16., 12.6));
        remove_strip = false;
    }
    SECTION("touching upper demand is conservatively retained")
    {
        demand.push_back(clearance_rectangle(15., 12.8, 16., 14.));
        remove_strip = false;
    }
    SECTION("an attached normal base extension stays")
    {
        base.push_back(clearance_rectangle(5., 9., 6., 12.));
        remove_strip = false;
    }
    SECTION("an upper base layer can connect a useful lower base strip")
    {
        // The caller projects every base layer together, so the bridge may be
        // in another physical base layer; do not judge the bottom layer alone.
        base.push_back(clearance_rectangle(5., 9., 31.7, 12.8));
        remove_strip = false;
    }
    SECTION("unknown provenance stays")
    {
        parents      = {body};
        remove_strip = false;
    }
    SECTION("a whole uncut separate strip stays")
    {
        parents      = base;
        remove_strip = false;
    }
    SECTION("no known useful sibling does not erase a source island")
    {
        demand       = {clearance_rectangle(60., 60., 70., 70.)};
        remove_strip = false;
    }
    SECTION("missing demand stays")
    {
        demand.clear();
        remove_strip = false;
    }
    const ExPolygons removed = find_unneeded_raft_base_regions(base, parents, demand);
    REQUIRE(removed.empty() == !remove_strip);
    if (remove_strip) {
        REQUIRE(removed.size() == 1);
        REQUIRE(diff_ex(ExPolygons{strip}, removed).empty());
        REQUIRE(intersection_ex(removed, ExPolygons{body}).empty());
    }
    REQUIRE_THROWS_AS(find_unneeded_raft_base_regions(base, parents, demand, [] { throw CanceledException(); }), CanceledException);
}

TEST_CASE("Captured dragon raft loses its unused base strip while retaining a strip with demand",
          "[SupportMaterial][Raft][Geometry][Bearing]")
{
    const auto     input = geometry_dragon_outlines();
    const Polygons bed{clearance_rectangle(0., 0., 101., 101.).contour};
    const auto     plan = plan_raft_geometry(input, {0., 0.3, 0.3, 0.21, 0.21}, bed, {true, 40., 0.8, 0.}, 0.4);
    REQUIRE(plan.changed);
    const ExPolygons probe{clearance_rectangle(30., 51.1, 40., 52.3)};
    REQUIRE_FALSE(intersection_ex(plan.layers[0], probe).empty());
    ExPolygons demand;
    for (size_t i = 1; i < input.size(); ++i)
        demand.insert(demand.end(), input[i].begin(), input[i].end());
    const ExPolygons removed = find_unneeded_raft_base_regions(plan.layers[0], plan.uncut_layers[0], demand);
    REQUIRE_FALSE(intersection_ex(removed, probe).empty());
    REQUIRE(intersection_ex(diff_ex(plan.layers[0], removed), probe).empty());
    REQUIRE(intersection_ex(removed, demand).empty());
    // Same long strip, same size: any real bearing demand must retain it.
    demand.push_back(probe.front());
    REQUIRE(intersection_ex(find_unneeded_raft_base_regions(plan.layers[0], plan.uncut_layers[0], demand), probe).empty());
}

TEST_CASE("Base remnant cleanup updates paths and islands across merged base stacks and restores source data",
          "[SupportMaterial][Raft][Geometry][Bearing]")
{
    const int  base_count        = GENERATE(1, 2);
    const int  middle_count      = GENERATE(0, 2);
    const bool shared            = GENERATE(false, true);
    const bool model_needs_strip = GENERATE(false, true);
    CAPTURE(base_count, middle_count, shared, model_needs_strip);
    const auto         input  = geometry_dragon_outlines();
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {101., 0.}, {101., 101.}, {0., 101.}}));
    config.set("raft_segmented", true);
    config.set("raft_base_layers", base_count);
    config.set("raft_interface_layers", middle_count);
    config.set("raft_surface_layers", 2);
    config.set("raft_base_layer_height", 0.3);
    config.set("raft_interface_layer_height", 0.3);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_base_line_width", 0.6);
    config.set("raft_interface_line_width", 0.6);
    config.set("raft_surface_line_width", 0.42);
    config.set("raft_base_wall_count", 4);
    config.set("raft_interface_wall_count", 0);
    config.set("raft_surface_wall_count", 0);
    Model model;
    add_geometry_test_cube(model, 1., model_needs_strip ? Vec3d(35., 51.7, 0.) : Vec3d(20., 20., 0.));
    if (shared)
        add_geometry_test_cube(model, 1., Vec3d(75., 75., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    restore_unmerged_rafts(print);
    for (size_t oi = 0; oi < print.objects().size(); ++oi) {
        PrintObject&            object = *print.get_object(oi);
        const SupportParameters params(object);
        const Point             shift = object.instances().front().shift;
        REQUIRE(object.support_layers().size() == size_t(base_count + middle_count + 2));
        // As in the existing wedge test, inject the captured nominal outlines
        // with a valid source recipe. Overlapping sources exercise single-owner
        // output; this fixture does not claim the small cube has dragon geometry.
        for (size_t i = 0; i < object.support_layers().size(); ++i) {
            SupportLayer& layer = *object.support_layers()[i];
            const auto    phase = object.slicing_parameters().raft_phase(i);
            const Flow&   flow  = params.raft_flow(phase);
            layer.raft_outline  = input[phase == RaftPhase::Base ? 0 : phase == RaftPhase::Interface ? 1 : 3];
            translate(layer.raft_outline, -shift);
            layer.support_fills.clear();
            const double y = phase == RaftPhase::Base ? 51.7 : 20.;
            extrusion_entities_append_paths(layer.support_fills.entities,
                                            Polylines{Polyline{Point::new_scale(30., y) - shift, Point::new_scale(40., y) - shift}},
                                            phase == RaftPhase::Base ? erSupportMaterial : erSupportMaterialInterface,
                                            flow.mm3_per_mm() * params.raft_flow_ratio(phase), flow.width(), flow.height());
        }
    }
    const ExPolygons probe{clearance_rectangle(30., 51.1, 40., 52.3)};
    const ExPolygons original = geometry_layer_material(print, 0);
    REQUIRE_FALSE(intersection_ex(original, probe).empty());
    merge_overlapping_rafts(print);
    std::vector<ExPolygons> first_result;
    for (size_t i = 0; i < size_t(base_count + middle_count + 2); ++i) {
        const ExPolygons material = geometry_layer_material(print, i);
        REQUIRE_FALSE(material.empty());
        first_result.push_back(material);
        size_t     owners = 0;
        ExPolygons islands;
        for (const PrintObject* object : print.objects()) {
            const SupportLayer& layer = *object->support_layers()[i];
            REQUIRE(layer.raft_merge_backup != nullptr);
            owners += !layer.support_fills.empty();
            ExPolygons local = layer.support_islands;
            translate(local, object->instances().front().shift);
            islands.insert(islands.end(), local.begin(), local.end());
        }
        REQUIRE(owners == 1);
        if (i < size_t(base_count)) {
            REQUIRE(intersection_ex(material, probe).empty() == !model_needs_strip);
            REQUIRE(intersection_ex(islands, probe).empty() == !model_needs_strip);
        }
    }
    // Covered recipients deliberately have no extrusion. Validate the shared
    // owner paths, bed containment and disjointness with the merger helper.
    require_shared_rafts_are_disjoint(print);
    merge_overlapping_rafts(print);
    for (size_t i = 0; i < first_result.size(); ++i)
        require_same_geometry_material(geometry_layer_material(print, i), first_result[i]);
    restore_unmerged_rafts(print);
    require_same_geometry_material(geometry_layer_material(print, 0), original);
}

TEST_CASE("Small cut fragments do not cancel otherwise valid raft sections", "[SupportMaterial][Raft][Geometry]")
{
    const ExPolygons          outline{geometry_wedge_outline()};
    const Polygons            bed{clearance_rectangle(0., 0., 100., 100.).contour};
    const RaftGeometryOptions options{true, 40., 0.8, 0.};
    const auto                result = plan_raft_geometry({outline}, {0.}, bed, options, 0.01);
    REQUIRE(result.changed);
    REQUIRE(result.layer_cuts.front() == 4);
    REQUIRE(result.layers.front().size() > 8);
    REQUIRE(intersection_ex(result.layers.front(), ExPolygons{clearance_rectangle(49.8, 10., 50.2, 90.)}).empty());
    REQUIRE(intersection_ex(result.layers.front(), ExPolygons{clearance_rectangle(10., 49.8, 90., 50.2)}).empty());
}

TEST_CASE("A walled raft emits printable sections without rolling back for omitted fragments", "[SupportMaterial][Raft][Geometry]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("raft_segmented", true);
    config.set("raft_base_line_width", 0.6);
    config.set("raft_base_wall_count", 4);
    config.set("raft_interface_wall_count", 0);
    config.set("raft_surface_wall_count", 0);
    Model model;
    add_geometry_test_cube(model, 60., Vec3d(50., 50., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    restore_unmerged_rafts(print);
    PrintObject&            object = *print.get_object(0);
    const SupportParameters params(object);
    const Point             shift = object.instances().front().shift;
    // Inject deterministic raft contours into the post-support stage. Keep a
    // valid source extrusion recipe so this exercises the real regeneration,
    // clipping and commit checks, without depending on support grid rounding.
    for (size_t i = 0; i < object.support_layers().size(); ++i) {
        SupportLayer& layer = *object.support_layers()[i];
        const auto    phase = object.slicing_parameters().raft_phase(i);
        const Flow&   flow  = params.raft_flow(phase);
        layer.raft_outline  = {geometry_wedge_outline()};
        translate(layer.raft_outline, -shift);
        layer.support_fills.clear();
        extrusion_entities_append_paths(layer.support_fills.entities,
                                        Polylines{Polyline{Point::new_scale(15., 15.) - shift, Point::new_scale(20., 15.) - shift}},
                                        phase == RaftPhase::Base ? erSupportMaterial : erSupportMaterialInterface,
                                        flow.mm3_per_mm() * params.raft_flow_ratio(phase), flow.width(), flow.height());
    }
    merge_overlapping_rafts(print);
    for (size_t i = 0; i < object.support_layers().size(); ++i) {
        const SupportLayer& layer = *object.support_layers()[i];
        REQUIRE(layer.raft_merge_backup != nullptr);
        REQUIRE(layer.support_islands.size() >= 8);
        const ExPolygons material = union_ex(layer.support_fills.polygons_covered_by_width());
        for (const ExPolygon& island : layer.support_islands)
            REQUIRE_FALSE(intersection_ex(ExPolygons{island}, material).empty());
        const ExPolygons placed = geometry_layer_material(print, i);
        REQUIRE(intersection_ex(placed, ExPolygons{clearance_rectangle(49.8, 10., 50.2, 90.)}).empty());
        REQUIRE(intersection_ex(placed, ExPolygons{clearance_rectangle(10., 49.8, 90., 50.2)}).empty());
    }
    require_raft_inside_bed(print);
}

TEST_CASE("A single object's segmented raft restores exactly when disabled and survives cache replay", "[SupportMaterial][Raft][Geometry]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    Model              model;
    add_geometry_test_cube(model, 60., Vec3d(50., 50., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    const ExPolygons baseline = geometry_layer_material(print, 2);
    REQUIRE_FALSE(intersection_ex(baseline, ExPolygons{clearance_rectangle(49.85, 49.85, 50.15, 50.15)}).empty());
    const double model_z = print.objects().front()->layers().front()->print_z;
    config.set("raft_segmented", true);
    process_raft_bed_test(print, model, config);
    REQUIRE(print.objects().front()->support_layers().front()->raft_merge_backup != nullptr);
    const ExPolygons segmented = geometry_layer_material(print, 2);
    REQUIRE_FALSE(diff_ex(baseline, segmented).empty());
    REQUIRE(intersection_ex(segmented, ExPolygons{clearance_rectangle(49.85, 49.85, 50.15, 50.15)}).empty());
    REQUIRE(print.objects().front()->layers().front()->print_z == Approx(model_z));
    require_raft_inside_bed(print);
    print.process();
    require_same_geometry_material(geometry_layer_material(print, 2), segmented);

    const RaftMergeTemporaryPath cache;
    REQUIRE(print.export_cached_data(cache.path.string()) == 0);
    Print loaded;
    loaded.apply(model, config);
    REQUIRE(loaded.validate().string.empty());
    loaded.set_status_silent();
    REQUIRE(loaded.load_cached_data(cache.path.string()) == 0);
    require_same_geometry_material(geometry_layer_material(loaded, 2), baseline);
    merge_overlapping_rafts(loaded);
    require_same_geometry_material(geometry_layer_material(loaded, 2), segmented);
    loaded.process(nullptr, true);
    require_same_geometry_material(geometry_layer_material(loaded, 2), segmented);

    config.set("raft_segmented", false);
    process_raft_bed_test(print, model, config);
    REQUIRE(print.objects().front()->support_layers().front()->raft_merge_backup == nullptr);
    require_same_geometry_material(geometry_layer_material(print, 2), baseline);
}

TEST_CASE("Segmented rafts remain inside 100 mm and EP7 excluded bed regions", "[SupportMaterial][Raft][Geometry]")
{
    const bool         ep7    = GENERATE(false, true);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("raft_segmented", true);
    if (ep7) {
        config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {180., 0.}, {180., 180.}, {0., 180.}}));
        config.set_key_value("bed_exclude_area", new ConfigOptionPoints({{169., 0.}, {180., 0.}, {180., 23.}, {169., 23.}}));
        config.set("raft_chunk_size", 20.);
    }
    Model model;
    add_geometry_test_cube(model, ep7 ? 30. : 98., ep7 ? Vec3d(153., 40., 0.) : Vec3d(50., 50., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    REQUIRE(print.objects().front()->support_layers().front()->raft_merge_backup != nullptr);
    require_raft_inside_bed(print);
}

TEST_CASE("Raft smoothing alone reaches generated paths and restores the original raft", "[SupportMaterial][Raft][Geometry]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    TriangleMesh       mesh   = make_cube(40., 20., 0.8);
    mesh.merge(make_cube(20., 40., 0.8));
    Model        model;
    ModelObject* object = model.add_object();
    object->add_volume(std::move(mesh));
    object->center_around_origin();
    object->add_instance()->set_offset(Vec3d(50., 50., 0.));
    object->ensure_on_bed();
    Print print;
    process_raft_bed_test(print, model, config);
    const ExPolygons    baseline = geometry_layer_material(print, 2);
    const SupportLayer* first    = print.objects().front()->support_layers().front();
    const ExPolygons    outline  = union_ex(first->raft_outline, first->support_fills.polygons_covered_by_width());
    const double        model_z  = print.objects().front()->layers().front()->print_z;

    config.set("raft_smoothing", 1.);
    process_raft_bed_test(print, model, config);
    first = print.objects().front()->support_layers().front();
    REQUIRE(first->raft_merge_backup != nullptr);
    REQUIRE_FALSE(diff_ex(first->support_islands, outline).empty());
    REQUIRE_FALSE(diff_ex(outline, first->support_islands).empty());
    REQUIRE(print.objects().front()->layers().front()->print_z == Approx(model_z));
    require_raft_inside_bed(print);

    config.set("raft_smoothing", 0.);
    process_raft_bed_test(print, model, config);
    REQUIRE(print.objects().front()->support_layers().front()->raft_merge_backup == nullptr);
    require_same_geometry_material(geometry_layer_material(print, 2), baseline);
}

TEST_CASE("Shared segmented rafts split merged outlines and restore after moving a neighbour", "[SupportMaterial][Raft][Geometry]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("raft_segmented", true);
    config.set("raft_chunk_size", 20.);
    Model model;
    for (const Vec3d& position : {Vec3d(30., 30., 0.), Vec3d(41., 30., 0.), Vec3d(30., 41., 0.), Vec3d(41., 41., 0.)})
        add_geometry_test_cube(model, 8., position);
    Print print;
    process_raft_bed_test(print, model, config);
    REQUIRE(raft_path_owners(print, 2) == 1);
    const ExPolygons material = geometry_layer_material(print, 2);
    REQUIRE(intersection_ex(material, ExPolygons{clearance_rectangle(35.35, 35.35, 35.65, 35.65)}).empty());
    require_shared_rafts_are_disjoint(print);
    model.objects.back()->instances.front()->set_offset(Vec3d(75., 75., 0.));
    process_raft_bed_test(print, model, config);
    REQUIRE(raft_path_owners(print, 2) == 2);
    REQUIRE(print.objects().back()->support_layers().front()->raft_merge_backup != nullptr);
    REQUIRE(print.objects().back()->support_layers().front()->support_islands.size() == 1);
    require_shared_rafts_are_disjoint(print);
}

TEST_CASE("Different geometry settings retain the existing shared raft instead of restoring duplicates", "[SupportMaterial][Raft][Geometry]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    Model              model;
    ModelObject*       first = add_geometry_test_cube(model, 8., Vec3d(30., 40., 0.));
    add_geometry_test_cube(model, 8., Vec3d(41., 40., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    const ExPolygons baseline = geometry_layer_material(print, 2);
    first->config.set("raft_segmented", true);
    first->config.set("raft_chunk_size", 5.);
    process_raft_bed_test(print, model, config);
    REQUIRE(raft_path_owners(print, 2) == 1);
    require_same_geometry_material(geometry_layer_material(print, 2), baseline);
    require_shared_rafts_are_disjoint(print);
}

TEST_CASE("Raft geometry trial retains multi-instance sources", "[SupportMaterial][Raft][Geometry]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    Model        model;
    ModelObject* object = add_geometry_test_cube(model, 20., Vec3d(25., 25., 0.));
    object->add_instance()->set_offset(Vec3d(65., 65., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    const ExPolygons baseline = geometry_layer_material(print, 2);
    config.set("raft_segmented", true);
    config.set("raft_chunk_size", 10.);
    process_raft_bed_test(print, model, config);
    require_same_geometry_material(geometry_layer_material(print, 2), baseline);
    for (const PrintObject* source : print.objects())
        REQUIRE(source->support_layers().front()->raft_merge_backup == nullptr);
}

TEST_CASE("Raft geometry parameters round trip and do not change legacy defaults", "[SupportMaterial][Raft][Geometry]")
{
    const DynamicPrintConfig defaults = DynamicPrintConfig::full_print_config();
    REQUIRE_FALSE(defaults.opt_bool("raft_segmented"));
    REQUIRE(defaults.opt_float("raft_smoothing") == 0.);
    REQUIRE(defaults.opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    DynamicPrintConfig source = defaults;
    source.set("raft_segmented", true);
    source.set("raft_chunk_size", 35.);
    source.set("raft_chunk_gap", 0.6);
    source.set("raft_smoothing", 1.);
    DynamicPrintConfig loaded = defaults;
    for (const std::string key : {"raft_segmented", "raft_chunk_size", "raft_chunk_gap", "raft_smoothing"}) {
        REQUIRE(print_config_def.get(key) != nullptr);
        loaded.set_deserialize_strict(key, source.opt_serialize(key));
        REQUIRE(loaded.opt_serialize(key) == source.opt_serialize(key));
    }
}

TEST_CASE("Raft geometry does not bypass actual clearance errors", "[SupportMaterial][Raft][Geometry][Clearance]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("raft_segmented", true);
    Model model;
    add_geometry_test_cube(model, 60., Vec3d(45., 45., 0.));
    ModelObject* unrafted = add_geometry_test_cube(model, 2., Vec3d(76.5, 60., 0.));
    disable_test_raft(*unrafted);
    Print print;
    REQUIRE_THROWS_WITH(process_raft_bed_test(print, model, config), Catch::Matchers::Contains("has raft disabled"));
}

namespace {
ExtrusionPaths tagged_raft_paths(const ExtrusionEntityCollection& fills, const std::vector<unsigned char>& flags, bool raft)
{
    REQUIRE(flags.size() == fills.entities.size());
    ExtrusionEntityCollection selected;
    ExtrusionPaths            paths;
    for (size_t i = 0; i < fills.entities.size(); ++i)
        if (bool(flags[i]) == raft)
            selected.append(*fills.entities[i]);
    getExtrusionPathsFromEntity(&selected, paths);
    return paths;
}

void require_same_support_paths(const ExtrusionPaths& actual, const ExtrusionPaths& expected)
{
    REQUIRE(actual.size() == expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        REQUIRE(actual[i].polyline == expected[i].polyline);
        REQUIRE(actual[i].role() == expected[i].role());
        REQUIRE(actual[i].width == expected[i].width);
        REQUIRE(actual[i].height == expected[i].height);
        REQUIRE(actual[i].mm3_per_mm == expected[i].mm3_per_mm);
    }
}

void require_supported_raft_inside_bed(const Print& print)
{
    Polygons    bed    = diff(Polygons{Polygon(get_bed_shape(print.config()))}, get_bed_excluded_area(print.config()));
    const Vec3d origin = print.get_plate_origin();
    for (auto& polygon : bed)
        polygon.translate(Point::new_scale(origin.x(), origin.y()));
    for (const PrintObject* object : print.objects())
        for (const SupportLayer* layer : object->support_layers()) {
            if (layer->print_z > object->slicing_parameters().raft_contact_top_z + EPSILON)
                continue;
            ExtrusionPaths paths;
            getExtrusionPathsFromEntity(&layer->support_fills, paths);
            const ExPolygons coverage = raft_extrusion_coverage(paths);
            for (const auto& instance : object->instances()) {
                ExPolygons placed = coverage;
                translate(placed, instance.shift);
                REQUIRE(diff_ex(placed, offset_ex(bed, scale_(0.0001))).empty());
            }
        }
}
} // namespace

TEST_CASE("Support generators label raft paths and preserve ordinary supports during geometry enhancement",
          "[SupportMaterial][Raft][Geometry][Supported]")
{
    const auto style        = GENERATE(smsGrid, smsSnug, smsTreeOrganic, smsTreeSlim, smsTreeStrong, smsTreeHybrid);
    const bool segmentation = GENERATE(false, true);
    CAPTURE(style, segmentation);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("enable_support", true);
    config.set("raft_segmented", segmentation);
    config.set("raft_chunk_size", 10.);
    config.set("raft_smoothing", 1.);
    config.set("support_threshold_angle", 30);
    config.set("support_top_z_distance", 0.2);
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(style));
    config.set_key_value("support_type",
                         new ConfigOptionEnum<SupportType>(style == smsGrid || style == smsSnug ? stNormalAuto : stTreeAuto));
    TriangleMesh mesh = Slic3r::Test::mesh(TestMesh::cube_20x20x20);
    mesh.rotate_x(float(Geometry::deg2rad(15.)));
    Model model;
    auto* object = model.add_object();
    object->add_volume(std::move(mesh));
    object->add_instance()->set_offset(Vec3d(45., 45., 0.));
    object->ensure_on_bed();
    Print print;
    process_raft_bed_test(print, model, config);
    const PrintObject& printed = *print.objects().front();
    REQUIRE(printed.support_layers().size() > printed.slicing_parameters().raft_layers());
    size_t                      enhanced = 0;
    std::vector<ExtrusionPaths> original_support, original_paths;
    for (const SupportLayer* layer : printed.support_layers()) {
        // Compare restoration with this generation's source. Independently
        // generated tree supports may differ in traversal and path subdivision.
        const auto& source = layer->raft_merge_backup ? layer->raft_merge_backup->fills : layer->support_fills;
        original_paths.emplace_back();
        getExtrusionPathsFromEntity(&source, original_paths.back());
        if (layer->raft_merge_backup) {
            ++enhanced;
            REQUIRE(layer->has_raft_provenance());
            const auto& backup   = *layer->raft_merge_backup;
            const auto  ordinary = tagged_raft_paths(backup.fills, backup.entity_flags, false);
            require_same_support_paths(tagged_raft_paths(layer->support_fills, layer->raft_entity_flags, false), ordinary);
            original_support.push_back(ordinary);
            const auto material = raft_extrusion_coverage(tagged_raft_paths(layer->support_fills, layer->raft_entity_flags, true));
            REQUIRE(intersection_ex(material, raft_extrusion_coverage(ordinary)).empty());
        } else {
            ExtrusionPaths ordinary;
            getExtrusionPathsFromEntity(&layer->support_fills, ordinary);
            original_support.push_back(std::move(ordinary));
        }
    }
    REQUIRE(enhanced == printed.slicing_parameters().raft_layers());
    // Every ordinary path retains its owner, coordinates, Z/height and flow on replay.
    merge_overlapping_rafts(print);
    for (size_t i = 0; i < printed.support_layers().size(); ++i) {
        const SupportLayer& layer = *printed.support_layers()[i];
        ExtrusionPaths      ordinary;
        if (layer.raft_merge_backup)
            ordinary = tagged_raft_paths(layer.support_fills, layer.raft_entity_flags, false);
        else
            getExtrusionPathsFromEntity(&layer.support_fills, ordinary);
        require_same_support_paths(ordinary, original_support[i]);
    }
    require_supported_raft_inside_bed(print);

    restore_unmerged_rafts(print);
    for (size_t i = 0; i < printed.support_layers().size(); ++i) {
        const SupportLayer& layer = *printed.support_layers()[i];
        REQUIRE(layer.raft_merge_backup == nullptr);
        ExtrusionPaths paths;
        getExtrusionPathsFromEntity(&layer.support_fills, paths);
        require_same_support_paths(paths, original_paths[i]);
    }
    // Exercise configuration invalidation while the enhanced paths are active.
    merge_overlapping_rafts(print);
    REQUIRE(printed.support_layers().front()->raft_merge_backup != nullptr);

    config.set("raft_segmented", false);
    config.set("raft_smoothing", 0.);
    REQUIRE(print.objects().front()->is_step_done(posSimplifySupportPath));
    print.apply(model, config);
    REQUIRE_FALSE(print.objects().front()->is_step_done(posSupportMaterial));
    REQUIRE_FALSE(print.objects().front()->is_step_done(posSimplifySupportPath));
    process_raft_bed_test(print, model, config);
    Print baseline;
    process_raft_bed_test(baseline, model, config);
    REQUIRE(print.objects().front()->support_layers().size() == baseline.objects().front()->support_layers().size());
    for (size_t i = 0; i < baseline.objects().front()->support_layers().size(); ++i) {
        const SupportLayer& restored = *print.objects().front()->support_layers()[i];
        const SupportLayer& fresh    = *baseline.objects().front()->support_layers()[i];
        REQUIRE(restored.raft_merge_backup == nullptr);
        REQUIRE(restored.print_z == fresh.print_z);
        REQUIRE(restored.support_fills.empty() == fresh.support_fills.empty());
        // Grid/Snug are deterministic for this fixture. Tree path equality is
        // checked against its own source above, not a separate parallel run.
        if (style == smsGrid || style == smsSnug)
            require_same_geometry_material(union_ex(restored.support_fills.polygons_covered_by_width()),
                                           union_ex(fresh.support_fills.polygons_covered_by_width()));
    }
    require_supported_raft_inside_bed(print);
}

TEST_CASE("Raft masks use each support path height instead of the layer's deepest extrusion",
          "[SupportMaterial][Raft][Geometry][Supported][Height]")
{
    // The thin path is above, touching, or genuinely penetrating the raft top.
    // The deep column shares its print Z and its broad layer envelope.
    const double penetration = GENERATE(-0.002, 0., 0.01);
    CAPTURE(penetration);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("enable_support", true);
    config.set("raft_segmented", true);
    config.set("raft_chunk_size", 10.);
    config.set("raft_smoothing", 1.);
    Model model;
    add_geometry_test_cube(model, 20., Vec3d(50., 50., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    restore_unmerged_rafts(print);
    PrintObject& object      = *print.get_object(0);
    const size_t raft_layers = object.slicing_parameters().raft_layers();
    REQUIRE(object.support_layers().size() == raft_layers);
    const double raft_top = object.slicing_parameters().raft_contact_top_z;
    const Point  shift    = object.instances().front().shift;
    auto*        support  = object.add_support_layer(int(raft_layers), int(raft_layers), 0.005, raft_top + 0.005);
    const auto   append   = [&](double x1, double y1, double x2, double y2, double height) {
        const Flow flow(0.6f, float(height), 0.4f);
        extrusion_entities_append_paths(support->support_fills.entities,
                                            Polylines{Polyline{Point::new_scale(x1, y1) - shift, Point::new_scale(x2, y2) - shift}},
                                            erSupportMaterial, flow.mm3_per_mm(), flow.width(), flow.height());
    };
    append(35., 46., 65., 46., 0.005 + penetration);
    append(54., 35., 54., 65., 0.205);
    support->support_islands = {clearance_rectangle(25., 25., 75., 75.)};
    translate(support->support_islands, -shift);
    std::vector<ExtrusionPaths> original(object.support_layers().size());
    for (size_t i = 0; i < original.size(); ++i)
        getExtrusionPathsFromEntity(&object.support_layers()[i]->support_fills, original[i]);
    const auto& ordinary = original.back();
    REQUIRE(ordinary.size() == 2);
    for (size_t replay = 0; replay < 2; ++replay) {
        merge_overlapping_rafts(print);
        ExtrusionPaths preserved;
        getExtrusionPathsFromEntity(&support->support_fills, preserved);
        require_same_support_paths(preserved, ordinary);
        REQUIRE(support->raft_merge_backup == nullptr);
        for (size_t i = 0; i < raft_layers; ++i) {
            const SupportLayer& layer = *object.support_layers()[i];
            REQUIRE(layer.raft_merge_backup != nullptr);
            const auto paths = tagged_raft_paths(layer.support_fills, layer.raft_entity_flags, true);
            REQUIRE_FALSE(paths.empty());
            const auto material = raft_extrusion_coverage(paths);
            for (const auto& path : ordinary) {
                const double overlap = std::min(layer.print_z, support->print_z) -
                                       std::max(layer.print_z - double(paths.front().height), support->print_z - double(path.height));
                if (overlap > EPSILON)
                    REQUIRE(intersection_ex(material, raft_extrusion_coverage(ExtrusionPaths{path})).empty());
            }
            if (i + 1 == raft_layers) {
                // Tangency/air above the raft is legal: retaining XY coverage
                // proves the fix did not just keep the excessive old mask.
                const bool covers_thin = !intersection_ex(material, raft_extrusion_coverage(ExtrusionPaths{ordinary.front()})).empty();
                REQUIRE(covers_thin == (penetration <= 0.));
            }
        }
        require_supported_raft_inside_bed(print);
    }
    restore_unmerged_rafts(print);
    for (size_t i = 0; i < original.size(); ++i) {
        ExtrusionPaths restored;
        getExtrusionPathsFromEntity(&object.support_layers()[i]->support_fills, restored);
        require_same_support_paths(restored, original[i]);
    }
}

TEST_CASE("Tilted Grid-supported cube keeps raft enhancement with modulated contact paths",
          "[SupportMaterial][Raft][Geometry][Supported][Height]")
{
    const double bed_size   = GENERATE(100., 180.);
    const double chunk_size = GENERATE(20., 40.);
    CAPTURE(bed_size, chunk_size);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {bed_size, 0.}, {bed_size, bed_size}, {0., bed_size}}));
    config.set("enable_support", true);
    config.set("initial_layer_print_height", 0.24);
    config.set("line_width", 0.42);
    config.set("support_line_width", 0.42);
    config.set("support_threshold_angle", 30);
    config.set("support_top_z_distance", 0.2);
    config.set("support_interface_top_layers", 3);
    config.set("support_interface_bottom_layers", 0);
    config.set("support_object_xy_distance", 0.35);
    config.set("support_object_first_layer_gap", 0.2);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set("raft_base_layer_height", 0.3);
    config.set("raft_interface_layer_height", 0.3);
    config.set("raft_surface_layer_height", 0.2);
    config.set("raft_base_margin", 3.);
    config.set("raft_interface_margin", 2.);
    config.set("raft_surface_margin", 1.5);
    for (const char* key : {"raft_base_line_width", "raft_interface_line_width", "raft_surface_line_width", "raft_base_line_spacing",
                            "raft_interface_line_spacing", "raft_surface_line_spacing"})
        config.set(key, 0.);
    config.set("raft_airgap", 0.27);
    config.set("raft_layer_0_z_overlap", 0.1);
    config.set("raft_segmented", true);
    config.set("raft_chunk_size", chunk_size);
    config.set("raft_smoothing", 1.);
    TriangleMesh mesh = make_cube(20., 20., 16.);
    mesh.rotate_x(float(Geometry::deg2rad(25.)));
    Model model;
    auto* object = model.add_object();
    object->add_volume(std::move(mesh));
    object->add_instance()->set_offset(Vec3d(0.5 * bed_size, 0.5 * bed_size, 0.));
    object->ensure_on_bed();
    Print print;
    process_raft_bed_test(print, model, config);
    const PrintObject&          printed           = *print.objects().front();
    size_t                      enhanced          = 0;
    bool                        modulated_contact = false;
    const double                raft_top          = printed.slicing_parameters().raft_contact_top_z;
    std::vector<ExtrusionPaths> original, ordinary;
    for (const SupportLayer* layer : printed.support_layers()) {
        const auto& source = layer->raft_merge_backup ? layer->raft_merge_backup->fills : layer->support_fills;
        original.emplace_back();
        getExtrusionPathsFromEntity(&source, original.back());
        ordinary.emplace_back();
        if (layer->raft_merge_backup) {
            ++enhanced;
            const auto& backup = *layer->raft_merge_backup;
            ordinary.back()    = tagged_raft_paths(backup.fills, backup.entity_flags, false);
            require_same_support_paths(tagged_raft_paths(layer->support_fills, layer->raft_entity_flags, false), ordinary.back());
            REQUIRE_FALSE(tagged_raft_paths(layer->support_fills, layer->raft_entity_flags, true).empty());
        } else
            getExtrusionPathsFromEntity(&layer->support_fills, ordinary.back());
        bool above = false, below = false;
        for (const auto& path : ordinary.back()) {
            above |= layer->print_z - double(path.height) >= raft_top - EPSILON;
            below |= layer->print_z - double(path.height) < raft_top - EPSILON;
        }
        modulated_contact |= above && below;
    }
    REQUIRE(modulated_contact);
    REQUIRE(enhanced == printed.slicing_parameters().raft_layers());
    // Compare actual physical intervals across different support print Zs.
    for (const SupportLayer* layer : printed.support_layers()) {
        if (!layer->raft_merge_backup)
            continue;
        const auto raft_paths = tagged_raft_paths(layer->support_fills, layer->raft_entity_flags, true);
        const auto material   = raft_extrusion_coverage(raft_paths);
        for (size_t i = 0; i < ordinary.size(); ++i)
            for (const auto& path : ordinary[i])
                if (std::min(layer->print_z, printed.support_layers()[i]->print_z) >
                    std::max(layer->print_z - double(raft_paths.front().height),
                             printed.support_layers()[i]->print_z - double(path.height)) +
                        EPSILON)
                    REQUIRE(intersection_ex(material, raft_extrusion_coverage(ExtrusionPaths{path})).empty());
    }
    require_supported_raft_inside_bed(print);
    restore_unmerged_rafts(print);
    for (size_t i = 0; i < original.size(); ++i) {
        ExtrusionPaths restored;
        getExtrusionPathsFromEntity(&printed.support_layers()[i]->support_fills, restored);
        require_same_support_paths(restored, original[i]);
    }
}

TEST_CASE("Raft provenance preserves identical-flow columns across cuts cache and cancellation",
          "[SupportMaterial][Raft][Geometry][Supported]")
{
    const double       bed_size = GENERATE(100., 180.);
    DynamicPrintConfig config   = raft_bed_test_config();
    config.set("enable_support", true);
    config.set("raft_segmented", true);
    config.set("raft_smoothing", 1.);
    config.set("raft_base_layers", 2);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_layers", 2);
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {bed_size, 0.}, {bed_size, bed_size}, {0., bed_size}}));
    Model model;
    add_geometry_test_cube(model, 60., Vec3d(50., 50., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    restore_unmerged_rafts(print);
    PrintObject&                object = *print.get_object(0);
    const SupportParameters     params(object);
    std::vector<ExtrusionPaths> original, ordinary;
    for (size_t i = 0; i < object.support_layers().size(); ++i) {
        SupportLayer& layer = *object.support_layers()[i];
        REQUIRE(layer.has_raft_provenance());
        const auto  phase = object.slicing_parameters().raft_phase(i);
        const Flow& flow  = params.raft_flow(phase);
        // Deliberately indistinguishable by flow/role; only provenance can
        // preserve this column across the centre of the radial cuts.
        extrusion_entities_append_paths(layer.support_fills.entities,
                                        Polylines{Polyline{Point::new_scale(50., 23.) - object.instances().front().shift,
                                                           Point::new_scale(50., 77.) - object.instances().front().shift}},
                                        phase == RaftPhase::Base ? erSupportMaterial : erSupportMaterialInterface,
                                        flow.mm3_per_mm() * params.raft_flow_ratio(phase), flow.width(), flow.height());
        layer.raft_entity_flags.resize(layer.support_fills.entities.size(), 0);
        ordinary.push_back(tagged_raft_paths(layer.support_fills, layer.raft_entity_flags, false));
        original.emplace_back();
        getExtrusionPathsFromEntity(&layer.support_fills, original.back());
    }
    merge_overlapping_rafts(print);
    for (size_t i = 0; i < object.support_layers().size(); ++i) {
        const SupportLayer& layer = *object.support_layers()[i];
        REQUIRE(layer.raft_merge_backup != nullptr);
        require_same_support_paths(tagged_raft_paths(layer.support_fills, layer.raft_entity_flags, false), ordinary[i]);
        REQUIRE(intersection_ex(raft_extrusion_coverage(tagged_raft_paths(layer.support_fills, layer.raft_entity_flags, true)),
                                raft_extrusion_coverage(ordinary[i]))
                    .empty());
    }
    require_raft_inside_bed(print);
    const RaftMergeTemporaryPath cache;
    REQUIRE(print.export_cached_data(cache.path.string()) == 0);
    Print loaded;
    loaded.apply(model, config);
    loaded.set_status_silent();
    REQUIRE(loaded.load_cached_data(cache.path.string()) == 0);
    merge_overlapping_rafts(loaded);
    for (size_t i = 0; i < ordinary.size(); ++i) {
        const auto& layer = *loaded.objects().front()->support_layers()[i];
        REQUIRE(layer.raft_merge_backup != nullptr);
        require_same_support_paths(tagged_raft_paths(layer.support_fills, layer.raft_entity_flags, false), ordinary[i]);
    }
    print.cancel();
    REQUIRE_THROWS_AS(merge_overlapping_rafts(print), CanceledException);
    print.restart();
    for (size_t i = 0; i < original.size(); ++i) {
        ExtrusionPaths      paths;
        const SupportLayer& layer = *object.support_layers()[i];
        REQUIRE(layer.raft_merge_backup == nullptr);
        getExtrusionPathsFromEntity(&layer.support_fills, paths);
        require_same_support_paths(paths, original[i]);
    }
    // Unknown/old cache metadata must never be guessed from a matching flow.
    object.support_layers()[1]->raft_entity_flags.clear();
    merge_overlapping_rafts(print);
    for (size_t i = 0; i < original.size(); ++i) {
        ExtrusionPaths      paths;
        const SupportLayer& layer = *object.support_layers()[i];
        REQUIRE(layer.raft_merge_backup == nullptr);
        getExtrusionPathsFromEntity(&layer.support_fills, paths);
        require_same_support_paths(paths, original[i]);
    }
}

TEST_CASE("Shared enhanced raft emits once while ordinary columns keep their object owners",
          "[SupportMaterial][Raft][Geometry][Supported][Merge]")
{
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("enable_support", true);
    config.set("raft_segmented", true);
    config.set("raft_chunk_size", 15.);
    Model model;
    add_geometry_test_cube(model, 20., Vec3d(38., 50., 0.));
    add_geometry_test_cube(model, 20., Vec3d(60., 50., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    restore_unmerged_rafts(print);
    std::vector<std::vector<ExtrusionPaths>> ordinary(print.objects().size());
    for (size_t o = 0; o < print.objects().size(); ++o) {
        PrintObject&            object = *print.get_object(o);
        const SupportParameters params(object);
        for (size_t i = 0; i < object.support_layers().size(); ++i) {
            SupportLayer& layer = *object.support_layers()[i];
            REQUIRE(layer.has_raft_provenance());
            const auto   phase = object.slicing_parameters().raft_phase(i);
            const Flow&  flow  = params.raft_flow(phase);
            const double x     = o == 0 ? 35. : 63.;
            extrusion_entities_append_paths(layer.support_fills.entities,
                                            Polylines{Polyline{Point::new_scale(x, 41.) - object.instances().front().shift,
                                                               Point::new_scale(x, 59.) - object.instances().front().shift}},
                                            phase == RaftPhase::Base ? erSupportMaterial : erSupportMaterialInterface,
                                            flow.mm3_per_mm() * params.raft_flow_ratio(phase), flow.width(), flow.height());
            layer.raft_entity_flags.resize(layer.support_fills.entities.size(), 0);
            ordinary[o].push_back(tagged_raft_paths(layer.support_fills, layer.raft_entity_flags, false));
        }
    }
    for (size_t replay = 0; replay < 3; ++replay) {
        merge_overlapping_rafts(print);
        for (size_t i = 0; i < ordinary.front().size(); ++i) {
            size_t     raft_owners = 0;
            ExPolygons raft_material, support_material;
            for (size_t o = 0; o < print.objects().size(); ++o) {
                const PrintObject&  object = *print.get_object(o);
                const SupportLayer& layer  = *object.support_layers()[i];
                REQUIRE(layer.raft_merge_backup != nullptr);
                REQUIRE(layer.has_raft_provenance());
                REQUIRE_FALSE(layer.raft_covered_by_shared_paths);
                require_same_support_paths(tagged_raft_paths(layer.support_fills, layer.raft_entity_flags, false), ordinary[o][i]);
                const auto raft_paths = tagged_raft_paths(layer.support_fills, layer.raft_entity_flags, true);
                raft_owners += !raft_paths.empty();
                ExPolygons raft_area    = raft_extrusion_coverage(raft_paths);
                ExPolygons support_area = raft_extrusion_coverage(ordinary[o][i]);
                translate(raft_area, object.instances().front().shift);
                translate(support_area, object.instances().front().shift);
                raft_material    = union_ex(raft_material, raft_area);
                support_material = union_ex(support_material, support_area);
            }
            REQUIRE(raft_owners == 1);
            REQUIRE(intersection_ex(raft_material, support_material).empty());
        }
        require_supported_raft_inside_bed(print);
    }
}

namespace {
// Synchronous observation injects cancellation after real work has begun, with
// no sleeps, worker races, or test-only switches in the production API. Missing
// stage messages fail the test instead of silently testing pre-cancellation.
class ScopedRaftLogObserver
{
    class Backend : public boost::log::sinks::basic_sink_backend<boost::log::sinks::synchronized_feeding>
    {
    public:
        explicit Backend(std::function<void(const std::string&)> callback) : callback(std::move(callback)) {}
        void consume(const boost::log::record_view& record)
        {
            const auto message = record.attribute_values()["Message"].extract<std::string>();
            if (message)
                callback(message.get());
        }
        std::function<void(const std::string&)> callback;
    };
    using Sink = boost::log::sinks::synchronous_sink<Backend>;
    unsigned                previous_level;
    boost::shared_ptr<Sink> sink;

public:
    explicit ScopedRaftLogObserver(std::function<void(const std::string&)> callback)
        : previous_level(get_logging_level()), sink(boost::make_shared<Sink>(boost::make_shared<Backend>(std::move(callback))))
    {
        boost::log::core::get()->add_sink(sink);
        set_logging_level(3);
    }
    ~ScopedRaftLogObserver()
    {
        boost::log::core::get()->remove_sink(sink);
        set_logging_level(previous_level);
    }
    ScopedRaftLogObserver(const ScopedRaftLogObserver&)            = delete;
    ScopedRaftLogObserver& operator=(const ScopedRaftLogObserver&) = delete;
};
} // namespace

TEST_CASE("Cancellation during raft enhancement leaves all source paths intact and permits recovery",
          "[SupportMaterial][Raft][Geometry][Stability][Cancel]")
{
    const std::string stage     = GENERATE(std::string("[Raft][Geometry] options,"), std::string("[Raft][Geometry] phase planning,"),
                                           std::string("[Raft][Geometry] status=applied,"));
    const bool        supported = GENERATE(false, true);
    CAPTURE(stage, supported);
    DynamicPrintConfig config = raft_bed_test_config();
    config.set("enable_support", supported);
    config.set("raft_segmented", true);
    config.set("raft_chunk_size", 15.);
    config.set("raft_smoothing", 1.);
    Model model;
    add_geometry_test_cube(model, 20., Vec3d(38., 50., 0.));
    add_geometry_test_cube(model, 20., Vec3d(60., 50., 0.));
    Print print;
    process_raft_bed_test(print, model, config);
    restore_unmerged_rafts(print);
    struct Source
    {
        SupportLayer*              layer;
        ExtrusionPaths             paths;
        ExPolygons                 islands;
        std::vector<unsigned char> flags;
    };
    std::vector<Source> sources;
    for (size_t o = 0; o < print.objects().size(); ++o) {
        PrintObject&            object = *print.get_object(o);
        const SupportParameters params(object);
        for (size_t i = 0; i < object.support_layers().size(); ++i) {
            SupportLayer& layer = *object.support_layers()[i];
            if (supported) {
                REQUIRE(layer.has_raft_provenance());
                const auto   phase = object.slicing_parameters().raft_phase(i);
                const Flow&  flow  = params.raft_flow(phase);
                const double x     = o == 0 ? 35. : 63.;
                extrusion_entities_append_paths(layer.support_fills.entities,
                                                Polylines{Polyline{Point::new_scale(x, 41.) - object.instances().front().shift,
                                                                   Point::new_scale(x, 59.) - object.instances().front().shift}},
                                                phase == RaftPhase::Base ? erSupportMaterial : erSupportMaterialInterface,
                                                flow.mm3_per_mm() * params.raft_flow_ratio(phase), flow.width(), flow.height());
                layer.raft_entity_flags.resize(layer.support_fills.entities.size(), 0);
            }
            Source source{&layer, {}, layer.support_islands, layer.raft_entity_flags};
            getExtrusionPathsFromEntity(&layer.support_fills, source.paths);
            sources.push_back(std::move(source));
        }
    }
    bool triggered    = false;
    auto requested_at = std::chrono::steady_clock::time_point{};
    {
        ScopedRaftLogObserver observer([&](const std::string& message) {
            if (!triggered && message.find(stage) == 0) {
                triggered    = true;
                requested_at = std::chrono::steady_clock::now();
                print.cancel();
            }
        });
        REQUIRE_THROWS_AS(merge_overlapping_rafts(print), CanceledException);
    }
    REQUIRE(triggered);
    const double response_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - requested_at).count();
    // Report the measurement; avoid machine-dependent timing assertions.
    WARN("raft cancellation stage=" << stage << ", supported=" << supported << ", response_ms=" << response_ms);
    for (const Source& source : sources) {
        const SupportLayer& layer = *source.layer;
        REQUIRE(layer.raft_merge_backup == nullptr);
        REQUIRE_FALSE(layer.raft_covered_by_shared_paths);
        REQUIRE(layer.raft_entity_flags == source.flags);
        require_same_geometry_material(layer.support_islands, source.islands);
        ExtrusionPaths actual;
        getExtrusionPathsFromEntity(&layer.support_fills, actual);
        require_same_support_paths(actual, source.paths);
    }
    print.restart();
    merge_overlapping_rafts(print);
    REQUIRE(print.objects().front()->support_layers().front()->raft_merge_backup != nullptr);
    require_shared_rafts_are_disjoint(print);
    require_supported_raft_inside_bed(print);
    restore_unmerged_rafts(print);
    for (const Source& source : sources) {
        ExtrusionPaths actual;
        getExtrusionPathsFromEntity(&source.layer->support_fills, actual);
        require_same_support_paths(actual, source.paths);
    }
}

TEST_CASE("Raft geometry callbacks cancel partway through planning and coverage without changing inputs",
          "[SupportMaterial][Raft][Geometry][Stability][Cancel]")
{
    const Polygons                bed{clearance_rectangle(0., 0., 180., 180.).contour};
    const ExPolygons              outline{clearance_rectangle(10., 10., 160., 80.)};
    const std::vector<ExPolygons> layers(3, outline);
    const RaftGeometryOptions     options{true, 20., 0.8, 1.};
    const auto                    baseline = plan_raft_geometry(layers, {0., 0.3, 0.25}, bed, options, 0.4);
    REQUIRE(baseline.changed);
    const auto check_midflight = [&](const auto& operation) {
        size_t total = 0;
        operation([&] { ++total; });
        REQUIRE(total > 2);
        size_t       visited   = 0;
        const size_t cancel_at = std::max(size_t(2), total / 2);
        REQUIRE_THROWS_AS(operation([&] {
                              if (++visited == cancel_at)
                                  throw CanceledException();
                          }),
                          CanceledException);
        REQUIRE(visited == cancel_at);
        size_t recovered = 0;
        operation([&] { ++recovered; });
        REQUIRE(recovered == total);
    };
    check_midflight([&](const std::function<void()>& cancel) { plan_raft_geometry(layers, {0., 0.3, 0.25}, bed, options, 0.4, cancel); });
    check_midflight([&](const std::function<void()>& cancel) {
        prepare_raft_path_regions(baseline.layers.front(), baseline.uncut_layers.front(), 0.4, cancel);
    });
    ExtrusionPaths paths;
    for (int i = 0; i < 12; ++i) {
        ExtrusionPath path(erSupportMaterial, 0.08, 0.4, 0.2);
        path.polyline = Polyline{Point::new_scale(20., 20. + i), Point::new_scale(80., 20. + i)};
        paths.push_back(std::move(path));
    }
    const auto original_paths = paths;
    check_midflight([&](const std::function<void()>& cancel) { raft_extrusion_coverage(paths, cancel); });
    require_same_support_paths(paths, original_paths);
    const auto recovered = plan_raft_geometry(layers, {0., 0.3, 0.25}, bed, options, 0.4);
    for (size_t i = 0; i < layers.size(); ++i) {
        require_same_geometry_material(layers[i], outline);
        require_same_geometry_material(recovered.layers[i], baseline.layers[i]);
    }
}

TEST_CASE("Shared rafts survive sixteen configuration and placement changes with cache recovery",
          "[SupportMaterial][Raft][Geometry][Stability][Replay]")
{
    const double       bed_size = GENERATE(100., 180.);
    DynamicPrintConfig config   = raft_bed_test_config();
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {bed_size, 0.}, {bed_size, bed_size}, {0., bed_size}}));
    Model model;
    add_geometry_test_cube(model, 20., Vec3d(38., 50., 0.));
    ModelObject* second = add_geometry_test_cube(model, 20., Vec3d(60., 50., 0.));
    Print        print;
    for (size_t iteration = 0; iteration < 16; ++iteration) {
        CAPTURE(bed_size, iteration);
        config.set("raft_segmented", iteration % 4 >= 2);
        config.set("raft_smoothing", iteration % 2 ? 1. : 0.);
        config.set("raft_chunk_size", iteration % 3 ? 15. : 20.);
        config.set("raft_chunk_gap", iteration % 3 ? 0.8 : 0.6);
        second->instances.front()->set_offset(Vec3d(iteration % 3 ? 60. : 78., 50., 0.));
        second->invalidate_bounding_box();
        process_raft_bed_test(print, model, config);
        Print fresh;
        process_raft_bed_test(fresh, model, config);
        REQUIRE(all_raft_volume(print) > 0.);
        REQUIRE(all_raft_volume(print) == Approx(all_raft_volume(fresh)));
        for (const PrintObject* object : print.objects())
            REQUIRE(object->support_layers().size() == object->slicing_parameters().raft_layers());
        for (size_t layer = 0; layer < 3; ++layer) {
            const ExPolygons material = geometry_layer_material(print, layer);
            REQUIRE_FALSE(material.empty());
            require_same_geometry_material(material, geometry_layer_material(fresh, layer));
            REQUIRE(raft_path_owners(print, layer) == raft_path_owners(fresh, layer));
        }
        // Shared recipients may have no local paths. This helper checks their
        // coverage flag, physical overlap and deposited widths against the bed.
        require_shared_rafts_are_disjoint(print);
        if (iteration == 7) {
            const RaftMergeTemporaryPath cache;
            REQUIRE(print.export_cached_data(cache.path.string()) == 0);
            Print loaded;
            loaded.apply(model, config);
            loaded.set_status_silent();
            REQUIRE(loaded.load_cached_data(cache.path.string()) == 0);
            loaded.process(nullptr, true);
            require_shared_rafts_are_disjoint(loaded);
            for (size_t layer = 0; layer < 3; ++layer)
                require_same_geometry_material(geometry_layer_material(loaded, layer), geometry_layer_material(fresh, layer));
        }
    }
    REQUIRE(model.delete_object(second));
    process_raft_bed_test(print, model, config);
    Print fresh;
    process_raft_bed_test(fresh, model, config);
    REQUIRE(print.objects().size() == 1);
    require_raft_inside_bed(print);
    require_same_geometry_material(geometry_layer_material(print, 2), geometry_layer_material(fresh, 2));
}
