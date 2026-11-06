#include <catch2/catch.hpp>

#include "libslic3r/InProcessReconstructionControl.hpp"

#include <future>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace Slic3r;

namespace {
constexpr uint64_t MIB = 1024 * 1024;

InProcessReconstructionEstimateInput small_estimate()
{
    InProcessReconstructionEstimateInput input;
    input.input_faces      = 12;
    input.input_vertices   = 8;
    input.surface_area_mm2 = 600.;
    input.extent_mm        = {{10., 10., 10.}};
    input.voxel_mm         = 0.1;
    input.memory_bytes     = 64 * MIB;
    input.memory_limit_mb  = 1024;
    return input;
}
} // namespace

TEST_CASE("In-process reconstruction rejects invalid deadlines and missing clocks", "[ModelRepair][InProcessReconstruction]")
{
    REQUIRE_THROWS_AS(InProcessReconstructionControl(0), std::invalid_argument);
    REQUIRE_THROWS_AS(InProcessReconstructionControl(300001), std::invalid_argument);
    REQUIRE_THROWS_AS(InProcessReconstructionControl(100, {}), std::invalid_argument);
}

TEST_CASE("In-process reconstruction observes a sticky cooperative deadline", "[ModelRepair][InProcessReconstruction]")
{
    using Clock                        = InProcessReconstructionControl::Clock;
    auto                           now = Clock::time_point{};
    InProcessReconstructionControl control(100, [&] { return now; });
    control.poll();
    REQUIRE(control.keep_running());
    now += std::chrono::milliseconds(99);
    control.poll();
    REQUIRE(control.keep_running());
    now += std::chrono::milliseconds(1);
    control.poll();
    REQUIRE(control.stop_reason() == InProcessReconstructionStop::TimedOut);
    REQUIRE(control.elapsed_ms() == 100);
    control.request_cancel();
    control.poll();
    REQUIRE(control.stop_reason() == InProcessReconstructionStop::TimedOut);
    REQUIRE(std::string(inprocess_reconstruction_stop_name(control.stop_reason())) == "timed_out");
}

TEST_CASE("In-process reconstruction stops on clock failure", "[ModelRepair][InProcessReconstruction]")
{
    SECTION("clock throws during construction")
    {
        InProcessReconstructionControl control(100, []() -> InProcessReconstructionControl::Clock::time_point { throw 1; });
        REQUIRE(control.stop_reason() == InProcessReconstructionStop::MonitorFailed);
    }
    SECTION("clock throws during polling")
    {
        bool fail = false;
        InProcessReconstructionControl control(100, [&]() -> InProcessReconstructionControl::Clock::time_point {
            if (fail)
                throw std::runtime_error("clock failure");
            return {};
        });
        fail = true;
        REQUIRE_NOTHROW(control.poll());
        REQUIRE(control.stop_reason() == InProcessReconstructionStop::MonitorFailed);
    }
    SECTION("clock moves backwards")
    {
        auto now = InProcessReconstructionControl::Clock::time_point{};
        InProcessReconstructionControl control(100, [&] { return now; });
        now -= std::chrono::milliseconds(1);
        control.poll();
        REQUIRE(control.stop_reason() == InProcessReconstructionStop::MonitorFailed);
    }
}

TEST_CASE("In-process reconstruction cancellation is atomic and does not detach work", "[ModelRepair][InProcessReconstruction]")
{
    InProcessReconstructionControl control(300000);
    std::promise<void>             entered;
    auto                           task = std::async(std::launch::async, [&] {
        entered.set_value();
        while (control.keep_running())
            std::this_thread::yield();
        return control.stop_reason();
    });
    entered.get_future().wait();
    control.request_cancel();
    REQUIRE(task.get() == InProcessReconstructionStop::Canceled);
    control.poll();
    REQUIRE(control.stop_reason() == InProcessReconstructionStop::Canceled);
}

TEST_CASE("In-process reconstruction can read this process memory usage", "[ModelRepair][InProcessReconstruction]")
{
    uint64_t bytes_used = 0;
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    REQUIRE(current_reconstruction_memory_bytes(bytes_used));
    REQUIRE(bytes_used > 0);
#else
    REQUIRE_FALSE(current_reconstruction_memory_bytes(bytes_used));
#endif
}

TEST_CASE("In-process reconstruction admission keeps a clearly labeled heuristic", "[ModelRepair][InProcessReconstruction]")
{
    const auto input    = small_estimate();
    const auto estimate = estimate_inprocess_reconstruction(input);
    REQUIRE(estimate.accepted);
    REQUIRE(estimate.reason == "within_heuristic_budget");
    REQUIRE(estimate.estimated_surface_cells > 0);
    REQUIRE(estimate.estimated_incremental_bytes > 128 * input.max_output_faces);
    REQUIRE(estimate.estimated_total_bytes == estimate.estimated_incremental_bytes + input.memory_bytes);
}

