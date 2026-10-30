#include "ModelRepairEvidence.hpp"

#include "ModelRepair.hpp"
#include "TriangleMesh.hpp"
#include "TriangleMeshSlicer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace Slic3r {
namespace {
using json                                              = nlohmann::json;
using Clock                                             = std::chrono::steady_clock;
constexpr std::array<const char*, 7> ISSUE_KEYS         = {"boundary",   "nonmanifold_edge", "nonmanifold_vertex", "self_intersection",
                                                           "degenerate", "duplicate",        "orientation"};
constexpr std::size_t                MAX_INPUT_FACES    = 3000000;
constexpr std::size_t                MAX_INPUT_VERTICES = 3000000;
// A work/memory guard, not a geometric repair threshold. Count potential
// triangle/plane intersections before the slicer allocates its line arrays.
constexpr std::size_t MAX_SECTION_INTERSECTIONS = 10000000;
constexpr std::size_t MAX_SECTION_SAMPLES       = 16;
constexpr double      MAX_SECTION_COORDINATE_MM = 1000000.;

const char* slicing_mode_name(MeshSlicingParams::SlicingMode mode)
{
    switch (mode) {
    case MeshSlicingParams::SlicingMode::Regular: return "regular";
    case MeshSlicingParams::SlicingMode::EvenOdd: return "even_odd";
    case MeshSlicingParams::SlicingMode::Positive: return "positive";
    case MeshSlicingParams::SlicingMode::PositiveLargestContour: return "positive_largest_contour";
    }
    return "invalid";
}

bool not_run(const json& value)
{
    return value.is_null() || (value.is_object() && (value.empty() || (value.contains("status") && value["status"] == "not_run")));
}

const json* field(const json& value, const char* key)
{
    if (!value.is_object())
        return nullptr;
    const auto it = value.find(key);
    return it == value.end() ? nullptr : &*it;
}

bool flag(const json& value, const char* key, bool expected = true)
{
    const auto* item = field(value, key);
    return item && item->is_boolean() && item->get<bool>() == expected;
}

std::string text_field(const json& value, const char* key)
{
    const auto* item = field(value, key);
    return item && item->is_string() ? item->get<std::string>() : std::string{};
}

bool count(const json& value, uint64_t& result)
{
    if (value.is_number_unsigned()) {
        result = value.get<uint64_t>();
        return true;
    }
    if (value.is_number_integer() && value.get<int64_t>() >= 0) {
        result = static_cast<uint64_t>(value.get<int64_t>());
        return true;
    }
    return false;
}

bool count_field(const json& value, const char* key, uint64_t& result)
{
    const auto* item = field(value, key);
    return item && count(*item, result);
}

bool geometric_blocker(const std::string& reason)
{
    return std::find(ISSUE_KEYS.begin(), ISSUE_KEYS.end(), reason) != ISSUE_KEYS.end() || reason == "shared_vertex" ||
           reason == "surface_conversion_failed" || reason == "zero_volume" || reason == "ambiguous_contact";
}

std::string failure_category(const std::string& reason)
{
    if (reason == "canceled")
        return "canceled";
    if (reason == "memory_allocation_failed" || reason == "resource_limit" || reason == "timeout" || reason == "time_limit" ||
        reason == "input_limit" || reason == "plane_limit" || reason == "intersection_limit" || reason == "coordinate_limit")
        return "resource";
    if (reason == "invalid_arguments" || reason == "invalid_options" || reason == "input_failed" || reason == "invalid_input")
        return "input";
    if (reason == "io_failed" || reason == "export_failed")
        return "io";
    return reason.empty() ? "none" : "execution";
}

struct EvidenceStop : std::runtime_error
{
    const char* status;
    EvidenceStop(const char* state, const char* reason) : std::runtime_error(reason), status(state) {}
};

json unrun_stage() { return {{"status", "not_run"}}; }
} // namespace

