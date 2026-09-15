#include <catch2/catch.hpp>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

#include <libslic3r/Format/OBJ.hpp>
#include <libslic3r/Format/STL.hpp>
#include <libslic3r/MeshBoolean.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/ObjColorMapping.hpp>
#include <libslic3r/TexturePainting.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <slic3r/Utils/ModelRepairPolicy.hpp>
#include <slic3r/Utils/ModelExportPolicy.hpp>

using namespace Slic3r;

TEST_CASE("OBJ GUI RGB8 conversion is stable and bounded", "[OBJ][GUIColor]")
{
    for (int value = 0; value <= 255; ++value)
        CHECK(ObjColorMapping::to_byte(value / 255.f) == value);
    CHECK(ObjColorMapping::to_byte(0.5f) == 128);
    CHECK(ObjColorMapping::to_byte(-1.f) == 0);
    CHECK(ObjColorMapping::to_byte(2.f) == 255);
    CHECK(ObjColorMapping::to_byte(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("OBJ GUI discrete colors survive repeated palette creation", "[OBJ][GUIColor]")
{
    const RGBA        red   = {204.f / 255.f, 64.f / 255.f, 55.f / 255.f, 1.f};
    const RGBA        white = {1.f, 1.f, 1.f, 1.f};
    std::vector<RGBA> input = {red, white, red};
    std::vector<RGBA> colors;
    std::vector<int>  labels;
    REQUIRE(ObjColorMapping::collect_exact(input, 16, colors, labels));
    REQUIRE((colors == std::vector<RGBA>{red, white}));
    REQUIRE((labels == std::vector<int>{0, 1, 0}));
    for (int round = 0; round < 5; ++round) {
        input = colors;
        REQUIRE(ObjColorMapping::collect_exact(input, 16, colors, labels));
        CHECK((colors == std::vector<RGBA>{red, white}));
        CHECK((labels == std::vector<int>{0, 1}));
    }
}

TEST_CASE("OBJ GUI exact palette respects reduction and capacity", "[OBJ][GUIColor]")
{
    std::vector<RGBA> input;
    for (int i = 0; i < 16; ++i)
        input.push_back({i / 255.f, 0.f, 0.f, 1.f});
    std::vector<RGBA> colors;
    std::vector<int>  labels;
    REQUIRE(ObjColorMapping::collect_exact(input, 16, colors, labels));
    CHECK(colors.size() == 16);
    // Asking for fewer colors must fall back to the existing reduction algorithm.
    CHECK_FALSE(ObjColorMapping::collect_exact(input, 2, colors, labels));
    CHECK(colors.empty());
    CHECK(labels.empty());
    input.push_back({16.f / 255.f, 0.f, 0.f, 1.f});
    CHECK_FALSE(ObjColorMapping::collect_exact(input, 16, colors, labels));
    CHECK(colors.empty());
    CHECK(labels.empty());
}

TEST_CASE("OBJ GUI uses printable RGB rather than alpha as filament identity", "[OBJ][GUIColor]")
{
    const std::vector<RGBA> input = {{1.f, 0.f, 0.f, 1.f}, {1.f, 0.f, 0.f, 0.5f}};
    std::vector<RGBA>       colors;
    std::vector<int>        labels;
    REQUIRE(ObjColorMapping::collect_exact(input, 16, colors, labels));
    CHECK(colors.size() == 1);
    CHECK((labels == std::vector<int>{0, 0}));
}

TEST_CASE("OBJ GUI reuses existing and proposed filament colors", "[OBJ][GUIColor]")
{
    using ObjColorMapping::RGB8;
    const RGB8              red = {204, 64, 55}, white = {255, 255, 255}, green = {0, 255, 0};
    const std::vector<RGB8> input = {red, white, red};
    const auto              first = ObjColorMapping::plan_filaments(input, {green, red}, 16);
    CHECK((first.slots == std::vector<int>{2, 3, 2}));
    CHECK((first.added == std::vector<RGB8>{white}));
    const auto again = ObjColorMapping::plan_filaments(input, {green, red, white}, 16);
    CHECK(again.added.empty());
    CHECK(again.slots == first.slots);
    // Duplicate existing colors reuse the first slot deterministically.
    const auto duplicate = ObjColorMapping::plan_filaments({red}, {red, red}, 16);
    CHECK((duplicate.slots == std::vector<int>{1}));
    CHECK(duplicate.added.empty());
    const auto proposed = ObjColorMapping::plan_filaments({white, white}, {green}, 16);
    CHECK((proposed.slots == std::vector<int>{2, 2}));
    CHECK(proposed.added.size() == 1);
}

TEST_CASE("OBJ GUI full palette still reuses exact colors after overflow", "[OBJ][GUIColor]")
{
    using ObjColorMapping::RGB8;
    const RGB8 red = {255, 0, 0}, white = {255, 255, 255}, green = {0, 255, 0};
    const auto plan = ObjColorMapping::plan_filaments({white, red, green}, {red}, 2);
    CHECK((plan.slots == std::vector<int>{2, 1, 0}));
    CHECK((plan.added == std::vector<RGB8>{white}));
    const auto full = ObjColorMapping::plan_filaments({green, red, white}, {red, white}, 2);
    CHECK((full.slots == std::vector<int>{0, 1, 2}));
    CHECK(full.added.empty());
}

TEST_CASE("OBJ GUI commits only referenced new filaments after manual remapping", "[OBJ][GUIColor]")
{
    using ObjColorMapping::RGB8;
    const RGB8 red = {255, 0, 0}, white = {255, 255, 255};
    // One existing slot; proposed slots 2 and 3. User remapped slot 2 to existing slot 1.
    const auto plan = ObjColorMapping::compact_filaments(1, {red, white}, {1, 3, 3});
    CHECK((plan.added == std::vector<RGB8>{white}));
    CHECK((plan.slots == std::vector<int>{1, 2, 2}));
    const auto all_existing = ObjColorMapping::compact_filaments(1, {red, white}, {1, 1});
    CHECK(all_existing.added.empty());
    CHECK((all_existing.slots == std::vector<int>{1, 1}));
}

namespace {

std::string read_text_file(const boost::filesystem::path& path)
{
    boost::nowide::ifstream stream(path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

size_t count_lines_with_prefix(const std::string& text, const std::string& prefix)
{
    size_t count = 0;
    size_t pos   = 0;
    while (pos < text.size()) {
        const size_t end = text.find('\n', pos);
        if (text.compare(pos, prefix.size(), prefix) == 0)
            ++count;
        if (end == std::string::npos)
            break;
        pos = end + 1;
    }
    return count;
}

bool has_atomic_output_artifacts(const boost::filesystem::path& directory)
{
    for (const boost::filesystem::directory_entry& entry : boost::filesystem::directory_iterator(directory)) {
        const std::string filename = entry.path().filename().string();
        if (filename.find(".tmp-") != std::string::npos || filename.find(".bak-") != std::string::npos)
            return true;
    }
    return false;
}

class ScopedCurrentPath
{
public:
    explicit ScopedCurrentPath(const boost::filesystem::path& path) : m_previous(boost::filesystem::current_path())
    {
        boost::filesystem::current_path(path);
    }

    ~ScopedCurrentPath() { boost::filesystem::current_path(m_previous); }

    ScopedCurrentPath(const ScopedCurrentPath&)            = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

private:
    boost::filesystem::path m_previous;
};

} // namespace

namespace {

struct ObjImportFixture
{
    boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                        boost::filesystem::unique_path("orcaslicer-obj-import-%%%%-%%%%");

    ObjImportFixture()
    {
        REQUIRE(boost::filesystem::create_directory(directory));
        write("palette.mtl", "newmtl color\nKd 1 0 0\nmap_Kd texture.png\n");
    }

    ~ObjImportFixture()
    {
        boost::system::error_code error;
        boost::filesystem::remove_all(directory, error);
    }

    void write(const std::string& name, const std::string& contents) const
    {
        boost::nowide::ofstream stream((directory / name).string());
        REQUIRE(stream.is_open());
        stream << contents;
        stream.close();
        REQUIRE(stream.good());
    }

    void obj(const std::string& faces) const
    {
        write("input.obj", "mtllib palette.mtl\nv 0 0 0 1 0 0\nv 1 0 0 1 0 0\n"
                           "v 1 1 0 1 0 0\nv 0 1 0 1 0 0\n"
                           "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nusemtl color\n" +
                               faces);
    }

    bool load(TriangleMesh& mesh, ObjInfo& info, std::string& message, bool geometry_only = false) const
    {
        return load_obj((directory / "input.obj").string().c_str(), &mesh, info, message, geometry_only);
    }

    Model read_model(bool                             strict_colors,
                     bool*                            ignored        = nullptr,
                     const std::vector<unsigned int>* slots          = nullptr,
                     ObjImportColorFn                 color_callback = nullptr) const
    {
        return Model::read_from_file((directory / "input.obj").string(), nullptr, nullptr, LoadStrategy::AddDefaultInstances, nullptr,
                                     nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, color_callback, false, nullptr, slots,
                                     nullptr, false, strict_colors, ignored);
    }
};

} // namespace

TEST_CASE("OBJ material import tolerates faces with omitted UV coordinates", "[OBJ][CLI][Import]")
{
    ObjImportFixture fixture;
    SECTION("all UVs omitted") { fixture.obj("f 1 2 3\n"); }
    SECTION("normal references without UVs") { fixture.obj("f 1//1 2//1 3//1\n"); }
    SECTION("one corner omits its UV") { fixture.obj("f 1/1 2 3/3\n"); }
    TriangleMesh mesh;
    ObjInfo      info;
    std::string  message;
    REQUIRE(fixture.load(mesh, info, message));
    CHECK(mesh.facets_count() == 1);
    CHECK(info.face_colors.size() == 1);
    CHECK(info.uvs.empty());
    CHECK_FALSE(info.has_uv_png);
    CHECK(info.uv_map_pngs.empty());
}

TEST_CASE("OBJ import rejects invalid UV indices unless explicitly loading geometry only", "[OBJ][CLI][Import]")
{
    ObjImportFixture fixture;
    SECTION("positive out of range") { fixture.obj("f 1/5 2/2 3/3\n"); }
    SECTION("negative out of range") { fixture.obj("f 1/-5 2/-3 3/-2\n"); }
    SECTION("invalid fourth corner") { fixture.obj("f 1/1 2/2 3/3 4/9\n"); }
    TriangleMesh mesh;
    ObjInfo      info;
    std::string  message;
    CHECK_FALSE(fixture.load(mesh, info, message));
    CHECK(message.find("texture coordinate index") != std::string::npos);
    ObjInfo geometry_info;
    REQUIRE(fixture.load(mesh, geometry_info, message, true));
    CHECK(geometry_info.face_colors.empty());
    CHECK(geometry_info.uvs.empty());
}

TEST_CASE("OBJ negative UV indices and quad triangulation preserve UV corner order", "[OBJ][CLI][Import]")
{
    ObjImportFixture fixture;
    fixture.obj("f 1/-4 2/-3 3/-2 4/-1\n");
    TriangleMesh mesh;
    ObjInfo      info;
    std::string  message;
    REQUIRE(fixture.load(mesh, info, message));
    REQUIRE(mesh.facets_count() == 2);
    REQUIRE(info.uvs.size() == 2);
    const std::array<Vec2f, 3> first{Vec2f(0, 0), Vec2f(1, 0), Vec2f(1, 1)};
    const std::array<Vec2f, 3> second{Vec2f(0, 0), Vec2f(1, 1), Vec2f(0, 1)};
    for (size_t i = 0; i < 3; ++i) {
        CHECK((info.uvs[0][i] - first[i]).squaredNorm() == 0.f);
        CHECK((info.uvs[1][i] - second[i]).squaredNorm() == 0.f);
    }
}

TEST_CASE("Geometry-only OBJ import skips a failing MTL parser and all color metadata", "[OBJ][CLI][Import]")
{
    ObjImportFixture fixture;
    fixture.obj("f 1/1 2/2 3/3\n");
    fixture.write("palette.mtl", std::string(70000, 'X'));
    TriangleMesh mesh;
    ObjInfo      info;
    std::string  message;
    CHECK_FALSE(fixture.load(mesh, info, message));
    ObjInfo geometry_info;
    REQUIRE(fixture.load(mesh, geometry_info, message, true));
    CHECK(mesh.facets_count() == 1);
    CHECK(geometry_info.vertex_colors.empty());
    CHECK(geometry_info.face_colors.empty());
    CHECK(geometry_info.material_names.empty());
    CHECK(geometry_info.face_material_ids.empty());
    CHECK(geometry_info.uvs.empty());
    CHECK(geometry_info.pngs.empty());

    // Exercise the Model::read_from_file forwarding used by the CLI, not just the low-level loader.
    Model model = Model::read_from_file((fixture.directory / "input.obj").string(), nullptr, nullptr, LoadStrategy::AddDefaultInstances,
                                        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, false, nullptr, nullptr,
                                        nullptr, true);
    REQUIRE(model.objects.size() == 1);
    REQUIRE(model.objects.front()->volumes.size() == 1);
    CHECK(model.objects.front()->volumes.front()->mmu_segmentation_facets.empty());
}

TEST_CASE("Geometry-only OBJ import still rejects invalid geometry indices", "[OBJ][CLI][Import]")
{
    ObjImportFixture fixture;
    fixture.obj("f 1 2 99\n");
    TriangleMesh mesh;
    ObjInfo      info;
    std::string  message;
    CHECK_FALSE(fixture.load(mesh, info, message, true));
    CHECK(message.find("vertex index") != std::string::npos);
}

TEST_CASE("Strict CLI color import rejects unsupported or unresolved source colors", "[OBJ][CLI][Import]")
{
    ObjImportFixture fixture;
    fixture.write("input.obj", "mtllib palette.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                               "vt 0 0\nvt 1 0\nvt 0 1\nusemtl color\nf 1/1 2/2 3/3\n");
    SECTION("texture colors") {}
    SECTION("vertex colors") { fixture.obj("f 1 2 3\n"); }
    SECTION("missing material") { fixture.write("palette.mtl", "newmtl unrelated\nKd 1 0 0\n"); }
    const auto load = [&](bool geometry_only, bool strict_colors) {
        return Model::read_from_file((fixture.directory / "input.obj").string(), nullptr, nullptr, LoadStrategy::AddDefaultInstances,
                                     nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, false, nullptr, nullptr,
                                     nullptr, geometry_only, strict_colors);
    };
    REQUIRE_THROWS(load(false, true));
    CHECK_NOTHROW(load(false, false)); // Existing import policy remains available outside repair color output.
    CHECK_NOTHROW(load(true, false));
}

TEST_CASE("Missing OBJ libraries drop all source appearance but retain geometry", "[OBJ][CLI][Import][MissingMTL]")
{
    ObjImportFixture fixture;
    std::string      libraries;
    SECTION("missing library") { libraries = "mtllib absent.mtl\n"; }
    SECTION("usemtl without mtllib") {}
    SECTION("one of multiple libraries is missing") { libraries = "mtllib palette.mtl\nmtllib absent.mtl\n"; }
    SECTION("missing library is declared first") { libraries = "mtllib absent.mtl\nmtllib palette.mtl\n"; }
    fixture.write("input.obj", libraries + "v 0 0 0 1 0 0\nv 1 0 0 1 0 0\nv 0 1 0 1 0 0\n"
                                           "usemtl color\nf 1/99 2/99 3/99\n");
    TriangleMesh mesh;
    ObjInfo      info;
    std::string  message;
    REQUIRE(fixture.load(mesh, info, message));
    CHECK(mesh.facets_count() == 1);
    CHECK(info.missing_mtl);
    CHECK(info.vertex_colors.empty());
    CHECK(info.face_colors.empty());
    CHECK(info.uvs.empty());
    CHECK(info.pngs.empty());
    CHECK(info.uv_map_pngs.empty());
    CHECK_FALSE(info.has_uv_png);
    // Names are not source colors: retain them for explicit --model-filaments.
    REQUIRE(info.material_names.size() == 1);
    CHECK(info.material_names.front() == "color");
    REQUIRE(info.face_material_ids.size() == 1);
    CHECK(info.face_material_ids.front() == 0);

    bool ignored = false;
    REQUIRE_NOTHROW(fixture.read_model(true, &ignored));
    CHECK(ignored);

    // A reused result must not leak fallback state into the next colored import.
    fixture.obj("f 1 2 3\n");
    REQUIRE(fixture.load(mesh, info, message));
    CHECK_FALSE(info.missing_mtl);
    CHECK(info.face_colors.size() == 1);
}

TEST_CASE("Missing OBJ libraries do not hide invalid geometry or malformed existing MTL", "[OBJ][CLI][Import][MissingMTL]")
{
    ObjImportFixture fixture;
    std::string      vertices  = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    std::string      faces     = "f 1 2 3\n";
    std::string      libraries = "mtllib absent.mtl\n";
    SECTION("invalid geometry index") { faces = "f 1 2 99\n"; }
    SECTION("non-finite coordinate") { vertices = "v nan 0 0\nv 1 0 0\nv 0 1 0\n"; }
    SECTION("unsupported polygon") { faces = "f 1 2 3 1 2\n"; }
    SECTION("existing malformed library")
    {
        libraries += "mtllib palette.mtl\n";
        fixture.write("palette.mtl", std::string(70000, 'X'));
    }
    fixture.write("input.obj", libraries + vertices + "usemtl color\n" + faces);
    TriangleMesh mesh;
    ObjInfo      info;
    std::string  message;
    CHECK_FALSE(fixture.load(mesh, info, message));
    CHECK_FALSE(message.empty());
    CHECK_THROWS(fixture.read_model(true));
}

TEST_CASE("Missing OBJ library lookup never falls back to the working directory", "[OBJ][CLI][Import][MissingMTL]")
{
    ObjImportFixture fixture;
    ObjImportFixture unrelated;
    unrelated.write("absent.mtl", "newmtl color\nKd 0 1 0\n");
    fixture.write("input.obj", "mtllib absent.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl color\nf 1 2 3\n");
    ScopedCurrentPath current_path(unrelated.directory);
    TriangleMesh      mesh;
    ObjInfo           info;
    std::string       message;
    REQUIRE(fixture.load(mesh, info, message));
    CHECK(info.missing_mtl);
    CHECK(info.face_colors.empty());
}

TEST_CASE("Missing OBJ MTL works through GUI and CLI imports and CGAL hole repair", "[OBJ][CLI][GUIColor][ModelRepair][CGAL][MissingMTL]")
{
    ObjImportFixture fixture;
    fixture.write("input.obj", "mtllib absent.mtl\nv 0 0 0\nv 10 0 0\nv 0 10 0\nv 0 0 10\n"
                               "usemtl color\nf 1 3 2\nf 1 2 4\nf 1 4 3\n");
    bool             callback_called = false;
    ObjImportColorFn callback    = [&](std::vector<RGBA>&, bool, std::vector<unsigned char>&, unsigned char&) { callback_called = true; };
    bool             gui_ignored = false, cli_ignored = false;
    Model            gui_model = fixture.read_model(false, &gui_ignored, nullptr, callback);
    Model            cli_model = fixture.read_model(true, &cli_ignored);
    CHECK(gui_ignored);
    CHECK(cli_ignored);
    CHECK_FALSE(callback_called);
    // Include the GUI's post-import cleanup, not just the OBJ loader. A zero
    // signed volume used to delete this repairable open tetrahedron here.
    REQUIRE(gui_model.objects.size() == 1);
    CHECK(gui_model.objects.front()->get_object_stl_stats().volume == 0.0);
    CHECK(gui_model.remove_empty_objects() == 0);
    REQUIRE(gui_model.objects.size() == 1);
    CHECK_FALSE(gui_model.looks_like_saved_in_meters());
    CHECK_FALSE(gui_model.looks_like_imperial_units());
    ModelRepairOptions cli_options;
    cli_options.allow_partial = true;
    ModelRepairReport gui_report, cli_report;
    REQUIRE(repair_model(gui_model, gui_report, gui_model_repair_options()));
    REQUIRE(repair_model(cli_model, cli_report, cli_options));
    CHECK(gui_report.mesh_validated());
    CHECK(cli_report.mesh_validated());
    for (const Model* model : {&gui_model, &cli_model}) {
        REQUIRE(model->objects.size() == 1);
        REQUIRE(model->objects.front()->volumes.size() == 1);
        const ModelVolume* volume = model->objects.front()->volumes.front();
        CHECK(volume->mmu_segmentation_facets.empty());
        CHECK(volume->mesh().facets_count() == 4);
        CHECK(its_volume(volume->mesh().its) == Approx(1000. / 6.).margin(0.001));
        CHECK_FALSE(MeshBoolean::cgal::requires_repair(volume->mesh()));
    }
}

TEST_CASE("Missing MTL preserves explicit replacement filament bindings", "[OBJ][CLI][Import][MissingMTL]")
{
    ObjImportFixture fixture;
    fixture.write("input.obj", "mtllib absent.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"
                               "usemtl first\nf 1 3 2\nf 1 2 4\nusemtl second\nf 1 4 3\nf 2 3 4\n");
    const std::vector<unsigned int> slots{1, 2};
    bool                            ignored = false;
    Model                           model   = fixture.read_model(true, &ignored, &slots);
    CHECK(ignored);
    REQUIRE(model.objects.size() == 1);
    REQUIRE(model.objects.front()->volumes.size() == 1);
    CHECK_FALSE(model.objects.front()->volumes.front()->mmu_segmentation_facets.empty());
}

TEST_CASE("Geometry-only OBJ export propagates file-open errors", "[OBJ][CLI]")
{
    TriangleMesh cube = make_cube(10., 10., 10.);

    CHECK_FALSE(store_obj("", &cube));

    const boost::filesystem::path output_path = boost::filesystem::temp_directory_path() /
                                                boost::filesystem::unique_path("orcaslicer-obj-%%%%-%%%%.obj");
    REQUIRE(store_obj(output_path.string().c_str(), &cube));
    CHECK(boost::filesystem::exists(output_path));
    CHECK(boost::filesystem::file_size(output_path) > 0);

    boost::system::error_code error_code;
    boost::filesystem::remove(output_path, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Geometry-only OBJ export rejects empty meshes and output symlinks", "[OBJ][CLI]")
{
    const boost::filesystem::path empty_output = boost::filesystem::temp_directory_path() /
                                                 boost::filesystem::unique_path("orcaslicer-empty-obj-%%%%-%%%%.obj");
    TriangleMesh empty_mesh;
    CHECK_FALSE(store_obj(empty_output.string().c_str(), &empty_mesh));
    CHECK_FALSE(boost::filesystem::exists(empty_output));

#ifndef _WIN32
    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-obj-symlink-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const boost::filesystem::path victim = directory / "victim.txt";
    {
        boost::nowide::ofstream stream(victim.string());
        REQUIRE(stream.is_open());
        stream << "do not replace";
    }
    const boost::filesystem::path output = directory / "output.obj";
    boost::filesystem::create_symlink(victim, output);

    TriangleMesh cube = make_cube(10., 10., 10.);
    CHECK_FALSE(store_obj(output.string().c_str(), &cube));
    CHECK(read_text_file(victim) == "do not replace");

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
#endif
}

TEST_CASE("OBJ exporters reject invalid mesh data before creating output", "[OBJ][CLI][validation]")
{
    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-obj-validation-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));

    TriangleMesh invalid_index                         = make_cube(10., 10., 10.);
    invalid_index.its.indices.front()[0]               = -1;
    const boost::filesystem::path invalid_index_output = directory / "invalid-index.obj";
    CHECK_FALSE(store_obj(invalid_index_output.string().c_str(), &invalid_index));
    CHECK_FALSE(boost::filesystem::exists(invalid_index_output));

    TriangleMesh non_finite                         = make_cube(10., 10., 10.);
    non_finite.its.vertices.front().x()             = std::numeric_limits<float>::quiet_NaN();
    const boost::filesystem::path non_finite_output = directory / "non-finite.obj";
    CHECK_FALSE(store_obj(non_finite_output.string().c_str(), &non_finite));
    CHECK_FALSE(boost::filesystem::exists(non_finite_output));

    const boost::filesystem::path direct_output = directory / "direct-invalid.obj";
    {
        boost::nowide::ofstream stream(direct_output.string());
        REQUIRE(stream.is_open());
        stream << "existing OBJ output\n";
    }
    CHECK_FALSE(invalid_index.write_obj_file(direct_output.string().c_str()));
    CHECK(read_text_file(direct_output) == "existing OBJ output\n");

    PaintedMesh painted_mesh;
    painted_mesh.vertices                        = {{{0.f, 0.f, 0.f}}, {{1.f, 0.f, 0.f}}, {{0.f, 1.f, 0.f}}};
    painted_mesh.indices                         = {{{0, 1, 3}}};
    painted_mesh.face_colors                     = {{{255, 0, 0}}};
    painted_mesh.cluster_colors                  = {{{255, 0, 0}}};
    const boost::filesystem::path painted_output = directory / "invalid-painted.obj";
    const std::vector<RGBA>       colors{{1.f, 0.f, 0.f, 1.f}};
    CHECK_FALSE(store_painted_mesh_obj(painted_output.string().c_str(), painted_mesh, colors));
    CHECK_FALSE(boost::filesystem::exists(painted_output));
    CHECK_FALSE(boost::filesystem::exists(directory / "invalid-painted.mtl"));

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Painted OBJ export rejects an empty output path", "[OBJ][CLI][validation]")
{
    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-painted-empty-path-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));

    PaintedMesh painted_mesh;
    painted_mesh.vertices       = {{{0.f, 0.f, 0.f}}, {{1.f, 0.f, 0.f}}, {{0.f, 1.f, 0.f}}};
    painted_mesh.indices        = {{{0, 1, 2}}};
    painted_mesh.face_colors    = {{{255, 0, 0}}};
    painted_mesh.cluster_colors = {{{255, 0, 0}}};
    const std::vector<RGBA> colors{{1.f, 0.f, 0.f, 1.f}};

    {
        ScopedCurrentPath current_path(directory);
        CHECK_FALSE(store_painted_mesh_obj("", painted_mesh, colors));
        CHECK_FALSE(boost::filesystem::exists(directory / ".obj"));
        CHECK_FALSE(boost::filesystem::exists(directory / ".mtl"));
    }

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Geometry-only OBJ replaces existing output transactionally", "[OBJ][CLI][atomic]")
{
    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-obj-atomic-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const boost::filesystem::path output = directory / "model.obj";
    {
        boost::nowide::ofstream stream(output.string());
        REQUIRE(stream.is_open());
        stream << "previous valid output\n";
    }

    TriangleMesh cube = make_cube(10., 10., 10.);
    REQUIRE(store_obj(output.string().c_str(), &cube));
    CHECK(read_text_file(output).find("previous valid output") == std::string::npos);
    CHECK(count_lines_with_prefix(read_text_file(output), "f ") == cube.facets_count());
    CHECK_FALSE(has_atomic_output_artifacts(directory));

    TriangleMesh      empty_mesh;
    const std::string committed_output = read_text_file(output);
    CHECK_FALSE(store_obj(output.string().c_str(), &empty_mesh));
    CHECK(read_text_file(output) == committed_output);
    CHECK_FALSE(has_atomic_output_artifacts(directory));

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("OBJ material library discovery preserves arbitrary mtllib sidecars", "[OBJ][CLI]")
{
    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-obj-mtllib-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const boost::filesystem::path source_directory  = directory / "source";
    const boost::filesystem::path working_directory = directory / "working";
    REQUIRE(boost::filesystem::create_directory(source_directory));
    REQUIRE(boost::filesystem::create_directory(working_directory));
    const boost::filesystem::path obj_path = source_directory / "source.obj";
    const boost::filesystem::path mtl_path = source_directory / "palette.mtl";
    {
        boost::nowide::ofstream stream(obj_path.string());
        REQUIRE(stream.is_open());
        stream << "mtllib palette.mtl\n"
                  "v 0 0 0\n"
                  "v 1 0 0\n"
                  "v 0 1 0\n"
                  "f 1 2 3\n";
    }
    {
        boost::nowide::ofstream stream(mtl_path.string());
        REQUIRE(stream.is_open());
        stream << "newmtl material\n";
    }
    {
        boost::nowide::ofstream stream((working_directory / "palette.mtl").string());
        REQUIRE(stream.is_open());
        stream << "newmtl unrelated_working_directory_material\n";
    }

    std::vector<std::string> libraries;
    std::string              message;
    {
        ScopedCurrentPath scoped_current_path(working_directory);
        REQUIRE(load_obj_material_libraries(obj_path.string().c_str(), libraries, message));
        REQUIRE(libraries.size() == 1);
        CHECK(boost::filesystem::weakly_canonical(libraries.front()) == boost::filesystem::weakly_canonical(mtl_path));
    }

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("OBJ primary arrange color follows first material used by a face", "[OBJ][CLI][ColorGroup]")
{
    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-obj-primary-color-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const boost::filesystem::path obj_path = directory / "model.obj";
    const boost::filesystem::path mtl_path = directory / "palette.mtl";
    {
        boost::nowide::ofstream stream(obj_path.string());
        REQUIRE(stream.is_open());
        stream << "mtllib palette.mtl\n"
                  "v 0 0 0\n"
                  "v 1 0 0\n"
                  "v 0 1 0\n"
                  "v 2 0 0\n"
                  "v 3 0 0\n"
                  "v 2 1 0\n"
                  "usemtl red\n"
                  "f 1 2 3\n"
                  "usemtl blue\n"
                  "f 4 5 6\n";
    }
    {
        boost::nowide::ofstream stream(mtl_path.string());
        REQUIRE(stream.is_open());
        // MTL declaration order is intentionally the opposite of face-use order.
        stream << "newmtl blue\nKd 0 0 1\n"
                  "newmtl red\nKd 1 0 0\n";
    }

    Model       model;
    ObjInfo     obj_info;
    std::string message;
    REQUIRE(load_obj(obj_path.string().c_str(), &model, obj_info, message));
    CHECK(primary_obj_color_group(obj_info) == "#FF0000");

    ObjInfo no_color;
    CHECK(primary_obj_color_group(no_color).empty());

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Painted OBJ export rejects non-regular OBJ and MTL targets", "[OBJ][CLI]")
{
    PaintedMesh painted_mesh;
    painted_mesh.vertices       = {{{0.f, 0.f, 0.f}}, {{1.f, 0.f, 0.f}}, {{0.f, 1.f, 0.f}}};
    painted_mesh.indices        = {{{0, 1, 2}}};
    painted_mesh.face_colors    = {{{255, 0, 0}}};
    painted_mesh.cluster_colors = {{{255, 0, 0}}};
    const std::vector<RGBA> colors{{1.f, 0.f, 0.f, 1.f}};

    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-painted-target-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const boost::filesystem::path directory_target = directory / "directory.obj";
    REQUIRE(boost::filesystem::create_directory(directory_target));
    CHECK_FALSE(store_painted_mesh_obj(directory_target.string().c_str(), painted_mesh, colors));
    CHECK(boost::filesystem::is_directory(directory_target));

#ifndef _WIN32
    const boost::filesystem::path victim = directory / "victim.txt";
    {
        boost::nowide::ofstream stream(victim.string());
        REQUIRE(stream.is_open());
        stream << "do not replace";
    }

    const boost::filesystem::path obj_link = directory / "linked.obj";
    boost::filesystem::create_symlink(victim, obj_link);
    CHECK_FALSE(store_painted_mesh_obj(obj_link.string().c_str(), painted_mesh, colors));
    CHECK(read_text_file(victim) == "do not replace");

    const boost::filesystem::path mtl_link_obj = directory / "linked_mtl.obj";
    boost::filesystem::path       mtl_link     = mtl_link_obj;
    mtl_link.replace_extension(".mtl");
    boost::filesystem::create_symlink(victim, mtl_link);
    CHECK_FALSE(store_painted_mesh_obj(mtl_link_obj.string().c_str(), painted_mesh, colors));
    CHECK_FALSE(boost::filesystem::exists(mtl_link_obj));
    CHECK(read_text_file(victim) == "do not replace");
#endif

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Painted OBJ and MTL replace an existing pair transactionally", "[OBJ][CLI][atomic]")
{
    PaintedMesh painted_mesh;
    painted_mesh.vertices       = {{{0.f, 0.f, 0.f}}, {{1.f, 0.f, 0.f}}, {{0.f, 1.f, 0.f}}};
    painted_mesh.indices        = {{{0, 1, 2}}};
    painted_mesh.face_colors    = {{{255, 0, 0}}};
    painted_mesh.cluster_colors = {{{255, 0, 0}}};
    const std::vector<RGBA> colors{{1.f, 0.f, 0.f, 1.f}};

    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-painted-atomic-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const boost::filesystem::path obj_path = directory / "painted.obj";
    const boost::filesystem::path mtl_path = directory / "painted.mtl";
    {
        boost::nowide::ofstream obj_stream(obj_path.string());
        boost::nowide::ofstream mtl_stream(mtl_path.string());
        REQUIRE(obj_stream.is_open());
        REQUIRE(mtl_stream.is_open());
        obj_stream << "old obj\n";
        mtl_stream << "old mtl\n";
    }

    REQUIRE(store_painted_mesh_obj(obj_path.string().c_str(), painted_mesh, colors));
    CHECK(read_text_file(obj_path).find("old obj") == std::string::npos);
    CHECK(read_text_file(obj_path).find("mtllib painted.mtl") != std::string::npos);
    CHECK(read_text_file(mtl_path).find("old mtl") == std::string::npos);
    CHECK(read_text_file(mtl_path).find("newmtl filament_1") != std::string::npos);
    CHECK_FALSE(has_atomic_output_artifacts(directory));

    const std::string committed_obj = read_text_file(obj_path);
    const std::string committed_mtl = read_text_file(mtl_path);
    painted_mesh.indices.front()[0] = 99;
    CHECK_FALSE(store_painted_mesh_obj(obj_path.string().c_str(), painted_mesh, colors));
    CHECK(read_text_file(obj_path) == committed_obj);
    CHECK(read_text_file(mtl_path) == committed_mtl);
    CHECK_FALSE(has_atomic_output_artifacts(directory));

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Multicolor OBJ exports Orca filament assignments for every instance", "[OBJ][CLI]")
{
    Model        model;
    ModelObject* object = model.add_object();
    ModelVolume* first  = object->add_volume(make_cube(10., 10., 10.));
    ModelVolume* second = object->add_volume(make_cube(6., 6., 6.));
    first->config.set("extruder", 1);
    second->config.set("extruder", 2);
    object->add_instance()->set_offset(Vec3d::Zero());
    object->add_instance()->set_offset(Vec3d(30., 0., 0.));

    const boost::filesystem::path obj_path = boost::filesystem::temp_directory_path() /
                                             boost::filesystem::unique_path("orcaslicer-multicolor-%%%%-%%%%.obj");
    boost::filesystem::path mtl_path = obj_path;
    mtl_path.replace_extension(".mtl");
    const std::vector<RGBA> colors{{1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}};

    REQUIRE(store_multicolor_obj(obj_path.string().c_str(), model, colors));
    REQUIRE(boost::filesystem::exists(obj_path));
    REQUIRE(boost::filesystem::exists(mtl_path));

    const std::string obj = read_text_file(obj_path);
    const std::string mtl = read_text_file(mtl_path);
    CHECK(obj.find("semantic_source: filament_and_mmu_assignments") != std::string::npos);
    CHECK(obj.find("usemtl filament_1") != std::string::npos);
    CHECK(obj.find("usemtl filament_2") != std::string::npos);
    CHECK(obj.find("vt ") == std::string::npos);
    CHECK(count_lines_with_prefix(obj, "g color_group_") == 4);
    CHECK(count_lines_with_prefix(obj, "f ") == 48);
    CHECK(mtl.find("newmtl filament_1") != std::string::npos);
    CHECK(mtl.find("newmtl filament_2") != std::string::npos);
    CHECK(mtl.find("Kd 1 0 0") != std::string::npos);
    CHECK(mtl.find("Kd 0 1 0") != std::string::npos);

    boost::system::error_code error_code;
    boost::filesystem::remove(obj_path, error_code);
    CHECK_FALSE(error_code);
    boost::filesystem::remove(mtl_path, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Multicolor OBJ validates all filament slots before creating output", "[OBJ][CLI]")
{
    Model        model;
    ModelObject* object = model.add_object();
    ModelVolume* volume = object->add_volume(make_cube(10., 10., 10.));
    volume->config.set("extruder", 3);
    object->add_instance();

    const boost::filesystem::path obj_path = boost::filesystem::temp_directory_path() /
                                             boost::filesystem::unique_path("orcaslicer-invalid-multicolor-%%%%-%%%%.obj");
    boost::filesystem::path mtl_path = obj_path;
    mtl_path.replace_extension(".mtl");
    const std::vector<RGBA> colors{{1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}};

    {
        boost::nowide::ofstream obj_stream(obj_path.string());
        boost::nowide::ofstream mtl_stream(mtl_path.string());
        REQUIRE(obj_stream.is_open());
        REQUIRE(mtl_stream.is_open());
        obj_stream << "existing obj\n";
        mtl_stream << "existing mtl\n";
    }

    CHECK_FALSE(store_multicolor_obj(obj_path.string().c_str(), model, colors));
    CHECK(read_text_file(obj_path) == "existing obj\n");
    CHECK(read_text_file(mtl_path) == "existing mtl\n");

    boost::system::error_code error_code;
    boost::filesystem::remove(obj_path, error_code);
    CHECK_FALSE(error_code);
    boost::filesystem::remove(mtl_path, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Multicolor OBJ rejects non-finite transformed geometry before replacing output", "[OBJ][CLI][validation]")
{
    Model        model;
    ModelObject* object = model.add_object();
    ModelVolume* volume = object->add_volume(make_cube(10., 10., 10.));
    volume->config.set("extruder", 1);
    Transform3d transform = Transform3d::Identity();
    transform(0, 0)       = std::numeric_limits<double>::max();
    volume->set_transformation(transform);
    object->add_instance();

    const boost::filesystem::path directory = boost::filesystem::temp_directory_path() /
                                              boost::filesystem::unique_path("orcaslicer-multicolor-transform-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const boost::filesystem::path obj_path = directory / "model.obj";
    const boost::filesystem::path mtl_path = directory / "model.mtl";
    {
        boost::nowide::ofstream obj_stream(obj_path.string());
        boost::nowide::ofstream mtl_stream(mtl_path.string());
        REQUIRE(obj_stream.is_open());
        REQUIRE(mtl_stream.is_open());
        obj_stream << "existing obj\n";
        mtl_stream << "existing mtl\n";
    }

    const std::vector<RGBA> colors{{1.f, 0.f, 0.f, 1.f}};
    CHECK_FALSE(store_multicolor_obj(obj_path.string().c_str(), model, colors));
    CHECK(read_text_file(obj_path) == "existing obj\n");
    CHECK(read_text_file(mtl_path) == "existing mtl\n");
    CHECK_FALSE(has_atomic_output_artifacts(directory));

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Multicolor OBJ combines base filament and MMU facet assignments", "[OBJ][CLI][MMU]")
{
    Model        model;
    ModelObject* object = model.add_object();
    ModelVolume* volume = object->add_volume(make_cube(10., 10., 10.));
    volume->config.set("extruder", 1);
    object->add_instance();

    TriangleSelector selector(volume->mesh());
    selector.set_facet(0, EnforcerBlockerType::Extruder2);
    REQUIRE(volume->mmu_segmentation_facets.set(selector));

    const boost::filesystem::path obj_path = boost::filesystem::temp_directory_path() /
                                             boost::filesystem::unique_path("orcaslicer-mmu-%%%%-%%%%.obj");
    boost::filesystem::path mtl_path = obj_path;
    mtl_path.replace_extension(".mtl");
    const std::vector<RGBA> colors{{1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}};

    REQUIRE(store_multicolor_obj(obj_path.string().c_str(), model, colors));
    const std::string obj = read_text_file(obj_path);
    CHECK(obj.find("usemtl filament_1") != std::string::npos);
    CHECK(obj.find("usemtl filament_2") != std::string::npos);
    CHECK(count_lines_with_prefix(obj, "f ") == 12);

    boost::system::error_code error_code;
    boost::filesystem::remove(obj_path, error_code);
    CHECK_FALSE(error_code);
    boost::filesystem::remove(mtl_path, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("GUI repair preserves imported OBJ colors through staging and export", "[OBJ][GUIColor][ModelRepair][CGAL][Painting][HoleColor]")
{
    // Regression for 11-colored-missing-top.obj: the GUI reused green in slot
    // 1, added red in slot 2, and represented red as the volume's base color.
    // With keep_painting off, the old GUI repair erased all six green faces.
    const std::vector<RGBA> colors{{0.f, 1.f, 0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}};
    TriangleMesh           input;
    ObjInfo                input_info;
    std::string            message;
    const std::string fixture = std::string(TEST_DATA_DIR) + "/model_repair_acceptance/11-colored-missing-top.obj";
    REQUIRE(load_obj(fixture.c_str(), &input, input_info, message));
    REQUIRE(input.its.indices.size() == 10);
    REQUIRE(input_info.face_colors.size() == 10);

    Model        model;
    ModelObject* object = model.add_object();
    ModelVolume* volume = object->add_volume(std::move(input));
    object->add_instance();
    volume->config.set("extruder", 2);
    TriangleSelector selector(volume->mesh());
    size_t           green_faces = 0;
    for (size_t i = 0; i < input_info.face_colors.size(); ++i) {
        if (input_info.face_colors[i] == colors[0]) {
            selector.set_facet(static_cast<int>(i), EnforcerBlockerType::Extruder1);
            ++green_faces;
        } else {
            REQUIRE(input_info.face_colors[i] == colors[1]);
        }
    }
    REQUIRE(green_faces == 6);
    REQUIRE(volume->mmu_segmentation_facets.set(selector));
    const auto source_mesh     = volume->mesh_ptr();
    const auto source_painting = volume->mmu_segmentation_facets.get_data();

    // Use the exact explicit-GUI policy, not hand-written preservation flags.
    // Stage and move volume structures just as the GUI does before its refresh.
    ModelRepairOptions options = gui_model_repair_options();
    options.targets            = {{0, std::nullopt}};
    REQUIRE(options.allow_partial);
    REQUIRE(options.preserve_annotations);
    REQUIRE_FALSE(options.allow_annotation_clearing);
    auto              staged = make_model_repair_staging_copy(model, options.targets);
    ModelRepairReport report;
    REQUIRE(repair_staged_model(*staged, report, options));
    REQUIRE(report.committed);
    REQUIRE(report.mesh_validated());
    REQUIRE(report.volume_reports.size() == 1);
    CHECK(report.volume_reports.front().annotations_preserved);
    CHECK_FALSE(report.volume_reports.front().annotations_cleared);
    CHECK(volume->mesh_ptr() == source_mesh);
    CHECK(volume->mmu_segmentation_facets.get_data() == source_painting);
    auto prepared = model.prepare_move_object_volume_structures(*staged, report.changed_object_indices);
    model.commit_move_object_volume_structures(std::move(prepared));
    volume = model.objects.front()->volumes.front();
    CHECK(volume->extruder_id() == 2);
    CHECK_FALSE(volume->mmu_segmentation_facets.empty());

    const auto directory = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orcaslicer-gui-repair-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const auto path = directory / "repaired.obj";
    REQUIRE(store_multicolor_obj(path.string().c_str(), model, colors));
    TriangleMesh exported;
    ObjInfo      info;
    REQUIRE(load_obj(path.string().c_str(), &exported, info, message));
    REQUIRE(info.face_colors.size() == exported.its.indices.size());
    CHECK(its_num_open_edges(exported.its) == 0);
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(exported));
    CHECK(its_volume(exported.its) == Approx(1000.).margin(0.001));

    // Compare coverage instead of facet counts, allowing valid retriangulation.
    // The six green source faces cover 300 mm2; the new top uses base red.
    double green_area = 0., red_area = 0., top_area = 0.;
    for (size_t i = 0; i < exported.its.indices.size(); ++i) {
        const auto&  face = exported.its.indices[i];
        const Vec3f& a    = exported.its.vertices[face[0]];
        const Vec3f& b    = exported.its.vertices[face[1]];
        const Vec3f& c    = exported.its.vertices[face[2]];
        const double area = 0.5 * (b - a).cross(c - a).norm();
        if (info.face_colors[i] == colors[0])
            green_area += area;
        else {
            REQUIRE(info.face_colors[i] == colors[1]);
            red_area += area;
        }
        if (std::abs(a.z() - 10.f) < 1e-4f && std::abs(b.z() - 10.f) < 1e-4f && std::abs(c.z() - 10.f) < 1e-4f) {
            CHECK(info.face_colors[i] == colors[1]);
            top_area += area;
        }
    }
    CHECK(green_area == Approx(300.).margin(0.001));
    CHECK(red_area == Approx(300.).margin(0.001));
    CHECK(top_area == Approx(100.).margin(0.001));

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Multicolor OBJ keeps a closed indexed mesh across painting seams", "[OBJ][CLI][MMU][Topology]")
{
    const bool subdivide = GENERATE(false, true);
    const bool mirrored = GENERATE(false, true);
    INFO(subdivide);
    INFO(mirrored);
    Model        model;
    ModelObject* object = model.add_object();
    ModelVolume* volume = object->add_volume(make_cube(10., 10., 10.));
    volume->config.set("extruder", 1);
    Transform3d transform   = Transform3d::Identity();
    transform(0, 0)         = mirrored ? -1. : 1.;
    transform.translation() = Vec3d(30., 20., 10.);
    object->add_instance()->set_transformation(Geometry::Transformation(transform));

    TriangleSelector selector(volume->mesh());
    if (subdivide) {
        // Split one original triangle along one side, with one green child.
        // Its unsplit neighbor now has a T-joint. The strict exporter must
        // subdivide that neighbor too, while retaining its red base color.
        TriangleSelector::TriangleSplittingData data;
        data.triangles_to_split.emplace_back(0, 0);
        data.bitstream = {true, false, false, false, false, false, false, true, false, false, false, false};
        data.update_used_states(0);
        selector.deserialize(data, false);
        REQUIRE(selector.get_facets(EnforcerBlockerType::NONE).indices.size() +
                    selector.get_facets(EnforcerBlockerType::Extruder2).indices.size() ==
                13);
    } else {
        selector.set_facet(0, EnforcerBlockerType::Extruder2);
    }
    REQUIRE(volume->mmu_segmentation_facets.set(selector));
    const auto original_mesh     = volume->mesh_ptr();
    const auto original_painting = volume->mmu_segmentation_facets.get_data();

    const auto directory = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("orcaslicer-color-seam-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const auto              path = directory / "model.obj";
    const std::vector<RGBA> colors{{1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}};
    REQUIRE(store_multicolor_obj(path.string().c_str(), model, colors));
    TriangleMesh exported;
    ObjInfo      info;
    std::string  message;
    // Low-level loading does not run repair or merge vertices by position.
    REQUIRE(load_obj(path.string().c_str(), &exported, info, message));
    CHECK(exported.its.vertices.size() == (subdivide ? 9 : 8));
    CHECK(exported.its.indices.size() == (subdivide ? 14 : 12));
    CHECK(its_num_open_edges(exported.its) == 0);
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(exported));
    CHECK(its_volume(exported.its) == Approx(1000.).margin(0.001));
    CHECK((exported.bounding_box().center() - Vec3d(mirrored ? 25. : 35., 25., 15.)).norm() < 0.001);

    REQUIRE(info.face_colors.size() == exported.its.indices.size());
    double red_area = 0., green_area = 0.;
    for (size_t i = 0; i < exported.its.indices.size(); ++i) {
        const auto&  face     = exported.its.indices[i];
        const auto&  vertices = exported.its.vertices;
        const double area     = 0.5 * (vertices[face[1]] - vertices[face[0]]).cross(vertices[face[2]] - vertices[face[0]]).norm();
        if (info.face_colors[i] == colors[0])
            red_area += area;
        else {
            REQUIRE(info.face_colors[i] == colors[1]);
            green_area += area;
        }
    }
    CHECK(green_area == Approx(subdivide ? 25. : 50.).margin(0.001));
    CHECK(red_area + green_area == Approx(600.).margin(0.001));
    CHECK(volume->mesh_ptr() == original_mesh);
    CHECK(volume->mmu_segmentation_facets.get_data() == original_painting);

    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Multicolor OBJ does not weld independent geometry at coincident positions", "[OBJ][CLI][MMU][Topology]")
{
    const int arrangement = GENERATE(0, 1, 2, 3);
    INFO(arrangement); // same volume, different volumes, different objects, instances
    Model        model;
    ModelObject* object = model.add_object();
    TriangleMesh mesh   = make_cube(10., 10., 10.);
    if (arrangement == 0) {
        TriangleMesh second = mesh;
        second.translate(10.f, 0.f, 0.f); // two shells touching along a face
        mesh.merge(second);
    }
    ModelVolume*     volume = object->add_volume(std::move(mesh));
    TriangleSelector selector(volume->mesh());
    selector.set_facet(0, EnforcerBlockerType::Extruder2);
    REQUIRE(volume->mmu_segmentation_facets.set(selector));
    object->add_instance();
    if (arrangement == 1)
        object->add_volume(make_cube(10., 10., 10.));
    else if (arrangement == 2) {
        ModelObject* second = model.add_object();
        second->add_volume(make_cube(10., 10., 10.));
        second->add_instance();
    } else if (arrangement == 3)
        object->add_instance(); // coincident copies must still have different indices

    const auto directory = boost::filesystem::temp_directory_path() /
                           boost::filesystem::unique_path("orcaslicer-independent-colors-%%%%-%%%%");
    REQUIRE(boost::filesystem::create_directory(directory));
    const auto              path = directory / "model.obj";
    const std::vector<RGBA> colors{{1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}};
    REQUIRE(store_multicolor_obj(path.string().c_str(), model, colors));
    TriangleMesh exported;
    ObjInfo      info;
    std::string  message;
    REQUIRE(load_obj(path.string().c_str(), &exported, info, message));
    CHECK(exported.its.vertices.size() == 16);
    CHECK(exported.its.indices.size() == 24);
    CHECK(its_num_open_edges(exported.its) == 0);
    const auto shells = its_split(exported.its);
    REQUIRE(shells.size() == 2);
    for (const auto& shell : shells)
        CHECK(its_volume(shell) == Approx(1000.).margin(0.001));
    // These deliberately touching/overlapping objects are not a claim of
    // globally intersection-free geometry. The exporter must not merge them.
    boost::system::error_code error_code;
    boost::filesystem::remove_all(directory, error_code);
    CHECK_FALSE(error_code);
}

TEST_CASE("Multicolor OBJ refuses to replace directories", "[OBJ][CLI]")
{
    Model        model;
    ModelObject* object = model.add_object();
    object->add_volume(make_cube(10., 10., 10.));
    object->add_instance();

    const boost::filesystem::path obj_path = boost::filesystem::temp_directory_path() /
                                             boost::filesystem::unique_path("orcaslicer-multicolor-directory-%%%%-%%%%.obj");
    REQUIRE(boost::filesystem::create_directory(obj_path));

    const std::vector<RGBA> colors{{1.f, 0.f, 0.f, 1.f}};
    CHECK_FALSE(store_multicolor_obj(obj_path.string().c_str(), model, colors));
    CHECK(boost::filesystem::is_directory(obj_path));

    boost::system::error_code error_code;
    boost::filesystem::remove(obj_path, error_code);
    CHECK_FALSE(error_code);
}

namespace {

// Compare absolute face coordinates and winding, allowing only float IO/transform
// rounding. No recentering, best-fit rotation or coordinate welding in this check.
void check_export_faces(const TriangleMesh& actual, const TriangleMesh& expected)
{
    REQUIRE(actual.its.indices.size() == expected.its.indices.size());
    std::vector<bool> matched(expected.its.indices.size(), false);
    for (const auto& face : actual.its.indices) {
        bool found = false;
        for (size_t i = 0; i < expected.its.indices.size() && !found; ++i) {
            if (matched[i])
                continue;
            const auto& reference = expected.its.indices[i];
            for (int start = 0; start < 3 && !found; ++start) {
                bool same = true;
                for (int corner = 0; corner < 3; ++corner)
                    same &=
                        (actual.its.vertices[face[corner]] - expected.its.vertices[reference[(corner + start) % 3]]).cwiseAbs().maxCoeff() <
                        0.0001f;
                if (same)
                    found = matched[i] = true;
            }
        }
        REQUIRE(found);
    }
}

enum class ExportSelection { SingleObject, CombinedObjects, AllInstances, OneInstance };

} // namespace

TEST_CASE("GUI model export retains legacy STL origin rules", "[OBJ][Export][Coordinates]")
{
    Model        model;
    ModelObject* object        = model.add_object();
    object->origin_translation = Vec3d(-15., -5., -4.);
    object->add_instance();
    CHECK(gui_model_export_offset(*object, GuiModelExportMode::SingleObject).isApprox(Vec3d(15., 5., 4.)));
    CHECK(gui_model_export_offset(*object, GuiModelExportMode::CombinedObjects).isZero());
    CHECK(gui_model_export_offset(*object, GuiModelExportMode::SeparateObjects).isApprox(Vec3d(15., 5., 4.)));
    object->add_instance();
    CHECK(gui_model_export_offset(*object, GuiModelExportMode::SingleObject).isZero());
    CHECK(gui_model_export_offset(*object, GuiModelExportMode::CombinedObjects).isZero());
    CHECK(gui_model_export_offset(*object, GuiModelExportMode::SeparateObjects).isApprox(Vec3d(15., 5., 4.)));
}

TEST_CASE("GUI OBJ snapshots match STL absolute coordinates without changing the scene", "[OBJ][Export][Coordinates][GUIColor]")
{
    const auto   selection = GENERATE(ExportSelection::SingleObject, ExportSelection::CombinedObjects, ExportSelection::AllInstances,
                                      ExportSelection::OneInstance);
    const bool   colored   = GENERATE(false, true);
    const double rotation  = GENERATE(0., 0.5 * PI, 0.37);
    CAPTURE(static_cast<int>(selection), colored, rotation);
    ObjImportFixture files;
    Model            model;
    ModelObject*     first = model.add_object();
    ModelVolume*     body  = first->add_volume(make_cube(30., 10., 12.));
    body->set_offset(Vec3d(0., 0., -2.));
    body->config.set("extruder", 1);
    ModelVolume* part = first->add_volume(make_cube(5., 4., 3.));
    part->set_offset(Vec3d(40., 2., 5.));
    part->set_rotation(Vec3d(0., 0., 0.23));
    part->config.set("extruder", 2);
    if (colored) {
        TriangleSelector selector(body->mesh());
        selector.set_facet(0, EnforcerBlockerType::Extruder2);
        REQUIRE(body->mmu_segmentation_facets.set(selector));
    }
    first->center_around_origin();
    auto* instance = first->add_instance();
    instance->set_rotation(Vec3d(rotation, 0., 0.));
    instance->set_scaling_factor(Vec3d(1.5, 0.75, 2.));
    instance->set_offset(Vec3d(110., 95., 22.));
    if (selection == ExportSelection::AllInstances || selection == ExportSelection::OneInstance) {
        auto* other = first->add_instance(*instance);
        other->set_offset(Vec3d(180., 145., 32.));
    }
    if (selection == ExportSelection::CombinedObjects) {
        auto* other  = model.add_object();
        auto* volume = other->add_volume(make_cube(7., 8., 9.));
        volume->set_offset(Vec3d(-35., -20., 6.));
        volume->config.set("extruder", 1);
        other->center_around_origin();
        other->add_instance()->set_offset(Vec3d(40., 30., 11.));
    }
    const Model        original(model);
    const TriangleMesh original_scene = model.mesh();

    // Independent oracle: legacy GUI STL applies volume + instance transforms,
    // then subtracts origin_translation only for a single-object/single-instance export.
    TriangleMesh expected;
    for (const ModelObject* object : model.objects) {
        for (size_t i = 0; i < object->instances.size(); ++i) {
            if (selection == ExportSelection::OneInstance && i != 1)
                continue;
            for (const ModelVolume* volume : object->volumes) {
                TriangleMesh mesh(volume->mesh());
                mesh.transform(volume->get_matrix(), true);
                mesh.transform(object->instances[i]->get_matrix(), true);
                if (selection == ExportSelection::SingleObject)
                    mesh.translate(-object->origin_translation.cast<float>());
                expected.merge(mesh);
            }
        }
    }
    const auto stl_path = files.directory / "reference.stl";
    REQUIRE(store_stl(stl_path.string().c_str(), &expected, true));
    TriangleMesh stl;
    REQUIRE(stl.ReadSTLFile(stl_path.string().c_str(), false));
    check_export_faces(stl, expected);

    const auto mode = selection == ExportSelection::SingleObject || selection == ExportSelection::AllInstances ?
                          GuiModelExportMode::SingleObject :
                          GuiModelExportMode::CombinedObjects;
    for (int repeat = 0; repeat < 2; ++repeat) {
        CAPTURE(repeat);
        Model snapshot;
        for (const ModelObject* source : model.objects) {
            auto* object = snapshot.add_object(*source);
            object->translate_instances(gui_model_export_offset(*source, mode));
            if (selection == ExportSelection::OneInstance)
                object->delete_instance(0);
        }
        const auto path = files.directory / ("export-" + std::to_string(repeat) + ".obj");
        if (colored) {
            REQUIRE(store_multicolor_obj(path.string().c_str(), snapshot, {{1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}}));
            const auto contents = read_text_file(path);
            CHECK(contents.find("usemtl filament_1") != std::string::npos);
            CHECK(contents.find("usemtl filament_2") != std::string::npos);
        } else {
            REQUIRE(store_obj(path.string().c_str(), &snapshot));
        }
        TriangleMesh obj;
        ObjInfo      info;
        std::string  message;
        REQUIRE(load_obj(path.string().c_str(), &obj, info, message, !colored));
        if (colored)
            REQUIRE(info.face_colors.size() == obj.its.indices.size());
        check_export_faces(obj, stl);
    }
    check_export_faces(model.mesh(), original_scene);
    for (size_t i = 0; i < model.objects.size(); ++i) {
        const auto* object = model.objects[i];
        const auto* before = original.objects[i];
        CHECK(object->origin_translation.isApprox(before->origin_translation));
        REQUIRE(object->instances.size() == before->instances.size());
        for (size_t j = 0; j < object->instances.size(); ++j)
            CHECK(object->instances[j]->get_matrix().matrix().isApprox(before->instances[j]->get_matrix().matrix()));
        for (size_t j = 0; j < object->volumes.size(); ++j) {
            CHECK(object->volumes[j]->get_matrix().matrix().isApprox(before->volumes[j]->get_matrix().matrix()));
            CHECK(object->volumes[j]->mmu_segmentation_facets.get_data() == before->volumes[j]->mmu_segmentation_facets.get_data());
        }
    }
    // The GUI policy is opt-in: direct/CLI OBJ export still uses scene coordinates.
    const auto cli_path = files.directory / "direct.obj";
    REQUIRE(store_obj(cli_path.string().c_str(), &model));
    TriangleMesh direct;
    ObjInfo      info;
    std::string  message;
    REQUIRE(load_obj(cli_path.string().c_str(), &direct, info, message, true));
    check_export_faces(direct, original_scene);
}
