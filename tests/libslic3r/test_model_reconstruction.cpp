#include <catch2/catch.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/ModelInspection.hpp>
#include <libslic3r/ModelReconstruction.hpp>
#include <libslic3r/InProcessReconstructionControl.hpp>
#include <libslic3r/ModelRepairEvidence.hpp>
#include <libslic3r/ModelStageReport.hpp>
#include <libslic3r/Format/STL.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <meshlib/MeshLibAdapter.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

using namespace Slic3r;

namespace {
struct ReconstructionTempDirectory
{
    boost::filesystem::path path = boost::filesystem::temp_directory_path() /
                                   boost::filesystem::unique_path("orca-stage-stl-%%%%-%%%%-%%%%");
    ReconstructionTempDirectory() { boost::filesystem::create_directory(path); }
    ~ReconstructionTempDirectory()
    {
        boost::system::error_code ignored;
        boost::filesystem::remove_all(path, ignored);
    }
};

bool same_ordered_triangles(const TriangleMesh& first, const TriangleMesh& second)
{
    if (first.its.indices.size() != second.its.indices.size())
        return false;
    for (size_t face = 0; face < first.its.indices.size(); ++face)
        for (int corner = 0; corner < 3; ++corner)
            if (first.its.vertices[first.its.indices[face][corner]] != second.its.vertices[second.its.indices[face][corner]])
                return false;
    return true;
}

bool inside_closed_mesh(const TriangleMesh& mesh, const Vec3d& point)
{
    double angle = 0.;
    for (const auto& face : mesh.its.indices) {
        const Vec3d a = mesh.its.vertices[face[0]].cast<double>() - point;
        const Vec3d b = mesh.its.vertices[face[1]].cast<double>() - point;
        const Vec3d c = mesh.its.vertices[face[2]].cast<double>() - point;
        angle += 2. * std::atan2(a.dot(b.cross(c)),
                                 a.norm() * b.norm() * c.norm() + a.dot(b) * c.norm() + b.dot(c) * a.norm() + c.dot(a) * b.norm());
    }
    return std::abs(angle) > 2. * PI;
}

Model reconstruction_model()
{
    Model model;
    auto* object = model.add_object();
    object->name = "reconstruction source";
    object->add_volume(make_cube(10., 10., 10.));
    object->add_instance();
    return model;
}
} // namespace

TEST_CASE("Binary STL export preserves every validated reconstruction triangle", "[ModelReconstruction]")
{
    ReconstructionTempDirectory temp;
    auto candidate = make_cube(7., 11., 13.);
    candidate.rotate_z(0.37);
    candidate.translate(2.f, -3.f, 1.f);
    nlohmann::json validation;
    REQUIRE(validate_reconstruction_mesh(candidate, validation, RECONSTRUCTION_MAX_OUTPUT_FACES));
    const auto path = (temp.path / "candidate.stl").string();
    REQUIRE(store_stl(path.c_str(), &candidate, true));
    std::string hash;
    CHECK(same_ordered_triangles(candidate, read_bounded_model_stl(path, hash)));
}

TEST_CASE("Stage readback preserves exact triangles and permits face reordering", "[ModelStageReport][AppBasic]")
{
    ReconstructionTempDirectory temp;
    const auto                  input    = (temp.path / "input.stl").string();
    const auto                  artifact = (temp.path / "artifact.stl").string();
    auto                        source   = make_cube(10., 10., 10.);
    REQUIRE(store_stl(input.c_str(), &source, true));
    const auto context   = prepare_model_stage_report(input, input, temp.path.string());
    auto       candidate = source;
    const int  variation = GENERATE(0, 1, 2, 3, 4, 5);
    switch (variation) {
    case 1: std::reverse(candidate.its.indices.begin(), candidate.its.indices.end()); break;
    case 2: candidate.its.vertices[0].x() += 0.001f; break;
    case 3: std::swap(candidate.its.indices[0][0], candidate.its.indices[0][1]); break;
    case 4: candidate.its.indices.pop_back(); break;
    case 5: candidate.its.indices[0] = candidate.its.indices[1]; break;
    }
    REQUIRE(store_stl(artifact.c_str(), &candidate, true));
    ModelInspectionOptions options;
    options.analyze_components = false;
    const auto inspection      = inspect_model_mesh(source, options);
    const auto write           = [&] {
        write_model_stage_report(context, temp.path.string(), artifact, source, inspection, "check", "not_requested", {});
    };
    if (variation < 2) {
        REQUIRE_NOTHROW(write());
        CHECK(boost::filesystem::exists(temp.path / "model-stage.json"));
    } else {
        CHECK_THROWS_WITH(write(), "Stage artifact does not match the inspected triangles.");
        CHECK_FALSE(boost::filesystem::exists(temp.path / "model-stage.json"));
    }
}

TEST_CASE("Only App check and advanced skip independent geometry comparison", "[ModelStageReport][AppBasic]")
{
    ReconstructionTempDirectory temp;
    const auto                  input     = (temp.path / "input.stl").string();
    const auto                  artifact  = (temp.path / "artifact.stl").string();
    auto                        source    = make_cube(10., 10., 10.);
    auto                        candidate = source;
    candidate.translate(1., 0., 0.);
    REQUIRE(store_stl(input.c_str(), &source, true));
    REQUIRE(store_stl(artifact.c_str(), &candidate, true));
    ModelInspectionOptions options;
    options.analyze_components = GENERATE(false, true);
    const std::string stage    = GENERATE("check", "advanced", "reconstruction");
    const auto        context  = prepare_model_stage_report(input, input, temp.path.string());
    write_model_stage_report(context, temp.path.string(), artifact, candidate, inspect_model_mesh(candidate, options), stage,
                             "not_requested", {});
    boost::nowide::ifstream stream((temp.path / "model-stage.json").string());
    nlohmann::json          report;
    stream >> report;
    const auto& change = report.at("change");
    if (!options.analyze_components && stage != "reconstruction") {
        CHECK((change == nlohmann::json{{"status", "not_requested"}, {"reason", "app_basic"}}));
        CHECK(report.at("inspection").at("scope") == "basic");
    } else {
        CHECK(change.at("status") == "completed");
        CHECK(change.at("bbox_deviation_mm") == 1.);
        CHECK(change.at("sampled_source_to_candidate_mm").get<double>() > 0.25);
    }
    CHECK(report.at("artifact").at("readback_verified") == true);
}

TEST_CASE("Stage component indexing keeps exact edge connectivity", "[ModelStageReport]")
{
    ReconstructionTempDirectory temp;
    indexed_triangle_set        its;
    its.vertices = {{0.f, 0.f, 0.f}, {2.f, 0.f, 0.f},  {0.f, 2.f, 0.f},   {-0.f, 0.f, -0.f}, {-0.f, 2.f, 0.f}, {-2.f, 0.f, 0.f},
                    {0.f, 0.f, 0.f}, {0.f, -2.f, 0.f}, {-2.f, -2.f, 0.f}, {4.f, 0.f, 0.f},   {6.f, 0.f, 0.f},  {4.f, 2.f, 0.f}};
    // First two share an exact edge across signed zero; the third only touches
    // a vertex. Duplicate/degenerate records sharing an edge add no component.
    its.indices = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {9, 10, 11}, {2, 1, 0}, {9, 10, 10}};
    TriangleMesh       source(std::move(its));
    const auto         input    = (temp.path / "input.stl").string();
    const auto         artifact = (temp.path / "artifact.stl").string();
    REQUIRE(store_stl(input.c_str(), &source, true));
    REQUIRE(store_stl(artifact.c_str(), &source, true));
    ModelInspectionOptions options;
    const auto context         = prepare_model_stage_report(input, input, temp.path.string());
    write_model_stage_report(context, temp.path.string(), artifact, source, inspect_model_mesh(source, options), "check", "not_requested",
                             {});
    boost::nowide::ifstream stream((temp.path / "model-stage.json").string());
    nlohmann::json          report;
    stream >> report;
    CHECK(report.at("change").at("reference_components") == 3);
    CHECK(report.at("change").at("candidate_components") == 3);
    CHECK(report.at("change").at("components_complete") == true);
}