json model_inspection_boundary(const json& inspection)
{
    json result = {{"status", "not_run"},
                   {"known_defects", json::array()},
                   {"incomplete_checks", json::array()},
                   {"structural_blockers", json::array()},
                   {"basic_checks_complete", false},
                   {"inspection_complete", false},
                   {"has_geometry_defects", false},
                   {"printability", "not_assessed"}};
    if (not_run(inspection))
        return result;
    bool        unknown = false, basic_complete = true, repair_needed = false;
    const bool  basic_scope = text_field(inspection, "inspection_scope") == "basic";
    const auto  incomplete = [&](const std::string& key) { result["incomplete_checks"].push_back(key); };
    const auto* issues     = field(inspection, "issues");
    for (const auto* key : ISSUE_KEYS) {
        const auto* issue       = issues ? field(*issues, key) : nullptr;
        if (basic_scope && std::string(key) != "boundary" && issue && text_field(*issue, "status") == "not_requested" &&
            field(*issue, "count") && issue->at("count").is_null() && flag(*issue, "complete", false))
            continue; // Deliberately outside App scope; never report a zero count.
        uint64_t    value       = 0;
        const bool  valid_count = issue && count_field(*issue, "count", value);
        const bool  complete    = issue && flag(*issue, "complete") && valid_count;
        // A measured defect remains a defect even when automatic repair has
        // no applicable operation. Do not report it as passed.
        if (valid_count && value != 0)
            repair_needed = true;
        if (!complete) {
            incomplete(key);
            basic_complete = false;
            unknown        = true;
        }
        // A truncated self-intersection count still proves the observed pairs;
        // do not erase this evidence merely because the total is unknown.
        if (valid_count && value != 0)
            result["known_defects"].push_back(
                {{"check", key}, {"count", value}, {"count_complete", complete && !flag(*issue, "count_is_lower_bound")}});
    }
    if (!flag(inspection, "basic_checks_complete")) {
        incomplete("basic_checks_complete");
        unknown = true;
    }
    const auto* summary_complete = field(inspection, "check_complete");
    const auto  summary_status   = text_field(inspection, "mesh_status");
    if (!summary_complete || !summary_complete->is_boolean() ||
        (summary_status != "passed" && summary_status != "needs_repair" && summary_status != "unknown")) {
        incomplete("inspection_summary");
        unknown = true;
    }
    const auto* components          = field(inspection, "component_analysis");
    bool        structural_complete = basic_scope;
    if (basic_scope) {
        result["inspection_scope"]   = "basic";
        result["component_analysis"] = "not_requested";
    } else if (!components || !components->is_object()) {
        incomplete("component_analysis");
        unknown = true;
    } else {
        uint64_t component_count = 0;
        if (!count_field(*components, "component_count", component_count) || component_count == 0) {
            incomplete("component_analysis.component_count");
            unknown = true;
        }
        for (const auto* key : {"size_complete", "intersections_complete"}) {
            if (!flag(*components, key)) {
                incomplete(std::string("component_analysis.") + key);
                unknown = true;
            }
        }
        if (!flag(*components, "limit_reached", false)) {
            incomplete("component_analysis.limit_reached");
            const bool limited = flag(*components, "limit_reached");
            result["structural_blockers"].push_back(
                {{"reason", limited ? "component_limit" : "unknown_component_limit"}, {"kind", limited ? "resource" : "unknown"}});
            unknown = true;
        }
        const auto* list = field(*components, "components");
        if (!list || !list->is_array()) {
            incomplete("component_analysis.components");
            unknown = true;
        } else {
            // The list is a detail-limited sample, not necessarily every
            // component. Global completion may prove the unlisted components;
            // an incomplete global result cannot prove their blockers known.
            if (list->size() > component_count || (list->size() < component_count && !flag(*components, "containment_complete"))) {
                incomplete("component_analysis.unsampled_blockers");
                unknown = true;
            }
            for (const auto& component : *list) {
                const auto* blocked = field(component, "blocked_by");
                if (!blocked || !blocked->is_array()) {
                    incomplete("component_analysis.blocked_by");
                    unknown = true;
                    continue;
                }
                for (const auto& item : *blocked) {
                    const std::string reason   = item.is_string() ? item.get<std::string>() : "malformed_blocker";
                    const bool        geometry = geometric_blocker(reason);
                    const bool        resource = reason == "component_limit" || reason == "self_intersection_incomplete";
                    json              blocker  = {{"reason", reason}, {"kind", geometry ? "geometry" : resource ? "resource" : "unknown"}};
                    uint64_t          id       = 0;
                    if (count_field(component, "id", id))
                        blocker["component_id"] = id;
                    result["structural_blockers"].push_back(std::move(blocker));
                    if (!geometry)
                        unknown = true;
                }
            }
        }
        structural_complete = flag(*components, "containment_complete") && flag(*components, "size_complete");
        if (!flag(*components, "containment_complete")) {
            incomplete("component_analysis.containment");
            // Known geometric obstruction is not an execution/resource failure.
            // Unexplained or malformed incompleteness remains unknown.
            if (!flag(*components, "containment_complete", false) || result["structural_blockers"].empty())
                unknown = true;
        }
    }
    const bool has_defects          = !result["known_defects"].empty();
    const bool complete             = basic_complete && !unknown && structural_complete && flag(inspection, "check_complete");
    result["basic_checks_complete"] = basic_complete && flag(inspection, "basic_checks_complete");
    result["inspection_complete"]   = complete;
    result["has_geometry_defects"]  = has_defects;
    result["status"]                = unknown                                                       ? "incomplete" :
                                      repair_needed                                                 ? "defects_detected" :
                                      complete && text_field(inspection, "mesh_status") == "passed" ? "passed" :
                                                                                                      "incomplete";
    return result;
}

