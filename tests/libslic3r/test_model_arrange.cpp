#include <catch2/catch.hpp>

#include <chrono>

#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;

namespace {

Model make_model(TriangleMesh mesh)
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(std::move(mesh));
    object->add_instance();
    return model;
}

DynamicPrintConfig support_config(SupportType type)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_support", true);
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(type));
    return config;
}

} // namespace

TEST_CASE("Arrange support margin ignores a supported cube", "[Arrange][Support]")
{
    Model model = make_model(make_cube(20.0, 20.0, 20.0));
    ModelInstance *instance = model.objects.front()->instances.front();
    DynamicPrintConfig config = support_config(stTreeAuto);

    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(1.0));
}

TEST_CASE("Arrange support analysis bounds large mesh inspection", "[Arrange][Support]")
{
    indexed_triangle_set mesh = its_make_cube(20.0, 20.0, 20.0);
    const auto original_faces = mesh.indices;
    mesh.indices.reserve(120000);
    while (mesh.indices.size() < 120000)
        mesh.indices.insert(mesh.indices.end(), original_faces.begin(), original_faces.end());

    Model model = make_model(TriangleMesh(std::move(mesh)));
    ModelInstance *instance = model.objects.front()->instances.front();
    DynamicPrintConfig config = support_config(stTreeAuto);

    const auto started_at = std::chrono::steady_clock::now();
    const double margin = estimate_arrange_support_margin(*instance, config);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
    WARN("120,000-face support margin analysis: " << elapsed_ms << " ms");
    REQUIRE(margin == Approx(1.0));
}

TEST_CASE("Arrange support margin follows per-object support type", "[Arrange][Support]")
{
    Model model = make_model(make_cube(20.0, 20.0, 20.0));
    ModelInstance *instance = model.objects.front()->instances.front();
    instance->set_rotation(X, Geometry::deg2rad(15.0));

    DynamicPrintConfig global_config = support_config(stNormalAuto);
    const double normal_margin = estimate_arrange_support_margin(*instance, global_config);

    instance->get_object()->config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTreeAuto));
    const double tree_margin = estimate_arrange_support_margin(*instance, global_config);

    REQUIRE(normal_margin == Approx(6.0));
    REQUIRE(tree_margin == Approx(12.0));

    // Arrange margins intentionally follow the upstream fixed safety envelope.
    // Custom support geometry parameters must not silently mix radius, diameter,
    // or support-generation offsets into the one-sided arrange margin.
    instance->get_object()->config.set("tree_support_auto_brim", false);
    instance->get_object()->config.set("tree_support_brim_width", 15.0);
    const double manual_tree_margin = estimate_arrange_support_margin(*instance, global_config);

    REQUIRE(manual_tree_margin == Approx(12.0));
}

TEST_CASE("Normal support uses the upstream fixed arrange margin", "[Arrange][Support]")
{
    Model model = make_model(make_cube(20.0, 20.0, 20.0));
    ModelInstance *instance = model.objects.front()->instances.front();
    instance->set_rotation(X, Geometry::deg2rad(15.0));

    DynamicPrintConfig config = support_config(stNormalAuto);
    config.set("support_object_xy_distance", 0.35);
    config.set("support_expansion", 0.0);

    // Reproduces gui-run-20260814-161220.log. The previous dynamic estimate was
    // 2 + 0.35 + 0 = 2.35 mm and allowed a support extrusion to reach Y=101.199
    // on a 101 mm bed. Normal support now keeps the upstream 6 mm allowance.
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(6.0));

    config.set("support_object_xy_distance", 2.0);
    config.set("support_expansion", 8.0);
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(6.0));
}

TEST_CASE("Automatic tree support keeps the full first-layer safety envelope", "[Arrange][Support]")
{
    Model model = make_model(make_cube(20.0, 20.0, 200.0));
    ModelInstance *instance = model.objects.front()->instances.front();
    instance->set_rotation(X, Geometry::deg2rad(15.0));

    DynamicPrintConfig config = support_config(stTreeAuto);

    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(12.0));
}

TEST_CASE("Arrange support margin detects an edge-resting cube that produces a tree support base", "[Arrange][Support]")
{
    Model model = make_model(make_cube(18.0, 18.0, 18.0));
    ModelInstance *instance = model.objects.front()->instances.front();
    // Reproduces gui-run-20260814-115336.log: the real tree-support detector
    // classified this approximately 44.6-degree pose as a sharp tail and added
    // a first-layer support base, although the old face-threshold precheck did not.
    instance->set_rotation(X, Geometry::deg2rad(44.6));

    DynamicPrintConfig config = support_config(stTreeAuto);
    config.set("support_threshold_angle", 30);
    config.set("support_remove_small_overhang", true);

    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(12.0));
}

