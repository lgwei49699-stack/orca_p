#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <chrono>

#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/ClipperUtils.hpp"
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

DynamicPrintConfig raft_config(RaftMode mode)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_support", false);
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(mode));
    return config;
}

DynamicPrintConfig unrafted_bed_config(double bed_size)
{
    DynamicPrintConfig config = raft_config(RaftMode::Legacy);
    // FullPrintConfig does not include the machine's printer_technology option.
    config.set_key_value("printer_technology", new ConfigOptionEnum<PrinterTechnology>(ptFFF));
    config.set("raft_layers", 0);
    config.set("enforce_support_layers", 0);
    config.set("skirt_loops", 0);
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btNoBrim));
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {bed_size, 0.}, {bed_size, bed_size}, {0., bed_size}}));
    config.erase("filament_type");
    return config;
}

} // namespace

TEST_CASE("Compact rafts only opt in uniform independent support-free objects", "[Arrange][Raft][Compact]")
{
    DynamicPrintConfig config = unrafted_bed_config(100.);
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    Model         model  = make_model(make_cube(20., 20., 2.));
    ModelObject*  second = model.add_object(*model.objects.front());
    ArrangeParams params;
    bool          eligible            = false;
    bool          duplicate_candidate = false;
    bool          virtual_obstacle    = false;
    bool          wipe_tower          = false;
    bool          single_object       = false;
    SECTION("uniform objects") { eligible = true; }
    SECTION("a virtual exclusion is not an unrafted object")
    {
        eligible         = true;
        virtual_obstacle = true;
    }
    SECTION("raft disabled") { second->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy)); }
    SECTION("support enabled") { second->config.set("enable_support", true); }
    SECTION("forced support") { second->config.set("enforce_support_layers", 1); }
    SECTION("skirt") { config.set("skirt_loops", 1); }
    SECTION("sequential") { params.is_seq_print = true; }
    SECTION("layer height differs") { second->config.set("layer_height", 0.16); }
    SECTION("raft flow differs") { second->config.set_key_value("raft_base_flow", new ConfigOptionPercent(80.)); }
    SECTION("segmentation differs") { second->config.set("raft_segmented", true); }
    SECTION("multiple instances") { second->add_instance(); }
    SECTION("synthetic Fill Bed copies") { duplicate_candidate = true; }
    SECTION("another support filament") { second->config.set("support_filament", 2); }
    SECTION("another model filament") { second->config.set("extruder", 2); }
    SECTION("volume modifier") { second->volumes.front()->config.set("layer_height", 0.16); }
    SECTION("single object keeps bed-fit behavior") { single_object = true; }
    SECTION("wipe tower keeps ordinary reservations")
    {
        wipe_tower       = true;
        virtual_obstacle = true;
    }

    ArrangePolygons selected{get_instance_arrange_poly(model.objects.front()->instances.front(), config)};
    ArrangePolygons fixed{get_instance_arrange_poly(second->instances.front(), config)};
    fixed.front().bed_idx = 0;
    if (single_object)
        fixed.clear();
    if (duplicate_candidate)
        selected.push_back(selected.front());
    if (virtual_obstacle) {
        fixed.emplace_back();
        fixed.back().is_virt_object = true;
        fixed.back().is_wipe_tower  = wipe_tower;
    }
    const double original = selected.front().minimum_inflation;
    REQUIRE(prepare_compact_raft_arrangement(selected, fixed, config, params) == eligible);
    REQUIRE(selected.front().minimum_inflation == original);
    REQUIRE((selected.front().compact_raft_obstacle_margin > 0.) == eligible);
    // Re-evaluation must clear an earlier eligibility proof.
    params.is_seq_print = true;
    REQUIRE_FALSE(prepare_compact_raft_arrangement(selected, fixed, config, params));
    REQUIRE(selected.front().compact_raft_obstacle_margin == 0.);
    for (const auto& item : fixed)
        REQUIRE(item.compact_raft_obstacle_margin == 0.);
}

TEST_CASE("Compact rafts pack four large objects while preserving model spacing and fixed coordinates", "[Arrange][Raft][Compact]")
{
    const double bed_size  = GENERATE(100., 180.);
    const bool   one_fixed = GENERATE(false, true);
    const double distance  = GENERATE(0., 2.);
    CAPTURE(bed_size, one_fixed, distance);
    DynamicPrintConfig config = unrafted_bed_config(bed_size);
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("raft_base_margin", 5.);
    config.set("raft_interface_margin", 4.);
    config.set("raft_surface_margin", 3.);
    const double side  = (bed_size - 8.) / 2.;
    Model        model = make_model(make_cube(side, side, 2.));
    model.objects.front()->instances.front()->set_offset(Vec3d(2., 2., 0.));
    for (int i = 1; i < 4; ++i)
        model.add_object(*model.objects.front());
    ArrangePolygons selected, fixed;
    for (size_t i = 0; i < model.objects.size(); ++i)
        (one_fixed && i == 0 ? fixed : selected).push_back(get_instance_arrange_poly(model.objects[i]->instances.front(), config));
    if (one_fixed)
        fixed.front().bed_idx = 0;
    ArrangeParams params;
    params.parallel         = false;
    params.min_obj_distance = scaled(distance);
    params.progressind      = [](unsigned, std::string) {};
    update_arrange_params(params, &config, selected);
    update_selected_items_inflation(selected, &config, params);
    update_unselected_items_inflation(fixed, &config, params);
    const Points bed = get_shrink_bedpts(&config, params);
    if (!one_fixed) {
        ArrangePolygons baseline = selected;
        arrangement::arrange(baseline, fixed, bed, params);
        std::set<int> plates;
        for (const auto& item : baseline) {
            REQUIRE(item.bed_idx >= 0);
            plates.insert(item.bed_idx);
        }
        REQUIRE(plates.size() > 1);
    }
    const Vec3d           fixed_offset   = model.objects.front()->instances.front()->get_offset();
    const ArrangePolygons original_fixed = fixed;
    REQUIRE(prepare_compact_raft_arrangement(selected, fixed, config, params));
    arrangement::arrange(selected, fixed, bed, params);
    const BoundingBox bounds(bed);
    for (const auto& item : selected) {
        REQUIRE(item.bed_idx == 0);
        REQUIRE(bounds.contains(get_extents(item.transformed_poly())));
        REQUIRE(item.minimum_inflation == Approx(5.)); // fallback reservation survives
    }
    ArrangePolygons all = selected;
    all.insert(all.end(), fixed.begin(), fixed.end());
    for (size_t i = 0; i < all.size(); ++i)
        for (size_t j = i + 1; j < all.size(); ++j) {
            REQUIRE(intersection_ex(offset_ex(all[i].transformed_poly(), scaled(0.999), ClipperLib::jtSquare),
                                    offset_ex(all[j].transformed_poly(), scaled(0.999), ClipperLib::jtSquare))
                        .empty());
            if (one_fixed) {
                // Do not recover a plate by stealing the fixed object's 2 mm
                // gap: the previous 0.0002 mm shrink made the last slot unfit.
                const BoundingBox a = get_extents(all[i].transformed_poly());
                const BoundingBox b = get_extents(all[j].transformed_poly());
                const coord_t gap = std::max({a.min.x() - b.max.x(), b.min.x() - a.max.x(), a.min.y() - b.max.y(), b.min.y() - a.max.y()});
                REQUIRE(gap >= scaled(2. - 0.00001));
            }
        }
    if (one_fixed) {
        REQUIRE(model.objects.front()->instances.front()->get_offset().isApprox(fixed_offset));
        REQUIRE(fixed.front().translation == original_fixed.front().translation);
        REQUIRE(fixed.front().rotation == original_fixed.front().rotation);
        REQUIRE(fixed.front().bed_idx == original_fixed.front().bed_idx);
        REQUIRE(fixed.front().inflation == original_fixed.front().inflation);
        REQUIRE(fixed.front().minimum_inflation == original_fixed.front().minimum_inflation);
    }
}