TEST_CASE("Local hole evidence preserves original triangles and bounds each cap", "[ModelStageReport][HoleFill]")
{
    auto       complete   = make_cube(10., 10., 10.);
    auto       open       = complete;
    const auto horizontal = [](const TriangleMesh& mesh, const auto& face, float z) {
        return mesh.its.vertices[face[0]].z() == z && mesh.its.vertices[face[1]].z() == z && mesh.its.vertices[face[2]].z() == z;
    };
    auto& faces = open.its.indices;
    faces.erase(std::remove_if(faces.begin(), faces.end(), [&](const auto& face) { return horizontal(open, face, 10.f); }), faces.end());
    REQUIRE(faces.size() == 10);
    SECTION("One open face is capped without moving the original surface")
    {
        const auto proof = inspect_model_hole_fill(open, complete);
        CHECK(proof.at("status") == "verified");
        CHECK(proof.at("source_faces") == 10);
        CHECK(proof.at("preserved_faces") == 10);
        CHECK(proof.at("added_faces") == 2);
        CHECK(proof.at("patches") == 1);
        CHECK(proof.at("boundary_edges") == 4);
    }
    SECTION("Multiple separate holes remain separate disks")
    {
        faces.erase(std::remove_if(faces.begin(), faces.end(), [&](const auto& face) { return horizontal(open, face, 0.f); }), faces.end());
        const auto proof = inspect_model_hole_fill(open, complete);
        CHECK(proof.at("status") == "verified");
        CHECK(proof.at("patches") == 2);
    }
    SECTION("No-op has no added-surface exemption") { CHECK(inspect_model_hole_fill(complete, complete).at("status") == "unverified"); }
    SECTION("Triangle ordering and winding are separate from geometric preservation")
    {
        std::reverse(complete.its.indices.begin(), complete.its.indices.end());
        for (auto& face : complete.its.indices)
            std::swap(face[0], face[1]);
        CHECK(inspect_model_hole_fill(open, complete).at("status") == "verified");
    }
    SECTION("A changed original vertex is not a hole repair")
    {
        complete.its.vertices[0].x() += 0.01f;
        CHECK(inspect_model_hole_fill(open, complete).at("reason") == "original_surface_changed");
    }
    SECTION("A deleted independent component is not a hole repair")
    {
        auto extra = make_cube(1., 1., 1.);
        extra.translate(20., 0., 0.);
        open.merge(extra);
        CHECK(inspect_model_hole_fill(open, complete).at("status") == "unverified");
    }
    SECTION("An additional closed solid is not a hole patch")
    {
        auto extra = make_cube(1., 1., 1.);
        extra.translate(20., 0., 0.);
        complete.merge(extra);
        CHECK(inspect_model_hole_fill(open, complete).at("reason") == "not_disk_patch");
    }
    SECTION("A spike attached to a valid boundary exceeds its region")
    {
        complete         = open;
        const int center = int(complete.its.vertices.size());
        complete.its.vertices.emplace_back(5.f, 5.f, 12.f);
        std::vector<int> rim;
        for (const auto& point : std::vector<Vec3f>{{0.f, 0.f, 10.f}, {10.f, 0.f, 10.f}, {10.f, 10.f, 10.f}, {0.f, 10.f, 10.f}}) {
            const auto found = std::find(complete.its.vertices.begin(), complete.its.vertices.end(), point);
            REQUIRE(found != complete.its.vertices.end());
            rim.push_back(int(found - complete.its.vertices.begin()));
        }
        for (int i = 0; i < 4; ++i)
            complete.its.indices.emplace_back(rim[i], rim[(i + 1) % 4], center);
        CHECK(inspect_model_hole_fill(open, complete).at("reason") == "patch_outside_boundary_bounds");
    }
    SECTION("Joining separate boundaries with a tube is not capping a disk")
    {
        faces.erase(std::remove_if(faces.begin(), faces.end(), [&](const auto& face) { return !horizontal(open, face, 0.f); }),
                    faces.end());
        for (const auto& face : complete.its.indices)
            if (horizontal(complete, face, 10.f))
                faces.push_back(face);
        CHECK(inspect_model_hole_fill(open, complete).at("reason") == "not_disk_patch");
    }
    SECTION("Duplicate input faces are not certified")
    {
        faces.push_back(faces.front());
        CHECK(inspect_model_hole_fill(open, complete).at("reason") == "duplicate_source_face");
    }
    SECTION("Added faces on interior edges are not source boundary caps")
    {
        complete = open;
        const int tip = int(complete.its.vertices.size());
        complete.its.vertices.emplace_back(5.f, 5.f, 5.f);
        complete.its.indices.emplace_back(faces.front()[0], faces.front()[1], tip);
        CHECK(inspect_model_hole_fill(open, complete).at("reason") == "not_source_boundary");
    }
    SECTION("Patch budget stops proof without changing the meshes")
    {
        complete.its.indices.resize(open.its.indices.size() + 100001, complete.its.indices.front());
        CHECK(inspect_model_hole_fill(open, complete).at("reason") == "patch_limit");
        CHECK(open.its.indices.size() == 10);
    }
}

TEST_CASE("Stage STL loading shares only exact vertices and preserves file identity", "[ModelReconstruction][ModelStageReport]")
{
    ReconstructionTempDirectory temp;
    const bool                  binary = GENERATE(true, false);
    const auto                  input  = (temp.path / "coordinates.stl").string();
    indexed_triangle_set        its;
    const float                 near_x = std::nextafter(5.3f, std::numeric_limits<float>::infinity());
    its.vertices = {{-1.1f, 2.3f, 0.f}, {5.3f, 2.3f, 0.f}, {5.3f, 4.7f, 1.f}, {near_x, 2.3f, 0.f}, {-1.1f, 2.3f, -0.f}};
    // Include reversed, duplicate and degenerate records: sharing is not repair.
    its.indices = {{0, 1, 2}, {2, 1, 0}, {4, 1, 2}, {0, 0, 1}, {0, 3, 2}};
    TriangleMesh source(std::move(its));
    REQUIRE(store_stl(input.c_str(), &source, binary));
    const auto  original_sha256 = model_mesh_file_sha256(input);
    std::string parsed_sha256;
    const auto  loaded = read_bounded_model_stl(input, parsed_sha256);
    CHECK(parsed_sha256 == original_sha256);
    CHECK(model_mesh_file_sha256(input) == original_sha256);
    REQUIRE(loaded.its.vertices.size() == 4);
    REQUIRE(loaded.its.indices.size() == source.its.indices.size());
    CHECK(same_ordered_triangles(source, loaded));
    CHECK(loaded.its.indices[0][1] != loaded.its.indices[4][1]);
    CHECK(loaded.stats().number_of_facets == loaded.its.indices.size());
}

TEST_CASE("Stage ASCII STL ignores non-finite normals but requires finite vertices", "[ModelStageReport]")
{
    ReconstructionTempDirectory temp;
    const auto                  input = (temp.path / "normal.stl").string();
    const std::string           token = GENERATE("NaN", "-nan", "+INF", "-Infinity", "0");
    const auto write_triangle = [&](const std::string& normal, const std::string& x) {
        boost::nowide::ofstream out(input);
        out << "solid normal\nfacet normal " << normal << " 0 0\nouter loop\nvertex " << x
            << " 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid normal\n";
    };
    write_triangle(token, "0");
    const auto  original_sha256 = model_mesh_file_sha256(input);
    std::string parsed_sha256;
    const auto  loaded = read_bounded_model_stl(input, parsed_sha256);
    REQUIRE(loaded.its.indices.size() == 1);
    CHECK(loaded.its.vertices[loaded.its.indices[0][1]] == Vec3f(1.f, 0.f, 0.f));
    CHECK(parsed_sha256 == original_sha256);
    CHECK(model_mesh_file_sha256(input) == original_sha256);
    if (token != "0") {
        write_triangle("0", token);
        CHECK_THROWS_AS(read_bounded_model_stl(input, parsed_sha256), std::invalid_argument);
    }
    write_triangle("missing", "0");
    CHECK_THROWS_AS(read_bounded_model_stl(input, parsed_sha256), std::invalid_argument);
    write_triangle("", "0");
    CHECK_THROWS_AS(read_bounded_model_stl(input, parsed_sha256), std::invalid_argument);
}

TEST_CASE("Stage comparison distinguishes zero-area cleanup from lost valid surfaces", "[ModelStageReport]")
{
    auto      candidate = make_cube(10., 10., 10.);
    auto      source    = candidate;
    const int far       = int(source.its.vertices.size());
    source.its.vertices.insert(source.its.vertices.end(), {{20.f, 0.f, 0.f}, {30.f, 0.f, 0.f}, {40.f, 0.f, 0.f}});
    source.its.indices.emplace_back(far, far + 1, far + 2);
    SECTION("Exact zero-area tail removal preserves every real face")
    {
        const auto change = compare_model_stage_meshes(source, candidate);
        CHECK(change.at("reference_excluded_degenerate_faces") == 1);
        CHECK(change.at("degenerate_cleanup_preserves_surface") == true);
    }
    SECTION("Ordinary healthy models do not need an additional surface comparison")
    {
        CHECK_FALSE(compare_model_stage_meshes(candidate, candidate).contains("degenerate_cleanup_preserves_surface"));
    }
    SECTION("A nearly collinear triangle still has area and cannot be excluded")
    {
        source.its.vertices[far + 2].y() = 0.000001f;
        const auto change                = compare_model_stage_meshes(source, candidate);
        CHECK(change.at("reference_excluded_degenerate_faces") == 0);
        CHECK_FALSE(change.contains("degenerate_cleanup_preserves_surface"));
    }
    SECTION("A changed original face prevents certification")
    {
        candidate.its.vertices[0].x() += 0.0001f;
        CHECK(compare_model_stage_meshes(source, candidate).at("degenerate_cleanup_preserves_surface") == false);
    }
    SECTION("A deleted thin solid remains a real surface even with a zero-area tail")
    {
        auto thin = make_cube(10., 10., 0.001);
        thin.translate(50., 0., 0.);
        source.merge(thin);
        CHECK(compare_model_stage_meshes(source, candidate).at("degenerate_cleanup_preserves_surface") == false);
    }
    SECTION("A deleted nonzero isolated sheet is not zero-area cleanup")
    {
        const int point = int(source.its.vertices.size());
        source.its.vertices.insert(source.its.vertices.end(), {{2.f, 2.f, 5.f}, {4.f, 2.f, 5.f}, {2.f, 4.f, 5.f}});
        source.its.indices.emplace_back(point, point + 1, point + 2);
        CHECK(compare_model_stage_meshes(source, candidate).at("degenerate_cleanup_preserves_surface") == false);
    }
    SECTION("An unclean candidate never receives the cleanup proof")
    {
        candidate = source;
        CHECK(compare_model_stage_meshes(source, candidate).at("degenerate_cleanup_preserves_surface") == false);
    }
}

