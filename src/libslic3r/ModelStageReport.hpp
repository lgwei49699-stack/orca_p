#ifndef slic3r_ModelStageReport_hpp_
#define slic3r_ModelStageReport_hpp_

#include "TriangleMesh.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace Slic3r {

// Created only on explicit CLI opt-in. No repair, selection policy or GUI state.
struct ModelStageReportContext
{
    std::string  input_file;
    std::string  reference_file;
    std::string  input_sha256;
    std::string  reference_sha256;
    TriangleMesh reference_mesh;
};

ModelStageReportContext prepare_model_stage_report(const std::string& input_file,
                                                   const std::string& reference_file,
                                                   const std::string& output_directory);

// Read-only evidence for bounded, valid stage meshes. Verifies unchanged source
// triangles plus disk patches on source boundaries; never replaces inspection.
nlohmann::json inspect_model_hole_fill(const TriangleMesh& source, const TriangleMesh& candidate);

// Every triangle must match after rounding each coordinate to 1e-6 mm.
bool model_stage_preserves_surface(const TriangleMesh& source, const TriangleMesh& candidate);

// Publishes model-stage.json only after identity, readback and comparisons
// succeed. Consumers still require exit 0; failed invocations are never rescued
// from leftover files. The inspection must describe inspected_mesh exactly.
void write_model_stage_report(const ModelStageReportContext& context,
                              const std::string&             output_directory,
                              const std::string&             artifact_file,
                              const TriangleMesh&            inspected_mesh,
                              const nlohmann::json&          inspection,
                              const std::string&             stage,
                              const std::string&             repair_status,
                              const nlohmann::json&          reconstruction);
} // namespace Slic3r
#endif
