#ifndef SLIC3R_IN_PROCESS_RECONSTRUCTION_CONTROL_HPP
#define SLIC3R_IN_PROCESS_RECONSTRUCTION_CONTROL_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Slic3r {

enum class InProcessReconstructionStop { None, Canceled, TimedOut, MonitorFailed };

const char* inprocess_reconstruction_stop_name(InProcessReconstructionStop reason) noexcept;

uint64_t current_reconstruction_process_id() noexcept;

// Samples this process only. False (including an unsupported platform) means
// unknown, never zero memory. Does not change process limits or signal handlers.
// macOS: physical footprint (dirty/compressed memory); Windows: working set;
// Linux: resident set. Used once for preflight, not for runtime polling.
const char* reconstruction_memory_metric() noexcept;
bool        current_reconstruction_memory_bytes(uint64_t& bytes) noexcept;

// Cooperative deadline/cancellation only. Memory admission is a separate preflight;
// there is no runtime memory monitoring or hard process isolation.
// The caller polls and owns/join()s the background task. SDK callbacks and phase
// checkpoints only read keep_running(); they never run caller/UI callbacks.
// An SDK/CGAL operation without cancellation support can delay completion after
// a stop request. No destructor terminates threads or the host process.
class InProcessReconstructionControl
{
public:
    using Clock        = std::chrono::steady_clock;
    using ClockReader  = std::function<Clock::time_point()>;

    explicit InProcessReconstructionControl(
        unsigned timeout_ms, ClockReader clock_reader = [] { return std::chrono::steady_clock::now(); });

    // Call only from the coordinating caller thread. These functions do not
    // throw; clock failures latch MonitorFailed. The first stop reason wins.
    void                        poll() noexcept;
    void                        request_cancel() noexcept;
    bool                        keep_running() const noexcept;
    InProcessReconstructionStop stop_reason() const noexcept;

    uint64_t elapsed_ms() const noexcept;

private:
    void request_stop(InProcessReconstructionStop reason) noexcept;

    unsigned                                 m_timeout_ms;
    ClockReader                              m_clock_reader;
    Clock::time_point                        m_started;
    std::atomic<InProcessReconstructionStop> m_stop{InProcessReconstructionStop::None};
    std::atomic<uint64_t>                    m_elapsed_ms{0};
};

struct InProcessReconstructionEstimateInput
{
    size_t                input_faces      = 0;
    size_t                input_vertices   = 0;
    size_t                max_output_faces = 1000000;
    double                surface_area_mm2 = 0.;
    std::array<double, 3> extent_mm{{0., 0., 0.}};
    double                voxel_mm        = 0.;
    uint64_t              memory_bytes    = 0;
    size_t                memory_limit_mb = 1024;
};

struct InProcessReconstructionEstimate
{
    bool accepted = false;
    // Machine-readable reason; acceptance is not a guarantee of peak memory.
    std::string reason;
    uint64_t    estimated_incremental_bytes = 0;
    uint64_t    estimated_total_bytes       = 0;
    uint64_t    estimated_surface_cells     = 0;
};

// Geometry-only workload for a future, separate reconstruction process. Does
// not include the check/repair process's resident memory or promise peak usage.
InProcessReconstructionEstimate estimate_reconstruction_workload(const InProcessReconstructionEstimateInput& input);

// Checked heuristic for rejecting clearly excessive requests before SDK work.
// Includes input copies/topology, a candidate/validation reserve, surface work,
// grid masks and distance-sampling workspace. This is not an upper
// bound for MeshLib/CGAL memory and does not enforce a runtime memory limit.
InProcessReconstructionEstimate estimate_inprocess_reconstruction(const InProcessReconstructionEstimateInput& input);

} // namespace Slic3r

#endif
