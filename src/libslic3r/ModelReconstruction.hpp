#ifndef slic3r_ModelReconstruction_hpp_
#define slic3r_ModelReconstruction_hpp_

#include "MeshReconstruction.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Slic3r {
class Model;
class ModelObject;

struct ReconstructionLimits
{
    // Zero selects a size-aware budget. Explicit caller values are retained.
    unsigned              timeout_ms      = 0;
    std::size_t           memory_limit_mb = 0;
    std::function<bool()> is_canceled;
    // Runs on the caller/UI thread. Cancellation waits for SDK work to return.
    std::function<void()> on_tick;
};

struct ModelReconstructionOptions
{
    MeshReconstructionOptions mesh;
    ReconstructionLimits      limits;
    // CLI requires an explicit size. GUI supplies the value shown to the user.
    double voxel_mm = 0.;
};

bool model_reconstruction_available() noexcept;

// Resolves only resource limits, preserving cancellation/tick callbacks. Physical
// RAM is an upper bound for automatic budgets, not an estimate of free memory.
ReconstructionLimits model_reconstruction_limits(std::size_t                 input_faces,
                                                 const ReconstructionLimits& requested,
                                                 std::size_t                 physical_memory_bytes);

struct ModelReconstructionResult
{
    // candidate_ready means a checked geometric candidate, not print readiness.
    std::string            status = "failed";
    std::string            message;
    nlohmann::json         report;
    std::shared_ptr<Model> candidate_model;
};

// Empty means eligible for this deliberately limited first version. Never
// clears annotations or mutates the source to make it eligible.
std::string model_reconstruction_ineligibility(const ModelObject& object);

// Identity includes mesh, transforms, ordinary configuration and every
// eligibility condition. Intended for rejecting a stale GUI candidate.
std::string model_reconstruction_source_identity(const ModelObject& object);

// Produces a private sparse staging Model, preserving original object indices.
// Does not commit to the source, move onto the bed, slice, or start printing.
ModelReconstructionResult reconstruct_model_candidate(const Model& model, size_t object_index, const ModelReconstructionOptions& options);

// Public standalone CLI. STL only for v1 to avoid geometry-only import silently
// discarding material/texture semantics. Writes to a new, nonexistent directory.
int run_model_reconstruction_cli(const std::vector<std::string>& args);

// Reuse the standalone parser for explicit stage reports: one bounded,
// geometry-only STL snapshot, no import repair, hash of the bytes actually parsed.
// Shares only exactly equal vertices; all triangle coordinates/order remain.
TriangleMesh read_bounded_model_stl(const std::string& path, std::string& sha256);
std::string  model_mesh_file_sha256(const std::string& path);
} // namespace Slic3r

#endif
