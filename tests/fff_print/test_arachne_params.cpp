#include <catch2/catch.hpp>

#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Surface.hpp"

#include <iomanip>
#include <sstream>

using namespace Slic3r;

namespace {
class TestWallToolPaths : public Arachne::WallToolPaths
{
public:
    using WallToolPaths::WallToolPaths;
    using WallToolPaths::removeSmallLines;
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
