#include "ReconstructionProcess.hpp"
#include "I18N.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#include <libproc.h>
#else
#include <sys/prctl.h>
extern char** environ;
#endif
#endif

namespace Slic3r {
namespace {

using Clock                  = std::chrono::steady_clock;
constexpr auto POLL_INTERVAL = std::chrono::milliseconds(20);

bool memory_limit_bytes(size_t memory_limit_mb, size_t& bytes)
{
    constexpr size_t BYTES_PER_MB = 1024 * 1024;
    if (memory_limit_mb == 0 || memory_limit_mb > std::numeric_limits<size_t>::max() / BYTES_PER_MB)
        return false;
    bytes = memory_limit_mb * BYTES_PER_MB;
    return true;
}

bool has_nul(const std::string& value) { return value.find('\0') != std::string::npos; }

ReconstructionProcessResult failure(ReconstructionProcessStatus status, const std::string& message) { return {status, -1, message}; }

ReconstructionProcessResult process_exit_result(int exit_code)
{
    switch (exit_code) {
    case 0: return {ReconstructionProcessStatus::Success, 0, ""};
    case 124: return {ReconstructionProcessStatus::TimedOut, exit_code, L("The worker supervision deadline was exceeded.")};
    case 125: return {ReconstructionProcessStatus::MemoryLimitExceeded, exit_code, L("The worker exceeded its memory limit.")};
    case 126: return {ReconstructionProcessStatus::MonitorFailed, exit_code, L("The worker could not continue resource monitoring.")};
    default: return {ReconstructionProcessStatus::Failed, exit_code, L("The worker exited unsuccessfully.")};
    }
}

#ifdef _WIN32

class UniqueHandle
{
public:
    explicit UniqueHandle(HANDLE handle = nullptr) : m_handle(handle) {}
    ~UniqueHandle()
    {
        if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
    }
    UniqueHandle(const UniqueHandle&)            = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    HANDLE        get() const { return m_handle; }
    void          reset(HANDLE handle)
    {
        if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
        m_handle = handle;
    }

private:
    HANDLE m_handle;
};

std::string windows_error(const char* operation)
{
    return std::string(operation) + L(" failed (Windows error ") + std::to_string(GetLastError()) + ").";
}

bool from_utf8(const std::string& source, std::wstring& result)
{
    if (source.empty()) {
        result.clear();
        return true;
    }
    if (source.size() > size_t(std::numeric_limits<int>::max()))
        return false;
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(), int(source.size()), nullptr, 0);
    if (length == 0)
        return false;
    result.resize(size_t(length));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(), int(source.size()), &result[0], length) == length;
}

// Microsoft CRT argv quoting, including empty arguments and trailing backslashes.
std::wstring quote_argument(const std::wstring& argument)
{
    std::wstring result(1, L'"');
    size_t       backslashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        result.append(character == L'"' ? 2 * backslashes + 1 : backslashes, L'\\');
        result.push_back(character);
        backslashes = 0;
    }
    result.append(2 * backslashes, L'\\');
    result.push_back(L'"');
    return result;
}

class WindowsChild
{
public:
    WindowsChild(HANDLE process, HANDLE job) : m_process(process), m_job(job) {}
    ~WindowsChild()
    {
        if (!m_finished) {
            // Also handles failures between CreateProcess and AssignProcessToJobObject.
            TerminateJobObject(m_job, ERROR_PROCESS_ABORTED);
            TerminateProcess(m_process, ERROR_PROCESS_ABORTED);
            WaitForSingleObject(m_process, INFINITE);
        }
    }
    WindowsChild(const WindowsChild&)            = delete;
    WindowsChild& operator=(const WindowsChild&) = delete;
    void          finished() { m_finished = true; }

private:
    HANDLE m_process;
    HANDLE m_job;
    bool   m_finished = false;
};

bool resident_bytes(HANDLE process, uint64_t& bytes)
{
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(process, &counters, sizeof(counters)))
        return false;
    bytes = counters.WorkingSetSize;
    return true;
}

