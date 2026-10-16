#include <catch2/catch.hpp>

#include "libslic3r/ModelInspection.hpp"
#include "libslic3r/ModelRepair.hpp"
#include "libslic3r/ModelRepairEvidence.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Slic3r;
using json = nlohmann::json;

namespace {
TriangleMesh missing_side(int axis = 0, float coordinate = 0.f, double depth = 10.)
{
    auto mesh = its_make_cube(10., depth, 10.);
    mesh.indices.erase(std::remove_if(mesh.indices.begin(), mesh.indices.end(),
                                      [&](const Vec3i32& face) {
                                          return mesh.vertices[face[0]][axis] == coordinate && mesh.vertices[face[1]][axis] == coordinate &&
                                                 mesh.vertices[face[2]][axis] == coordinate;
                                      }),
                       mesh.indices.end());
    return TriangleMesh(std::move(mesh));
}

json healthy_inspection()
{
    json report = {{"issues", json::object()},
                   {"basic_checks_complete", true},
                   {"check_complete", true},
                   {"mesh_status", "passed"},
                   {"component_analysis",
                    {{"component_count", 1},
                     {"size_complete", true},
                     {"containment_complete", true},
                     {"intersections_complete", true},
                     {"limit_reached", false},
                     {"components", {{{"id", 0}, {"blocked_by", json::array()}}}}}}};
    for (const auto* key :
         {"boundary", "nonmanifold_edge", "nonmanifold_vertex", "self_intersection", "degenerate", "duplicate", "orientation"})
        report["issues"][key] = {{"count", 0}, {"complete", true}};
    return report;
}

json residual_inspection()
{
    auto report                           = healthy_inspection();
    report["issues"]["boundary"]["count"] = 4;
    report["mesh_status"]                 = "needs_repair";
    return report;
}

ModelRepairReport unresolved_repair()
{
    ModelRepairReport report;
    report.attempted  = true;
    report.completed  = true;
    report.unresolved = 1;
    return report;
}

json completed_sections(std::size_t open = 0, std::size_t gaps = 0, bool empty_regions = false)
{
    return {{"status", "completed"},
            {"planes", 3},
            {"requested_planes", 3},
            {"remaining_open_chains", open},
            {"gap_closed_planes", gaps},
            {"axes", {{{"axis", "Z"}, {"requested_planes", 3}, {"completed_planes", 3}}}},
            {"coverage", "reference_z_midplanes"},
            {"printer_layer_plan", false},
            {"effective_regions", empty_regions ? 0 : 3},
            {"empty_region_planes", empty_regions ? 3 : 0},
            {"effective_area_sum_mm2", empty_regions ? 0. : 300.},
            {"nondegenerate_closed_loop_planes", 3},
            {"empty_region_planes_with_closed_loops", empty_regions ? 3 : 0},
            {"all_effective_regions_empty", empty_regions},
            {"parameters_can_remove_regions", false}};
}

const json eligible_reconstruction = {{"status", "eligible"}, {"reason", ""}};
} // namespace

TEST_CASE("Repair evidence distinguishes healthy and known defective inspections", "[RepairEvidence]")
{
    const auto healthy = model_inspection_boundary(healthy_inspection());
    REQUIRE(healthy["status"] == "passed");
    REQUIRE(healthy["inspection_complete"] == true);
    REQUIRE(healthy["known_defects"].empty());
    REQUIRE(healthy["printability"] == "not_assessed");

    auto defective                                                 = residual_inspection();
    defective["check_complete"]                                    = false;
    defective["component_analysis"]["containment_complete"]        = false;
    defective["component_analysis"]["components"][0]["blocked_by"] = {"self_intersection"};
    const auto result                                              = model_inspection_boundary(defective);
    REQUIRE(result["status"] == "defects_detected");
    REQUIRE(result["inspection_complete"] == false);
    REQUIRE(result["known_defects"][0]["check"] == "boundary");
    REQUIRE(result["known_defects"][0]["count"] == 4);
    REQUIRE(result["structural_blockers"][0]["kind"] == "geometry");
    REQUIRE_FALSE(result["incomplete_checks"].empty());
}

TEST_CASE("Partial inspection preserves known defects without turning unknown into geometry", "[RepairEvidence]")
{
    auto input                                                 = residual_inspection();
    input["issues"]["self_intersection"]                       = {{"count", 20}, {"complete", false}};
    input["basic_checks_complete"]                             = false;
    input["check_complete"]                                    = false;
    input["component_analysis"]["intersections_complete"]      = false;
    input["component_analysis"]["components"][0]["blocked_by"] = {"self_intersection_incomplete"};
    const auto result                                          = model_inspection_boundary(input);
    REQUIRE(result["status"] == "incomplete");
    REQUIRE(result["known_defects"].size() == 2);
    REQUIRE(result["known_defects"][1]["count"] == 20);
    REQUIRE(result["known_defects"][1]["count_complete"] == false);
    REQUIRE(result["structural_blockers"][0]["kind"] == "resource");

    for (const std::string reason : {"component_limit", "future_unknown_reason", "memory_allocation_failed"}) {
        auto report                                                 = residual_inspection();
        report["component_analysis"]["components"][0]["blocked_by"] = {reason};
        REQUIRE(model_inspection_boundary(report)["status"] == "incomplete");
    }
}