TEST_CASE("Compact raft jobs preserve exclusions explicit spacing cancellation and multi-plate output", "[Arrange][Raft][Compact]")
{
    const int scenario = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(scenario);
    DynamicPrintConfig config = unrafted_bed_config(100.);
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("raft_base_margin", 5.);
    Model model = make_model(make_cube(scenario == 3 ? 98. : 15., scenario == 3 ? 98. : 15., 2.));
    model.add_object(*model.objects.front());
    if (scenario == 6) {
        model.objects[0]->arrange_group = "#FF0000";
        model.objects[1]->arrange_group = "#00FF00";
    }
    ArrangePolygons selected, fixed;
    for (auto* object : model.objects)
        selected.push_back(get_instance_arrange_poly(object->instances.front(), config));
    ArrangeParams params;
    params.parallel         = false;
    params.min_obj_distance = scaled(scenario == 2 ? 12. : 2.);
    params.progressind      = [](unsigned, std::string) {};
    ArrangePolygon obstacle;
    obstacle.poly = ExPolygon(
        Polygon{{scaled(42.), scaled(42.)}, {scaled(58.), scaled(42.)}, {scaled(58.), scaled(58.)}, {scaled(42.), scaled(58.)}});
    obstacle.is_virt_object = true;
    obstacle.bed_idx        = 0;
    obstacle.inflation      = scaled(1.);
    if (scenario == 0)
        params.excluded_regions.push_back(obstacle);
    if (scenario == 1)
        fixed.push_back(obstacle);
    update_arrange_params(params, &config, selected);
    REQUIRE(prepare_compact_raft_arrangement(selected, fixed, config, params));
    update_selected_items_inflation(selected, &config, params);
    update_unselected_items_inflation(fixed, &config, params);
    const ArrangePolygons before                  = selected;
    bool                  canceled_during_packing = false;
    size_t                published               = 0;
    params.on_packed                              = [&](const ArrangePolygon&) { ++published; };
    if (scenario == 4)
        params.stopcondition = []() { return true; };
    if (scenario == 5) {
        params.progressind   = [&](unsigned, std::string) { canceled_during_packing = true; };
        params.stopcondition = [&]() { return canceled_during_packing; };
    }
    const Points bed = get_shrink_bedpts(&config, params);
    arrangement::arrange(selected, fixed, bed, params);
    for (size_t i = 0; i < selected.size(); ++i) {
        const auto& item = selected[i];
        if (scenario == 4 || scenario == 5) {
            REQUIRE(item.translation == before[i].translation);
            REQUIRE(item.rotation == before[i].rotation);
            REQUIRE(item.bed_idx == before[i].bed_idx);
            continue;
        }
        REQUIRE(item.bed_idx >= 0);
        REQUIRE(BoundingBox(bed).contains(get_extents(item.transformed_poly())));
        if (scenario <= 1) {
            REQUIRE(item.bed_idx == 0);
            REQUIRE(
                intersection_ex(offset_ex(item.transformed_poly(), scaled(item.compact_raft_obstacle_margin - 0.001), ClipperLib::jtSquare),
                                offset_ex(obstacle.transformed_poly(), scaled(0.999), ClipperLib::jtSquare))
                    .empty());
        }
    }
    if (scenario == 2) {
        REQUIRE(selected[0].bed_idx == selected[1].bed_idx);
        REQUIRE(intersection_ex(offset_ex(selected[0].transformed_poly(), scaled(5.999), ClipperLib::jtSquare),
                                offset_ex(selected[1].transformed_poly(), scaled(5.999), ClipperLib::jtSquare))
                    .empty());
    }
    if (scenario == 3 || scenario == 6)
        REQUIRE(selected[0].bed_idx != selected[1].bed_idx);
    if (scenario == 4 || scenario == 5)
        REQUIRE(published == 0);
    else
        REQUIRE(published == selected.size());
    if (scenario == 5)
        REQUIRE(canceled_during_packing);
}

