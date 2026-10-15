#include <catch2/catch.hpp>

#include <libslic3r/MeshBoolean.hpp>
#include <libslic3r/MeshReconstruction.hpp>
#include <libslic3r/TriangleMesh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace Slic3r;

namespace {
TriangleMesh marker_mesh()
{
    auto marker = make_cube(1., 2., 3.);
    marker.translate(100.f, 200.f, 300.f);
    return marker;
}

void require_unchanged(const TriangleMesh& mesh, const indexed_triangle_set& saved)
{
    CHECK(mesh.its.vertices == saved.vertices);
    CHECK(mesh.its.indices == saved.indices);
}

void require_rejected(const TriangleMesh& source, const char* status, const MeshReconstructionOptions& options = {})
{
    TriangleMesh   candidate = marker_mesh();
    const auto     marker    = candidate.its;
    nlohmann::json report;
    REQUIRE_FALSE(reconstruct_mesh(source, candidate, report, options));
    INFO(report.dump());
    CHECK(report.at("status") == status);
    CHECK_FALSE(report.at("validation").at("complete").get<bool>());
    require_unchanged(candidate, marker);
}

TriangleMesh folded_tube()
{
    indexed_triangle_set tube;
    for (int ring = 0; ring < 4; ++ring) {
        const float x = float(ring);
        tube.vertices.emplace_back(x, 0.f, 0.f);
        tube.vertices.emplace_back(x, 1.f, 0.f);
        tube.vertices.emplace_back(x, 1.f, 1.f);
        tube.vertices.emplace_back(x, 0.f, 1.f);
    }
    tube.vertices[6] = Vec3f(0.5f, 0.5f, -0.2f);
    tube.indices     = {Vec3i32(0, 2, 1), Vec3i32(0, 3, 2)};
    for (int ring = 0; ring < 3; ++ring)
        for (int corner = 0; corner < 4; ++corner) {
            const int a = ring * 4 + corner, b = ring * 4 + (corner + 1) % 4;
            tube.indices.emplace_back(a, b, b + 4);
            tube.indices.emplace_back(a, b + 4, a + 4);
        }
    tube.indices.emplace_back(12, 13, 14);
    tube.indices.emplace_back(12, 14, 15);
    return TriangleMesh(std::move(tube));
}

TriangleMesh cube_with_small_opening()
{
    auto       mesh = make_cube(2., 2., 2.);
    auto       its  = mesh.its;
    const auto f    = its.indices.back();
    its.indices.pop_back();
    const Vec3f center = (its.vertices[f[0]] + its.vertices[f[1]] + its.vertices[f[2]]) / 3.f;
    const int   first  = int(its.vertices.size());
    for (int i = 0; i < 3; ++i)
        its.vertices.push_back(center + (its.vertices[f[i]] - center) * 0.05f);
    for (int i = 0; i < 3; ++i) {
        const int next = (i + 1) % 3;
        its.indices.emplace_back(f[i], f[next], first + next);
        its.indices.emplace_back(f[i], first + next, first + i);
    }
    return TriangleMesh(std::move(its));
}

TriangleMesh cube_with_short_edge()
{
    auto      its = make_cube(2., 2., 2.).its;
    const int a = its.indices.front()[0], b = its.indices.front()[1];
    const int inserted = int(its.vertices.size());
    its.vertices.push_back(its.vertices[a] + 1e-6f * (its.vertices[b] - its.vertices[a]));
    auto original_faces = std::move(its.indices);
    its.indices.clear();
    for (const auto& f : original_faces) {
        bool split = false;
        for (int i = 0; i < 3; ++i) {
            const int u = f[i], v = f[(i + 1) % 3], w = f[(i + 2) % 3];
            if ((u == a && v == b) || (u == b && v == a)) {
                its.indices.emplace_back(u, inserted, w);
                its.indices.emplace_back(inserted, v, w);
                split = true;
                break;
            }
        }
        if (!split)
            its.indices.push_back(f);
    }
    return TriangleMesh(std::move(its));
}

TriangleMesh square_tube()
{
    indexed_triangle_set       its;
    const std::array<Vec2f, 8> ring{Vec2f(0.f, 0.f), Vec2f(4.f, 0.f), Vec2f(4.f, 4.f), Vec2f(0.f, 4.f),
                                    Vec2f(1.f, 1.f), Vec2f(3.f, 1.f), Vec2f(3.f, 3.f), Vec2f(1.f, 3.f)};
    for (int z = 0; z < 2; ++z)
        for (const auto& p : ring)
            its.vertices.emplace_back(p.x(), p.y(), float(z));
    for (int j = 0; j < 4; ++j) {
        const int k = (j + 1) % 4;
        its.indices.emplace_back(j, k, k + 8);
        its.indices.emplace_back(j, k + 8, j + 8);
        its.indices.emplace_back(j + 4, k + 12, k + 4);
        its.indices.emplace_back(j + 4, j + 12, k + 12);
        its.indices.emplace_back(j, j + 4, k + 4);
        its.indices.emplace_back(j, k + 4, k);
        its.indices.emplace_back(j + 8, k + 12, j + 12);
        its.indices.emplace_back(j + 8, k + 8, k + 12);
    }
    return TriangleMesh(std::move(its));
}
} // namespace

