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
#include <libproc.h>
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
    case InProcessReconstructionStop::MemoryLimitExceeded: return "memory_limit_exceeded";
    case InProcessReconstructionStop::MonitorFailed: return "monitor_failed";
    }
    return "monitor_failed";
}

bool current_reconstruction_resident_bytes(uint64_t& bytes) noexcept
{
    bytes = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return false;
    bytes = counters.WorkingSetSize;
#elif defined(__APPLE__)
    proc_taskinfo info{};
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &info, sizeof(info)) != sizeof(info))
        return false;
    bytes = info.pti_resident_size;
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

InProcessReconstructionControl::InProcessReconstructionControl(unsigned    timeout_ms,
                                                               size_t      memory_limit_mb,
                                                               RssReader   rss_reader,
                                                               ClockReader clock_reader)
    : m_timeout_ms(timeout_ms), m_memory_limit_bytes(0), m_rss_reader(std::move(rss_reader)), m_clock_reader(std::move(clock_reader))
{
    if (timeout_ms == 0 || timeout_ms > 300000 || !valid_memory_budget(memory_limit_mb) || !m_rss_reader || !m_clock_reader)
        throw std::invalid_argument("Invalid cooperative reconstruction budget or monitoring reader.");
    m_memory_limit_bytes = uint64_t(memory_limit_mb) * BYTES_PER_MIB;
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
        uint64_t bytes = 0;
        if (!m_rss_reader(bytes) || bytes == 0) {
            request_stop(InProcessReconstructionStop::MonitorFailed);
            return;
        }
        m_resident_bytes.store(bytes, std::memory_order_relaxed);
        m_peak_resident_bytes.store(std::max(bytes, m_peak_resident_bytes.load(std::memory_order_relaxed)), std::memory_order_relaxed);
        if (bytes > m_memory_limit_bytes)
            request_stop(InProcessReconstructionStop::MemoryLimitExceeded);
    } catch (...) {
        request_stop(InProcessReconstructionStop::MonitorFailed);
    }
}

void InProcessReconstructionControl::request_cancel() noexcept { request_stop(InProcessReconstructionStop::Canceled); }
bool InProcessReconstructionControl::keep_running() const noexcept { return stop_reason() == InProcessReconstructionStop::None; }
InProcessReconstructionStop InProcessReconstructionControl::stop_reason() const noexcept { return m_stop.load(std::memory_order_relaxed); }
uint64_t InProcessReconstructionControl::elapsed_ms() const noexcept { return m_elapsed_ms.load(std::memory_order_relaxed); }
uint64_t InProcessReconstructionControl::resident_bytes() const noexcept { return m_resident_bytes.load(std::memory_order_relaxed); }
uint64_t InProcessReconstructionControl::peak_resident_bytes() const noexcept
{
    return m_peak_resident_bytes.load(std::memory_order_relaxed);
}

InProcessReconstructionEstimate estimate_inprocess_reconstruction(const InProcessReconstructionEstimateInput& input)
{
    InProcessReconstructionEstimate result;
    result.reason = "invalid_input_or_budget";
    if (input.input_faces == 0 || input.input_faces > MAX_INPUT_FACES || input.input_vertices == 0 ||
        input.input_vertices > 3 * input.input_faces || input.max_output_faces < 4 || input.max_output_faces > MAX_OUTPUT_FACES ||
        !valid_memory_budget(input.memory_limit_mb) || !std::isfinite(input.surface_area_mm2) || input.surface_area_mm2 <= 0. ||
        !std::isfinite(input.voxel_mm) || input.voxel_mm <= 0. || input.voxel_mm > std::numeric_limits<float>::max() ||
        float(input.voxel_mm) <= 0.)
        return result;
    if (input.resident_bytes == 0) {
        result.reason = "resident_memory_unknown";
        return result;
    }

    bool any_extent = false;
    // Use the SDK's actual float voxel, not the decimal double requested by CLI.
    const long double voxel = float(input.voxel_mm);
    for (double extent : input.extent_mm) {
        if (!std::isfinite(extent) || extent < 0.)
            return result;
        any_extent = any_extent || extent > 0.;
        // Reserve padding for the sparse-grid neighborhood. This checks integer
        // representability only; the estimate below is not a dense-grid bound.
        if (static_cast<long double>(extent) / voxel > std::numeric_limits<int32_t>::max() - 16) {
            result.reason = "voxel_grid_dimension_limit";
            return result;
        }
    }
    if (!any_extent)
        return result;
    const long double cells       = std::ceil(static_cast<long double>(input.surface_area_mm2) / voxel / voxel);
    const long double incremental = std::ceil(
        32 * static_cast<long double>(BYTES_PER_MIB) + 256 * static_cast<long double>(input.input_faces) +
        48 * static_cast<long double>(input.input_vertices) + 128 * static_cast<long double>(input.max_output_faces) + 64 * cells);
    const long double total = std::ceil(incremental + input.resident_bytes);
    // Compare strictly below 2^64, avoiding an out-of-range integer cast even on
    // platforms where long double has only double precision.
    const long double uint64_end = std::ldexp(1.L, 64);
    if (!std::isfinite(cells) || !std::isfinite(incremental) || !std::isfinite(total) || cells >= uint64_end || incremental >= uint64_end ||
        total >= uint64_end) {
        result.reason = "memory_estimate_overflow";
        return result;
    }
    result.estimated_surface_cells     = uint64_t(cells);
    result.estimated_incremental_bytes = uint64_t(incremental);
    result.estimated_total_bytes       = uint64_t(total);
    if (total > static_cast<long double>(input.memory_limit_mb) * BYTES_PER_MIB) {
        result.reason = "estimated_memory_budget_exceeded";
        return result;
    }
    result.accepted = true;
    result.reason   = "within_heuristic_budget";
    return result;
}

} // namespace Slic3r
