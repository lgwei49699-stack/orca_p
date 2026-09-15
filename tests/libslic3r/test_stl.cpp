#include <catch2/catch.hpp>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <iterator>
#include <limits>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <locale>
#include <sstream>

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

void append_stl_u32(std::string& data, uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        data.push_back(char((value >> shift) & 0xff));
}

float read_stl_float(const std::string& data, size_t offset)
{
    REQUIRE(data.size() >= offset + 4);
    uint32_t bits = 0;
    for (size_t i = 0; i < 4; ++i)
        bits |= uint32_t(static_cast<unsigned char>(data[offset + i])) << (8 * i);
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::string zero_normal_binary_stl(const indexed_triangle_set& mesh, size_t count, size_t header_size, bool solid_header)
{
    std::string data = solid_header ? "solid still a binary STL" : "binary STL with zero normals";
    data.resize(header_size, '\0');
    append_stl_u32(data, uint32_t(count));
    for (size_t face = 0; face < count; ++face) {
        data.append(12, '\0');
        for (int index : mesh.indices[face])
            for (size_t axis = 0; axis < 3; ++axis) {
                const float coordinate = mesh.vertices[index][axis];
                uint32_t    bits;
                static_assert(sizeof(bits) == sizeof(coordinate));
                std::memcpy(&bits, &coordinate, sizeof(bits));
                append_stl_u32(data, bits);
            }
        data.append(2, '\0');
    }
    return data;
}

void write_stl_test_file(const boost::filesystem::path& path, const std::string& data)
{
    boost::nowide::ofstream stream(path.string(), std::ios::binary);
    REQUIRE(stream.is_open());
    stream.write(data.data(), std::streamsize(data.size()));
    stream.close();
    REQUIRE_FALSE(stream.fail());
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

TEST_CASE("Binary STL import uses size and count instead of high bytes", "[stl][Import][Binary]")
{
    const size_t      count        = GENERATE(size_t(1), size_t(2), size_t(12));
    const size_t      header_size  = GENERATE(size_t(80), size_t(128));
    const bool        solid_header = GENERATE(false, true);
    const auto        original     = make_cube(10., 10., 10.).its;
    const std::string data         = zero_normal_binary_stl(original, count, header_size, solid_header);
    REQUIRE(data.size() == header_size + 4 + count * 50);
    // These bytes fooled the previous probe. One/two-facet files are also
    // shorter than its unconditional 128-byte facet read.
    REQUIRE(std::all_of(data.begin() + header_size + 4, data.end(), [](unsigned char c) { return c <= 127; }));
    const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orca-stl-binary-%%%%-%%%%.stl");
    write_stl_test_file(path, data);

    TriangleMesh     mesh;
    MeshRepairReport report;
    REQUIRE(mesh.ReadSTLFile(path.string().c_str(), false, nullptr, int(header_size), &report));
    REQUIRE(mesh.its.indices.size() == count);
    CHECK(report.load_succeeded);
    CHECK_FALSE(report.attempted);
    CHECK(report.original_facets == count);
    for (size_t face = 0; face < count; ++face)
        for (size_t corner = 0; corner < 3; ++corner)
            CHECK(mesh.its.vertices[mesh.its.indices[face][corner]] == original.vertices[original.indices[face][corner]]);

    // GUI and CLI both reach the same reader through load_stl(). Legacy repair
    // remains optional and produces its existing structured report.
    Model      model;
    const bool repair = count == 12;
    REQUIRE(load_stl(path.string().c_str(), &model, nullptr, nullptr, int(header_size), repair, &report));
    CHECK(report.load_succeeded);
    CHECK(report.enabled == repair);
    CHECK(report.attempted == repair);
    CHECK(model.objects.front()->volumes.front()->mesh().facets_count() == count);
    CHECK(read_stl_test_file(path) == data);
    CHECK(boost::filesystem::remove(path));
}

TEST_CASE("Binary STL invalid data and cancellation do not publish geometry", "[stl][Import][Binary]")
{
    auto      original = make_cube(10., 10., 10.).its;
    const int failure  = GENERATE(0, 1, 2, 3, 4, 5);
    if (failure == 0)
        original.vertices.front().x() = std::numeric_limits<float>::quiet_NaN();
    else if (failure == 1)
        original.vertices.front().x() = std::numeric_limits<float>::infinity();
    std::string data = zero_normal_binary_stl(original, 12, 80, false);
    if (failure == 2)
        data.pop_back();
    else if (failure == 3)
        std::fill(data.begin() + 80, data.begin() + 84, char(0xff)); // Never allocate from a bogus count.
    else if (failure == 4)
        data = std::string(84, '\0'); // Empty binary STL.
    const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orca-stl-invalid-%%%%-%%%%.stl");
    write_stl_test_file(path, data);
    Model               model;
    auto*               object          = model.add_object();
    auto*               volume          = object->add_volume(make_cube(1., 1., 1.));
    const auto          mesh            = volume->mesh_ptr();
    bool                callback_called = false;
    ImportstlProgressFn progress;
    if (failure == 5)
        progress = [&](int current, int total, bool& cancel, std::string&, std::string&) {
            CHECK(current == 0);
            CHECK(total == 12);
            callback_called = true;
            cancel          = true;
        };
    MeshRepairReport report;
    CHECK_FALSE(load_stl(path.string().c_str(), &model, nullptr, progress, 80, false, &report));
    CHECK(callback_called == (failure == 5));
    CHECK_FALSE(report.load_succeeded);
    REQUIRE(model.objects.size() == 1);
    CHECK(model.objects.front() == object);
    REQUIRE(object->volumes.size() == 1);
    CHECK(object->volumes.front()->mesh_ptr() == mesh);
    CHECK(read_stl_test_file(path) == data);
    CHECK(boost::filesystem::remove(path));
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

    non_finite.vertices.front().x() = std::numeric_limits<float>::quiet_NaN();
    check_rejected(non_finite, "nan-coordinate");

    invalid_index.indices.front()[0] = int(invalid_index.vertices.size());
    check_rejected(invalid_index, "out-of-range-index");

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("STL export preserves finite facets with degenerate or extreme normals", "[stl][CLI][validation][ModelRepair][Partial]")
{
    const bool           binary = GENERATE(false, true);
    const int            shape  = GENERATE(0, 1, 2, 3);
    indexed_triangle_set geometry;
    geometry.indices = {{0, 1, 2}};
    if (shape == 0) {
        geometry.vertices           = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
        geometry.indices.front()[1] = 0; // Repeated vertex, not an invalid index.
    } else if (shape == 1) {
        // Exact collinear record retained by the user's 2.4.2 export.
        geometry.vertices = {{140.546630859375f, 139.074462890625f, 0.f},
                             {140.54669189453125f, 139.0745849609375f, 0.f},
                             {140.5467529296875f, 139.07470703125f, 0.f}};
    } else if (shape == 2) {
        const float edge  = std::ldexp(1.f, -100);
        geometry.vertices = {{0.f, 0.f, 0.f}, {edge, 0.f, 0.f}, {0.f, edge, 0.f}};
    } else {
        const float edge  = std::numeric_limits<float>::max();
        geometry.vertices = {{-edge, 0.f, 0.f}, {edge, 0.f, 0.f}, {0.f, edge, 0.f}};
    }
    TriangleMesh mesh(geometry);
    const auto   directory = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orcaslicer-stl-normal-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const auto output = directory / "model.stl";
    write_stl_test_file(output, "previous output\n");
    REQUIRE(store_stl(output.string().c_str(), &mesh, binary));
    CHECK(mesh.its.indices == geometry.indices);
    CHECK(mesh.its.vertices == geometry.vertices);
    CHECK_FALSE(has_stl_atomic_artifacts(directory));

    const auto           data = read_stl_test_file(output);
    Vec3f                normal;
    std::array<Vec3f, 3> vertices;
    if (binary) {
        REQUIRE(data.size() == 134); // Exactly one facet, including a zero-area one.
        CHECK(data.substr(80, 4) == std::string("\1\0\0\0", 4));
        for (size_t axis = 0; axis < 3; ++axis)
            normal[axis] = read_stl_float(data, 84 + axis * 4);
        for (size_t corner = 0; corner < 3; ++corner)
            for (size_t axis = 0; axis < 3; ++axis)
                vertices[corner][axis] = read_stl_float(data, 96 + corner * 12 + axis * 4);
    } else {
        std::istringstream input(data);
        input.imbue(std::locale::classic());
        std::string line, token;
        REQUIRE(bool(std::getline(input, line)));
        input >> token;
        REQUIRE(token == "facet");
        input >> token >> normal[0] >> normal[1] >> normal[2];
        REQUIRE(token == "normal");
        input >> token;
        REQUIRE(token == "outer");
        input >> token;
        REQUIRE(token == "loop");
        for (auto& vertex : vertices) {
            input >> token >> vertex[0] >> vertex[1] >> vertex[2];
            REQUIRE(token == "vertex");
        }
        REQUIRE_FALSE(input.fail());
        input >> token;
        REQUIRE(token == "endloop");
        input >> token;
        REQUIRE(token == "endfacet");
        input >> token;
        REQUIRE(token == "endsolid");
    }
    CHECK(normal.allFinite());
    if (shape < 2)
        CHECK(normal == Vec3f::Zero());
    else
        CHECK(normal == Vec3f::UnitZ());
    for (size_t corner = 0; corner < 3; ++corner)
        CHECK(vertices[corner] == geometry.vertices[geometry.indices.front()[corner]]);

    if (shape < 3) {
        TriangleMesh reloaded;
        REQUIRE(reloaded.ReadSTLFile(output.string().c_str(), false));
        REQUIRE(reloaded.facets_count() == 1);
        for (size_t corner = 0; corner < 3; ++corner)
            CHECK(reloaded.its.vertices[reloaded.its.indices.front()[corner]] == vertices[corner]);
    }
    boost::system::error_code error;
    boost::filesystem::remove_all(directory, error);
    CHECK_FALSE(error);
}
