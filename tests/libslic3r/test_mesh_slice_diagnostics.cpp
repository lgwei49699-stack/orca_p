#include <catch2/catch.hpp>

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <utility>
#include <vector>

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

using namespace Slic3r;

namespace {

indexed_triangle_set box_without_face(int axis, float coordinate, double width = 10., double depth = 10.)
{
    indexed_triangle_set mesh = its_make_cube(width, depth, 10.);
    mesh.indices.erase(std::remove_if(mesh.indices.begin(), mesh.indices.end(),
                                      [&](const Vec3i32& face) {
                                          return mesh.vertices[face[0]][axis] == coordinate && mesh.vertices[face[1]][axis] == coordinate &&
                                                 mesh.vertices[face[2]][axis] == coordinate;
                                      }),
                       mesh.indices.end());
    return mesh;
}

// Parallel face slicing may change loop start points and loop order. Preserve
// orientation and every vertex while removing only those ordering differences.
using CanonicalLoop = std::vector<std::pair<coord_t, coord_t>>;
std::vector<CanonicalLoop> canonical_loops(const Polygons& polygons)
{
    std::vector<CanonicalLoop> result;
    for (const Polygon& polygon : polygons) {
        CanonicalLoop loop;
        for (const Point& point : polygon.points)
            loop.emplace_back(point.x(), point.y());
        if (!loop.empty())
            std::rotate(loop.begin(), std::min_element(loop.begin(), loop.end()), loop.end());
        result.emplace_back(std::move(loop));
    }
    std::sort(result.begin(), result.end());
    return result;
}

void require_same_slices(const std::vector<Polygons>& actual, const std::vector<Polygons>& expected)
{
    REQUIRE(actual.size() == expected.size());
    for (size_t layer = 0; layer < actual.size(); ++layer) {
        INFO("Layer " << layer);
        REQUIRE(canonical_loops(actual[layer]) == canonical_loops(expected[layer]));
    }
}

using CanonicalRegion = std::pair<CanonicalLoop, std::vector<CanonicalLoop>>;
std::vector<CanonicalRegion> canonical_regions(const ExPolygons& regions)
{
    std::vector<CanonicalRegion> result;
    for (const ExPolygon& region : regions)
        result.emplace_back(canonical_loops({region.contour}).front(), canonical_loops(region.holes));
    std::sort(result.begin(), result.end());
    return result;
}

void require_same_regions(const std::vector<ExPolygons>& actual, const std::vector<ExPolygons>& expected)
{
    REQUIRE(actual.size() == expected.size());
    for (size_t layer = 0; layer < actual.size(); ++layer) {
        INFO("Layer " << layer);
        REQUIRE(canonical_regions(actual[layer]) == canonical_regions(expected[layer]));
    }
}

double region_area_mm2(const ExPolygons& regions)
{
    double area_mm2 = 0.;
    for (const ExPolygon& region : regions)
        area_mm2 += region.area() * SCALING_FACTOR * SCALING_FACTOR;
    return area_mm2;
}

indexed_triangle_set nested_and_disjoint_boxes()
{
    indexed_triangle_set mesh = its_make_cube(10., 10., 10.);
    indexed_triangle_set hole = its_make_cube(4., 4., 10.);
    for (Vec3f& vertex : hole.vertices)
        vertex += Vec3f(3.f, 3.f, 0.f);
    for (Vec3i32& face : hole.indices)
        std::swap(face[0], face[1]);
    its_merge(mesh, hole);
    indexed_triangle_set separate = its_make_cube(2., 2., 10.);
    for (Vec3f& vertex : separate.vertices)
        vertex += Vec3f(15.f, 0.f, 0.f);
    its_merge(mesh, separate);
    return mesh;
}

void require_no_open_chains(const MeshSliceDiagnostic& diagnostic)
{
    REQUIRE(diagnostic.open_chains_before_gap_closing == 0);
    REQUIRE(diagnostic.remaining_open_chains == 0);
    REQUIRE(diagnostic.remaining_open_length_mm == 0.);
    REQUIRE_FALSE(diagnostic.gap_closing_used);
}

} // namespace