TEST_CASE("Malformed inspection data never proves a pass", "[RepairEvidence]")
{
    REQUIRE(model_inspection_boundary(json())["status"] == "not_run");
    REQUIRE(model_inspection_boundary(json::object())["status"] == "not_run");
    for (const json value : {json(1), json("passed"), json::array(), json{{"check_complete", true}}})
        REQUIRE(model_inspection_boundary(value)["status"] == "incomplete");
    for (const json value : {json(-1), json("0"), json(nullptr), json(0.5)}) {
        auto report                           = healthy_inspection();
        report["issues"]["boundary"]["count"] = value;
        REQUIRE(model_inspection_boundary(report)["status"] == "incomplete");
    }
    for (const json value : {json(1), json("true"), json(nullptr)}) {
        auto report                              = healthy_inspection();
        report["issues"]["boundary"]["complete"] = value;
        REQUIRE(model_inspection_boundary(report)["status"] == "incomplete");
    }
    auto report                                = healthy_inspection();
    report["component_analysis"]["components"] = nullptr;
    REQUIRE(model_inspection_boundary(report)["status"] == "incomplete");
    report                                        = healthy_inspection();
    report["component_analysis"]["limit_reached"] = true;
    REQUIRE(model_inspection_boundary(report)["status"] == "incomplete");
}

TEST_CASE("Truncated component samples cannot hide unknown structural blockers", "[RepairEvidence]")
{
    auto report                                                 = residual_inspection();
    report["component_analysis"]["component_count"]             = 2;
    report["component_analysis"]["details_truncated"]           = true;
    report["component_analysis"]["containment_complete"]        = false;
    report["component_analysis"]["components"][0]["blocked_by"] = {"self_intersection"};
    REQUIRE(model_inspection_boundary(report)["status"] == "incomplete");
    // Detail truncation by itself is not a resource failure when the producer
    // reports its aggregate structure checks complete.
    report                                            = healthy_inspection();
    report["component_analysis"]["component_count"]   = 2;
    report["component_analysis"]["details_truncated"] = true;
    REQUIRE(model_inspection_boundary(report)["status"] == "passed");
}

TEST_CASE("Real mesh inspections retain their existing seven-check semantics", "[RepairEvidence]")
{
    const TriangleMesh healthy(its_make_cube(10., 10., 10.));
    REQUIRE(model_inspection_boundary(inspect_model_mesh(healthy))["status"] == "passed");
    const auto open = model_inspection_boundary(inspect_model_mesh(missing_side()));
    REQUIRE(open["status"] == "defects_detected");
    REQUIRE(open["has_geometry_defects"] == true);
    auto reversed = healthy;
    for (auto& face : reversed.its.indices)
        std::swap(face[0], face[1]);
    // Whole-shell direction is legacy supplemental data, not an eighth defect.
    REQUIRE(model_inspection_boundary(inspect_model_mesh(reversed))["status"] == "passed");
}

TEST_CASE("Repair report completion does not imply full inspection or printing", "[RepairEvidence]")
{
    REQUIRE(model_repair_boundary(ModelRepairReport{})["status"] == "not_run");
    auto report = unresolved_repair();
    auto result = model_repair_boundary(report);
    REQUIRE(result["status"] == "completed");
    REQUIRE(result["repair_status"] == "unresolved");
    REQUIRE(result["execution_completed"] == true);
    REQUIRE(result["engine_mesh_validated"] == false);
    report.committed = true;
    REQUIRE(model_repair_boundary(report)["repair_status"] == "partially_repaired");
    report.unresolved = 0;
    report.success    = true;
    result            = model_repair_boundary(report);
    REQUIRE(result["engine_mesh_validated"] == true);
    REQUIRE(result["full_inspection"] == "not_established_by_repair_report");
    REQUIRE(result["printability"] == "not_assessed");
    report.failed = 1;
    REQUIRE(model_repair_boundary(report)["status"] == "failed");
    report.canceled = true;
    REQUIRE(model_repair_boundary(report)["status"] == "canceled");
}

TEST_CASE("Single-Z region evidence is read-only and is not print approval", "[RepairEvidence]")
{
    const TriangleMesh mesh(its_make_cube(10., 10., 10.));
    const auto         vertices = mesh.its.vertices;
    const auto         indices  = mesh.its.indices;
    const auto         evidence = inspect_model_section_evidence(mesh);
    REQUIRE(evidence["status"] == "completed");
    REQUIRE(evidence["planes"] == 50);
    REQUIRE(evidence["requested_planes"] == 50);
    REQUIRE(evidence["axes"].size() == 1);
    REQUIRE(evidence["axes"][0]["axis"] == "Z");
    REQUIRE(evidence["coverage"] == "reference_z_midplanes");
    REQUIRE(evidence["effective_regions"] == 50);
    REQUIRE(evidence["effective_area_sum_mm2"].get<double>() == Approx(5000.));
    REQUIRE(evidence["all_effective_regions_empty"] == false);
    REQUIRE(evidence["remaining_open_chains"] == 0);
    REQUIRE(evidence["gap_closed_planes"] == 0);
    REQUIRE(evidence["printability"] == "not_assessed");
    REQUIRE(evidence["printer_layer_plan"] == false);
    REQUIRE(mesh.its.vertices == vertices);
    REQUIRE(mesh.its.indices == indices);
}

TEST_CASE("Section evidence distinguishes open chains and heuristic gap closure", "[RepairEvidence]")
{
    const auto open = inspect_model_section_evidence(missing_side());
    REQUIRE(open["status"] == "completed");
    REQUIRE(open["remaining_open_chains"].get<std::size_t>() > 0);
    const auto missing_top = inspect_model_section_evidence(missing_side(2, 10.f));
    REQUIRE(missing_top["status"] == "completed");
    REQUIRE(missing_top["remaining_open_chains"] == 0);
    REQUIRE(missing_top["effective_regions"].get<std::size_t>() > 0);
    const auto gaps = inspect_model_section_evidence(missing_side(0, 0.f, 1.));
    REQUIRE(gaps["status"] == "completed");
    REQUIRE(gaps["gap_closed_planes"].get<std::size_t>() > 0);
}