TEST_CASE("Explicit reconstruction produces a reviewed candidate for a healthy cube", "[MeshReconstruction]")
{
    const auto     source = make_cube(2., 2., 2.);
    const auto     saved  = source.its;
    TriangleMesh   candidate;
    nlohmann::json report;
    REQUIRE(reconstruct_mesh(source, candidate, report));
    INFO(report.dump());
    CHECK(report.at("status") == "candidate_ready");
    CHECK(report.at("validation").at("requires_manual_confirmation").get<bool>());
    CHECK(report.at("validation").at("complete").get<bool>());
    CHECK_FALSE(report.at("validation").at("reference_interior_check_complete").get<bool>());
    CHECK(report.at("validation").at("interior_check_status") == "not_preserved");
    CHECK_FALSE(report.at("validation").at("printability_verified").get<bool>());
    CHECK_FALSE(report.at("validation").at("design_intent_restored").get<bool>());
    CHECK_FALSE(report.at("metrics").at("distance_is_upper_bound").get<bool>());
    CHECK(report.at("metrics").at("sampled_source_to_candidate_mm").get<double>() > 0.);
    CHECK(report.at("metrics").at("sampled_candidate_to_source_mm").get<double>() <= 0.25);
    CHECK(report.at("final_mesh_check").at("inspection").at("mesh_status") == "passed");
    CHECK(candidate.its.indices.size() != source.its.indices.size());
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(candidate));
    require_unchanged(source, saved);
}

TEST_CASE("Exact coordinate soup fragments do not imply multiple solids", "[MeshReconstruction]")
{
    const auto           cube = make_cube(2., 2., 2.);
    indexed_triangle_set fragmented;
    for (const auto& f : cube.its.indices) {
        const int first = int(fragmented.vertices.size());
        for (int i = 0; i != 3; ++i)
            fragmented.vertices.push_back(cube.its.vertices[f[i]]);
        fragmented.indices.emplace_back(first, first + 1, first + 2);
    }
    const TriangleMesh source(std::move(fragmented));
    const auto         saved = source.its;
    TriangleMesh       candidate;
    nlohmann::json     report;
    REQUIRE(reconstruct_mesh(source, candidate, report));
    INFO(report.dump());
    CHECK(report.at("input").at("canonical_vertices") == 8);
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(candidate));
    require_unchanged(source, saved);
}

TEST_CASE("Closed self-intersections can produce an explicitly approximate reconstruction", "[MeshReconstruction]")
{
    const auto source = folded_tube();
    const auto saved  = source.its;
    REQUIRE(MeshBoolean::cgal::does_self_intersect(source));
    MeshReconstructionOptions options;
    // A warning threshold smaller than the offset must not veto valid geometry.
    options.max_deviation_mm = 0.001;
    TriangleMesh   candidate;
    nlohmann::json report;
    REQUIRE(reconstruct_mesh(source, candidate, report, options));
    INFO(report.dump());
    CHECK(report.at("validation").at("requires_manual_confirmation").get<bool>());
    CHECK(report.at("validation").at("geometry_complete").get<bool>());
    CHECK_FALSE(report.at("validation").at("reference_interior_check_complete").get<bool>());
    CHECK(report.at("validation").at("interior_check_status") == "not_preserved");
    CHECK(report.at("metrics").at("deviation_warning").get<bool>());
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(candidate));
    require_unchanged(source, saved);
}

