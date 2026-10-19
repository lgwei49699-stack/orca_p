#ifndef slic3r_Utils_ModelRepairBackup_hpp_
#define slic3r_Utils_ModelRepairBackup_hpp_

#include "libslic3r/Model.hpp"
#include "libslic3r/ModelRepair.hpp"

namespace Slic3r {

struct ModelRepairBackupResult
{
    size_t queued_objects  = 0;
    size_t failed_objects  = 0;
    bool   metadata_queued = false;
    bool   metadata_failed = false;

    bool failed() const { return failed_objects != 0 || metadata_failed; }
};

// Call on the GUI thread only AFTER the live scene commit and final geometry /
// instance adjustments. report.committed alone only describes the staging
// transaction; it is not permission to back up an uncommitted live scene.
// Private repair models do not have backup enabled. Callbacks keep the same
// dispatch policy testable without wx or the asynchronous backup manager.
// Indices in a successful repair report are unique.
template<class SaveObject, class SaveMetadata>
ModelRepairBackupResult queue_committed_model_repair_backup(Model&                   scene,
                                                            const ModelRepairReport& report,
                                                            SaveObject&&             save_object,
                                                            SaveMetadata&&           save_metadata) noexcept
{
    ModelRepairBackupResult result;
    if (!scene.is_need_backup() || !report.success || !report.committed || !report.can_continue())
        return result;

    for (size_t object_index : report.changed_object_indices) {
        ModelObject* object = object_index < scene.objects.size() ? scene.objects[object_index] : nullptr;
        // save_object_mesh silently skips empty objects / instances. Do not
        // report those as queued or publish metadata referencing a stale mesh.
        if (object == nullptr || object->get_model() != &scene || object->volumes.empty() || object->instances.empty()) {
            ++result.failed_objects;
            continue;
        }
        try {
            save_object(*object);
            ++result.queued_objects;
        } catch (...) {
            // Backup is best effort after the geometry transaction. Continue
            // with other changed objects; never roll back the committed scene.
            ++result.failed_objects;
        }
    }

    if (result.queued_objects != 0 && result.failed_objects == 0) {
        try {
            // The existing backup queue handles mesh tasks before metadata.
            // This reports enqueue success, NOT completion of disk writes.
            save_metadata();
            result.metadata_queued = true;
        } catch (...) {
            result.metadata_failed = true;
        }
    }
    return result;
}

} // namespace Slic3r

#endif // slic3r_Utils_ModelRepairBackup_hpp_