TEST_CASE("Mesh slice diagnostics preserve ordinary slicing and plane alignment", "[MeshSliceDiagnostics]")
{
    const indexed_triangle_set mesh = its_make_cube(10., 10., 10.);
    const std::vector<float>   zs{-1.f, 0.f, 0.2f, 5.f, 9.8f, 10.f, 11.f};
    MeshSlicingParams          params;
    const auto                 result = slice_mesh_with_diagnostics(mesh, zs, params);

    REQUIRE(result.diagnostics.size() == zs.size());
    require_same_slices(result.slices, slice_mesh(mesh, zs, params));
    REQUIRE(result.slices.front().empty());
    REQUIRE(result.slices.back().empty());
    REQUIRE_FALSE(result.slices[3].empty());
    for (const auto& diagnostic : result.diagnostics)
        require_no_open_chains(diagnostic);
}

TEST_CASE("Mesh slice diagnostics retain the open chains discarded by slicing", "[MeshSliceDiagnostics]")
{
    const indexed_triangle_set mesh              = box_without_face(0, 0.f);
    const auto                 original_indices  = mesh.indices;
    const auto                 original_vertices = mesh.vertices;
    const std::vector<float>   zs{0.2f, 5.f, 9.8f};
    const auto                 result = slice_mesh_with_diagnostics(mesh, zs, MeshSlicingParams{});

    REQUIRE(mesh.indices == original_indices);
    REQUIRE(mesh.vertices == original_vertices);
    REQUIRE(result.diagnostics.size() == zs.size());
    require_same_slices(result.slices, slice_mesh(mesh, zs, MeshSlicingParams{}));
    for (size_t layer = 0; layer < zs.size(); ++layer) {
        INFO("Layer " << layer);
        REQUIRE(result.slices[layer].empty());
        // This is the existing greedy chainer's intermediate count, not the
        // number of geometric boundary components. Depending on face order,
        // exact chaining can leave coincident-ended fragments that the gap
        // pass subsequently joins at zero distance. The invariant is that an
        // open chain was present and exactly the three intact sides survive.
        REQUIRE(result.diagnostics[layer].open_chains_before_gap_closing > 0);
        REQUIRE(result.diagnostics[layer].remaining_open_chains == 1);
        REQUIRE(result.diagnostics[layer].remaining_open_length_mm == Approx(30.));
        REQUIRE_FALSE(result.diagnostics[layer].gap_closing_used);
    }
}

TEST_CASE("Mesh slice diagnostics expose gaps hidden by normal loop closing", "[MeshSliceDiagnostics]")
{
    // The missing side is 1 mm across, below the slicer's existing 2 mm
    // heuristic. A closed output polygon must not hide that it was patched.
    const indexed_triangle_set mesh = box_without_face(0, 0.f, 10., 1.);
    const std::vector<float>   zs{0.2f, 5.f, 9.8f};
    const auto                 result = slice_mesh_with_diagnostics(mesh, zs, MeshSlicingParams{});

    require_same_slices(result.slices, slice_mesh(mesh, zs, MeshSlicingParams{}));
    for (size_t layer = 0; layer < zs.size(); ++layer) {
        REQUIRE_FALSE(result.slices[layer].empty());
        // Fragment count is seed/order dependent; actual nonzero patching and
        // the absence of residual chains are the geometric observations here.
        REQUIRE(result.diagnostics[layer].open_chains_before_gap_closing > 0);
        REQUIRE(result.diagnostics[layer].remaining_open_chains == 0);
        REQUIRE(result.diagnostics[layer].remaining_open_length_mm == 0.);
        REQUIRE(result.diagnostics[layer].gap_closing_used);
    }
}

TEST_CASE("Coincident unwelded vertices are not reported as nonzero gap closing", "[MeshSliceDiagnostics]")
{
    const indexed_triangle_set original = its_make_cube(10., 10., 10.);
    indexed_triangle_set       unwelded;
    for (const Vec3i32& face : original.indices) {
        const int first = int(unwelded.vertices.size());
        for (int corner = 0; corner < 3; ++corner)
            unwelded.vertices.push_back(original.vertices[face[corner]]);
        unwelded.indices.emplace_back(first, first + 1, first + 2);
    }
    const auto result = slice_mesh_with_diagnostics(unwelded, {5.f}, MeshSlicingParams{});

    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE_FALSE(result.slices.front().empty());
    REQUIRE(result.diagnostics.front().open_chains_before_gap_closing > 0);
    REQUIRE(result.diagnostics.front().remaining_open_chains == 0);
    REQUIRE(result.diagnostics.front().remaining_open_length_mm == 0.);
    REQUIRE_FALSE(result.diagnostics.front().gap_closing_used);
    require_same_slices(result.slices, slice_mesh(unwelded, std::vector<float>{5.f}, MeshSlicingParams{}));
}

