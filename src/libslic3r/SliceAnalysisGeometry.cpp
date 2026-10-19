#include "SliceAnalysisGeometry.hpp"
#undef PI

#include <CGAL/Arr_curve_data_traits_2.h>
#include <CGAL/Arr_segment_traits_2.h>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Fraction_traits.h>
#include <CGAL/General_polygon_set_2.h>
#include <CGAL/Gps_traits_2.h>
#include <CGAL/number_utils.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Slic3r::SliceAnalysisGeometry {
namespace {

using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;

using Winding = boost::multiprecision::cpp_int;
struct AddWinding
{
    Winding operator()(const Winding& left, const Winding& right) const { return left + right; }
};
using WindingTraits = CGAL::Arr_curve_data_traits_2<CGAL::Arr_segment_traits_2<Kernel>, Winding, AddWinding>;
using NonzeroSet    = CGAL::General_polygon_set_2<CGAL::Gps_traits_2<WindingTraits>>;
using DirectedCurve = WindingTraits::X_monotone_curve_2;

Kernel::FT exact_coordinate(coord_t value)
{
    // Small integral chunks avoid both double conversion and backend-specific
    // long/long-long constructors (notably 32-bit long on Windows).
    const uint64_t magnitude = value < 0 ? uint64_t(-(value + 1)) + 1 : uint64_t(value);
    Kernel::FT     result(0);
    for (int shift = 48; shift >= 0; shift -= 16)
        result = result * 65536 + int((magnitude >> shift) & 0xffff);
    return value < 0 ? -result : result;
}

Rational rational(const Kernel::FT& value)
{
    using Fraction = CGAL::Fraction_traits<Kernel::FT>;
    Fraction::Numerator_type   numerator;
    Fraction::Denominator_type denominator;
    Fraction::Decompose{}(value, numerator, denominator);
    // Fraction_traits keeps this independent of CGAL's configured exact
    // backend. Decimal integer transfer is lossless; no floating conversion.
    std::ostringstream numerator_text, denominator_text;
    numerator_text.imbue(std::locale::classic());
    denominator_text.imbue(std::locale::classic());
    numerator_text << CGAL::exact(numerator);
    denominator_text << CGAL::exact(denominator);
    return Rational(boost::multiprecision::cpp_int(numerator_text.str())) /
           Rational(boost::multiprecision::cpp_int(denominator_text.str()));
}

struct RegionMeasure
{
    Rational                twice_area;
    std::array<Rational, 2> min, max;
};

template<class Face> std::optional<RegionMeasure> measure_face(Face face, const std::function<void()>& throw_if_canceled)
{
    if (face->is_unbounded() || face->number_of_outer_ccbs() != 1)
        return std::nullopt;
    const auto                outer = *face->outer_ccbs_begin();
    const auto&               first = outer->source()->point();
    std::array<Kernel::FT, 2> minimum{first.x(), first.y()}, maximum = minimum;
    Kernel::FT                twice_area(0);
    const auto                measure_boundary = [&](auto boundary, bool update_bounds) {
        auto current = boundary;
        do {
            throw_if_canceled();
            const auto& source = current->source()->point();
            const auto& target = current->target()->point();
            twice_area += source.x() * target.y() - source.y() * target.x();
            if (update_bounds) {
                minimum[0] = (std::min)(minimum[0], source.x());
                minimum[1] = (std::min)(minimum[1], source.y());
                maximum[0] = (std::max)(maximum[0], source.x());
                maximum[1] = (std::max)(maximum[1], source.y());
            }
            ++current;
        } while (current != boundary);
    };
    // Halfedges keep the face on the left: inner CCBs and hole detours
    // subtract their area without relying on a simple polygon representation.
    measure_boundary(outer, true);
    for (auto hole = face->inner_ccbs_begin(); hole != face->inner_ccbs_end(); ++hole)
        measure_boundary(*hole, false);
    if (twice_area <= 0)
        return std::nullopt;
    return RegionMeasure{rational(twice_area), {rational(minimum[0]), rational(minimum[1])}, {rational(maximum[0]), rational(maximum[1])}};
}

void append_directed_curve(std::vector<DirectedCurve>& curves, const Kernel::Point_2& source, const Kernel::Point_2& target)
{
    if (source == target)
        return; // A zero-length part of a closed walk contributes no winding.
    const Winding delta = Kernel().less_xy_2_object()(source, target) ? 1 : -1;
    curves.emplace_back(WindingTraits::Base_x_monotone_curve_2(source, target), delta);
}

void append_ring(std::vector<DirectedCurve>& curves, const Polygon& ring, const std::function<void()>& throw_if_canceled)
{
    if (ring.points.empty())
        return;
    Kernel::Point_2 previous(exact_coordinate(ring.points.back().x()), exact_coordinate(ring.points.back().y()));
    for (const Point& point : ring.points) {
        throw_if_canceled();
        const Kernel::Point_2 current(exact_coordinate(point.x()), exact_coordinate(point.y()));
        append_directed_curve(curves, previous, current);
        previous = current;
    }
}

void classify_nonzero(NonzeroSet& set, const std::function<void()>& throw_if_canceled)
{
    auto& arrangement = set.arrangement();
    using Face        = NonzeroSet::Arrangement_2::Face_handle;
    std::map<const void*, Winding> winding;
    std::deque<Face>               pending;
    const Face                     outside = arrangement.unbounded_face();
    winding.emplace(&*outside, 0);
    pending.push_back(outside);
    while (!pending.empty()) {
        throw_if_canceled();
        const Face face = pending.front();
        pending.pop_front();
        const Winding value          = winding.at(&*face);
        const auto    visit_boundary = [&](auto boundary) {
            auto current = boundary;
            do {
                throw_if_canceled();
                const Face adjacent = current->twin()->face();
                const bool forward  = Kernel().less_xy_2_object()(current->source()->point(), current->target()->point());
                // Curve data is signed in canonical lexicographic direction,
                // independent of any curve-direction choices made by CGAL.
                Winding delta = current->curve().data();
                if (!forward)
                    delta = -delta;
                const Winding next_value = value - delta; // left minus right = directed multiplicity
                const auto    inserted   = winding.emplace(&*adjacent, next_value);
                if (inserted.second)
                    pending.push_back(adjacent);
                else if (inserted.first->second != next_value)
                    throw std::runtime_error("Inconsistent exact closed-walk winding");
                ++current;
            } while (current != boundary);
        };
        for (auto outer = face->outer_ccbs_begin(); outer != face->outer_ccbs_end(); ++outer)
            visit_boundary(*outer);
        for (auto inner = face->inner_ccbs_begin(); inner != face->inner_ccbs_end(); ++inner)
            visit_boundary(*inner);
    }
    for (auto face = arrangement.faces_begin(); face != arrangement.faces_end(); ++face) {
        throw_if_canceled();
        const auto found = winding.find(&*face);
        if (found == winding.end())
            throw std::runtime_error("Unclassified exact arrangement face");
        face->set_contained(found->second != 0);
    }
    throw_if_canceled();
    // Remove boundaries between selected faces: shared edges connect, points
    // do not. Preserve the resulting separate faces rather than PWH scanning.
    set.remove_redundant_edges();
    set.fix_curves_direction();
    for (auto face = arrangement.faces_begin(); face != arrangement.faces_end(); ++face)
        face->set_visited(false);
    throw_if_canceled();
}

void build_nonzero(NonzeroSet&                       set,
                   const std::vector<DirectedCurve>& curves,
                   bool                              already_split,
                   const std::function<void()>&      throw_if_canceled)
{
    throw_if_canceled();
    if (already_split)
        CGAL::insert_non_intersecting_curves(set.arrangement(), curves.begin(), curves.end());
    else
        CGAL::insert(set.arrangement(), curves.begin(), curves.end());
    classify_nonzero(set, throw_if_canceled);
}

std::unique_ptr<NonzeroSet> nonzero_set(const Polygons& rings, const std::function<void()>& throw_if_canceled)
{
    std::vector<DirectedCurve> curves;
    for (const Polygon& ring : rings)
        append_ring(curves, ring, throw_if_canceled);
    auto result = std::make_unique<NonzeroSet>();
    build_nonzero(*result, curves, false, throw_if_canceled);
    return result;
}

std::unique_ptr<NonzeroSet> expolygon_set(const ExPolygon& polygon, const std::function<void()>& throw_if_canceled)
{
    auto result = nonzero_set({polygon.contour}, throw_if_canceled);
    for (const Polygon& hole : polygon.holes) {
        throw_if_canceled();
        result->difference(*nonzero_set({hole}, throw_if_canceled));
    }
    return result;
}

} // namespace

