#include "InProcessReconstructionControl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <unistd.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace Slic3r {
namespace {
constexpr uint64_t BYTES_PER_MIB    = 1024 * 1024;
constexpr size_t   MAX_INPUT_FACES  = 3000000;
constexpr size_t   MAX_OUTPUT_FACES = 1000000;

bool valid_memory_budget(size_t value) { return value >= 128 && value <= 4096; }
} // namespace

const char* inprocess_reconstruction_stop_name(InProcessReconstructionStop reason) noexcept
{
    switch (reason) {
    case InProcessReconstructionStop::None: return "running";
    case InProcessReconstructionStop::Canceled: return "canceled";
    case InProcessReconstructionStop::TimedOut: return "timed_out";
    case InProcessReconstructionStop::MonitorFailed: return "monitor_failed";
    }
    return "monitor_failed";
}

uint64_t current_reconstruction_process_id() noexcept
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<uint64_t>(getpid());
#endif
}

const char* reconstruction_memory_metric() noexcept
{
#if defined(__APPLE__)
    return "physical_footprint";
#elif defined(_WIN32)
    return "working_set";
#else
    return "resident_set";
#endif
}

bool current_reconstruction_memory_bytes(uint64_t& bytes) noexcept
{
    bytes = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return false;
    bytes = counters.WorkingSetSize;
#elif defined(__APPLE__)
    // Footprint includes dirty/compressed pages, excluding reclaimable clean
    // caches. RSS can both overcount those caches and miss compressed memory.
    task_vm_info_data_t    info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS ||
        count < TASK_VM_INFO_REV1_COUNT)
        return false;
    bytes = info.phys_footprint;
#elif defined(__linux__)
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        return false;
    FILE* file = std::fopen("/proc/self/statm", "r");
    if (!file)
        return false;
    unsigned long long total_pages = 0, resident_pages = 0;
    const bool         parsed = std::fscanf(file, "%llu %llu", &total_pages, &resident_pages) == 2;
    std::fclose(file);
    if (!parsed || resident_pages > std::numeric_limits<uint64_t>::max() / uint64_t(page_size))
        return false;
    bytes = resident_pages * uint64_t(page_size);
#else
    return false;
#endif
    return bytes != 0;
}

InProcessReconstructionControl::InProcessReconstructionControl(unsigned timeout_ms, ClockReader clock_reader)
    : m_timeout_ms(timeout_ms), m_clock_reader(std::move(clock_reader))
{
    if (timeout_ms == 0 || timeout_ms > 300000 || !m_clock_reader)
        throw std::invalid_argument("Invalid cooperative reconstruction deadline or clock reader.");
    try {
        m_started = m_clock_reader();
    } catch (...) {
        request_stop(InProcessReconstructionStop::MonitorFailed);
    }
}

void InProcessReconstructionControl::request_stop(InProcessReconstructionStop reason) noexcept
{
    auto expected = InProcessReconstructionStop::None;
    m_stop.compare_exchange_strong(expected, reason, std::memory_order_relaxed);
}

void InProcessReconstructionControl::poll() noexcept
{
    try {
        const auto now = m_clock_reader();
        if (now < m_started) {
            request_stop(InProcessReconstructionStop::MonitorFailed);
            return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_started).count();
        m_elapsed_ms.store(uint64_t(elapsed), std::memory_order_relaxed);
        if (uint64_t(elapsed) >= m_timeout_ms)
            request_stop(InProcessReconstructionStop::TimedOut);
    } catch (...) {
        request_stop(InProcessReconstructionStop::MonitorFailed);
    }
}

void InProcessReconstructionControl::request_cancel() noexcept { request_stop(InProcessReconstructionStop::Canceled); }
bool InProcessReconstructionControl::keep_running() const noexcept { return stop_reason() == InProcessReconstructionStop::None; }
InProcessReconstructionStop InProcessReconstructionControl::stop_reason() const noexcept { return m_stop.load(std::memory_order_relaxed); }
uint64_t InProcessReconstructionControl::elapsed_ms() const noexcept { return m_elapsed_ms.load(std::memory_order_relaxed); }

