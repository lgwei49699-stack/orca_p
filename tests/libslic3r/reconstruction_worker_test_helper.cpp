#include "libslic3r/ReconstructionProcess.hpp"

#include <boost/nowide/args.hpp>
#include <boost/nowide/fstream.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

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
    return 64;
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

int exit_after_spawning_worker(const std::string& executable, const std::string& pid_path)
{
    Slic3r::ReconstructionProcessOptions options;
    options.timeout_ms      = 6000;
    options.memory_limit_mb = 512;
    options.on_tick         = [&] {
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
#endif

} // namespace

int main(int argc, char** argv)
{
    // The process API's contract is UTF-8 even when the Windows CRT argv is not.
    boost::nowide::args utf8_args(argc, argv);
    try {
        if (argc == 3)
            return run_guard_test(argv[1], parse_pid(argv[2]));
#if defined(__linux__) || defined(__APPLE__)
        if (argc == 4 && std::string(argv[1]) == "--parent-exit")
            return exit_after_spawning_worker(argv[2], argv[3]);
        if (argc == 4 && std::string(argv[1]) == "--wait-for-parent")
            return wait_for_parent(parse_pid(argv[2]), argv[3]);
#endif
    } catch (...) {
        return 70;
    }
    return 64;
}
