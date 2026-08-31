#include <catch2/catch.hpp>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <iterator>
#include <limits>

#include "libslic3r/Model.hpp"
#include "libslic3r/Format/STL.hpp"

namespace {

std::string read_stl_test_file(const boost::filesystem::path& path)
{
    boost::nowide::ifstream stream(path.string(), std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool has_stl_atomic_artifacts(const boost::filesystem::path& directory)
{
    for (const boost::filesystem::directory_entry& entry : boost::filesystem::directory_iterator(directory)) {
        const std::string filename = entry.path().filename().string();
        if (filename.find(".tmp-") != std::string::npos || filename.find(".bak-") != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

using namespace Slic3r;

static inline std::string stl_path(const char* path) { return std::string(TEST_DATA_DIR) + "/test_stl/" + path; }

SCENARIO("Reading an STL file", "[stl]")
{
    GIVEN("umlauts in the path of a binary STL file, Czech characters in the file name")
    {
        WHEN("STL file is read")
        {
			Slic3r::Model model;
            THEN("load should succeed")
            {
                REQUIRE(Slic3r::load_stl(stl_path("Geräte/20mmbox-čřšřěá.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
            }
        }
    }
    GIVEN("in ASCII format")
    {
        WHEN("line endings LF")
        {
			Slic3r::Model model;
            THEN("load should succeed")
            {
				REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-LF.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
			}
		}
        WHEN("line endings CRLF")
        {
			Slic3r::Model model;
            THEN("load should succeed")
            {
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
        WHEN("nonstandard STL file (text after ending tags, invalid normals, for example infinities)")
        {
			Slic3r::Model model;
            THEN("load should succeed")
            {
				REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-nonstandard.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
			}
		}
	}
}

SCENARIO("STL import exposes a structured repair report", "[stl][repair]")
{
    GIVEN("automatic repair is enabled for a valid STL")
    {
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

    GIVEN("automatic repair is explicitly disabled")
    {
        Slic3r::Model      model;
        MeshRepairReport  report;

        REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-LF.stl").c_str(), &model, nullptr, nullptr, 80, false, &report));
        REQUIRE_FALSE(report.enabled);
        REQUIRE_FALSE(report.attempted);
        REQUIRE(report.load_succeeded);
        REQUIRE(std::string(report.status()) == "skipped");
        REQUIRE(report.initial_open_edges == report.remaining_open_edges);
        REQUIRE(report.remaining_open_edges == 0);
        const indexed_triangle_set& mesh = model.objects.front()->volumes.front()->mesh().its;
        REQUIRE(mesh.vertices.size() < mesh.indices.size() * 3);
    }

    GIVEN("repair changes geometry but leaves open edges")
    {
        MeshRepairReport report;
        report.load_succeeded      = true;
        report.enabled             = true;
        report.attempted           = true;
        report.edges_fixed         = 2;
        report.remaining_open_edges = 3;

        REQUIRE(std::string(report.status()) == "partially_repaired");
    }
}

TEST_CASE("STL export rejects empty meshes and output symlinks", "[stl][CLI]")
{
    const boost::filesystem::path empty_output = boost::filesystem::temp_directory_path() /
                                                 boost::filesystem::unique_path("orcaslicer-empty-stl-%%%%-%%%%.stl");
    TriangleMesh empty_mesh;
    CHECK_FALSE(store_stl(empty_output.string().c_str(), &empty_mesh, true));
    CHECK_FALSE(boost::filesystem::exists(empty_output));

    TriangleMesh                  cube          = make_cube(10., 10., 10.);
    const boost::filesystem::path binary_output = boost::filesystem::temp_directory_path() /
                                                  boost::filesystem::unique_path("orcaslicer-binary-stl-%%%%-%%%%.stl");
    const boost::filesystem::path ascii_output = boost::filesystem::temp_directory_path() /
                                                 boost::filesystem::unique_path("orcaslicer-ascii-stl-%%%%-%%%%.stl");
    REQUIRE(store_stl(binary_output.string().c_str(), &cube, true));
    REQUIRE(store_stl(ascii_output.string().c_str(), &cube, false));
    CHECK(boost::filesystem::file_size(binary_output) > 84);
    CHECK(boost::filesystem::file_size(ascii_output) > 0);

    boost::system::error_code output_error;
    boost::filesystem::remove(binary_output, output_error);
    CHECK_FALSE(output_error);
    boost::filesystem::remove(ascii_output, output_error);
    CHECK_FALSE(output_error);

#ifndef _WIN32
    if (boost::filesystem::exists("/dev/full")) {
        CHECK_FALSE(its_write_stl_binary("/dev/full", "", cube.its));
        CHECK_FALSE(its_write_stl_ascii("/dev/full", "", cube.its));
    }

    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-stl-symlink-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const boost::filesystem::path victim = directory / "victim.txt";
    {
        boost::nowide::ofstream stream(victim.string());
        REQUIRE(stream.is_open());
        stream << "do not replace";
    }
    const boost::filesystem::path output = directory / "output.stl";
    boost::filesystem::create_symlink(victim, output);

    CHECK_FALSE(store_stl(output.string().c_str(), &cube, true));
    boost::nowide::ifstream stream(victim.string());
    const std::string       contents{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    CHECK(contents == "do not replace");

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
#endif
}

TEST_CASE("STL export replaces existing output transactionally", "[stl][CLI][atomic]")
{
    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-stl-atomic-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const boost::filesystem::path output = directory / "model.stl";
    {
        boost::nowide::ofstream stream(output.string(), std::ios::binary);
        REQUIRE(stream.is_open());
        stream << "previous valid output\n";
    }

    TriangleMesh cube = make_cube(10., 10., 10.);
    REQUIRE(store_stl(output.string().c_str(), &cube, true));
    const std::string committed_output = read_stl_test_file(output);
    CHECK(committed_output.find("previous valid output") == std::string::npos);
    CHECK(committed_output.size() > 84);
    CHECK_FALSE(has_stl_atomic_artifacts(directory));

    TriangleMesh empty_mesh;
    CHECK_FALSE(store_stl(output.string().c_str(), &empty_mesh, true));
    CHECK(read_stl_test_file(output) == committed_output);
    CHECK_FALSE(has_stl_atomic_artifacts(directory));

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("STL writers reject invalid mesh data before creating output", "[stl][CLI][validation]")
{
    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-stl-validation-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));

    const auto check_rejected = [&](const indexed_triangle_set& mesh, const std::string& stem) {
        const boost::filesystem::path binary_output = directory / (stem + "-binary.stl");
        const boost::filesystem::path ascii_output  = directory / (stem + "-ascii.stl");
        CHECK_FALSE(its_write_stl_binary(binary_output.string().c_str(), "", mesh));
        CHECK_FALSE(its_write_stl_ascii(ascii_output.string().c_str(), "", mesh));
        CHECK_FALSE(boost::filesystem::exists(binary_output));
        CHECK_FALSE(boost::filesystem::exists(ascii_output));
    };

    indexed_triangle_set invalid_index = make_cube(10., 10., 10.).its;
    invalid_index.indices.front()[0]   = -1;
    check_rejected(invalid_index, "invalid-index");

    indexed_triangle_set non_finite = make_cube(10., 10., 10.).its;
    non_finite.vertices.front().x() = std::numeric_limits<float>::infinity();
    check_rejected(non_finite, "non-finite");

    indexed_triangle_set degenerate = make_cube(10., 10., 10.).its;
    degenerate.indices.front()[1]   = degenerate.indices.front()[0];
    check_rejected(degenerate, "degenerate");

    indexed_triangle_set overflowing_edge;
    const float          max_value = std::numeric_limits<float>::max();
    overflowing_edge.vertices      = {{-max_value, 0.f, 0.f}, {max_value, 0.f, 0.f}, {0.f, 1.f, 0.f}};
    overflowing_edge.indices       = {{0, 1, 2}};
    check_rejected(overflowing_edge, "overflowing-edge");

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}