TEST_CASE("Region construction observes fill-rule cancellation without a second mesh cut", "[RepairEvidence]")
{
    TriangleMesh coincident(its_make_cube(10., 10., 10.));
    coincident.merge(TriangleMesh(its_make_cube(10., 10., 10.)));
    ModelSectionEvidenceOptions options;
    options.slice_zs = std::vector<float>{1.f, 5.f, 9.f};
    auto regular     = inspect_model_section_evidence(coincident, options);
    REQUIRE(regular["status"] == "completed");
    REQUIRE(regular["effective_regions"] == 3);
    options.slicing_params.mode = MeshSlicingParams::SlicingMode::EvenOdd;
    auto even_odd               = inspect_model_section_evidence(coincident, options);
    REQUIRE(even_odd["status"] == "completed");
    REQUIRE(even_odd["all_effective_regions_empty"] == true);
    REQUIRE(even_odd["nondegenerate_closed_loop_planes"] == 3);
    REQUIRE(even_odd["empty_region_planes_with_closed_loops"] == 3);
    REQUIRE(even_odd["parameters"]["mode"] == "even_odd");
    const auto boundary = model_check_boundary(residual_inspection(), model_repair_boundary(unresolved_repair()), even_odd, "",
                                               eligible_reconstruction);
    REQUIRE(boundary["reconstruction"]["assessment"] == "undetermined");
    REQUIRE(boundary["reconstruction"]["printer_layer_plan"] == false);
    REQUIRE(even_odd["parameters_can_remove_regions"] == true);

    TriangleMesh opposed(its_make_cube(10., 10., 10.));
    auto         reverse = its_make_cube(10., 10., 10.);
    for (auto& face : reverse.indices)
        std::swap(face[0], face[1]);
    opposed.merge(TriangleMesh(std::move(reverse)));
    options.slicing_params.mode = MeshSlicingParams::SlicingMode::Regular;
    const auto canceled         = inspect_model_section_evidence(opposed, options);
    REQUIRE(canceled["status"] == "completed");
    REQUIRE(canceled["all_effective_regions_empty"] == true);
    REQUIRE(canceled["nondegenerate_closed_loop_planes"] == 3);
    REQUIRE(model_check_boundary(residual_inspection(), model_repair_boundary(unresolved_repair()), canceled, "",
                                 eligible_reconstruction)["reconstruction"]["assessment"] == "evidence_present");
}

TEST_CASE("Correct cavities and empty gaps between separate parts are not region failures", "[RepairEvidence]")
{
    TriangleMesh cavity(its_make_cube(10., 10., 10.));
    auto         inner = its_make_cube(8., 8., 8.);
    for (auto& vertex : inner.vertices)
        vertex += Vec3f(1.f, 1.f, 1.f);
    for (auto& face : inner.indices)
        std::swap(face[0], face[1]);
    cavity.merge(TriangleMesh(std::move(inner)));
    ModelSectionEvidenceOptions options;
    options.slice_zs  = std::vector<float>{5.f};
    const auto hollow = inspect_model_section_evidence(cavity, options);
    REQUIRE(hollow["status"] == "completed");
    REQUIRE(hollow["effective_regions"] == 1);
    REQUIRE(hollow["effective_area_sum_mm2"].get<double>() == Approx(36.));
    REQUIRE(hollow["all_effective_regions_empty"] == false);

    TriangleMesh parts(its_make_cube(10., 10., 1.));
    auto         upper = its_make_cube(10., 10., 1.);
    for (auto& vertex : upper.vertices)
        vertex.z() += 3.f;
    parts.merge(TriangleMesh(std::move(upper)));
    const auto separated = inspect_model_section_evidence(parts);
    REQUIRE(separated["status"] == "completed");
    REQUIRE(separated["empty_region_planes"].get<std::size_t>() > 0);
    REQUIRE(separated["effective_regions"].get<std::size_t>() > 0);
    REQUIRE(separated["empty_region_planes_with_closed_loops"] == 0);
    REQUIRE(model_check_boundary(residual_inspection(), model_repair_boundary(unresolved_repair()), separated, "",
                                 eligible_reconstruction)["reconstruction"]["assessment"] == "undetermined");
}

TEST_CASE("A missed thin section remains unknown rather than evidence for reconstruction", "[RepairEvidence]")
{
    auto thin = its_make_cube(10., 10., 0.02);
    for (auto& vertex : thin.vertices)
        vertex.z() += 0.19f;
    ModelSectionEvidenceOptions options;
    options.slice_zs    = std::vector<float>{0.1f, 0.3f};
    const auto evidence = inspect_model_section_evidence(TriangleMesh(std::move(thin)), options);
    REQUIRE(evidence["status"] == "completed");
    REQUIRE(evidence["all_effective_regions_empty"] == true);
    REQUIRE(evidence["nondegenerate_closed_loop_planes"] == 0);
    REQUIRE(model_check_boundary(residual_inspection(), model_repair_boundary(unresolved_repair()), evidence, "",
                                 eligible_reconstruction)["reconstruction"]["assessment"] == "undetermined");
}

