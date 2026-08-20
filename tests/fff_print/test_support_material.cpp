#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <fstream>
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
    config.angle                   = 45.;
    config.angle_increment         = 90.;
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
    REQUIRE(plan.layers[0].angle == Approx(45.));
    REQUIRE(plan.layers[1].angle == Approx(135.));
    REQUIRE(plan.layers[2].angle == Approx(45.));
    REQUIRE(plan.layers[3].angle == Approx(135.));
    REQUIRE(plan.layers[4].angle == Approx(45.));
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

TEST_CASE("Cura V1 raft reaches support toolpaths and G-code phase controls", "[SupportMaterial][Raft][CuraV1][GCode]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.6}));
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.42);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.8, false));
    config.set("inner_wall_line_width", 0.42);
    config.set("outer_wall_line_width", 0.42);
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
    REQUIRE(object->support_layers().size() >= 5);
    const SupportParameters support_params(*object);
    const Flow nominal_base_flow(0.6f, 0.3f, 0.6f);
    const Flow nominal_interface_flow(0.6f, 0.3f, 0.6f);
    const Flow nominal_surface_flow(0.42f, 0.2f, 0.6f);
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
    REQUIRE(support_params.raft_layer_angle(0) == Approx(Geometry::deg2rad(45.f)));
    REQUIRE(support_params.raft_layer_angle(1) == Approx(Geometry::deg2rad(135.f)));
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
    REQUIRE(object->layers().size() >= 2);
    REQUIRE(object->layers()[0]->id() == 5);
    REQUIRE(object->layers()[0]->height == Approx(0.42));
    REQUIRE(object->layers()[0]->print_z == Approx(1.99));
    REQUIRE(object->layers()[1]->height == Approx(0.2));
    REQUIRE(object->layers()[1]->print_z == Approx(2.09));
    // Overlap changes print Z only; the mesh must still be sliced through its
    // full 20 mm height instead of cropping the top by the overlap amount.
    REQUIRE(object->layers().back()->slice_z + 0.5 * object->layers().back()->height >= 20.0 - EPSILON);
    REQUIRE(object->layers()[0]->get_region(0)->flow(frPerimeter).width() == Approx(0.8));
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
    const std::string first_model_layer_gcode = layer_section(1.99, 2.09);
    const auto fan_command_count = [](const std::string &section) {
        size_t count = 0;
        for (size_t position = 0; (position = section.find("M106 S", position)) != std::string::npos; ++count)
            position += 6;
        return count;
    };
    REQUIRE(base_gcode.find("F600") != std::string::npos);
    REQUIRE(interface_gcode.find("F1500") != std::string::npos);
    REQUIRE(surface_gcode.find("F3600") != std::string::npos);
    REQUIRE(interface_gcode.find("M106 S51") != std::string::npos);
    REQUIRE(surface_gcode.find("M106 S102") != std::string::npos);
    REQUIRE(fan_command_count(base_gcode) == 1);
    REQUIRE(fan_command_count(interface_gcode) == 1);
    REQUIRE(fan_command_count(surface_gcode) == 1);
    // CoolingBuffer prepends the automatic fan command before the layer
    // marker. The post-raft model layer must therefore reset the Surface fan
    // to the model-local layer-0 value between the last raft marker and the
    // first model marker, instead of treating physical layer 5 as fully cooled.
    const size_t first_model_marker = layer_marker_position(1.99);
    const size_t last_raft_marker = layer_marker_position(1.3);
    const size_t model_fan_reset = gcode.rfind("M106 S0", first_model_marker);
    REQUIRE(model_fan_reset != std::string::npos);
    REQUIRE(model_fan_reset > last_raft_marker);
    REQUIRE(first_model_layer_gcode.find("F420") != std::string::npos);
    REQUIRE(first_model_layer_gcode.find("F660") != std::string::npos);
    REQUIRE(first_model_layer_gcode.find("F180") != std::string::npos);
    boost::filesystem::remove(gcode_path);
}

TEST_CASE("Cura V1 raft reaches organic tree support layers", "[SupportMaterial][Raft][CuraV1][TreeSupport]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
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
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTreeAuto));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsTreeOrganic));
    config.set("support_threshold_angle", 30);
    config.set("support_top_z_distance", 0.2);

    TriangleMesh tilted_cube = Slic3r::Test::mesh(TestMesh::cube_20x20x20);
    tilted_cube.rotate_x(float(Geometry::deg2rad(15.)));
    Model model;
    ModelObject *model_object = model.add_object();
    model_object->name = "Cura-style raft organic tree cube";
    model_object->add_volume(std::move(tilted_cube));
    ModelInstance *instance = model_object->add_instance();
    instance->set_offset(Vec3d(100., 100., 0.));
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    print.process();

    const PrintObject *object = print.objects().front();
    const SupportParameters support_params(*object);
    REQUIRE(object->slicing_parameters().raft_layers() == 5);
    REQUIRE(object->support_layers().size() >= 5);
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