ReconstructionProcessResult run_platform_process(const std::string&                  executable,
                                                 const std::vector<std::string>&     args,
                                                 const ReconstructionProcessOptions& options,
                                                 size_t                              limit_bytes)
{
    std::wstring executable_wide;
    if (!from_utf8(executable, executable_wide))
        return failure(ReconstructionProcessStatus::StartFailed, L("The worker executable path is not valid UTF-8."));
    std::wstring command = quote_argument(executable_wide);
    for (const std::string& argument : args) {
        std::wstring wide;
        if (!from_utf8(argument, wide))
            return failure(ReconstructionProcessStatus::StartFailed, L("A worker argument is not valid UTF-8."));
        command += L' ';
        command += quote_argument(wide);
    }

    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (job.get() == nullptr)
        return failure(ReconstructionProcessStatus::StartFailed, windows_error("CreateJobObject"));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY |
                                              JOB_OBJECT_LIMIT_JOB_MEMORY;
    limits.ProcessMemoryLimit = limit_bytes;
    limits.JobMemoryLimit     = limit_bytes;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return failure(ReconstructionProcessStatus::StartFailed, windows_error(L("Setting worker memory limits")));

    UniqueHandle completion_port(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1));
    if (completion_port.get() == nullptr)
        return failure(ReconstructionProcessStatus::StartFailed, windows_error("CreateIoCompletionPort"));
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT association{};
    association.CompletionKey  = job.get();
    association.CompletionPort = completion_port.get();
    if (!SetInformationJobObject(job.get(), JobObjectAssociateCompletionPortInformation, &association, sizeof(association)))
        return failure(ReconstructionProcessStatus::StartFailed, windows_error(L("Associating worker memory notifications")));

    STARTUPINFOW        startup{};
    PROCESS_INFORMATION info{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(executable_wide.c_str(), &command[0], nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                        nullptr, &startup, &info))
        return failure(ReconstructionProcessStatus::StartFailed, windows_error("CreateProcess"));
    UniqueHandle process(info.hProcess);
    UniqueHandle thread(info.hThread);
    WindowsChild child(process.get(), job.get());
    if (!AssignProcessToJobObject(job.get(), process.get()))
        return failure(ReconstructionProcessStatus::StartFailed, windows_error("AssignProcessToJobObject"));
    if (ResumeThread(thread.get()) == DWORD(-1))
        return failure(ReconstructionProcessStatus::StartFailed, windows_error("ResumeThread"));

    const auto deadline = Clock::now() + std::chrono::milliseconds(options.timeout_ms);
    unsigned   memory_query_failures = 0;
    for (;;) {
        DWORD        notification = 0;
        ULONG_PTR    key          = 0;
        LPOVERLAPPED value        = nullptr;
        while (GetQueuedCompletionStatus(completion_port.get(), &notification, &key, &value, 0)) {
            if (notification == JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT || notification == JOB_OBJECT_MSG_JOB_MEMORY_LIMIT)
                return failure(ReconstructionProcessStatus::MemoryLimitExceeded, L("The worker exceeded its memory limit."));
        }
        if (GetLastError() != WAIT_TIMEOUT)
            return failure(ReconstructionProcessStatus::MonitorFailed, windows_error(L("Reading worker memory notifications")));
        const DWORD wait = WaitForSingleObject(process.get(), 0);
        if (wait == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            if (!GetExitCodeProcess(process.get(), &exit_code))
                return failure(ReconstructionProcessStatus::MonitorFailed, windows_error("GetExitCodeProcess"));
            child.finished();
            return process_exit_result(static_cast<int>(exit_code));
        }
        if (wait != WAIT_TIMEOUT)
            return failure(ReconstructionProcessStatus::MonitorFailed, windows_error(L("Waiting for the worker")));
        if (options.is_canceled && options.is_canceled())
            return failure(ReconstructionProcessStatus::Canceled, L("Reconstruction was canceled."));
        if (Clock::now() >= deadline)
            return failure(ReconstructionProcessStatus::TimedOut, L("Reconstruction exceeded its time limit."));
        if (options.include_caller_memory) {
            uint64_t caller_rss = 0;
            uint64_t child_rss  = 0;
            if (!resident_bytes(GetCurrentProcess(), caller_rss) || caller_rss == 0)
                return failure(ReconstructionProcessStatus::MonitorFailed, L("Cannot observe reconstruction worker memory usage."));
            if (!resident_bytes(process.get(), child_rss) || child_rss == 0) {
                // A child may have exited between the wait and RSS query. Only
                // its confirmed exit may replace a missing observation.
                if (WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0) {
                    DWORD exit_code = 0;
                    if (GetExitCodeProcess(process.get(), &exit_code)) {
                        child.finished();
                        return process_exit_result(static_cast<int>(exit_code));
                    }
                }
                if (++memory_query_failures >= 3)
                    return failure(ReconstructionProcessStatus::MonitorFailed, L("Cannot observe reconstruction worker memory usage."));
                std::this_thread::sleep_for(POLL_INTERVAL);
                continue;
            }
            memory_query_failures = 0;
            // Subtraction avoids overflow even if the OS reports an unexpected
            // value. Shared mappings may be counted twice: this is conservative.
            if (caller_rss > limit_bytes || child_rss > limit_bytes - caller_rss)
                return failure(ReconstructionProcessStatus::MemoryLimitExceeded, L("The worker exceeded its memory limit."));
        }
        if (options.on_tick)
            options.on_tick();
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
}

