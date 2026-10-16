#include <catch2/catch.hpp>

#include "libslic3r/ReconstructionProcess.hpp"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>

#if defined(__linux__) || defined(__APPLE__)
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace Slic3r;

TEST_CASE("Reconstruction process rejects invalid budgets before spawning", "[ModelRepair][ReconstructionProcess]")
{
    ReconstructionProcessOptions options;
    options.timeout_ms = 0;
    REQUIRE(run_reconstruction_process("/unused", {}, options).status == ReconstructionProcessStatus::StartFailed);
    options.timeout_ms      = 100;
    options.memory_limit_mb = 0;
    REQUIRE(run_reconstruction_process("/unused", {}, options).status == ReconstructionProcessStatus::StartFailed);
    options.memory_limit_mb = std::numeric_limits<size_t>::max();
    REQUIRE(run_reconstruction_process("/unused", {}, options).status == ReconstructionProcessStatus::StartFailed);
}

TEST_CASE("Reconstruction worker rejects invalid limits without changing this process", "[ModelRepair][ReconstructionProcess]")
{
    // Never lower a limit in the test runner itself; the successful worker-side
    // limit path belongs in the executable's isolated worker integration tests.
    std::string error;
    REQUIRE_FALSE(apply_reconstruction_worker_limits(0, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE_FALSE(apply_reconstruction_worker_limits(std::numeric_limits<size_t>::max(), error));
    REQUIRE_FALSE(error.empty());
}

#ifdef RECONSTRUCTION_TEST_HELPER
TEST_CASE("Reconstruction worker guard stops normally in an isolated process", "[ModelRepair][ReconstructionWorkerGuard]")
{
    ReconstructionProcessOptions options;
    options.timeout_ms      = 5000;
    options.memory_limit_mb = 1024;
    const auto result = run_reconstruction_process(RECONSTRUCTION_TEST_HELPER, {"--normal", std::to_string(reconstruction_process_id())},
                                                   options);
    INFO(result.message);
    REQUIRE(result.status == ReconstructionProcessStatus::Success);
    REQUIRE(result.exit_code == 0);
}

TEST_CASE("Reconstruction worker guard independently enforces its deadline", "[ModelRepair][ReconstructionWorkerGuard]")
{
    ReconstructionProcessOptions options;
    options.timeout_ms      = 5000;
    options.memory_limit_mb = 1024;
    const auto started      = std::chrono::steady_clock::now();
    const auto result = run_reconstruction_process(RECONSTRUCTION_TEST_HELPER, {"--timeout", std::to_string(reconstruction_process_id())},
                                                   options);
    INFO(result.message);
    REQUIRE(result.status == ReconstructionProcessStatus::TimedOut);
    // The parent watchdog returns -1; 124 proves the worker exited on its own.
    REQUIRE(result.exit_code == 124);
    REQUIRE(std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(1000));
}

TEST_CASE("Reconstruction worker guard independently enforces resident memory", "[ModelRepair][ReconstructionWorkerGuard]")
{
    ReconstructionProcessOptions options;
    options.timeout_ms      = 7000;
    options.memory_limit_mb = 512; // The helper's own guard is only 128 MiB.
    const auto result = run_reconstruction_process(RECONSTRUCTION_TEST_HELPER, {"--memory", std::to_string(reconstruction_process_id())},
                                                   options);
    INFO(result.message);
    REQUIRE(result.status == ReconstructionProcessStatus::MemoryLimitExceeded);
    REQUIRE(result.exit_code == 125);
}

TEST_CASE("Reconstruction worker guard rejects the wrong parent identity", "[ModelRepair][ReconstructionWorkerGuard]")
{
    ReconstructionProcessOptions options;
    options.timeout_ms      = 5000;
    options.memory_limit_mb = 1024;
    const auto result       = run_reconstruction_process(RECONSTRUCTION_TEST_HELPER,
                                                         {"--wrong-parent", std::to_string(reconstruction_process_id())}, options);
    INFO(result.message);
    REQUIRE(result.status == ReconstructionProcessStatus::TimedOut);
    REQUIRE(result.exit_code == 124);
}
#endif

#if defined(__linux__) || defined(__APPLE__)
namespace {

class ProcessTestDirectory
{
public:
    ProcessTestDirectory()
    {
        char  pattern[] = "/tmp/slic3r-reconstruction-process-XXXXXX";
        char* path      = mkdtemp(pattern);
        if (path == nullptr)
            throw std::runtime_error("Cannot create a reconstruction process test directory");
        m_path = path;
    }
    ~ProcessTestDirectory()
    {
        unlink(pid_path().c_str());
        unlink(survivor_path().c_str());
        unlink(executable_path().c_str());
        rmdir(m_path.c_str());
    }
    std::string pid_path() const { return m_path + "/worker.pid"; }
    std::string survivor_path() const { return pid_path() + ".survived"; }
    std::string executable_path() const { return m_path + u8"/重建 worker"; }
    pid_t       worker_pid() const
    {
        std::ifstream input(pid_path());
        long          pid = -1;
        input >> pid;
        return input && pid > 0 ? static_cast<pid_t>(pid) : -1;
    }

private:
    std::string m_path;
};

std::vector<std::string> sleeping_worker_args(const ProcessTestDirectory& directory)
{
    // Explicit shell use is limited to this test fixture. The runner itself
    // passes argv directly. Ignoring TERM also exercises the KILL fallback.
    return {"-c", "trap '' TERM; printf '%s\\n' \"$$\" > \"$1\"; exec /bin/sleep 30", "worker", directory.pid_path()};
}

void require_reaped(pid_t pid)
{
    REQUIRE(pid > 0);
    int status = 0;
    errno      = 0;
    REQUIRE(waitpid(pid, &status, WNOHANG) == -1);
    REQUIRE(errno == ECHILD);
    errno = 0;
    REQUIRE(kill(pid, 0) == -1);
    REQUIRE(errno == ESRCH);
}

#ifdef RECONSTRUCTION_TEST_HELPER
bool orphan_worker_has_exited(pid_t pid)
{
    if (kill(pid, 0) != 0)
        return errno == ESRCH;
#ifdef __linux__
    // In containers PID 1 may delay reaping adopted children. A zombie cannot
    // execute or retain the worker's memory and still proves guard termination.
    std::ifstream status("/proc/" + std::to_string(pid) + "/stat");
    std::string   line;
    std::getline(status, line);
    const size_t name_end = line.rfind(')');
    if (name_end != std::string::npos && name_end + 2 < line.size() && line[name_end + 2] == 'Z')
        return true;
#endif
    return false;
}
#endif

} // namespace

TEST_CASE("Reconstruction process preserves exit status and argv", "[ModelRepair][ReconstructionProcess]")
{
    const auto success = run_reconstruction_process("/bin/sh", {"-c", "exit 0"});
    REQUIRE(success.status == ReconstructionProcessStatus::Success);
    REQUIRE(success.exit_code == 0);

    const auto failure = run_reconstruction_process("/bin/sh", {"-c", "exit 7"});
    REQUIRE(failure.status == ReconstructionProcessStatus::Failed);
    REQUIRE(failure.exit_code == 7);

    ProcessTestDirectory directory;
    REQUIRE(symlink("/bin/sh", directory.executable_path().c_str()) == 0);
    const std::string literal   = u8"spaces 中文 $() ; ' \" \\ trailing\\";
    const auto        arguments = run_reconstruction_process(directory.executable_path(),
                                                             {"-c", "test \"$1\" = \"$2\" && test -z \"$3\"", "worker", literal, literal, ""});
    REQUIRE(arguments.status == ReconstructionProcessStatus::Success);
}

TEST_CASE("Reconstruction process does not search PATH or hide start failures", "[ModelRepair][ReconstructionProcess]")
{
    REQUIRE(run_reconstruction_process("/nonexistent/reconstruction-worker", {}).status == ReconstructionProcessStatus::StartFailed);
    REQUIRE(run_reconstruction_process("sh", {}).status == ReconstructionProcessStatus::StartFailed);
    REQUIRE(run_reconstruction_process("/bin/sh", {std::string("a\0b", 3)}).status == ReconstructionProcessStatus::StartFailed);
}

TEST_CASE("Fast reconstruction children retain their real exit status", "[ModelRepair][ReconstructionProcess]")
{
    // Exercise exec/startup and the task-info disappearance window repeatedly.
    // A transient missing RSS sample must not replace a completed child's status.
    for (int iteration = 0; iteration < 40; ++iteration) {
        const int code = iteration % 2 == 0 ? 0 : 7;
        const auto result = run_reconstruction_process("/bin/sh", {"-c", "exit " + std::to_string(code)});
        INFO(iteration);
        REQUIRE(result.status == (code == 0 ? ReconstructionProcessStatus::Success : ReconstructionProcessStatus::Failed));
        REQUIRE(result.exit_code == code);
    }
}

TEST_CASE("Reconstruction process honors cancellation before startup", "[ModelRepair][ReconstructionProcess]")
{
    ReconstructionProcessOptions options;
    options.is_canceled = [] { return true; };
    // A missing executable proves the canceled operation never tried to start.
    REQUIRE(run_reconstruction_process("/nonexistent/reconstruction-worker", {}, options).status == ReconstructionProcessStatus::Canceled);
}

TEST_CASE("Reconstruction process timeout kills and reaps the worker", "[ModelRepair][ReconstructionProcess]")
{
    ProcessTestDirectory         directory;
    ReconstructionProcessOptions options;
    options.timeout_ms = 500;
    const auto result  = run_reconstruction_process("/bin/sh", sleeping_worker_args(directory), options);
    REQUIRE(result.status == ReconstructionProcessStatus::TimedOut);
    require_reaped(directory.worker_pid());
}

TEST_CASE("Reconstruction process cancellation pumps the caller and reaps the worker", "[ModelRepair][ReconstructionProcess]")
{
    ProcessTestDirectory         directory;
    ReconstructionProcessOptions options;
    unsigned                     ticks = 0;
    options.timeout_ms                 = 5000;
    options.on_tick                    = [&] { ++ticks; };
    options.is_canceled                = [&] { return ticks >= 3 && directory.worker_pid() > 0; };
    const auto result                  = run_reconstruction_process("/bin/sh", sleeping_worker_args(directory), options);
    REQUIRE(result.status == ReconstructionProcessStatus::Canceled);
    REQUIRE(ticks >= 3);
    require_reaped(directory.worker_pid());
}

TEST_CASE("Reconstruction process callback exceptions cannot leak a worker", "[ModelRepair][ReconstructionProcess]")
{
    ProcessTestDirectory         directory;
    ReconstructionProcessOptions options;
    options.timeout_ms = 5000;
    SECTION("on_tick")
    {
        options.on_tick = [&] {
            if (directory.worker_pid() > 0)
                throw std::runtime_error("tick failure");
        };
        REQUIRE_THROWS_WITH(run_reconstruction_process("/bin/sh", sleeping_worker_args(directory), options), "tick failure");
    }
    SECTION("is_canceled")
    {
        options.is_canceled = [&]() -> bool {
            if (directory.worker_pid() > 0)
                throw std::runtime_error("cancel failure");
            return false;
        };
        REQUIRE_THROWS_WITH(run_reconstruction_process("/bin/sh", sleeping_worker_args(directory), options), "cancel failure");
    }
    require_reaped(directory.worker_pid());
}

TEST_CASE("Reconstruction process observes and stops excessive resident memory", "[ModelRepair][ReconstructionProcess]")
{
    ReconstructionProcessOptions options;
    options.timeout_ms      = 5000;
    options.memory_limit_mb = 16;
    // Keep an approximately 32 MiB shell variable alive while waiting. This
    // intentionally exercises the parent's RSS watchdog without a worker-side
    // RLIMIT_AS, and uses no Python or other optional runtime.
    const auto result = run_reconstruction_process(
        "/bin/sh", {"-c", "value=x; i=0; while [ \"$i\" -lt 25 ]; do value=\"$value$value\"; i=$((i+1)); done; /bin/sleep 30"}, options);
    REQUIRE(result.status == ReconstructionProcessStatus::MemoryLimitExceeded);
}

#ifdef RECONSTRUCTION_TEST_HELPER
TEST_CASE("Reconstruction worker guard cannot outlive an abruptly exiting parent", "[ModelRepair][ReconstructionWorkerGuard]")
{
    ProcessTestDirectory         directory;
    ReconstructionProcessOptions options;
    options.timeout_ms      = 7000;
    options.memory_limit_mb = 1024;
    const auto parent       = run_reconstruction_process(RECONSTRUCTION_TEST_HELPER,
                                                         {"--parent-exit", RECONSTRUCTION_TEST_HELPER, directory.pid_path()}, options);
    INFO(parent.message);
    REQUIRE(parent.status == ReconstructionProcessStatus::Success);
    const pid_t child_pid = directory.worker_pid();
    REQUIRE(child_pid > 0);

    // Never signal a PID read from a file: a recycled PID might identify an
    // unrelated process. The test-owned helper has its own four-second fallback
    // and marks that path, so even a failed guard leaves no long-running helper.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    bool       exited   = false;
    do {
        exited = orphan_worker_has_exited(child_pid);
        if (!exited)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (!exited && std::chrono::steady_clock::now() < deadline);
    REQUIRE(exited);
    REQUIRE(access(directory.survivor_path().c_str(), F_OK) != 0);
    REQUIRE(errno == ENOENT);
}
#endif

#endif