TEST_CASE("Mixed raft preparation protects both movable and fixed objects without changing uniform scenes", "[Arrange][Raft][Clearance]")
{
    Model              model  = make_model(make_cube(10., 10., 10.));
    ModelObject*       off    = model.add_object(*model.objects.front());
    DynamicPrintConfig config = raft_config(RaftMode::CuraV1);
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterAndInner));
    config.set("brim_width", 2.);
    config.set("brim_object_gap", 0.);
    config.set("skirt_loops", 0);
    ArrangeParams params;
    params.min_obj_distance = scaled(0.2);

    SECTION("all rafts retain their original reservation")
    {
        ArrangePolygons selected{get_instance_arrange_poly(model.objects[0]->instances[0], config),
                                 get_instance_arrange_poly(off->instances[0], config)};
        const double    before = selected.front().minimum_inflation;
        ArrangePolygons fixed;
        REQUIRE_FALSE(prepare_mixed_raft_clearance(selected, fixed, config, params));
        REQUIRE(selected.front().minimum_inflation == before);
    }
    SECTION("virtual exclusions are not unrafted models")
    {
        ArrangePolygons selected{get_instance_arrange_poly(model.objects[0]->instances[0], config)};
        ArrangePolygons fixed(1);
        fixed.front().is_virt_object = true;
        REQUIRE_FALSE(prepare_mixed_raft_clearance(selected, fixed, config, params));
    }
    SECTION("all rafts disabled retain the original reservation")
    {
        config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
        config.set("raft_layers", 0);
        ArrangePolygons selected{get_instance_arrange_poly(model.objects[0]->instances[0], config),
                                 get_instance_arrange_poly(off->instances[0], config)};
        ArrangePolygons fixed;
        REQUIRE_FALSE(prepare_mixed_raft_clearance(selected, fixed, config, params));
        REQUIRE(selected.front().minimum_inflation == 0.);
        REQUIRE(selected.back().minimum_inflation == 0.);
    }
    SECTION("pure legacy mixing stays unchanged")
    {
        config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
        config.set("raft_layers", 0);
        model.objects[0]->config.set("raft_layers", 3);
        ArrangePolygons selected{get_instance_arrange_poly(model.objects[0]->instances[0], config),
                                 get_instance_arrange_poly(off->instances[0], config)};
        ArrangePolygons fixed;
        REQUIRE_FALSE(prepare_mixed_raft_clearance(selected, fixed, config, params));
        REQUIRE(selected.front().minimum_inflation == 0.);
        REQUIRE(selected.back().minimum_inflation == 0.);
    }
    SECTION("either side may be fixed, and small explicit distances cannot remove its footprint")
    {
        const bool raft_fixed = GENERATE(false, true);
        off->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
        off->config.set("raft_layers", 0);
        ArrangePolygons selected{get_instance_arrange_poly(model.objects[0]->instances[0], config)};
        ArrangePolygons fixed{get_instance_arrange_poly(off->instances[0], config)};
        if (raft_fixed)
            selected.swap(fixed);
        const double original = raft_fixed ? fixed.front().brim_width : selected.front().brim_width;
        REQUIRE(prepare_mixed_raft_clearance(selected, fixed, config, params));
        REQUIRE((raft_fixed ? fixed.front() : selected.front()).minimum_inflation > original);
        REQUIRE((raft_fixed ? selected.front() : fixed.front()).minimum_inflation > 2.);
        update_selected_items_inflation(selected, &config, params);
        update_unselected_items_inflation(fixed, &config, params);
        REQUIRE(selected.front().inflation >= scaled(selected.front().minimum_inflation));
        REQUIRE(fixed.front().inflation >= scaled(fixed.front().minimum_inflation));
        const double before = selected.front().minimum_inflation;
        REQUIRE(prepare_mixed_raft_clearance(selected, fixed, config, params));
        REQUIRE(selected.front().minimum_inflation == before);
        REQUIRE(off->config.opt_int("raft_layers") == 0);
    }
    SECTION("sequential printing keeps its existing clearance policy")
    {
        off->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
        off->config.set("raft_layers", 0);
        ArrangePolygons selected{get_instance_arrange_poly(model.objects[0]->instances[0], config)};
        ArrangePolygons fixed{get_instance_arrange_poly(off->instances[0], config)};
        params.is_seq_print = true;
        REQUIRE_FALSE(prepare_mixed_raft_clearance(selected, fixed, config, params));
        REQUIRE(fixed.front().minimum_inflation == 0.);
    }
}

TEST_CASE("Legacy raft keeps the existing arrange margin", "[Arrange][Raft]")
{
    Model model = make_model(make_cube(20.0, 20.0, 20.0));
    ModelInstance *instance = model.objects.front()->instances.front();
    DynamicPrintConfig config = raft_config(RaftMode::Legacy);

    config.set("raft_layers", 20);
    config.set("raft_expansion", 20.0);
    config.set("raft_first_layer_expansion", 20.0);
    config.set("raft_base_margin", 30.0);
    config.set("raft_interface_margin", 30.0);
    config.set("raft_surface_margin", 30.0);

    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(1.0));
    REQUIRE(get_instance_arrange_poly(instance, config).minimum_inflation == Approx(0.0));
}

TEST_CASE("Cura V1 raft arrange margin follows the generated phase footprint", "[Arrange][Raft]")
{
    Model model = make_model(make_cube(20.0, 20.0, 20.0));
    ModelInstance *instance = model.objects.front()->instances.front();
    DynamicPrintConfig config = raft_config(RaftMode::CuraV1);

    config.set("raft_base_margin", 3.0);
    config.set("raft_interface_margin", 5.0);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_margin", 2.0);
    // Stale legacy values must not affect Cura V1 arranging.
    config.set("raft_layers", 20);
    config.set("raft_expansion", 30.0);
    config.set("raft_first_layer_expansion", 30.0);

    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(5.0));

    config.set("raft_interface_layers", 0);
    config.set("raft_interface_margin", 50.0);
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(3.0));

    config.set("raft_base_margin", 1.0);
    config.set("raft_surface_margin", 3.0);
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(3.5));

    config.set("raft_base_margin", 3.0);
    instance->get_object()->config.set("raft_surface_margin", 7.0);
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(7.5));
}