#elif defined(__linux__) || defined(__APPLE__)

class PosixChild
{
public:
    explicit PosixChild(pid_t pid) : m_pid(pid) {}
    ~PosixChild()
    {
        if (m_pid <= 0)
            return;
        // posix_spawn created a dedicated process group; terminate descendants too.
        kill(-m_pid, SIGTERM);
        const auto deadline = Clock::now() + std::chrono::milliseconds(250);
        int        status   = 0;
        while (Clock::now() < deadline) {
            // Keep an exited leader waitable until after killing the group. This
            // prevents PID reuse and also cleans up children that ignored TERM.
            siginfo_t info{};
            if (waitid(P_PID, id_t(m_pid), &info, WEXITED | WNOHANG | WNOWAIT) == 0) {
                if (info.si_pid == m_pid)
                    break;
            } else if (errno == ECHILD) {
                m_pid = -1;
                return;
            } else if (errno != EINTR) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (m_pid > 0) {
            kill(-m_pid, SIGKILL);
            while (waitpid(m_pid, &status, 0) < 0 && errno == EINTR) {}
            m_pid = -1;
        }
    }
    PosixChild(const PosixChild&)            = delete;
    PosixChild& operator=(const PosixChild&) = delete;

    bool poll(int& status, int& error)
    {
        error = 0;
        pid_t result;
        do {
            result = waitpid(m_pid, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == m_pid) {
            m_pid = -1;
            return true;
        }
        if (result < 0) {
            error = errno;
            if (error == ECHILD)
                m_pid = -1;
        }
        return false;
    }

private:
    pid_t m_pid;
};

bool resident_bytes(pid_t pid, uint64_t& bytes)
{
#ifdef __APPLE__
    proc_taskinfo info{};
    if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &info, sizeof(info)) != sizeof(info))
        return false;
    bytes = info.pti_resident_size;
    return true;
#else
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        return false;
    const std::string path = "/proc/" + std::to_string(pid) + "/statm";
    FILE*             file = std::fopen(path.c_str(), "r");
    if (file == nullptr)
        return false;
    unsigned long long total_pages = 0;
    unsigned long long rss_pages   = 0;
    const bool         parsed      = std::fscanf(file, "%llu %llu", &total_pages, &rss_pages) == 2;
    std::fclose(file);
    if (!parsed || rss_pages > std::numeric_limits<uint64_t>::max() / uint64_t(page_size))
        return false;
    bytes = rss_pages * uint64_t(page_size);
    return true;
#endif
}

ReconstructionProcessResult completed_process(int status)
{
    if (WIFEXITED(status)) {
        const int exit_code = WEXITSTATUS(status);
        return process_exit_result(exit_code);
    }
    if (WIFSIGNALED(status))
        return {ReconstructionProcessStatus::Failed, 128 + WTERMSIG(status), L("The worker was terminated by a signal.")};
    return failure(ReconstructionProcessStatus::MonitorFailed, L("The worker returned an unrecognized wait status."));
}

ReconstructionProcessResult run_platform_process(const std::string&                  executable,
                                                 const std::vector<std::string>&     args,
                                                 const ReconstructionProcessOptions& options,
                                                 size_t                              limit_bytes)
{
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const std::string& argument : args)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);

    posix_spawnattr_t attributes;
    int               error = posix_spawnattr_init(&attributes);
    if (error != 0)
        return failure(ReconstructionProcessStatus::StartFailed,
                       L("Cannot initialize worker attributes: ") + std::string(std::strerror(error)));
    // Reset inherited signal masks/handlers so a GUI's signal setup cannot hide cancellation.
    sigset_t signal_mask;
    sigset_t signal_defaults;
    sigemptyset(&signal_mask);
    sigemptyset(&signal_defaults);
    sigaddset(&signal_defaults, SIGTERM);
    sigaddset(&signal_defaults, SIGINT);
    error = posix_spawnattr_setsigmask(&attributes, &signal_mask);
    if (error == 0)
        error = posix_spawnattr_setsigdefault(&attributes, &signal_defaults);
    if (error == 0)
        error = posix_spawnattr_setpgroup(&attributes, 0);
    if (error == 0)
        error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    pid_t pid = -1;
