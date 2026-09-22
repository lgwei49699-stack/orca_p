#ifndef libslic3r_ModelRepair_hpp_
#define libslic3r_ModelRepair_hpp_

#include "MeshRepairOutcome.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

class Model;
class TriangleMesh;

enum class ModelRepairVolumeStatus {
    Skipped,
    Repaired,
    PartiallyRepaired,
    Unresolved,
    RolledBack,
    Failed,
};

const char* model_repair_volume_status(ModelRepairVolumeStatus status);

struct ModelRepairVolumeReport
{
    size_t                  object_index = 0;
    size_t                  volume_index = 0;
    std::string             object_name;
    std::string             volume_name;
    bool                    attempted             = false;
    ModelRepairVolumeStatus result                = ModelRepairVolumeStatus::Skipped;
    size_t                  before_facets         = 0;
    size_t                  after_facets          = 0;
    size_t                  before_open_edges     = 0;
    size_t                  after_open_edges      = 0;
    size_t                  repaired_parts        = 0;
    size_t                  split_parts           = 1;
    size_t                  removed_parts         = 0;
    size_t                  basic_removed_faces   = 0;
    size_t                  unresolved_parts      = 0;
    bool                    annotations_cleared   = false;
    bool                    annotations_preserved = false;
    std::string             message;

    const char* status() const { return model_repair_volume_status(result); }
};

struct ModelRepairFailure
{
    size_t      object_index = 0;
    size_t      volume_index = 0;
    std::string object_name;
    std::string volume_name;
    std::string message;
    bool        recoverable = false;
};

struct ModelRepairReport
{
    bool   success             = false;
    bool   attempted           = false;
    bool   committed           = false;
    bool   canceled            = false;
    bool   completed           = false;
    size_t objects             = 0;
    size_t volumes             = 0;
    size_t repaired            = 0;
    size_t mesh_repaired       = 0;
    size_t skipped             = 0;
    size_t failed              = 0;
    size_t rolled_back         = 0;
    size_t split_volumes       = 0;
    size_t created_parts       = 0;
    size_t removed_parts       = 0;
    size_t basic_removed_faces = 0;
    size_t unresolved          = 0; // Selected volumes with remaining geometric problems.

    size_t before_facets     = 0;
    size_t after_facets      = 0;
    size_t before_open_edges = 0;
    size_t after_open_edges  = 0;

    std::vector<ModelRepairVolumeReport> volume_reports;
    std::vector<ModelRepairFailure>      failures;
    std::vector<size_t>                  changed_object_indices;

    const char* status() const;
    bool        can_continue() const { return completed && !canceled && failed == 0; }
    bool        mesh_validated() const { return can_continue() && unresolved == 0; }
};

struct ModelRepairTarget
{
    size_t                object_index = 0;
    std::optional<size_t> volume_index;
};

struct ModelRepairOptions
{
    // Empty means all MODEL_PART volumes. A target without volume_index means
    // all MODEL_PART volumes belonging to that object.
    std::vector<ModelRepairTarget> targets;

    // Keep independently safe basic cleanup, verified CGAL checkpoints within
    // a part, and other successfully repaired parts on geometric rejection.
    // Invalid data, inspection/worker exceptions, annotation failures and
    // cancellation remain fatal.
    // Deletion that opens connections is not retained. Isolated collinear caps
    // may instead form an independently certified, surface-preserving topology
    // baseline, retained on later geometric rejection but never exceptions. Callers
    // must distinguish partial success from mesh_validated(); no improvement
    // returns false but can_continue() may permit a separate slicing attempt.
    // Explicitly enabled by GUI/CLI; the core's default remains transactional.
    bool allow_partial = false;

    // CGAL does not expose interruption inside a single repair call. These
    // callbacks are therefore observed between volumes and once more before
    // the transaction commits.
    std::function<bool()>                                                               is_canceled;
    std::function<void(size_t completed, size_t total, const ModelRepairVolumeReport&)> on_progress;

    // Optional full mesh inspection hook. It returns true when repair is
    // required and is used both before repair and for the final repaired-mesh
    // validation. GUI callers use a synchronous wrapper that performs the CGAL
    // inspection on a worker while keeping Model mutations on the caller.
    // Inspection exceptions fail the transaction instead of being converted
    // into "repair needed" and starting a more expensive operation after an
    // allocation or diagnostic failure.
    std::function<bool(const TriangleMesh& mesh)> mesh_requires_repair;

