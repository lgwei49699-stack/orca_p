#include <catch2/catch.hpp>
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Utils.hpp"

#include <memory>
#include <fstream>
#include <boost/filesystem.hpp>

#include "libslic3r/GCode.hpp"
#include "libslic3r/GCode/WipeTower2.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Print.hpp"

using namespace Slic3r;

namespace Slic3r {
// Existing solver used by ToolOrdering, exercised without constructing a tower.
std::vector<unsigned int> get_extruders_order(const std::vector<std::vector<float>>&,
                                             std::vector<unsigned int>, std::optional<unsigned int>);
}

TEST_CASE("Tool ordering checks matrix bounds before planning swaps", "[GCode][WipeTowerSlots]")
{
    const std::vector<std::vector<float>> matrix{{0.f, 120.f}, {240.f, 0.f}};
    REQUIRE(get_extruders_order(matrix, {}, std::nullopt).empty());
    REQUIRE(get_extruders_order(matrix, {0, 1}, 0) == std::vector<unsigned int>{0, 1});
    REQUIRE_THROWS_AS(get_extruders_order(matrix, {0, 2}, 0), SlicingError);
    REQUIRE_THROWS_AS(get_extruders_order(matrix, {0, 1}, 2), SlicingError);
    REQUIRE_THROWS_AS(get_extruders_order({{0.f, 120.f}, {}}, {0, 1}, 0), SlicingError);
}

TEST_CASE("Wipe tower priming validates material slots before indexing", "[GCode][WipeTowerSlots]")
{
    PrintConfig       config;
    PrintRegionConfig region;
    const int         mode = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(mode);
    std::vector<std::vector<float>> matrix{{0.f, 140.f}, {140.f, 0.f}};
    std::vector<unsigned int>       tools{0, 1};
    if (mode == 1)
        tools = {0, 2}; // A model still refers to a slot missing from the palette.
    if (mode == 2)
        matrix[1].clear();
    if (mode == 3)
        tools.clear();
    if (mode == 4)
        tools = {0}; // No previous tool exists for the last/only priming section.
    WipeTower2 tower(config, region, 0, Vec3d::Zero(), matrix, 0);
    tower.set_extruder(0, config);
    if (mode != 5)
        tower.set_extruder(1, config);
    if (mode == 1 || mode == 2 || mode == 5) {
        REQUIRE_THROWS_AS(tower.prime(0.2f, tools, false), SlicingError);
    } else {
        const auto result = tower.prime(0.2f, tools, false);
        REQUIRE(result.size() == tools.size());
        for (const auto& section : result)
            REQUIRE_FALSE(section.gcode.empty());
    }
}

