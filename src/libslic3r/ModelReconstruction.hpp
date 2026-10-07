#ifndef slic3r_ModelReconstruction_hpp_
#define slic3r_ModelReconstruction_hpp_

#include "MeshReconstruction.hpp"
#include "ReconstructionProcess.hpp"

#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Slic3r {
class Model;
class ModelObject;

struct ModelReconstructionOptions
{
    MeshReconstructionOptions    mesh;
    ReconstructionProcessOptions process;
    std::string                  executable;
};

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

// Internal headless entry, dispatched before normal CLI/GUI startup. It only
// reads/writes the fixed protocol filenames inside the supplied task directory.
int run_model_reconstruction_worker(const std::string& task_directory);

// Public standalone CLI. STL only for v1 to avoid geometry-only import silently
// discarding material/texture semantics. Writes to a new, nonexistent directory.
int run_model_reconstruction_cli(const std::string& executable, const std::vector<std::string>& args);
} // namespace Slic3r

#endif
