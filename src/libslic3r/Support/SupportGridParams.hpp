#ifndef slic3r_SupportGridParams_hpp_
#define slic3r_SupportGridParams_hpp_

#include "../Flow.hpp"
#include "../Geometry.hpp"
#include "../PrintConfig.hpp"

#include <cmath>

namespace Slic3r {

// Shared by normal support generation and the pre-slice arrangement estimate.
struct SupportGridParams
{
    SupportGridParams(const PrintObjectConfig& object_config, const Flow& support_material_flow)
        : style(object_config.support_style.value)
        , grid_resolution(object_config.support_base_pattern_spacing.value + support_material_flow.spacing())
        , support_angle(Geometry::deg2rad(object_config.support_angle.value))
        , extrusion_width(support_material_flow.spacing())
        , support_closing_radius(2.0)
        , expansion_to_slice(coord_t(support_material_flow.scaled_spacing() / 2 + 5))
        , expansion_to_propagate(-3)
    {}

    // Packing can rotate objects after this estimate. Use the full grid-cell
    // diagonal rather than an angle-specific XY projection. Snug closing and
    // smoothing need their own allowance instead of the grid resolution.
    double arrange_expansion() const
    {
        return style == smsSnug ? support_closing_radius + extrusion_width :
                                  std::sqrt(2.0) * grid_resolution + unscale<double>(expansion_to_slice);
    }

    SupportMaterialStyle style;
    double               grid_resolution;
    double               support_angle;
    double               extrusion_width;
    double               support_closing_radius;
    coord_t              expansion_to_slice;
    coord_t              expansion_to_propagate;
};

} // namespace Slic3r

#endif
