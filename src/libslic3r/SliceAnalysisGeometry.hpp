#ifndef libslic3r_SliceAnalysisGeometry_hpp_
#define libslic3r_SliceAnalysisGeometry_hpp_

#include "ExPolygon.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace Slic3r::SliceAnalysisGeometry {

using Rational = boost::multiprecision::cpp_rational;

struct Region
{
    struct Shape;
    std::shared_ptr<const Shape> shape;
    Rational                     twice_area;
    std::array<Rational, 2>      min, max;
};

using Regions = std::vector<Region>;

// Exact positive-area connected regions; point contacts do not connect them.
// Coordinates and twice_area retain scaled units and rational intersections.
// Each source ExPolygon is NonZero(contour) minus the union of NonZero(holes).
// Pass only the rings belonging to one source path to regions_from_nonzero;
// independent sources must be combined with union_regions instead.
// Empty/zero-area sets return {}; invalid Region handles and inconsistent
// internal geometry throw std::runtime_error. Cancellation propagates.
Regions region_from_expolygon(const ExPolygon& polygon, const std::function<void()>& throw_if_canceled);
Regions regions_from_nonzero(const Polygons& rings, const std::function<void()>& throw_if_canceled);
// Batch independent owners before measuring or rebuilding their components.
// Each polygon/group keeps the same individual NonZero semantics above;
// their sets are then united. Null polygon pointers are invalid input.
Regions union_expolygons(const std::vector<const ExPolygon*>& polygons, const std::function<void()>& throw_if_canceled);
Regions union_nonzero_groups(const std::vector<Polygons>& groups, const std::function<void()>& throw_if_canceled);
Regions union_regions(const Regions& regions, const std::function<void()>& throw_if_canceled);
Regions intersect_regions(const Region& left, const Region& right, const std::function<void()>& throw_if_canceled);
bool    positive_area_overlap(const Region& left, const Region& right, const std::function<void()>& throw_if_canceled);
Regions subtract_regions(const Region& target, const Regions& material, const std::function<void()>& throw_if_canceled);

} // namespace Slic3r::SliceAnalysisGeometry

#endif