TEST_CASE("Stage union comparison distinguishes internal interfaces from lost material", "[ModelStageReport][MaterialUnion]")
{
    auto source = make_cube(10., 10., 10.);
    auto second = source;
    second.translate(5., 0., 0.);
    source.merge(second);
    auto candidate = make_cube(15., 10., 10.);
    SECTION("Intersecting closed solids may lose only their internal interfaces")
    {
        CHECK_FALSE(compare_model_stage_meshes(source, candidate).contains("material_union"));
        const auto change = compare_model_stage_meshes(source, candidate, true);
        CHECK(change.at("sampled_source_to_candidate_mm").get<double>() > 0.25);
        const auto& proof = change.at("material_union");
        REQUIRE(proof.at("status") == "completed");
        CHECK(proof.at("source_components") == 2);
        CHECK(proof.at("expected_components") == 1);
        CHECK(proof.at("interior_samples").get<int>() > 0);
        CHECK(proof.at("sampled_exterior_to_candidate_mm").get<double>() == Approx(0));
    }
    SECTION("Numeric planar seams are distinct from real gaps in intersecting parts")
    {
        float displacement = 0;
        SECTION("Float coordinate noise") { displacement = 0.000002f; }
        SECTION("A real narrow gap") { displacement = 0.001f; }
        source = make_cube(10., 10., 10.);
        second = source;
        second.translate(10., 0., 0.);
        for (auto& vertex : second.its.vertices)
            if (vertex.x() == 10.f)
                vertex.x() += vertex.y() == 0.f ? -displacement : displacement;
        source.merge(second);
        candidate = make_cube(20., 10., 10.);
        const auto proof = compare_model_stage_meshes(source, candidate, true).at("material_union");
        REQUIRE(proof.at("status") == "completed");
        if (displacement < 1e-5f)
            CHECK(proof.at("sampled_exterior_to_candidate_mm").get<double>() < 1e-5);
        else
            CHECK(proof.at("sampled_exterior_to_candidate_mm").get<double>() > 0.25);
    }
    SECTION("An enlarged candidate cannot certify that original outer surfaces were internal")
    {
        candidate = make_cube(15., 14., 14.);
        candidate.translate(0., -2., -2.);
        const auto proof = compare_model_stage_meshes(source, candidate, true).at("material_union");
        REQUIRE(proof.at("status") == "completed");
        CHECK(proof.at("sampled_exterior_to_candidate_mm").get<double>() > 1.9);
    }
    SECTION("A deleted thin independent part must remain in the exterior and component count")
    {
        auto thin = make_cube(1., 1., 0.001);
        thin.translate(20., 0., 0.);
        source.merge(thin);
        const auto proof = compare_model_stage_meshes(source, candidate, true).at("material_union");
        REQUIRE(proof.at("status") == "completed");
        CHECK(proof.at("expected_components") == 2);
        CHECK(proof.at("sampled_exterior_to_candidate_mm").get<double>() > 5.);
    }
    SECTION("Nested shells and cavity walls cannot be treated as overlapping material")
    {
        auto cavity = make_cube(2., 2., 2.);
        cavity.translate(2., 2., 2.);
        SECTION("Cavity") { cavity.flip_triangles(); }
        SECTION("Separate nested part") {}
        source.merge(cavity);
        const auto proof = compare_model_stage_meshes(source, candidate, true).at("material_union");
        if (proof.at("status") == "completed")
            CHECK(proof.at("sampled_exterior_to_candidate_mm").get<double>() > 1.9);
        else
            CHECK(proof.at("reason") == "source_not_solid");
    }
    SECTION("A through-hole wall remains exterior even inside the candidate solid")
    {
        indexed_triangle_set ring;
        for (float z : {0.f, 2.f})
            for (const auto& xy : {Vec2f(-5, -5), Vec2f(5, -5), Vec2f(5, 5), Vec2f(-5, 5),
                                   Vec2f(-2, -2), Vec2f(2, -2), Vec2f(2, 2), Vec2f(-2, 2)})
                ring.vertices.emplace_back(xy.x(), xy.y(), z);
        const auto quad = [&](int a, int b, int c, int d) {
            ring.indices.emplace_back(a, b, c);
            ring.indices.emplace_back(a, c, d);
        };
        for (int i = 0; i < 4; ++i) {
            const int n = (i + 1) % 4;
            quad(i, 4 + i, 4 + n, n);
            quad(8 + i, 8 + n, 12 + n, 12 + i);
            quad(i, n, 8 + n, 8 + i);
            quad(4 + i, 12 + i, 12 + n, 4 + n);
        }
        source = TriangleMesh(std::move(ring));
        second = make_cube(4., 4., 2.);
        second.translate(4., -2., 0.);
        source.merge(second);
        candidate = make_cube(13., 10., 2.);
        candidate.translate(-5., -5., 0.);
        const auto proof = compare_model_stage_meshes(source, candidate, true).at("material_union");
        REQUIRE(proof.at("status") == "completed");
        CHECK(proof.at("sampled_exterior_to_candidate_mm").get<double>() > 0.9);
    }
    SECTION("An open original shell does not support a volume classification")
    {
        source.its.indices.pop_back();
        CHECK(compare_model_stage_meshes(source, candidate, true).at("material_union").at("status") == "not_applicable");
    }
    SECTION("Touching solids alone do not justify a component merge")
    {
        source = make_cube(10., 10., 10.);
        second = source;
        second.translate(10., 0., 0.);
        source.merge(second);
        CHECK(compare_model_stage_meshes(source, candidate, true).at("material_union").at("status") == "not_applicable");
    }
}

TEST_CASE("Stage surface preservation permits numeric cracks but protects real parts", "[ModelStageReport]")
{
    auto source = make_cube(10., 10., 10.);
    auto candidate = source;
    SECTION("Sub-nanometer coordinate noise and winding changes preserve the surface")
    {
        source.its.vertices[0].x() += 1e-8f;
        std::swap(candidate.its.indices[0][0], candidate.its.indices[0][1]);
        std::reverse(candidate.its.indices.begin(), candidate.its.indices.end());
        CHECK(model_stage_preserves_surface(source, candidate));
    }
    SECTION("A moved surface is not numeric cleanup")
    {
        candidate.its.vertices[0].x() += 0.0001f;
        CHECK_FALSE(model_stage_preserves_surface(source, candidate));
    }
    SECTION("Missing real faces cannot be exempted")
    {
        candidate.its.indices.pop_back();
        CHECK_FALSE(model_stage_preserves_surface(source, candidate));
    }
    SECTION("A duplicate replacing a real face cannot be exempted")
    {
        candidate.its.indices[0] = candidate.its.indices[1];
        CHECK_FALSE(model_stage_preserves_surface(source, candidate));
    }
    SECTION("Joining separate solids across a real gap is not numeric cleanup")
    {
        auto second = make_cube(10., 10., 0.001);
        second.translate(10.001, 0., 0.);
        source.merge(second);
        second.translate(-0.001, 0., 0.);
        candidate.merge(second);
        CHECK_FALSE(model_stage_preserves_surface(source, candidate));
    }
}

TEST_CASE("Stage STL above one million faces retains the section input budget", "[ModelReconstruction][ModelStageReport][slow]")
{
    ReconstructionTempDirectory temp;
    const auto                  input = (temp.path / "million-face.stl").string();
    auto                        its   = its_make_cube(10., 10., 10.);
    // A flat grid adds one million faces below the chosen section. Each edge
    // has bounded adjacency; no expensive all-pairs CGAL inspection is needed.
    constexpr int columns = 1000, rows = 500;
    const int     offset = int(its.vertices.size());
    its.vertices.reserve(its.vertices.size() + (columns + 1) * (rows + 1));
    its.indices.reserve(its.indices.size() + 2 * columns * rows);
    for (int row = 0; row <= rows; ++row)
        for (int column = 0; column <= columns; ++column)
            its.vertices.emplace_back(100.f + column * 0.001f, 100.f + row * 0.001f, 0.f);
    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < columns; ++column) {
            const int lower = offset + row * (columns + 1) + column;
            const int upper = lower + columns + 1;
            its.indices.emplace_back(lower, lower + 1, upper);
            its.indices.emplace_back(lower + 1, upper + 1, upper);
        }
    TriangleMesh source(std::move(its));
    REQUIRE(source.its.indices.size() == 1000012);
    REQUIRE(store_stl(input.c_str(), &source, true));
    const auto  original_sha256 = model_mesh_file_sha256(input);
    std::string parsed_sha256;
    const auto  loaded = read_bounded_model_stl(input, parsed_sha256);
    CHECK(parsed_sha256 == original_sha256);
    CHECK(model_mesh_file_sha256(input) == original_sha256);
    REQUIRE(loaded.its.vertices.size() == source.its.vertices.size());
    REQUIRE(loaded.its.indices.size() == source.its.indices.size());
    CHECK(same_ordered_triangles(source, loaded));

    ModelSectionEvidenceOptions options;
    options.slice_zs = std::vector<float>{5.f};
    {
        // Recreate the old parser representation, with three stored vertices
        // per face, to bind this regression to the previous input-limit failure.
        TriangleMesh unshared;
        unshared.its.vertices.reserve(3 * loaded.its.indices.size());
        unshared.its.indices.reserve(loaded.its.indices.size());
        for (const auto& face : loaded.its.indices) {
            const int first = int(unshared.its.vertices.size());
            for (int corner = 0; corner < 3; ++corner)
                unshared.its.vertices.push_back(loaded.its.vertices[face[corner]]);
            unshared.its.indices.emplace_back(first, first + 1, first + 2);
        }
        const auto previous = inspect_model_section_evidence(unshared, options);
        CHECK(previous.at("status") == "incomplete");
        CHECK(previous.at("reason") == "input_limit");
    }
    const auto sections = inspect_model_section_evidence(loaded, options);
    INFO(sections.dump(2));
    REQUIRE(sections.at("status") == "completed");
    CHECK(sections.at("planes") == 1);
    CHECK(sections.at("effective_regions") == 1);
    CHECK(sections.at("effective_area_sum_mm2").get<double>() == Approx(100.));
    // This verifies the parser/section-budget chain, not a million-face repair.
}