TEST_CASE("Caller-provided layer plans and slicing parameters are explicit and validated", "[RepairEvidence]")
{
    const TriangleMesh          mesh(its_make_cube(10., 10., 10.));
    ModelSectionEvidenceOptions options;
    options.slice_zs                               = std::vector<float>{10.25f, 12.f, 19.75f};
    options.slicing_params.trafo.translation().z() = 10.;
    options.slicing_params.closing_radius          = 0.01f;
    options.slicing_params.resolution              = 0.0025;
    auto result                                    = inspect_model_section_evidence(mesh, options);
    REQUIRE(result["status"] == "completed");
    REQUIRE(result["coverage"] == "caller_supplied_z_planes");
    REQUIRE(result["printer_layer_plan"] == false);
    REQUIRE(result["effective_regions"] == 3);
    REQUIRE(result["spacing_mm"].is_null());
    REQUIRE(result["parameters"]["closing_radius_mm"].get<double>() == Approx(0.01));
    REQUIRE(result["parameters_can_remove_regions"] == true);
    options.printer_layer_plan = true;
    REQUIRE(inspect_model_section_evidence(mesh, options)["printer_layer_plan"] == true);
    options.slice_zs.reset();
    REQUIRE(inspect_model_section_evidence(mesh, options)["reason"] == "invalid_options");
    options.printer_layer_plan = false;
    for (const std::vector<float> values : {std::vector<float>{}, {1.f, 1.f}, {2.f, 1.f}, {std::numeric_limits<float>::quiet_NaN()}}) {
        options.slice_zs = values;
        REQUIRE(inspect_model_section_evidence(mesh, options)["reason"] == "invalid_options");
    }
    options                               = {};
    options.slicing_params.closing_radius = -1.f;
    REQUIRE(inspect_model_section_evidence(mesh, options)["reason"] == "invalid_options");
    options                                        = {};
    options.slicing_params.trafo.translation().x() = std::numeric_limits<double>::infinity();
    REQUIRE(inspect_model_section_evidence(mesh, options)["reason"] == "invalid_options");
    options.slicing_params.trafo.translation().x() = 1.e9;
    REQUIRE(inspect_model_section_evidence(mesh, options)["reason"] == "coordinate_limit");
    options                           = {};
    options.slicing_params.resolution = 1.e9;
    REQUIRE(inspect_model_section_evidence(mesh, options)["reason"] == "coordinate_limit");
    options                               = {};
    options.slicing_params.closing_radius = 400000.f;
    options.slicing_params.extra_offset   = 300000.f;
    REQUIRE(inspect_model_section_evidence(mesh, options)["reason"] == "coordinate_limit");
    for (double scale : {0., -1.}) {
        options                                     = {};
        options.slicing_params.trafo.linear()(0, 0) = scale;
        REQUIRE(inspect_model_section_evidence(mesh, options)["reason"] == "unsupported_transform");
    }
    options                                     = {};
    options.slicing_params.trafo.matrix()(3, 2) = 0.01;
    REQUIRE(inspect_model_section_evidence(mesh, options)["reason"] == "unsupported_transform");
}

TEST_CASE("Section budgets and cancellation cannot become zero-defect reports", "[RepairEvidence]")
{
    const TriangleMesh          mesh(its_make_cube(10., 10., 10.));
    ModelSectionEvidenceOptions options;
    options.max_planes = 1;
    auto result        = inspect_model_section_evidence(mesh, options);
    REQUIRE(result["status"] == "incomplete");
    REQUIRE(result["reason"] == "plane_limit");
    REQUIRE(result["remaining_open_chains"].is_null());
    REQUIRE(result["all_effective_regions_empty"].is_null());
    REQUIRE(result["planes"] == 0);
    options             = {};
    options.is_canceled = [] { return true; };
    result              = inspect_model_section_evidence(mesh, options);
    REQUIRE(result["status"] == "canceled");
    REQUIRE(result["remaining_open_chains"].is_null());
    std::atomic<unsigned> calls{0};
    options.is_canceled = [&] { return ++calls >= 3; };
    result              = inspect_model_section_evidence(mesh, options);
    REQUIRE(result["status"] == "canceled");
    REQUIRE(calls >= 3);
    options            = {};
    options.spacing_mm = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(inspect_model_section_evidence(mesh, options)["reason"] == "invalid_options");
    options            = {};
    options.timeout_ms = 0;
    REQUIRE(inspect_model_section_evidence(mesh, options)["reason"] == "invalid_options");
    options             = {};
    options.is_canceled = []() -> bool { throw std::bad_alloc(); };
    result              = inspect_model_section_evidence(mesh, options);
    REQUIRE(result["status"] == "incomplete");
    REQUIRE(result["reason"] == "memory_allocation_failed");
    options.is_canceled = []() -> bool { throw std::runtime_error("test"); };
    REQUIRE(inspect_model_section_evidence(mesh, options)["status"] == "failed");
}

TEST_CASE("Invalid or oversized section inputs are rejected without mutation", "[RepairEvidence]")
{
    REQUIRE(inspect_model_section_evidence(TriangleMesh{})["reason"] == "invalid_input");
    TriangleMesh mesh(its_make_cube(10., 10., 10.));
    mesh.its.indices[0][0] = -1;
    REQUIRE(inspect_model_section_evidence(mesh)["reason"] == "invalid_input");
    REQUIRE(mesh.its.indices[0][0] == -1);
    mesh                    = TriangleMesh(its_make_cube(10., 10., 10.));
    mesh.its.vertices[0][0] = std::numeric_limits<float>::infinity();
    REQUIRE(inspect_model_section_evidence(mesh)["reason"] == "invalid_input");
    mesh.its.indices.resize(3000001);
    const auto result = inspect_model_section_evidence(mesh);
    REQUIRE(result["status"] == "incomplete");
    REQUIRE(result["reason"] == "input_limit");
    REQUIRE(result["remaining_open_chains"].is_null());
}