TEST_CASE("Cura V1 raft footprint reaches the arrange polygon and automatic inflation", "[Arrange][Raft]")
{
    Model model = make_model(make_cube(20.0, 20.0, 20.0));
    ModelInstance *instance = model.objects.front()->instances.front();
    DynamicPrintConfig config = raft_config(RaftMode::CuraV1);

    config.set("raft_base_margin", 3.0);
    config.set("raft_interface_margin", 1.0);
    config.set("raft_surface_margin", 1.0);
    config.erase("filament_type");

    ArrangePolygons selected{get_instance_arrange_poly(instance, config)};
    ArrangeParams params;
    REQUIRE(selected.front().brim_width == Approx(3.0));

    update_selected_items_inflation(selected, &config, params);
    REQUIRE(selected.front().inflation == scaled(3.0));

    params.min_obj_distance = scaled(2.0);
    update_selected_items_inflation(selected, &config, params);
    REQUIRE(selected.front().inflation == scaled(3.0));

    params.min_obj_distance = scaled(8.0);
    update_selected_items_inflation(selected, &config, params);
    REQUIRE(selected.front().inflation == scaled(4.0));
}

TEST_CASE("Cura V1 physical footprint is not reduced to fit the current bed", "[Arrange][Raft]")
{
    Model model = make_model(make_cube(20.0, 20.0, 20.0));
    ModelInstance *instance = model.objects.front()->instances.front();
    DynamicPrintConfig config = raft_config(RaftMode::CuraV1);
    config.set("raft_base_margin", 3.0);
    config.set("raft_interface_margin", 1.0);
    config.set("raft_interface_layers", 0);
    config.set("raft_surface_margin", 1.0);
    config.set_key_value("printable_area", new ConfigOptionPoints{Vec2d(0.0, 0.0), Vec2d(24.0, 0.0), Vec2d(24.0, 24.0), Vec2d(0.0, 24.0)});

    ArrangePolygons cura_items{get_instance_arrange_poly(instance, config)};
    ArrangeParams params;
    params.min_obj_distance = scaled(10.0);
    update_selected_items_inflation(cura_items, &config, params);
    REQUIRE(cura_items.front().inflation == scaled(3.0));

    // Legacy keeps the existing clamp behavior; only Cura V1 marks its margin
    // as a non-overridable physical footprint.
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    ArrangePolygons legacy_items{get_instance_arrange_poly(instance, config)};
    update_selected_items_inflation(legacy_items, &config, params);
    REQUIRE(legacy_items.front().minimum_inflation == Approx(0.0));
    REQUIRE(legacy_items.front().inflation < scaled(2.0));
}

TEST_CASE("Large Cura rafts fit empty beds without reducing their pairwise reservation", "[Arrange][Raft][BedClip]")
{
    const double bed_size = GENERATE(100., 180.);
    const bool   occupied = GENERATE(false, true);
    CAPTURE(bed_size, occupied);
    Model model = make_model(make_cube(bed_size - 2., bed_size - 2., 2.));
    model.add_object(*model.objects.front());
    DynamicPrintConfig config = raft_config(RaftMode::CuraV1);
    config.set("skirt_loops", 0);
    config.set("raft_base_margin", 3.);
    config.set("raft_interface_margin", 1.);
    config.set("raft_surface_margin", 1.);
    config.erase("filament_type");
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {bed_size, 0.}, {bed_size, bed_size}, {0., bed_size}}));
    ArrangePolygons items;
    for (ModelObject* object : model.objects)
        items.push_back(get_instance_arrange_poly(object->instances.front(), config));
    ArrangePolygons fixed;
    if (occupied) {
        ArrangePolygon obstacle;
        obstacle.poly = ExPolygon(
            Polygon{{scaled(45.), scaled(45.)}, {scaled(55.), scaled(45.)}, {scaled(55.), scaled(55.)}, {scaled(45.), scaled(55.)}});
        obstacle.bed_idx = 0;
        fixed.push_back(obstacle);
    }
    ArrangeParams params;
    params.parallel    = false;
    params.progressind = [](unsigned, std::string) {};
    update_arrange_params(params, &config, items);
    update_selected_items_inflation(items, &config, params);
    const Points      bed = get_shrink_bedpts(&config, params);
    const BoundingBox bounds(bed);
    arrangement::arrange(items, fixed, bed, params);
    REQUIRE(items[0].bed_idx >= (occupied ? 1 : 0));
    REQUIRE(items[1].bed_idx >= (occupied ? 1 : 0));
    REQUIRE(items[0].bed_idx != items[1].bed_idx);
    for (const ArrangePolygon& item : items) {
        REQUIRE(item.inflation == scaled(3.));
        REQUIRE(bounds.contains(get_extents(item.transformed_poly())));
        // The full reservation cannot fit: success must use bed clipping,
        // rather than silently reducing the spacing used between objects.
        REQUIRE_FALSE(diff_ex(offset_ex(item.transformed_poly(), item.inflation), Polygons{Polygon(bed)}).empty());
    }
}

