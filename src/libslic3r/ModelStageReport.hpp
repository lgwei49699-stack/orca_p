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
