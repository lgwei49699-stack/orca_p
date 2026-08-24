#include "RaftPlan.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Slic3r {
namespace {

constexpr double VALIDATION_EPSILON = 1e-9;

bool is_finite_non_negative(double value)
{
    return std::isfinite(value) && value >= 0.0;
}

bool is_finite_positive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

bool nearly_equal(double lhs, double rhs)
{
    const double scale = std::max({ 1.0, std::abs(lhs), std::abs(rhs) });
    return std::abs(lhs - rhs) <= VALIDATION_EPSILON * scale;
}

double normalize_line_angle(double angle)
{
    double normalized = std::fmod(angle, 180.0);
    if (normalized < 0.0)
        normalized += 180.0;
    return normalized;
}

size_t phase_rank(RaftPhase phase)
{
    switch (phase) {
    case RaftPhase::Base: return 0;
    case RaftPhase::Interface: return 1;
    case RaftPhase::Surface: return 2;
    }
    return 3;
}

bool fail_validation(std::string *error, const char *message)
{
    if (error != nullptr)
        *error = message;
    return false;
}

void append_phase_layers(RaftPhasePlan &plan, const RaftPlanConfig &config, RaftPhase phase, const RaftPhaseConfig &phase_config,
                         size_t total_layer_count, double &print_z)
{
    for (size_t index = 0; index < phase_config.layer_count; ++index) {
        const double height = phase == RaftPhase::Base && index == 0 ? config.first_base_layer_height : phase_config.layer_height;
        const size_t global_index = plan.layers.size();
        const size_t layers_before_surface = total_layer_count - global_index - 1;
        print_z += height;

        plan.layers.push_back({ phase,
                                index,
                                print_z,
                                height,
                                phase_config.line_width,
                                phase_config.line_spacing,
                                normalize_line_angle(config.surface_angle + 90.0 * static_cast<double>(layers_before_surface)),
                                phase_config.flow_ratio,
                                phase_config.speed,
                                phase_config.fan_speed,
                                phase_config.wall_count,
                                phase_config.margin });
    }
}

} // namespace

double RaftPhasePlan::top_z() const noexcept
{
    return layers.empty() ? 0.0 : layers.back().print_z;
}

size_t RaftPhasePlan::phase_layer_count(RaftPhase phase) const noexcept
{
    size_t count = 0;
    for (const RaftPhaseLayer &layer : layers)
        if (layer.phase == phase)
            ++count;
    return count;
}

const RaftPhaseLayer *RaftPhasePlan::find_layer(RaftPhase phase, size_t index) const noexcept
{
    for (const RaftPhaseLayer &layer : layers)
        if (layer.phase == phase && layer.index == index)
            return &layer;
    return nullptr;
}

bool RaftPhasePlan::validate(std::string *error) const
{
    if (error != nullptr)
        error->clear();

    if (mode != RaftPlanMode::Legacy && mode != RaftPlanMode::CuraV1)
        return fail_validation(error, "unknown raft plan mode");
    if (!is_finite_non_negative(airgap))
        return fail_validation(error, "airgap must be finite and non-negative");
    if (!is_finite_non_negative(overlap))
        return fail_validation(error, "overlap must be finite and non-negative");
    if (mode == RaftPlanMode::CuraV1 && layers.empty())
        return fail_validation(error, "a Cura V1 raft plan must contain layers");

    std::array<size_t, 3> expected_indices { 0, 0, 0 };
    double expected_print_z = 0.0;
    size_t previous_phase_rank = 0;
    bool first_layer = true;

    for (const RaftPhaseLayer &layer : layers) {
        const size_t rank = phase_rank(layer.phase);
        if (rank >= expected_indices.size())
            return fail_validation(error, "layer has an unknown raft phase");
        if (!first_layer && rank < previous_phase_rank)
            return fail_validation(error, "raft phases must be ordered Base, Interface, Surface");
        if (layer.index != expected_indices[rank])
            return fail_validation(error, "phase layer indices must be contiguous and zero-based");
        if (!is_finite_positive(layer.height))
            return fail_validation(error, "layer height must be finite and positive");
        if (!is_finite_positive(layer.line_width))
            return fail_validation(error, "line width must be finite and positive");
        if (!is_finite_positive(layer.line_spacing))
            return fail_validation(error, "line spacing must be finite and positive");
        if (!std::isfinite(layer.angle) || layer.angle < 0.0 || layer.angle >= 180.0)
            return fail_validation(error, "line angle must be in the [0, 180) degree range");
        if (!is_finite_positive(layer.flow_ratio))
            return fail_validation(error, "flow ratio must be finite and positive");
        if (!is_finite_positive(layer.speed))
            return fail_validation(error, "speed must be finite and positive");
        if (!is_finite_non_negative(layer.fan_speed) || layer.fan_speed > 100.0)
            return fail_validation(error, "fan speed must be in the [0, 100] percent range");
        if (!is_finite_non_negative(layer.margin))
            return fail_validation(error, "margin must be finite and non-negative");

        expected_print_z += layer.height;
        if (!nearly_equal(layer.print_z, expected_print_z))
            return fail_validation(error, "layer print_z values must be cumulative and contiguous");

        ++expected_indices[rank];
        previous_phase_rank = rank;
        first_layer = false;
    }

    if (mode == RaftPlanMode::CuraV1 && expected_indices[phase_rank(RaftPhase::Base)] == 0)
        return fail_validation(error, "a Cura V1 raft plan must contain at least one Base layer");
    if (mode == RaftPlanMode::CuraV1 && expected_indices[phase_rank(RaftPhase::Surface)] == 0)
        return fail_validation(error, "a Cura V1 raft plan must contain at least one Surface layer");

    return true;
}

RaftPhasePlan build_cura_raft_phase_plan(const RaftPlanConfig &config)
{
    RaftPhasePlan plan;
    plan.mode = RaftPlanMode::CuraV1;
    plan.airgap = config.airgap;
    plan.overlap = config.overlap;

    const size_t total_layer_count = config.base_config.layer_count + config.interface_config.layer_count +
                                     config.surface_config.layer_count;
    double print_z = 0.0;
    append_phase_layers(plan, config, RaftPhase::Base, config.base_config, total_layer_count, print_z);
    append_phase_layers(plan, config, RaftPhase::Interface, config.interface_config, total_layer_count, print_z);
    append_phase_layers(plan, config, RaftPhase::Surface, config.surface_config, total_layer_count, print_z);
    return plan;
}

} // namespace Slic3r
