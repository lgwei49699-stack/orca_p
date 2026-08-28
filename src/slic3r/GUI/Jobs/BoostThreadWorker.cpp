#include <exception>

#include <wx/app.h>

#include "BoostThreadWorker.hpp"

namespace Slic3r { namespace GUI {

void BoostThreadWorker::WorkerMessage::deliver(BoostThreadWorker &runner)
{
    switch(MsgType(get_type())) {
    case Empty: break;
    case Status: {
        auto info = boost::get<StatusInfo>(m_data);
        if (runner.get_pri()) {
            runner.get_pri()->set_progress(info.status);
            runner.get_pri()->set_status_text(info.msg.c_str());
        }
        break;
    }
    case Finalize: {
        auto& entry = boost::get<JobEntry>(m_data);
        entry.job->finalize(entry.canceled, entry.eptr);

        // Unhandled exceptions are rethrown without mercy.
        if (entry.eptr)
            std::rethrow_exception(entry.eptr);

        break;
    }
    case MainThreadCall: {
        auto &calldata = boost::get<MainThreadCallData >(m_data);
        calldata.fn();
        calldata.promise.set_value();

        break;
    }
    }
}

void BoostThreadWorker::notify_main_thread()
{
    if (!wxTheApp || m_ui_dispatch_pending.exchange(true))
        return;

    // CallAfter queues a real, thread-safe wx event. Unlike wxWakeUpIdle(),
    // it does not depend on an idle handler being reached through a modal
    // event loop. The weak token prevents a queued callback from accessing a
    // worker that has already begun destruction.
    const std::weak_ptr<void> lifetime = m_ui_lifetime_weak;
    wxTheApp->CallAfter([this, lifetime]() {
        const std::shared_ptr<void> keep_alive = lifetime.lock();
        if (!keep_alive)
            return;

        // Clear before draining. If the worker posts another message while
        // process_events() is running, it will schedule a follow-up callback
        // instead of leaving the message without a wake-up.
        m_ui_dispatch_pending.store(false);
        process_events();
    });
}

void BoostThreadWorker::run()
{
    bool stop = false;
    while (!stop) {
        bool finalization_queued = false;
        m_input_queue
            .consume_one(BlockingWait{0, &m_running}, [this, &stop, &finalization_queued](JobEntry &e) {
                if (!e.job)
                    stop = true;
                else {
                    m_canceled.store(false);

                    try {
                        e.job->process(*this);
                    } catch (...) {
                        e.eptr = std::current_exception();
                    }

                    e.canceled = m_canceled.load();
                    m_output_queue.push(std::move(e)); // finalization message
                    finalization_queued = true;
                }
                m_running.store(false);
            });
        if (finalization_queued) {
            // Send the completion notification only after this job is no
            // longer marked as running.
            notify_main_thread();
        }
    };
}

void BoostThreadWorker::update_status(int st, const std::string &msg)
{
    m_output_queue.push(st, msg);
    notify_main_thread();
}

std::future<void> BoostThreadWorker::call_on_main_thread(std::function<void ()> fn)
{
    MainThreadCallData cbdata{std::move(fn), {}};
    std::future<void> future = cbdata.promise.get_future();

    m_output_queue.push(std::move(cbdata));
    notify_main_thread();

    return future;
}

BoostThreadWorker::BoostThreadWorker(std::shared_ptr<ProgressIndicator> pri,
                                     boost::thread::attributes &attribs,
                                     const char *               name)
    : m_progress(std::move(pri)), m_name{name}
{
    if (m_progress)
        m_progress->set_cancel_callback([this](){ cancel(); });

    m_thread = create_thread(attribs, [this] { this->run(); });

    std::string nm{name};
    if (!nm.empty()) set_thread_name(m_thread, name);
}

constexpr int ABORT_WAIT_MAX_MS = 10000;

BoostThreadWorker::~BoostThreadWorker()
{
    // Any CallAfter already queued on the GUI thread must become a no-op
    // before teardown starts. The worker thread is joined below, so the weak
    // token member remains valid while it can still call notify_main_thread().
    m_ui_lifetime.reset();

    bool joined = false;
    try {
        cancel_all();
        wait_for_idle(ABORT_WAIT_MAX_MS);
        m_input_queue.push(JobEntry{nullptr});
        joined = join(ABORT_WAIT_MAX_MS);
    } catch(...) {}

    if (!joined)
        BOOST_LOG_TRIVIAL(error)
            << "Could not join worker thread '" << m_name << "'";
}

bool BoostThreadWorker::join(int timeout_ms)
{
    if (!m_thread.joinable())
        return true;

    if (timeout_ms <= 0) {
        m_thread.join();
    }
    else if (m_thread.try_join_for(boost::chrono::milliseconds(timeout_ms))) {
        return true;
    }
    else
        return false;

    return true;
}

void BoostThreadWorker::process_events()
{
    while (m_output_queue.consume_one([this](WorkerMessage &msg) {
        msg.deliver(*this);
    }));
}

bool BoostThreadWorker::wait_for_current_job(unsigned timeout_ms)
{
    bool ret = true;

    if (!is_idle()) {
        bool was_finish = false;
        bool timeout_reached = false;
        while (!timeout_reached && !was_finish) {
            timeout_reached =
                !m_output_queue.consume_one(BlockingWait{timeout_ms},
                                            [this, &was_finish](
                                                WorkerMessage &msg) {
                                                msg.deliver(*this);
                                                if (msg.get_type() ==
                                                    WorkerMessage::Finalize)
                                                    was_finish = true;
                                            });
        }

        ret = !timeout_reached;
    }

    return ret;
}

bool BoostThreadWorker::wait_for_idle(unsigned timeout_ms)
{
    bool timeout_reached = false;
    while (!timeout_reached && !is_idle()) {
        timeout_reached = !m_output_queue
                               .consume_one(BlockingWait{timeout_ms},
                                            [this](WorkerMessage &msg) {
                                                msg.deliver(*this);
                                            });
    }

    return !timeout_reached;
}

bool BoostThreadWorker::push(std::unique_ptr<Job> job)
{
    if (!job)
        return false;

    m_input_queue.push(JobEntry{std::move(job)});
    return true;
}

}} // namespace Slic3r::GUI
