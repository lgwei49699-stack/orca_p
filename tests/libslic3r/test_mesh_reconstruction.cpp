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
void require_unchanged(const TriangleMesh& mesh, const indexed_triangle_set& saved)
{
    CHECK(mesh.its.vertices == saved.vertices);
    CHECK(mesh.its.indices == saved.indices);
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

TEST_CASE("Complete reconstruction checks retain 256 solids and reject an incomplete 257-solid check", "[MeshReconstruction][Inspection]")
{
    TriangleMesh mesh;
    for (int i = 0; i < 256; ++i) {
        auto part = make_cube(1., 1., 1.);
        part.translate(2.f * (i % 16), 2.f * (i / 16), 0.f);
        mesh.merge(part);
    }
    nlohmann::json report;
    REQUIRE(validate_reconstruction_mesh(mesh, report, 4000));
    CHECK(report["inspection"]["component_analysis"]["component_count"] == 256);
    CHECK(report["inspection"]["component_analysis"]["containment_complete"] == true);
    CHECK(report["inspection"]["component_analysis"]["orientation_complete"] == true);

    auto extra = make_cube(1., 1., 1.);
    extra.translate(0.f, 32.f, 0.f);
    mesh.merge(extra);
    const auto saved = mesh.its;
    REQUIRE_FALSE(validate_reconstruction_mesh(mesh, report, 4000));
    CHECK(report["inspection"]["component_analysis"]["component_count"] == 257);
    CHECK(report["inspection"]["component_analysis"]["limit_reached"] == true);
    CHECK(report["inspection"]["check_complete"] == false);
    CHECK(report["status"] == "candidate_rejected");
    require_unchanged(mesh, saved);
}

TEST_CASE("Reconstruction validation distinguishes intersections from other rejected geometry", "[MeshReconstruction]")
{
    auto mesh = make_cube(3., 3., 3.);
    nlohmann::json report;
    SECTION("Overlapping closed surfaces need intersection repair")
    {
        auto other = make_cube(3., 3., 3.);
        other.translate(1., 1., 1.);
        mesh.merge(other);
        REQUIRE_FALSE(validate_reconstruction_mesh(mesh, report, 100));
        CHECK(report.at("reason") == "self_intersection");
    }
    SECTION("Open surfaces do not trigger intersection repair")
    {
        mesh.its.indices.pop_back();
        REQUIRE_FALSE(validate_reconstruction_mesh(mesh, report, 100));
        CHECK(report.at("reason") != "self_intersection");
    }
    CHECK(report.at("status") == "candidate_rejected");
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