TEST_CASE("Arrange applies per-object margins unless spacing is explicit", "[Arrange][Support]")
{
    ArrangePolygons selected(2);
    selected[0].brim_width = 1.0;
    selected[1].brim_width = 12.0;
    selected[0].poly.contour = Polygon{{0, 0}, {scaled(10.0), 0}, {scaled(10.0), scaled(10.0)}, {0, scaled(10.0)}};
    selected[1].poly.contour = selected[0].poly.contour;

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    SECTION("automatic spacing keeps each object's own support margin")
    {
        ArrangeParams params;
        update_selected_items_inflation(selected, &config, params);

        REQUIRE(selected[0].inflation == scaled(1.0));
        REQUIRE(selected[1].inflation == scaled(12.0));
    }

    SECTION("explicit spacing overrides support-aware margins")
    {
        ArrangeParams params;
        params.min_obj_distance = scaled(8.0);
        update_selected_items_inflation(selected, &config, params);

        REQUIRE(selected[0].inflation == scaled(4.0));
        REQUIRE(selected[1].inflation == scaled(4.0));
    }
}

TEST_CASE("CLI auto-plate mode keeps omitted and explicit values distinct", "[Arrange][CLI]")
{
    // The CLI config contains a default value of 0 even when --auto-plate was
    // omitted. Presence therefore has to participate in resolving the mode.
    REQUIRE(resolve_arrange_auto_plate_value(false, 0) == -1);
    REQUIRE(resolve_arrange_auto_plate_value(true, 0) == 0);
    REQUIRE(resolve_arrange_auto_plate_value(true, 1) == 1);
    REQUIRE(resolve_arrange_auto_plate_value(true, 2) == 2);
}

TEST_CASE("Arrange reserves a wipe tower only when it is actually needed", "[Arrange][WipeTower]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_prime_tower", true);
    config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));

    ArrangeParams params;
    params.allow_multi_materials_on_same_plate = true;

    ArrangePolygons selected(1);
    selected.front().extrude_ids = {1};
    selected.front().bed_temp = 60;

    std::string reason;
    REQUIRE_FALSE(arrange_wipe_tower_needed(config, selected, params, false, &reason));
    REQUIRE(reason == "not_required");

    SECTION("disabled option always wins")
    {
        config.set("enable_prime_tower", false);
        selected.front().extrude_ids = {1, 2};
        REQUIRE_FALSE(arrange_wipe_tower_needed(config, selected, params, false, &reason));
        REQUIRE(reason == "disabled");
    }

    SECTION("smooth timelapse needs a tower with one filament")
    {
        config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlSmooth));
        REQUIRE(arrange_wipe_tower_needed(config, selected, params, false, &reason));
        REQUIRE(reason == "smooth_timelapse");
    }

    SECTION("a multi-extruder object needs a tower")
    {
        selected.front().extrude_ids = {1, 2};
        REQUIRE(arrange_wipe_tower_needed(config, selected, params, false, &reason));
        REQUIRE(reason == "multi_extruder_object");
    }

    SECTION("separate extruders with the same bed temperature need a tower")
    {
        selected.emplace_back();
        selected.back().extrude_ids = {2};
        selected.back().bed_temp = 60;
        REQUIRE(arrange_wipe_tower_needed(config, selected, params, false, &reason));
        REQUIRE(reason == "same_bed_temperature_multi_extruder");
    }

    SECTION("different bed temperatures remain on separate compatible groups")
    {
        selected.emplace_back();
        selected.back().extrude_ids = {2};
        selected.back().bed_temp = 70;
        REQUIRE_FALSE(arrange_wipe_tower_needed(config, selected, params, false, &reason));
        REQUIRE(reason == "not_required");
    }

    SECTION("separate objects do not share a tower when multi-material plates are disabled")
    {
        params.allow_multi_materials_on_same_plate = false;
        selected.emplace_back();
        selected.back().extrude_ids = {2};
        selected.back().bed_temp = 60;
        REQUIRE_FALSE(arrange_wipe_tower_needed(config, selected, params, false, &reason));
        REQUIRE(reason == "not_required");
    }

    SECTION("separate extruders already fixed to one plate need a tower")
    {
        params.allow_multi_materials_on_same_plate = false;
        selected.emplace_back();
        selected.back().extrude_ids = {2};
        selected.back().bed_temp = 70;
        REQUIRE(arrange_wipe_tower_needed(config, selected, params, true, &reason));
        REQUIRE(reason == "fixed_plate_multi_extruder");
    }

    SECTION("single extruder fixed to one plate still needs no tower")
    {
        params.allow_multi_materials_on_same_plate = false;
        selected.emplace_back();
        selected.back().extrude_ids = {1};
        selected.back().bed_temp = 60;
        REQUIRE_FALSE(arrange_wipe_tower_needed(config, selected, params, true, &reason));
        REQUIRE(reason == "not_required");
    }

    SECTION("sequential printing does not reserve a tower")
    {
        params.is_seq_print = true;
        selected.front().extrude_ids = {1, 2};
        REQUIRE_FALSE(arrange_wipe_tower_needed(config, selected, params, true, &reason));
        REQUIRE(reason == "sequential_printing");
    }
}