TEST_CASE("Raft bed clipping preserves support, exclusion and print-mode constraints", "[Arrange][Raft][BedClip]")
{
    Model              model  = make_model(make_cube(98., 98., 2.));
    DynamicPrintConfig config = raft_config(RaftMode::CuraV1);
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {100., 0.}, {100., 100.}, {0., 100.}}));
    config.set("skirt_loops", 0);
    config.set("raft_base_margin", 3.);
    config.set("raft_interface_margin", 1.);
    config.set("raft_surface_margin", 1.);
    config.erase("filament_type");
    ArrangeParams params;
    params.parallel        = false;
    params.progressind     = [](unsigned, std::string) {};
    bool expected_eligible = true;
    SECTION("the model itself must fit") { model.objects.front()->instances.front()->set_scaling_factor(Vec3d(1.02, 1.02, 1.)); }
    SECTION("support remains fully reserved")
    {
        config.set("enable_support", true);
        expected_eligible = false;
    }
    SECTION("forced support layers remain reserved")
    {
        config.set("enforce_support_layers", 1);
        expected_eligible = false;
    }
    SECTION("manual support enforcers remain reserved")
    {
        model.objects.front()->add_volume(make_cube(1., 1., 1.))->set_type(ModelVolumeType::SUPPORT_ENFORCER);
        expected_eligible = false;
    }
    SECTION("skirts remain reserved")
    {
        config.set("skirt_loops", 1);
        expected_eligible = false;
    }
    SECTION("object skirt overrides remain reserved")
    {
        model.objects.front()->config.set("skirt_loops", 1);
        expected_eligible = false;
    }
    SECTION("legacy is not opted into clipping")
    {
        config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
        config.set("raft_layers", 3);
        expected_eligible = false;
    }
    SECTION("sequential printing is not opted into clipping") { params.is_seq_print = true; }
    SECTION("height limit is retained") { params.printable_height = 1.f; }
    SECTION("cancellation does not place a fallback object")
    {
        params.stopcondition = []() { return true; };
    }
    SECTION("every bed retains its exclusion region")
    {
        ArrangePolygon exclusion;
        exclusion.poly = ExPolygon(
            Polygon{{scaled(49.), scaled(49.)}, {scaled(51.), scaled(49.)}, {scaled(51.), scaled(51.)}, {scaled(49.), scaled(51.)}});
        exclusion.is_virt_object = true;
        params.excluded_regions.push_back(exclusion);
    }
    ArrangePolygons items{get_instance_arrange_poly(model.objects.front()->instances.front(), config)};
    items.front().bed_idx = arrangement::UNARRANGED;
    REQUIRE(items.front().allow_raft_bed_clipping == expected_eligible);
    update_arrange_params(params, &config, items);
    update_selected_items_inflation(items, &config, params);
    // Keep the negative control too large for ordinary packing, including
    // Legacy's historical fit-to-bed inflation clamp.
    items.front().inflation = std::max(items.front().inflation, scaled(3.));
    arrangement::arrange(items, {}, get_shrink_bedpts(&config, params), params);
    REQUIRE(items.front().bed_idx < 0);
}

TEST_CASE("Unrafted models at the usable bed size fit separate empty beds", "[Arrange][Raft][BedClip]")
{
    const double bed_size = GENERATE(100., 101., 180.);
    const bool   occupied = GENERATE(false, true);
    const double spacing  = GENERATE(0., 10.);
    const BrimType brim     = GENERATE(btNoBrim, btOuterOnly, btInnerOnly, btOuterAndInner, btAutoBrim);
    CAPTURE(bed_size, occupied, spacing, brim);
    Model model = make_model(make_cube(bed_size - 2., bed_size - 2., 2.));
    // Exercise two instances of one object; each must get its own empty bed.
    model.objects.front()->add_instance();
    DynamicPrintConfig config = unrafted_bed_config(bed_size);
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4});
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.5, false));
    config.set_key_value("line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set_key_value("inner_wall_line_width", new ConfigOptionFloatOrPercent(0.45, false));
    // Effective object settings, even when the global profile disables brim.
    model.objects.front()->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(brim));
    model.objects.front()->config.set("brim_width", 2.);
    model.objects.front()->config.set("brim_object_gap", 0.3);
    ArrangePolygons items;
    for (ModelInstance* instance : model.objects.front()->instances)
        items.push_back(get_instance_arrange_poly(instance, config));
    ArrangePolygons fixed;
    if (occupied) {
        ArrangePolygon obstacle;
        obstacle.poly = ExPolygon(Polygon{{scaled(45.), scaled(45.)}, {scaled(55.), scaled(45.)},
                                          {scaled(55.), scaled(55.)}, {scaled(45.), scaled(55.)}});
        obstacle.bed_idx = 0;
        fixed.push_back(obstacle);
    }
    ArrangeParams params;
    params.parallel         = false;
    params.min_obj_distance = scaled(spacing);
    params.progressind      = [](unsigned, std::string) {};
    update_arrange_params(params, &config, items);
    update_selected_items_inflation(items, &config, params);
    const Points      bed = get_shrink_bedpts(&config, params);
    const BoundingBox bounds(bed);
    REQUIRE(bounds.size().x() == scaled(bed_size - 2.));
    arrangement::arrange(items, fixed, bed, params);
    REQUIRE(items[0].bed_idx >= (occupied ? 1 : 0));
    REQUIRE(items[1].bed_idx >= (occupied ? 1 : 0));
    REQUIRE(items[0].bed_idx != items[1].bed_idx);
    for (const ArrangePolygon& item : items) {
        REQUIRE(item.allow_unrafted_bed_fit == (brim == btNoBrim));
        REQUIRE(item.allow_brim_bed_clipping == (brim != btNoBrim));
        REQUIRE_FALSE(item.allow_raft_bed_clipping);
        if (brim == btOuterOnly || brim == btOuterAndInner)
            REQUIRE(item.brim_bed_clipping_margin > 2.3);
        if (brim == btAutoBrim)
            REQUIRE(item.brim_bed_clipping_margin == Approx(18.3 + 0.5 * 0.5));
        REQUIRE(item.inflation == scaled(spacing > 0. ? spacing / 2. : 1.));
        REQUIRE(bounds.contains(get_extents(item.transformed_poly())));
        REQUIRE(get_extents(item.transformed_poly()).center() == bounds.center());
        // The model fits, but its unchanged pairwise spacing does not.
        REQUIRE_FALSE(diff_ex(offset_ex(item.transformed_poly(), item.inflation), Polygons{Polygon(bed)}).empty());
    }
}

