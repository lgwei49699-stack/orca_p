#include "FixModelByCgal.hpp"
#include "ModelRepairPolicy.hpp"

#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/MeshRepair.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Thread.hpp"
#include "slic3r/GUI/GUI.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

#include <boost/log/trivial.hpp>

namespace Slic3r {

namespace {

class ThreadJoiner
{
public:
    explicit ThreadJoiner(std::thread& thread) : m_thread(thread) {}
    ~ThreadJoiner()
    {
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    std::thread& m_thread;
};

template<class Worker>
bool run_cgal_worker(Worker&& worker_task, GUI::ProgressDialog& progress_dialog, const wxString& message, std::atomic_bool& cancel_requested)
{
    std::mutex              mutex;
    std::condition_variable condition;
    bool                    finished = false;
    bool                    result   = false;
    std::exception_ptr      worker_exception;

    std::thread  worker([&]() {
        try {
            set_current_thread_name("cgal_fix_mesh");
            result = worker_task();
        } catch (...) {
            worker_exception = std::current_exception();
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            finished = true;
        }
        condition.notify_one();
    });
    ThreadJoiner joiner(worker);

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (condition.wait_for(lock, std::chrono::milliseconds(100), [&finished]() { return finished; }))
                break;
        }

        // The repair kernel checks cancellation between stages. Individual
        // CGAL calls and full inspection still finish before the worker joins.
        if (!progress_dialog.Pulse(message))
            cancel_requested = true;
    }

    worker.join();
    if (worker_exception)
        std::rethrow_exception(worker_exception);
    return result;
}

bool inspect_mesh_on_worker(const TriangleMesh&  source,
                            GUI::ProgressDialog& progress_dialog,
                            const wxString&      message,
                            std::atomic_bool&    cancel_requested)
{
    return run_cgal_worker([&source]() { return MeshBoolean::cgal::requires_repair(source); }, progress_dialog, message, cancel_requested);
}

bool repair_mesh_on_worker(const TriangleMesh&  source,
                           TriangleMesh&        repaired,
                           std::string&         error,
                           GUI::ProgressDialog& progress_dialog,
                           const wxString&      message,
                           std::atomic_bool&    cancel_requested)
{
    struct Canceled
    {};
    std::string worker_error;
    const bool  success = run_cgal_worker(
        [&]() {
            TriangleMesh working_mesh = source;
            bool         repaired_ok  = false;
            try {
                repaired_ok = Slic3r::repair_mesh(working_mesh, nullptr, &worker_error, [&](int) {
                    if (cancel_requested.load())
                        throw Canceled{};
                });
            } catch (const Canceled&) {
                return false; // ModelRepair observes the flag and rolls back the transaction.
            }
            if (repaired_ok)
                repaired = std::move(working_mesh);
            return repaired_ok;
        },
        progress_dialog, message, cancel_requested);
    error = std::move(worker_error);
    return success;
}

} // namespace

namespace {

bool stage_owned_model_repair_by_cgal_gui(std::shared_ptr<Model>                working_model,
                                          const std::vector<ModelRepairTarget>& targets,
                                          GUI::ProgressDialog&                  progress_dialog,
                                          const wxString&                       message_header,
                                          StagedModelRepair&                    staged_repair,
                                          ModelRepairOptions                    options,
                                          bool                                  progress_initialized)
{
    staged_repair = StagedModelRepair{};
    static std::atomic<size_t> next_transaction_id{0};
    staged_repair.transaction_id = ++next_transaction_id;
    if (working_model == nullptr) {
        staged_repair.error = "Could not stage model repair: working model is missing.";
        return true;
    }

    // ObjectBase IDs and timestamps are deliberately main-thread-only. The
    // model transaction remains here; full inspection plus the private
    // TriangleMesh copy and CGAL repair run on workers.
    std::atomic_bool  cancel_requested{false};
    ModelRepairReport report;

    options.targets              = targets;
    options.is_canceled          = [&cancel_requested]() { return cancel_requested.load(); };
    options.mesh_requires_repair = [&](const TriangleMesh& source) {
        return inspect_mesh_on_worker(source, progress_dialog, message_header, cancel_requested);
    };
    options.repair_mesh_from_source = [&](const TriangleMesh& source, TriangleMesh& repaired, std::string& error) {
        return repair_mesh_on_worker(source, repaired, error, progress_dialog, message_header, cancel_requested);
    };
    options.run_validation = [&](const std::function<bool()>& validation) {
        return run_cgal_worker(validation, progress_dialog, message_header, cancel_requested);
    };
    options.on_progress = [&](size_t completed, size_t total, const ModelRepairVolumeReport& volume_report) {
        const int         percent = total == 0 ? 100 : std::min(static_cast<int>((completed * 100) / total), 99);
        const std::string message = volume_report.volume_name.empty() ? volume_report.object_name : volume_report.volume_name;
        const wxString    detail  = message.empty() ? wxString() : GUI::from_u8(message);
        if (!progress_dialog.Update(percent, message_header + detail))
            cancel_requested = true;
    };

    if (!progress_initialized && !progress_dialog.Update(0, message_header))
        return false;

    try {
        // The GUI already owns a disposable private Model copy. Avoid a second
        // full-model transaction copy; on failure this staging model is simply
        // discarded by the caller.
        repair_staged_model(*working_model, report, options);
    } catch (const std::exception& exception) {
        staged_repair.error = exception.what();
    } catch (...) {
        staged_repair.error = "Repair failed with an unknown error.";
    }

    staged_repair.report = std::move(report);
    BOOST_LOG_TRIVIAL(info) << "CGAL GUI model repair summary: event=staging_completed, transaction=" << staged_repair.transaction_id
                            << ", status=" << staged_repair.report.status() << ", staging_changed=" << staged_repair.report.committed
                            << ", objects=" << staged_repair.report.objects << ", volumes=" << staged_repair.report.volumes
                            << ", repaired=" << staged_repair.report.repaired << ", skipped=" << staged_repair.report.skipped
                            << ", unresolved=" << staged_repair.report.unresolved
                            << ", basic_removed_faces=" << staged_repair.report.basic_removed_faces
                            << ", failed=" << staged_repair.report.failed
                            << ", preserve_annotations=" << options.preserve_annotations
                            << ", allow_annotation_clearing=" << options.allow_annotation_clearing
                            << ", facets=" << staged_repair.report.before_facets << "->"
                            << staged_repair.report.after_facets << ", open_edges=" << staged_repair.report.before_open_edges << "->"
                            << staged_repair.report.after_open_edges;
    for (const ModelRepairFailure& failure : staged_repair.report.failures)
        BOOST_LOG_TRIVIAL(warning) << "CGAL GUI model repair failure: object=" << failure.object_name << ", volume=" << failure.volume_name
                                   << ", message=" << failure.message;
    if (cancel_requested || staged_repair.report.canceled)
        return false;

    if (staged_repair.error.empty() && staged_repair.report.success)
        staged_repair.model = std::move(working_model);
    return true;
}

} // namespace