TEST_CASE("Fine reconstruction admission includes volume storage for the glasses model", "[ModelRepair][InProcessReconstruction]")
{
    auto input             = small_estimate();
    input.input_faces      = 2304;
    input.input_vertices   = 1152;
    input.surface_area_mm2 = 10303.44;
    input.extent_mm        = {{163.632, 174.537, 31.004}};
    input.memory_bytes     = 1024 * MIB;
    input.memory_limit_mb  = 2048;
    input.voxel_mm         = 0.075;
    REQUIRE(estimate_inprocess_reconstruction(input).accepted);

    // This Mac fixture exceeded 3 GiB at 0.05 mm despite the old surface-only
    // estimate accepting it. Reject before allocating the SDK grid, not after
    // a long computation; the same host can still accept the coarser request.
    input.voxel_mm  = 0.05;
    const auto fine = estimate_inprocess_reconstruction(input);
    REQUIRE_FALSE(fine.accepted);
    REQUIRE(fine.reason == "estimated_memory_budget_exceeded");
    input.voxel_mm = 0.075;
    REQUIRE(estimate_inprocess_reconstruction(input).accepted);
}

TEST_CASE("In-process reconstruction admission rejects invalid and overflow-prone requests", "[ModelRepair][InProcessReconstruction]")
{
    auto input = small_estimate();
    SECTION("invalid input faces") { input.input_faces = 3000001; }
    SECTION("invalid vertices") { input.input_vertices = 3 * input.input_faces + 1; }
    SECTION("missing input vertices") { input.input_vertices = 0; }
    SECTION("empty input") { input.input_faces = 0; }
    SECTION("invalid output cap") { input.max_output_faces = 1000001; }
    SECTION("output cannot form solid") { input.max_output_faces = 3; }
    SECTION("unknown process memory") { input.memory_bytes = 0; }
    SECTION("unsupported memory cap") { input.memory_limit_mb = 4097; }
    SECTION("zero surface") { input.surface_area_mm2 = 0.; }
    SECTION("infinite surface") { input.surface_area_mm2 = std::numeric_limits<double>::infinity(); }
    SECTION("negative extent") { input.extent_mm[0] = -1.; }
    SECTION("infinite extent") { input.extent_mm[0] = std::numeric_limits<double>::infinity(); }
    SECTION("missing extent") { input.extent_mm = {{0., 0., 0.}}; }
    SECTION("zero voxel") { input.voxel_mm = 0.; }
    SECTION("negative voxel") { input.voxel_mm = -0.1; }
    SECTION("infinite voxel") { input.voxel_mm = std::numeric_limits<double>::infinity(); }
    SECTION("not a number voxel") { input.voxel_mm = std::numeric_limits<double>::quiet_NaN(); }
    SECTION("underflow in SDK float") { input.voxel_mm = 1e-100; }
    SECTION("overflow in SDK float") { input.voxel_mm = 1e100; }
    SECTION("sparse grid dimension integer limit") { input.voxel_mm = 1e-9; }
    SECTION("surface estimate overflow") { input.surface_area_mm2 = std::numeric_limits<double>::max(); }
    SECTION("total estimate overflow") { input.memory_bytes = std::numeric_limits<uint64_t>::max(); }
    SECTION("current process already over budget") { input.memory_bytes = 2048 * MIB; }
    SECTION("fine grid exceeds heuristic budget") { input.voxel_mm = 0.0001; }
    const auto estimate = estimate_inprocess_reconstruction(input);
    REQUIRE_FALSE(estimate.accepted);
    REQUIRE_FALSE(estimate.reason.empty());
}

TEST_CASE("In-process reconstruction admission is monotonic for finer voxel grids", "[ModelRepair][InProcessReconstruction]")
{
    auto       input  = small_estimate();
    const auto coarse = estimate_inprocess_reconstruction(input);
    input.voxel_mm *= 0.5;
    const auto fine = estimate_inprocess_reconstruction(input);
    REQUIRE(fine.accepted);
    REQUIRE(fine.estimated_surface_cells >= coarse.estimated_surface_cells * 3);
    REQUIRE(fine.estimated_incremental_bytes > coarse.estimated_incremental_bytes);
}

TEST_CASE("Future-process workload is independent of check-process residency", "[InProcessReconstruction][ReconstructionAdmission]")
{
    auto input = small_estimate();
    const auto running = estimate_inprocess_reconstruction(input);
    REQUIRE(running.accepted);
    input.memory_bytes = 0;
    const auto workload = estimate_reconstruction_workload(input);
    REQUIRE(workload.accepted);
    REQUIRE(workload.estimated_incremental_bytes == running.estimated_incremental_bytes);
    REQUIRE(workload.estimated_total_bytes == 0);
    REQUIRE_FALSE(estimate_inprocess_reconstruction(input).accepted);
    input.memory_bytes = 10 * 1024 * MIB;
    REQUIRE(estimate_reconstruction_workload(input).estimated_incremental_bytes == workload.estimated_incremental_bytes);
    REQUIRE_FALSE(estimate_inprocess_reconstruction(input).accepted);
}