#ifdef __APPLE__
    char** environment = *_NSGetEnviron();
#else
    char** environment = environ;
#endif
    if (error == 0)
        error = posix_spawn(&pid, executable.c_str(), nullptr, &attributes, argv.data(), environment);
    posix_spawnattr_destroy(&attributes);
    if (error != 0)
        return failure(ReconstructionProcessStatus::StartFailed,
                       L("Cannot start reconstruction worker: ") + std::string(std::strerror(error)));
    PosixChild child(pid);

    const auto deadline = Clock::now() + std::chrono::milliseconds(options.timeout_ms);
    unsigned   memory_query_failures = 0;
    for (;;) {
        int status = 0;
        if (child.poll(status, error))
            return completed_process(status);
        if (error != 0)
            return failure(ReconstructionProcessStatus::MonitorFailed,
                           L("Cannot wait for reconstruction worker: ") + std::string(std::strerror(error)));
        if (options.is_canceled && options.is_canceled())
            return failure(ReconstructionProcessStatus::Canceled, L("Reconstruction was canceled."));
        if (Clock::now() >= deadline)
            return failure(ReconstructionProcessStatus::TimedOut, L("Reconstruction exceeded its time limit."));
        uint64_t caller_rss = 0;
        if (options.include_caller_memory && (!resident_bytes(getpid(), caller_rss) || caller_rss == 0))
            return failure(ReconstructionProcessStatus::MonitorFailed, L("Cannot observe reconstruction worker memory usage."));
        uint64_t rss = 0;
        if (!resident_bytes(pid, rss) || (options.include_caller_memory && rss == 0)) {
            // proc_pidinfo can disappear before waitpid reports a dying child.
            // Allow only a bounded observation grace period, never restart the
            // worker or treat missing RSS as zero. Cancellation/deadline remain active.
            if (child.poll(status, error))
                return completed_process(status);
            if (error != 0 || ++memory_query_failures >= 3)
                return failure(ReconstructionProcessStatus::MonitorFailed, L("Cannot observe reconstruction worker memory usage."));
            std::this_thread::sleep_for(POLL_INTERVAL);
            continue;
        }
        memory_query_failures = 0;
        // In nested mode, both workers share the same budget; do not add first
        // because an invalid large RSS value could overflow the total.
        if (caller_rss > limit_bytes || rss > limit_bytes - caller_rss)
            return failure(ReconstructionProcessStatus::MemoryLimitExceeded, L("The worker exceeded its memory limit."));
        if (options.on_tick)
            options.on_tick();
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
}

#endif

} // namespace

uint64_t reconstruction_process_id()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#elif defined(__linux__) || defined(__APPLE__)
    return static_cast<uint64_t>(getpid());
#else
    return 0;
#endif
}

struct ReconstructionWorkerGuard::Impl
{
    std::atomic<bool> stop{false};
    std::thread       supervisor;
    uint64_t          expected_parent = 0;
    size_t            limit_bytes     = 0;
    Clock::time_point deadline;
#ifdef _WIN32
    UniqueHandle parent;
#endif