    // Optional synchronous mesh-only execution hook. repair_model keeps every
    // Model / ModelObject / ModelVolume mutation on its calling thread and
    // grants the hook exclusive access only to a private TriangleMesh copy.
    // GUI callers may use this hook to run CGAL on a worker while the main
    // thread services progress and cancellation, then return the result here.
    // The hook must not retain mesh/error references or return before any
    // worker using them has joined. on_progress is synchronous as well.
    std::function<bool(TriangleMesh& mesh, std::string& error)> repair_mesh;

    // Optional source-to-result variant used when the mesh copy itself should
    // happen on the worker. When supplied, it takes precedence over
    // repair_mesh. The source is immutable and may be shared with the staging
    // volume; the hook must fully initialize result on success and join every
    // worker before returning.
    std::function<bool(const TriangleMesh& source, TriangleMesh& result, std::string& error)> repair_mesh_from_source;

    // Optional partial-aware source-to-result worker. Used only in partial
    // mode, with precedence over the legacy bool hooks. Rejected outputs are
    // ignored; partial outputs must pass the shared read-only progress gate.
    // As with the legacy hooks, all workers must join before returning.
    std::function<MeshRepairOutcome(const TriangleMesh& source, TriangleMesh& result, std::string& error)> repair_mesh_with_partial;

    // Optional synchronous runner for the read-only SavedPainting coverage
    // check. The supplied task only reads immutable painting snapshots and the
    // private staging volumes. GUI callers may execute it on a worker while the
    // main thread services progress and cancellation. The runner must not
    // retain the task and must join every worker before returning.
    std::function<bool(const std::function<bool()>& task)> run_painting_validation;

    // Optional synchronous worker for basic face filtering, certified topology
    // preparation and read-only volume-cleanup / partial-candidate validation.
    // Only private mesh copies are modified; never touches Model IDs/state.
    std::function<void(const std::function<void()>& task)> run_mesh_preparation;

    // Permit topology-indexed annotations that cannot or should not be remapped
    // to be cleared. Geometry-only CLI repair enables this explicitly.
    bool allow_annotation_clearing = true;

    // Spatially remap support, seam, MMU and fuzzy-skin painting onto repaired
    // facets. Exterior-facet annotations are not supported by SavedPainting;
    // they still require allow_annotation_clearing when present.
    bool preserve_annotations = false;

    // Split disconnected shells that can be proven spatially separate into
    // individual volumes before repair. Nested, touching, or intersecting
    // shells stay together so cavity orientation is not destroyed. Empty and
    // small/degenerate parts may then be removed using the Orca 2.4.2 policy.
    // Both operations participate in the same transaction.
    // Partial mode may also separate independent groups while retaining all
    // touching/nested patches within each group. That fallback preserves winding
    // and transforms and only retains the split when actual repair/cleanup changes
    // are accepted; grouping alone is not progress on an unresolved model.
    bool split_components    = true;
    // Legacy name; includes 2.4.2's source-local size/volume heuristics:
    // min dimension or abs(volume) <= EPSILON, min/max dimension <= 1e-6,
    // or abs(volume)/bbox volume <= 1e-6. No instance/volume scaling is applied.
    // Disabling removal does not disable candidate validation or strict color
    // protection. Removal must never empty an object or silently lose painting.
    bool remove_non_3d_parts = true;
};

// Build the sparse, index-preserving private Model used by repair transactions.
// Only objects named by targets are copied; empty targets copy every existing
// object. This helper is public so GUI staging and headless repair share the
// exact same selection and validation rules.
std::unique_ptr<Model> make_model_repair_staging_copy(const Model& model, const std::vector<ModelRepairTarget>& targets);

// Repairs all selected MODEL_PART volumes. Splitting, non-3D cleanup and CGAL
// repair are staged on a private model copy; by default, if any volume fails or
// the user cancels, neither geometry nor volume structure is committed.
// With allow_partial, geometric rejection is recorded without discarding safe
// changes. A true return may therefore mean partially_repaired, not mesh-valid.
bool repair_model(Model& model, ModelRepairReport& report, const ModelRepairOptions& options = {});

// Repairs an already isolated staging model without creating another Model
// copy. This is intended for GUI or other transactional callers that will
// discard the entire staging model on failure/cancellation and explicitly move
// the successful volume structures into the source model. A failed call may
// leave this private staging model partially modified.
bool repair_staged_model(Model& model, ModelRepairReport& report, const ModelRepairOptions& options = {});

} // namespace Slic3r

#endif // libslic3r_ModelRepair_hpp_