TEST_CASE("Reconstruction evidence is conditional and never an execution decision", "[RepairEvidence]")
{
    const auto repaired = model_repair_boundary(unresolved_repair());
    auto       result   = model_check_boundary(healthy_inspection(), {}, {});
    REQUIRE(result["schema_version"] == 2);
    REQUIRE(result["reconstruction"]["assessment"] == "not_needed");
    result = model_check_boundary(residual_inspection(), repaired, completed_sections());
    REQUIRE(result["reconstruction"]["assessment"] == "undetermined");
    result = model_check_boundary(residual_inspection(), repaired, completed_sections(0, 1));
    REQUIRE(result["reconstruction"]["assessment"] == "undetermined");
    result = model_check_boundary(residual_inspection(), {}, completed_sections(1));
    REQUIRE(result["reconstruction"]["assessment"] == "undetermined");
    result = model_check_boundary(residual_inspection(), repaired, completed_sections(1));
    REQUIRE(result["reconstruction"]["assessment"] == "undetermined");
    result = model_check_boundary(residual_inspection(), repaired, completed_sections(1, 0, true));
    REQUIRE(result["reconstruction"]["assessment"] == "undetermined");
    REQUIRE(result["reconstruction"]["reason"] == "reconstruction_eligibility_not_evaluated");
    result = model_check_boundary(residual_inspection(), repaired, completed_sections(1, 0, true), "", eligible_reconstruction);
    REQUIRE(result["reconstruction"]["assessment"] == "evidence_present");
    REQUIRE(result["reconstruction"]["reason"] == "no_effective_regions");
    REQUIRE(result["reconstruction"]["necessity"] == "not_established");
    REQUIRE(result["reconstruction"]["applicability"] == "eligible");
    REQUIRE(result["reconstruction"]["evidence_scope"] == "reference_z_midplanes");
    REQUIRE(result["reconstruction"]["automatic_adoption"] == false);
    REQUIRE(result["reconstruction"]["printability"] == "not_assessed");
    result = model_check_boundary(residual_inspection(), repaired, completed_sections(1, 0, true), "",
                                  {{"status", "unsupported"}, {"reason", "painted"}});
    REQUIRE(result["reconstruction"]["assessment"] == "blocked");
    REQUIRE(result["reconstruction"]["applicability_reason"] == "painted");
    auto shrinking                             = completed_sections(1, 0, true);
    shrinking["parameters_can_remove_regions"] = true;
    REQUIRE(model_check_boundary(residual_inspection(), repaired, shrinking, "", eligible_reconstruction)["reconstruction"]["assessment"] ==
            "undetermined");
}

TEST_CASE("Confirmed proper crossing recommends a candidate without section work", "[RepairEvidence]")
{
    // Rule-unit fixture: the exact geometric classifier is tested separately.
    // This is not a claim that advanced repair failed on a synthetic mesh.
    auto inspected                                    = healthy_inspection();
    inspected["triangle_count"]                       = 6;
    inspected["mesh_status"]                          = "needs_repair";
    inspected["issues"]["self_intersection"]["count"] = 3;
    inspected["proper_crossings"]                     = {{"status", "completed"},
                                                         {"detected", true},
                                                         {"pairs_checked", 2},
                                                         {"total_pairs", 3},
                                                         {"search_complete", true},
                                                         {"witness", {{"faces", {0, 3}}, {"segment", {{0., 0., 0.}, {1., 0., 0.}}}}}};
    const auto repaired                               = model_repair_boundary(unresolved_repair());
    const json skipped                                = {{"status", "not_run"}, {"reason", "reconstruction_evidence_already_present"}};
    const auto decision                               = [&](const json& input, const json& repair = json()) {
        return model_check_boundary(input, repair.is_null() ? repaired : repair, skipped, "", eligible_reconstruction);
    };
    const auto result = decision(inspected);
    REQUIRE(result["reconstruction"]["assessment"] == "evidence_present");
    REQUIRE(result["reconstruction"]["reason"] == "proper_crossing_after_repair");
    REQUIRE(result["reconstruction"]["evidence_scope"] == "retained_mesh_geometry");
    REQUIRE(result["reconstruction"]["policy"]["allow_boundary_closure"] == false);
    REQUIRE(result["sections"] == skipped);
    REQUIRE(result["proper_crossings"] == inspected["proper_crossings"]);
    REQUIRE(result["reconstruction"]["automatic_adoption"] == false);
    REQUIRE(result["reconstruction"]["necessity"] == "not_established");
    REQUIRE(decision(inspected, json::object())["reconstruction"]["assessment"] == "undetermined");
    REQUIRE(model_check_boundary(inspected, repaired, {}, "", {})["reconstruction"]["assessment"] == "undetermined");
    REQUIRE(model_check_boundary(inspected, repaired, {}, "resource_limit", eligible_reconstruction)["reconstruction"]["assessment"] ==
            "blocked");
    REQUIRE(model_check_boundary(inspected, repaired, {}, "", {{"status", "unsupported"}})["reconstruction"]["assessment"] == "blocked");
    for (const auto& patch : {json{{"status", "incomplete"}}, json{{"search_complete", false}}, json{{"detected", 1}},
                              json{{"pairs_checked", 0}}, json{{"pairs_checked", 4}}, json{{"total_pairs", 2}}, json{{"witness", nullptr}},
                              json{{"witness", {{"faces", {0, 6}}, {"segment", {{0., 0., 0.}, {1., 0., 0.}}}}}},
                              json{{"witness", {{"faces", {0, 0}}, {"segment", {{0., 0., 0.}, {1., 0., 0.}}}}}},
                              json{{"witness", {{"faces", {0, 3}}, {"segment", {{0., 0., 0.}, {0., 0., 0.}}}}}}}) {
        auto invalid = inspected;
        invalid["proper_crossings"].update(patch);
        REQUIRE(decision(invalid)["reconstruction"]["assessment"] == "blocked");
    }
    auto touching = inspected;
    touching["proper_crossings"].update({{"detected", false}, {"pairs_checked", 3}, {"witness", nullptr}});
    const auto inconclusive = model_check_boundary(touching, repaired, completed_sections(), "", eligible_reconstruction);
    REQUIRE(inconclusive["reconstruction"]["assessment"] == "undetermined");
    REQUIRE(inconclusive["reconstruction"]["reason"] == "no_conclusive_region_failure");
    touching["proper_crossings"]["pairs_checked"] = 2;
    REQUIRE(decision(touching)["reconstruction"]["assessment"] == "blocked");
}

