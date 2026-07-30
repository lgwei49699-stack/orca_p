#include <catch2/catch.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/Format/STL.hpp"

using namespace Slic3r;

static inline std::string stl_path(const char* path)
{
	return std::string(TEST_DATA_DIR) + "/test_stl/" + path;
}

SCENARIO("Reading an STL file", "[stl]") {
	GIVEN("umlauts in the path of a binary STL file, Czech characters in the file name") {
        WHEN("STL file is read") {
			Slic3r::Model model;
			THEN("load should succeed") {
                REQUIRE(Slic3r::load_stl(stl_path("Geräte/20mmbox-čřšřěá.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
            }
        }
    }
	GIVEN("in ASCII format") {
		WHEN("line endings LF") {
			Slic3r::Model model;
			THEN("load should succeed") {
				REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-LF.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
			}
		}
		WHEN("line endings CRLF") {
			Slic3r::Model model;
			THEN("load should succeed") {
				REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-CRLF.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
			}
		}
#if 0
		// ASCII STLs ending with just carriage returns are not supported. These were used by the old Macs, while the Unix based MacOS uses LFs as any other Unix.
		WHEN("line endings CR") {
			Slic3r::Model model;
			THEN("load should succeed") {
				REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-CR.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
			}
		}

#endif
		WHEN("nonstandard STL file (text after ending tags, invalid normals, for example infinities)") {
			Slic3r::Model model;
			THEN("load should succeed") {
				REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-nonstandard.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
			}
		}
	}
}

SCENARIO("STL import exposes a structured repair report", "[stl][repair]") {
    GIVEN("automatic repair is enabled for a valid STL") {
        Slic3r::Model      model;
        MeshRepairReport  report;

        REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-LF.stl").c_str(), &model, nullptr, nullptr, 80, true, &report));
        REQUIRE(report.enabled);
        REQUIRE(report.attempted);
        REQUIRE(report.load_succeeded);
        REQUIRE(report.original_facets > 0);
        REQUIRE(report.final_facets == model.objects.front()->volumes.front()->mesh().facets_count());
        REQUIRE(report.remaining_open_edges == model.objects.front()->volumes.front()->mesh().stats().open_edges);
    }

    GIVEN("automatic repair is explicitly disabled") {
        Slic3r::Model      model;
        MeshRepairReport  report;

        REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-LF.stl").c_str(), &model, nullptr, nullptr, 80, false, &report));
        REQUIRE_FALSE(report.enabled);
        REQUIRE_FALSE(report.attempted);
        REQUIRE(report.load_succeeded);
        REQUIRE(std::string(report.status()) == "skipped");
        REQUIRE(report.initial_open_edges == report.remaining_open_edges);
    }

    GIVEN("repair changes geometry but leaves open edges") {
        MeshRepairReport report;
        report.load_succeeded      = true;
        report.enabled             = true;
        report.attempted           = true;
        report.edges_fixed         = 2;
        report.remaining_open_edges = 3;

        REQUIRE(std::string(report.status()) == "partially_repaired");
    }
}