TEST_CASE("Reconstruction accepts defective triangle soups without a reference solid", "[MeshReconstruction]")
{
    auto source = make_cube(2., 2., 2.);
    SECTION("Small missing patch") { source = cube_with_small_opening(); }
    SECTION("Missing cube side") { source.its.indices.erase(source.its.indices.begin(), source.its.indices.begin() + 2); }
    SECTION("Separate solids")
    {
        auto second = make_cube(1., 1., 1.);
        second.translate(4.f, 0.f, 0.f);
        source.merge(second);
    }
    SECTION("Nested cavity may be filled")
    {
        auto cavity = make_cube(1., 1., 1.);
        cavity.translate(0.5f, 0.5f, 0.5f);
        cavity.flip_triangles();
        source.merge(cavity);
    }
    SECTION("Intersecting shells")
    {
        auto second = make_cube(2., 2., 2.);
        second.translate(1.f, 0.f, 0.f);
        source.merge(second);
    }
    SECTION("Non-manifold shared edge")
    {
        auto second = make_cube(2., 2., 2.);
        second.translate(2.f, 2.f, 0.f);
        source.merge(second);
    }
    SECTION("Reversed shell") { source.flip_triangles(); }
    SECTION("Conflicting local orientation") { std::swap(source.its.indices.front()[0], source.its.indices.front()[1]); }
    SECTION("Duplicate face") { source.its.indices.push_back(source.its.indices.front()); }
    SECTION("Degenerate face") { source.its.indices.front()[0] = source.its.indices.front()[1]; }
    SECTION("Thin solid") { source = make_cube(2., 2., 0.1); }
    SECTION("An isolated triangle may become a thickened shell") { source.its.indices.resize(1); }
    SECTION("A valid handle is not an input defect") { source = square_tube(); }

    const auto     saved = source.its;
    TriangleMesh   candidate;
    nlohmann::json report;
    REQUIRE(reconstruct_mesh(source, candidate, report));
    INFO(report.dump());
    CHECK(report["status"] == "candidate_ready");
    const auto& inspection = report["final_mesh_check"]["inspection"];
    CHECK(inspection["check_complete"] == true);
    CHECK(inspection["mesh_status"] == "passed");
    REQUIRE(inspection["issues"].size() == 7);
    for (const auto& check : inspection["issues"]) {
        CHECK(check["complete"] == true);
        CHECK(check["count"] == 0);
    }
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(candidate));
    require_unchanged(source, saved);
}

TEST_CASE("Reconstruction records cleanup and excludes discarded vertices from bounds", "[MeshReconstruction]")
{
    auto       source = make_cube(2., 2., 2.);
    const auto face   = source.its.indices.front();
    source.its.indices.push_back(face);
    source.its.indices.emplace_back(face[2], face[1], face[0]);
    const int extra = int(source.its.vertices.size());
    source.its.vertices.emplace_back(100.f, 0.f, 0.f);
    source.its.indices.emplace_back(extra, extra, extra);
    TriangleMesh   candidate;
    nlohmann::json report;
    REQUIRE(reconstruct_mesh(source, candidate, report));
    CHECK(report["input"]["removed_duplicate_faces"] == 2);
    CHECK(report["input"]["removed_degenerate_faces"] == 1);
    CHECK(report["input"]["canonical_vertices"] == 8);
    CHECK(report["metrics"]["bbox_deviation_mm"].get<double>() < 0.1);
}

TEST_CASE("Open solid reconstruction closes a missing side instead of making a thin envelope", "[MeshReconstruction][BoundaryClosure]")
{
    auto source = make_cube(10., 10., 10.);
    source.its.indices.erase(source.its.indices.begin(), source.its.indices.begin() + 2);
    SECTION("Original winding") {}
    SECTION("Reversed winding") { source.flip_triangles(); }
    const auto     saved = source.its;
    TriangleMesh   closed, envelope;
    nlohmann::json report, envelope_report;
    REQUIRE(reconstruct_mesh(source, closed, report));
    INFO(report.dump());
    CHECK(report["boundary_closure"]["status"] == "closed_input_ready");
    CHECK(report["boundary_closure"]["closed_loops"] == 1);
    CHECK(report["boundary_closure"]["added_faces"] == 2);
    CHECK(report["boundary_closure"]["validation"]["inspection"]["mesh_status"] == "passed");
    CHECK_FALSE(report["metrics"]["open_surface_envelope_warning"].get<bool>());
    CHECK(report["metrics"]["output_volume_mm3"].get<double>() > 1000.);
    CHECK(report["metrics"]["output_volume_mm3"].get<double>() < 1100.);
    CHECK_FALSE(report["validation"]["printability_verified"].get<bool>());
    // Disabling closure retains the old surface-wrap semantics, with an explicit
    // warning instead of suggesting that seven clean checks imply useful paths.
    MeshReconstructionOptions options;
    options.close_open_boundaries = false;
    REQUIRE(reconstruct_mesh(source, envelope, envelope_report, options));
    CHECK(envelope_report["boundary_closure"]["status"] == "disabled");
    CHECK(envelope_report["metrics"]["open_surface_envelope_warning"].get<bool>());
    CHECK(envelope_report["metrics"]["output_volume_mm3"].get<double>() < 100.);
    require_unchanged(source, saved);
}