TEST_CASE("Bounded coplanar patch evidence preserves completion and witness boundaries", "[RepairEvidence][ProperCrossing]")
{
    indexed_triangle_set mesh;
    mesh.vertices = {{-2.f, -2.f, 0.f}, {2.f, -2.f, 0.f}, {0.f, 2.f, 0.f}, {0.f, -1.f, -1.f},
                     {0.f, -1.f, 1.f},  {0.f, 1.f, 0.f},  {0.f, -1.f, 0.f}};
    mesh.indices  = {{0, 1, 2}, {3, 6, 5}, {6, 4, 5}};
    ModelInspectionOptions options;
    options.classify_proper_crossings = true;
    const auto inspected              = inspect_model_mesh(TriangleMesh(mesh), options);
    REQUIRE(inspected["proper_crossings"]["schema_version"] == 2);
    REQUIRE(inspected["proper_crossings"]["witness"]["kind"] == "coplanar_edge_patches");
    // Only the retained-mesh classifier and decision projection are exercised;
    // this does not claim advanced repair actually failed on this fixture.
    const auto repaired = model_repair_boundary(unresolved_repair());
    const auto decision = [&](const json& input) {
        return model_check_boundary(input, repaired, {}, "", eligible_reconstruction)["reconstruction"];
    };
    const auto result = decision(inspected);
    REQUIRE(result["assessment"] == "evidence_present");
    REQUIRE(result["reason"] == "proper_crossing_after_repair");
    REQUIRE(result["automatic_adoption"] == false);
    REQUIRE(result["necessity"] == "not_established");

    auto unsplit      = mesh;
    unsplit.indices   = {{0, 1, 2}, {3, 4, 5}};
    const auto strict = inspect_model_mesh(TriangleMesh(unsplit), options);
    REQUIRE(strict["proper_crossings"]["witness"]["kind"] == "triangle_interiors");
    REQUIRE(strict["proper_crossings"]["neighborhood"]["reason"] == "not_required");
    REQUIRE(decision(strict)["assessment"] == "evidence_present");

    for (const auto& patch : {json{{"classification_complete", false}}, json{{"budget_exhausted", true}}, json{{"schema_version", 3}},
                              json{{"schema_version", "2"}}, json{{"classification_scope", "all_mesh_crossings"}},
                              json{{"pairs_checked", 0}}, json{{"neighborhood", {{"reason", "face_limit"}}}}}) {
        auto invalid = inspected;
        invalid["proper_crossings"].update(patch);
        REQUIRE(decision(invalid)["assessment"] == "blocked");
    }
    for (const auto& patch :
         {json{{"support_faces", {{0}, {1, 1}}}}, json{{"support_faces", {{0, 2}, {1, 2}}}}, json{{"support_faces", {{0}, {1, 3}}}},
          json{{"support_faces", {{0}, {2}}}}, json{{"support_faces", {{0}, {1}}}}, json{{"support_faces", {{0, 1, 2}, {1}}}},
          json{{"source", "shared_nonmanifold_edge"}}, json{{"kind", "triangle_interiors"}}}) {
        auto invalid = inspected;
        invalid["proper_crossings"]["witness"].update(patch);
        REQUIRE(decision(invalid)["assessment"] == "blocked");
    }
    auto missing = inspected;
    missing["proper_crossings"].erase("classification_complete");
    REQUIRE(decision(missing)["assessment"] == "blocked");
    missing = inspected;
    missing["proper_crossings"].erase("schema_version");
    REQUIRE(decision(missing)["assessment"] == "blocked");
    for (const auto* key : {"classification_scope", "classification_complete", "budget_exhausted", "neighborhood"})
        missing["proper_crossings"].erase(key);
    REQUIRE(decision(missing)["assessment"] == "blocked");
    for (const auto& patch : {json{{"reason", "not_required"}}, json{{"faces_scanned", 0}}, json{{"candidates", 0}}}) {
        auto invalid = inspected;
        invalid["proper_crossings"]["neighborhood"].update(patch);
        REQUIRE(decision(invalid)["assessment"] == "blocked");
    }
    auto double_patch                                                 = inspected;
    double_patch["triangle_count"]                                    = 4;
    double_patch["proper_crossings"]["neighborhood"]["faces_scanned"] = 4;
    double_patch["proper_crossings"]["witness"]["support_faces"]      = {{0, 3}, {1, 2}};
    REQUIRE(decision(double_patch)["assessment"] == "blocked");

    auto negative = inspected;
    negative["proper_crossings"].update(
        {{"detected", false}, {"pairs_checked", inspected["proper_crossings"]["total_pairs"]}, {"witness", nullptr}});
    REQUIRE(model_check_boundary(negative, repaired, completed_sections(), "", eligible_reconstruction)["reconstruction"]["assessment"] ==
            "undetermined");
    negative["proper_crossings"]["classification_complete"] = false;
    REQUIRE(model_check_boundary(negative, repaired, completed_sections(), "", eligible_reconstruction)["reconstruction"]["assessment"] ==
            "blocked");
}

