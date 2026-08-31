#ifndef libslic3r_ModelRepair_hpp_
#define libslic3r_ModelRepair_hpp_

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
};

struct ModelRepairReport
{
    bool   success       = false;
    bool   attempted     = false;
    bool   committed     = false;
    bool   canceled      = false;
    size_t objects       = 0;
    size_t volumes       = 0;
    size_t repaired      = 0;
    size_t mesh_repaired = 0;
    size_t skipped       = 0;
    size_t failed        = 0;
    size_t rolled_back   = 0;
    size_t split_volumes = 0;
    size_t created_parts = 0;
    size_t removed_parts = 0;

    size_t before_facets     = 0;
    size_t after_facets      = 0;
    size_t before_open_edges = 0;
    size_t after_open_edges  = 0;

    std::vector<ModelRepairVolumeReport> volume_reports;
    std::vector<ModelRepairFailure>      failures;
    std::vector<size_t>                  changed_object_indices;

    const char* status() const;
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

    // Optional synchronous runner for the read-only SavedPainting coverage
    // check. The supplied task only reads immutable painting snapshots and the
    // private staging volumes. GUI callers may execute it on a worker while the
    // main thread services progress and cancellation. The runner must not
    // retain the task and must join every worker before returning.
    std::function<bool(const std::function<bool()>& task)> run_painting_validation;

    // Permit topology-indexed annotations that cannot or should not be remapped
    // to be cleared. Geometry-only CLI repair enables this explicitly.
    bool allow_annotation_clearing = true;

    // Spatially remap support, seam, MMU and fuzzy-skin painting onto repaired
    // facets. Exterior-facet annotations are not supported by SavedPainting;
    // they still require allow_annotation_clearing when present.
    bool preserve_annotations = false;

    // Split disconnected shells that can be proven spatially separate into
    // individual volumes before repair. Nested, touching, or intersecting
    // shells stay together so cavity orientation is not destroyed. Empty,
    // planar, and vanishingly thin parts may then be removed. Both operations
    // participate in the same transaction.
    bool split_components    = true;
    bool remove_non_3d_parts = true;
};

// Build the sparse, index-preserving private Model used by repair transactions.
// Only objects named by targets are copied; empty targets copy every existing
// object. This helper is public so GUI staging and headless repair share the
// exact same selection and validation rules.
std::unique_ptr<Model> make_model_repair_staging_copy(const Model& model, const std::vector<ModelRepairTarget>& targets);

// Repairs all selected MODEL_PART volumes. Splitting, non-3D cleanup and CGAL
// repair are staged on a private model copy; if any volume fails or the user
// cancels, neither geometry nor volume structure is committed to the model.
bool repair_model(Model& model, ModelRepairReport& report, const ModelRepairOptions& options = {});

// Repairs an already isolated staging model without creating another Model
// copy. This is intended for GUI or other transactional callers that will
// discard the entire staging model on failure/cancellation and explicitly move
// the successful volume structures into the source model. A failed call may
// leave this private staging model partially modified.
bool repair_staged_model(Model& model, ModelRepairReport& report, const ModelRepairOptions& options = {});

} // namespace Slic3r

#endif // libslic3r_ModelRepair_hpp_