TEST_CASE("Reconstruction workload advice follows physical size and voxel spacing", "[ModelReconstruction]")
{
    const BoundingBoxf3 box(Vec3d::Zero(), Vec3d(100, 100, 10));
    CHECK(model_reconstruction_grid_warning(box, 0.1) == nullptr); // Exactly 100 million.
    CHECK(model_reconstruction_grid_warning(box, 0.09) != nullptr);
    CHECK(model_reconstruction_grid_warning(box, 0.2) == nullptr);
    const BoundingBoxf3 flat(Vec3d::Zero(), Vec3d(2000, 2000, 0));
    CHECK(model_reconstruction_grid_warning(flat, 0.1) != nullptr);
}

TEST_CASE("Reconstruction budgets scale without overriding explicit limits", "[ModelReconstruction]")
{
    const auto   automatic = ModelReconstructionOptions{}.limits;
    const size_t ram = size_t(16) * 1024 * 1024 * 1024;
    for (const auto faces : {12u, 200000u, 200001u, 1000000u, 1000001u, 1808823u, 3000000u}) {
        const auto budget = model_reconstruction_limits(faces, automatic, ram);
        CHECK(budget.timeout_ms == (faces > 1000000 ? 300000 : faces > 200000 ? 180000 : 120000));
        CHECK(budget.memory_limit_mb == (faces > 1000000 ? 4096 : faces > 200000 ? 2048 : 1024));
    }
    CHECK(model_reconstruction_limits(1808823, automatic, ram / 2).memory_limit_mb == 2048);
    CHECK(model_reconstruction_limits(1808823, automatic, 0).memory_limit_mb == 1024);
    CHECK(model_reconstruction_limits(1808823, automatic, 1024 * 1024 * 1024).memory_limit_mb == 256);
    auto requested = automatic;
    requested.timeout_ms = 1000;
    requested.memory_limit_mb = 512;
    int ticks = 0;
    requested.is_canceled = [] { return true; };
    requested.on_tick = [&] { ++ticks; };
    const auto explicit_budget = model_reconstruction_limits(1808823, requested, ram);
    CHECK(explicit_budget.timeout_ms == 1000);
    CHECK(explicit_budget.memory_limit_mb == 512);
    CHECK(explicit_budget.is_canceled());
    explicit_budget.on_tick();
    CHECK(ticks == 1);
    requested.timeout_ms = 300001;
    CHECK_THROWS(model_reconstruction_limits(12, requested, ram));
    requested.timeout_ms = 0;
    requested.memory_limit_mb = 4097;
    CHECK_THROWS(model_reconstruction_limits(12, requested, ram));
    requested.memory_limit_mb = 127;
    CHECK_THROWS(model_reconstruction_limits(12, requested, ram));
}

TEST_CASE("Reconstruction eligibility never clears unsupported data", "[ModelReconstruction]")
{
    Model model  = reconstruction_model();
    auto& object = *model.objects.front();
    REQUIRE(model_reconstruction_ineligibility(object).empty());
    SECTION("multiple instances") { object.add_instance(); }
    SECTION("modifier") { object.volumes.front()->set_type(ModelVolumeType::PARAMETER_MODIFIER); }
    SECTION("multiple volumes") { object.add_volume(make_cube(1., 1., 1.)); }
    SECTION("layer profile") { object.layer_height_profile.set({0., 0.2, 10., 0.1}); }
    SECTION("connector") { object.volumes.front()->cut_info.is_connector = true; }
    REQUIRE_FALSE(model_reconstruction_ineligibility(object).empty());
    const auto                 identity = model_reconstruction_source_identity(object);
    ModelReconstructionOptions options;
    options.voxel_mm                  = 0.1;
    auto                       result = reconstruct_model_candidate(model, 0, options);
    CHECK(result.status == "unsupported");
    CHECK_FALSE(result.candidate_model);
    CHECK(model_reconstruction_source_identity(object) == identity);
    CHECK_FALSE(result.report.at("print_ready").get<bool>());
}

TEST_CASE("Reconstruction source identity follows relevant edits", "[ModelReconstruction]")
{
    Model      model  = reconstruction_model();
    auto&      object = *model.objects.front();
    const auto before = model_reconstruction_source_identity(object);
    SECTION("name") { object.name = "renamed"; }
    SECTION("instance placement") { object.instances.front()->set_offset(Vec3d(1., 2., 3.)); }
    SECTION("volume placement") { object.volumes.front()->set_offset(Vec3d(1., 2., 3.)); }
    SECTION("mesh") { object.volumes.front()->set_mesh(make_cube(12., 10., 10.)); }
    SECTION("config") { object.config.set_key_value("perimeters", new ConfigOptionInt(4)); }
    CHECK(model_reconstruction_source_identity(object) != before);
}

TEST_CASE("Reconstruction failure and pre-cancel preserve the source", "[ModelReconstruction]")
{
    Model                      model    = reconstruction_model();
    auto&                      object   = *model.objects.front();
    const auto                 identity = model_reconstruction_source_identity(object);
    const auto                 mesh     = object.volumes.front()->mesh_ptr();
    ModelReconstructionOptions options;
    options.voxel_mm = 0.0803;
    SECTION("pre-cancel")
    {
        options.limits.is_canceled = [] { return true; };
    }
    SECTION("invalid parameters") { options.mesh.max_deviation_mm = -1.; }
    SECTION("meshlib requires voxel") { options.voxel_mm = 0.; }
    SECTION("meshlib invalid voxel")
    {
        options.voxel_mm = std::numeric_limits<double>::infinity();
    }
    SECTION("meshlib float voxel underflow")
    {
        options.voxel_mm = 1e-100;
    }
    SECTION("meshlib pre-cancel")
    {
        options.voxel_mm            = 0.0803;
        options.limits.is_canceled  = [] { return true; };
    }
    SECTION("early unknown cancellation callback exception")
    {
        options.voxel_mm            = 0.0803;
        options.limits.is_canceled  = []() -> bool { throw 17; };
    }
    SECTION("excessive input") { options.mesh.max_input_faces = 4; }
    const auto result = reconstruct_model_candidate(model, 0, options);
    REQUIRE_FALSE(result.candidate_model);
    CHECK(result.status != "candidate_ready");
    CHECK(model_reconstruction_source_identity(object) == identity);
    CHECK(object.volumes.front()->mesh_ptr() == mesh);
    CHECK_FALSE(result.report.at("mesh_validated").get<bool>());
    CHECK(result.report.at("trial_slice") == "not_run");
}

TEST_CASE("SDK-OFF MeshLib requests never fall back to another backend", "[ModelReconstruction]")
{
    if (OrcaMeshLib::available()) {
        SUCCEED("SDK is enabled; the explicit in-process integration test verifies this build.");
        return;
    }
    Model                      model    = reconstruction_model();
    const auto                 identity = model_reconstruction_source_identity(*model.objects.front());
    ModelReconstructionOptions options;
    options.voxel_mm = 0.0803;
    const auto result  = reconstruct_model_candidate(model, 0, options);
    CHECK_FALSE(result.candidate_model);
    CHECK(result.status == "failed");
    CHECK(result.report.at("message").get<std::string>().find("does not include the MeshLib SDK backend") != std::string::npos);
    CHECK(model_reconstruction_source_identity(*model.objects.front()) == identity);
}

TEST_CASE("In-process MeshLib joins stopped work and remains reusable", "[.][MeshLibInProcessIntegration]")
{
    // Explicit opt-in: a disabled SDK must fail this requested integration run,
    // not silently skip it or count unavailable functionality as tested.
    REQUIRE(OrcaMeshLib::available());
    Model                      model       = reconstruction_model();
    auto&                      object      = *model.objects.front();
    const auto                 identity    = model_reconstruction_source_identity(object);
    const auto                 source_mesh = object.volumes.front()->mesh_ptr();
    ModelReconstructionOptions options;
    options.voxel_mm                = 0.0803;
    options.limits.memory_limit_mb  = 2048;
    const auto check_source         = [&] {
        CHECK(model_reconstruction_source_identity(object) == identity);
        CHECK(object.volumes.front()->mesh_ptr() == source_mesh);
    };
    const auto check_candidate = [&] {
        const auto result = reconstruct_model_candidate(model, 0, options);
        INFO(result.report.dump(2));
        REQUIRE(result.status == "candidate_ready");
        REQUIRE(result.candidate_model);
        CHECK(result.report["execution"]["mode"] == "in_process");
        CHECK(result.report["execution"]["process_id"] == current_reconstruction_process_id());
        CHECK(result.report["execution"]["hard_timeout"] == false);
        CHECK(result.report["execution"]["crash_isolation"] == false);
        CHECK(result.report["resource_limits"]["hard_memory_limit"] == false);
        CHECK(result.report["resource_limits"]["budget_mode"] == "preflight_memory_cooperative_timeout");
        CHECK(result.report["resource_limits"]["memory_at_admission_bytes"].get<uint64_t>() > 0);
        CHECK_FALSE(result.report["resource_limits"].contains("observed_peak_memory_bytes"));
        CHECK(result.report["engine"]["sdk"]["runtime_identity_verified"] == true);
        CHECK_FALSE(result.report["engine"].contains("sidecar"));
        CHECK(result.report["mesh_validated"] == true);
        CHECK(result.report["print_ready"] == false);
        check_source();
    };
    check_candidate();

    SECTION("cancel requested during work")
    {
        bool   cancel               = false;
        size_t ticks                = 0;
        options.limits.is_canceled  = [&] { return cancel; };
        options.limits.on_tick      = [&] {
            ++ticks;
            cancel = true;
        };
        const auto result = reconstruct_model_candidate(model, 0, options);
        INFO(result.report.dump(2));
        REQUIRE(ticks > 0);
        CHECK(result.status == "canceled");
        CHECK(result.message == "Reconstruction was canceled. The original model was kept.");
        CHECK_FALSE(result.candidate_model);
        CHECK(result.report["mesh_validated"] == false);
    }
    SECTION("expired cooperative budget discards all late output")
    {
        options.limits.timeout_ms  = 1;
        const auto result          = reconstruct_model_candidate(model, 0, options);
        INFO(result.report.dump(2));
        CHECK(result.status == "timed_out");
        CHECK(result.message == "Reconstruction exceeded its time budget. The original model was kept.");
        CHECK_FALSE(result.candidate_model);
        CHECK(result.report["mesh_validated"] == false);
    }
    SECTION("caller callback exception is contained")
    {
        size_t ticks            = 0;
        options.limits.on_tick  = [&] {
            ++ticks;
            throw std::runtime_error("test callback failure");
        };
        const auto result = reconstruct_model_candidate(model, 0, options);
        INFO(result.report.dump(2));
        REQUIRE(ticks > 0);
        CHECK(result.status == "failed");
        CHECK_FALSE(result.candidate_model);
        CHECK(result.report["mesh_validated"] == false);
    }
    SECTION("unknown caller callback exception is contained")
    {
        size_t ticks            = 0;
        options.limits.on_tick  = [&] {
            ++ticks;
            throw 17;
        };
        const auto result = reconstruct_model_candidate(model, 0, options);
        INFO(result.report.dump(2));
        REQUIRE(ticks > 0);
        CHECK(result.status == "failed");
        CHECK_FALSE(result.candidate_model);
        CHECK(result.report["mesh_validated"] == false);
    }
    check_source();
    options.limits.is_canceled = {};
    options.limits.on_tick     = {};
    options.limits.timeout_ms  = 0;
    check_candidate();
}