TEST_CASE("Unrafted empty-bed retry preserves attachments and hard bed constraints", "[Arrange][Raft][BedClip]")
{
    Model              model  = make_model(make_cube(99., 99., 2.));
    DynamicPrintConfig config = unrafted_bed_config(101.);
    const BrimType     brim   = GENERATE(btNoBrim, btOuterAndInner, btAutoBrim);
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(brim));
    CAPTURE(brim);
    ArrangeParams params;
    params.parallel    = false;
    params.progressind = [](unsigned, std::string) {};
    bool expected_eligible = true;
    SECTION("a 100 mm model still fails the 99 mm usable width")
    {
        model.objects.front()->instances.front()->set_scaling_factor(Vec3d(100. / 99., 100. / 99., 1.));
    }
    SECTION("enabled support remains reserved even without an overhang")
    {
        config.set("enable_support", true);
        expected_eligible = false;
    }
    SECTION("forced support remains reserved")
    {
        model.objects.front()->config.set("enforce_support_layers", 1);
        expected_eligible = false;
    }
    SECTION("manual support remains reserved")
    {
        model.objects.front()->add_volume(make_cube(1., 1., 1.))->set_type(ModelVolumeType::SUPPORT_ENFORCER);
        expected_eligible = false;
    }
    SECTION("active legacy raft remains reserved")
    {
        config.set("raft_layers", 3);
        expected_eligible = false;
    }
    SECTION("non-FFF printing retains its existing policy")
    {
        config.set_key_value("printer_technology", new ConfigOptionEnum<PrinterTechnology>(ptSLA));
        expected_eligible = false;
    }
    SECTION("painted ears retain their original policy")
    {
        config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btPainted));
        expected_eligible = false;
    }
    SECTION("automatic ears retain their original policy")
    {
        config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btEar));
        expected_eligible = false;
    }
    SECTION("skirts remain reserved")
    {
        model.objects.front()->config.set("skirt_loops", 1);
        expected_eligible = false;
    }
    SECTION("height limit is retained") { params.printable_height = 1.f; }
    SECTION("sequential printing retains its policy") { params.is_seq_print = true; }
    SECTION("cancellation does not place a fallback object") { params.stopcondition = []() { return true; }; }
    SECTION("exclusion regions remain reserved on every bed")
    {
        ArrangePolygon exclusion;
        exclusion.poly = ExPolygon(Polygon{{scaled(49.), scaled(49.)}, {scaled(51.), scaled(49.)},
                                           {scaled(51.), scaled(51.)}, {scaled(49.), scaled(51.)}});
        exclusion.is_virt_object = true;
        params.excluded_regions.push_back(exclusion);
    }
    ArrangePolygons items{get_instance_arrange_poly(model.objects.front()->instances.front(), config)};
    items.front().bed_idx = arrangement::UNARRANGED;
    REQUIRE(items.front().allow_unrafted_bed_fit == (expected_eligible && brim == btNoBrim));
    REQUIRE(items.front().allow_brim_bed_clipping == (expected_eligible && brim != btNoBrim));
    REQUIRE_FALSE(items.front().allow_raft_bed_clipping);
    update_arrange_params(params, &config, items);
    update_selected_items_inflation(items, &config, params);
    arrangement::arrange(items, {}, get_shrink_bedpts(&config, params), params);
    REQUIRE(items.front().bed_idx < 0);
}

TEST_CASE("Empty-bed brim fitting reserves brim against exclusions without changing pairwise spacing", "[Arrange][Raft][BedClip]")
{
    Model              model  = make_model(make_cube(99., 50., 2.));
    DynamicPrintConfig config = unrafted_bed_config(101.);
    const bool         automatic = GENERATE(false, true);
    CAPTURE(automatic);
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(automatic ? btAutoBrim : btOuterOnly));
    config.set("brim_width", 5.);
    config.set("brim_object_gap", 0.);
    ArrangePolygons items{get_instance_arrange_poly(model.objects.front()->instances.front(), config)};
    ArrangeParams   params;
    params.parallel        = false;
    params.allow_rotations = false;
    params.progressind     = [](unsigned, std::string) {};
    ArrangePolygon exclusion;
    // At the centred candidate this is outside the model and its 1 mm spacing,
    // but inside the brim's actual reservation. It applies to every empty bed.
    // The automatic case is beyond the configured 5 mm but within its 18 mm cap.
    const double exclusion_y = automatic ? 88. : 78.;
    exclusion.poly           = ExPolygon(Polygon{{scaled(45.), scaled(exclusion_y)},
                                                 {scaled(55.), scaled(exclusion_y)},
                                                 {scaled(55.), scaled(exclusion_y + 1.)},
                                                 {scaled(45.), scaled(exclusion_y + 1.)}});
    exclusion.is_virt_object = true;
    params.excluded_regions.push_back(exclusion);
    update_arrange_params(params, &config, items);
    update_selected_items_inflation(items, &config, params);
    REQUIRE(items.front().allow_brim_bed_clipping);
    REQUIRE(items.front().brim_bed_clipping_margin > (automatic ? 18. : 5.));
    REQUIRE(items.front().inflation == scaled(1.));
    arrangement::arrange(items, {}, get_shrink_bedpts(&config, params), params);
    REQUIRE(items.front().bed_idx < 0);
    REQUIRE(items.front().inflation == scaled(1.));
}

TEST_CASE("Empty-bed model fitting uses effective object settings", "[Arrange][Raft][BedClip]")
{
    Model              model  = make_model(make_cube(99., 60., 2.));
    DynamicPrintConfig config = unrafted_bed_config(101.);
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("enable_support", true);
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    ModelObject& object = *model.objects.front();
    object.config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    object.config.set("raft_layers", 0);
    object.config.set("enable_support", false);
    object.config.set("skirt_loops", 0);
    const BrimType brim             = GENERATE(btNoBrim, btAutoBrim);
    const double   configured_width = GENERATE(0., 2.);
    CAPTURE(brim, configured_width);
    object.config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(brim));
    object.config.set("brim_width", configured_width);
    ArrangePolygons items{get_instance_arrange_poly(object.instances.front(), config)};
    REQUIRE(items.front().allow_unrafted_bed_fit == (brim == btNoBrim));
    REQUIRE(items.front().allow_brim_bed_clipping == (brim == btAutoBrim));
    if (brim == btAutoBrim)
        REQUIRE(items.front().brim_bed_clipping_margin > 18.);
    ArrangeParams params;
    params.parallel    = false;
    params.progressind = [](unsigned, std::string) {};
    update_arrange_params(params, &config, items);
    update_selected_items_inflation(items, &config, params);
    const Points bed = get_shrink_bedpts(&config, params);
    arrangement::arrange(items, {}, bed, params);
    REQUIRE(items.front().bed_idx == 0);
    REQUIRE(BoundingBox(bed).contains(get_extents(items.front().transformed_poly())));
}

