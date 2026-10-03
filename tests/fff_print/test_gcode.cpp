#include <catch2/catch.hpp>
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Utils.hpp"

#include <memory>

#include "libslic3r/GCode.hpp"

using namespace Slic3r;

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