TEST_CASE("Arrange plans wipe towers from the actual per-bed material assignment", "[Arrange][WipeTower]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_prime_tower", true);
    config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));

    ArrangeParams params;
    params.allow_multi_materials_on_same_plate = true;

    ArrangePolygons arranged(4);
    arranged[0].bed_idx = 0;
    arranged[0].bed_temp = 60;
    arranged[0].extrude_ids = {1};
    arranged[1] = arranged[0];
    arranged[2] = arranged[0];
    arranged[2].bed_idx = 1;
    arranged[3] = arranged[2];
    arranged[3].extrude_ids = {2};

    const ArrangeWipeTowerPlan plan = arrange_wipe_tower_plan(config, arranged, {}, params);
    REQUIRE(plan.size() == 1);
    REQUIRE(plan.count(0) == 0);
    const std::set<int> expected_extruders{1, 2};
    REQUIRE(plan.at(1) == expected_extruders);

    SECTION("separate single-material beds reserve no invisible towers")
    {
        arranged[3].extrude_ids = {1};
        REQUIRE(arrange_wipe_tower_plan(config, arranged, {}, params).empty());
    }

    SECTION("a fixed object participates in the bed-specific decision")
    {
        arranged.resize(1);
        ArrangePolygons fixed(1);
        fixed.front().bed_idx = 0;
        fixed.front().bed_temp = 60;
        fixed.front().extrude_ids = {2};
        const ArrangeWipeTowerPlan fixed_plan = arrange_wipe_tower_plan(config, arranged, fixed, params);
        REQUIRE(fixed_plan.size() == 1);
        const std::set<int> expected_extruders{1, 2};
        REQUIRE(fixed_plan.at(0) == expected_extruders);
    }

    SECTION("an actual mixed bed still needs a tower when material mixing was disabled")
    {
        params.allow_multi_materials_on_same_plate = false;
        REQUIRE(arrange_wipe_tower_plan(config, arranged, {}, params).size() == 1);
    }

    SECTION("smooth timelapse reserves one tower on each used bed")
    {
        config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlSmooth));
        arranged[3].extrude_ids = {1};
        const ArrangeWipeTowerPlan smooth_plan = arrange_wipe_tower_plan(config, arranged, {}, params);
        REQUIRE(smooth_plan.size() == 2);
        REQUIRE(smooth_plan.count(0) == 1);
        REQUIRE(smooth_plan.count(1) == 1);
    }

    SECTION("a multi-extruder object reserves a tower only on its own bed")
    {
        arranged.resize(1);
        arranged.front().bed_idx = 3;
        arranged.front().extrude_ids = {1, 2};
        const ArrangeWipeTowerPlan object_plan = arrange_wipe_tower_plan(config, arranged, {}, params);
        REQUIRE(object_plan.size() == 1);
        REQUIRE(object_plan.count(3) == 1);
    }
}

TEST_CASE("Current-plate arrange rejects every logical-bed overflow", "[Arrange][CurrentPlate]")
{
    ArrangePolygons arranged(2);
    arranged[0].bed_idx = 0;
    arranged[1].bed_idx = 0;
    REQUIRE(arrange_result_fits_single_plate(arranged));

    arranged[1].bed_idx = 1;
    REQUIRE_FALSE(arrange_result_fits_single_plate(arranged));

    arranged[1].bed_idx = -1;
    REQUIRE_FALSE(arrange_result_fits_single_plate(arranged));
}

TEST_CASE("Model merge preserves per-model process overrides for arrange", "[Arrange][Support][ModelProcess]")
{
    Model first_source = make_model(make_cube(20.0, 20.0, 20.0));
    Model second_source = make_model(make_cube(20.0, 20.0, 20.0));

    ModelObject *first_object = first_source.objects.front();
    first_object->config.set("layer_height", 0.12);
    first_object->config.set("enable_support", false);

    ModelObject *second_object = second_source.objects.front();
    second_object->config.set("layer_height", 0.28);
    second_object->config.set("enable_support", true);
    second_object->config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTreeAuto));
    second_object->instances.front()->set_rotation(X, Geometry::deg2rad(15.0));

    Model merged;
    ModelObject *merged_first = merged.add_object(*first_object);
    ModelObject *merged_second = merged.add_object(*second_object);

    REQUIRE(merged_first->config.opt_float("layer_height") == Approx(0.12));
    REQUIRE(merged_second->config.opt_float("layer_height") == Approx(0.28));
    const DynamicPrintConfig global_config = DynamicPrintConfig::full_print_config();
    REQUIRE(estimate_arrange_support_margin(*merged_first->instances.front(), global_config) == Approx(1.0));
    const double merged_tree_margin = estimate_arrange_support_margin(*merged_second->instances.front(), global_config);
    REQUIRE(merged_tree_margin == Approx(12.0));
}