TEST_CASE("Fixed raft reservations keep their actual positions through final alignment", "[Arrange][Raft][Fixed]")
{
    const bool                 custom_center = GENERATE(false, true);
    Model                      model;
    const std::array<Vec3d, 3> positions{Vec3d(56.55, 58.55, 5.05), Vec3d(56.55, 42.45, 5.05), Vec3d(42.45, 51.588883, 5.05)};
    for (const Vec3d& position : positions) {
        ModelObject* object = model.add_object();
        TriangleMesh mesh   = make_cube(10.1, 10.1, 10.1);
        mesh.translate(-5.05f, -5.05f, -5.05f);
        object->add_volume(std::move(mesh));
        object->add_instance()->set_offset(position);
    }
    DynamicPrintConfig config = raft_config(RaftMode::CuraV1);
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {100., 0.}, {100., 100.}, {0., 100.}}));
    config.set("skirt_loops", 0);
    config.erase("filament_type");
    ArrangePolygons fixed{get_instance_arrange_poly(model.objects[0]->instances[0], config)};
    ArrangePolygons items{get_instance_arrange_poly(model.objects[1]->instances[0], config),
                          get_instance_arrange_poly(model.objects[2]->instances[0], config)};
    fixed.front().inflation = items.front().inflation = scaled(7.60735);
    items.back().inflation                            = scaled(3.25);
    ArrangeParams params;
    params.parallel    = false;
    params.progressind = [](unsigned, std::string) {};
    if (custom_center)
        params.align_center = Vec2d(0.25, 0.75);
    arrangement::arrange(items, fixed, get_shrink_bedpts(&config, params), params);
    const ExPolygons fixed_area = offset_ex(fixed.front().transformed_poly(), fixed.front().inflation - scaled(0.001), ClipperLib::jtSquare);
    for (ArrangePolygon& item : items) {
        REQUIRE(item.bed_idx == 0);
        REQUIRE(
            intersection_ex(fixed_area, offset_ex(item.transformed_poly(), item.inflation - scaled(0.001), ClipperLib::jtSquare)).empty());
        item.apply();
    }
    REQUIRE(model.objects[0]->instances[0]->get_offset().isApprox(positions[0]));
}

TEST_CASE("Fixed raft crossing the bed edge is still an obstacle", "[Arrange][Raft][Fixed][BedClip]")
{
    ArrangePolygon fixed;
    fixed.poly = ExPolygon(
        Polygon{{scaled(1.), scaled(1.)}, {scaled(99.), scaled(1.)}, {scaled(99.), scaled(99.)}, {scaled(1.), scaled(99.)}});
    fixed.bed_idx   = 0;
    fixed.inflation = scaled(3.);
    ArrangePolygon moving;
    moving.poly   = ExPolygon(Polygon{{0, 0}, {scaled(10.), 0}, {scaled(10.), scaled(10.)}, {0, scaled(10.)}});
    moving.height = 10.;
    ArrangePolygons items{moving};
    ArrangeParams   params;
    params.parallel    = false;
    params.progressind = [](unsigned, std::string) {};
    arrangement::arrange(items, {fixed}, BoundingBox(Point::new_scale(1., 1.), Point::new_scale(99., 99.)), params);
    // An occupied bed must never be reported as a successful placement, even
    // if the upstream packer retries it after temporarily removing obstacles.
    REQUIRE(items.front().bed_idx != 0);
}

TEST_CASE("Cura V1 keeps independent margins for multiple objects and instances", "[Arrange][Raft][ModelProcess]")
{
    Model model;
    auto add_cube = [&model]() {
        ModelObject *object = model.add_object();
        object->add_volume(make_cube(20.0, 20.0, 20.0));
        object->add_instance();
        return object;
    };

    ModelObject *global_object = add_cube();
    ModelObject *local_object = add_cube();
    ModelObject *mode_override_object = add_cube();
    local_object->add_instance();

    DynamicPrintConfig config = raft_config(RaftMode::CuraV1);
    config.set("raft_base_margin", 3.0);
    config.set("raft_interface_margin", 5.0);
    config.set("raft_interface_layers", 2);
    config.set("raft_surface_margin", 2.0);
    config.erase("filament_type");

    // Resolve every key independently: local values override only the keys
    // present on this object, while base margin still falls back to global.
    local_object->config.set("raft_interface_layers", 0);
    local_object->config.set("raft_surface_margin", 7.0);
    mode_override_object->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));

    ArrangePolygons selected;
    selected.emplace_back(get_instance_arrange_poly(global_object->instances.front(), config));
    selected.emplace_back(get_instance_arrange_poly(local_object->instances.front(), config));
    selected.emplace_back(get_instance_arrange_poly(local_object->instances.back(), config));
    selected.emplace_back(get_instance_arrange_poly(mode_override_object->instances.front(), config));

    REQUIRE(selected[0].brim_width == Approx(5.0));
    REQUIRE(selected[1].brim_width == Approx(7.5));
    REQUIRE(selected[2].brim_width == Approx(7.5));
    REQUIRE(selected[3].brim_width == Approx(1.0));

    ArrangeParams params;
    update_selected_items_inflation(selected, &config, params);
    REQUIRE(selected[0].inflation == scaled(5.0));
    REQUIRE(selected[1].inflation == scaled(7.5));
    REQUIRE(selected[2].inflation == scaled(7.5));
    REQUIRE(selected[3].inflation == scaled(1.0));

    ArrangePolygons unselected{get_instance_arrange_poly(local_object->instances.front(), config)};
    params.min_obj_distance = scaled(2.0);
    update_unselected_items_inflation(unselected, &config, params);
    REQUIRE(unselected.front().inflation == scaled(7.5));

    DynamicPrintConfig legacy_global = raft_config(RaftMode::Legacy);
    legacy_global.set("raft_base_margin", 4.0);
    legacy_global.set("raft_interface_margin", 6.0);
    legacy_global.set("raft_interface_layers", 2);
    legacy_global.set("raft_surface_margin", 2.0);
    legacy_global.erase("filament_type");
    mode_override_object->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    const ArrangePolygon local_cura = get_instance_arrange_poly(mode_override_object->instances.front(), legacy_global);
    REQUIRE(local_cura.brim_width == Approx(6.0));
}

