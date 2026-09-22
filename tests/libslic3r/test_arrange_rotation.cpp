#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>
#include <locale>

#include <boost/nowide/fstream.hpp>

#include "libslic3r/Arrange.hpp"
#include "libslic3r/ClipperUtils.hpp"

using namespace Slic3r;
using namespace Slic3r::arrangement;

namespace {

ArrangePolygons rotation_fixture()
{
    boost::nowide::ifstream input(std::string(TEST_DATA_DIR) + "/arrange/stable_rotation.polygons");
    input.imbue(std::locale::classic());
    REQUIRE(input.is_open());
    size_t count = 0;
    REQUIRE(bool(input >> count));
    REQUIRE(count == 7);
    ArrangePolygons items(count);
    for (size_t i = 0; i < count; ++i) {
        ArrangePolygon& item     = items[i];
        size_t          vertices = 0;
        REQUIRE(bool(input >> item.name >> vertices));
        REQUIRE(vertices >= 3);
        for (size_t j = 0; j < vertices; ++j) {
            double x = 0.0, y = 0.0;
            REQUIRE(bool(input >> x >> y));
            item.poly.contour.points.emplace_back(scaled(x), scaled(y));
        }
        item.itemid      = int(i);
        item.bed_idx     = 0;
        item.extrude_ids = {1};
        item.height      = 5.0;
        item.inflation   = scaled(1.0);
        item.translation = Point::new_scale(50.0, 50.0);
    }
    return items;
}

ArrangeParams rotation_params()
{
    ArrangeParams params;
    params.allow_rotations = true;
    params.parallel        = false;
    params.progressind     = {};
    return params;
}

void require_same_layout(const ArrangePolygons& actual, const ArrangePolygons& expected)
{
    REQUIRE(actual.size() == expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        INFO("object: " << actual[i].name);
        REQUIRE(actual[i].bed_idx == expected[i].bed_idx);
        REQUIRE(actual[i].itemid == expected[i].itemid);
        REQUIRE(std::abs(std::remainder(actual[i].rotation - expected[i].rotation, 2.0 * PI)) < 1e-7);
        REQUIRE((actual[i].translation - expected[i].translation).cast<double>().norm() <= scaled(0.001));
    }
}

void require_safe_layout(const ArrangePolygons& items, const BoundingBox& bed)
{
    // Use almost the full requested inflation to tolerate integer-grid touching.
    std::vector<ExPolygons> footprints;
    for (const ArrangePolygon& item : items) {
        REQUIRE(item.bed_idx >= 0);
        ExPolygons footprint = offset_ex(item.poly, float(item.inflation - scaled(0.001)), ClipperLib::jtSquare);
        REQUIRE_FALSE(footprint.empty());
        for (ExPolygon& polygon : footprint) {
            polygon.rotate(item.rotation);
            polygon.translate(item.translation.x(), item.translation.y());
            const BoundingBox bb = polygon.contour.bounding_box();
            REQUIRE(bb.min.x() >= bed.min.x());
            REQUIRE(bb.min.y() >= bed.min.y());
            REQUIRE(bb.max.x() <= bed.max.x());
            REQUIRE(bb.max.y() <= bed.max.y());
        }
        footprints.push_back(std::move(footprint));
    }
    for (size_t i = 0; i < items.size(); ++i)
        for (size_t j = i + 1; j < items.size(); ++j)
            if (items[i].bed_idx == items[j].bed_idx)
                REQUIRE(intersection(footprints[i], footprints[j]).empty());
}

} // namespace

TEST_CASE("Stable arrange seeds do not depend on contour traversal", "[Arrange][Rotation][fast]")
{
    for (const ArrangePolygon& item : rotation_fixture()) {
        const auto expected = stable_arrange_seed_rotation(item.poly);
        REQUIRE(expected.has_value());
        REQUIRE(*expected >= 0.0);
        REQUIRE(*expected < PI / 2.0);
        for (size_t start = 0; start < item.poly.contour.size(); ++start) {
            ExPolygon copy = item.poly;
            std::rotate(copy.contour.points.begin(), copy.contour.points.begin() + start, copy.contour.points.end());
            for (int winding = 0; winding < 2; ++winding) {
                copy.contour.reverse();
                const auto actual = stable_arrange_seed_rotation(copy);
                REQUIRE(actual.has_value());
                REQUIRE(*actual == Approx(*expected).margin(1e-12));
            }
        }
    }
}

