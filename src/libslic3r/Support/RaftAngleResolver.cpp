#include "RaftAngleResolver.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "../Fill/Fill.hpp"
#include "../Geometry.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"

namespace Slic3r {
namespace {

constexpr double ANGLE_EPSILON = 1e-6;

double normalize_line_angle_radians(double angle)
{
    angle = std::fmod(angle, M_PI);
    if (angle < 0.0)
        angle += M_PI;
    return angle;
}

double normalize_line_angle_degrees(double angle)
{
    angle = std::fmod(angle, 180.0);
    if (angle < 0.0)
        angle += 180.0;
    return angle;
}

bool is_directional_bottom_pattern(InfillPattern pattern)
{
    switch (pattern) {
    case ipMonotonic:
    case ipMonotonicLine:
    case ipRectilinear:
    case ipAlignedRectilinear: return true;
    default: return false;
    }
}

bool has_randomized_rotation_template(const std::string &rotation_template)
{
    return rotation_template.find_first_of("~^") != std::string::npos;
}

bool line_angles_equal(double lhs, double rhs)
{
    const double difference = std::abs(normalize_line_angle_radians(lhs) - normalize_line_angle_radians(rhs));
    return std::min(difference, M_PI - difference) <= ANGLE_EPSILON;
}

} // namespace

double resolve_cura_raft_surface_angle(const PrintObject &object, double fallback_angle)
{
    const double fallback = normalize_line_angle_degrees(fallback_angle);
    if (object.layers().empty())
        return fallback;

    const Layer *first_model_layer = object.get_layer(0);
    if (first_model_layer == nullptr)
        return fallback;

    std::optional<double> model_l1_angle;
    const auto            object_matrix = object.trafo().matrix();
    const double          object_rotation = std::atan2(double(object_matrix(1, 0)), double(object_matrix(0, 0)));

    for (const LayerRegion *layer_region : first_model_layer->regions()) {
        // Support preview may construct SupportParameters before prepare_infill().
        // Raw L1 region slices are already available after slicing and provide a
        // stable way to ignore regions which do not touch model layer 0.
        if (layer_region == nullptr || layer_region->get_slices().empty())
            continue;

        const PrintRegionConfig &region_config = layer_region->region().config();
        // A region with zero bottom shell layers may touch model layer 0, but
        // Orca converts that surface to ordinary internal fill. There is no
        // actual model Bottom hatch for the Raft Surface to follow.
        if (region_config.bottom_shell_layers.value == 0)
            continue;
        if (!is_directional_bottom_pattern(region_config.bottom_surface_pattern.value))
            continue;
        // These template operators consume rand() while resolving a layer.
        // Re-evaluating them here would not reproduce the angle later used by
        // model Bottom fill, so the layer has no stable direction to mirror.
        if (has_randomized_rotation_template(region_config.solid_infill_rotate_template.value))
            return fallback;

        double region_angle = calculate_infill_rotation_angle(&object, 0, region_config.solid_infill_direction.value,
                                                               region_config.solid_infill_rotate_template.value);
        if (region_config.align_infill_direction_to_model.value)
            region_angle += object_rotation;
        region_angle = normalize_line_angle_radians(region_angle);

        if (!model_l1_angle.has_value())
            model_l1_angle = region_angle;
        else if (!line_angles_equal(*model_l1_angle, region_angle))
            return fallback;
    }

    if (!model_l1_angle.has_value())
        return fallback;

    return normalize_line_angle_degrees(Geometry::rad2deg(*model_l1_angle + 0.5 * M_PI));
}

} // namespace Slic3r
