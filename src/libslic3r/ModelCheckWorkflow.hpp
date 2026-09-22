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
};

// Check-only findings return normally. Fatal/IO failures throw; report retains
// the last published working model. Advanced repair without a retainable change
// reports unresolved, not a successful repair. Never overwrites the input.
void run_model_check_workflow(const ModelCheckWorkflowOptions& options, ModelCheckWorkflowReport& report);

// Public schema v4: expose current status and seven issue counts only, without
// historical/structural details or artifact path fields. The internal report
// retains artifact paths; this projection does not change model export.
// Ordinary repair/slicing JSON and the internal inspection schema are unchanged.
nlohmann::json model_check_business_result(const nlohmann::json& checks, int return_code, const std::string& error);
} // namespace Slic3r

#endif