TEST_CASE("Boundary closure leaves closed geometry on the existing wrap path", "[MeshReconstruction][BoundaryClosure]")
{
    auto source = make_cube(2., 2., 2.);
    SECTION("Solid") {}
    SECTION("Handle with a real through hole") { source = square_tube(); }
    SECTION("Closed intersecting shells")
    {
        auto second = source;
        second.translate(1.f, 0.f, 0.f);
        source.merge(second);
    }
    TriangleMesh   a, b;
    nlohmann::json enabled, disabled;
    REQUIRE(reconstruct_mesh(source, a, enabled));
    MeshReconstructionOptions options;
    options.close_open_boundaries = false;
    REQUIRE(reconstruct_mesh(source, b, disabled, options));
    CHECK(enabled["boundary_closure"]["status"] == "no_open_edges");
    CHECK_FALSE(enabled["metrics"]["open_surface_envelope_warning"].get<bool>());
    require_unchanged(a, b.its);
}

TEST_CASE("Boundary closure refuses unsafe or over-budget patches without losing the source", "[MeshReconstruction][BoundaryClosure]")
{
    auto source = make_cube(2., 2., 2.);
    source.its.indices.erase(source.its.indices.begin(), source.its.indices.begin() + 2);
    std::string expected_status;
    SECTION("Single flat triangle cannot acquire a solid interior by capping")
    {
        source.its.indices.resize(1);
        expected_status = "closure_invalid";
    }
    SECTION("Open intersections are not patched")
    {
        auto second = source;
        second.translate(1.f, 0.f, 0.f);
        source.merge(second);
        expected_status = "closure_intersection";
    }
    SECTION("An oversized boundary skips patch search")
    {
        indexed_triangle_set cone;
        cone.vertices.emplace_back(0.f, 0.f, 3.f);
        constexpr int count = 513;
        for (int i = 0; i < count; ++i) {
            const double angle = i * 2. * std::acos(-1.) / count;
            cone.vertices.emplace_back(float(2. * std::cos(angle)), float(2. * std::sin(angle)), 0.f);
            cone.indices.emplace_back(0, i + 1, (i + 1) % count + 1);
        }
        source          = TriangleMesh(std::move(cone));
        expected_status = "closure_budget";
    }
    const auto     saved = source.its;
    TriangleMesh   candidate;
    nlohmann::json report;
    REQUIRE(reconstruct_mesh(source, candidate, report));
    INFO(report.dump());
    // The triangulator can refuse a planar duplicate cap before final validation.
    if (expected_status == "closure_invalid")
        CHECK((report["boundary_closure"]["status"] == expected_status || report["boundary_closure"]["status"] == "closure_patch_failed"));
    else
        CHECK(report["boundary_closure"]["status"] == expected_status);
    CHECK_FALSE(report["boundary_closure"]["used_for_wrapping"].get<bool>());
    CHECK(report["boundary_closure"]["added_faces"] == 0);
    CHECK(report["metrics"]["open_surface_envelope_warning"].get<bool>());
    CHECK(report["final_mesh_check"]["inspection"]["mesh_status"] == "passed");
    require_unchanged(source, saved);
}

TEST_CASE("Reconstruction retries bounded coarse profiles without weakening validation", "[MeshReconstruction]")
{
    const auto                source = make_cube(2., 2., 2.);
    MeshReconstructionOptions options;
    options.alpha_mm         = 0.1;
    options.offset_mm        = 0.02;
    options.max_output_faces = 1500;
    TriangleMesh   candidate;
    nlohmann::json report;
    const bool     ready = reconstruct_mesh(source, candidate, report, options);
    INFO(report.dump());
    REQUIRE(ready);
    REQUIRE(report["attempts"].size() >= 2);
    REQUIRE(report["attempts"].size() <= 3);
    CHECK(report["attempts"][0]["status"] == "resource_limit");
    CHECK(report["attempts"].back()["status"] == "candidate_ready");
    CHECK(report["final_mesh_check"]["inspection"]["check_complete"] == true);
    CHECK(report["final_mesh_check"]["inspection"]["mesh_status"] == "passed");
}