TEST_CASE("Mesh slice diagnostics do not certify a missing horizontal cap", "[MeshSliceDiagnostics]")
{
    const indexed_triangle_set mesh = box_without_face(2, 10.f);
    REQUIRE(mesh.indices.size() == 10);
    const std::vector<float> zs{0.2f, 5.f, 9.8f};
    const auto               result = slice_mesh_with_diagnostics(mesh, zs, MeshSlicingParams{});

    // Interior horizontal sections cannot detect the missing top. Mesh
    // inspection is still required; nonempty sections are not print approval.
    require_same_slices(result.slices, slice_mesh(its_make_cube(10., 10., 10.), zs, MeshSlicingParams{}));
    for (const auto& diagnostic : result.diagnostics)
        require_no_open_chains(diagnostic);
}

TEST_CASE("Mesh slice diagnostic lengths follow the slicing transform", "[MeshSliceDiagnostics]")
{
    const indexed_triangle_set mesh = box_without_face(0, 0.f);
    MeshSlicingParams          params;
    params.trafo = Eigen::Translation3d(3., 4., 20.) * Eigen::Scaling(2., 2., 1.);
    const std::vector<float> zs{20.2f, 25.f, 29.8f};
    const auto               result = slice_mesh_with_diagnostics(mesh, zs, params);

    require_same_slices(result.slices, slice_mesh(mesh, zs, params));
    for (const auto& diagnostic : result.diagnostics) {
        REQUIRE(diagnostic.remaining_open_chains == 1);
        REQUIRE(diagnostic.remaining_open_length_mm == Approx(60.));
    }
}

TEST_CASE("Mesh slice diagnostics preserve modes and single-plane behavior", "[MeshSliceDiagnostics]")
{
    indexed_triangle_set mesh = its_make_cube(10., 10., 10.);
    for (Vec3i32& face : mesh.indices)
        std::swap(face[0], face[1]);
    for (const auto mode : {MeshSlicingParams::SlicingMode::Regular, MeshSlicingParams::SlicingMode::EvenOdd,
                            MeshSlicingParams::SlicingMode::Positive, MeshSlicingParams::SlicingMode::PositiveLargestContour}) {
        MeshSlicingParams params;
        params.mode       = mode;
        const auto result = slice_mesh_with_diagnostics(mesh, {5.f}, params);
        REQUIRE(result.diagnostics.size() == 1);
        require_no_open_chains(result.diagnostics.front());
        require_same_slices(result.slices, slice_mesh(mesh, std::vector<float>{5.f}, params));
        REQUIRE(canonical_loops(result.slices.front()) == canonical_loops(slice_mesh(mesh, 5.f, params)));
    }
}

TEST_CASE("Mesh slice diagnostics handle empty input and cancellation", "[MeshSliceDiagnostics]")
{
    const auto empty_planes = slice_mesh_with_diagnostics(its_make_cube(10., 10., 10.), {}, MeshSlicingParams{});
    REQUIRE(empty_planes.slices.empty());
    REQUIRE(empty_planes.diagnostics.empty());

    const auto empty_mesh = slice_mesh_with_diagnostics(indexed_triangle_set{}, {1.f, 2.f}, MeshSlicingParams{});
    REQUIRE(empty_mesh.slices.size() == 2);
    REQUIRE(empty_mesh.diagnostics.size() == 2);
    for (size_t layer = 0; layer < 2; ++layer) {
        REQUIRE(empty_mesh.slices[layer].empty());
        require_no_open_chains(empty_mesh.diagnostics[layer]);
    }

    struct Cancelled : std::runtime_error
    {
        Cancelled() : std::runtime_error("cancelled") {}
    };
    const auto mesh = its_make_cube(10., 10., 10.);
    REQUIRE_THROWS_AS(slice_mesh_with_diagnostics(mesh, {1.f}, MeshSlicingParams{}, [] { throw Cancelled{}; }), Cancelled);

    std::atomic<size_t> cancellation_checks{0};
    REQUIRE_THROWS_AS(slice_mesh_with_diagnostics(mesh, {1.f, 2.f}, MeshSlicingParams{},
                                                  [&] {
                                                      if (++cancellation_checks >= 2)
                                                          throw Cancelled{};
                                                  }),
                      Cancelled);
}

