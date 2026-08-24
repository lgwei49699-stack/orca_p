#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>

#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Support/RaftPlan.hpp"
#include "libslic3r/Support/SupportParameters.hpp"
#include "libslic3r/Utils.hpp"

#include "test_data.hpp" // get access to init_print, etc

using namespace Slic3r::Test;
using namespace Slic3r;

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

TEST_CASE("Cura V1 raft validates a shared multi-object Z schedule", "[SupportMaterial][Raft][CuraV1][Validation]")
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
    ModelObject *first_object = add_cube("first Cura raft object", 80.);
    ModelObject *second_object = add_cube("second Cura raft object", 120.);

    const char *expected_key = nullptr;
    SECTION("matching schedules remain valid") {}
    SECTION("a Cura raft cannot share the plate with an unrafted legacy object")
    {
        // Put the unrafted object first to characterize the m_objects.front()
        // hazard in plate-level skirt/brim/wipe-tower height selection.
        first_object->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
        expected_key = "raft_mode";
    }
    SECTION("resolved Base heights must match")
    {
        second_object->config.set("raft_base_layer_height", 0.25);
        expected_key = "raft_base_layer_height";
    }

    Print print;
    print.auto_assign_extruders(first_object);
    print.auto_assign_extruders(second_object);
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

TEST_CASE("Cura V1 raft overlap separates enclosed transition bottoms from external bridges",
          "[SupportMaterial][Raft][CuraV1][TransitionBottom]")
{
    struct LayerTwoRoles {
        bool   has_bottom {false};
        bool   has_bridge {false};
        bool   has_bottom_fill {false};
        bool   has_bridge_fill {false};
        bool   l1_has_hole {false};
        bool   l1_holes_clear {true};
        double bottom_area {0.};
        double bridge_area {0.};
        size_t l1_contact_collections {0};
        size_t l1_closed_bottom_paths {0};
    };

    const auto slice_fixture = [](RaftMode raft_mode, double overlap) {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
        config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(raft_mode));
        config.set("layer_height", 0.2);
        config.set("initial_layer_print_height", 0.24);
        config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.5, false));
        config.set("raft_airgap", 0.27);
        config.set("raft_layer_0_z_overlap", overlap);
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
        config.set("raft_layers", 3);
        config.set("before_layer_change_gcode", std::string("G92 E0"));

        // Model layer 0 is a 20x20 mm frame with a closed 12x12 mm recess.
        // Model layer 1 closes that recess and also extends 4 mm beyond both
        // outer sides. The recess is the raft transition bottom; the two tabs
        // are genuine external bridges and must keep their bridge role.
        TriangleMesh fixture = make_cube(20., 4., 0.24);
        const auto merge_box = [&fixture](double size_x, double size_y, double size_z, double x, double y, double z) {
            TriangleMesh box = make_cube(size_x, size_y, size_z);
            box.translate(Vec3f(float(x), float(y), float(z)));
            fixture.merge(box);
        };
        merge_box(20., 4., 0.24, 0., 16., 0.);
        merge_box(4., 12., 0.24, 0., 4., 0.);
        merge_box(4., 12., 0.24, 16., 4., 0.);
        merge_box(28., 20., 1.0, -4., 0., 0.24);
        // A disconnected tower gives the first model layer a second island.
        // Together with the frame hole this exercises Contact Skin topology
        // without introducing a large external fixture.
        merge_box(5., 5., 1.0, 30., 0., 0.);

        Model model;
        ModelObject *model_object = model.add_object();
        model_object->name = "Cura raft transition bottom and external bridge fixture";
        model_object->add_volume(std::move(fixture));
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
        REQUIRE(object->layers().size() >= 2);
        const LayerRegion *first_layer_region = object->layers()[0]->get_region(0);
        const SurfaceCollection &surfaces = object->layers()[1]->get_region(0)->fill_surfaces;
        LayerTwoRoles roles;
        for (const Surface &surface : first_layer_region->fill_surfaces.surfaces)
            roles.l1_has_hole |= surface.surface_type == stBottom && !surface.expolygon.holes.empty();

        for (const ExtrusionEntity *entity : first_layer_region->fills.entities) {
            const auto *collection = dynamic_cast<const ExtrusionEntityCollection *>(entity);
            if (collection != nullptr && collection->no_sort &&
                std::any_of(collection->entities.begin(), collection->entities.end(), [](const ExtrusionEntity *path) {
                    return path->role() == erBottomSurface && path->length() > 0. &&
                           path->first_point() == path->last_point();
                }))
                ++roles.l1_contact_collections;
        }

        Polygons l1_bottom_coverage;
        const ExtrusionEntityCollection first_layer_flattened = first_layer_region->fills.flatten();
        for (const ExtrusionEntity *entity : first_layer_flattened.entities) {
            if (entity->role() != erBottomSurface)
                continue;
            roles.l1_closed_bottom_paths +=
                entity->length() > 0. && entity->first_point() == entity->last_point() ? 1 : 0;
            entity->polygons_covered_by_width(l1_bottom_coverage, 0.f);
        }
        const ExPolygons covered_bottom = union_ex(l1_bottom_coverage);
        for (const Surface &surface : first_layer_region->fill_surfaces.surfaces) {
            if (surface.surface_type != stBottom)
                continue;
            for (const Polygon &hole : surface.expolygon.holes) {
                const Point hole_center = hole.bounding_box().center();
                roles.l1_holes_clear &= std::none_of(
                    covered_bottom.begin(), covered_bottom.end(),
                    [&hole_center](const ExPolygon &covered) { return covered.contains(hole_center); });
            }
        }

        for (const Surface &surface : surfaces.surfaces) {
            const double area = std::abs(surface.expolygon.area());
            if (surface.surface_type == stBottom) {
                roles.has_bottom = true;
                roles.bottom_area += area;
            } else if (surface.surface_type == stBottomBridge) {
                roles.has_bridge = true;
                roles.bridge_area += area;
            }
        }
        const ExtrusionEntityCollection flattened = object->layers()[1]->get_region(0)->fills.flatten();
        for (const ExtrusionEntity *entity : flattened.entities) {
            roles.has_bottom_fill |= entity->role() == erBottomSurface;
            roles.has_bridge_fill |= entity->role() == erBridgeInfill || entity->role() == erInternalBridgeInfill;
        }
        return roles;
    };

    const LayerTwoRoles cura_transition = slice_fixture(RaftMode::CuraV1, 0.1);
    REQUIRE(cura_transition.has_bottom);
    REQUIRE(cura_transition.has_bridge);
    REQUIRE(cura_transition.has_bottom_fill);
    REQUIRE(cura_transition.has_bridge_fill);
    REQUIRE(cura_transition.bottom_area > 0.);
    REQUIRE(cura_transition.bridge_area > 0.);
    REQUIRE(cura_transition.l1_has_hole);
    REQUIRE(cura_transition.l1_holes_clear);
    REQUIRE(cura_transition.l1_contact_collections >= 2);
    REQUIRE(cura_transition.l1_closed_bottom_paths >= 3);

    // Both guards are compatibility boundaries: without positive overlap, or
    // in Legacy mode, the enclosed layer-0 recess retains the historical
    // BottomBridge classification together with the external tabs.
    const LayerTwoRoles cura_without_overlap = slice_fixture(RaftMode::CuraV1, 0.0);
    REQUIRE_FALSE(cura_without_overlap.has_bottom);
    REQUIRE(cura_without_overlap.has_bridge);

    const LayerTwoRoles legacy = slice_fixture(RaftMode::Legacy, 0.1);
    REQUIRE_FALSE(legacy.has_bottom);
    REQUIRE(legacy.has_bridge);
}

