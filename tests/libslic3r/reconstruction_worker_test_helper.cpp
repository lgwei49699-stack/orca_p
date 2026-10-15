#include "libslic3r/ReconstructionProcess.hpp"

#include <boost/nowide/args.hpp>
#include <boost/nowide/fstream.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

namespace {

constexpr size_t NESTED_ALLOCATION_BYTES = 64 * 1024 * 1024;
constexpr size_t NESTED_MEMORY_LIMIT_MB  = 112;

std::unique_ptr<unsigned char[]> allocate_resident_memory()
{
    auto                    allocation = std::make_unique<unsigned char[]>(NESTED_ALLOCATION_BYTES);
    volatile unsigned char* pages      = allocation.get();
    for (size_t offset = 0; offset < NESTED_ALLOCATION_BYTES; offset += 4096)
        pages[offset] = 0x5a;
    pages[NESTED_ALLOCATION_BYTES - 1] = 0x5a;
    return allocation;
}

// The fixture must prove that each worker is below the nested budget on its
// own. A process with unusual startup overhead fails explicitly rather than
// accidentally passing the combined-RSS test for the wrong reason.
bool allocation_fits_own_budget()
{
    uint64_t rss = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return false;
    rss = counters.WorkingSetSize;
#elif defined(__APPLE__)
    proc_taskinfo info{};
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &info, sizeof(info)) != sizeof(info))
        return false;
    rss = info.pti_resident_size;
#elif defined(__linux__)
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        return false;
    FILE* file = std::fopen("/proc/self/statm", "r");
    if (file == nullptr)
        return false;
    unsigned long long total_pages = 0;
    unsigned long long rss_pages   = 0;
    const bool         parsed      = std::fscanf(file, "%llu %llu", &total_pages, &rss_pages) == 2;
    std::fclose(file);
    if (!parsed)
        return false;
    rss = rss_pages * uint64_t(page_size);
#endif
    std::cerr << "nested fixture RSS: " << rss << " bytes\n";
    return rss >= NESTED_ALLOCATION_BYTES && rss < NESTED_MEMORY_LIMIT_MB * 1024 * 1024;
}

uint64_t parse_pid(const std::string& value)
{
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Invalid test parent PID");
    size_t         consumed = 0;
    const uint64_t pid      = std::stoull(value, &consumed);
    if (consumed != value.size() || pid == 0)
        throw std::invalid_argument("Invalid test parent PID");
    return pid;
}