    Impl(uint64_t parent_pid, unsigned timeout_ms, size_t memory_limit_mb)
        : expected_parent(parent_pid), deadline(Clock::now() + std::chrono::milliseconds(uint64_t(timeout_ms) + 1000))
    {
        if (parent_pid == 0 || parent_pid == reconstruction_process_id() || timeout_ms == 0 ||
            !memory_limit_bytes(memory_limit_mb, limit_bytes))
            throw std::invalid_argument(L("Invalid reconstruction worker supervision parameters."));
#ifdef _WIN32
        if (parent_pid > std::numeric_limits<DWORD>::max())
            throw std::invalid_argument(L("Invalid reconstruction worker parent process ID."));
        parent.reset(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(parent_pid)));
        if (parent.get() == nullptr)
            std::_Exit(124);
        // A fixed handle prevents PID reuse after acquisition. Also confirm that
        // the requested PID is this worker's actual parent, not an arbitrary live
        // process, and reject a recycled PID created after this worker.
        UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (snapshot.get() == INVALID_HANDLE_VALUE)
            throw std::runtime_error(windows_error(L("Identifying the worker parent")));
        PROCESSENTRY32W entry{};
        entry.dwSize    = sizeof(entry);
        bool identified = false;
        if (Process32FirstW(snapshot.get(), &entry)) {
            do {
                if (entry.th32ProcessID == GetCurrentProcessId()) {
                    if (entry.th32ParentProcessID != DWORD(parent_pid))
                        std::_Exit(124);
                    identified = true;
                    break;
                }
            } while (Process32NextW(snapshot.get(), &entry));
        }
        FILETIME parent_created{}, worker_created{}, exited{}, kernel{}, user{};
        if (!identified || !GetProcessTimes(parent.get(), &parent_created, &exited, &kernel, &user) ||
            !GetProcessTimes(GetCurrentProcess(), &worker_created, &exited, &kernel, &user))
            throw std::runtime_error(L("Cannot verify the reconstruction worker parent identity."));
        if (CompareFileTime(&parent_created, &worker_created) > 0)
            std::_Exit(124);
#elif defined(__linux__) || defined(__APPLE__)
        if (parent_pid > uint64_t(std::numeric_limits<pid_t>::max()))
            throw std::invalid_argument(L("Invalid reconstruction worker parent process ID."));
        if (uint64_t(getppid()) != expected_parent)
            std::_Exit(124);
#ifdef __linux__
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0)
            throw std::runtime_error(L("Cannot install reconstruction worker parent-death protection."));
        // Close the race where the parent exited just before prctl took effect.
        if (uint64_t(getppid()) != expected_parent)
            std::_Exit(124);
#endif
#else
        throw std::runtime_error(L("Worker supervision is unsupported on this platform."));
#endif
        check();
        supervisor = std::thread([this] {
            try {
                while (!stop.load()) {
                    check();
                    std::this_thread::sleep_for(POLL_INTERVAL);
                }
            } catch (const std::bad_alloc&) {
                std::_Exit(125);
            } catch (...) {
                std::_Exit(126);
            }
        });
    }

    ~Impl()
    {
        stop.store(true);
        if (supervisor.joinable())
            supervisor.join();
    }

    void check() const
    {
        if (Clock::now() >= deadline)
            std::_Exit(124);
        uint64_t rss = 0;
#ifdef _WIN32
        const DWORD wait = WaitForSingleObject(parent.get(), 0);
        if (wait == WAIT_OBJECT_0)
            std::_Exit(124);
        if (wait != WAIT_TIMEOUT)
            std::_Exit(126);
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            std::_Exit(126);
        rss = counters.WorkingSetSize;
#elif defined(__linux__) || defined(__APPLE__)
        // getppid changes on orphan reparenting and cannot accidentally accept a
        // recycled process with the requested numeric PID as this one's parent.
        if (uint64_t(getppid()) != expected_parent || kill(pid_t(expected_parent), 0) != 0)
            std::_Exit(124);
        if (!resident_bytes(getpid(), rss))
            std::_Exit(126);
#else
        std::_Exit(126);
#endif
        if (rss > limit_bytes)
            std::_Exit(125);
    }
};

ReconstructionWorkerGuard::ReconstructionWorkerGuard(uint64_t parent_pid, unsigned timeout_ms, size_t memory_limit_mb)
    : m_impl(std::make_unique<Impl>(parent_pid, timeout_ms, memory_limit_mb))
{}

ReconstructionWorkerGuard::~ReconstructionWorkerGuard() = default;

const char* reconstruction_process_status_name(ReconstructionProcessStatus status)
{
    switch (status) {
    case ReconstructionProcessStatus::Success: return "success";
    case ReconstructionProcessStatus::Failed: return "failed";
    case ReconstructionProcessStatus::Canceled: return "canceled";
    case ReconstructionProcessStatus::TimedOut: return "timed_out";
    case ReconstructionProcessStatus::StartFailed: return "start_failed";
    case ReconstructionProcessStatus::MemoryLimitExceeded: return "memory_limit_exceeded";
    case ReconstructionProcessStatus::MonitorFailed: return "monitor_failed";
    }
    return "unknown";
}

