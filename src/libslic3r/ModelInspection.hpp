#ifndef slic3r_ModelInspection_hpp_
#define slic3r_ModelInspection_hpp_

#include <cstddef>
#include <stdexcept>
#include <vector>
#include <nlohmann/json.hpp>

namespace Slic3r {
class Model;
class TriangleMesh;

// Explicit geometry admission limits, distinct from malformed input or an
// inspection execution error. The legacy runtime_error contract is preserved.
class ModelInspectionLimitError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

struct ModelInspectionOptions
{
    // Shared defaults for direct inspection and the model-check workflow,
    // including reinspection of serialized results.
    std::size_t max_triangles        = 3000000;
    std::size_t max_vertices         = 3000000;
    unsigned    max_intersections    = 20000;
    std::size_t max_solid_components = 128;
    std::size_t detail_limit         = 200;
    // App basic counts edge/face defects, then tests vertex/intersection existence
    // only if needed. Full enumerates all seven defects and analyzes components.
    bool analyze_components = true;
    // Optional existence test on existing intersection pairs; at most one side
    // may use an exact two-face coplanar edge neighborhood. Shared-edge fans
    // and two-sided patch assembly remain unproven. No second pair search/edits.
    bool classify_proper_crossings = false;
    // Only used after the strict triangle-interior fast path finds no witness.
    // Exceeding either budget leaves the existence classification incomplete.
    std::size_t max_crossing_neighborhood_candidates = 4096;
    std::size_t max_crossing_neighborhood_faces      = 3000000;
};

// Seven geometry checks and two public supplemental checks, on the supplied mesh.
// Legacy whole-shell orientation metadata does not affect the task conclusion.
// Exact coordinate canonicalization is diagnostic only; it never edits the mesh.
// Throws on invalid input/resource failures. An incomplete check is never a pass.
nlohmann::json inspect_model_mesh(const TriangleMesh& mesh, const ModelInspectionOptions& options = {});

// Counts for the repair transaction's duplicate/degenerate-face comparison.
// No vertex/intersection searches and no whole-model pass/fail conclusion.
nlohmann::json inspect_model_mesh_topology(const TriangleMesh& mesh);

struct ModelMeshCleanupPlan
{
    std::vector<std::size_t> kept_faces;
    std::size_t              zero_area_faces         = 0;
    std::size_t              same_winding_duplicates = 0;
};

// Geometry-only read-only plan. The transaction decides whether attributes and
// remaining geometry permit applying it; a plan alone is not a safe repair.
ModelMeshCleanupPlan plan_model_mesh_cleanup(const TriangleMesh& mesh);

// Transactional whitelist: exact zero-area faces and same-winding duplicates.
// Annotated volumes and candidates that would erase a volume are retained and
// reported as deferred. No welding, filling, shell splitting or orientation edits.
nlohmann::json prepare_model_for_inspection(Model& model);
} // namespace Slic3r

#endif