TEST_CASE("Installing a matching convex hull invalidates an existing footprint", "[ModelReconstruction]")
{
    Model         model    = reconstruction_model();
    auto&         object   = *model.objects.front();
    auto&         volume   = *object.volumes.front();
    auto&         instance = *object.instances.front();
    const Polygon before   = volume.get_convex_hull_2d(instance.get_matrix());
    REQUIRE(before.is_valid());

    volume.set_mesh(make_cube(2., 2., 2.));
    volume.set_convex_hull(make_cube(2., 2., 2.));
    const Polygon installed = volume.get_convex_hull_2d(instance.get_matrix());
    REQUIRE(installed.is_valid());
    CHECK(installed.points != before.points);
    volume.invalidate_convex_hull_2d();
    CHECK(volume.get_convex_hull_2d(instance.get_matrix()) == installed);
}

TEST_CASE("MeshLib reconstruction preserves placement and staged metadata", "[.][ReconstructionIntegration]")
{
    REQUIRE(model_reconstruction_available());
    Model model    = reconstruction_model();
    auto& object   = *model.objects.front();
    auto& volume   = *object.volumes.front();
    auto& instance = *object.instances.front();
    volume.set_mesh(make_cube(2., 2., 2.));
    volume.set_offset(Vec3d(0.2, 0.3, 0.4));
    instance.set_offset(Vec3d(20., 30., 0.));
    instance.set_scaling_factor(Vec3d(1.1, 0.9, 1.5));
    instance.set_mirror(Vec3d(-1., 1., 1.));
    instance.set_assemble_offset(Vec3d(1., 2., 3.));
    volume.source.input_file = "source.stl";
    object.config.set_key_value("perimeters", new ConfigOptionInt(4));
    volume.calculate_convex_hull();
    volume.invalidate_convex_hull_2d();
    const Polygon source_footprint = volume.get_convex_hull_2d(instance.get_matrix());
    REQUIRE(source_footprint.is_valid());
    REQUIRE(instance.convex_hull_2d().is_valid());
    const auto                 identity           = model_reconstruction_source_identity(object);
    const auto                 volume_transform   = volume.get_matrix();
    const auto                 instance_transform = instance.get_matrix();
    const auto                 assembly           = instance.get_assemble_transformation().get_matrix();
    ModelReconstructionOptions options;
    options.voxel_mm   = 0.1;
    auto result        = reconstruct_model_candidate(model, 0, options);
    INFO(result.report.dump(2));
    REQUIRE(result.status == "candidate_ready");
    REQUIRE(result.candidate_model);
    const auto& candidate = *result.candidate_model->objects.front();
    CHECK(candidate.volumes.front()->get_matrix().matrix() == volume_transform.matrix());
    CHECK(candidate.instances.front()->get_matrix().matrix() == instance_transform.matrix());
    CHECK(candidate.instances.front()->get_assemble_transformation().get_matrix().matrix() == assembly.matrix());
    CHECK(candidate.config.opt_int("perimeters") == 4);
    CHECK(candidate.volumes.front()->source.input_file == "source.stl");
    CHECK(candidate.volumes.front()->id() != volume.id());
    CHECK(result.report["coordinate_validation"]["local"]["validation"]["complete"] == true);
    CHECK(result.report["coordinate_validation"]["physical_round_trip"]["validation"]["complete"] == true);
    CHECK(result.report["convex_hull_validation"]["validation"]["complete"] == true);
    CHECK_FALSE(result.report.at("convex_hull_mesh_identity").get<std::string>().empty());
    auto& candidate_volume   = *candidate.volumes.front();
    auto& candidate_instance = *candidate.instances.front();
    REQUIRE(candidate_volume.get_convex_hull_shared_ptr());
    CHECK_FALSE(candidate_volume.get_convex_hull().empty());
    const Polygon candidate_footprint = candidate_volume.get_convex_hull_2d(candidate_instance.get_matrix());
    REQUIRE(candidate_footprint.is_valid());
    CHECK(candidate_footprint.points != source_footprint.points);
    candidate_volume.invalidate_convex_hull_2d();
    CHECK(candidate_volume.get_convex_hull_2d(candidate_instance.get_matrix()) == candidate_footprint);
    CHECK(volume.get_convex_hull_2d(instance.get_matrix()) == source_footprint);
    CHECK(model_reconstruction_source_identity(object) == identity);
}

TEST_CASE("MeshLib skips distant hole filling only for verified closed surfaces", "[.][MeshLibInProcessIntegration]")
{
    REQUIRE(model_reconstruction_available());
    Model  model       = reconstruction_model();
    auto   source      = make_cube(4., 4., 4.);
    bool   close_holes = false;
    bool   rejected    = false;
    size_t removed     = 0;
    SECTION("Closed solid") {}
    SECTION("Intersecting closed solids still use winding-number union")
    {
        auto other = make_cube(4., 4., 4.);
        other.translate(2.f, 2.f, 2.f);
        source.merge(other);
    }
    SECTION("Open face still requires hole filling")
    {
        source.its.indices.erase(source.its.indices.begin());
        close_holes = true;
    }
    SECTION("Conflicting face direction keeps hole filling")
    {
        std::swap(source.its.indices.front()[0], source.its.indices.front()[1]);
        close_holes = true;
        rejected    = true; // Existing hole filling does not repair this inconsistent winding.
    }
    SECTION("Duplicate input keeps the existing reconstruction policy")
    {
        source.its.indices.push_back(source.its.indices.front());
        close_holes = true;
        removed     = 1;
    }
    SECTION("Collapsed face without a duplicate still takes the cleanup path")
    {
        const int v = source.its.indices.front()[0];
        source.its.indices.emplace_back(v, v, v);
        close_holes = true;
        removed     = 1;
    }
    model.objects.front()->volumes.front()->set_mesh(source);
    const auto                 identity = model_reconstruction_source_identity(*model.objects.front());
    ModelReconstructionOptions options;
    options.voxel_mm  = 0.1;
    const auto result = reconstruct_model_candidate(model, 0, options);
    INFO(result.report.dump(2));
    CHECK(result.report["engine"]["sdk"]["parameters"]["close_holes_in_hole_winding_number"] == close_holes);
    CHECK(result.report["engine"]["input_preparation"]["removed_redundant_faces"] == removed);
    CHECK(model_reconstruction_source_identity(*model.objects.front()) == identity);
    if (rejected) {
        CHECK_FALSE(result.candidate_model);
        CHECK(result.status == "candidate_rejected");
        CHECK(result.report["mesh_validated"] == false);
        return;
    }
    REQUIRE(result.candidate_model);
    const auto& candidate = result.candidate_model->objects.front()->volumes.front()->mesh();
    CHECK(inside_closed_mesh(candidate, Vec3d(2., 2., 2.)));
    CHECK_FALSE(inside_closed_mesh(candidate, Vec3d(7., 2., 2.)));
    CHECK(result.report["mesh_validated"] == true);
    CHECK(model_reconstruction_source_identity(*model.objects.front()) == identity);
}

