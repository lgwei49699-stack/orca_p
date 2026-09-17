#include "ModelCheckWorkflow.hpp"
#include "ModelInspection.hpp"
#include "Model.hpp"
#include "PrintConfig.hpp"
#include "Format/OBJ.hpp"
#include "Format/STL.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <array>
#include <iomanip>
#include <limits>
#include <locale>
#include <stdexcept>

namespace Slic3r {
namespace {
namespace fs = boost::filesystem;
using json   = nlohmann::json;

Model load_working_model(const std::string&  path,
                         DynamicPrintConfig& config,
                         bool                ignore,
                         bool&               ignored,
                         bool                repair_stl    = false,
                         MeshRepairReport*   import_report = nullptr)
{
    // Each task runs native import once. Internal baseline/export reinspection
    // reads the artifact without another repair, regardless of its filename.
    return Model::read_from_file(path, &config, nullptr, LoadStrategy::AddDefaultInstances, nullptr, nullptr, nullptr, nullptr, nullptr,
                                 nullptr, nullptr, 0, nullptr, repair_stl, import_report, nullptr, nullptr, ignore, !ignore, &ignored);
}

std::vector<RGBA> model_colors(const DynamicPrintConfig& config)
{
    std::vector<RGBA> colors;
    const auto*       option = config.option<ConfigOptionStrings>("filament_colour");
    if (!option)
        return colors;
    for (const auto& color : option->values) {
        if (color.size() != 7 || color.front() != '#' || color.find_first_not_of("0123456789aAbBcCdDeEfF", 1) != std::string::npos)
            throw std::runtime_error("Invalid imported OBJ face color.");
        const auto value = std::stoul(color.substr(1), nullptr, 16);
        colors.push_back({float((value >> 16) & 255) / 255.f, float((value >> 8) & 255) / 255.f, float(value & 255) / 255.f, 1.f});
    }
    return colors;
}

// Every destination is in a newly created run directory. Plain OBJ uses enough
// digits to round-trip Orca's float vertices (the old generic exporter doesn't).
std::vector<std::string> save_working_model(const fs::path& path, Model& model, const DynamicPrintConfig& config)
{
    const auto colors = model_colors(config);
    const auto mesh   = model.mesh();
    if (mesh.its.indices.empty())
        throw std::runtime_error("Refusing to export an empty working model.");
    for (const auto& v : mesh.its.vertices)
        if (!v.allFinite())
            throw std::runtime_error("Working model transform produced non-finite geometry.");
    if (path.extension() == ".stl") {
        if (!colors.empty())
            throw std::runtime_error("STL cannot preserve OBJ colors; use OBJ or explicitly ignore materials.");
        if (!store_stl(path.string().c_str(), &model, true))
            throw std::runtime_error("Could not save the working STL model.");
        return {path.string()};
    }
    if (!colors.empty()) {
        if (!store_multicolor_obj(path.string().c_str(), model, colors))
            throw std::runtime_error("Could not preserve face colors in the working OBJ model.");
        auto mtl = path;
        mtl.replace_extension(".mtl");
        return {path.string(), mtl.string()};
    }
    boost::nowide::ofstream out(path.string(), std::ios::out | std::ios::trunc);
    out.imbue(std::locale::classic());
    out << "# OrcaSlicer geometry working copy\n" << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (const auto& v : mesh.its.vertices)
        out << "v " << v.x() << ' ' << v.y() << ' ' << v.z() << '\n';
    for (const auto& f : mesh.its.indices)
        out << "f " << f[0] + 1 << ' ' << f[1] + 1 << ' ' << f[2] + 1 << '\n';
    out.close();
    if (!out)
        throw std::runtime_error("Could not save the working OBJ model.");
    return {path.string()};
}

void publish_check(ModelCheckWorkflowReport& report, const json& inspection, const std::string& model_path, const char* stage)
{
    report.check["inspection"]     = inspection;
    report.check["checked_stage"]  = stage;
    report.check["working_model"]  = model_path;
    report.check["check_complete"] = inspection.at("check_complete");
    report.check["mesh_status"]    = inspection.at("mesh_status");
    report.check["next_action"]    = inspection.at("next_action");
}

json business_issue_counts(const json& inspection)
{
    if (!inspection.is_object())
        return nullptr; // Not inspected is different from seven zero counts.
    constexpr std::array<const char*, 7> keys{"boundary",   "nonmanifold_edge", "nonmanifold_vertex", "self_intersection",
                                              "degenerate", "duplicate",        "orientation"};
    json                                 counts = json::object();
    for (const char* key : keys) {
        const auto& issue = inspection.at("issues").at(key);
        // Incomplete counts are lower bounds, never a clean-model conclusion.
        counts[key] = {{"count", issue.at("count")}, {"complete", issue.at("complete")}};
    }
    return counts;
}

} // namespace

json model_check_business_result(const json& checks, int return_code, const std::string& error)
{
    json       result        = {{"return_code", return_code}, {"error_string", error}, {"model_checks", json::array()}};
    const json no_inspection = nullptr;
    const json no_repair     = json::object();
    for (const auto& check : checks) {
        const auto& inspection = check.contains("inspection") ? check.at("inspection") : no_inspection;
        const auto& advanced   = check.contains("advanced_repair") ? check.at("advanced_repair") : no_repair;
        const auto  status     = advanced.value("status", "not_requested");
        json        summary    = {{"schema_version", 4},
                                  {"mesh_status", check.value("mesh_status", "unknown")},
                                  {"check_complete", check.value("check_complete", false)},
                                  {"input_materials_ignored", check.value("input_materials_ignored", false)},
                                  {"repair_status", status},
                                  {"issues", business_issue_counts(inspection)}};
        // Presentation-only reduction: aggregate completeness still includes
        // internal structural checks. Do not turn unknown findings into a pass.
        result["model_checks"].push_back(std::move(summary));
    }
    return result;
}

void run_model_check_workflow(const ModelCheckWorkflowOptions& options, ModelCheckWorkflowReport& report)
{
    report                    = {};
    report.check              = {{"schema_version", 2},
                                 {"input_file", options.input_file},
                                 {"check_complete", false},
                                 {"mesh_status", "unknown"},
                                 {"operation_status", "running"},
                                 {"next_action", "retry_check"},
                                 {"working_model", nullptr},
                                 {"basic_repair", nullptr},
                                 {"inspection", nullptr},
                                 {"slicing", "not_requested"},
                                 {"advanced_repair",
                                  {{"requested", options.advanced_repair}, {"status", options.advanced_repair ? "not_started" : "not_requested"}}}};
    std::string source_format = fs::path(options.input_file).extension().string();
    boost::algorithm::to_lower(source_format);
    if (source_format != ".stl" && source_format != ".obj")
        throw std::invalid_argument("--check-model requires one STL or OBJ input.");
    if (!options.export_format.empty() && options.export_format != "obj" && options.export_format != "stl")
        throw std::invalid_argument("Invalid model check export format.");
    if (fs::file_size(options.input_file) > 64 * 1024 * 1024)
        throw std::runtime_error("Model check input exceeds the 64 MiB limit.");
    DynamicPrintConfig config;
    MeshRepairReport   import_report;
    const bool         repair_stl = source_format == ".stl";
    Model model = load_working_model(options.input_file, config, options.ignore_materials, report.input_materials_ignored, repair_stl,
                                     &import_report);
    report.check["input_materials_ignored"] = report.input_materials_ignored;
    const auto triangle_count               = model.mesh().its.indices.size();
    if (triangle_count == 0 || triangle_count > ModelInspectionOptions{}.max_triangles)
        throw std::runtime_error("Model check requires 1 to 1,000,000 decoded triangles.");
    // Match ordinary Orca source import instead of independently deleting
    // zero-area records: even a zero-area triangle can participate in paired
    // edge topology. OBJ keeps its native import/material handling, not ADMesh.
    report.check["basic_repair"] = {{"policy", repair_stl ? "orca_stl_import_v1" : "orca_obj_import_v1"},
                                    {"status", "completed"},
                                    {"changed", repair_stl && import_report.changed()},
                                    {"facets_after", triangle_count}};
    if (repair_stl) {
        report.check["basic_repair"]["facets_before"]               = import_report.original_facets;
        report.check["basic_repair"]["edges_fixed"]                 = import_report.edges_fixed;
        report.check["basic_repair"]["degenerate_facets_removed"]   = import_report.degenerate_facets_removed;
        report.check["basic_repair"]["disconnected_facets_removed"] = import_report.disconnected_facets_removed;
        report.check["basic_repair"]["facets_reversed"]             = import_report.facets_reversed;
        report.check["basic_repair"]["normals_fixed"]               = import_report.normals_fixed;
    }

    const fs::path parent = fs::absolute(options.output_directory.empty() ? "." : options.output_directory);
    fs::create_directories(parent);
    const fs::path run = parent / fs::unique_path("model-check-%%%%-%%%%-%%%%");
    if (!fs::create_directory(run))
        throw std::runtime_error("Could not create a private model check output directory.");
    // Always persist the decoded baseline: import normalization and OBJ color
    // subdivision mean that an unchanged cleanup count need not equal raw input.
    const fs::path baseline               = run / ("prepared" + source_format);
    report.output_files                   = save_working_model(baseline, model, config);
    report.check["working_model"]         = baseline.string();
    report.check["prepared_model"]        = baseline.string();
    report.check["prepared_output_files"] = report.output_files;
    DynamicPrintConfig working_config;
    bool               ignored      = false;
    Model              working      = load_working_model(baseline.string(), working_config, options.ignore_materials, ignored);
    auto               before       = inspect_model_mesh(working.mesh());
    report.check["before_advanced"] = before;
    publish_check(report, before, baseline.string(), "serialized_prepared");
    if (options.advanced_repair) {
        if (before.at("mesh_status") == "passed") {
            report.check["advanced_repair"]["status"] = "not_needed";
        } else {
            report.repair_attempted                   = true;
            report.check["advanced_repair"]["status"] = "running";
            ModelRepairOptions repair_options;
            repair_options.allow_partial                     = true;
            repair_options.preserve_annotations              = !options.ignore_materials;
            repair_options.allow_annotation_clearing         = options.ignore_materials;
            repair_options.remove_non_3d_parts               = options.ignore_materials;
            const bool ok                                    = repair_model(working, report.repair, repair_options);
            report.check["advanced_repair"]["engine_status"] = report.repair.status();
            report.check["advanced_repair"]["committed"]     = report.repair.committed;
            // The shared engine returns false for an unchanged unresolved mesh
            // as well as for a fatal transaction. Consult its structured report
            // before interpreting that bool; cancellation/worker/paint failures
            // must still remain fatal even when unresolved items were recorded.
            const bool unchanged_unresolved = !report.repair.committed && report.repair.unresolved != 0;
            if (!report.repair.can_continue() || (!ok && !unchanged_unresolved)) {
                report.check["advanced_repair"]["status"]         = "failed";
                report.check["advanced_repair"]["retained_stage"] = "prepared";
                throw std::runtime_error("Advanced repair failed; the prepared working model was retained.");
            }
            if (!report.repair.committed) {
                report.check["advanced_repair"]["status"]         = "unresolved";
                report.check["advanced_repair"]["retained_stage"] = "prepared";
                throw std::runtime_error("Advanced repair produced no retainable change; see the remaining inspection findings.");
            }
        }
    }
    // Export the advanced result or an explicitly requested format conversion.
    // Recheck the serialized/reimported artifact, not a different in-memory mesh.
    if (report.repair.committed || !options.export_format.empty()) {
        const std::string  format = options.export_format.empty() ? source_format.substr(1) : options.export_format;
        const fs::path     output = run / ("result." + format);
        const auto         files  = save_working_model(output, working, working_config);
        DynamicPrintConfig reloaded_config;
        Model              reloaded = load_working_model(output.string(), reloaded_config, options.ignore_materials, ignored);
        const auto         after    = inspect_model_mesh(reloaded.mesh());
        // Allocate the complete publication before replacing the retained
        // baseline report. An allocation/serialization failure must not mix stages.
        auto accepted = report;
        publish_check(accepted, after, output.string(), report.repair.committed ? "serialized_advanced" : "serialized_prepared");
        accepted.output_files.insert(accepted.output_files.end(), files.begin(), files.end());
        accepted.check["final_output_files"] = files;
        if (report.repair.committed) {
            accepted.check["after_advanced"]                    = after;
            accepted.check["advanced_repair"]["status"]         = after.at("mesh_status") == "passed" ? "repaired" : "partially_repaired";
            accepted.check["advanced_repair"]["retained_stage"] = "advanced";
        }
        report = std::move(accepted);
    }
    report.check["operation_status"] = "completed";
}
} // namespace Slic3r
