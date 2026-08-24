#ifndef slic3r_RaftPlan_hpp_
#define slic3r_RaftPlan_hpp_

#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r {

enum class RaftPhase {
    Base,
    Interface,
    Surface,
};

enum class RaftPlanMode {
    Legacy,
    CuraV1,
};

// Resolved settings for one Cura V1 raft phase. Lengths are expressed in mm,
// speeds in mm/s, fan speeds as percentages in [0, 100], and flow_ratio as a
// multiplier where 1.0 means 100%.
struct RaftPhaseConfig {
    size_t layer_count { 0 };
    double layer_height { 0.0 };
    double line_width { 0.0 };
    double line_spacing { 0.0 };
    double flow_ratio { 1.0 };
    double speed { 0.0 };
    double fan_speed { 0.0 };
    size_t wall_count { 0 };
    double margin { 0.0 };
};

// This POD intentionally contains no PrintConfig types. It is the boundary
// between configuration resolution and the Cura raft implementation.
struct RaftPlanConfig {
    // Resolved height of the first physical Raft Base layer. This is not the
    // model's initial layer height and may intentionally differ from it.
    double first_base_layer_height { 0.0 };
    double airgap { 0.0 };
    double overlap { 0.0 };
    // Input angle of the final Surface layer, in degrees. Earlier physical
    // raft layers are derived backwards in 90 degree steps so changing the
    // total layer count never flips the model-contacting Surface direction.
    double surface_angle { 135.0 };

    RaftPhaseConfig base_config;
    RaftPhaseConfig interface_config;
    RaftPhaseConfig surface_config;
};

struct RaftPhaseLayer {
    RaftPhase phase { RaftPhase::Base };
    size_t index { 0 };
    double print_z { 0.0 };
    double height { 0.0 };
    double line_width { 0.0 };
    double line_spacing { 0.0 };
    double angle { 0.0 };
    double flow_ratio { 1.0 };
    double speed { 0.0 };
    double fan_speed { 0.0 };
    size_t wall_count { 0 };
    double margin { 0.0 };
};

struct RaftPhasePlan {
    RaftPlanMode mode { RaftPlanMode::Legacy };
    double airgap { 0.0 };
    double overlap { 0.0 };
    std::vector<RaftPhaseLayer> layers;

    bool empty() const noexcept { return layers.empty(); }
    double top_z() const noexcept;
    size_t phase_layer_count(RaftPhase phase) const noexcept;
    const RaftPhaseLayer *find_layer(RaftPhase phase, size_t index) const noexcept;

    // Returns false and, when supplied, writes the first validation error.
    bool validate(std::string *error = nullptr) const;
};

// Build a deterministic, configuration-independent Cura V1 raft schedule.
// The first Base layer height replaces the configured Base height only for
// the first generated layer. The final Surface angle is anchored explicitly;
// preceding angles alternate backwards across phase boundaries and are
// normalized into [0, 180) degrees.
RaftPhasePlan build_cura_raft_phase_plan(const RaftPlanConfig &config);

} // namespace Slic3r

#endif /* slic3r_RaftPlan_hpp_ */
