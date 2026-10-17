#ifndef slic3r_ModelCheckWorkflow_hpp_
#define slic3r_ModelCheckWorkflow_hpp_

#include "ModelRepair.hpp"
#include "ModelInspection.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Slic3r {
struct ModelCheckWorkflowOptions
{
    std::string input_file;
    std::string output_directory;
    std::string export_format; // Empty: source format; otherwise "stl" or "obj".
    bool        advanced_repair  = false;
    bool        ignore_materials = false;
    // Optional read-only report extension. With advanced_repair, normal
    // unresolved geometry returns normally only after evidence completes.
    // Legacy callers pay no section cost and retain the six-field v4 projection.
    bool repair_evidence = false;
    // Only meaningful with advanced_repair + repair_evidence. This records a
    // caller policy for recommendations; the workflow never closes/rebuilds.
    bool allow_boundary_closure = false;
    // Server integration only; defaults preserve the v4 report and workload.
    bool        stage_report = false;
    std::string reference_file;
    // Check-only capacity is independent of the repair engine's own budgets.
    // Keep bounded geometry and intersection work; never downsample for inspection.
    std::size_t            max_input_bytes = 256 * 1024 * 1024;
    ModelInspectionOptions inspection_options;
};

struct ModelCheckWorkflowReport
{
    nlohmann::json           check = nlohmann::json::object();
    ModelRepairReport        repair;
    bool                     repair_attempted        = false;
    bool                     input_materials_ignored = false;
    std::vector<std::string> output_files;
    // Internal evidence only. Stages classify exceptions without parsing error
    // messages; only legacy geometric non-progress uses repair_no_progress to
    // retain its old exit code without misclassifying it as an engine exception.
    std::string failure_stage = "input";
    std::string failure_kind;
    bool        repair_no_progress = false;
};

// Check-only findings return normally. Fatal/IO failures throw; report retains
// the last published working model. Advanced repair without a retainable change
// reports unresolved, not a successful repair. The explicit evidence combination
// returns normally for that outcome only if its evidence workflow completes;
// legacy advanced repair still throws on no progress. Never overwrites the input.
void run_model_check_workflow(const ModelCheckWorkflowOptions& options, ModelCheckWorkflowReport& report);

// Public schema v4: preserve the six existing fields and seven issue counts.
// Explicit repair_evidence requests add a versioned boundary evidence object. No artifact path fields;
// the internal report retains paths and this projection does not change export.
// Existing field meanings and the internal inspection schema are unchanged;
// ordinary repair JSON separately gains engine-result boundary evidence.
nlohmann::json model_check_business_result(const nlohmann::json& checks, int return_code, const std::string& error);
} // namespace Slic3r

#endif