TEST_CASE("Cura V1 raft contact skin keeps an outline and retracts hatch from acute tips",
          "[SupportMaterial][Raft][CuraV1][ContactSkin]")
{
    struct TipMetrics {
        double hatch_tip_x {-1.e9};
        double outline_tip_x {-1.e9};
        double longest_contour_segment {0.};
        double gcode_bottom_length {-1.};
        size_t closed_bottom_paths {0};
        size_t bottom_surface_loops {0};
        bool   contact_skin_ordered {false};
        bool   bottom_paths_inside_surface {true};
        bool   gcode_first_bottom_path_closed {false};
        bool   gcode_bottom_speed_correct {true};
    };

    const auto slice_pointed_prism = [](RaftMode raft_mode, InfillPattern pattern = ipMonotonic, bool symmetric = false,
                                        double seam_gap = -1.) {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
        config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(raft_mode));
        config.set("layer_height", 0.2);
        config.set("initial_layer_print_height", 0.24);
        config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.52, false));
        config.set_key_value("bottom_surface_pattern", new ConfigOptionEnum<InfillPattern>(pattern));
        config.set_key_value("top_bottom_infill_wall_overlap", new ConfigOptionPercent(25.));
        config.set("symmetric_infill_y_axis", symmetric);
        config.set("solid_infill_direction", 45.);
        config.set("initial_layer_infill_speed", 17.);
        config.set("enable_arc_fitting", false);
        if (seam_gap >= 0.)
            config.set_key_value("seam_gap", new ConfigOptionFloatOrPercent(seam_gap, false));
        config.set("wall_loops", 2);
        config.set("bottom_shell_layers", 3);
        config.set("top_shell_layers", 3);
        config.set("raft_airgap", 0.27);
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
        config.set("raft_layers", 3);
        config.set("support_filament", 1);
        config.set("support_interface_filament", 1);
        config.set("support_top_z_distance", 0.2);
        config.set("before_layer_change_gcode", std::string("G92 E0"));

        // Synthetic full configs contain enum-vector defaults without their
        // serialization maps. Normal presets provide these maps before G-code
        // export.
        for (const std::string &key : config.keys()) {
            const auto *enum_option = dynamic_cast<const ConfigOptionEnumsGeneric *>(config.option(key));
            const ConfigOptionDef *option_def = print_config_def.get(key);
            if (enum_option != nullptr && enum_option->keys_map == nullptr && option_def != nullptr &&
                option_def->enum_keys_map != nullptr) {
                auto *mapped_option = new ConfigOptionEnumsGeneric(option_def->enum_keys_map);
                mapped_option->values = enum_option->values;
                config.set_key_value(key, mapped_option);
            }
        }

        // A triangular prism approximates an acute hull tip without relying
        // on a large Benchy fixture. Its sloped sides are 26.565 degrees;
        // the configured bottom hatch is 45 degrees.
        const float height = 1.f;
        std::vector<Vec3f> vertices {
            {0.f, 0.f, 0.f}, {0.f, 20.f, 0.f}, {20.f, 10.f, 0.f},
            {0.f, 0.f, height}, {0.f, 20.f, height}, {20.f, 10.f, height}
        };
        std::vector<Vec3i32> facets {
            {0, 1, 2}, {3, 5, 4},
            {0, 3, 4}, {0, 4, 1},
            {1, 4, 5}, {1, 5, 2},
            {2, 5, 3}, {2, 3, 0}
        };

        Model model;
        ModelObject *model_object = model.add_object();
        model_object->name = "Cura raft acute contact skin fixture";
        model_object->add_volume(TriangleMesh(vertices, facets));
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
        REQUIRE_FALSE(object->layers().empty());
        const LayerRegion *layer_region = object->layers().front()->get_region(0);
        const ExtrusionEntityCollection &fills = layer_region->fills;
        const ExtrusionEntityCollection flattened = fills.flatten();

        ExPolygons bottom_surfaces;
        for (const Surface &surface : layer_region->fill_surfaces.surfaces)
            if (surface.surface_type == stBottom)
                bottom_surfaces.emplace_back(surface.expolygon);

        TipMetrics metrics;
        const auto is_closed_bottom_path = [](const ExtrusionEntity *path) {
            return path->role() == erBottomSurface && path->length() > 0. && path->first_point() == path->last_point();
        };
        for (const ExtrusionEntity *entity : fills.entities) {
            const auto *contact_skin = dynamic_cast<const ExtrusionEntityCollection *>(entity);
            if (contact_skin == nullptr || !contact_skin->no_sort || contact_skin->entities.size() < 2)
                continue;
            metrics.contact_skin_ordered =
                is_closed_bottom_path(contact_skin->entities.front()) &&
                std::any_of(
                    contact_skin->entities.begin() + 1, contact_skin->entities.end(),
                    [&is_closed_bottom_path](const ExtrusionEntity *path) {
                        return path->role() == erBottomSurface && !is_closed_bottom_path(path);
                    });
            if (metrics.contact_skin_ordered)
                break;
        }

        size_t bottom_path_count = 0;
        for (const ExtrusionEntity *entity : flattened.entities) {
            if (entity->role() != erBottomSurface)
                continue;
            const bool closed_path = is_closed_bottom_path(entity);
            metrics.closed_bottom_paths += closed_path ? 1 : 0;
            metrics.bottom_surface_loops += entity->is_loop() ? 1 : 0;
            Polylines polylines;
            entity->collect_polylines(polylines);
            for (const Polyline &polyline : polylines) {
                ++bottom_path_count;
                for (const Point &point : polyline.points) {
                    metrics.outline_tip_x = std::max(metrics.outline_tip_x, unscale<double>(point.x()));
                    if (closed_path && !std::any_of(
                            bottom_surfaces.begin(), bottom_surfaces.end(),
                            [&point](const ExPolygon &surface) { return surface.contains(point); }))
                        metrics.bottom_paths_inside_surface = false;
                }
                for (size_t point_id = 1; point_id < polyline.points.size(); ++point_id) {
                    const Vec2d delta = (polyline.points[point_id] - polyline.points[point_id - 1]).cast<double>();
                    const double segment_length = unscale<double>(delta.norm());
                    if (segment_length < 0.05)
                        continue;
                    const double angle = std::atan2(std::abs(delta.y()), std::abs(delta.x()));
                    if (std::abs(angle - Geometry::deg2rad(45.)) < Geometry::deg2rad(3.)) {
                        metrics.hatch_tip_x = std::max(
                            metrics.hatch_tip_x,
                            std::max(unscale<double>(polyline.points[point_id - 1].x()),
                                     unscale<double>(polyline.points[point_id].x())));
                    }
                    if (std::abs(angle - std::atan(0.5)) < Geometry::deg2rad(3.))
                        metrics.longest_contour_segment = std::max(metrics.longest_contour_segment, segment_length);
                }
            }
        }
        REQUIRE(bottom_path_count > 0);
        if (pattern != ipConcentric && pattern != ipConcentricInternal)
            REQUIRE(metrics.hatch_tip_x > -1.e8);

        if (seam_gap >= 0.) {
            const boost::filesystem::path gcode_path =
                boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orca-contact-skin-%%%%-%%%%.gcode");
            set_resources_dir((boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path() / "resources").string());
            GCodeProcessorResult gcode_result;
            print.export_gcode(gcode_path.string(), &gcode_result, nullptr);
            boost::filesystem::remove(gcode_path);

            metrics.gcode_bottom_length = 0.;
            const float model_first_layer_z = float(object->layers().front()->print_z);
            bool first_bottom_started = false;
            bool first_bottom_finished = false;
            Vec2f first_bottom_start = Vec2f::Zero();
            Vec2f first_bottom_end = Vec2f::Zero();
            for (size_t move_id = 1; move_id < gcode_result.moves.size(); ++move_id) {
                const GCodeProcessorResult::MoveVertex &move = gcode_result.moves[move_id];
                const bool is_model_first_layer_bottom =
                    move.type == EMoveType::Extrude && move.extrusion_role == erBottomSurface &&
                    std::abs(move.position.z() - model_first_layer_z) < 1.e-4f;
                if (is_model_first_layer_bottom) {
                    const Vec2f previous = gcode_result.moves[move_id - 1].position.head<2>();
                    metrics.gcode_bottom_length += double((move.position.head<2>() - previous).norm());
                    metrics.gcode_bottom_speed_correct &= std::abs(move.feedrate - 17.f) < 1.e-3f;
                    if (!first_bottom_started) {
                        first_bottom_started = true;
                        first_bottom_start = previous;
                    }
                    if (!first_bottom_finished)
                        first_bottom_end = move.position.head<2>();
                } else if (first_bottom_started && !first_bottom_finished) {
                    metrics.gcode_first_bottom_path_closed = (first_bottom_end - first_bottom_start).norm() < 0.02f;
                    first_bottom_finished = true;
                }
            }
            if (first_bottom_started && !first_bottom_finished)
                metrics.gcode_first_bottom_path_closed = (first_bottom_end - first_bottom_start).norm() < 0.02f;
            REQUIRE(first_bottom_started);
        }
        return metrics;
    };

    const TipMetrics cura   = slice_pointed_prism(RaftMode::CuraV1, ipMonotonic, false, 0.);
    const TipMetrics cura_large_seam_gap = slice_pointed_prism(RaftMode::CuraV1, ipMonotonic, false, 80.);
    const TipMetrics legacy = slice_pointed_prism(RaftMode::Legacy);

    // CuraV1 adds one long contour-following skin line. Legacy keeps the
    // historical short boundary links from the selected monotonic pattern.
    REQUIRE(cura.longest_contour_segment > 10.);
    REQUIRE(legacy.longest_contour_segment < 3.);
    REQUIRE(cura.closed_bottom_paths >= 1);
    REQUIRE(cura.bottom_surface_loops == 0);
    REQUIRE(legacy.closed_bottom_paths == 0);
    REQUIRE(cura.contact_skin_ordered);
    REQUIRE_FALSE(legacy.contact_skin_ordered);
    REQUIRE(cura.bottom_paths_inside_surface);
    REQUIRE(cura.gcode_first_bottom_path_closed);
    REQUIRE(cura_large_seam_gap.gcode_first_bottom_path_closed);
    REQUIRE(cura.gcode_bottom_speed_correct);
    REQUIRE(cura_large_seam_gap.gcode_bottom_speed_correct);
    REQUIRE(cura_large_seam_gap.gcode_bottom_length == Approx(cura.gcode_bottom_length).margin(0.02));

    // The original hatch starts at least one material line farther from the
    // acute tip, leaving a continuous contour-led peel region.
    REQUIRE(legacy.hatch_tip_x - cura.hatch_tip_x > 0.35);
    REQUIRE(cura.outline_tip_x - cura.hatch_tip_x > 0.35);

    // CrossZag mirrors its input around the configured symmetry axis and then
    // mirrors generated paths back. The contact outline must remain in the
    // model coordinate system while only the original pattern uses that path.
    const TipMetrics symmetric_crosszag = slice_pointed_prism(RaftMode::CuraV1, ipCrossZag, true);
    REQUIRE(symmetric_crosszag.closed_bottom_paths >= 1);
    REQUIRE(symmetric_crosszag.bottom_surface_loops == 0);
    REQUIRE(symmetric_crosszag.contact_skin_ordered);
    REQUIRE(symmetric_crosszag.bottom_paths_inside_surface);

    // Native Concentric bottom fill already owns its outline and must keep its
    // historical topology instead of receiving a second contact outline.
    const TipMetrics concentric = slice_pointed_prism(RaftMode::CuraV1, ipConcentric);
    REQUIRE_FALSE(concentric.contact_skin_ordered);
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