TEST_CASE("Open mesh boundaries only recommend reconstruction under explicit closed-solid policy", "[RepairEvidence]")
{
    const auto                inspected = residual_inspection();
    const auto                repaired  = model_repair_boundary(unresolved_repair());
    ModelRepairEvidencePolicy policy;
    REQUIRE(model_check_boundary(inspected, repaired, {}, "", eligible_reconstruction, policy)["reconstruction"]["assessment"] ==
            "undetermined");
    policy.allow_boundary_closure = true;
    const auto result             = model_check_boundary(inspected, repaired, {}, "", eligible_reconstruction, policy);
    REQUIRE(result["reconstruction"]["assessment"] == "evidence_present");
    REQUIRE(result["reconstruction"]["reason"] == "open_boundaries_under_closed_solid_policy");
    REQUIRE(result["reconstruction"]["policy"]["allow_boundary_closure"] == true);
    REQUIRE(result["sections"]["status"] == "not_run");
    REQUIRE(result["reconstruction"]["automatic_adoption"] == false);
    auto incomplete                              = inspected;
    incomplete["issues"]["boundary"]["complete"] = false;
    REQUIRE(model_check_boundary(incomplete, repaired, {}, "", eligible_reconstruction, policy)["reconstruction"]["assessment"] ==
            "blocked");
    REQUIRE(model_check_boundary(inspected, {}, {}, "", eligible_reconstruction, policy)["reconstruction"]["assessment"] == "undetermined");
    REQUIRE(model_check_boundary(healthy_inspection(), repaired, {}, "", eligible_reconstruction, policy)["reconstruction"]["assessment"] ==
            "not_needed");
    for (const auto* key : {"nonmanifold_edge", "nonmanifold_vertex", "duplicate", "degenerate", "orientation"}) {
        auto residual                    = healthy_inspection();
        residual["issues"][key]["count"] = 200;
        residual["mesh_status"]          = "needs_repair";
        REQUIRE(model_check_boundary(residual, repaired, completed_sections(), "", eligible_reconstruction,
                                     policy)["reconstruction"]["assessment"] == "undetermined");
    }
}

TEST_CASE("Failure unknown and incomplete observations never become reconstruction evidence", "[RepairEvidence]")
{
    const auto repair = model_repair_boundary(unresolved_repair());
    for (const std::string failure : {"invalid_arguments", "input_failed", "io_failed", "inspection_failed", "repair_failed",
                                      "export_failed", "execution_failed", "memory_allocation_failed", "resource_limit", "canceled"}) {
        const auto result = model_check_boundary(residual_inspection(), repair, completed_sections(1), failure);
        REQUIRE(result["reconstruction"]["assessment"] == "blocked");
        REQUIRE(result["reconstruction"]["failure_kind"] == failure);
    }
    for (const std::string status : {"failed", "canceled", "incomplete"}) {
        auto bad_repair      = repair;
        bad_repair["status"] = status;
        REQUIRE(model_check_boundary(residual_inspection(), bad_repair, completed_sections(1))["reconstruction"]["assessment"] ==
                "blocked");
        auto bad_sections      = completed_sections(1);
        bad_sections["status"] = status;
        REQUIRE(model_check_boundary(residual_inspection(), repair, bad_sections)["reconstruction"]["assessment"] == "blocked");
    }
    auto unknown                                       = residual_inspection();
    unknown["issues"]["self_intersection"]["complete"] = false;
    REQUIRE(model_check_boundary(unknown, repair, completed_sections(1))["reconstruction"]["assessment"] == "blocked");
    auto malformed                 = completed_sections(1);
    malformed["effective_regions"] = nullptr;
    const auto invalid             = model_check_boundary(residual_inspection(), repair, malformed)["reconstruction"];
    REQUIRE(invalid["assessment"] == "blocked");
    REQUIRE(invalid["reason"] == "invalid_section_evidence");
    REQUIRE(model_check_boundary(residual_inspection(), json(1), json("completed"))["reconstruction"]["assessment"] == "undetermined");
}

TEST_CASE("Evidence workflow failures retain their substage cause without changing the wrapper", "[RepairEvidence]")
{
    const auto repair          = model_repair_boundary(unresolved_repair());
    const auto failed_sections = [&](const json& sections, const std::string& failure = "repair_evidence_incomplete") {
        return model_check_boundary(residual_inspection(), repair, sections, failure, eligible_reconstruction)["reconstruction"];
    };
    for (const auto* cause :
         {"plane_limit", "time_limit", "input_limit", "intersection_limit", "coordinate_limit", "memory_allocation_failed"}) {
        const auto result = failed_sections({{"status", "incomplete"}, {"reason", cause}});
        REQUIRE(result["assessment"] == "blocked");
        REQUIRE(result["reason"] == "workflow_failure");
        REQUIRE(result["failure_kind"] == "repair_evidence_incomplete");
        REQUIRE(result["failure_stage"] == "sections");
        REQUIRE(result["failure_cause"] == cause);
        REQUIRE(result["failure_category"] == "resource");
    }
    const auto canceled = failed_sections({{"status", "canceled"}, {"reason", "canceled"}});
    REQUIRE(canceled["failure_category"] == "canceled");
    REQUIRE(canceled["failure_cause"] == "canceled");
    for (const auto* cause : {"execution_failed", "future_unknown_reason", "unsupported_transform", "degenerate_extent",
                              "coordinate_precision", "candidate_limit", "face_limit"}) {
        const auto result = failed_sections({{"status", "incomplete"}, {"reason", cause}});
        REQUIRE(result["failure_category"] == "execution");
        REQUIRE(result["failure_cause"] == cause);
    }
    const auto invalid_input = failed_sections({{"status", "failed"}, {"reason", "invalid_input"}});
    REQUIRE(invalid_input["failure_category"] == "input");
    auto completed      = completed_sections();
    completed["reason"] = "plane_limit";
    const auto stale    = failed_sections(completed);
    REQUIRE(stale["failure_category"] == "execution");
    REQUIRE_FALSE(stale.contains("failure_cause"));
    REQUIRE_FALSE(stale.contains("failure_stage"));
    const auto no_reason = failed_sections({{"status", "incomplete"}});
    REQUIRE(no_reason["failure_category"] == "execution");
    REQUIRE(no_reason["failure_stage"] == "sections");
    REQUIRE_FALSE(no_reason.contains("failure_cause"));
    // An earlier IO/repair failure must not be reclassified by later/stale
    // evidence, even if that evidence contains a recognized resource reason.
    const auto io = failed_sections({{"status", "incomplete"}, {"reason", "plane_limit"}}, "io_failed");
    REQUIRE(io["failure_kind"] == "io_failed");
    REQUIRE(io["failure_category"] == "io");
    REQUIRE_FALSE(io.contains("failure_cause"));
    const auto direct = failed_sections({{"status", "incomplete"}, {"reason", "plane_limit"}}, "");
    REQUIRE(direct["reason"] == "section_evidence_not_completed");
    REQUIRE(direct["failure_category"] == "resource");
    REQUIRE(direct["failure_cause"] == "plane_limit");
}

