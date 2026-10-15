#ifndef SLIC3R_RECONSTRUCTION_PROCESS_HPP
#define SLIC3R_RECONSTRUCTION_PROCESS_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {

enum class ReconstructionProcessStatus { Success, Failed, Canceled, TimedOut, StartFailed, MemoryLimitExceeded, MonitorFailed };

struct ReconstructionProcessOptions
{
    unsigned              timeout_ms      = 60000;
    size_t                memory_limit_mb = 1024;
    std::function<bool()> is_canceled;
    // Called synchronously on the caller's thread. Keep this callback short.
    // Callback exceptions terminate and reap the worker before propagating.
    std::function<void()> on_tick;
    // Only for a disposable worker launching a single leaf sidecar. Count this
    // caller and its direct child against one RSS budget; this does not monitor
    // an arbitrary process tree. The ordinary parent/worker path stays unchanged.
    bool include_caller_memory = false;
};

struct ReconstructionProcessResult
{
    ReconstructionProcessStatus status    = ReconstructionProcessStatus::StartFailed;
    int                         exit_code = -1;
    std::string                 message;
};

const char* reconstruction_process_status_name(ReconstructionProcessStatus status);

uint64_t reconstruction_process_id();

// Only construct in the dedicated, disposable reconstruction worker. Its
// supervision thread exits the entire process without unwinding if the expected
// parent disappears or its identity is wrong (124), the timeout plus a 1000 ms
// grace elapses (124), RSS exceeds the budget (125), or monitoring fails (126).
// Linux also asks the kernel to SIGKILL the worker when its parent dies. macOS
// RSS monitoring remains a sampled budget, not a hard allocation cap. Normal
// destruction stops and joins the thread. Never construct this in the GUI, CLI
// parent or a test runner; even guard tests need a separate disposable process.
class ReconstructionWorkerGuard
{
public:
    ReconstructionWorkerGuard(uint64_t parent_pid, unsigned timeout_ms, size_t memory_limit_mb);
    ~ReconstructionWorkerGuard();
    ReconstructionWorkerGuard(const ReconstructionWorkerGuard&)            = delete;
    ReconstructionWorkerGuard& operator=(const ReconstructionWorkerGuard&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Run an executable directly, with UTF-8 paths/arguments and no shell or PATH lookup.
// args excludes argv[0]. Both limits must be positive. This call owns and waits for
// the child; cancellation, timeout, monitor failure and exceptions terminate it.
// Linux uses worker-side RLIMIT_AS plus an RSS watchdog. macOS uses only the RSS
// watchdog: it is a sampled cancellation budget, not a hard allocation limit.
// Windows places the suspended child in a memory-limited, kill-on-close Job first.
// include_caller_memory additionally samples caller + child RSS on all platforms;
// inherited Windows ancestor Job limits are retained (no breakaway is requested).
ReconstructionProcessResult run_reconstruction_process(const std::string&                  executable,
                                                       const std::vector<std::string>&     args,
                                                       const ReconstructionProcessOptions& options = {});

// Call only in the dedicated worker, before parsing its model or doing geometry.
// Linux lowers RLIMIT_AS; Windows verifies the parent's Job memory limit. macOS
// checks the starting RSS; keep both parent watchdog and WorkerGuard active.
// Failure must abort the worker. This function must never run in the GUI process.
bool apply_reconstruction_worker_limits(size_t memory_limit_mb, std::string& error);

} // namespace Slic3r

#endif