struct Region::Shape
{
    NonzeroSet geometry;
};

namespace {

Regions connected_regions(const NonzeroSet& set, const std::function<void()>& throw_if_canceled)
{
    Regions     regions;
    const auto& arrangement = set.arrangement();
    for (auto face = arrangement.faces_begin(); face != arrangement.faces_end(); ++face) {
        throw_if_canceled();
        if (!face->contained())
            continue;
        const auto measured = measure_face(face, throw_if_canceled);
        if (!measured)
            throw std::runtime_error("Invalid exact positive-area region");
        std::vector<DirectedCurve> curves;
        const auto                 append_boundary = [&](auto boundary) {
            auto current = boundary;
            do {
                throw_if_canceled();
                append_directed_curve(curves, current->source()->point(), current->target()->point());
                ++current;
            } while (current != boundary);
        };
        append_boundary(*face->outer_ccbs_begin());
        for (auto hole = face->inner_ccbs_begin(); hole != face->inner_ccbs_end(); ++hole)
            append_boundary(*hole);
        // Copy only this face's already split boundary, never the complete
        // arrangement for every Region and never an integer-rounded polygon.
        auto shape = std::make_shared<Region::Shape>();
        build_nonzero(shape->geometry, curves, true, throw_if_canceled);
        regions.push_back({std::move(shape), measured->twice_area, measured->min, measured->max});
    }
    return regions;
}

const NonzeroSet& geometry(const Region& region)
{
    if (!region.shape)
        throw std::runtime_error("Missing exact region shape");
    return region.shape->geometry;
}

bool boundary_witness_inside(const NonzeroSet& source, const NonzeroSet& other, const std::function<void()>& throw_if_canceled)
{
    const auto& arrangement = source.arrangement();
    for (auto face = arrangement.faces_begin(); face != arrangement.faces_end(); ++face) {
        throw_if_canceled();
        if (!face->contained())
            continue;
        const auto                           edge  = *face->outer_ccbs_begin();
        const auto&                          start = edge->source()->point();
        const auto&                          end   = edge->target()->point();
        const std::array<Kernel::Point_2, 2> witnesses{start, Kernel::Point_2((start.x() + end.x()) / 2, (start.y() + end.y()) / 2)};
        for (const auto& point : witnesses) {
            throw_if_canceled();
            const bool inside = other.oriented_side(point) == CGAL::ON_POSITIVE_SIDE;
            throw_if_canceled();
            if (inside)
                return true;
        }
        return false; // A Region contains one positive-area connected face.
    }
    return false;
}

template<class BuildLeaf>
std::unique_ptr<NonzeroSet> union_range(size_t                       begin,
                                        size_t                       end,
                                        const BuildLeaf&             build_leaf,
                                        const std::function<void()>& throw_if_canceled)
{
    throw_if_canceled();
    if (end - begin == 1)
        return build_leaf(begin);
    const size_t middle = begin + (end - begin) / 2;
    auto         left   = union_range(begin, middle, build_leaf, throw_if_canceled);
    auto         right  = union_range(middle, end, build_leaf, throw_if_canceled);
    throw_if_canceled();
    left->join(*right);
    throw_if_canceled();
    return left;
}

} // namespace