TEST_CASE("Only established inspection or crossing budgets refine evidence failure classification", "[RepairEvidence]")
{
    const auto repair            = model_repair_boundary(unresolved_repair());
    const auto failed_inspection = [&](const json& inspection) {
        return model_check_boundary(inspection, repair, {}, "repair_evidence_incomplete", eligible_reconstruction)["reconstruction"];
    };
    auto limited                                   = residual_inspection();
    limited["component_analysis"]["limit_reached"] = true;
    auto result                                    = failed_inspection(limited);
    REQUIRE(result["failure_stage"] == "inspection");
    REQUIRE(result["failure_cause"] == "component_limit");
    REQUIRE(result["failure_category"] == "resource");
    limited                                                 = residual_inspection();
    limited["issues"]["self_intersection"]["complete"]      = false;
    limited["component_analysis"]["intersections_complete"] = false;
    result                                                  = failed_inspection(limited);
    REQUIRE(result["failure_cause"] == "self_intersection_incomplete");
    REQUIRE(result["failure_category"] == "resource");
    limited["component_analysis"]["intersections_complete"] = true;
    REQUIRE(failed_inspection(limited)["failure_category"] == "execution");
    for (const auto* cause : {"candidate_limit", "face_limit"}) {
        auto crossing                = residual_inspection();
        crossing["proper_crossings"] = {{"schema_version", 2},
                                        {"status", "incomplete"},
                                        {"classification_complete", false},
                                        {"budget_exhausted", true},
                                        {"neighborhood", {{"reason", cause}}}};
        result                       = failed_inspection(crossing);
        REQUIRE(result["assessment"] == "blocked");
        REQUIRE(result["failure_kind"] == "repair_evidence_incomplete");
        REQUIRE(result["failure_stage"] == "proper_crossings");
        REQUIRE(result["failure_cause"] == cause);
        REQUIRE(result["failure_category"] == "resource");
        for (const auto& patch : {json{{"schema_version", 1}}, json{{"status", "completed"}}, json{{"classification_complete", true}},
                                  json{{"budget_exhausted", false}}, json{{"neighborhood", {{"reason", "future_unknown_reason"}}}}}) {
            auto invalid = crossing;
            invalid["proper_crossings"].update(patch);
            const auto unclassified = failed_inspection(invalid);
            REQUIRE(unclassified["failure_category"] == "execution");
            REQUIRE_FALSE(unclassified.contains("failure_cause"));
        }
    }
}

TEST_CASE("Contradictory completed-region evidence cannot recommend reconstruction", "[RepairEvidence]")
{
    const auto repair = model_repair_boundary(unresolved_repair());
    const auto base   = completed_sections(0, 0, true);
    for (const auto& patch :
         {json{{"empty_region_planes", 2}}, json{{"effective_regions", 1}}, json{{"effective_area_sum_mm2", 1.}},
          json{{"effective_area_sum_mm2", -1.}}, json{{"nondegenerate_closed_loop_planes", 4}},
          json{{"empty_region_planes_with_closed_loops", 4}}, json{{"all_effective_regions_empty", false}}, json{{"requested_planes", 2}},
          json{{"axes", {{{"axis", "Z"}, {"completed_planes", 2}, {"requested_planes", 3}}}}}}) {
        auto observed = base;
        observed.update(patch);
        REQUIRE(model_check_boundary(residual_inspection(), repair, observed, "",
                                     eligible_reconstruction)["reconstruction"]["assessment"] != "evidence_present");
    }
}

TEST_CASE("A successful reconstruction candidate remains unconfirmed and not print-certified", "[RepairEvidence]")
{
    const json input  = {{"status", "candidate_ready"},
                         {"mesh_validated", true},
                         {"print_ready", true},
                         {"applied", true},
                         {"metrics", {{"sampled_source_to_candidate_mm", 0.8}, {"deviation_warning", true}}},
                         {"engine", {{"boundary_closure", {{"applied", true}}}}}};
    const auto copy   = input;
    const auto result = model_reconstruction_boundary(input);
    REQUIRE(input == copy);
    REQUIRE(result["status"] == "candidate_ready");
    REQUIRE(result["mesh_validated"] == true);
    REQUIRE(result["metrics"] == input["metrics"]);
    REQUIRE(result["boundary_closure"] == input["engine"]["boundary_closure"]);
    REQUIRE(result["automatic_adoption"] == false);
    REQUIRE(result["shape_structure_guaranteed"] == false);
    REQUIRE(result["printability"] == "not_assessed");
    REQUIRE(model_reconstruction_boundary({})["status"] == "not_run");
    REQUIRE(model_reconstruction_boundary(json(1))["status"] == "unknown");
}

TEST_CASE("Manual section evidence benchmark reports every bounded observation", "[.RepairEvidenceBenchmark]")
{
    const char* path = std::getenv("ORCA_SECTION_BENCHMARK_MODEL");
    REQUIRE(path != nullptr);
    TriangleMesh mesh;
    REQUIRE(mesh.ReadSTLFile(path, false));
    for (int run = 0; run < 5; ++run) {
        const auto result = inspect_model_section_evidence(mesh);
        std::cout << "section_evidence_benchmark "
                  << json{{"run", run + 1},
                          {"faces", mesh.its.indices.size()},
                          {"elapsed_ms", result["elapsed_ms"]},
                          {"region_construction_ms", result.value("region_construction_ms", json(nullptr))},
                          {"planes", result["planes"]},
                          {"status", result["status"]},
                          {"reason", result["reason"]}}
                         .dump()
                  << '\n';
        REQUIRE(result["status"] != "failed");
    }
}