TEST_CASE("Mesh region diagnostics preserve fill modes and layer-specific mode selection", "[MeshSliceDiagnostics]")
{
    const auto                                        mesh = nested_and_disjoint_boxes();
    const std::vector<float>                          zs{0.2f, 3.f, 6.f, 9.8f};
    const std::vector<MeshSlicingParams::SlicingMode> modes{MeshSlicingParams::SlicingMode::Regular,
                                                            MeshSlicingParams::SlicingMode::EvenOdd,
                                                            MeshSlicingParams::SlicingMode::Positive,
                                                            MeshSlicingParams::SlicingMode::PositiveLargestContour};
    const std::vector<double>                         expected_areas{88., 88., 104., 100.};
    for (size_t mode = 0; mode < modes.size(); ++mode) {
        for (size_t below = 0; below < modes.size(); ++below) {
            INFO("Mode " << mode << ", lower mode " << below);
            MeshSlicingParamsEx params;
            params.mode                            = modes[mode];
            params.mode_below                      = modes[below];
            params.slicing_mode_normal_below_layer = 2;
            const auto result                      = slice_mesh_ex_with_diagnostics(mesh, zs, params);
            require_same_regions(result.slices, slice_mesh_ex(mesh, zs, params));
            REQUIRE(result.diagnostics.size() == zs.size());
            for (size_t layer = 0; layer < zs.size(); ++layer) {
                require_no_open_chains(result.diagnostics[layer]);
                REQUIRE(result.diagnostics[layer].closed_loops == 3);
                // Raw loop area includes the hole, independently of its winding.
                REQUIRE(result.diagnostics[layer].closed_loop_area_mm2 == Approx(120.));
                REQUIRE(region_area_mm2(result.slices[layer]) == Approx(expected_areas[layer < 2 ? below : mode]));
            }
        }
    }
}

TEST_CASE("Mesh region diagnostics preserve closing offsets simplification and transforms", "[MeshSliceDiagnostics]")
{
    const auto               mesh              = nested_and_disjoint_boxes();
    const auto               original_vertices = mesh.vertices;
    const auto               original_indices  = mesh.indices;
    const std::vector<float> zs{19.f, 20.2f, 25.f, 29.8f, 31.f};
    MeshSlicingParamsEx      params;
    params.trafo = Eigen::Translation3d(3., 4., 20.) * Eigen::Scaling(2., 2., 1.);
    for (const float closing : {0.f, 0.1f, 0.5f}) {
        for (const float offset : {-0.25f, 0.f, 0.15f}) {
            for (const double resolution : {0., 0.0025, 0.1}) {
                INFO("Closing " << closing << ", offset " << offset << ", resolution " << resolution);
                params.closing_radius = closing;
                params.extra_offset   = offset;
                params.resolution     = resolution;
                const auto result     = slice_mesh_ex_with_diagnostics(mesh, zs, params);
                require_same_regions(result.slices, slice_mesh_ex(mesh, zs, params));
                REQUIRE(result.diagnostics.size() == zs.size());
                REQUIRE(result.diagnostics.front().closed_loops == 0);
                REQUIRE(result.diagnostics.front().closed_loop_area_mm2 == 0.);
                REQUIRE(result.diagnostics.back().closed_loops == 0);
                REQUIRE(result.diagnostics[2].closed_loop_area_mm2 == Approx(480.));
            }
        }
    }
    REQUIRE(mesh.vertices == original_vertices);
    REQUIRE(mesh.indices == original_indices);
}

TEST_CASE("Mesh region diagnostics expose closed loops that cancel during region construction", "[MeshSliceDiagnostics]")
{
    // Independently indexed coincident shells, with opposite orientations:
    // chaining succeeds, but the ordinary NonZero fill rule cancels the loops.
    // This is different from a plane that never intersected a model surface.
    indexed_triangle_set mesh     = its_make_cube(10., 10., 10.);
    indexed_triangle_set reversed = mesh;
    for (Vec3i32& face : reversed.indices)
        std::swap(face[0], face[1]);
    its_merge(mesh, reversed);
    const std::vector<float> zs{0.2f, 5.f, 9.8f};
    const auto               result = slice_mesh_ex_with_diagnostics(mesh, zs, MeshSlicingParamsEx{});
    require_same_regions(result.slices, slice_mesh_ex(mesh, zs, MeshSlicingParamsEx{}));
    for (size_t layer = 0; layer < zs.size(); ++layer) {
        require_no_open_chains(result.diagnostics[layer]);
        REQUIRE(result.diagnostics[layer].closed_loops == 2);
        REQUIRE(result.diagnostics[layer].closed_loop_area_mm2 == Approx(200.));
        REQUIRE(result.slices[layer].empty());
    }

    MeshSlicingParamsEx positive;
    positive.mode     = MeshSlicingParams::SlicingMode::Positive;
    const auto filled = slice_mesh_ex_with_diagnostics(mesh, zs, positive);
    require_same_regions(filled.slices, slice_mesh_ex(mesh, zs, positive));
    REQUIRE(region_area_mm2(filled.slices[1]) == Approx(100.));
    REQUIRE(filled.diagnostics[1].closed_loop_area_mm2 == Approx(200.));
}