TEST_CASE("MeshLib reports topology changes independently of surface deviation", "[.][MeshLibInProcessIntegration]")
{
    REQUIRE(model_reconstruction_available());
    Model   model          = reconstruction_model();
    auto    source         = make_cube(4., 4., 4.);
    bool    comparable     = true;
    bool    changed        = false;
    int64_t expected_euler = 2;
    SECTION("A closed solid keeps its topology") {}
    SECTION("Separated solids keep their independent surfaces")
    {
        auto other = source;
        other.translate(5.f, 0.f, 0.f);
        source.merge(other);
        expected_euler = 4;
    }
    SECTION("A subvoxel gap merges two valid solids")
    {
        auto other = source;
        other.translate(4.02f, 0.f, 0.f);
        source.merge(other);
        expected_euler = 4;
        changed        = true;
    }
    SECTION("A subvoxel torus bore closes without changing component count")
    {
        source         = make_torus(1.01, 1., PI / 16.);
        expected_euler = 0;
        changed        = true;
    }
    SECTION("An open input cannot establish the original closed topology")
    {
        source.its.indices.pop_back();
        comparable = false;
    }
    model.objects.front()->volumes.front()->set_mesh(source);
    const auto                 identity = model_reconstruction_source_identity(*model.objects.front());
    ModelReconstructionOptions options;
    options.voxel_mm              = 0.1;
    options.mesh.max_deviation_mm = 3.; // Geometry warning and topology evidence are independent.
    const auto result             = reconstruct_model_candidate(model, 0, options);
    INFO(result.report.dump(2));
    REQUIRE(result.candidate_model);
    CHECK(result.report.at("mesh_validated") == true);
    CHECK(result.report.at("metrics").at("deviation_warning") == false);
    const auto& structure = result.report.at("engine").at("structure_preservation");
    CHECK(structure.at("verified") == false);
    CHECK(structure.at("topology_comparison_complete") == comparable);
    if (comparable) {
        CHECK(structure.at("input_euler_characteristic") == expected_euler);
        CHECK(structure.at("output_euler_characteristic") == (changed ? 2 : expected_euler));
        CHECK(structure.at("topology_changed") == changed);
    } else {
        CHECK(structure.at("topology_changed").is_null());
        CHECK_FALSE(structure.contains("input_euler_characteristic"));
    }
    CHECK(model_reconstruction_source_identity(*model.objects.front()) == identity);
}

TEST_CASE("A valid native reconstruction is retained without local repair", "[.][MeshLibInProcessIntegration]")
{
    REQUIRE(model_reconstruction_available());
    Model model = reconstruction_model();
    const auto& source = model.objects.front()->volumes.front()->mesh();
    OrcaMeshLib::Mesh input;
    for (const auto& v : source.its.vertices)
        input.vertices.push_back({v.x(), v.y(), v.z()});
    for (const auto& f : source.its.indices)
        input.faces.push_back({f.x(), f.y(), f.z()});
    OrcaMeshLib::Options native_options;
    native_options.voxel_mm    = 0.3;
    native_options.close_holes = false;
    const auto native = OrcaMeshLib::rebuild(input, native_options);
    REQUIRE(native.success);
    indexed_triangle_set its;
    for (const auto& v : native.mesh.vertices)
        its.vertices.emplace_back(v[0], v[1], v[2]);
    for (const auto& f : native.mesh.faces)
        its.indices.emplace_back(f[0], f[1], f[2]);
    const TriangleMesh expected(std::move(its));
    nlohmann::json validation;
    REQUIRE(validate_reconstruction_mesh(expected, validation, RECONSTRUCTION_MAX_OUTPUT_FACES));

    ModelReconstructionOptions options;
    options.voxel_mm = 0.3;
    const auto result = reconstruct_model_candidate(model, 0, options);
    INFO(result.report.dump(2));
    REQUIRE(result.candidate_model);
    CHECK_FALSE(result.report.at("engine").at("sdk").at("elapsed_ms").contains("local_cleanup"));
    CHECK_FALSE(result.report.at("engine").contains("pre_cleanup_mesh_check"));
    CHECK(same_ordered_triangles(expected, result.candidate_model->objects.front()->volumes.front()->mesh()));
}

TEST_CASE("MeshLib reconstruction retains a known cavity and thin wall", "[.][MeshLibInProcessIntegration]")
{
    REQUIRE(model_reconstruction_available());
    Model  model           = reconstruction_model();
    auto   source          = make_cube(20., 20., 20.);
    double expected_volume = 7000.;
    bool   thin_wall       = false;
    bool   blind_pocket    = false;
    SECTION("Enclosed cavity")
    {
        auto inner = make_cube(10., 10., 10.);
        inner.translate(5.f, 5.f, 5.f);
        inner.flip_triangles();
        source.merge(inner);
    }
    SECTION("Blind pocket keeps its opening and floor")
    {
        blind_pocket    = true;
        expected_volume = 6500.;
        source          = make_cube(20., 20., 5.);
        for (double x : {0., 15.}) {
            auto wall = make_cube(5., 20., 20.);
            wall.translate(x, 0., 0.);
            source.merge(wall);
        }
        for (double y : {0., 15.}) {
            auto wall = make_cube(10., 5., 20.);
            wall.translate(5., y, 0.);
            source.merge(wall);
        }
    }
    SECTION("A 0.1 mm wall is not expanded into a 0.2 mm envelope")
    {
        source          = make_cube(20., 20., 0.1);
        expected_volume = 40.;
        thin_wall       = true;
    }
    model.objects.front()->volumes.front()->set_mesh(source);
    const auto                 identity = model_reconstruction_source_identity(*model.objects.front());
    ModelReconstructionOptions options;
    options.voxel_mm  = 0.1;
    const auto result = reconstruct_model_candidate(model, 0, options);
    INFO(result.report.dump(2));
    REQUIRE(result.candidate_model);
    const auto& candidate = result.candidate_model->objects.front()->volumes.front()->mesh();
    CHECK(its_volume(candidate.its) == Approx(expected_volume).epsilon(0.01));
    if (thin_wall)
        CHECK(candidate.bounding_box().size().z() == Approx(0.1).margin(0.001));
    else {
        CHECK(result.report.at("engine").at("final_mesh_check").at("metrics").at("component_count") == (blind_pocket ? 1 : 2));
        // Volume and shell count alone do not prove that the cavity survived.
        for (double z : {5.2, 10., 14.8}) {
            CHECK_FALSE(inside_closed_mesh(candidate, Vec3d(10., 10., z)));
            CHECK(inside_closed_mesh(candidate, Vec3d(4., 10., z)));
        }
        CHECK(inside_closed_mesh(candidate, Vec3d(10., 10., 4.)));
        CHECK(inside_closed_mesh(candidate, Vec3d(10., 10., 19.8)) == !blind_pocket);
    }
    CHECK(model_reconstruction_source_identity(*model.objects.front()) == identity);
    CHECK(result.report.at("backend") == "meshlib");
    CHECK_FALSE(result.report.at("print_ready").get<bool>());
}

TEST_CASE("MeshLib reconstruction keeps source and candidate geometry independent of placement", "[.][MeshLibInProcessIntegration]")
{
    REQUIRE(model_reconstruction_available());
    Model     model    = reconstruction_model();
    auto&     volume   = *model.objects.front()->volumes.front();
    auto&     instance = *model.objects.front()->instances.front();
    auto      its      = make_cube(6., 6., 6.).its;
    const int a = its.indices.front()[0], b = its.indices.front()[1];
    const int middle = int(its.vertices.size());
    its.vertices.push_back((its.vertices[a] + 1e-7f * (its.vertices[b] - its.vertices[a])).eval());
    const auto original_faces = its.indices;
    its.indices.clear();
    for (const auto& face : original_faces) {
        bool split = false;
        for (int corner = 0; corner < 3; ++corner) {
            const int first = face[corner], second = face[(corner + 1) % 3], third = face[(corner + 2) % 3];
            if ((first == a && second == b) || (first == b && second == a)) {
                its.indices.emplace_back(first, middle, third);
                its.indices.emplace_back(middle, second, third);
                split = true;
                break;
            }
        }
        if (!split)
            its.indices.push_back(face);
    }
    REQUIRE(its.indices.size() == 14);
    for (const auto& face : its.indices)
        REQUIRE((its.vertices[face[1]] - its.vertices[face[0]]).cross(its.vertices[face[2]] - its.vertices[face[0]]).squaredNorm() > 0.f);
    volume.set_mesh(TriangleMesh(std::move(its)));
    volume.set_offset(Vec3d::Zero());
    ModelReconstructionOptions options;
    options.voxel_mm = 0.1;
    TriangleMesh baseline;
    // Include a distant plate position: placement must remain a transform, not
    // be baked into either the source or candidate float vertices.
    for (double offset : {0., 50.5, -50.5, 2000.5}) {
        instance.set_offset(Vec3d(offset, -offset, offset == 0. ? 0. : std::copysign(3.5234421491622925, offset)));
        const auto identity = model_reconstruction_source_identity(*model.objects.front());
        const auto result   = reconstruct_model_candidate(model, 0, options);
        INFO(result.report.dump(2));
        REQUIRE(result.candidate_model);
        CHECK(result.report.at("engine").at("sdk").at("skipped_face_count") == 0);
        CHECK(result.report.at("engine").at("sdk").at("input").at("loaded_faces") == 14);
        CHECK(result.report.at("coordinate_validation").at("physical_round_trip").at("validation").at("complete") == true);
        CHECK(result.report.at("coordinate_validation").at("includes_placement_translation") == false);
        CHECK(result.report.at("placement").at("before_min_z_mm").get<double>() == Approx(instance.get_offset().z()).margin(1e-6));
        CHECK(model_reconstruction_source_identity(*model.objects.front()) == identity);
        const auto& object = *result.candidate_model->objects.front();
        CHECK(object.instances.front()->get_matrix().matrix() == instance.get_matrix().matrix());
        if (offset == 0.)
            baseline = object.volumes.front()->mesh();
        else
            CHECK(same_ordered_triangles(baseline, object.volumes.front()->mesh()));
    }
}