ReconstructionProcessResult run_reconstruction_process(const std::string&                  executable,
                                                       const std::vector<std::string>&     args,
                                                       const ReconstructionProcessOptions& options)
{
    size_t limit_bytes = 0;
    if (executable.empty() || has_nul(executable) ||
        std::any_of(args.begin(), args.end(), [](const std::string& arg) { return has_nul(arg); }))
        return failure(ReconstructionProcessStatus::StartFailed, L("The worker executable or arguments are invalid."));
#ifdef _WIN32
    const bool drive_absolute = executable.size() >= 3 && executable[1] == ':' && (executable[2] == '\\' || executable[2] == '/');
    const bool unc_absolute   = executable.size() >= 2 &&
                              ((executable[0] == '\\' && executable[1] == '\\') || (executable[0] == '/' && executable[1] == '/'));
    if (!drive_absolute && !unc_absolute)
#else
    if (executable.front() != '/')
#endif
        return failure(ReconstructionProcessStatus::StartFailed, L("The worker executable path must be absolute."));
    if (options.timeout_ms == 0 || !memory_limit_bytes(options.memory_limit_mb, limit_bytes))
        return failure(ReconstructionProcessStatus::StartFailed, L("Worker time and memory limits must be positive and representable."));
    if (options.is_canceled && options.is_canceled())
        return failure(ReconstructionProcessStatus::Canceled, L("Reconstruction was canceled before starting the worker."));
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    if (options.include_caller_memory) {
        uint64_t caller_rss = 0;
#ifdef _WIN32
        const bool observed = resident_bytes(GetCurrentProcess(), caller_rss);
#else
        const bool observed = resident_bytes(getpid(), caller_rss);
#endif
        if (!observed || caller_rss == 0)
            return failure(ReconstructionProcessStatus::MonitorFailed, L("Cannot observe reconstruction worker memory usage."));
        if (caller_rss > limit_bytes)
            return failure(ReconstructionProcessStatus::MemoryLimitExceeded, L("The worker exceeded its memory limit."));
    }
    return run_platform_process(executable, args, options, limit_bytes);
#else
    return failure(ReconstructionProcessStatus::StartFailed, L("Reconstruction process limits are unsupported on this platform."));
#endif
}

bool apply_reconstruction_worker_limits(size_t memory_limit_mb, std::string& error)
{
    error.clear();
    size_t bytes = 0;
    if (!memory_limit_bytes(memory_limit_mb, bytes)) {
        error = L("The worker memory limit must be positive and representable.");
        return false;
    }
#ifdef _WIN32
    BOOL in_job = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &in_job) || !in_job) {
        error = L("The reconstruction worker is not running in a memory-limited Job.");
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr)) {
        error = windows_error(L("Querying worker memory limits"));
        return false;
    }
    const DWORD required = JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if ((limits.BasicLimitInformation.LimitFlags & required) != required || limits.ProcessMemoryLimit == 0 || limits.JobMemoryLimit == 0 ||
        limits.ProcessMemoryLimit > bytes || limits.JobMemoryLimit > bytes) {
        error = L("The reconstruction worker Job does not enforce the requested memory limit.");
        return false;
    }
    return true;
#elif defined(__APPLE__)
    // Darwin's large shared-cache/ASLR mappings make RLIMIT_AS unsuitable for
    // expressing an RSS budget. Do not claim that this check is a hard limit.
    uint64_t rss = 0;
    if (!resident_bytes(getpid(), rss)) {
        error = L("Cannot observe reconstruction worker memory usage.");
        return false;
    }
    if (rss > bytes) {
        error = L("The reconstruction worker already exceeds its memory budget.");
        return false;
    }
    return true;
#elif defined(__linux__)
    if (uint64_t(bytes) >= uint64_t(RLIM_INFINITY)) {
        error = L("The worker memory limit exceeds the platform resource-limit range.");
        return false;
    }
    rlimit limits{};
    if (getrlimit(RLIMIT_AS, &limits) != 0) {
        error = L("Cannot read worker address-space limit: ") + std::string(std::strerror(errno));
        return false;
    }
    const rlim_t requested = static_cast<rlim_t>(bytes);
    limits.rlim_cur        = std::min(requested, limits.rlim_cur);
    limits.rlim_max        = std::min(requested, limits.rlim_max);
    if (setrlimit(RLIMIT_AS, &limits) != 0) {
        error = L("Cannot set worker address-space limit: ") + std::string(std::strerror(errno));
        return false;
    }
    return true;
#else
    error = L("Reconstruction worker memory limits are unsupported on this platform.");
    return false;
#endif
}

} // namespace Slic3r