TEST_CASE("Reconstruction validates options and resource budgets without touching candidates", "[MeshReconstruction]")
{
    const auto                source = make_cube(2., 2., 2.);
    MeshReconstructionOptions options;
    SECTION("Zero alpha")
    {
        options.alpha_mm = 0;
        require_rejected(source, "invalid_options", options);
    }
    SECTION("Negative offset")
    {
        options.offset_mm = -0.01;
        require_rejected(source, "invalid_options", options);
    }
    SECTION("Nonfinite tolerance")
    {
        options.max_deviation_mm = std::numeric_limits<double>::quiet_NaN();
        require_rejected(source, "invalid_options", options);
    }
    SECTION("Offset exceeds alpha scale")
    {
        options.offset_mm = 0.2;
        require_rejected(source, "invalid_options", options);
    }
    SECTION("Input face limit")
    {
        options.max_input_faces = 4;
        require_rejected(source, "resource_limit", options);
    }
    SECTION("Output refinement limit")
    {
        options.max_output_faces = 4;
        require_rejected(source, "resource_limit", options);
    }
    SECTION("Tiny alpha cannot launch unbounded refinement")
    {
        options.alpha_mm  = 1e-9;
        options.offset_mm = 1e-10;
        require_rejected(source, "resource_limit", options);
    }
    SECTION("Callers cannot remove the supported budget")
    {
        options.max_output_faces = std::numeric_limits<std::size_t>::max();
        require_rejected(source, "invalid_options", options);
    }
    SECTION("Callers cannot exceed the supported input budget")
    {
        options.max_input_faces = RECONSTRUCTION_MAX_INPUT_FACES + 1;
        require_rejected(source, "invalid_options", options);
    }
}

TEST_CASE("Reconstruction rejects invalid data and protects aliased source", "[MeshReconstruction]")
{
    auto source = make_cube(2., 2., 2.);
    SECTION("Empty input") { require_rejected(TriangleMesh(), "invalid_input"); }
    SECTION("Invalid index")
    {
        source.its.indices.front()[0] = -1;
        require_rejected(source, "invalid_input");
    }
    SECTION("Nonfinite coordinate")
    {
        source.its.vertices.front().x() = std::numeric_limits<float>::infinity();
        require_rejected(source, "invalid_input");
        CHECK(std::isinf(source.its.vertices.front().x()));
    }
    SECTION("Only degenerate geometry cannot create a replacement solid")
    {
        for (auto& face : source.its.indices)
            face[0] = face[1];
        require_rejected(source, "invalid_input");
    }
    SECTION("Aliasing")
    {
        const auto     saved = source.its;
        nlohmann::json report;
        REQUIRE_FALSE(reconstruct_mesh(source, source, report));
        CHECK(report.at("status") == "invalid_input");
        require_unchanged(source, saved);
    }
}

TEST_CASE("Final reconstruction validation checks actual float geometry without reconstruction", "[MeshReconstruction]")
{
    auto mesh     = make_cube(2., 2., 2.);
    bool expected = false;
    SECTION("A valid single solid is accepted") { expected = true; }
    SECTION("An open mesh is rejected") { mesh.its.indices.pop_back(); }
    SECTION("A reversed shell is rejected") { mesh.flip_triangles(); }
    SECTION("A self-intersecting shell is rejected") { mesh = folded_tube(); }
    SECTION("A non-manifold duplicated face is rejected") { mesh.its.indices.push_back(mesh.its.indices.front()); }
    SECTION("A degenerate face is rejected") { mesh.its.indices.front()[0] = mesh.its.indices.front()[1]; }
    SECTION("Two closed solids are accepted")
    {
        auto other = make_cube(1., 1., 1.);
        other.translate(4.f, 0.f, 0.f);
        mesh.merge(other);
        expected = true;
    }
    SECTION("An otherwise valid genus-one tube is accepted")
    {
        mesh     = square_tube();
        expected = true;
        REQUIRE_FALSE(MeshBoolean::cgal::requires_repair(mesh));
    }
    SECTION("An inward second disconnected solid is rejected")
    {
        auto other = make_cube(1., 1., 1.);
        other.translate(4.f, 0.f, 0.f);
        other.flip_triangles();
        mesh.merge(other);
    }
    SECTION("A correctly oriented enclosed cavity is a valid boundary")
    {
        auto inner = make_cube(1., 1., 1.);
        inner.translate(0.5f, 0.5f, 0.5f);
        inner.flip_triangles();
        mesh.merge(inner);
        expected = true;
    }
    const auto     saved = mesh.its;
    nlohmann::json report;
    const bool     result = validate_reconstruction_mesh(mesh, report, 100);
    INFO(report.dump());
    REQUIRE(result == expected);
    CHECK(report.at("validation").at("complete").get<bool>() == expected);
    CHECK(report.at("validation").at("scope") == "float_surface_and_model_checks");
    CHECK_FALSE(report.at("validation").at("design_intent_restored").get<bool>());
    if (expected)
        CHECK(report.at("status") == "validated_mesh");
    require_unchanged(mesh, saved);
}