int run_guard_test(const std::string& mode, uint64_t parent_pid)
{
    using namespace Slic3r;
    if (mode == "--normal") {
        {
            ReconstructionWorkerGuard guard(parent_pid, 25, 512);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // Survive beyond this guard's deadline to prove normal destruction
        // stopped and joined its supervision thread, not just that startup worked.
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        return 0;
    }
    if (mode == "--timeout") {
        ReconstructionWorkerGuard guard(parent_pid, 100, 512);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return 80; // The guard must exit with 124 before this finite fallback.
    }
    if (mode == "--memory") {
        ReconstructionWorkerGuard                     guard(parent_pid, 5000, 128);
        constexpr size_t                              CHUNK_BYTES = 4 * 1024 * 1024;
        std::vector<std::unique_ptr<unsigned char[]>> chunks;
        chunks.reserve(64);
        for (unsigned chunk = 0; chunk < 64; ++chunk) {
            auto                    allocation = std::make_unique<unsigned char[]>(CHUNK_BYTES);
            volatile unsigned char* pages      = allocation.get();
            // Touch every page, retaining all chunks. Merely reserving address
            // space would not exercise the guard's resident-memory watchdog.
            for (size_t offset = 0; offset < CHUNK_BYTES; offset += 4096)
                pages[offset] = 0x5a;
            pages[CHUNK_BYTES - 1] = 0x5a;
            chunks.emplace_back(std::move(allocation));
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 81; // Allocation is bounded even if the watchdog is broken.
    }
    if (mode == "--wrong-parent") {
        // A valid, non-self PID different from the real parent must fail the
        // identity check, rather than the guard's invalid-argument check.
        uint64_t wrong_parent = 1;
        while (wrong_parent == parent_pid || wrong_parent == reconstruction_process_id())
            ++wrong_parent;
        ReconstructionWorkerGuard guard(wrong_parent, 5000, 512);
        return 82;
    }
    if (mode == "--nested-memory-child") {
        ReconstructionWorkerGuard guard(parent_pid, 5000, NESTED_MEMORY_LIMIT_MB);
        const auto                allocation = allocate_resident_memory();
        if (!allocation_fits_own_budget())
            return 86;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return 0;
    }
    return 64;
}

int run_nested_memory_test(const std::string& executable, uint64_t parent_pid, bool include_caller_memory)
{
    using namespace Slic3r;
    ReconstructionWorkerGuard guard(parent_pid, 10000, 512);
    const auto                allocation = allocate_resident_memory();
    if (!allocation_fits_own_budget())
        return 87;
    ReconstructionProcessOptions options;
    options.timeout_ms            = 5000;
    options.memory_limit_mb       = NESTED_MEMORY_LIMIT_MB;
    options.include_caller_memory = include_caller_memory;
    const auto result = run_reconstruction_process(executable, {"--nested-memory-child", std::to_string(reconstruction_process_id())},
                                                   options);
    std::cerr << "nested fixture result: " << reconstruction_process_status_name(result.status) << " / " << result.exit_code << '\n';
    if (include_caller_memory)
        return result.status == ReconstructionProcessStatus::MemoryLimitExceeded && result.exit_code == -1 ? 0 : 88;
    return result.status == ReconstructionProcessStatus::Success && result.exit_code == 0 ? 0 : 89;
}

#if defined(__linux__) || defined(__APPLE__)
int wait_for_parent(uint64_t parent_pid, const std::string& pid_path)
{
    Slic3r::ReconstructionWorkerGuard guard(parent_pid, 10000, 512);
    {
        boost::nowide::ofstream output(pid_path);
        output << Slic3r::reconstruction_process_id() << '\n';
        output.close();
        if (!output)
            return 83;
    }
    // A broken orphan check still cannot leave a long-lived test process. The
    // survivor marker makes this finite fallback distinguishable from the guard.
    std::this_thread::sleep_for(std::chrono::seconds(4));
    boost::nowide::ofstream survived(pid_path + ".survived");
    survived << "worker outlived its parent\n";
    survived.close();
    return 84;
}

int exit_after_spawning_worker(const std::string& executable, const std::string& pid_path, bool include_caller_memory)
{
    Slic3r::ReconstructionProcessOptions options;
    options.timeout_ms            = 6000;
    options.memory_limit_mb       = 512;
    options.include_caller_memory = include_caller_memory;
    options.on_tick               = [&] {
        boost::nowide::ifstream input(pid_path);
        uint64_t                child_pid = 0;
        if (input >> child_pid && child_pid != 0) {
            // Deliberately bypass run_reconstruction_process's RAII cleanup to
            // simulate an abruptly exiting CLI. The child has its own process
            // group, so the outer test's parent runner cannot kill it by group.
            std::_Exit(0);
        }
    };
    Slic3r::run_reconstruction_process(executable, {"--wait-for-parent", std::to_string(Slic3r::reconstruction_process_id()), pid_path},
                                       options);
    return 85;
}

int cancel_nested_worker(const std::string& executable, uint64_t parent_pid, const std::string& pid_path)
{
    using namespace Slic3r;
    ReconstructionWorkerGuard    guard(parent_pid, 10000, 512);
    ReconstructionProcessOptions options;
    options.timeout_ms            = 6000;
    options.memory_limit_mb       = 512;
    options.include_caller_memory = true;
    options.is_canceled           = [&] {
        boost::nowide::ifstream input(pid_path);
        uint64_t                child_pid = 0;
        return bool(input >> child_pid) && child_pid != 0;
    };
    const auto result = run_reconstruction_process(executable,
                                                   {"--wait-for-parent", std::to_string(reconstruction_process_id()), pid_path}, options);
    return result.status == ReconstructionProcessStatus::Canceled ? 0 : 90;
}
#endif

} // namespace

int main(int argc, char** argv)
{
    // The process API's contract is UTF-8 even when the Windows CRT argv is not.
    boost::nowide::args utf8_args(argc, argv);
    try {
        if (argc == 3)
            return run_guard_test(argv[1], parse_pid(argv[2]));
        if (argc == 4 && (std::string(argv[1]) == "--nested-memory" || std::string(argv[1]) == "--nested-memory-legacy"))
            return run_nested_memory_test(argv[2], parse_pid(argv[3]), std::string(argv[1]) == "--nested-memory");
#if defined(__linux__) || defined(__APPLE__)
        if (argc == 4 && (std::string(argv[1]) == "--parent-exit" || std::string(argv[1]) == "--nested-parent-exit"))
            return exit_after_spawning_worker(argv[2], argv[3], std::string(argv[1]) == "--nested-parent-exit");
        if (argc == 4 && std::string(argv[1]) == "--wait-for-parent")
            return wait_for_parent(parse_pid(argv[2]), argv[3]);
        if (argc == 5 && std::string(argv[1]) == "--nested-cancel")
            return cancel_nested_worker(argv[2], parse_pid(argv[3]), argv[4]);
#endif
    } catch (...) {
        return 70;
    }
    return 64;
}
