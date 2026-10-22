#include <catch2/catch.hpp>

#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Arachne/utils/PolylineStitcher.hpp"

#include <iomanip>
#include <sstream>

using namespace Slic3r;

TEST_CASE("Arachne retains a short island wall whose pieces share an endpoint", "[Arachne][ResidualIsland]")
{
    // Native section of a 0.433 mm2 island. The skeleton has two odd lines
    // meeting at one end. Gap-first stitching used to turn them into a tiny
    // out-and-back loop, which simplification collapsed to two equal points.
    const Polygon     island({Point(13814176, -1163888), Point(13812835, -1089816), Point(13838032, -925877),  Point(13831731, -820122),
                              Point(13817785, -782856),  Point(13810303, -635976),  Point(13847003, -562423),  Point(13845592, -394670),
                              Point(13862984, -300750),  Point(13861554, -287036),  Point(13804949, -172150),  Point(13763633, -109899),
                              Point(13758392, -107166),  Point(13733998, -98374),   Point(13680538, -150779),  Point(13510194, -137392),
                              Point(13422765, -228640),  Point(13392429, -310225),  Point(13365112, -425704),  Point(13357718, -686291),
                              Point(13406656, -902233),  Point(13431513, -935545),  Point(13499190, -1083633), Point(13678716, -1196097),
                              Point(13748538, -1198095)});
    PrintConfig       print;
    PrintObjectConfig object;
    object.min_bead_width.value                   = 85.;
    object.min_feature_size.value                 = 25.;
    object.min_length_factor.value                = 0.;
    object.wall_transition_angle.value            = 10.;
    object.wall_transition_filter_deviation.value = 25.;
    object.wall_transition_length.value           = 100.;
    object.wall_distribution_count.value          = 1;
    const double height                           = 0.12498163876916468;
    const Flow   outer(0.42, height, 0.4), inner(0.45, height, 0.4);
    auto         params = Arachne::make_paths_params(46, object, print);
    REQUIRE_FALSE(params.optimize_model_detail_paths);
    params.optimize_model_detail_paths = true;
    for (bool precise : {true, false}) {
        const coord_t          delta   = outer.scaled_width() - outer.scaled_spacing();
        const Polygons         outline = offset(ExPolygon(island).simplify_p(scale_(0.01)), precise ? -delta : -delta / 2);
        Arachne::WallToolPaths paths(outline, outer.scaled_spacing(), inner.scaled_spacing(), 2, precise ? -delta / 2 : 0, height, params);
        const auto&            lines              = paths.getToolPaths();
        auto                   legacy_params      = params;
        legacy_params.optimize_model_detail_paths = false;
        Arachne::WallToolPaths legacy(outline, outer.scaled_spacing(), inner.scaled_spacing(), 2, precise ? -delta / 2 : 0, height,
                                      legacy_params);
        const auto&            legacy_lines = legacy.getToolPaths();
        if (precise) {
            // Off retains the original degenerate line; the optimization must
            // not be silently activated by normal slicing or reporting.
            for (const auto& group : legacy_lines)
                for (const auto& line : group)
                    CHECK(line.polylineLength() == 0);
        }
        CAPTURE(precise);
        REQUIRE(lines.size() == 1);
        REQUIRE(lines.front().size() == 1);
        const auto& line = lines.front().front();
        CHECK(line.is_odd);
        CHECK_FALSE(line.is_closed);
        if (precise) {
            CHECK(line.front().p != line.back().p);
        } else {
            // This legacy path is nonzero after simplification. The bounded
            // recovery must leave it byte-for-byte intact, not restitch it.
            REQUIRE(line.size() == 4);
            CHECK(line[0].p == Point(13606446, -499528));
            CHECK(line[1].p == Point(13611513, -828082));
            CHECK(line[2].p == Point(13597752, -534216));
            CHECK(line[3].p == line[0].p);
        }
        CHECK(unscale<double>(line.polylineLength()) > 0.1);
        for (const auto& junction : line)
            CHECK(unscale<double>(junction.w) >= 0.34);
        ExtrusionPaths output;
        extrusion_paths_append(output, line, erExternalPerimeter, outer);
        REQUIRE_FALSE(output.empty());
        for (const auto& path : output) {
            CHECK(path.polyline.is_valid());
            CHECK(path.length() > 0.);
            CHECK(path.width >= 0.34);
            CHECK(path.mm3_per_mm > 0.);
        }
        auto filtered_params              = params;
        filtered_params.min_length_factor = 100.f;
        Arachne::WallToolPaths filtered(outline, outer.scaled_spacing(), inner.scaled_spacing(), 2, precise ? -delta / 2 : 0, height,
                                        filtered_params);
        CHECK(filtered.getToolPaths().empty());
        Arachne::WallToolPaths no_walls(outline, outer.scaled_spacing(), inner.scaled_spacing(), 0, 0, height, params);
        CHECK(no_walls.getToolPaths().empty());
    }
}