InProcessReconstructionEstimate estimate_reconstruction_workload(const InProcessReconstructionEstimateInput& input)
{
    InProcessReconstructionEstimate result;
    result.reason = "invalid_input_or_budget";
    if (input.input_faces == 0 || input.input_faces > MAX_INPUT_FACES || input.input_vertices == 0 ||
        input.input_vertices > 3 * input.input_faces || input.max_output_faces < 4 || input.max_output_faces > MAX_OUTPUT_FACES ||
        !std::isfinite(input.surface_area_mm2) || input.surface_area_mm2 <= 0. ||
        !std::isfinite(input.voxel_mm) || input.voxel_mm <= 0. || input.voxel_mm > std::numeric_limits<float>::max() ||
        float(input.voxel_mm) <= 0.)
        return result;

    bool                       any_extent = false;
    std::array<long double, 3> grid;
    // Use the SDK's actual float voxel, not the decimal double requested by CLI.
    const long double voxel = float(input.voxel_mm);
    for (size_t axis = 0; axis < grid.size(); ++axis) {
        const double extent = input.extent_mm[axis];
        if (!std::isfinite(extent) || extent < 0.)
            return result;
        any_extent = any_extent || extent > 0.;
        // SDK calcOriginAndDimensions pads both ends by two voxels.
        if (static_cast<long double>(extent) / voxel > std::numeric_limits<int32_t>::max() - 16) {
            result.reason = "voxel_grid_dimension_limit";
            return result;
        }
        grid[axis] = std::ceil(static_cast<long double>(extent) / voxel) + 5;
    }
    if (!any_extent)
        return result;
    const long double cells       = std::ceil(static_cast<long double>(input.surface_area_mm2) / voxel / voxel);
    const long double plane_cells = grid[0] * grid[1];
    // Allow for grid masks and temporary distance samples. Native MeshLib
    // chooses its own allocation strategy; this estimate is only a preflight filter.
    const long double grid_masks         = 2 * std::ceil(plane_cells / 64) * 8 * grid[2];
    const long double distance_workspace = std::max(32 * static_cast<long double>(BYTES_PER_MIB), 8 * plane_cells) + 4 * plane_cells;
    const long double incremental        = std::ceil(grid_masks + distance_workspace + 256 * static_cast<long double>(input.input_faces) +
                                                     48 * static_cast<long double>(input.input_vertices) +
                                                     128 * static_cast<long double>(input.max_output_faces) + 64 * cells);
    // Compare strictly below 2^64, avoiding an out-of-range integer cast even on
    // platforms where long double has only double precision.
    const long double uint64_end = std::ldexp(1.L, 64);
    if (!std::isfinite(cells) || !std::isfinite(incremental) || cells >= uint64_end || incremental >= uint64_end) {
        result.reason = "memory_estimate_overflow";
        return result;
    }
    result.estimated_surface_cells     = uint64_t(cells);
    result.estimated_incremental_bytes = uint64_t(incremental);
    result.accepted = true;
    result.reason   = "workload_estimated";
    return result;
}

InProcessReconstructionEstimate estimate_inprocess_reconstruction(const InProcessReconstructionEstimateInput& input)
{
    auto result = estimate_reconstruction_workload(input);
    if (!result.accepted)
        return result;
    result.accepted = false;
    if (!valid_memory_budget(input.memory_limit_mb)) {
        result.reason = "invalid_input_or_budget";
        return result;
    }
    if (input.memory_bytes == 0) {
        result.reason = "process_memory_unknown";
        return result;
    }
    const long double total = static_cast<long double>(result.estimated_incremental_bytes) + input.memory_bytes;
    if (total >= std::ldexp(1.L, 64)) {
        result.reason = "memory_estimate_overflow";
        return result;
    }
    result.estimated_total_bytes = uint64_t(total);
    if (total > static_cast<long double>(input.memory_limit_mb) * BYTES_PER_MIB) {
        result.reason = "estimated_memory_budget_exceeded";
        return result;
    }
    result.accepted = true;
    result.reason   = "within_heuristic_budget";
    return result;
}

} // namespace Slic3r
