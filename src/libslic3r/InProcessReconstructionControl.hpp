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

enum class InProcessReconstructionStop { None, Canceled, TimedOut, MemoryLimitExceeded, MonitorFailed };

const char* inprocess_reconstruction_stop_name(InProcessReconstructionStop reason) noexcept;

// Samples this process only. False (including an unsupported platform) means
// unknown, never zero memory. Does not change process limits or signal handlers.
bool current_reconstruction_resident_bytes(uint64_t& bytes) noexcept;

// Cooperative control, not process isolation or a hard allocator/time limit.
// The caller polls and owns/join()s the background task. SDK callbacks and phase
// checkpoints only read keep_running(); they never run caller/UI callbacks.
// An SDK/CGAL operation without cancellation support can delay completion after
// a stop request. No destructor terminates threads or the host process.
class InProcessReconstructionControl
{
public:
    using Clock       = std::chrono::steady_clock;
    using RssReader   = std::function<bool(uint64_t&)>;
    using ClockReader = std::function<Clock::time_point()>;

    InProcessReconstructionControl(
        unsigned    timeout_ms,
        size_t      memory_limit_mb,
        RssReader   rss_reader   = current_reconstruction_resident_bytes,
        ClockReader clock_reader = [] { return Clock::now(); });

    // Call only from the coordinating caller thread. These functions do not
    // throw; reader failures latch MonitorFailed. The first stop reason wins.
    void                        poll() noexcept;
    void                        request_cancel() noexcept;
    bool                        keep_running() const noexcept;
    InProcessReconstructionStop stop_reason() const noexcept;

    uint64_t elapsed_ms() const noexcept;
    uint64_t resident_bytes() const noexcept;
    uint64_t peak_resident_bytes() const noexcept;

private:
    void request_stop(InProcessReconstructionStop reason) noexcept;

    unsigned                                 m_timeout_ms;
    uint64_t                                 m_memory_limit_bytes;
    RssReader                                m_rss_reader;
    ClockReader                              m_clock_reader;
    Clock::time_point                        m_started;
    std::atomic<InProcessReconstructionStop> m_stop{InProcessReconstructionStop::None};
    std::atomic<uint64_t>                    m_elapsed_ms{0};
    std::atomic<uint64_t>                    m_resident_bytes{0};
    std::atomic<uint64_t>                    m_peak_resident_bytes{0};
};

struct InProcessReconstructionEstimateInput
{
    size_t                input_faces      = 0;
    size_t                input_vertices   = 0;
    size_t                max_output_faces = 1000000;
    double                surface_area_mm2 = 0.;
    std::array<double, 3> extent_mm{{0., 0., 0.}};
    double                voxel_mm        = 0.;
    uint64_t              resident_bytes  = 0;
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

// Checked heuristic for rejecting clearly excessive requests before SDK work.
// Includes input copies/topology, a candidate/validation reserve, and a sparse
// surface-grid estimate. It is neither a dense-grid allocation nor an upper
// bound for MeshLib/CGAL memory. Runtime cooperative RSS polling is still needed.
InProcessReconstructionEstimate estimate_inprocess_reconstruction(const InProcessReconstructionEstimateInput& input);

} // namespace Slic3r

#endif