namespace {
class TestWallToolPaths : public Arachne::WallToolPaths
{
public:
    using WallToolPaths::WallToolPaths;
    using WallToolPaths::removeSmallLines;
    using WallToolPaths::stitchToolPaths;
    using WallToolPaths::simplifyToolPaths;
};

void append_path_signature(const ExtrusionEntity& entity, std::ostringstream& out)
{
    if (const auto* collection = dynamic_cast<const ExtrusionEntityCollection*>(&entity)) {
        for (const auto* child : collection->entities)
            append_path_signature(*child, out);
    } else if (const auto* loop = dynamic_cast<const ExtrusionLoop*>(&entity)) {
        out << "loop{";
        for (const auto& path : loop->paths)
            append_path_signature(path, out);
        out << '}';
    } else if (const auto* multipath = dynamic_cast<const ExtrusionMultiPath*>(&entity)) {
        out << "multipath{";
        for (const auto& path : multipath->paths)
            append_path_signature(path, out);
        out << '}';
    } else if (const auto* path = dynamic_cast<const ExtrusionPath*>(&entity)) {
        REQUIRE(path->width > 0.);
        REQUIRE(std::isfinite(path->mm3_per_mm));
        out << path->role() << ':' << path->width << ':' << path->height << ':' << path->mm3_per_mm;
        for (const auto& point : path->polyline.points)
            out << ',' << point.x() << ',' << point.y();
        out << ';';
    } else {
        FAIL("Unexpected concentric extrusion entity");
    }
}
} // namespace

TEST_CASE("Thin-wall stitching follows coincident junctions before adding a gap", "[Arachne][ResidualIsland]")
{
    using namespace Arachne;
    auto segment = [](double from, double to, bool odd = true, coord_t width = scale_(0.4)) {
        ExtrusionLine line(0, odd);
        line.junctions.emplace_back(Point::new_scale(from, 0.), width, 0);
        line.junctions.emplace_back(Point::new_scale(to, 0.), width, 0);
        return line;
    };
    for (bool swap : {false, true}) {
        for (bool reverse_first : {false, true}) {
            for (bool reverse_second : {false, true}) {
                VariableWidthLines input{segment(0., -0.17), segment(0., 0.04)};
                if (reverse_first)
                    input.front().reverse();
                if (reverse_second)
                    input.back().reverse();
                if (swap)
                    std::swap(input.front(), input.back());
                std::vector<VariableWidthLines> groups{input};
                TestWallToolPaths::stitchToolPaths(groups, scale_(0.42), true);
                REQUIRE(groups.front().size() == 1);
                const auto& line = groups.front().front();
                CHECK_FALSE(line.is_closed);
                CHECK(unscale<double>(line.polylineLength()) == Approx(0.21).margin(1e-6));
                CHECK(std::min(line.front().p.x(), line.back().p.x()) == scaled<coord_t>(-0.17));
                CHECK(std::max(line.front().p.x(), line.back().p.x()) == scaled<coord_t>(0.04));
                for (const auto& junction : line)
                    CHECK(junction.w == scaled<coord_t>(0.4));
                TestWallToolPaths::simplifyToolPaths(groups);
                CHECK(unscale<double>(groups.front().front().polylineLength()) == Approx(0.21).margin(1e-6));
            }
        }
    }

    // Normal walls, zero-width contours and separated odd lines still use
    // the same legacy gap pass. Compare all junctions, not just line counts.
    auto signature = [](const VariableWidthLines& lines) {
        std::ostringstream out;
        for (const auto& line : lines) {
            out << line.inset_idx << ':' << line.is_odd << ':' << line.is_closed << ';';
            for (const auto& j : line)
                out << j.p.x() << ',' << j.p.y() << ',' << j.w << ';';
        }
        return out.str();
    };
    for (auto input :
         {VariableWidthLines{segment(0., 1., false), segment(1., 2., false)},
          VariableWidthLines{segment(0., 1., true, 0), segment(1., 2., true, 0)}, VariableWidthLines{segment(0., 1.), segment(1.02, 2.)}}) {
        VariableWidthLines expected, closed;
        PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::stitch(input, expected, closed, scale_(0.42) - 1);
        REQUIRE(closed.empty());
        std::vector<VariableWidthLines> groups{input};
        TestWallToolPaths::stitchToolPaths(groups, scale_(0.42), true);
        CHECK(signature(groups.front()) == signature(expected));
    }
}

