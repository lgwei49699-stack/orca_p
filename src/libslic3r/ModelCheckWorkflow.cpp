#include "ModelCheckWorkflow.hpp"
#include "I18N.hpp"
#include "ModelInspection.hpp"
#include "ModelRepairEvidence.hpp"
#include "ModelReconstruction.hpp"
#include "ModelStageReport.hpp"
#include "Model.hpp"
#include "PrintConfig.hpp"
#include "Format/OBJ.hpp"
#include "Format/STL.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <array>
#include <chrono>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
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
        if (inspection.value("inspection_scope", "full") == "basic")
            for (const char* field :
                 {"status", "method", "count_is_lower_bound", "geometric_count", "same_winding_extra_faces", "opposite_winding_groups"})
                if (issue.contains(field))
                    counts[key][field] = issue.at(field);
    }
    return counts;
}

json check_boundary(const json& check);

void publish_section_evidence(ModelCheckWorkflowReport& report, const Model& model, const TriangleMesh* inspected_mesh = nullptr)
{
    if (!report.check.value("repair_evidence_requested", false) || !report.repair.can_continue())
        return;
    // Only the currently published prepared or round-tripped model is measured.
    // The opt-in combined workflow checks completion after observation; legacy
    // callers never enter this path.
    const auto started = std::chrono::steady_clock::now();
    try {
        if (model_inspection_boundary(report.check.at("inspection")).value("status", "") != "defects_detected")
            return;
        const auto reason                          = model.objects.size() == 1 && model.objects.front() ?
                                                         model_reconstruction_ineligibility(*model.objects.front()) :
                                                         std::string("Reconstruction requires exactly one model object.");
        report.check["reconstruction_eligibility"] = {{"status", reason.empty() ? "eligible" : "unsupported"}, {"reason", reason}};
        // Do not flatten multiple parts, modifiers or painted attributes into
        // an apparent reconstructable solid. Their ordinary workflow is intact.
        if (!reason.empty()) {
            report.check["section_evidence"] = {{"status", "not_run"}, {"reason", "reconstruction_unsupported"}};
            return;
        }
        const auto preliminary = check_boundary(report.check).at("reconstruction");
        if (preliminary.value("assessment", "") == "evidence_present") {
            report.check["section_evidence"] = {{"status", "not_run"}, {"reason", "reconstruction_evidence_already_present"}};
            return;
        }
        if (preliminary.value("assessment", "") == "blocked") {
            report.check["section_evidence"] = {{"status", "not_run"}, {"reason", "mesh_evidence_not_completed"}};
            return;
        }
        report.check["section_evidence"] = inspected_mesh ? inspect_model_section_evidence(*inspected_mesh) :
                                                            inspect_model_section_evidence(model.mesh());
    } catch (const std::bad_alloc&) {
        report.check["section_evidence"] = {{"status", "incomplete"}, {"reason", "memory_allocation_failed"}};
    } catch (const std::exception&) {
        report.check["section_evidence"] = {{"status", "failed"}, {"reason", "execution_failed"}};
    } catch (...) {
        report.check["section_evidence"] = {{"status", "failed"}, {"reason", "execution_failed"}};
    }
    // Includes model.mesh() preparation when it could not be reused. The inner
    // elapsed_ms deliberately measures only the shared section assessment.
    report.check["section_evidence"]["workflow_elapsed_ms"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
}

json check_boundary(const json& check)
{
    ModelRepairEvidencePolicy policy;
    policy.allow_boundary_closure = check.value("allow_boundary_closure", false);
    return model_check_boundary(check.value("inspection", json()), check.value("repair_evidence", json::object()),
                                check.value("section_evidence", json::object()), check.value("failure_kind", std::string{}),
                                check.value("reconstruction_eligibility", json::object()), policy);
}

void require_completed_evidence(ModelCheckWorkflowReport& report)
{
    // Reuse the public boundary validation, including coherent plane/region
    // counters. Neither a bare "completed" nor a generic "undetermined" proves
    // that the requested workflow ran to completion.
    const auto  boundary   = check_boundary(report.check);
    const auto& result     = boundary.at("reconstruction");
    const auto  assessment = result.value("assessment", "");
    const auto  reason     = result.value("reason", "");
    if (assessment == "not_needed" && reason == "no_detected_mesh_defects")
        return; // Healthy final mesh: section work is deliberately unnecessary.
    if (result.value("applicability", "") == "eligible" &&
        ((assessment == "undetermined" && reason == "no_conclusive_region_failure") ||
         (assessment == "evidence_present" && (reason == "no_effective_regions" || reason == "proper_crossing_after_repair" ||
                                               reason == "open_boundaries_under_closed_solid_policy"))))
        return;
    report.failure_stage = "repair_evidence";
    // Keep the existing workflow failure kind and CLI exit semantics. The
    // boundary projection preserves a known failing substage/cause separately
    // so, for example, a section plane budget is not reported as an engine fault.
    report.failure_kind  = "repair_evidence_incomplete";
    throw std::runtime_error("Repair evidence did not complete; see the boundary details.");
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
        if (check.value("repair_evidence_requested", false))
            summary["boundary"] = check_boundary(check);
        if (inspection.is_object() && inspection.value("inspection_scope", "full") == "basic")
            summary["inspection_scope"] = "basic";
        // The default contract still includes structural checks. Explicit basic
        // calls declare their narrower scope; incomplete geometry never passes.
        result["model_checks"].push_back(std::move(summary));
    }
    return result;
}