TEST_CASE("Stable arrange seeds retain non-axis-aligned geometry", "[Arrange][Rotation][fast]")
{
    ExPolygon  footprint{Polygon::new_scale({{-20.0, -10.0}, {20.0, -10.0}, {20.0, 10.0}, {-20.0, 10.0}})};
    const auto axis_seed = stable_arrange_seed_rotation(footprint);
    REQUIRE(axis_seed.has_value());
    REQUIRE(*axis_seed == Approx(0.0).margin(1e-12));
    footprint.rotate(23.0 * PI / 180.0);
    const ExPolygon before      = footprint;
    const auto      tilted_seed = stable_arrange_seed_rotation(footprint);
    REQUIRE(tilted_seed.has_value());
    REQUIRE(*tilted_seed == Approx(67.0 * PI / 180.0).margin(1e-6));
    REQUIRE(footprint.contour == before.contour);
}

TEST_CASE("Stable arrange seed handles degenerate geometry", "[Arrange][Rotation][fast]")
{
    REQUIRE_FALSE(stable_arrange_seed_rotation(ExPolygon{}).has_value());
    ExPolygon line{Polygon::new_scale({{0.0, 0.0}, {10.0, 0.0}, {20.0, 0.0}})};
    REQUIRE_FALSE(stable_arrange_seed_rotation(line).has_value());
}

TEST_CASE("Repeated layered arrangement has a stable rotation search", "[Arrange][Rotation]")
{
    const BoundingBox     bed{Point::new_scale(1.0, 1.0), Point::new_scale(100.0, 100.0)};
    const ArrangePolygons original = rotation_fixture();
    ArrangeParams         params   = rotation_params();
    ArrangePolygons       expected = original;
    arrange(expected, bed, params);
    require_safe_layout(expected, bed);

    ArrangePolygons items = original;
    // Includes rotations on both sides of the quadrant boundary from the GUI log.
    for (size_t i = 0; i < items.size(); ++i) {
        items[i].rotation    = (-90.0 + 45.0 * double(i)) * PI / 180.0;
        items[i].translation = Point::new_scale(130.0 + double(i), -20.0);
    }
    for (int run = 0; run < 10; ++run) {
        INFO("run: " << run);
        arrange(items, bed, params);
        require_same_layout(items, expected);
        require_safe_layout(items, bed);
        for (size_t i = 0; i < items.size(); ++i) {
            REQUIRE(items[i].poly.contour == original[i].poly.contour);
            REQUIRE(items[i].inflation == original[i].inflation);
        }
    }
}

TEST_CASE("Rotation disabled preserves incoming directions", "[Arrange][Rotation]")
{
    ArrangePolygons items = rotation_fixture();
    items.resize(1);
    items.front().rotation = 23.0 * PI / 180.0;
    ArrangeParams params   = rotation_params();
    params.allow_rotations = false;
    const BoundingBox bed{Point::new_scale(0.0, 0.0), Point::new_scale(200.0, 200.0)};
    arrange(items, bed, params);
    REQUIRE(items.front().bed_idx == 0);
    REQUIRE(items.front().rotation == Approx(23.0 * PI / 180.0));
}

TEST_CASE("Stable rotation retains fixed objects and rectangular bed fitting", "[Arrange][Rotation]")
{
    ArrangePolygons items = rotation_fixture();
    items.resize(1);
    items.front().poly.contour = Polygon::new_scale({{-35.0, -10.0}, {35.0, -10.0}, {35.0, 10.0}, {-35.0, 10.0}});
    ArrangePolygon fixed       = items.front();
    fixed.name                 = "fixed";
    fixed.poly.contour         = Polygon::new_scale({{-2.0, -2.0}, {2.0, -2.0}, {2.0, 2.0}, {-2.0, 2.0}});
    fixed.rotation             = PI / 6.0;
    fixed.translation          = Point::new_scale(6.0, 6.0);
    const ArrangePolygons exclusions{fixed};
    const BoundingBox     bed{Point::new_scale(0.0, 0.0), Point::new_scale(40.0, 100.0)};
    ArrangeParams         params = rotation_params();
    arrange(items, exclusions, bed, params);
    REQUIRE(items.front().bed_idx == 0);
    REQUIRE(exclusions.front().rotation == fixed.rotation);
    REQUIRE(exclusions.front().translation == fixed.translation);
    REQUIRE(exclusions.front().poly.contour == fixed.poly.contour);
    items.push_back(exclusions.front());
    require_safe_layout(items, bed);
}