TEST_CASE("Arachne parameters preserve explicit zero values and first-layer widths", "[Fill][Arachne][Parameters]")
{
    PrintConfig       print;
    PrintObjectConfig object;
    print.nozzle_diameter.values              = {0.4};
    object.initial_layer_min_bead_width.value = 90.;
    object.min_bead_width.value               = 75.;
    CHECK(Arachne::make_paths_params(0, object, print).min_bead_width == Approx(0.36));
    CHECK(Arachne::make_paths_params(1, object, print).min_bead_width == Approx(0.30));

    object.min_feature_size.value                 = 0.;
    object.min_length_factor.value                = 0.;
    object.initial_layer_min_bead_width.value     = 0.;
    object.min_bead_width.value                   = 0.;
    object.wall_transition_filter_deviation.value = 0.;
    object.wall_transition_length.value           = 0.;
    for (int layer : {0, 1, 20}) {
        const auto params = Arachne::make_paths_params(layer, object, print);
        CHECK(params.min_feature_size == 0.f);
        CHECK(params.min_length_factor == 0.f);
        CHECK(params.min_bead_width == 0.f);
        CHECK(params.wall_transition_filter_deviation == 0.f);
        CHECK(params.wall_transition_length == 0.f);
        CHECK_FALSE(params.is_top_or_bottom_layer);
    }
}

TEST_CASE("Arachne short-line threshold retains legacy fill and top-bottom protection", "[Fill][Arachne][Parameters]")
{
    // The existing shorterThan measure includes the closing distance. Keep
    // that convention unchanged and exercise both sides of its threshold.
    const Polygons outline;
    auto           remaining = [&](float factor, bool top_bottom, double length, bool odd = true, bool closed = false) {
        Arachne::WallToolPathsParams params{};
        params.min_length_factor      = factor;
        params.is_top_or_bottom_layer = top_bottom;
        TestWallToolPaths      paths(outline, scale_(0.4), scale_(0.4), 1, 0, 0.2, params);
        Arachne::ExtrusionLine line(0, odd);
        line.is_closed = closed;
        line.junctions.emplace_back(Point(0, 0), scale_(0.4), 0);
        line.junctions.emplace_back(Point::new_scale(length, 0.), scale_(0.4), 0);
        std::vector<Arachne::VariableWidthLines> lines{{line}};
        paths.removeSmallLines(lines);
        return lines.front().size();
    };
    Arachne::WallToolPathsParams fill_defaults;
    CHECK(fill_defaults.min_length_factor == 0.5f);
    CHECK_FALSE(fill_defaults.is_top_or_bottom_layer);
    CHECK(remaining(fill_defaults.min_length_factor, false, 0.099) == 0);
    CHECK(remaining(fill_defaults.min_length_factor, false, 0.100) == 1);
    CHECK(remaining(fill_defaults.min_length_factor, false, 0.101) == 1);
    CHECK(remaining(0.f, false, 0.01) == 1);
    CHECK(remaining(2.f, false, 0.3) == 0);
    CHECK(remaining(2.f, true, 0.3) == 1);
    CHECK(remaining(2.f, false, 0.01, false) == 1);
    CHECK(remaining(2.f, false, 0.01, true, true) == 1);
}

TEST_CASE("Both Arachne concentric fills are repeatable and independent of wall short-line settings", "[Fill][Arachne][Concentric]")
{
    PrintConfig       print;
    PrintObjectConfig object;
    PrintRegionConfig region;
    const ExPolygon   polygon(Polygon(
        {Point::new_scale(0, 0), Point::new_scale(8, 0), Point::new_scale(8, 1.1), Point::new_scale(3, 0.37), Point::new_scale(0, 0.8)}));
    const Surface     surface(stInternalSolid, polygon);
    for (const auto pattern : {ipConcentric, ipConcentricInternal}) {
        CAPTURE(pattern);
        std::string reference;
        for (double factor : {0., 0.5, 2., 25., 0.5}) {
            object.min_length_factor.value = factor;
            for (int repeat = 0; repeat < 3; ++repeat) {
                std::unique_ptr<Fill> filler(Fill::new_from_type(pattern));
                filler->print_config          = &print;
                filler->print_object_config   = &object;
                filler->no_overlap_expolygons = {polygon};
                filler->bounding_box          = polygon.contour.bounding_box();
                filler->angle                 = 0.;
                filler->loop_clipping         = 0;
                FillParams params;
                params.density        = 1.f;
                params.dont_adjust    = false;
                params.use_arachne    = true;
                params.layer_height   = 0.2;
                params.flow           = Flow(0.45f, 0.2f, 0.4f);
                params.extrusion_role = erSolidInfill;
                params.config         = &region;
                filler->spacing       = params.flow.spacing();
                ExtrusionEntityCollection output;
                filler->fill_surface_extrusion(&surface, params, output.entities);
                REQUIRE_FALSE(output.entities.empty());
                std::ostringstream signature;
                signature << std::setprecision(17);
                append_path_signature(output, signature);
                if (reference.empty())
                    reference = signature.str();
                CHECK(signature.str() == reference);
            }
        }
    }
}