TEST_CASE("MeshLib reconstruction retains an open pipe bore and a separate small solid", "[.][MeshLibInProcessIntegration]")
{
    REQUIRE(model_reconstruction_available());
    const double placement       = GENERATE(0., 50.5, -50.5);
    Model        model           = reconstruction_model();
    auto         source          = make_cube(6., 6., 6.);
    bool         pipe            = false;
    double       expected_volume = 216.125;
    Vec3d        small_center(8.25, 2.25, 2.25);
    SECTION("Independent 0.5 mm solid")
    {
        auto small = make_cube(0.5, 0.5, 0.5);
        small.translate(8., 2., 2.);
        source.merge(small);
    }
    SECTION("Independent tetrahedron with distinct vertices is retained")
    {
        indexed_triangle_set tetrahedron;
        tetrahedron.vertices = {Vec3f(8.f, 2.f, 2.f), Vec3f(8.5f, 2.f, 2.f), Vec3f(8.f, 2.5f, 2.f), Vec3f(8.f, 2.f, 2.5f)};
        tetrahedron.indices  = {Vec3i32(0, 2, 1), Vec3i32(0, 1, 3), Vec3i32(1, 2, 3), Vec3i32(0, 3, 2)};
        source.merge(TriangleMesh(std::move(tetrahedron)));
        small_center    = Vec3d(8.125, 2.125, 2.125);
        expected_volume = 216. + 0.125 / 6.;
    }
    SECTION("Open annular end must not become a solid lid")
    {
        pipe                             = true;
        const double         bore_radius = GENERATE(1.5, 3.);
        indexed_triangle_set rings;
        constexpr int        sides = 32;
        for (int ring = 0; ring < 2; ++ring) {
            const double radius = ring == 0 ? 5. : bore_radius;
            for (int k = 0; k < sides; ++k) {
                const double angle = 2. * PI * k / sides;
                rings.vertices.emplace_back(radius * std::cos(angle), radius * std::sin(angle), 0.f);
                rings.vertices.emplace_back(radius * std::cos(angle), radius * std::sin(angle), 6.f);
            }
            for (int k = 0; k < sides; ++k) {
                const int a = 2 * (ring * sides + k), b = 2 * (ring * sides + (k + 1) % sides);
                rings.indices.emplace_back(a, b, b + 1);
                rings.indices.emplace_back(a, b + 1, a + 1);
                if (ring == 1) {
                    std::swap(rings.indices[rings.indices.size() - 2][1], rings.indices[rings.indices.size() - 2][2]);
                    std::swap(rings.indices.back()[1], rings.indices.back()[2]);
                }
            }
        }
        for (int k = 0; k < sides; ++k) {
            const int a = 2 * k, b = 2 * ((k + 1) % sides), c = a + 2 * sides, d = b + 2 * sides;
            rings.indices.emplace_back(a, c, b);
            rings.indices.emplace_back(b, c, d);
        }
        // Real frame exports repeat the bore wall. Native import may reverse
        // one copy; keeping both then cancels the wall in winding-number tests.
        const int duplicate_mode = GENERATE(0, 1, 2, 3);
        CAPTURE(bore_radius, duplicate_mode, placement);
        if (duplicate_mode != 0) {
            std::vector<Vec3i32> copies;
            for (int i = 2 * sides; i < 4 * sides; ++i) {
                Vec3i32 face = rings.indices[i];
                if (duplicate_mode >= 2)
                    std::swap(face[1], face[2]);
                copies.push_back(face);
            }
            if (duplicate_mode == 3)
                rings.indices.insert(rings.indices.begin(), copies.begin(), copies.end());
            else
                rings.indices.insert(rings.indices.end(), copies.begin(), copies.end());
        }
        source          = TriangleMesh(std::move(rings));
        expected_volume = 0.5 * sides * std::sin(2. * PI / sides) * (25. - bore_radius * bore_radius) * 6.;
    }
    model.objects.front()->volumes.front()->set_mesh(source);
    auto& instance = *model.objects.front()->instances.front();
    instance.set_offset(Vec3d(placement, placement, placement == 0. ? 0. : 3.5234421491622925));
    const auto                 instance_matrix = instance.get_matrix().matrix().eval();
    const auto                 identity        = model_reconstruction_source_identity(*model.objects.front());
    ModelReconstructionOptions options;
    options.voxel_mm  = 0.1;
    const auto result = reconstruct_model_candidate(model, 0, options);
    INFO(result.report.dump(2));
    REQUIRE(result.candidate_model);
    CHECK(result.report.at("coordinate_validation").at("local").at("validation").at("complete") == true);
    CHECK(result.report.at("coordinate_validation").at("physical_round_trip").at("validation").at("complete") == true);
    CHECK(result.candidate_model->objects.front()->instances.front()->get_matrix().matrix() == instance_matrix);
    const auto& candidate = result.candidate_model->objects.front()->volumes.front()->mesh();
    CHECK(its_volume(candidate.its) == Approx(expected_volume).epsilon(0.02));
    const auto& metrics = result.report.at("engine").at("final_mesh_check").at("metrics");
    CHECK(metrics.at("component_count") == (pipe ? 1 : 2));
    // Solid-angle probes distinguish a through bore from a capped/filled tube.
    if (pipe) {
        for (double z : {0.1, 1., 3., 5., 5.9}) {
            CHECK_FALSE(inside_closed_mesh(candidate, Vec3d(0.1, 0.2, z)));
            CHECK(inside_closed_mesh(candidate, Vec3d(4., 0.1, z)));
        }
    } else {
        CHECK(inside_closed_mesh(candidate, small_center));
        CHECK_FALSE(inside_closed_mesh(candidate, Vec3d(7., 2.25, 2.25)));
    }
    CHECK(model_reconstruction_source_identity(*model.objects.front()) == identity);
    CHECK_FALSE(result.report.at("print_ready").get<bool>());
}

TEST_CASE("MeshLib reconstruction keeps a bore connected across distance grid slabs", "[.][MeshLibInProcessIntegration]")
{
    REQUIRE(model_reconstruction_available());
    // At 0.1 mm this grid contains over 16 million samples, spanning multiple
    // slabs. Exercise both the open far field and closed narrow surface band.
    const bool           closed = GENERATE(false, true);
    indexed_triangle_set rings;
    constexpr int        sides  = 16;
    constexpr float      height = 60.f;
    for (int ring = 0; ring < 2; ++ring) {
        const double radius = ring == 0 ? 8. : 6.;
        for (int k = 0; k < sides; ++k) {
            const double angle = 2. * PI * k / sides;
            rings.vertices.emplace_back(radius * std::cos(angle), radius * std::sin(angle), 0.f);
            rings.vertices.emplace_back(radius * std::cos(angle), radius * std::sin(angle), height);
        }
        for (int k = 0; k < sides; ++k) {
            const int a = 2 * (ring * sides + k), b = 2 * (ring * sides + (k + 1) % sides);
            rings.indices.emplace_back(a, b, b + 1);
            rings.indices.emplace_back(a, b + 1, a + 1);
            if (ring == 1) {
                std::swap(rings.indices[rings.indices.size() - 2][1], rings.indices[rings.indices.size() - 2][2]);
                std::swap(rings.indices.back()[1], rings.indices.back()[2]);
            }
        }
    }
    for (int k = 0; k < sides; ++k) {
        const int a = 2 * k, b = 2 * ((k + 1) % sides), c = a + 2 * sides, d = b + 2 * sides;
        rings.indices.emplace_back(a, c, b);
        rings.indices.emplace_back(b, c, d);
        if (closed) {
            rings.indices.emplace_back(a + 1, b + 1, c + 1);
            rings.indices.emplace_back(b + 1, d + 1, c + 1);
        }
    }
    Model model = reconstruction_model();
    model.objects.front()->volumes.front()->set_mesh(TriangleMesh(std::move(rings)));
    const auto                 identity = model_reconstruction_source_identity(*model.objects.front());
    ModelReconstructionOptions options;
    options.voxel_mm  = 0.1;
    const auto result = reconstruct_model_candidate(model, 0, options);
    INFO(result.report.dump(2));
    REQUIRE(result.candidate_model);
    CHECK(result.report.at("engine").at("sdk").at("parameters").at("close_holes_in_hole_winding_number") == !closed);
    CHECK(result.report.at("engine").at("final_mesh_check").at("metrics").at("component_count") == 1);
    const auto&  candidate       = result.candidate_model->objects.front()->volumes.front()->mesh();
    const double expected_volume = 0.5 * sides * std::sin(2. * PI / sides) * (64. - 36.) * height;
    CHECK(its_volume(candidate.its) == Approx(expected_volume).epsilon(0.02));
    for (double z = 0.2; z < height; z += 0.5) {
        CAPTURE(z);
        CHECK_FALSE(inside_closed_mesh(candidate, Vec3d(0.1, 0.2, z)));
        CHECK(inside_closed_mesh(candidate, Vec3d(7., 0.1, z)));
    }
    CHECK(model_reconstruction_source_identity(*model.objects.front()) == identity);
}