TEST_CASE("Cura V1 combines normal support with relative raft expansion", "[Arrange][Raft][Support]")
{
    Model model = make_model(make_cube(20.0, 20.0, 20.0));
    ModelInstance *instance = model.objects.front()->instances.front();
    instance->set_rotation(X, Geometry::deg2rad(15.0));
    DynamicPrintConfig config = support_config(stNormalAuto);
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("raft_base_margin", 3.0);
    config.set("raft_interface_margin", 1.0);
    config.set("raft_interface_layers", 0);
    config.set("raft_surface_margin", 1.0);

    // SupportCommon starts with Surface margin plus its 0.5 mm stability
    // expansion, so Base adds only (3 - 1 - 0.5) mm beyond support.
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(7.5));

    config.set("raft_base_margin", 8.0);
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(12.5));

    config.set("raft_base_margin", 3.0);
    config.set("raft_surface_margin", 9.0);
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(9.5));

    config.set("raft_surface_margin", 1.0);
    config.set("raft_interface_margin", 10.0);
    config.set("raft_interface_layers", 2);
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(15.0));

    config.set("raft_interface_layers", 0);
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(7.5));
}

TEST_CASE("Cura V1 follows the active tree support raft generator", "[Arrange][Raft][Support]")
{
    Model model = make_model(make_cube(20.0, 20.0, 20.0));
    ModelInstance *instance = model.objects.front()->instances.front();
    instance->set_rotation(X, Geometry::deg2rad(15.0));
    DynamicPrintConfig config = support_config(stTreeAuto);
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    config.set("raft_base_margin", 3.0);
    config.set("raft_interface_margin", 1.0);
    config.set("raft_interface_layers", 0);
    config.set("raft_surface_margin", 1.0);

    // Default resolves to Organic and uses SupportCommon's relative expansion.
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsDefault));
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(13.5));

    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsTreeOrganic));
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(13.5));

    // Slim, Strong and Hybrid build raft_areas from the complete tree-support
    // footprint and then offset that area by the full active phase margin.
    for (SupportMaterialStyle style : {smsTreeSlim, smsTreeStrong, smsTreeHybrid}) {
        config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(style));
        INFO("support style: " << static_cast<int>(style));
        REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(15.0));
    }

    config.set("enable_support", false);
    config.set("raft_base_margin", 1.0);
    config.set("raft_surface_margin", 3.0);
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsTreeSlim));
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(3.0));

    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsTreeOrganic));
    REQUIRE(estimate_arrange_support_margin(*instance, config) == Approx(3.5));
}

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

TEST_CASE("CLI split-by-color keeps color groups on separate plates", "[Arrange][CLI][Color]")
{
    REQUIRE(resolve_arrange_allow_multicolor_oneplate(true, false));
    REQUIRE_FALSE(resolve_arrange_allow_multicolor_oneplate(false, false));
    REQUIRE_FALSE(resolve_arrange_allow_multicolor_oneplate(true, true));
    REQUIRE_FALSE(resolve_arrange_allow_multicolor_oneplate(false, true));
}

TEST_CASE("CLI color arrange groups are independent from filament assignments", "[Arrange][CLI][ColorGroup]")
{
    CHECK(resolve_model_arrange_group("manual", true, true, "#FF0000") == "manual");
    CHECK(resolve_model_arrange_group("", true, true, "#FF0000") == "#FF0000");
    CHECK(resolve_model_arrange_group("", true, true, "") == "__DEFAULT_COLOR__");
    CHECK(resolve_model_arrange_group("", false, true, "") == "__DEFAULT_COLOR__");
    CHECK(resolve_model_arrange_group("", false, false, "").empty());

    const std::set<std::string> no_groups;
    const std::set<std::string> red_group{"#FF0000"};
    const std::set<std::string> mixed_groups{"#00FF00", "#FF0000"};
    CHECK(arrangement::arrange_groups_compatible("#FF0000", no_groups));
    CHECK(arrangement::arrange_groups_compatible("", red_group));
    CHECK(arrangement::arrange_groups_compatible("#FF0000", red_group));
    CHECK_FALSE(arrangement::arrange_groups_compatible("#00FF00", red_group));
    CHECK_FALSE(arrangement::arrange_groups_compatible("#FF0000", mixed_groups));
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
    first_object->arrange_group = "#FF0000";
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
    REQUIRE(merged_first->arrange_group == "#FF0000");
    REQUIRE(get_instance_arrange_poly(merged_first->instances.front(), DynamicPrintConfig::full_print_config()).arrange_group == "#FF0000");
    REQUIRE(merged_second->config.opt_float("layer_height") == Approx(0.28));
    const DynamicPrintConfig global_config = DynamicPrintConfig::full_print_config();
    REQUIRE(estimate_arrange_support_margin(*merged_first->instances.front(), global_config) == Approx(1.0));
    const double merged_tree_margin = estimate_arrange_support_margin(*merged_second->instances.front(), global_config);
    REQUIRE(merged_tree_margin == Approx(12.0));
}
