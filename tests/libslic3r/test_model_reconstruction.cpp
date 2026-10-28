#include <catch2/catch.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/ModelReconstruction.hpp>
#include <libslic3r/ModelRepairEvidence.hpp>
#include <libslic3r/ModelStageReport.hpp>
#include <libslic3r/Format/STL.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <meshlib/MeshLibAdapter.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
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

TEST_CASE("Reconstruction budgets scale without overriding explicit limits", "[ModelReconstruction]")
{
    const auto automatic = ModelReconstructionOptions{}.process;
    const size_t ram = size_t(16) * 1024 * 1024 * 1024;
    for (const auto faces : {12u, 200000u, 200001u, 1000000u, 1000001u, 1808823u, 3000000u}) {
        const auto budget = model_reconstruction_process_options(faces, automatic, ram);
        CHECK(budget.timeout_ms == (faces > 1000000 ? 300000 : faces > 200000 ? 180000 : 60000));
        CHECK(budget.memory_limit_mb == (faces > 1000000 ? 4096 : faces > 200000 ? 2048 : 1024));
    }
    CHECK(model_reconstruction_process_options(1808823, automatic, ram / 2).memory_limit_mb == 2048);
    CHECK(model_reconstruction_process_options(1808823, automatic, 0).memory_limit_mb == 1024);
    CHECK(model_reconstruction_process_options(1808823, automatic, 1024 * 1024 * 1024).memory_limit_mb == 256);
    auto requested = automatic;
    requested.timeout_ms = 1000;
    requested.memory_limit_mb = 512;
    int ticks = 0;
    requested.is_canceled = [] { return true; };
    requested.on_tick = [&] { ++ticks; };
    const auto explicit_budget = model_reconstruction_process_options(1808823, requested, ram);
    CHECK(explicit_budget.timeout_ms == 1000);
    CHECK(explicit_budget.memory_limit_mb == 512);
    CHECK(explicit_budget.is_canceled());
    explicit_budget.on_tick();
    CHECK(ticks == 1);
    requested.timeout_ms = 300001;
    CHECK_THROWS(model_reconstruction_process_options(12, requested, ram));
    requested.timeout_ms = 0;
    requested.memory_limit_mb = 4097;
    CHECK_THROWS(model_reconstruction_process_options(12, requested, ram));
    requested.memory_limit_mb = 127;
    CHECK_THROWS(model_reconstruction_process_options(12, requested, ram));
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
    SECTION("missing worker") { options.executable = "/nonexistent-reconstruction-worker"; }
    SECTION("pre-cancel")
    {
        options.process.is_canceled = [] { return true; };
    }
    SECTION("invalid parameters") { options.mesh.alpha_mm = -1.; }
    SECTION("unknown backend") { options.engine = "unknown"; }
    SECTION("voxel rejected by default backend") { options.voxel_mm = 0.08; }
    SECTION("meshlib requires voxel") { options.engine = "meshlib"; }
    SECTION("meshlib invalid voxel")
    {
        options.engine   = "meshlib";
        options.voxel_mm = std::numeric_limits<double>::infinity();
    }
    SECTION("meshlib float voxel underflow")
    {
        options.engine   = "meshlib";
        options.voxel_mm = 1e-100;
    }
    SECTION("meshlib pre-cancel")
    {
        options.engine              = "meshlib";
        options.voxel_mm            = 0.0803;
        options.process.is_canceled = [] { return true; };
    }
    SECTION("early unknown cancellation callback exception")
    {
        options.engine              = "meshlib";
        options.voxel_mm            = 0.0803;
        options.process.is_canceled = []() -> bool { throw 17; };
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
    options.engine   = "meshlib";
    options.voxel_mm = 0.0803;
    // An invalid Alpha Wrap worker must never be consulted as a fallback.
    options.executable = "/nonexistent-reconstruction-worker";
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
    options.engine                  = "meshlib";
    options.voxel_mm                = 0.0803;
    options.executable              = "/must-not-launch-a-reconstruction-worker";
    options.process.memory_limit_mb = 2048;
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
        CHECK(result.report["execution"]["process_id"] == reconstruction_process_id());
        CHECK(result.report["execution"]["hard_timeout"] == false);
        CHECK(result.report["execution"]["crash_isolation"] == false);
        CHECK(result.report["resource_limits"]["hard_memory_limit"] == false);
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
        options.process.is_canceled = [&] { return cancel; };
        options.process.on_tick     = [&] {
            ++ticks;
            cancel = true;
        };
        const auto result = reconstruct_model_candidate(model, 0, options);
        INFO(result.report.dump(2));
        REQUIRE(ticks > 0);
        CHECK(result.status == "canceled");
        CHECK_FALSE(result.candidate_model);
        CHECK(result.report["mesh_validated"] == false);
    }
    SECTION("expired cooperative budget discards all late output")
    {
        options.process.timeout_ms = 1;
        const auto result          = reconstruct_model_candidate(model, 0, options);
        INFO(result.report.dump(2));
        CHECK(result.status == "timed_out");
        CHECK_FALSE(result.candidate_model);
        CHECK(result.report["mesh_validated"] == false);
    }
    SECTION("caller callback exception is contained")
    {
        size_t ticks            = 0;
        options.process.on_tick = [&] {
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
        options.process.on_tick = [&] {
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
    options.process.is_canceled = {};
    options.process.on_tick     = {};
    options.process.timeout_ms  = 0;
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

TEST_CASE("Isolated reconstruction preserves placement and staged metadata", "[.][ReconstructionIntegration]")
{
    // Run explicitly with the freshly built application, never a cached binary.
    const char* executable = std::getenv("ORCA_RECONSTRUCTION_BINARY");
    REQUIRE(executable != nullptr);
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
    options.executable = executable;
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
