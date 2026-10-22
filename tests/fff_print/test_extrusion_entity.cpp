#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/VariableWidth.hpp"
#include "libslic3r/libslic3r.h"

#include "test_data.hpp"

using namespace Slic3r;

TEST_CASE("Variable-width groups retain both endpoints after a width reversal", "[ExtrusionEntity][VariableWidth][WidthGrouping]")
{
    const float         height = GENERATE(0.08f, 0.24f, 0.32f);
    const ExtrusionRole role   = GENERATE(erGapFill, erExternalPerimeter, erSolidInfill);
    ThickPolyline       input;
    input.points = {Point(0, 0), Point(2000000, 0), Point(4000000, 0), Point(6000000, 0)};
    SECTION("new group narrows then widens") { input.width = {200000., 180000., 180000., 140000., 140000., 220000.}; }
    SECTION("new group widens then narrows") { input.width = {140000., 160000., 160000., 200000., 200000., 120000.}; }
    SECTION("shared vertex has discontinuous widths") { input.width = {200000., 190000., 260000., 200000., 200000., 220000.}; }
    SECTION("exactly at the grouping tolerance") { input.width = {200000., 175000., 175000., 150000., 150000., 175000.}; }
    SECTION("constant widths stay grouped") { input.width = {200000., 200000., 200000., 200000., 200000., 200000.}; }
    const Flow flow(0.45f, height, 0.4f);
    for (bool reverse : {false, true}) {
        if (reverse)
            input.reverse();
        CAPTURE(height, int(role), reverse, input.width);
        ExtrusionEntityCollection output;
        variable_width({input}, role, flow, output.entities, true);
        REQUIRE_FALSE(output.empty());
        CHECK(output.first_point() == input.first_point());
        CHECK(output.last_point() == input.last_point());
        Point  previous              = input.first_point();
        double total_length          = 0.;
        double expected_total_volume = 0.;
        for (const auto& line : input.thicklines())
            expected_total_volume += unscale<double>(line.length()) * unscale<double>(0.5 * (line.a_width + line.b_width)) * height;
        CHECK(output.total_volume() == Approx(expected_total_volume).epsilon(1e-5));
        for (const ExtrusionEntity* entity : output.entities) {
            const auto* path = dynamic_cast<const ExtrusionPath*>(entity);
            REQUIRE(path != nullptr);
            CHECK(path->first_point() == previous);
            previous = path->last_point();
            CHECK(path->role() == role);
            CHECK(path->height == height);
            const double lo_x            = std::min(path->first_point().x(), path->last_point().x());
            const double hi_x            = std::max(path->first_point().x(), path->last_point().x());
            double       min_width       = std::numeric_limits<double>::max();
            double       max_width       = 0.;
            double       expected_volume = 0.;
            for (const auto& line : input.thicklines()) {
                const double a = std::max(lo_x, double(std::min(line.a.x(), line.b.x())));
                const double b = std::min(hi_x, double(std::max(line.a.x(), line.b.x())));
                if (b <= a)
                    continue;
                const auto width_at = [&](double x) {
                    return line.a_width + (line.b_width - line.a_width) * (x - line.a.x()) / double(line.b.x() - line.a.x());
                };
                const double wa = width_at(a);
                const double wb = width_at(b);
                min_width       = std::min({min_width, wa, wb});
                max_width       = std::max({max_width, wa, wb});
                expected_volume += unscale<double>(b - a) * unscale<double>(0.5 * (wa + wb)) * height;
            }
            // One constant-flow group may only cover the existing 0.05 mm
            // endpoint-width range, including new groups and shared vertices.
            CHECK(max_width - min_width <= 50000. + 1e-6);
            CHECK(path->total_volume() == Approx(expected_volume).epsilon(1e-5));
            total_length += unscale<double>(path->length());
        }
        CHECK(total_length == Approx(6.));
        if (*std::max_element(input.width.begin(), input.width.end()) - *std::min_element(input.width.begin(), input.width.end()) <= 50000.)
            CHECK(output.entities.size() == 1);
    }
}