static void run_model_check_workflow_impl(const ModelCheckWorkflowOptions& options, ModelCheckWorkflowReport& report)
{
    report                    = {};
    report.check              = {{"schema_version", 2},
                                 {"repair_evidence_requested", options.repair_evidence},
                                 {"allow_boundary_closure", options.allow_boundary_closure},
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
    if (options.allow_boundary_closure && (!options.advanced_repair || !options.repair_evidence))
        throw std::invalid_argument("Boundary closure evidence requires advanced repair and repair evidence.");
    if (!options.stage_report && !options.reference_file.empty())
        throw std::invalid_argument(L("--model-reference requires --model-stage-report."));
    if (options.stage_report && (!options.export_format.empty() && options.export_format != "stl"))
        throw std::invalid_argument(L("Stage reports support only STL output."));
    std::optional<ModelStageReportContext> stage_context;
    TriangleMesh                           stage_mesh;
    if (options.stage_report)
        stage_context = prepare_model_stage_report(options.input_file, options.reference_file, options.output_directory);
    auto inspection_options = options.inspection_options;
    if (options.inspection_mode != "full" && options.inspection_mode != "basic")
        throw std::invalid_argument("Model inspection mode must be basic or full.");
    inspection_options.analyze_components = options.inspection_mode == "full";
    // The same pair search supplies evidence for the inspected serialized mesh.
    // Normal checks and ordinary advanced repair retain their old work budget.
    if (options.advanced_repair && options.repair_evidence)
        inspection_options.classify_proper_crossings = true;
    std::string source_format = fs::path(options.input_file).extension().string();
    boost::algorithm::to_lower(source_format);
    if (source_format != ".stl" && source_format != ".obj")
        throw std::invalid_argument("--check-model requires one STL or OBJ input.");
    if (!options.export_format.empty() && options.export_format != "obj" && options.export_format != "stl")
        throw std::invalid_argument("Invalid model check export format.");
    if (fs::file_size(options.input_file) > options.max_input_bytes) {
        report.failure_kind = "resource_limit";
        throw std::runtime_error("Model check input exceeds the " + std::to_string(options.max_input_bytes) + " byte limit.");
    }
    DynamicPrintConfig config;
    MeshRepairReport   import_report;
    const bool         repair_stl = source_format == ".stl";
    Model model = load_working_model(options.input_file, config, options.ignore_materials, report.input_materials_ignored, repair_stl,
                                     &import_report);
    report.check["input_materials_ignored"] = report.input_materials_ignored;
    const auto triangle_count               = model.mesh().its.indices.size();
    if (triangle_count == 0 || triangle_count > options.inspection_options.max_triangles) {
        report.failure_kind = triangle_count == 0 ? "input_failed" : "resource_limit";
        throw std::runtime_error("Model check requires 1 to " + std::to_string(options.inspection_options.max_triangles) +
                                 " decoded triangles.");
    }
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

    report.failure_stage  = "io";
    const fs::path parent = fs::absolute(options.output_directory.empty() ? "." : options.output_directory);
    fs::create_directories(parent);
    const fs::path run = parent / fs::unique_path("model-check-%%%%-%%%%-%%%%");
    if (!fs::create_directory(run))
        throw std::runtime_error("Could not create a private model check output directory.");
    // Always persist the decoded baseline: import normalization and OBJ color
    // subdivision mean that an unchanged cleanup count need not equal raw input.
    report.failure_stage                  = "export";
    const fs::path baseline               = run / ("prepared" + source_format);
    report.output_files                   = save_working_model(baseline, model, config);
    report.check["working_model"]         = baseline.string();
    report.check["prepared_model"]        = baseline.string();
    report.check["prepared_output_files"] = report.output_files;
    DynamicPrintConfig working_config;
    bool               ignored = false;
    Model              working = load_working_model(baseline.string(), working_config, options.ignore_materials, ignored);
    report.failure_stage       = "inspection";
    // Model import recenters float vertices and may lose a few coordinate bits
    // when restoring the volume transform. The stage contract must inspect the
    // actual serialized triangles, not a numerically changed reimport view.
    std::string serialized_sha256;
    auto        inspected_baseline  = stage_context ? read_bounded_model_stl(baseline.string(), serialized_sha256) : working.mesh();
    auto        before              = inspect_model_mesh(inspected_baseline, inspection_options);
    report.check["before_advanced"] = before;
    publish_check(report, before, baseline.string(), "serialized_prepared");
    if (stage_context)
        stage_mesh = std::move(inspected_baseline);
    else
        inspected_baseline = TriangleMesh{};
    if (options.advanced_repair) {
        if (before.at("mesh_status") == "passed") {
            report.check["advanced_repair"]["status"] = "not_needed";
        } else if (options.inspection_mode == "basic" && before.at("next_action") == "none") {
            report.check["advanced_repair"]["status"] = "unresolved";
        } else {
            report.failure_stage                      = "repair";
            report.repair_attempted                   = true;
            report.check["advanced_repair"]["status"] = "running";
            ModelRepairOptions repair_options;
            repair_options.allow_partial             = true;
            repair_options.basic_repair              = options.inspection_mode == "basic";
            repair_options.preserve_annotations      = !options.ignore_materials;
            repair_options.allow_annotation_clearing = options.ignore_materials;
            repair_options.remove_non_3d_parts       = options.ignore_materials && !repair_options.basic_repair;
            const bool ok                            = repair_model(working, report.repair, repair_options);
            if (options.repair_evidence)
                report.check["repair_evidence"] = model_repair_boundary(report.repair);
            report.check["advanced_repair"]["engine_status"] = report.repair.status();
            report.check["advanced_repair"]["committed"]     = report.repair.committed;
            // The shared engine returns false for an unchanged unresolved mesh
            // as well as for a fatal transaction. Consult its structured report
            // before interpreting that bool; cancellation/worker/paint failures
            // must still remain fatal even when unresolved items were recorded.
            const bool unchanged_unresolved = !report.repair.committed && report.repair.unresolved != 0;
            if (!report.repair.can_continue() || (!ok && !unchanged_unresolved)) {
                report.failure_kind                               = report.repair.canceled ? "canceled" : "repair_failed";
                report.check["advanced_repair"]["status"]         = "failed";
                report.check["advanced_repair"]["retained_stage"] = "prepared";
                throw std::runtime_error("Advanced repair failed; the prepared working model was retained.");
            }
            if (!report.repair.committed) {
                report.check["advanced_repair"]["status"]         = "unresolved";
                report.check["advanced_repair"]["retained_stage"] = "prepared";
                // App stage reports can retain an unresolved mesh without
                // requesting reconstruction diagnostics that the App will not use.
                if (!options.repair_evidence && !(options.stage_report && options.inspection_mode == "basic")) {
                    report.repair_no_progress = true;
                    throw std::runtime_error("Advanced repair produced no retainable change; see the remaining inspection findings.");
                }
                // Normal no-progress is a model outcome in the opt-in workflow,
                // not an execution error. Still honor requested export/recheck
                // and validate its evidence before reporting operation completed.
            }
        }
    }
    // Export the advanced result or an explicitly requested format conversion.
    // Recheck the serialized/reimported artifact, not a different in-memory mesh.
    if (report.repair.committed || !options.export_format.empty()) {
        report.failure_stage      = "export";
        const std::string  format = options.export_format.empty() ? source_format.substr(1) : options.export_format;
        const fs::path     output = run / ("result." + format);
        const auto         files  = save_working_model(output, working, working_config);
        DynamicPrintConfig reloaded_config;
        Model              reloaded = load_working_model(output.string(), reloaded_config, options.ignore_materials, ignored);
        report.failure_stage        = "inspection";
        auto       retained_mesh    = stage_context ? read_bounded_model_stl(output.string(), serialized_sha256) : reloaded.mesh();
        const auto after            = inspect_model_mesh(retained_mesh, inspection_options);
        if (!options.repair_evidence && !stage_context)
            retained_mesh = TriangleMesh{};
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
        publish_section_evidence(report, reloaded, &retained_mesh);
        if (stage_context)
            stage_mesh = std::move(retained_mesh);
    } else {
        publish_section_evidence(report, working, stage_context ? &stage_mesh : nullptr);
    }
    if (options.advanced_repair && options.repair_evidence)
        require_completed_evidence(report);
    if (stage_context) {
        report.failure_stage = "stage_report";
        write_model_stage_report(*stage_context, options.output_directory, report.check.at("working_model").get<std::string>(), stage_mesh,
                                 report.check.at("inspection"), options.advanced_repair ? "advanced" : "check",
                                 report.check.at("advanced_repair").at("status").get<std::string>(),
                                 check_boundary(report.check).at("reconstruction"));
    }
    report.check["operation_status"] = "completed";
}

void run_model_check_workflow(const ModelCheckWorkflowOptions& options, ModelCheckWorkflowReport& report)
{
    const auto record_failure = [&](const std::string& kind) {
        if (!report.repair_no_progress) {
            if (report.failure_kind.empty())
                report.failure_kind = kind;
            report.check["failure_kind"]  = report.failure_kind;
            report.check["failure_stage"] = report.failure_stage;
            if (report.repair_attempted && options.repair_evidence)
                report.check["repair_evidence"] = model_repair_boundary(report.repair);
        }
    };
    try {
        run_model_check_workflow_impl(options, report);
    } catch (const std::bad_alloc&) {
        record_failure("memory_allocation_failed");
        throw;
    } catch (const ModelInspectionLimitError&) {
        record_failure("resource_limit");
        throw;
    } catch (const fs::filesystem_error&) {
        record_failure("io_failed");
        throw;
    } catch (const std::invalid_argument&) {
        record_failure(report.failure_stage == "input" ? "input_failed" : report.failure_stage + "_failed");
        throw;
    } catch (const std::exception&) {
        record_failure(report.failure_stage == "input" || report.failure_stage == "io" || report.failure_stage == "inspection" ||
                               report.failure_stage == "repair" || report.failure_stage == "export" ?
                           report.failure_stage + "_failed" :
                           "execution_failed");
        throw;
    } catch (...) {
        record_failure("execution_failed");
        throw;
    }
}
} // namespace Slic3r