Regions region_from_expolygon(const ExPolygon& polygon, const std::function<void()>& throw_if_canceled)
{
    return connected_regions(*expolygon_set(polygon, throw_if_canceled), throw_if_canceled);
}

Regions regions_from_nonzero(const Polygons& rings, const std::function<void()>& throw_if_canceled)
{
    return connected_regions(*nonzero_set(rings, throw_if_canceled), throw_if_canceled);
}

Regions union_expolygons(const std::vector<const ExPolygon*>& polygons, const std::function<void()>& throw_if_canceled)
{
    throw_if_canceled();
    if (polygons.empty())
        return {};
    const auto build_leaf = [&](size_t index) {
        if (polygons[index] == nullptr)
            throw std::runtime_error("Missing exact source polygon");
        return expolygon_set(*polygons[index], throw_if_canceled);
    };
    auto result = union_range(0, polygons.size(), build_leaf, throw_if_canceled);
    return connected_regions(*result, throw_if_canceled);
}

Regions union_nonzero_groups(const std::vector<Polygons>& groups, const std::function<void()>& throw_if_canceled)
{
    throw_if_canceled();
    if (groups.empty())
        return {};
    const auto build_leaf = [&](size_t index) { return nonzero_set(groups[index], throw_if_canceled); };
    auto       result     = union_range(0, groups.size(), build_leaf, throw_if_canceled);
    return connected_regions(*result, throw_if_canceled);
}

