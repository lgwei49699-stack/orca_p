#include <catch2/catch.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <cmath>

using namespace Slic3r;

namespace {

void check_hull_vertices(const Polygon& hull, Points expected)
{
    REQUIRE(hull.points.size() == expected.size());
    Points     actual = hull.points;
    const auto less = [](const Point& a, const Point& b) {
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    };
    // The cyclic starting vertex is not part of the projection contract.
    std::sort(actual.begin(), actual.end(), less);
    std::sort(expected.begin(), expected.end(), less);
    // The clipping API transforms vertices as floats. Allow 0.001 mm (1 um),
    // expressed through the current coordinate scale instead of raw units.
    const coord_t tolerance = std::max<coord_t>(1, scaled<coord_t>(0.001));
    for (size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i, actual[i].x(), actual[i].y(), expected[i].x(), expected[i].y());
        CHECK(std::abs(actual[i].x() - expected[i].x()) <= tolerance);
        CHECK(std::abs(actual[i].y() - expected[i].y()) <= tolerance);
    }
}

} // namespace

// Follow upstream #14892: test ModelObject's projection independently of 3MF.
TEST_CASE("Model convex hull projects a part with scaling and offset", "[Model][ConvexHull]")
{
    Model        model;
    ModelObject* object = model.add_object();
    // Preserve the cube's raw [0,20] coordinates instead of recentering it.
    object->add_volume(make_cube(20., 20., 20.), ModelVolumeType::MODEL_PART, false);
    Geometry::Transformation transform;

    SECTION("identity keeps the 20 mm square")
    {
        check_hull_vertices(object->convex_hull_2d(transform.get_matrix()),
                            {{scaled(0.), scaled(0.)}, {scaled(20.), scaled(0.)},
                             {scaled(20.), scaled(20.)}, {scaled(0.), scaled(20.)}});
    }

    SECTION("scaling and offset change the projected footprint")
    {
        transform.set_scaling_factor({2., 2., 2.});
        transform.set_offset({10., 5., 0.});
        check_hull_vertices(object->convex_hull_2d(transform.get_matrix()),
                            {{scaled(10.), scaled(5.)}, {scaled(50.), scaled(5.)},
                             {scaled(50.), scaled(45.)}, {scaled(10.), scaled(45.)}});
    }
}

TEST_CASE("Sinking model projections distinguish the full hull from the above-bed hull", "[Model][ConvexHull]")
{
    // Closed tetrahedron: x,y,z >= 0 and x+y+z <= 20. Its full XY projection
    // has 20 mm legs. At local height h, the above-plane projection has legs
    // 20-h. These expectations come from the fixture, not captured output.
    indexed_triangle_set tetrahedron;
    tetrahedron.vertices = {{0.f, 0.f, 0.f}, {20.f, 0.f, 0.f}, {0.f, 20.f, 0.f}, {0.f, 0.f, 20.f}};
    tetrahedron.indices  = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
    Model        model;
    ModelObject* object = model.add_object();
    object->add_volume(TriangleMesh(tetrahedron), ModelVolumeType::MODEL_PART, false);

    const double z_offset = GENERATE(5., 0., -10., -15., -25.);
    CAPTURE(z_offset);
    Geometry::Transformation transform;
    transform.set_offset({0., 0., z_offset});

    // Orca's public hull includes the whole model, even below the bed.
    check_hull_vertices(object->convex_hull_2d(transform.get_matrix()),
                        {{scaled(0.), scaled(0.)}, {scaled(20.), scaled(0.)}, {scaled(0.), scaled(20.)}});

    // Explicit clipping is a separate API. Do not use its reference hull to
    // assert the public ModelObject method's different behavior.
    const Transform3f clipped_transform = transform.get_matrix().cast<float>();
    const Polygon     clipped           = its_convex_hull_2d_above(tetrahedron, clipped_transform, 0.f);
    if (z_offset < -20.) {
        CHECK(clipped.points.empty());
    } else {
        const double leg = std::min(20., 20. + z_offset);
        check_hull_vertices(clipped, {{scaled(0.), scaled(0.)}, {scaled(leg), scaled(0.)}, {scaled(0.), scaled(leg)}});
    }
}