TEST_CASE("Boundary preparation uses the input budget, not the wrap output budget", "[MeshReconstruction][BoundaryClosure]")
{
    auto source = make_cube(2., 2., 2.);
    source.its.indices.pop_back();
    TriangleMesh candidate;
    MeshReconstructionOptions options;
    options.max_output_faces = 4;
    nlohmann::json report;
    REQUIRE_FALSE(reconstruct_mesh(source, candidate, report, options));
    INFO(report.dump());
    CHECK(report.at("boundary_closure").at("used_for_wrapping") == true);
    CHECK(report.at("boundary_closure").at("added_faces") == 1);
    CHECK(report.at("status") == "resource_limit");
    CHECK(candidate.empty());
}

TEST_CASE("Small transform roundtrip error cannot approve collapsed float triangles", "[MeshReconstruction]")
{
    const auto     original = cube_with_short_edge();
    nlohmann::json report;
    REQUIRE(validate_reconstruction_mesh(original, report, 100));
    auto local = original;
    local.translate(1000.f, 1000.f, 1000.f);
    REQUIRE_FALSE(validate_reconstruction_mesh(local, report, 100));
    CHECK(report.at("reason") == "unsupported_degenerate");
    auto roundtrip = local;
    roundtrip.translate(-1000.f, -1000.f, -1000.f);
    REQUIRE(roundtrip.its.vertices.size() == original.its.vertices.size());
    for (std::size_t i = 0; i < original.its.vertices.size(); ++i)
        CHECK((original.its.vertices[i] - roundtrip.its.vertices[i]).norm() <= 1e-4f);
    REQUIRE_FALSE(validate_reconstruction_mesh(roundtrip, report, 100));
    CHECK(report.at("reason") == "unsupported_degenerate");
}

TEST_CASE("Engine independent comparison is read only and retains warning semantics", "[MeshReconstruction]")
{
    const auto                      source    = make_cube(10., 10., 10.);
    auto                            candidate = source;
    const MeshReconstructionOptions options;
    auto                            exact = compare_reconstruction_meshes(source, candidate, options);
    CHECK(exact.at("sampled_source_to_candidate_mm").get<double>() == Approx(0.));
    CHECK(exact.at("sampled_candidate_to_source_mm").get<double>() == Approx(0.));
    CHECK_FALSE(exact.at("deviation_warning").get<bool>());
    CHECK_FALSE(exact.at("distance_is_upper_bound").get<bool>());
    candidate.translate(1., 0., 0.);
    auto shifted = compare_reconstruction_meshes(source, candidate, options);
    CHECK(shifted.at("bbox_deviation_mm").get<double>() == Approx(1.));
    CHECK(shifted.at("deviation_warning").get<bool>());
    CHECK(source.bounding_box().min.x() == Approx(0.));
    auto invalid             = options;
    invalid.max_deviation_mm = 0.;
    CHECK_THROWS(compare_reconstruction_meshes(source, candidate, invalid));
}

TEST_CASE("Final reconstruction validation respects the caller face budget", "[MeshReconstruction]")
{
    const auto     mesh  = make_cube(2., 2., 2.);
    const auto     saved = mesh.its;
    nlohmann::json report;
    REQUIRE_FALSE(validate_reconstruction_mesh(mesh, report, 4));
    CHECK(report.at("status") == "resource_limit");
    REQUIRE_FALSE(validate_reconstruction_mesh(mesh, report, std::numeric_limits<std::size_t>::max()));
    CHECK(report.at("status") == "invalid_options");
    require_unchanged(mesh, saved);
}