TEST_CASE("Mesh region diagnostics retain raw observations for gaps and deliberate shrinking", "[MeshSliceDiagnostics]")
{
    const std::vector<float> zs{0.2f, 5.f, 9.8f};
    for (const double depth : {1., 10.}) {
        const auto mesh   = box_without_face(0, 0.f, 10., depth);
        const auto result = slice_mesh_ex_with_diagnostics(mesh, zs, MeshSlicingParamsEx{});
        const auto raw    = slice_mesh_with_diagnostics(mesh, zs, MeshSlicingParams{});
        require_same_regions(result.slices, slice_mesh_ex(mesh, zs, MeshSlicingParamsEx{}));
        for (size_t layer = 0; layer < zs.size(); ++layer) {
            REQUIRE(result.diagnostics[layer].remaining_open_chains == raw.diagnostics[layer].remaining_open_chains);
            REQUIRE(result.diagnostics[layer].remaining_open_length_mm == Approx(raw.diagnostics[layer].remaining_open_length_mm));
            REQUIRE(result.diagnostics[layer].gap_closing_used == raw.diagnostics[layer].gap_closing_used);
            REQUIRE(result.diagnostics[layer].closed_loops == raw.diagnostics[layer].closed_loops);
            REQUIRE(result.diagnostics[layer].closed_loop_area_mm2 == Approx(raw.diagnostics[layer].closed_loop_area_mm2));
        }
    }

    // An intentional negative offset can also remove otherwise valid regions.
    // The observation records this faithfully; it does not diagnose a defect.
    MeshSlicingParamsEx shrink;
    shrink.extra_offset = -1.f;
    const auto thin     = its_make_cube(10., 0.5, 10.);
    const auto result   = slice_mesh_ex_with_diagnostics(thin, zs, shrink);
    require_same_regions(result.slices, slice_mesh_ex(thin, zs, shrink));
    REQUIRE(result.diagnostics[1].closed_loop_area_mm2 == Approx(5.));
    REQUIRE(result.slices[1].empty());
}

TEST_CASE("Mesh region diagnostics handle empty input and cancellation", "[MeshSliceDiagnostics]")
{
    const auto mesh         = its_make_cube(10., 10., 10.);
    const auto empty_planes = slice_mesh_ex_with_diagnostics(mesh, {}, MeshSlicingParamsEx{});
    REQUIRE(empty_planes.slices.empty());
    REQUIRE(empty_planes.diagnostics.empty());

    const auto empty_mesh = slice_mesh_ex_with_diagnostics(indexed_triangle_set{}, {1.f, 2.f}, MeshSlicingParamsEx{});
    REQUIRE(empty_mesh.slices.size() == 2);
    REQUIRE(empty_mesh.diagnostics.size() == 2);
    for (size_t layer = 0; layer < 2; ++layer) {
        REQUIRE(empty_mesh.slices[layer].empty());
        REQUIRE(empty_mesh.diagnostics[layer].closed_loops == 0);
        REQUIRE(empty_mesh.diagnostics[layer].closed_loop_area_mm2 == 0.);
        require_no_open_chains(empty_mesh.diagnostics[layer]);
    }

    struct Cancelled : std::runtime_error
    {
        Cancelled() : std::runtime_error("cancelled") {}
    };
    REQUIRE_THROWS_AS(slice_mesh_ex_with_diagnostics(mesh, {1.f}, MeshSlicingParamsEx{}, [] { throw Cancelled{}; }), Cancelled);
    std::atomic<size_t> cancellation_checks{0};
    REQUIRE_THROWS_AS(slice_mesh_ex_with_diagnostics(mesh, {1.f, 2.f}, MeshSlicingParamsEx{},
                                                     [&] {
                                                         if (++cancellation_checks >= 2)
                                                             throw Cancelled{};
                                                     }),
                      Cancelled);
}