TEST_CASE("Variable-width grouping preserves bent centerlines before simplification", "[ExtrusionEntity][VariableWidth][WidthGrouping]")
{
    ThickPolyline input;
    input.points = {Point(0, 0), Point(2000000, 1000000), Point(4000000, 1010000), Point(6000000, 0)};
    SECTION("continuous width reversal") { input.width = {200000., 180000., 180000., 140000., 140000., 220000.}; }
    SECTION("discontinuous shared width") { input.width = {200000., 190000., 260000., 200000., 200000., 220000.}; }
    const Flow flow(0.45f, 0.24f, 0.4f);
    for (bool reverse : {false, true}) {
        if (reverse)
            input.reverse();
        CAPTURE(reverse, input.width);
        const ThickLines          source = input.thicklines();
        ExtrusionEntityCollection output;
        variable_width({input}, erSolidInfill, flow, output.entities, true);
        REQUIRE_FALSE(output.empty());
        Points points;
        Point  previous = input.first_point();
        double length   = 0.;
        for (const ExtrusionEntity* entity : output.entities) {
            const auto* path = dynamic_cast<const ExtrusionPath*>(entity);
            REQUIRE(path != nullptr);
            CHECK(path->first_point() == previous);
            previous = path->last_point();
            points.insert(points.end(), path->polyline.points.begin(), path->polyline.points.end());
            for (const Line& segment : path->polyline.lines()) {
                // A split can round an interpolated point by one integer unit
                // per axis, but may not shortcut a bend in the source path.
                CHECK(std::any_of(source.begin(), source.end(), [&](const ThickLine& line) {
                    return line.distance_to(segment.a) <= std::sqrt(2.) && line.distance_to(segment.b) <= std::sqrt(2.);
                }));
            }
            length += unscale<double>(path->length());
        }
        CHECK(previous == input.last_point());
        for (const Point& point : input.points)
            CHECK(std::find(points.begin(), points.end(), point) != points.end());
        double expected_length = 0.;
        double expected_volume = 0.;
        for (const ThickLine& line : source) {
            expected_length += unscale<double>(line.length());
            expected_volume += unscale<double>(line.length()) * unscale<double>(0.5 * (line.a_width + line.b_width)) * flow.height();
        }
        CHECK(length == Approx(expected_length).margin(1e-5));
        CHECK(output.total_volume() == Approx(expected_volume).epsilon(1e-5));
    }
}

TEST_CASE("Variable-width extrusion preserves the integrated width in either direction", "[ExtrusionEntity][VariableWidth]")
{
    const float         height = GENERATE(0.08f, 0.24f, 0.32f);
    const ExtrusionRole role   = GENERATE(erGapFill, erExternalPerimeter, erSolidInfill);
    ThickPolyline       input;
    input.points = {Point(0, 0), Point(scaled<coord_t>(3.), 0), Point(scaled<coord_t>(10.), 0)};
    std::vector<double> widths;
    SECTION("merged increasing widths") { widths = {0.10, 0.12, 0.12, 0.14}; }
    SECTION("split increasing widths") { widths = {0.10, 0.14, 0.14, 0.40}; }
    SECTION("constant widths") { widths = {0.40, 0.40, 0.40, 0.40}; }
    SECTION("narrow positive widths") { widths = {0.02, 0.03, 0.03, 0.04}; }
    for (double width : widths)
        input.width.push_back(scaled<double>(width));
    const double expected_volume = height * (3. * 0.5 * (widths[0] + widths[1]) + 7. * 0.5 * (widths[2] + widths[3]));
    const Flow   flow(0.45f, height, 0.4f);
    for (bool reverse : {false, true}) {
        if (reverse)
            input.reverse();
        CAPTURE(height, widths, reverse, role);
        ExtrusionEntityCollection output;
        variable_width({input}, role, flow, output.entities, true);
        REQUIRE_FALSE(output.empty());
        CHECK(output.first_point() == input.first_point());
        CHECK(output.last_point() == input.last_point());
        CHECK(output.total_volume() == Approx(expected_volume).epsilon(1e-5));
        double total_length = 0.;
        for (const ExtrusionEntity* entity : output.entities) {
            const auto* path = dynamic_cast<const ExtrusionPath*>(entity);
            REQUIRE(path != nullptr);
            CHECK(path->role() == role);
            CHECK(path->height == height);
            CHECK(path->mm3_per_mm > 0.);
            total_length += unscale<double>(path->length());
        }
        CHECK(total_length == Approx(10.).margin(1e-6));
    }
}

TEST_CASE("Variable-width closed extrusion preserves volume and loop identity", "[ExtrusionEntity][VariableWidth]")
{
    ThickPolyline input;
    input.points = {Point(0, 0), Point(scaled<coord_t>(2.), 0), Point(scaled<coord_t>(2.), scaled<coord_t>(2.)), Point(0, 0)};
    input.width  = {100000., 120000., 120000., 140000., 140000., 100000.};
    const Flow   flow(0.45f, 0.2f, 0.4f);
    const double expected_volume = 0.2 * (2. * 0.11 + 2. * 0.13 + std::sqrt(8.) * 0.12);
    for (bool reverse : {false, true}) {
        if (reverse)
            input.reverse();
        ExtrusionEntityCollection output;
        variable_width({input}, erGapFill, flow, output.entities, true);
        REQUIRE(output.entities.size() == 1);
        CHECK(dynamic_cast<const ExtrusionLoop*>(output.entities.front()) != nullptr);
        CHECK(output.total_volume() == Approx(expected_volume).epsilon(1e-5));
    }
}