json model_repair_boundary(const ModelRepairReport& report)
{
    const bool empty = !report.attempted && !report.completed && !report.canceled && !report.committed && !report.success &&
                       report.failed == 0 && report.unresolved == 0 && report.volumes == 0 && report.volume_reports.empty() &&
                       report.failures.empty();
    const bool  execution_ok = report.can_continue() && std::string(report.status()) != "failed";
    const char* state        = report.canceled    ? "canceled" :
                               report.failed != 0 ? "failed" :
                               empty              ? "not_run" :
                               report.completed   ? (execution_ok ? "completed" : "failed") :
                                                    "incomplete";
    return {{"schema_version", 1},
            {"status", state},
            {"repair_status", empty ? "not_run" : report.status()},
            {"attempted", report.attempted},
            {"completed", report.completed},
            {"canceled", report.canceled},
            {"failed", report.failed},
            {"unresolved", report.unresolved},
            {"changed", report.committed},
            {"execution_completed", execution_ok},
            {"engine_mesh_validated", !empty && execution_ok && report.mesh_validated()},
            {"full_inspection", "not_established_by_repair_report"},
            {"printability", "not_assessed"}};
}

json inspect_model_section_evidence(const TriangleMesh& mesh, const ModelSectionEvidenceOptions& options)
{
    const auto& params  = options.slicing_params;
    json        result  = {{"status", "incomplete"},
                           {"reason", "not_started"},
                           {"stage", "input_validation"},
                           {"spacing_mm", options.slice_zs ? json(nullptr) : json(options.spacing_mm)},
                           {"max_planes", options.max_planes},
                           {"timeout_ms", options.timeout_ms},
                           {"planes", 0},
                           {"requested_planes", nullptr},
                           {"axes", json::array()},
                           {"remaining_open_chains", nullptr},
                           {"open_chains_before_gap_closing", nullptr},
                           {"gap_closed_planes", nullptr},
                           {"remaining_open_length_mm", nullptr},
                           {"effective_regions", nullptr},
                           {"effective_area_sum_mm2", nullptr},
                           {"empty_region_planes", nullptr},
                           {"nondegenerate_closed_loop_planes", nullptr},
                           {"empty_region_planes_with_closed_loops", nullptr},
                           {"all_effective_regions_empty", nullptr},
                           {"samples", json::array()},
                           {"samples_truncated", false},
                           {"counts_scope", "completed_planes_only"},
                           {"area_scope", "sum_of_cross_section_areas_not_volume"},
                           {"coverage", options.slice_zs ? "caller_supplied_z_planes" : "reference_z_midplanes"},
                           {"time_limit", "cooperative"},
                           {"printer_layer_plan", options.slice_zs.has_value() && options.printer_layer_plan},
                           {"printability", "not_assessed"}};
    const auto  started = Clock::now();
    const auto  tick    = [&] {
        if (options.is_canceled && options.is_canceled())
            throw EvidenceStop("canceled", "canceled");
        if (Clock::now() - started >= std::chrono::milliseconds(options.timeout_ms))
            throw EvidenceStop("incomplete", "time_limit");
    };
    try {
        if ((!options.slice_zs && (!std::isfinite(options.spacing_mm) || options.spacing_mm <= 0)) || options.max_planes == 0 ||
            options.timeout_ms == 0 || (options.printer_layer_plan && !options.slice_zs) || !params.trafo.matrix().allFinite() ||
            !std::isfinite(params.closing_radius) || params.closing_radius < 0 || !std::isfinite(params.extra_offset) ||
            !std::isfinite(params.resolution) || params.resolution < 0 || std::string(slicing_mode_name(params.mode)) == "invalid" ||
            std::string(slicing_mode_name(params.mode_below)) == "invalid")
            throw EvidenceStop("failed", "invalid_options");
        const double determinant = params.trafo.linear().determinant();
        if (!std::isfinite(determinant) || determinant <= 0 || params.trafo.matrix().row(3) != Eigen::RowVector4d(0., 0., 0., 1.))
            throw EvidenceStop("incomplete", "unsupported_transform");
        const double coordinate_margin = std::abs(double(params.extra_offset)) + 2. * params.closing_radius + params.resolution;
        if (!std::isfinite(coordinate_margin) || coordinate_margin > MAX_SECTION_COORDINATE_MM)
            throw EvidenceStop("incomplete", "coordinate_limit");
        result["parameters"] = {{"mode", slicing_mode_name(params.mode)},
                                {"mode_below", slicing_mode_name(params.mode_below)},
                                {"slicing_mode_normal_below_layer", params.slicing_mode_normal_below_layer},
                                {"closing_radius_mm", params.closing_radius},
                                {"extra_offset_mm", params.extra_offset},
                                {"resolution_mm", params.resolution},
                                {"transform", json::array()}};
        for (int row = 0; row < 4; ++row) {
            json values = json::array();
            for (int column = 0; column < 4; ++column)
                values.push_back(params.trafo.matrix()(row, column));
            result["parameters"]["transform"].push_back(std::move(values));
        }
        // Shrinking and simplification may deliberately erase fine sections.
        // Active non-Regular modes are added after the layer plan is known.
        result["parameters_can_remove_regions"] = params.extra_offset < 0 || params.resolution > 0;
        tick();
        if (mesh.its.indices.size() > MAX_INPUT_FACES || mesh.its.vertices.size() > MAX_INPUT_VERTICES)
            throw EvidenceStop("incomplete", "input_limit");
        if (mesh.its.indices.empty() || mesh.its.vertices.empty())
            throw EvidenceStop("failed", "invalid_input");
        double     minimum_z = std::numeric_limits<double>::infinity(), maximum_z = -minimum_z;
        const auto transformed_z = [&](const Vec3f& vertex) { return (params.trafo * vertex.cast<double>()).z(); };
        for (std::size_t i = 0; i < mesh.its.vertices.size(); ++i) {
            if ((i & 4095) == 0)
                tick();
            const auto& vertex = mesh.its.vertices[i];
            if (!vertex.allFinite())
                throw EvidenceStop("failed", "invalid_input");
            const Vec3d transformed = params.trafo * vertex.cast<double>();
            // Leave ample headroom for Clipper arithmetic and offsets. This
            // is a coordinate/resource guard, not a printable-bed restriction.
            if (!transformed.allFinite() || transformed.cwiseAbs().maxCoeff() + coordinate_margin > MAX_SECTION_COORDINATE_MM)
                throw EvidenceStop("incomplete", "coordinate_limit");
            minimum_z = std::min(minimum_z, transformed.z());
            maximum_z = std::max(maximum_z, transformed.z());
        }
        for (std::size_t i = 0; i < mesh.its.indices.size(); ++i) {
            if ((i & 4095) == 0)
                tick();
            for (int corner = 0; corner < 3; ++corner) {
                const int id = mesh.its.indices[i][corner];
                if (id < 0 || std::size_t(id) >= mesh.its.vertices.size())
                    throw EvidenceStop("failed", "invalid_input");
            }
        }
        result["stage"] = "plane_planning";
        std::vector<float> reference_planes;
        if (!options.slice_zs) {
            const double extent = maximum_z - minimum_z;
            const double needed = std::ceil(extent / options.spacing_mm);
            if (!(extent > 0))
                throw EvidenceStop("incomplete", "degenerate_extent");
            if (!std::isfinite(needed) || needed < 1 || needed > double(options.max_planes))
                throw EvidenceStop("incomplete", "plane_limit");
            const auto n = static_cast<std::size_t>(needed);
            reference_planes.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                if ((i & 4095) == 0)
                    tick();
                const float z = float(minimum_z + (i + 0.5) * extent / n);
                if (!(z > minimum_z && z < maximum_z) || (!reference_planes.empty() && z <= reference_planes.back()))
                    throw EvidenceStop("incomplete", "coordinate_precision");
                reference_planes.push_back(z);
            }
        }
        const auto& planes         = options.slice_zs ? *options.slice_zs : reference_planes;
        result["requested_planes"] = planes.size();
        if (planes.size() > options.max_planes)
            throw EvidenceStop("incomplete", "plane_limit");
        if (planes.empty())
            throw EvidenceStop("failed", "invalid_options");
        for (std::size_t i = 0; i < planes.size(); ++i) {
            if (!std::isfinite(planes[i]) || (i != 0 && planes[i] <= planes[i - 1]))
                throw EvidenceStop("failed", "invalid_options");
            if (std::abs(planes[i]) > MAX_SECTION_COORDINATE_MM)
                throw EvidenceStop("incomplete", "coordinate_limit");
        }
        result["parameters_can_remove_regions"] = params.extra_offset < 0 || params.resolution > 0 ||
                                                  (params.slicing_mode_normal_below_layer < planes.size() &&
                                                   params.mode != MeshSlicingParams::SlicingMode::Regular) ||
                                                  (params.slicing_mode_normal_below_layer > 0 &&
                                                   params.mode_below != MeshSlicingParams::SlicingMode::Regular);
        result["z_range_mm"]      = {planes.front(), planes.back()};
        result["stage"]           = "work_estimation";
        std::size_t intersections = 0;
        for (std::size_t i = 0; i < mesh.its.indices.size(); ++i) {
            if ((i & 4095) == 0)
                tick();
            const auto& face = mesh.its.indices[i];
            const float z0   = float(transformed_z(mesh.its.vertices[face[0]]));
            const float z1   = float(transformed_z(mesh.its.vertices[face[1]]));
            const float z2   = float(transformed_z(mesh.its.vertices[face[2]]));
            const float low = std::min({z0, z1, z2}), high = std::max({z0, z1, z2});
            const auto  potential = static_cast<std::size_t>(std::upper_bound(planes.begin(), planes.end(), high) -
                                                             std::lower_bound(planes.begin(), planes.end(), low));
            if (potential > MAX_SECTION_INTERSECTIONS - intersections)
                throw EvidenceStop("incomplete", "intersection_limit");
            intersections += potential;
        }
        result["potential_triangle_plane_intersections"] = intersections;
        result["stage"]                                  = "section_and_region_construction";
        const auto cut_started                           = Clock::now();
        auto       observations                          = slice_mesh_ex_with_diagnostics(mesh.its, planes, params, tick);
        result["region_construction_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - cut_started).count();
        if (observations.diagnostics.size() != planes.size() || observations.slices.size() != planes.size())
            throw EvidenceStop("failed", "invalid_section_result");
        result["stage"]  = "summary";
        std::size_t open = 0, before_gap = 0, bridged = 0, regions = 0, empty_planes = 0, closed_planes = 0, canceled_planes = 0;
        double      open_length = 0, effective_area = 0;
        for (std::size_t i = 0; i < planes.size(); ++i) {
            tick();
            const auto&  d        = observations.diagnostics[i];
            const auto&  slice    = observations.slices[i];
            const double area_mm2 = area(slice) * SCALING_FACTOR * SCALING_FACTOR;
            if (!std::isfinite(area_mm2) || area_mm2 < 0 || !std::isfinite(d.closed_loop_area_mm2) || d.closed_loop_area_mm2 < 0)
                throw EvidenceStop("failed", "invalid_section_result");
            const bool closed = d.closed_loops != 0 && d.closed_loop_area_mm2 > 0;
            const bool empty  = slice.empty();
            open += d.remaining_open_chains;
            before_gap += d.open_chains_before_gap_closing;
            bridged += d.gap_closing_used;
            open_length += d.remaining_open_length_mm;
            regions += slice.size();
            effective_area += area_mm2;
            empty_planes += empty;
            closed_planes += closed;
            canceled_planes += closed && empty;
            if (empty || d.remaining_open_chains != 0 || d.gap_closing_used) {
                if (result["samples"].size() < MAX_SECTION_SAMPLES)
                    result["samples"].push_back({{"plane_index", i},
                                                 {"z_mm", planes[i]},
                                                 {"effective_regions", slice.size()},
                                                 {"effective_area_mm2", area_mm2},
                                                 {"closed_loops", d.closed_loops},
                                                 {"closed_loop_area_mm2", d.closed_loop_area_mm2},
                                                 {"remaining_open_chains", d.remaining_open_chains},
                                                 {"gap_closing_used", d.gap_closing_used}});
                else
                    result["samples_truncated"] = true;
            }
            // Geometry is only needed for this summary, not the JSON report.
            observations.slices[i] = ExPolygons{};
        }
        tick();
        result["axes"].push_back({{"axis", "Z"}, {"requested_planes", planes.size()}, {"completed_planes", planes.size()}});
        result["planes"]                                = planes.size();
        result["remaining_open_chains"]                 = open;
        result["open_chains_before_gap_closing"]        = before_gap;
        result["gap_closed_planes"]                     = bridged;
        result["remaining_open_length_mm"]              = open_length;
        result["effective_regions"]                     = regions;
        result["effective_area_sum_mm2"]                = effective_area;
        result["empty_region_planes"]                   = empty_planes;
        result["nondegenerate_closed_loop_planes"]      = closed_planes;
        result["empty_region_planes_with_closed_loops"] = canceled_planes;
        result["all_effective_regions_empty"]           = regions == 0;
        result["status"]                                = "completed";
        result["reason"]                                = "observed";
        result["stage"]                                 = "completed";
    } catch (const EvidenceStop& stop) {
        result["status"] = stop.status;
        result["reason"] = stop.what();
    } catch (const std::bad_alloc&) {
        result["status"] = "incomplete";
        result["reason"] = "memory_allocation_failed";
    } catch (const std::exception&) {
        result["status"] = "failed";
        result["reason"] = "execution_failed";
    } catch (...) {
        result["status"] = "failed";
        result["reason"] = "execution_failed";
    }
    if (result["status"] != "completed") {
        // No partial summary is accepted as a completed plane observation.
        result["samples"]           = json::array();
        result["samples_truncated"] = false;
    }
    result["elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    return result;
}

json model_check_boundary(const json&                      inspection,
                          const json&                      advanced,
                          const json&                      sections,
                          const std::string&               failure_kind,
                          const json&                      reconstruction_eligibility,
                          const ModelRepairEvidencePolicy& policy)
{
    const auto inspected      = model_inspection_boundary(inspection);
    const json repair         = not_run(advanced) ? unrun_stage() : advanced;
    const json observed       = sections.is_null() || (sections.is_object() && sections.empty()) ? unrun_stage() : sections;
    json       reconstruction = {{"assessment", "undetermined"},   {"reason", "insufficient_evidence"}, {"applicability", "not_evaluated"},
                                 {"necessity", "not_established"}, {"automatic_adoption", false},       {"printability", "not_assessed"}};
    reconstruction["policy"]  = {{"version", 1}, {"allow_boundary_closure", policy.allow_boundary_closure}};
    const json crossings      = field(inspection, "proper_crossings") ? inspection["proper_crossings"] : unrun_stage();
    // Validate the existence summary, not the display-limited intersection list.
    // A positive witness can stop classification early, but an incomplete pair
    // search must never become a completed recommendation in this workflow.
    uint64_t    checked_pairs = 0, total_pairs = 0, self_intersections = 0;
    const auto* issues        = field(inspection, "issues");
    const auto* self_issue    = issues ? field(*issues, "self_intersection") : nullptr;
    const auto* detected      = field(crossings, "detected");
    const auto* witness       = field(crossings, "witness");
    // Unversioned summaries are the original strict triangle-pair contract.
    // The bounded adjacency extension must declare its scope and completion;
    // otherwise an exhausted neighborhood search could look like a negative.
    const bool  versioned_crossings = field(crossings, "schema_version") != nullptr;
    uint64_t    crossing_version = 0, candidates = 0, candidate_limit = 0, scanned = 0, face_limit = 0;
    const auto* neighborhood = field(crossings, "neighborhood");
    const bool  classification_complete =
        versioned_crossings ?
             (count_field(crossings, "schema_version", crossing_version) && crossing_version == 2 &&
             text_field(crossings, "classification_scope") == "triangle_interiors_and_coplanar_edge_patches" &&
             flag(crossings, "classification_complete") && flag(crossings, "budget_exhausted", false) && neighborhood &&
             (text_field(*neighborhood, "reason") == "not_required" || text_field(*neighborhood, "reason") == "completed") &&
             count_field(*neighborhood, "candidates", candidates) && count_field(*neighborhood, "candidate_limit", candidate_limit) &&
             candidates <= candidate_limit && count_field(*neighborhood, "faces_scanned", scanned) &&
             count_field(*neighborhood, "face_limit", face_limit) && scanned <= face_limit) :
             (!field(crossings, "classification_scope") && !field(crossings, "classification_complete") &&
             !field(crossings, "budget_exhausted") && !neighborhood);
    bool        witness_valid = false;
    if (witness && witness->is_object()) {
        const auto* faces   = field(*witness, "faces");
        const auto* segment = field(*witness, "segment");
        uint64_t    first = 0, second = 0, triangle_count = 0;
        witness_valid = faces && faces->is_array() && faces->size() == 2 && count((*faces)[0], first) && count((*faces)[1], second) &&
                        first < second && count_field(inspection, "triangle_count", triangle_count) && second < triangle_count && segment &&
                        segment->is_array() && segment->size() == 2;
        if (witness_valid) {
            for (const auto& point : *segment) {
                if (!point.is_array() || point.size() != 3) {
                    witness_valid = false;
                    break;
                }
                for (const auto& coordinate : point)
                    if (!coordinate.is_number() || !std::isfinite(coordinate.get<double>()))
                        witness_valid = false;
            }
            witness_valid = witness_valid && (*segment)[0] != (*segment)[1];
        }
        if (witness_valid && versioned_crossings) {
            const auto  kind    = text_field(*witness, "kind");
            const auto* support = field(*witness, "support_faces");
            witness_valid       = text_field(*witness, "source") == "intersection_pair" && support && support->is_array() &&
                            support->size() == 2 && (kind == "triangle_interiors" || kind == "coplanar_edge_patches");
            std::array<uint64_t, 4> supporting_faces{};
            std::size_t             support_count = 0;
            if (witness_valid) {
                for (std::size_t side = 0; side < 2 && witness_valid; ++side) {
                    const auto& patch           = (*support)[side];
                    bool        contains_anchor = false;
                    if (!patch.is_array() || patch.empty() || patch.size() > 2) {
                        witness_valid = false;
                        break;
                    }
                    for (const auto& id : patch) {
                        uint64_t face_id = 0;
                        if (!count(id, face_id) || face_id >= triangle_count ||
                            std::find(supporting_faces.begin(), supporting_faces.begin() + support_count, face_id) !=
                                supporting_faces.begin() + support_count) {
                            witness_valid = false;
                            break;
                        }
                        supporting_faces[support_count++] = face_id;
                        contains_anchor                   = contains_anchor || face_id == (side == 0 ? first : second);
                    }
                    witness_valid = witness_valid && contains_anchor;
                }
                // At least one original triangle must strictly cross. Allowing
                // two reconstructed patches could regroup touching shells into
                // a fictitious crossing, especially at T-junctions.
                witness_valid = witness_valid && (kind == "triangle_interiors" ? support_count == 2 : support_count == 3);
                if (kind == "coplanar_edge_patches")
                    witness_valid = witness_valid && neighborhood && text_field(*neighborhood, "reason") == "completed" && candidates > 0 &&
                                    scanned == triangle_count;
            }
        } else if (witness_valid) {
            // Removing only version metadata must not reinterpret a patch
            // witness as the historical strict two-triangle proof.
            witness_valid = !field(*witness, "kind") && !field(*witness, "source") && !field(*witness, "support_faces");
        }
    }
    const bool crossing_summary_complete = classification_complete && text_field(crossings, "status") == "completed" &&
                                           flag(crossings, "search_complete") && count_field(crossings, "pairs_checked", checked_pairs) &&
                                           count_field(crossings, "total_pairs", total_pairs) && checked_pairs <= total_pairs &&
                                           self_issue && flag(*self_issue, "complete") &&
                                           count_field(*self_issue, "count", self_intersections) && total_pairs == self_intersections &&
                                           detected && detected->is_boolean() &&
                                           (detected->get<bool>() ? (checked_pairs > 0 && witness_valid) :
                                                                    (checked_pairs == total_pairs && witness && witness->is_null()));
    const bool  proper_crossing     = crossing_summary_complete && flag(crossings, "detected");
    const bool  invalid_crossings   = field(inspection, "proper_crossings") && !crossing_summary_complete;
    uint64_t    open_edges          = 0;
    const auto* boundary_issue      = issues ? field(*issues, "boundary") : nullptr;
    const bool  open_solid_boundary = policy.allow_boundary_closure && boundary_issue && flag(*boundary_issue, "complete") &&
                                     count_field(*boundary_issue, "count", open_edges) && open_edges > 0;
    const auto set_assessment = [&](const char* assessment, const char* reason) {
        reconstruction["assessment"] = assessment;
        reconstruction["reason"]     = reason;
    };
    const auto inspection_status = text_field(inspected, "status"), repair_status = text_field(repair, "status");
    const auto section_status = text_field(observed, "status");
    const auto eligibility    = text_field(reconstruction_eligibility, "status");
    if (eligibility == "eligible" || eligibility == "unsupported") {
        reconstruction["applicability"]        = eligibility;
        reconstruction["applicability_reason"] = text_field(reconstruction_eligibility, "reason");
    }
    const auto coverage                  = text_field(observed, "coverage");
    reconstruction["evidence_scope"]     = coverage.empty() ? "not_assessed" : coverage;
    reconstruction["printer_layer_plan"] = flag(observed, "printer_layer_plan");
    uint64_t    failures = 0, planes = 0, requested = 0, regions = 0, closed_planes = 0, empty_closed_planes = 0, empty_planes = 0;
    uint64_t    axis_completed = 0, axis_requested = 0;
    const auto* effective_area = field(observed, "effective_area_sum_mm2");
    const bool  area_valid     = effective_area && effective_area->is_number() && std::isfinite(effective_area->get<double>()) &&
                            effective_area->get<double>() >= 0;
    const bool repair_complete = repair_status == "completed" && flag(repair, "execution_completed") && flag(repair, "completed") &&
                                 flag(repair, "canceled", false) && count_field(repair, "failed", failures) && failures == 0;
    const bool sections_complete = section_status == "completed" && count_field(observed, "planes", planes) && planes > 0 &&
                                   count_field(observed, "requested_planes", requested) && planes == requested && field(observed, "axes") &&
                                   observed["axes"].is_array() && observed["axes"].size() == 1 &&
                                   text_field(observed["axes"][0], "axis") == "Z" &&
                                   count_field(observed["axes"][0], "requested_planes", axis_requested) && axis_requested == planes &&
                                   count_field(observed["axes"][0], "completed_planes", axis_completed) && axis_completed == planes &&
                                   (coverage == "reference_z_midplanes" || coverage == "caller_supplied_z_planes") &&
                                   count_field(observed, "effective_regions", regions) && area_valid &&
                                   count_field(observed, "empty_region_planes", empty_planes) && empty_planes <= planes &&
                                   count_field(observed, "nondegenerate_closed_loop_planes", closed_planes) && closed_planes <= planes &&
                                   count_field(observed, "empty_region_planes_with_closed_loops", empty_closed_planes) &&
                                   empty_closed_planes <= closed_planes && empty_closed_planes <= empty_planes &&
                                   flag(observed, "all_effective_regions_empty", regions == 0) &&
                                   (regions == 0 ? (empty_planes == planes && effective_area->get<double>() == 0.) :
                                                   (empty_planes < planes && effective_area->get<double>() > 0.));
    const bool no_effective_regions = sections_complete && flag(observed, "all_effective_regions_empty") && regions == 0 &&
                                      closed_planes > 0 && empty_closed_planes == closed_planes &&
                                      flag(observed, "parameters_can_remove_regions", false);
    const auto record_stage_failure = [&](const char* stage, const std::string& cause, const std::string& category) {
        reconstruction["failure_stage"]    = stage;
        reconstruction["failure_category"] = category;
        if (!cause.empty())
            reconstruction["failure_cause"] = cause;
    };
    const auto record_evidence_failure = [&] {
        // Only a failed/incomplete substage can refine the wrapper error. A
        // completed observation's stale or contradictory reason cannot do so.
        if (section_status == "failed" || section_status == "canceled" || section_status == "incomplete") {
            const auto cause = section_status == "canceled" ? std::string("canceled") : text_field(observed, "reason");
            record_stage_failure("sections", cause, cause.empty() ? "execution" : failure_category(cause));
            return;
        }
        if (inspection_status == "incomplete") {
            const auto* components = field(inspection, "component_analysis");
            if (components && flag(*components, "limit_reached")) {
                record_stage_failure("inspection", "component_limit", "resource");
                return;
            }
            if (components && self_issue && flag(*self_issue, "complete", false) && flag(*components, "intersections_complete", false)) {
                record_stage_failure("inspection", "self_intersection_incomplete", "resource");
                return;
            }
        }
        uint64_t    crossing_schema = 0;
        const auto* neighborhood    = field(crossings, "neighborhood");
        const auto  crossing_reason = neighborhood ? text_field(*neighborhood, "reason") : std::string{};
        if (count_field(crossings, "schema_version", crossing_schema) && crossing_schema == 2 &&
            text_field(crossings, "status") == "incomplete" && flag(crossings, "classification_complete", false) &&
            flag(crossings, "budget_exhausted") && (crossing_reason == "candidate_limit" || crossing_reason == "face_limit"))
            record_stage_failure("proper_crossings", crossing_reason, "resource");
    };
    if (!failure_kind.empty()) {
        set_assessment("blocked", "workflow_failure");
        reconstruction["failure_kind"]     = failure_kind;
        reconstruction["failure_category"] = failure_category(failure_kind);
        if (failure_kind == "repair_evidence_incomplete")
            record_evidence_failure();
    } else if (repair_status == "failed" || repair_status == "canceled" || repair_status == "incomplete") {
        set_assessment("blocked", "repair_not_completed");
    } else if (section_status == "failed" || section_status == "canceled" || section_status == "incomplete") {
        set_assessment("blocked", "section_evidence_not_completed");
        record_evidence_failure();
    } else if (section_status == "completed" && !sections_complete) {
        set_assessment("blocked", "invalid_section_evidence");
    } else if (inspection_status == "incomplete") {
        set_assessment("blocked", "inspection_incomplete");
        record_evidence_failure();
    } else if (invalid_crossings) {
        set_assessment("blocked", "proper_crossing_evidence_incomplete");
        record_evidence_failure();
    } else if (inspection_status == "passed") {
        set_assessment("not_needed", "no_detected_mesh_defects");
    } else if (inspection_status == "defects_detected" && eligibility == "unsupported") {
        set_assessment("blocked", "reconstruction_unsupported");
    } else if (inspection_status == "defects_detected" && repair_complete && (proper_crossing || open_solid_boundary)) {
        if (eligibility == "eligible") {
            set_assessment("evidence_present",
                           proper_crossing ? "proper_crossing_after_repair" : "open_boundaries_under_closed_solid_policy");
            reconstruction["evidence_scope"] = "retained_mesh_geometry";
        } else {
            set_assessment("undetermined", "reconstruction_eligibility_not_evaluated");
        }
    } else if (inspection_status == "defects_detected" && repair_complete && no_effective_regions && eligibility == "eligible") {
        // An observed failure to form any regions is stronger than defect
        // counts or open chains. It is still limited to this Z plan and its
        // parameters, not proof of design intent or reconstruction necessity.
        set_assessment("evidence_present", "no_effective_regions");
    } else if (inspection_status == "defects_detected" && repair_complete && no_effective_regions) {
        set_assessment("undetermined", "reconstruction_eligibility_not_evaluated");
    } else if (inspection_status == "defects_detected" && repair_complete && sections_complete) {
        set_assessment("undetermined", "no_conclusive_region_failure");
    }
    return {{"schema_version", 2},           {"inspection", inspected}, {"repair", repair},
            {"proper_crossings", crossings}, {"sections", observed},    {"reconstruction", reconstruction}};
}

json model_reconstruction_boundary(const json& report)
{
    json result = {{"schema_version", 1},
                   {"status", "not_run"},
                   {"mesh_validated", nullptr},
                   {"metrics", json::object()},
                   {"automatic_adoption", false},
                   {"shape_structure_guaranteed", false},
                   {"printability", "not_assessed"},
                   {"risks", {"shape_or_internal_structure_may_change", "sampled_distances_are_not_global_error_bounds"}}};
    if (not_run(report))
        return result;
    const std::string status = text_field(report, "status");
    result["status"]         = status.empty() ? "unknown" : status;
    if (const auto* validated = field(report, "mesh_validated"); validated && validated->is_boolean())
        result["mesh_validated"] = *validated;
    if (const auto* metrics = field(report, "metrics"); metrics && metrics->is_object())
        result["metrics"] = *metrics;
    result["shape_confirmation"] = text_field(report, "shape_confirmation");
    if (const auto* engine = field(report, "engine"); engine && engine->is_object()) {
        if (const auto* closure = field(*engine, "boundary_closure"); closure && closure->is_object())
            result["boundary_closure"] = *closure;
    }
    return result;
}
} // namespace Slic3r