Regions union_regions(const Regions& regions, const std::function<void()>& throw_if_canceled)
{
    throw_if_canceled();
    if (regions.size() <= 1) {
        if (!regions.empty())
            geometry(regions.front());
        throw_if_canceled();
        return regions;
    }
    // Keep exact sets throughout the balanced reduction. Only the final
    // arrangement is copied into separate positive-area connected Regions.
    const auto build_leaf = [&](size_t index) { return std::make_unique<NonzeroSet>(geometry(regions[index])); };
    auto       result     = union_range(0, regions.size(), build_leaf, throw_if_canceled);
    return connected_regions(*result, throw_if_canceled);
}

Regions intersect_regions(const Region& left, const Region& right, const std::function<void()>& throw_if_canceled)
{
    throw_if_canceled();
    NonzeroSet result = geometry(left);
    result.intersection(geometry(right));
    return connected_regions(result, throw_if_canceled);
}

bool positive_area_overlap(const Region& left, const Region& right, const std::function<void()>& throw_if_canceled)
{
    throw_if_canceled();
    const NonzeroSet& left_geometry  = geometry(left);
    const NonzeroSet& right_geometry = geometry(right);
    // A boundary point strictly inside the other regular set proves a
    // positive-area intersection. Boundary-only hits never suffice.
    if (boundary_witness_inside(left_geometry, right_geometry, throw_if_canceled) ||
        boundary_witness_inside(right_geometry, left_geometry, throw_if_canceled))
        return true;
    const bool result = left_geometry.do_intersect(right_geometry);
    throw_if_canceled();
    return result;
}

Regions subtract_regions(const Region& target, const Regions& material, const std::function<void()>& throw_if_canceled)
{
    throw_if_canceled();
    const NonzeroSet& target_geometry = geometry(target);
    if (material.empty()) {
        throw_if_canceled();
        return {target};
    }
    NonzeroSet result = target_geometry;
    for (const Region& region : material) {
        throw_if_canceled();
        result.difference(geometry(region));
    }
    return connected_regions(result, throw_if_canceled);
}

} // namespace Slic3r::SliceAnalysisGeometry
