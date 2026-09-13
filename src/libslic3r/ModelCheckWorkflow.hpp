#ifndef slic3r_ModelCheckWorkflow_hpp_
#define slic3r_ModelCheckWorkflow_hpp_

#include "ModelRepair.hpp"
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
} // namespace Slic3r

#endif