TEST_CASE("MeshLib standard reconstruction preserves an open dome's outer envelope", "[.][MeshLibInProcessIntegration]")
{
    REQUIRE(model_reconstruction_available());
    Model model  = reconstruction_model();
    auto  source = make_cube(12., 12., 1.03603);
    source.translate(-6.f, -6.f, -1.03603f);
    // An open-bottom decorative dome rests on the floor, as in the radio model.
    // Its central depression and base must remain. An open sheet does not define
    // a solid interior: the standard SDK (and MeshInspector) may separate it
    // from the base. Do not impose a single-shell/filled-interior guarantee.
    indexed_triangle_set dome;
    const int            sides = GENERATE(16, 20, 24, 28, 32, 48, 64, 192);
    INFO("Dome sides: " << sides);
    const double radii[]   = {4.157, 3.2, 1.85, 1.2, 0.};
    const double heights[] = {0., 1., 1.845, 0.8, 0.308};
    for (int ring = 0; ring < 5; ++ring)
        for (int k = 0; k < sides; ++k) {
            const double angle = 2. * PI * k / sides;
            dome.vertices.emplace_back(radii[ring] * std::cos(angle), radii[ring] * std::sin(angle), heights[ring]);
        }
    for (int ring = 0; ring < 4; ++ring)
        for (int k = 0; k < sides; ++k) {
            const int a = ring * sides + k, b = ring * sides + (k + 1) % sides, c = a + sides, d = b + sides;
            dome.indices.emplace_back(a, b, c);
            if (ring < 3)
                dome.indices.emplace_back(b, d, c);
        }
    source.merge(TriangleMesh(std::move(dome)));
    // Dense, rotated fans exercise the standard winding approximation.
    const double rotation = GENERATE(0., 0.73);
    INFO("Dome rotation: " << rotation);
    source.rotate_z(rotation);
    model.objects.front()->volumes.front()->set_mesh(source);
    const auto                 identity = model_reconstruction_source_identity(*model.objects.front());
    ModelReconstructionOptions options;
    options.voxel_mm  = 0.1;
    const auto result = reconstruct_model_candidate(model, 0, options);
    INFO(result.report.dump(2));
    REQUIRE(result.candidate_model);
    const auto& candidate = result.candidate_model->objects.front()->volumes.front()->mesh();
    const auto source_bounds = source.bounding_box();
    const auto output_bounds = candidate.bounding_box();
    CHECK((source_bounds.min - output_bounds.min).cwiseAbs().maxCoeff() <= 2. * options.voxel_mm);
    CHECK((source_bounds.max - output_bounds.max).cwiseAbs().maxCoeff() <= 2. * options.voxel_mm);
    CHECK(result.report.at("engine").at("structure_preservation").at("verified") == false);
    CHECK_FALSE(inside_closed_mesh(candidate, Vec3d(0., 0., 0.6)));
    CHECK(inside_closed_mesh(candidate, Vec3d(5., 0., -0.5)));
    CHECK(model_reconstruction_source_identity(*model.objects.front()) == identity);
}

TEST_CASE("MeshLib reconstruction preserves all faces at conflicting vertex fans", "[.][MeshLibInProcessIntegration]")
{
    REQUIRE(OrcaMeshLib::available());
    OrcaMeshLib::Mesh input;
    // Abstract non-manifold fans, with non-collinear points on a moment curve.
    // The SDK STL loader drops two of these six faces while trying to connect them.
    for (int i = 1; i <= 10; ++i) {
        const float a = 0.1f * i;
        input.vertices.push_back({a, a * a, a * a * a});
    }
    input.faces                   = {{0, 1, 2}, {0, 3, 4}, {5, 0, 6}, {6, 7, 5}, {6, 8, 9}, {6, 0, 4}};
    const auto           original = input;
    OrcaMeshLib::Options options;
    options.voxel_mm  = 0.1;
    const auto result = OrcaMeshLib::rebuild(input, options);
    const auto report = nlohmann::json::parse(result.report_json);
    INFO(report.dump(2));
    CHECK(report.at("input").at("loaded_faces") == input.faces.size());
    CHECK(report.at("skipped_face_count") == 0);
    CHECK(input.vertices == original.vertices);
    CHECK(input.faces == original.faces);
}

TEST_CASE("MeshLib sampling, extraction and simplification cancellation discard candidates and permit reuse",
          "[.][MeshLibInProcessIntegration]")
{
    REQUIRE(OrcaMeshLib::available());
    const auto        source = make_cube(6., 6., 6.);
    OrcaMeshLib::Mesh input;
    for (const auto& p : source.its.vertices)
        input.vertices.push_back({p.x(), p.y(), p.z()});
    for (const auto& f : source.its.indices)
        input.faces.push_back({f[0], f[1], f[2]});
    OrcaMeshLib::Options options;
    options.voxel_mm                = 0.1;
    options.close_holes             = GENERATE(false, true);
    const bool        throws        = GENERATE(false, true);
    const float       stop_progress = GENERATE(0.1f, 0.75f, 0.81f, 0.97f);
    std::atomic<bool> reached_stop_phase{false};
    std::atomic<float> observed_stop_progress{0.f};
    options.progress = [&](float progress) {
        if (progress <= stop_progress)
            return true;
        reached_stop_phase = true;
        observed_stop_progress.store(progress);
        if (throws)
            throw std::runtime_error("reconstruction callback failure");
        return false;
    };
    const auto canceled = OrcaMeshLib::rebuild(input, options);
    CHECK(reached_stop_phase.load());
    if (stop_progress == 0.75f)
        CHECK(observed_stop_progress.load() < 0.8f); // Extraction must stop before simplification starts.
    CHECK_FALSE(canceled.success);
    CHECK(canceled.status == (throws ? "callback_error" : "cancelled"));
    CHECK(canceled.mesh.faces.empty());
    CHECK(canceled.mesh.vertices.empty());
    options.progress  = {};
    const auto reused = OrcaMeshLib::rebuild(input, options);
    REQUIRE(reused.success);
    indexed_triangle_set mesh;
    for (const auto& p : reused.mesh.vertices)
        mesh.vertices.emplace_back(p[0], p[1], p[2]);
    for (const auto& f : reused.mesh.faces)
        mesh.indices.emplace_back(f[0], f[1], f[2]);
    nlohmann::json validation;
    CHECK(validate_reconstruction_mesh(TriangleMesh(std::move(mesh)), validation, RECONSTRUCTION_MAX_OUTPUT_FACES));
}

TEST_CASE("MeshLib output budget counts the simplified candidate and rejects excess faces", "[.][MeshLibInProcessIntegration]")
{
    REQUIRE(OrcaMeshLib::available());
    const auto        source = make_cube(6., 6., 6.);
    OrcaMeshLib::Mesh input;
    for (const auto& p : source.its.vertices)
        input.vertices.push_back({p.x(), p.y(), p.z()});
    for (const auto& f : source.its.indices)
        input.faces.push_back({f[0], f[1], f[2]});
    const auto          original = input;
    OrcaMeshLib::Options options;
    options.voxel_mm    = 0.1;
    options.close_holes = false;
    const auto reference = OrcaMeshLib::rebuild(input, options);
    REQUIRE(reference.success);
    REQUIRE(reference.mesh.faces.size() > 1);
    // Marching cubes produces many more vertices than survive simplification.
    // The output cap applies to the surviving geometry, not those deleted slots.
    options.max_output_faces = reference.mesh.faces.size();
    const auto exact = OrcaMeshLib::rebuild(input, options);
    INFO(exact.report_json);
    REQUIRE(exact.success);
    CHECK(exact.mesh.vertices == reference.mesh.vertices);
    CHECK(exact.mesh.faces == reference.mesh.faces);
    --options.max_output_faces;
    const auto excess = OrcaMeshLib::rebuild(input, options);
    INFO(excess.report_json);
    CHECK_FALSE(excess.success);
    CHECK(excess.status == "resource_limit");
    CHECK(excess.mesh.vertices.empty());
    CHECK(excess.mesh.faces.empty());
    CHECK(input.vertices == original.vertices);
    CHECK(input.faces == original.faces);
}

TEST_CASE("Automatic reconstruction admission uses physical grid and configured budgets", "[ReconstructionAdmission]")
{
    ReconstructionAdmissionOptions options;
    CHECK(options.voxel_mm == 0.30);
    CHECK(options.max_grid_cells == 200000000);
    CHECK(options.memory_limit_mb == 2048);
    CHECK(options.timeout_ms == 60000);
    options.voxel_mm = 1.;
    auto mesh = make_cube(100., 100., 100.);
    const auto original = mesh;
    options.max_grid_cells = 1000000;
    auto result = model_reconstruction_admission(mesh, options);
    if (!model_reconstruction_available()) {
        REQUIRE_FALSE(result.at("accepted").get<bool>());
        REQUIRE(result.at("reason") == "backend_unavailable");
        return;
    }
    REQUIRE(result.at("accepted").get<bool>());
    REQUIRE(result.at("grid_cells") == 1000000);
    REQUIRE(result.at("timeout_ms") == 60000);
    REQUIRE_FALSE(result.at("estimate_is_upper_bound").get<bool>());
    options.max_grid_cells = 999999;
    result = model_reconstruction_admission(mesh, options);
    REQUIRE_FALSE(result.at("accepted").get<bool>());
    REQUIRE(result.at("reason") == "grid_cell_budget_exceeded");
    options.voxel_mm = 2.;
    result = model_reconstruction_admission(mesh, options);
    REQUIRE(result.at("accepted").get<bool>());
    REQUIRE(result.at("grid_cells") == 125000);
    REQUIRE(same_ordered_triangles(mesh, original));

    // Thin broad geometry can fit the cell cap but exceed the surface-work budget.
    options = ReconstructionAdmissionOptions{};
    options.voxel_mm = 1.;
    result = model_reconstruction_admission(make_cube(10000., 10000., 0.1), options);
    REQUIRE_FALSE(result.at("accepted").get<bool>());
    REQUIRE(result.at("grid_cells") == 100000000);
    REQUIRE(result.at("reason") == "estimated_memory_budget_exceeded");
}

TEST_CASE("Automatic reconstruction admission validates configuration at entry", "[ReconstructionAdmission]")
{
    ReconstructionAdmissionOptions options;
    SECTION("zero voxel") { options.voxel_mm = 0.; }
    SECTION("nonfinite voxel") { options.voxel_mm = std::numeric_limits<double>::infinity(); }
    SECTION("float underflow") { options.voxel_mm = std::numeric_limits<double>::min(); }
    SECTION("empty grid cap") { options.max_grid_cells = 0; }
    SECTION("negative CLI cap converted to unsigned") { options.max_grid_cells = uint64_t(-1); }
    SECTION("unsupported memory") { options.memory_limit_mb = 4097; }
    SECTION("no time budget") { options.timeout_ms = 0; }
    SECTION("unsupported time budget") { options.timeout_ms = 300001; }
    REQUIRE_THROWS_AS(validate_reconstruction_admission_options(options), std::invalid_argument);
}