bool stage_model_repair_by_cgal_gui(const Model&                          model,
                                    const std::vector<ModelRepairTarget>& targets,
                                    GUI::ProgressDialog&                  progress_dialog,
                                    const wxString&                       message_header,
                                    StagedModelRepair&                    staged_repair)
{
    staged_repair = StagedModelRepair{};
    // Let the user cancel before staging performs any object copies.
    if (!progress_dialog.Update(0, message_header))
        return false;

    // Preserve original object indices but copy only selected objects. Volume
    // mesh pointers remain shared and immutable until a selected part is
    // repaired, while unrelated objects consume no staging memory.
    std::shared_ptr<Model> working_model;
    try {
        working_model = std::shared_ptr<Model>(make_model_repair_staging_copy(model, targets));
    } catch (const std::exception& exception) {
        staged_repair       = StagedModelRepair{};
        staged_repair.error = std::string("Could not stage model repair: ") + exception.what();
        return true;
    } catch (...) {
        staged_repair       = StagedModelRepair{};
        staged_repair.error = "Could not stage model repair.";
        return true;
    }
    return stage_owned_model_repair_by_cgal_gui(std::move(working_model), targets, progress_dialog, message_header, staged_repair,
                                                gui_model_repair_options(), true);
}

bool fix_model_with_cgal_gui(ModelObject&         model_object,
                             int                  volume_idx,
                             GUI::ProgressDialog& progress_dialog,
                             const wxString&      message_header,
                             std::string&         fix_result,
                             bool                 keep_painting,
                             ModelRepairReport*   repair_report)
{
    fix_result.clear();
    if (repair_report != nullptr)
        *repair_report = ModelRepairReport{};
    if (volume_idx < -1 || (volume_idx >= 0 && static_cast<size_t>(volume_idx) >= model_object.volumes.size())) {
        fix_result = "Repair failed: selected volume index is invalid.";
        return true;
    }

    std::shared_ptr<Model> working_source;
    try {
        working_source = std::make_shared<Model>();
        working_source->add_object(model_object);
    } catch (const std::exception& exception) {
        fix_result = std::string("Could not stage model repair: ") + exception.what();
        return true;
    } catch (...) {
        fix_result = "Could not stage model repair.";
        return true;
    }

    ModelRepairTarget target;
    target.object_index = 0;
    if (volume_idx >= 0)
        target.volume_index = static_cast<size_t>(volume_idx);

    StagedModelRepair staged_repair;
    const auto        log_result = [&staged_repair](const char* event) {
        BOOST_LOG_TRIVIAL(info) << "CGAL cut model repair: transaction=" << staged_repair.transaction_id << ", event=" << event;
    };
    // Temporary cut results keep their existing opt-in painting behavior and
    // all-or-nothing geometry policy; explicit GUI repair has a separate policy.
    ModelRepairOptions options;
    options.preserve_annotations      = keep_painting;
    options.allow_annotation_clearing = true;
    options.allow_partial             = false;
    if (!stage_owned_model_repair_by_cgal_gui(std::move(working_source), {target}, progress_dialog, message_header, staged_repair,
                                              std::move(options), false)) {
        log_result("canceled");
        return false;
    }

    if (repair_report != nullptr)
        *repair_report = staged_repair.report;

    if (!staged_repair.error.empty()) {
        log_result("staging_failed");
        fix_result = staged_repair.error;
        return true;
    }
    if (!staged_repair.report.success || staged_repair.model == nullptr) {
        log_result("repair_failed");
        fix_result = staged_repair.report.failures.empty() ? "Repair failed." : staged_repair.report.failures.front().message;
        return true;
    }

    ModelObject* repaired_object = staged_repair.model->objects.front();
    if (repaired_object == nullptr) {
        log_result("staged_object_missing");
        fix_result = "Repair failed: staged model object is missing.";
        return true;
    }

    if (!staged_repair.report.committed) {
        log_result("no_change");
        return true;
    }

    try {
        model_object.replace_volume_structure(*repaired_object);
    } catch (const std::exception& exception) {
        fix_result = std::string("Repair commit failed: ") + exception.what();
        log_result("commit_failed");
        return true;
    } catch (...) {
        fix_result = "Repair commit failed.";
        log_result("commit_failed");
        return true;
    }
    // The target here is a temporary cut object, not the GUI scene.
    log_result("cut_object_committed");
    return true;
}

} // namespace Slic3r