TEST_CASE("Variable-width default retains legacy grouping and last-group volume", "[ExtrusionEntity][VariableWidth][DetailPathOptIn]")
{
    ThickPolyline input;
    input.points = {Point(0, 0), Point(2000000, 0), Point(4000000, 0), Point(6000000, 0)};
    input.width  = {200000., 180000., 180000., 140000., 140000., 220000.};
    const Flow                flow(0.45f, 0.24f, 0.4f);
    ExtrusionEntityCollection implicit_off, explicit_off, enabled;
    variable_width({input}, erGapFill, flow, implicit_off.entities);
    variable_width({input}, erGapFill, flow, explicit_off.entities, false);
    variable_width({input}, erGapFill, flow, enabled.entities, true);
    REQUIRE(implicit_off.entities.size() == 2);
    REQUIRE(explicit_off.entities.size() == implicit_off.entities.size());
    // Frozen original grouping: [0,2] then [2,4,6], with the last group
    // using its starting widths (0.18, 0.14), not their endpoint means.
    const Points expected[] = {{Point(0, 0), Point(2000000, 0)}, {Point(2000000, 0), Point(4000000, 0), Point(6000000, 0)}};
    for (size_t i = 0; i < implicit_off.entities.size(); ++i) {
        const auto* a = dynamic_cast<const ExtrusionPath*>(implicit_off.entities[i]);
        const auto* b = dynamic_cast<const ExtrusionPath*>(explicit_off.entities[i]);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(a->polyline.points == expected[i]);
        CHECK(a->polyline.points == b->polyline.points);
        CHECK(a->width == b->width);
        CHECK(a->mm3_per_mm == b->mm3_per_mm);
    }
    CHECK(implicit_off.total_volume() == Approx(0.24 * (2. * 0.19 + 4. * 0.16)).epsilon(1e-5));
    CHECK(enabled.total_volume() == Approx(0.24 * (2. * 0.19 + 2. * 0.16 + 2. * 0.18)).epsilon(1e-5));
    CHECK(enabled.entities.size() > implicit_off.entities.size());
}

static inline Slic3r::Point random_point(float LO = -50, float HI = 50)
{
    Vec2f pt = Vec2f(LO, LO) + (Vec2d(rand(), rand()) * (HI - LO) / RAND_MAX).cast<float>();
    return pt.cast<coord_t>();
}

// build a sample extrusion entity collection with random start and end points.
static Slic3r::ExtrusionPath random_path(size_t length = 20, float LO = -50, float HI = 50)
{
    ExtrusionPath t {erPerimeter, 1.0, 1.0, 1.0};
    for (size_t j = 0; j < length; ++ j)
        t.polyline.append(random_point(LO, HI));
    return t;
}

static Slic3r::ExtrusionPaths random_paths(size_t count = 10, size_t length = 20, float LO = -50, float HI = 50)
{
    Slic3r::ExtrusionPaths p;
    for (size_t i = 0; i < count; ++ i)
        p.push_back(random_path(length, LO, HI));
    return p;
}

SCENARIO("ExtrusionEntityCollection: Polygon flattening", "[ExtrusionEntity]") {
    srand(0xDEADBEEF); // consistent seed for test reproducibility.

    // Generate one specific random path set and save it for later comparison
    Slic3r::ExtrusionPaths nosort_path_set = random_paths();

    Slic3r::ExtrusionEntityCollection sub_nosort;
    sub_nosort.append(nosort_path_set);
    sub_nosort.no_sort = true;

    Slic3r::ExtrusionEntityCollection sub_sort;
    sub_sort.no_sort = false;
    sub_sort.append(random_paths());

    GIVEN("A Extrusion Entity Collection with a child that has one child that is marked as no-sort") {
        Slic3r::ExtrusionEntityCollection sample;
        Slic3r::ExtrusionEntityCollection output;

        sample.append(sub_sort);
        sample.append(sub_nosort);
        sample.append(sub_sort);

        WHEN("The EEC is flattened with default options (preserve_order=false)") {
			output = sample.flatten();
            THEN("The output EEC contains no Extrusion Entity Collections") {
                CHECK(std::count_if(output.entities.cbegin(), output.entities.cend(), [=](const ExtrusionEntity* e) {return e->is_collection();}) == 0);
            }
        }
        WHEN("The EEC is flattened with preservation (preserve_order=true)") {
			output = sample.flatten(true);
            THEN("The output EECs contains one EEC.") {
                CHECK(std::count_if(output.entities.cbegin(), output.entities.cend(), [=](const ExtrusionEntity* e) {return e->is_collection();}) == 1);
            }
            AND_THEN("The ordered EEC contains the same order of elements than the original") {
                // find the entity in the collection
                for (auto e : output.entities)
                    if (e->is_collection()) {
                        ExtrusionEntityCollection *temp = dynamic_cast<ExtrusionEntityCollection*>(e);
                        // check each Extrusion path against nosort_path_set to see if the first and last match the same
                        CHECK(nosort_path_set.size() == temp->entities.size());
                        for (size_t i = 0; i < nosort_path_set.size(); ++ i) {
                            CHECK(temp->entities[i]->first_point() == nosort_path_set[i].first_point());
                            CHECK(temp->entities[i]->last_point() == nosort_path_set[i].last_point());
                        }
                    }
            }
        }
    }
}