TEST_CASE("Legacy flushing matrices are completed before slicing and export", "[GCode][FlushMatrixCompatibility]")
{
    const int mode = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    CAPTURE(mode);
    struct ResourcesGuard
    {
        std::string             previous = resources_dir();
        boost::filesystem::path output   = boost::filesystem::temp_directory_path() /
                                         boost::filesystem::unique_path("orca-flush-%%%%-%%%%.gcode");
        ResourcesGuard() { set_resources_dir(std::string(TEST_DATA_DIR) + "/../../resources"); }
        ~ResourcesGuard()
        {
            set_resources_dir(previous);
            boost::system::error_code ignored;
            boost::filesystem::remove(output, ignored);
            boost::filesystem::remove(output.string() + ".tmp", ignored);
        }
    } resources;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(4);
    config.set_num_extruders(4);
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75, 1.75, 1.75}));
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.4, 0.4, 0.4}));
    config.set_key_value("filament_colour", new ConfigOptionStrings({"#FFFFFF", "#000000", "#FF0000", "#FFFF00"}));
    if (mode >= 7)
        config.set_key_value("filament_colour", new ConfigOptionStrings(std::vector<std::string>(mode - 7, "#FFFFFF")));
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {240., 0.}, {240., 240.}, {0., 240.}}));
    config.set_key_value("bed_exclude_area", new ConfigOptionPoints());
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btNoBrim));
    config.set("skirt_loops", 0);
    config.set("enable_support", false);
    config.set("enable_prime_tower", true);
    config.set("single_extruder_multi_material", true);
    config.set("purge_in_prime_tower", true);
    config.set("flush_multiplier", 0.5);
    config.set("before_layer_change_gcode", std::string("G92 E0"));
    config.set_key_value("wipe_tower_x", new ConfigOptionFloats({180.}));
    config.set_key_value("wipe_tower_y", new ConfigOptionFloats({160.}));
    config.set_key_value("flush_volumes_vector", new ConfigOptionFloats({101., 102., 201., 202., 301., 302., 401., 402.}));
    std::vector<double> expected{0., 303., 403., 503., 303., 0., 503., 603., 403., 503., 0., 703., 503., 603., 703., 0.};
    if (mode == 0 || mode == 6) {
        // A complete matrix, including intentional zero pairs, must be untouched.
        expected[1] = mode == 0 ? 123. : 0.;
        expected[4] = 456.;
        config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats(expected));
    } else if (mode == 1 || mode >= 7) {
        config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats({0., 123., 456., 0.}));
        expected[1] = 123.;
        expected[4] = 456.;
    } else if (mode == 2) {
        config.erase("flush_volumes_matrix");
    } else if (mode == 3) {
        config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats());
    } else if (mode == 4) {
        // The row stride of a nonsquare legacy payload is not identifiable.
        config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats({0., 999., 0.}));
    } else {
        config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats());
        config.set_key_value("flush_volumes_vector", new ConfigOptionFloats({101.}));
        for (size_t i = 0; i < 4; ++i)
            for (size_t j = 0; j < 4; ++j)
                expected[i * 4 + j] = i == j ? 0. : 241.;
    }
    for (const auto& key : config.keys()) {
        auto*       value = dynamic_cast<ConfigOptionEnumsGeneric*>(config.option(key));
        const auto* def   = print_config_def.get(key);
        if (value && !value->keys_map && def && def->enum_keys_map) {
            auto* mapped   = new ConfigOptionEnumsGeneric(def->enum_keys_map);
            mapped->values = value->values;
            config.set_key_value(key, mapped);
        }
    }
    Model model;
    for (int i = 0; i < 2; ++i) {
        auto* object = model.add_object();
        auto* volume = object->add_volume(make_cube(8., 8., 1.), ModelVolumeType::MODEL_PART, false);
        volume->config.set("extruder", i == 0 ? 1 : 4);
        object->add_instance()->set_offset(Vec3d(40. + i * 50., 40., 0.));
    }
    Print print;
    print.set_status_silent();
    for (auto* object : model.objects)
        print.auto_assign_extruders(object);
    print.apply(model, config);
    REQUIRE(print.config().flush_volumes_matrix.values == expected);
    REQUIRE(print.full_print_config().option<ConfigOptionFloats>("flush_volumes_matrix")->values == expected);
    REQUIRE(print.config().flush_multiplier.value == 0.5);
    REQUIRE(print.extruders() == std::vector<unsigned int>{0, 3});
    INFO(print.validate().string);
    REQUIRE(print.validate().string.empty());
    print.process();
    GCodeProcessorResult result;
    print.export_gcode(resources.output.string(), &result);
    std::ifstream input(resources.output.string());
    REQUIRE(input.good());
    const std::string gcode{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    REQUIRE(print.tool_ordering().has_wipe_tower());
    REQUIRE(print.wipe_tower_data().number_of_toolchanges > 0);
    REQUIRE(gcode.find("T3") != std::string::npos);
    REQUIRE(gcode.find("; flush_volumes_matrix = " + print.full_print_config().opt_serialize("flush_volumes_matrix")) != std::string::npos);
    // Reapplying the same legacy input must not invalidate a completed result.
    REQUIRE(print.apply(model, config) == Print::APPLY_STATUS_UNCHANGED);
    REQUIRE(print.is_step_done(psWipeTower));
}

SCENARIO("Origin manipulation", "[GCode]") {
	Slic3r::GCode gcodegen;
	WHEN("set_origin to (10,0)") {
    	gcodegen.set_origin(Vec2d(10,0));
    	REQUIRE(gcodegen.origin() == Vec2d(10, 0));
    }
	WHEN("set_origin to (10,0) and translate by (5, 5)") {
		gcodegen.set_origin(Vec2d(10,0));
		gcodegen.set_origin(gcodegen.origin() + Vec2d(5, 5));
		THEN("origin returns reference to point") {
    		REQUIRE(gcodegen.origin() == Vec2d(15,5));
    	}
    }
}

TEST_CASE("Nozzle hardness warnings use explicit values then nozzle-type fallback", "[GCode][NozzleHardness]")
{
    struct ResourcesGuard
    {
        std::string previous = resources_dir();
        ResourcesGuard() { set_resources_dir(std::string(TEST_DATA_DIR) + "/../../resources"); }
        ~ResourcesGuard() { set_resources_dir(previous); }
    } resources;
    struct Case
    {
        int        hrc;
        NozzleType type;
        int        required;
        bool       warning;
    };
    for (const auto& test : std::vector<Case>{{0, ntBrass, 3, true},
                                              {0, ntUndefine, 3, false},
                                              {3, ntBrass, 3, false},
                                              {2, ntHardenedSteel, 3, true},
                                              {0, ntHardenedSteel, 3, false},
                                              {0, ntBrass, 0, false}}) {
        CAPTURE(test.hrc, test.type, test.required);
        PrintConfig config;
        config.nozzle_hrc.value           = test.hrc;
        config.nozzle_type.value          = test.type;
        config.required_nozzle_HRC.values = {test.required};
        GCodeProcessor processor;
        processor.apply_config(config);
        processor.initialize("hardness-regression.gcode");
        processor.process_buffer(
            "G90\nM83\nT0\n;LAYER_CHANGE\n;TYPE:Outer wall\n;WIDTH:0.4\n;HEIGHT:0.2\nG1 Z0.2\nG1 X0 Y0 F1200\nG1 X10 Y0 E1\n");
        processor.finalize(false);
        const auto& result = processor.get_result();
        REQUIRE_FALSE(result.print_statistics.total_volumes_per_extruder.empty());
        const bool warning = std::any_of(result.warnings.begin(), result.warnings.end(),
                                         [](const auto& item) { return item.msg == NOZZLE_HRC_CHECKER; });
        CHECK(warning == test.warning);
    }
}
