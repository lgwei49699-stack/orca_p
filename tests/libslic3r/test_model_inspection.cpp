#include <catch2/catch.hpp>
#include <test_utils.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/ModelInspection.hpp>
#include <libslic3r/ModelCheckWorkflow.hpp>
#include <libslic3r/ModelRepairEvidence.hpp>
#include <libslic3r/Format/STL.hpp>
#include <libslic3r/Format/OBJ.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <boost/filesystem.hpp>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>

using namespace Slic3r;

namespace {
struct InspectionTempDirectory
{
    boost::filesystem::path path = boost::filesystem::temp_directory_path() /
                                   boost::filesystem::unique_path("orca-inspection-%%%%-%%%%-%%%%");
    InspectionTempDirectory() { boost::filesystem::create_directory(path); }
    ~InspectionTempDirectory()
    {
        boost::system::error_code ignored;
        boost::filesystem::remove_all(path, ignored);
    }
};

std::string inspection_file_bytes(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

TriangleMesh inspection_triangle_pair(const std::array<Vec3f, 3>& second, float x_offset = 0.f)
{
    indexed_triangle_set its;
    its.vertices = {{-2.f, -2.f, 0.f}, {2.f, -2.f, 0.f}, {0.f, 2.f, 0.f}};
    its.vertices.insert(its.vertices.end(), second.begin(), second.end());
    for (auto& vertex : its.vertices)
        vertex.x() += x_offset;
    its.indices = {{0, 1, 2}, {3, 4, 5}};
    return TriangleMesh(std::move(its));
}

TriangleMesh inspection_crossing_pair(float x_offset = 0.f)
{
    return inspection_triangle_pair({Vec3f(0.f, -1.f, -1.f), Vec3f(0.f, -1.f, 1.f), Vec3f(0.f, 1.f, 0.f)}, x_offset);
}

TriangleMesh inspection_coplanar_pair(float x_offset = 0.f)
{
    return inspection_triangle_pair({Vec3f(-1.f, -1.f, 0.f), Vec3f(1.f, -1.f, 0.f), Vec3f(0.f, 1.f, 0.f)}, x_offset);
}

void check_inspection_with_crossings(nlohmann::json actual, const TriangleMesh& retained)
{
    REQUIRE(actual.contains("proper_crossings"));
    auto crossing_evidence = actual.at("proper_crossings");
    actual.erase("proper_crossings");
    CHECK(actual == inspect_model_mesh(retained));
    ModelInspectionOptions options;
    options.classify_proper_crossings = true;
    auto expected_crossings           = inspect_model_mesh(retained, options).at("proper_crossings");
    CHECK(crossing_evidence.at("elapsed_ms").get<double>() >= 0.0);
    // Only elapsed time is nondeterministic; every classification field must
    // match inspection of the retained serialized artifact.
    crossing_evidence.erase("elapsed_ms");
    expected_crossings.erase("elapsed_ms");
    CHECK(crossing_evidence == expected_crossings);
}
} // namespace

TEST_CASE("Every workflow task uses native STL import even for an existing working model", "[ModelRepair][Inspection][stl]")
{
    InspectionTempDirectory temp;
    // A name is not proof that an input has already been prepared.
    const auto input = (temp.path / "prepared.stl").string();
    auto       mesh  = make_cube(10., 10., 10.);
    std::swap(mesh.its.indices.front()[1], mesh.its.indices.front()[2]);
    mesh.its.indices.emplace_back(0, 0, 1);
    REQUIRE(store_stl(input.c_str(), &mesh, true));
    const auto       original_bytes = inspection_file_bytes(input);
    Model            native;
    MeshRepairReport import_report;
    REQUIRE(load_stl(input.c_str(), &native, nullptr, nullptr, 80, true, &import_report));
    REQUIRE(import_report.changed());
    native.add_default_instances(); // Match Model::read_from_file's workflow strategy.
    const auto expected = inspect_model_mesh(native.mesh());

    ModelCheckWorkflowOptions options;
    options.input_file       = input;
    options.output_directory = temp.path.string();
    ModelCheckWorkflowReport report;
    run_model_check_workflow(options, report);
    CHECK(report.check["schema_version"] == 2);
    CHECK(report.check["basic_repair"]["policy"] == "orca_stl_import_v1");
    CHECK(report.check["basic_repair"]["status"] == "completed");
    CHECK(report.check["basic_repair"]["changed"] == import_report.changed());
    CHECK(report.check["basic_repair"]["facets_before"] == 13);
    CHECK(report.check["inspection"]["triangle_count"] == expected["triangle_count"]);
    for (auto it = expected["issues"].begin(); it != expected["issues"].end(); ++it) {
        CHECK(report.check["inspection"]["issues"][it.key()]["count"] == it.value()["count"]);
        CHECK(report.check["inspection"]["issues"][it.key()]["complete"] == it.value()["complete"]);
    }
    options.input_file             = report.check["working_model"].get<std::string>();
    const auto       working_bytes = inspection_file_bytes(options.input_file);
    Model            repeated_native;
    MeshRepairReport repeated_import;
    REQUIRE(load_stl(options.input_file.c_str(), &repeated_native, nullptr, nullptr, 80, true, &repeated_import));
    repeated_native.add_default_instances();
    const auto repeated_expected = inspect_model_mesh(repeated_native.mesh());
    run_model_check_workflow(options, report);
    CHECK(report.check["basic_repair"]["policy"] == "orca_stl_import_v1");
    CHECK(report.check["basic_repair"]["status"] == "completed");
    CHECK(report.check["basic_repair"]["changed"] == repeated_import.changed());
    CHECK(report.check["inspection"] == repeated_expected);
    CHECK(inspection_file_bytes(options.input_file) == working_bytes);
    CHECK(inspection_file_bytes(input) == original_bytes);
}

TEST_CASE("Internal STL export reinspection is read-only but a new task runs native import", "[ModelRepair][Inspection][stl][OBJ]")
{
    InspectionTempDirectory temp;
    const auto              input = (temp.path / "source.obj").string();
    auto                    mesh  = make_cube(10., 10., 10.);
    std::swap(mesh.its.indices.front()[1], mesh.its.indices.front()[2]);
    mesh.its.indices.emplace_back(0, 0, 1);
    REQUIRE(store_obj(input.c_str(), &mesh));
    const auto                original_bytes = inspection_file_bytes(input);
    ModelCheckWorkflowOptions options;
    options.input_file       = input;
    options.output_directory = temp.path.string();
    options.export_format    = "stl";
    ModelCheckWorkflowReport report;
    run_model_check_workflow(options, report);
    CHECK(report.check["basic_repair"]["policy"] == "orca_obj_import_v1");
    CHECK(report.check["inspection"]["issues"]["degenerate"]["count"] == 1);
    CHECK(report.check["inspection"]["issues"]["orientation"]["count"] == 3);
    CHECK(report.check["inspection"]["triangle_count"] == 13);
    const auto exported       = report.check["working_model"].get<std::string>();
    const auto exported_bytes = inspection_file_bytes(exported);
    Model      raw_export;
    REQUIRE(load_stl(exported.c_str(), &raw_export, nullptr, nullptr, 80, false));
    raw_export.add_default_instances();
    CHECK(report.check["inspection"] == inspect_model_mesh(raw_export.mesh()));

    Model            native;
    MeshRepairReport import_report;
    REQUIRE(load_stl(exported.c_str(), &native, nullptr, nullptr, 80, true, &import_report));
    REQUIRE(import_report.changed());
    native.add_default_instances();
    options.input_file = exported;
    options.export_format.clear();
    run_model_check_workflow(options, report);
    CHECK(report.check["basic_repair"]["policy"] == "orca_stl_import_v1");
    CHECK(report.check["basic_repair"]["status"] == "completed");
    CHECK(report.check["basic_repair"]["changed"] == import_report.changed());
    CHECK(report.check["inspection"] == inspect_model_mesh(native.mesh()));
    CHECK(inspection_file_bytes(exported) == exported_bytes);
    CHECK(inspection_file_bytes(input) == original_bytes);
}

TEST_CASE("Workflow keeps native OBJ geometry instead of applying standalone face cleanup", "[ModelRepair][Inspection][OBJ]")
{
    InspectionTempDirectory temp;
    const auto              input = (temp.path / "source.obj").string();
    auto                    mesh  = make_cube(10., 10., 10.);
    mesh.its.indices.push_back(mesh.its.indices.front());
    mesh.its.indices.emplace_back(0, 0, 1);
    REQUIRE(store_obj(input.c_str(), &mesh));
    const auto                original_bytes = inspection_file_bytes(input);
    ModelCheckWorkflowOptions options;
    options.input_file       = input;
    options.output_directory = temp.path.string();
    ModelCheckWorkflowReport report;
    run_model_check_workflow(options, report);
    CHECK(report.check["basic_repair"]["policy"] == "orca_obj_import_v1");
    CHECK(report.check["basic_repair"]["changed"] == false);
    CHECK(report.check["inspection"]["triangle_count"] == 14);
    CHECK(report.check["inspection"]["issues"]["degenerate"]["count"] == 1);
    CHECK(report.check["inspection"]["issues"]["duplicate"]["count"] == 1);
    CHECK(inspection_file_bytes(input) == original_bytes);
}

TEST_CASE("Workflow enforces the same geometry budget at admission and serialized inspection", "[ModelRepair][Inspection]")
{
    InspectionTempDirectory temp;
    const auto              input = (temp.path / "bounded.stl").string();
    auto                    mesh  = make_cube(10., 10., 10.);
    REQUIRE(store_stl(input.c_str(), &mesh, true));
    const auto                original = inspection_file_bytes(input);
    ModelCheckWorkflowOptions options;
    options.input_file                       = input;
    options.output_directory                 = temp.path.string();
    options.max_input_bytes                  = original.size();
    options.inspection_options.max_triangles = 12;
    options.inspection_options.max_vertices  = 8;
    ModelCheckWorkflowReport report;
    SECTION("Exact limits accept the original and reimported export")
    {
        options.export_format = "obj";
        run_model_check_workflow(options, report);
        CHECK(report.check["check_complete"] == true);
        CHECK(report.check["mesh_status"] == "passed");
        CHECK(report.check["inspection"]["triangle_count"] == 12);
    }
    SECTION("Oversized source is rejected before importing")
    {
        options.max_input_bytes--;
        REQUIRE_THROWS_WITH(run_model_check_workflow(options, report),
                            "Model check input exceeds the " + std::to_string(options.max_input_bytes) + " byte limit.");
        CHECK(report.check["basic_repair"].is_null());
        CHECK(report.check["check_complete"] == false);
        CHECK(report.failure_kind == "resource_limit");
        CHECK_FALSE(report.check.contains("section_evidence"));
    }
    SECTION("Decoded triangle limit remains enforced")
    {
        options.inspection_options.max_triangles = 11;
        REQUIRE_THROWS_WITH(run_model_check_workflow(options, report), "Model check requires 1 to 11 decoded triangles.");
        CHECK(report.check["check_complete"] == false);
        CHECK(report.failure_kind == "resource_limit");
    }
    SECTION("Serialized inspection uses the workflow vertex budget")
    {
        options.inspection_options.max_vertices = 7;
        REQUIRE_THROWS_WITH(run_model_check_workflow(options, report), "Model inspection vertex limit exceeded.");
        CHECK(report.check["check_complete"] == false);
        CHECK(report.failure_kind == "resource_limit");
        CHECK(report.check["failure_stage"] == "inspection");
    }
    CHECK(inspection_file_bytes(input) == original);
}

TEST_CASE("Inspection geometry budgets have an explicit exception type", "[ModelRepair][Inspection][BoundaryEvidence]")
{
    const auto             mesh = make_cube(10., 10., 10.);
    ModelInspectionOptions options;
    SECTION("Triangle admission limit")
    {
        options.max_triangles = 11;
        REQUIRE_THROWS_AS(inspect_model_mesh(mesh, options), ModelInspectionLimitError);
        REQUIRE_THROWS_WITH(inspect_model_mesh(mesh, options), "Model inspection triangle limit exceeded.");
    }
    SECTION("Canonical vertex admission limit")
    {
        options.max_vertices = 7;
        REQUIRE_THROWS_AS(inspect_model_mesh(mesh, options), ModelInspectionLimitError);
        REQUIRE_THROWS_WITH(inspect_model_mesh(mesh, options), "Model inspection vertex limit exceeded.");
    }
    SECTION("Invalid input is not reclassified as resource exhaustion")
    {
        try {
            inspect_model_mesh(TriangleMesh{});
            FAIL("Empty mesh should not pass inspection");
        } catch (const ModelInspectionLimitError&) {
            FAIL("Invalid input is not a geometry admission limit");
        } catch (const std::runtime_error& error) {
            CHECK(std::string(error.what()) == "Cannot inspect an empty model.");
        }
    }
}

TEST_CASE("Workflow separates opt-in completion from legacy repair outcomes", "[ModelRepair][Inspection][BoundaryEvidence]")
{
    InspectionTempDirectory   temp;
    ModelCheckWorkflowOptions options;
    options.output_directory = temp.path.string();
    options.advanced_repair  = true;
    options.repair_evidence  = true;
    ModelCheckWorkflowReport report;

    SECTION("Healthy requested repair adds no section work")
    {
        options.input_file = std::string(TEST_DATA_DIR) + "/model_repair_acceptance/00-healthy-cube.obj";
        run_model_check_workflow(options, report);
        CHECK(report.check["advanced_repair"]["status"] == "not_needed");
        CHECK_FALSE(report.repair_attempted);
        CHECK_FALSE(report.check.contains("section_evidence"));
        const auto result = model_check_business_result(nlohmann::json::array({report.check}), 0, "");
        CHECK(result["model_checks"][0]["boundary"]["reconstruction"]["assessment"] == "not_needed");
    }
    SECTION("Geometric no-progress completes the opt-in workflow without claiming a repair")
    {
        options.input_file  = std::string(TEST_DATA_DIR) + "/model_repair_acceptance/09-nonmanifold-edge.obj";
        const auto original = inspection_file_bytes(options.input_file);
        run_model_check_workflow(options, report);
        CHECK_FALSE(report.repair_no_progress);
        CHECK_FALSE(report.repair.committed);
        CHECK(report.failure_kind.empty());
        CHECK(report.check["operation_status"] == "completed");
        CHECK(report.check["advanced_repair"]["status"] == "unresolved");
        CHECK(report.check["repair_evidence"]["execution_completed"] == true);
        CHECK(report.check["section_evidence"]["status"] == "completed");
        CHECK(report.check["checked_stage"] == "serialized_prepared");
        const auto  result   = model_check_business_result(nlohmann::json::array({report.check}), 0, "");
        const auto& evidence = result["model_checks"][0]["boundary"];
        CHECK(result["return_code"] == 0);
        CHECK(result["error_string"] == "");
        CHECK(result["model_checks"][0]["repair_status"] == "unresolved");
        CHECK(result["model_checks"][0]["mesh_status"] == "needs_repair");
        CHECK(evidence["repair"]["status"] == "completed");
        CHECK(evidence["reconstruction"]["reason"] == "no_conclusive_region_failure");
        CHECK_FALSE(evidence["reconstruction"].contains("failure_kind"));
        CHECK(evidence["reconstruction"]["automatic_adoption"] == false);
        CHECK(inspection_file_bytes(options.input_file) == original);
    }
    SECTION("No-progress still honors explicit conversion and observes the final artifact")
    {
        options.input_file    = std::string(TEST_DATA_DIR) + "/model_repair_acceptance/09-nonmanifold-edge.obj";
        options.export_format = "stl";
        run_model_check_workflow(options, report);
        REQUIRE(report.check["advanced_repair"]["status"] == "unresolved");
        CHECK(report.check["operation_status"] == "completed");
        const auto output = report.check["working_model"].get<std::string>();
        CHECK(boost::filesystem::path(output).filename() == "result.stl");
        Model retained;
        REQUIRE(load_stl(output.c_str(), &retained, nullptr, nullptr, 80, false));
        retained.add_default_instances(); // Match the workflow loader before Model::mesh().
        check_inspection_with_crossings(report.check["inspection"], retained.mesh());
        const auto observed = inspect_model_section_evidence(retained.mesh());
        for (const char* key : {"requested_planes", "planes", "effective_regions", "axes"})
            CHECK(report.check["section_evidence"][key] == observed[key]);
    }
    SECTION("Fatal advanced repair is blocked without section work")
    {
        options.input_file  = std::string(TEST_DATA_DIR) + "/model_repair_acceptance/10-only-triangle.obj";
        const auto original = inspection_file_bytes(options.input_file);
        REQUIRE_THROWS_WITH(run_model_check_workflow(options, report), "Advanced repair failed; the prepared working model was retained.");
        CHECK_FALSE(report.repair_no_progress);
        CHECK(report.failure_kind == "repair_failed");
        CHECK_FALSE(report.check.contains("section_evidence"));
        const auto result = model_check_business_result(nlohmann::json::array({report.check}), -25, "Advanced repair failed");
        CHECK(result["model_checks"][0]["boundary"]["reconstruction"]["assessment"] == "blocked");
        CHECK(result["model_checks"][0]["boundary"]["reconstruction"]["failure_kind"] == "repair_failed");
        CHECK(inspection_file_bytes(options.input_file) == original);
    }
    SECTION("Legacy unresolved workflow does not run the new preflight")
    {
        options.repair_evidence = false;
        options.input_file      = std::string(TEST_DATA_DIR) + "/model_repair_acceptance/09-nonmanifold-edge.obj";
        REQUIRE_THROWS_WITH(run_model_check_workflow(options, report),
                            "Advanced repair produced no retainable change; see the remaining inspection findings.");
        CHECK(report.repair_no_progress);
        CHECK_FALSE(report.check.contains("section_evidence"));
        CHECK_FALSE(report.check.contains("repair_evidence"));
        const auto result = model_check_business_result(nlohmann::json::array({report.check}), -25, "No retainable change");
        CHECK(result["model_checks"][0].size() == 6);
        CHECK_FALSE(result["model_checks"][0].contains("boundary"));
    }
}

TEST_CASE("Unfinished preflight cannot complete the opt-in workflow", "[ModelRepair][Inspection][BoundaryEvidence]")
{
    InspectionTempDirectory temp;
    auto                    mesh = load_model("model_repair_acceptance/09-nonmanifold-edge.obj");
    // 500 mm requires 2500 reference planes, beyond the unchanged 2000 cap.
    for (auto& vertex : mesh.its.vertices)
        vertex.z() *= 25.f;
    bool partial = false;
    SECTION("Unchanged unresolved model") {}
    SECTION("Retained partial repair")
    {
        mesh.its.indices.push_back(mesh.its.indices.front());
        partial = true;
    }
    const auto input = (temp.path / "tall-nonmanifold.obj").string();
    REQUIRE(store_obj(input.c_str(), &mesh));
    const auto                original = inspection_file_bytes(input);
    ModelCheckWorkflowOptions options;
    options.input_file       = input;
    options.output_directory = temp.path.string();
    options.advanced_repair  = true;
    options.repair_evidence  = true;
    ModelCheckWorkflowReport report;
    REQUIRE_THROWS_WITH(run_model_check_workflow(options, report), "Repair evidence did not complete; see the boundary details.");
    CHECK_FALSE(report.repair_no_progress);
    CHECK(report.failure_kind == "repair_evidence_incomplete");
    CHECK(report.check["failure_stage"] == "repair_evidence");
    CHECK(report.check["advanced_repair"]["status"] == (partial ? "partially_repaired" : "unresolved"));
    CHECK(report.check["section_evidence"]["status"] == "incomplete");
    CHECK(report.check["section_evidence"]["reason"] == "plane_limit");
    CHECK(boost::filesystem::exists(report.check["working_model"].get<std::string>()));
    const auto result = model_check_business_result(nlohmann::json::array({report.check}), -25, "Evidence incomplete");
    CHECK(result["model_checks"][0]["boundary"]["reconstruction"]["assessment"] == "blocked");
    CHECK(inspection_file_bytes(input) == original);

    options.repair_evidence = false;
    if (partial) {
        run_model_check_workflow(options, report);
        CHECK(report.check["operation_status"] == "completed");
        CHECK(report.check["advanced_repair"]["status"] == "partially_repaired");
    } else {
        REQUIRE_THROWS_WITH(run_model_check_workflow(options, report),
                            "Advanced repair produced no retainable change; see the remaining inspection findings.");
        CHECK(report.repair_no_progress);
    }
    CHECK_FALSE(report.check.contains("section_evidence"));
}

TEST_CASE("Retained partial result section evidence observes the serialized final model", "[ModelRepair][Inspection][BoundaryEvidence]")
{
    InspectionTempDirectory temp;
    auto                    mesh = load_model("model_repair_acceptance/09-nonmanifold-edge.obj");
    mesh.its.indices.push_back(mesh.its.indices.front());
    const auto input = (temp.path / "duplicate-nonmanifold.obj").string();
    REQUIRE(store_obj(input.c_str(), &mesh));
    const auto                original = inspection_file_bytes(input);
    ModelCheckWorkflowOptions options;
    options.input_file       = input;
    options.output_directory = temp.path.string();
    options.advanced_repair  = true;
    options.repair_evidence  = true;
    ModelCheckWorkflowReport report;
    run_model_check_workflow(options, report);
    REQUIRE(report.repair.committed);
    CHECK(report.check["advanced_repair"]["status"] == "partially_repaired");
    CHECK(report.check["checked_stage"] == "serialized_advanced");
    const auto output   = report.check["working_model"].get<std::string>();
    auto       reloaded = Model::read_from_file(output);
    const auto observed = inspect_model_section_evidence(reloaded.mesh());
    CHECK(report.check["section_evidence"]["status"] == "completed");
    for (const char* key : {"requested_planes", "planes", "remaining_open_chains", "gap_closed_planes", "axes"})
        CHECK(report.check["section_evidence"][key] == observed[key]);
    check_inspection_with_crossings(report.check["inspection"], reloaded.mesh());
    CHECK(inspection_file_bytes(input) == original);
}

TEST_CASE("Inspection reports seven checks without editing a healthy mesh", "[ModelRepair][Inspection]")
{
    auto       mesh     = make_cube(10., 10., 10.);
    const auto vertices = mesh.its.vertices;
    const auto faces    = mesh.its.indices;
    const auto result   = inspect_model_mesh(mesh);
    REQUIRE(result["issues"].size() == 7);
    for (const auto& issue : result["issues"])
        REQUIRE(issue["count"] == 0);
    REQUIRE(result["check_complete"] == true);
    REQUIRE(result["mesh_status"] == "passed");
    REQUIRE(result["component_analysis"]["component_count"] == 1);
    REQUIRE(result["printability_verified"] == false);
    REQUIRE(mesh.its.vertices == vertices);
    REQUIRE(mesh.its.indices == faces);
}

TEST_CASE("Open edges are counted on nondegenerate triangles, not as holes", "[ModelRepair][Inspection]")
{
    auto mesh = make_cube(10., 10., 10.);
    mesh.its.indices.erase(mesh.its.indices.begin(), mesh.its.indices.begin() + 2);
    mesh              = TriangleMesh(std::move(mesh.its));
    const auto result = inspect_model_mesh(mesh);
    REQUIRE(result["issues"]["boundary"]["count"] == 4);
    REQUIRE(result["basic_checks_complete"] == true);
    REQUIRE(result["mesh_status"] == "needs_repair");
    REQUIRE(result["component_analysis"]["expected_orientation_unknown"] == 1);
    REQUIRE(result["component_analysis"]["containment_complete"] == true);
    REQUIRE(result["check_complete"] == true); // Unknown shell direction does not make the remaining checks incomplete.
}

TEST_CASE("Local winding conflict is distinct from whole-shell orientation", "[ModelRepair][Inspection]")
{
    auto mesh = make_cube(10., 10., 10.);
    std::swap(mesh.its.indices.front()[1], mesh.its.indices.front()[2]);
    mesh              = TriangleMesh(std::move(mesh.its));
    const auto result = inspect_model_mesh(mesh);
    REQUIRE(result["issues"]["orientation"]["count"] == 3);
    REQUIRE(result["issues"]["boundary"]["count"] == 0);
    REQUIRE(result["component_analysis"]["actual_orientation_known"] == 0);
    REQUIRE(result["mesh_status"] == "needs_repair");
    REQUIRE(result["next_action"] == "advanced_repair");
}

TEST_CASE("A degenerate record is not a second surface at an open edge", "[ModelRepair][Inspection]")
{
    indexed_triangle_set its;
    its.vertices      = {Vec3f(0, 0, 0), Vec3f(10, 0, 0), Vec3f(0, 10, 0), Vec3f(20, 0, 0)};
    its.indices       = {Vec3i32(0, 1, 2), Vec3i32(0, 1, 3)};
    const auto result = inspect_model_mesh(TriangleMesh(std::move(its)));
    REQUIRE(result["issues"]["degenerate"]["count"] == 1);
    REQUIRE(result["issues"]["boundary"]["count"] == 3);
    REQUIRE(result["issues"]["nonmanifold_edge"]["count"] == 0);
}

TEST_CASE("Preparation failure leaves all earlier objects unchanged", "[ModelRepair][Preparation]")
{
    auto mesh = make_cube(10., 10., 10.);
    mesh.its.indices.push_back(mesh.its.indices.front());
    mesh = TriangleMesh(std::move(mesh.its));
    Model      model;
    auto*      first                 = model.add_object("duplicate", "", mesh)->volumes.front();
    const auto original              = first->mesh().its.indices;
    const auto id                    = first->id();
    auto       invalid               = make_cube(10., 10., 10.);
    invalid.its.vertices.front().x() = std::numeric_limits<float>::infinity();
    model.add_object()->add_volume(TriangleMesh{})->set_mesh(std::move(invalid));
    REQUIRE_THROWS(prepare_model_for_inspection(model));
    REQUIRE(first->id() == id);
    REQUIRE(first->mesh().its.indices == original);
}

TEST_CASE("Controlled preparation only removes exact degenerates and same-winding duplicates", "[ModelRepair][Preparation]")
{
    auto       mesh = make_cube(10., 10., 10.);
    const auto face = mesh.its.indices.front();
    mesh.its.indices.push_back(Vec3i32(face[1], face[2], face[0]));
    mesh.its.indices.push_back(Vec3i32(face[0], face[0], face[1]));
    mesh           = TriangleMesh(std::move(mesh.its));
    const auto raw = inspect_model_mesh(mesh);
    REQUIRE(raw["issues"]["degenerate"]["count"] == 1);
    REQUIRE(raw["issues"]["duplicate"]["count"] == 1);
    Model model;
    model.add_object("cleanup", "", mesh);
    const auto cleanup = prepare_model_for_inspection(model);
    REQUIRE(cleanup["changed"] == true);
    REQUIRE(cleanup["zero_area_faces_removed"] == 1);
    REQUIRE(cleanup["same_winding_duplicate_faces_removed"] == 1);
    REQUIRE(model.objects.front()->volumes.front()->mesh().facets_count() == 12);
    REQUIRE(prepare_model_for_inspection(model)["changed"] == false);
    REQUIRE(inspect_model_mesh(model.objects.front()->volumes.front()->mesh())["mesh_status"] == "passed");
}

TEST_CASE("Preparation retains opposite faces, planar surfaces and annotated duplicates", "[ModelRepair][Preparation]")
{
    SECTION("Opposite duplicate is not silently removed")
    {
        auto       mesh = make_cube(10., 10., 10.);
        const auto f    = mesh.its.indices.front();
        mesh.its.indices.push_back(Vec3i32(f[0], f[2], f[1]));
        mesh = TriangleMesh(std::move(mesh.its));
        Model model;
        model.add_object("opposite", "", mesh);
        REQUIRE(prepare_model_for_inspection(model)["changed"] == false);
        REQUIRE(inspect_model_mesh(mesh)["issues"]["duplicate"]["count"] == 1);
    }
    SECTION("A single triangle is retained")
    {
        indexed_triangle_set its;
        its.vertices = {Vec3f(0, 0, 0), Vec3f(10, 0, 0), Vec3f(0, 10, 0)};
        its.indices  = {Vec3i32(0, 1, 2)};
        Model model;
        model.add_object("plane", "", TriangleMesh(std::move(its)));
        REQUIRE(prepare_model_for_inspection(model)["changed"] == false);
        REQUIRE(model.objects.front()->volumes.front()->mesh().facets_count() == 1);
    }
    SECTION("An all-degenerate volume is deferred instead of erased")
    {
        indexed_triangle_set its;
        its.vertices = {Vec3f(0, 0, 0), Vec3f(1, 0, 0), Vec3f(2, 0, 0)};
        its.indices  = {Vec3i32(0, 1, 2)};
        Model model;
        model.add_object("line", "", TriangleMesh(std::move(its)));
        const auto prepared = prepare_model_for_inspection(model);
        REQUIRE(prepared["changed"] == false);
        REQUIRE(prepared["volumes"][0]["deferred_reason"] == "would_erase_volume");
    }
    SECTION("Face-indexed annotations require the existing strict advanced mapping")
    {
        auto mesh = make_cube(10., 10., 10.);
        mesh.its.indices.push_back(mesh.its.indices.front());
        mesh = TriangleMesh(std::move(mesh.its));
        Model model;
        auto* volume = model.add_object("paint", "", mesh)->volumes.front();
        volume->mmu_segmentation_facets.set_triangle_from_string(0, "1");
        REQUIRE(volume->mesh().facets_count() == 13);
        const auto original_mesh = volume->mesh_ptr();
        const auto vertices      = volume->mesh().its.vertices;
        const auto indices       = volume->mesh().its.indices;
        const auto id            = volume->id();
        const auto annotations   = volume->mmu_segmentation_facets.get_data();
        const auto prepared      = prepare_model_for_inspection(model);
        REQUIRE(prepared["changed"] == false);
        REQUIRE(prepared["volumes"][0]["deferred_reason"] == "annotations_require_advanced_repair");
        REQUIRE(volume->mesh().facets_count() == 13);
        REQUIRE(volume->mesh_ptr() == original_mesh);
        REQUIRE(volume->mesh().its.vertices == vertices);
        REQUIRE(volume->mesh().its.indices == indices);
        REQUIRE(volume->id() == id);
        REQUIRE(volume->mmu_segmentation_facets.get_data().bitstream == annotations.bitstream);
        REQUIRE_FALSE(volume->mmu_segmentation_facets.empty());
    }
}

TEST_CASE("Inspection distinguishes point contacts from edge-connected components", "[ModelRepair][Inspection]")
{
    auto mesh   = make_cube(10., 10., 10.);
    auto second = make_cube(10., 10., 10.);
    second.translate(10.f, 10.f, 10.f);
    mesh.merge(second);
    const auto result = inspect_model_mesh(mesh);
    REQUIRE(result["component_analysis"]["component_count"] == 2);
    REQUIRE(result["issues"]["nonmanifold_vertex"]["count"] == 1);
    REQUIRE(result["component_analysis"]["containment_complete"] == false);
    REQUIRE(result["mesh_status"] == "needs_repair");
}

TEST_CASE("Whole shell direction no longer affects the public inspection conclusion", "[ModelRepair][Inspection]")
{
    auto mesh = make_cube(10., 10., 10.);
    mesh.flip_triangles();
    const auto result = inspect_model_mesh(mesh);
    REQUIRE(result["issues"]["orientation"]["count"] == 0);
    REQUIRE(result["component_analysis"]["actual_orientation_known"] == 1);
    REQUIRE(result["component_analysis"]["orientation_suspect_count"] == 1);
    REQUIRE(result["check_complete"] == true);
    REQUIRE(result["mesh_status"] == "passed");
    REQUIRE(result["next_action"] == "none");
    const nlohmann::json check    = {{"inspection", result},
                                     {"mesh_status", result["mesh_status"]},
                                     {"check_complete", result["check_complete"]},
                                     {"next_action", result["next_action"]}};
    const auto           business = model_check_business_result(nlohmann::json::array({check}), 0, "");
    REQUIRE_FALSE(business["model_checks"][0].contains("supplementary"));
    CHECK(business["model_checks"][0]["mesh_status"] == "passed");
}

TEST_CASE("Nested cavity and small separate part are not automatically errors", "[ModelRepair][Inspection]")
{
    auto mesh   = make_cube(10., 10., 10.);
    auto cavity = make_cube(2., 2., 2.);
    cavity.translate(4.f, 4.f, 4.f);
    cavity.flip_triangles();
    mesh.merge(cavity);
    auto small = make_cube(0.1, 0.1, 0.1);
    small.translate(20.f, 20.f, 20.f);
    mesh.merge(small);
    const auto result = inspect_model_mesh(mesh);
    REQUIRE(result["component_analysis"]["component_count"] == 3);
    REQUIRE(result["component_analysis"]["confirmed_internal_components"] == 1);
    REQUIRE(result["component_analysis"]["small_component_candidates"] == 1);
    REQUIRE(result["component_analysis"]["orientation_suspect_count"] == 0);
    REQUIRE(result["mesh_status"] == "passed");
}

TEST_CASE("Limits and detail sampling cannot turn an incomplete inspection into a pass", "[ModelRepair][Inspection]")
{
    auto mesh   = make_cube(10., 10., 10.);
    auto second = make_cube(10., 10., 10.);
    second.translate(5.f, 5.f, 5.f);
    mesh.merge(second);
    SECTION("Intersection cap is a lower bound, including a zero cap")
    {
        ModelInspectionOptions options;
        options.max_intersections = 0;
        const auto result         = inspect_model_mesh(mesh, options);
        REQUIRE(result["issues"]["self_intersection"]["complete"] == false);
        REQUIRE(result["issues"]["self_intersection"]["count_is_lower_bound"] == true);
        REQUIRE(result["check_complete"] == false);
        REQUIRE(result["mesh_status"] != "passed");
    }
    SECTION("Detail sampling doesn't truncate detection counts")
    {
        ModelInspectionOptions options;
        options.detail_limit = 0;
        const auto result    = inspect_model_mesh(mesh, options);
        REQUIRE(result["issues"]["self_intersection"]["count"].get<std::size_t>() > 0);
        REQUIRE(result["issues"]["self_intersection"]["complete"] == true);
        REQUIRE(result["issues"]["self_intersection"]["pairs"].empty());
        REQUIRE(result["issues"]["self_intersection"]["details_truncated"] == true);
    }
    SECTION("Too many components skip containment")
    {
        auto separate = make_cube(10., 10., 10.);
        second.translate(50.f, 0.f, 0.f);
        separate.merge(second);
        ModelInspectionOptions options;
        options.max_solid_components = 1;
        REQUIRE(inspect_model_mesh(separate, options)["component_analysis"]["limit_reached"] == true);
        REQUIRE(inspect_model_mesh(separate, options)["mesh_status"] == "unknown");
    }
    SECTION("Invalid input is a failure, not zero issues")
    {
        ModelInspectionOptions options;
        options.max_triangles = 1;
        REQUIRE_THROWS(inspect_model_mesh(mesh, options));
        mesh.its.indices.front()[0] = -1;
        REQUIRE_THROWS(inspect_model_mesh(mesh));
        REQUIRE_THROWS(inspect_model_mesh(TriangleMesh{}));
    }
}

TEST_CASE("Proper crossings are opt-in evidence without changing existing inspection", "[ModelRepair][Inspection][ProperCrossing]")
{
    auto mesh = inspection_crossing_pair();
    SECTION("An exact crossing is reported with original face IDs")
    {
        // The self-intersection search excludes this first face. Evidence must
        // still refer to the original mesh, not the compact nondegenerate list.
        mesh.its.indices.insert(mesh.its.indices.begin(), Vec3i32(0, 0, 1));
    }
    SECTION("Very small crossings are not rounded into plane contacts")
    {
        mesh.its.vertices[3].z() = -1e-20f;
        mesh.its.vertices[4].z() = 1e-20f;
    }
    const auto baseline = inspect_model_mesh(mesh);
    CHECK_FALSE(baseline.contains("proper_crossings"));
    ModelInspectionOptions options;
    options.classify_proper_crossings = true;
    auto        result                = inspect_model_mesh(mesh, options);
    const auto& evidence              = result.at("proper_crossings");
    CHECK(evidence.at("status") == "completed");
    CHECK(evidence.at("detected") == true);
    CHECK(evidence.at("search_complete") == true);
    CHECK(evidence.at("pairs_checked") == 1);
    CHECK(evidence.at("total_pairs") == 1);
    CHECK(evidence.at("elapsed_ms").get<double>() >= 0.0);
    const auto first = mesh.its.indices.size() - 2;
    CHECK(evidence.at("witness").at("faces") == nlohmann::json::array({first, first + 1}));
    REQUIRE(evidence.at("witness").at("segment").size() == 2);
    CHECK(evidence.at("witness").at("segment")[0] != evidence.at("witness").at("segment")[1]);
    result.erase("proper_crossings");
    CHECK(result == baseline);
}

TEST_CASE("Proper crossings exclude contact and coplanar intersections", "[ModelRepair][Inspection][ProperCrossing]")
{
    TriangleMesh mesh;
    std::size_t  expected_pairs = 1;
    SECTION("Coplanar overlapping area") { mesh = inspection_coplanar_pair(); }
    SECTION("Point contact with only one plane strictly crossed")
    {
        mesh = inspection_triangle_pair({Vec3f(0.f, 0.f, 0.f), Vec3f(0.f, 0.f, 1.f), Vec3f(1.f, 0.f, 1.f)});
    }
    SECTION("Both planes are strictly crossed but their intersection is only a point")
    {
        mesh = inspection_triangle_pair({Vec3f(0.f, -2.f, 0.f), Vec3f(0.f, -3.f, -1.f), Vec3f(0.f, -3.f, 1.f)});
    }
    SECTION("Edge lying on another triangle")
    {
        mesh = inspection_triangle_pair({Vec3f(-1.f, 0.f, 0.f), Vec3f(1.f, 0.f, 0.f), Vec3f(0.f, 0.f, 1.f)});
    }
    SECTION("Healthy closed mesh has no self intersections")
    {
        mesh           = make_cube(10., 10., 10.);
        expected_pairs = 0;
    }
    ModelInspectionOptions options;
    options.classify_proper_crossings = true;
    const auto  result                = inspect_model_mesh(mesh, options);
    const auto& evidence              = result.at("proper_crossings");
    CHECK(result.at("issues").at("self_intersection").at("count") == expected_pairs);
    CHECK(evidence.at("status") == "completed");
    CHECK(evidence.at("detected") == false);
    CHECK(evidence.at("search_complete") == true);
    CHECK(evidence.at("pairs_checked") == expected_pairs);
    CHECK(evidence.at("total_pairs") == expected_pairs);
    CHECK(evidence.at("witness").is_null());
}

TEST_CASE("Proper crossing evidence retains incomplete search boundaries", "[ModelRepair][Inspection][ProperCrossing]")
{
    ModelInspectionOptions options;
    options.classify_proper_crossings = true;
    options.max_intersections         = 1;
    auto mesh                         = inspection_crossing_pair();
    mesh.merge(inspection_crossing_pair(10.f));
    bool expected_positive = true;
    SECTION("A found crossing is a witness but does not complete the search") {}
    SECTION("Zero pair budget cannot claim that crossings are absent")
    {
        options.max_intersections = 0;
        expected_positive         = false;
    }
    SECTION("A truncated contact-only list cannot rule out crossings")
    {
        mesh = inspection_coplanar_pair();
        mesh.merge(inspection_coplanar_pair(10.f));
        expected_positive = false;
    }
    const auto  result   = inspect_model_mesh(mesh, options);
    const auto& evidence = result.at("proper_crossings");
    CHECK(evidence.at("detected") == expected_positive);
    CHECK(evidence.at("status") == (expected_positive ? "completed" : "incomplete"));
    CHECK(evidence.at("search_complete") == false);
    CHECK(evidence.at("pairs_checked") == options.max_intersections);
    CHECK(evidence.at("total_pairs") == options.max_intersections);
    CHECK(evidence.at("witness").is_null() == !expected_positive);
    CHECK(result.at("issues").at("self_intersection").at("complete") == false);
    CHECK(result.at("issues").at("self_intersection").at("count_is_lower_bound") == true);
}

TEST_CASE("Proper crossing classification is not limited to display samples", "[ModelRepair][Inspection][ProperCrossing]")
{
    auto mesh = inspection_coplanar_pair();
    for (int i = 1; i < 201; ++i)
        mesh.merge(inspection_coplanar_pair(float(i * 10)));
    mesh.merge(inspection_crossing_pair(2010.f));
    ModelInspectionOptions options;
    options.classify_proper_crossings = true;
    SECTION("Default display limit hides the late crossing") {}
    SECTION("No display samples requested") { options.detail_limit = 0; }
    const auto  result   = inspect_model_mesh(mesh, options);
    const auto& evidence = result.at("proper_crossings");
    CHECK(result.at("issues").at("self_intersection").at("count") == 202);
    CHECK(result.at("issues").at("self_intersection").at("pairs").size() == options.detail_limit);
    CHECK(evidence.at("status") == "completed");
    CHECK(evidence.at("detected") == true);
    CHECK(evidence.at("search_complete") == true);
    CHECK(evidence.at("pairs_checked") == 202);
    CHECK(evidence.at("total_pairs") == 202);
    CHECK(evidence.at("witness").at("faces") == nlohmann::json::array({402, 403}));
}

TEST_CASE("Business check JSON is bounded and omits artifact paths", "[ModelRepair][Inspection][JSON]")
{
    using nlohmann::json;
    const auto inspection = inspect_model_mesh(make_cube(10., 10., 10.));
    json       check      = {{"inspection", inspection},
                             {"before_advanced", inspection},
                             {"input_file", "/source.obj"},
                             {"working_model", "/prepared.obj"},
                             {"prepared_output_files", {"/prepared.obj", "/prepared.mtl"}},
                             {"mesh_status", "passed"},
                             {"check_complete", true},
                             {"next_action", "none"},
                             {"advanced_repair", {{"status", "not_requested"}}}};
    const auto original   = check;
    const auto result     = model_check_business_result(json::array({check}), 0, "");
    REQUIRE(result.size() == 3);
    CHECK(result.at("return_code") == 0);
    CHECK(result.at("error_string") == "");
    CHECK_FALSE(result.contains("output_files"));
    CHECK(result.dump().find("/prepared") == std::string::npos);
    CHECK(result.dump().find("/source") == std::string::npos);
    REQUIRE(result.at("model_checks").size() == 1);
    const auto& summary = result.at("model_checks").front();
    REQUIRE(summary.size() == 6);
    CHECK(summary.at("schema_version") == 4);
    CHECK(summary.at("mesh_status") == "passed");
    CHECK(summary.at("check_complete") == true);
    CHECK(summary.at("input_materials_ignored") == false);
    CHECK(summary.at("repair_status") == "not_requested");
    CHECK_FALSE(summary.contains("boundary"));
    REQUIRE(summary.at("issues").size() == 7);
    for (auto it = inspection.at("issues").begin(); it != inspection.at("issues").end(); ++it) {
        const json expected = {{"count", it.value().at("count")}, {"complete", it.value().at("complete")}};
        CHECK(summary.at("issues").at(it.key()) == expected);
    }
    for (const char* removed : {"input_file", "working_model", "next_action", "repair_message", "supplementary", "issues_before_repair"})
        CHECK_FALSE(summary.contains(removed));
    CHECK(check == original);

    SECTION("Boundary extension requires an explicit request and preserves old fields")
    {
        check["repair_evidence_requested"] = true;
        auto  extended                     = model_check_business_result(json::array({check}), 0, "");
        auto& item                         = extended["model_checks"][0];
        REQUIRE(item.size() == 7);
        CHECK(item["boundary"]["schema_version"] == 2);
        CHECK(item["boundary"]["inspection"]["status"] == "passed");
        CHECK(item["boundary"]["repair"]["status"] == "not_run");
        CHECK(item["boundary"]["sections"]["status"] == "not_run");
        CHECK(item["boundary"]["reconstruction"]["assessment"] == "not_needed");
        item.erase("boundary");
        CHECK(extended == result);
    }

    SECTION("Internal sampled geometry never increases the public payload")
    {
        check["inspection"]["issues"]["boundary"]["faces"]           = std::vector<int>(10000, 1);
        check["basic_repair"]                                        = {{"facets_before", 12345}};
        check["after_advanced"]                                      = check["inspection"];
        check["repair_message"]                                      = std::string(10000, 'x');
        check["before_advanced"]["issues"]["boundary"]["count"]      = 999;
        check["inspection"]["component_analysis"]["component_count"] = 999;
        CHECK(model_check_business_result(json::array({check}), 0, "") == result);
    }
    SECTION("Changing internal artifact paths does not alter the business response")
    {
        check["prepared_output_files"] = {"/prepared.mtl", "/prepared.obj", "/prepared.mtl"};
        check["working_model"]         = "/another.obj";
        check["final_output_files"]    = {"/another.obj", "/another.mtl"};
        CHECK(model_check_business_result(json::array({check}), 0, "") == result);
    }
    SECTION("Published final color model exposes repair status without artifact paths")
    {
        check["working_model"]             = "/result.obj";
        check["final_output_files"]        = {"/result.obj", "/result.mtl"};
        check["advanced_repair"]["status"] = "repaired";
        const auto repaired                = model_check_business_result(json::array({check}), 0, "");
        CHECK_FALSE(repaired.contains("output_files"));
        CHECK(repaired.dump().find("/result") == std::string::npos);
        CHECK(repaired.at("model_checks").front().at("repair_status") == "repaired");
        CHECK(repaired.at("model_checks").front().at("issues") == summary.at("issues"));
        CHECK_FALSE(repaired.at("model_checks").front().contains("issues_before_repair"));
    }
    SECTION("Explicit format conversion without repair has no fabricated progress")
    {
        check["working_model"]      = "/result.stl";
        check["final_output_files"] = {"/result.stl"};
        const auto converted        = model_check_business_result(json::array({check}), 0, "");
        CHECK(converted == result);
        CHECK_FALSE(converted.at("model_checks").front().contains("issues_before_repair"));
    }
    SECTION("Healthy repair request does not repeat the unchanged inspection")
    {
        check["advanced_repair"]["status"] = "not_needed";
        const auto unchanged               = model_check_business_result(json::array({check}), 0, "");
        CHECK_FALSE(unchanged.at("model_checks").front().contains("issues_before_repair"));
        CHECK_FALSE(unchanged.contains("output_files"));
    }
    SECTION("Omitting structural details does not convert an incomplete check into a pass")
    {
        TriangleMesh separate = make_cube(10., 10., 10.);
        TriangleMesh second   = make_cube(10., 10., 10.);
        second.translate(30., 0., 0.);
        separate.merge(second);
        ModelInspectionOptions options;
        options.max_solid_components = 1;
        check["inspection"]          = inspect_model_mesh(separate, options);
        check["mesh_status"]         = check["inspection"].at("mesh_status");
        check["check_complete"]      = check["inspection"].at("check_complete");
        const auto  incomplete       = model_check_business_result(json::array({check}), 0, "");
        const auto& current          = incomplete.at("model_checks").front();
        CHECK(current.at("mesh_status") == "unknown");
        CHECK(current.at("check_complete") == false);
        for (const auto& issue : current.at("issues")) {
            CHECK(issue.at("count") == 0);
            CHECK(issue.at("complete") == true);
        }
        CHECK_FALSE(current.contains("supplementary"));
        check["repair_evidence_requested"] = true;
        const auto extended                = model_check_business_result(json::array({check}), 0, "");
        CHECK(extended["model_checks"][0]["boundary"]["inspection"]["status"] == "incomplete");
        CHECK(extended["model_checks"][0]["boundary"]["reconstruction"]["assessment"] == "blocked");
    }
}

TEST_CASE("Business JSON distinguishes retained progress from complete or failed repair", "[ModelRepair][Inspection][JSON]")
{
    using nlohmann::json;
    auto before                                         = inspect_model_mesh(make_cube(10., 10., 10.));
    before["issues"]["self_intersection"]["count"]      = 200;
    auto after                                          = before;
    after["issues"]["self_intersection"]["count"]       = 10;
    after["issues"]["self_intersection"]["complete"]    = false;
    after["component_analysis"]["containment_complete"] = false;
    after["component_analysis"]["orientation_complete"] = false;
    json check                                          = {{"inspection", after},
                                                           {"before_advanced", before},
                                                           {"working_model", "/result.stl"},
                                                           {"prepared_output_files", {"/prepared.stl"}},
                                                           {"final_output_files", {"/result.stl"}},
                                                           {"mesh_status", "needs_repair"},
                                                           {"check_complete", false},
                                                           {"next_action", "advanced_repair"},
                                                           {"repair_message", "Residual intersections"},
                                                           {"input_materials_ignored", true},
                                                           {"advanced_repair", {{"status", "partially_repaired"}}}};
    SECTION("Partial success preserves lower bounds and unknown containment")
    {
        const auto  result  = model_check_business_result(json::array({check}), 0, "");
        const auto& summary = result.at("model_checks").front();
        CHECK_FALSE(summary.contains("issues_before_repair"));
        CHECK(summary.at("issues").at("self_intersection").at("count") == 10);
        CHECK(summary.at("issues").at("self_intersection").at("complete") == false);
        CHECK_FALSE(summary.contains("supplementary"));
        CHECK(summary.at("check_complete") == false);
        CHECK(summary.at("mesh_status") == "needs_repair");
        CHECK(summary.at("repair_status") == "partially_repaired");
        CHECK_FALSE(summary.contains("repair_message"));
        CHECK(summary.at("input_materials_ignored") == true);
        CHECK_FALSE(result.contains("output_files"));
    }
    SECTION("Failed export keeps the baseline and never exposes an unpublished candidate")
    {
        check["inspection"]    = before;
        check["working_model"] = "/prepared.stl";
        check.erase("final_output_files"); // Candidate publication did not happen.
        check["advanced_repair"] = {{"status", "failed"}, {"committed", true}};
        check["operation_error"] = "Export validation failed";
        const auto result        = model_check_business_result(json::array({check}), -25, "Export validation failed");
        CHECK(result.at("return_code") == -25);
        CHECK(result.at("error_string") == "Export validation failed");
        CHECK_FALSE(result.contains("output_files"));
        const auto& summary = result.at("model_checks").front();
        CHECK(summary.at("repair_status") == "failed");
        for (auto it = before.at("issues").begin(); it != before.at("issues").end(); ++it) {
            CHECK(summary.at("issues").at(it.key()).at("count") == it.value().at("count"));
            CHECK(summary.at("issues").at(it.key()).at("complete") == it.value().at("complete"));
        }
    }
    SECTION("No retained improvement is unresolved, not partial success")
    {
        check["inspection"]    = before;
        check["working_model"] = "/prepared.stl";
        check.erase("final_output_files");
        check["advanced_repair"]["status"] = "unresolved";
        const auto result                  = model_check_business_result(json::array({check}), -25, "No retainable change");
        CHECK(result.at("return_code") == -25);
        CHECK(result.at("model_checks").front().at("repair_status") == "unresolved");
        for (auto it = before.at("issues").begin(); it != before.at("issues").end(); ++it) {
            const auto& current = result.at("model_checks").front().at("issues").at(it.key());
            CHECK(current.at("count") == it.value().at("count"));
            CHECK(current.at("complete") == it.value().at("complete"));
        }
        CHECK_FALSE(result.contains("output_files"));
    }
}

TEST_CASE("Business JSON never substitutes zero findings for an inspection that did not run", "[ModelRepair][Inspection][JSON]")
{
    using nlohmann::json;
    json        check   = {{"inspection", nullptr}, {"advanced_repair", {{"status", "not_started"}}}};
    const auto  result  = model_check_business_result(json::array({check}), -27, "Invalid model");
    const auto& summary = result.at("model_checks").front();
    CHECK(result.at("return_code") == -27);
    CHECK(result.at("error_string") == "Invalid model");
    CHECK_FALSE(result.contains("output_files"));
    CHECK_FALSE(summary.contains("working_model"));
    CHECK(summary.at("issues").is_null());
    CHECK_FALSE(summary.contains("supplementary"));
    CHECK(summary.at("mesh_status") == "unknown");
    CHECK(summary.at("check_complete") == false);
    CHECK_FALSE(summary.contains("next_action"));
    CHECK(summary.at("repair_status") == "not_started");
    CHECK_FALSE(summary.contains("issues_before_repair"));
    check["prepared_output_files"] = {"/unpublished.stl"};
    CHECK(model_check_business_result(json::array({check}), -27, "Invalid model") == result);
    check["working_model"] = "/different.stl";
    CHECK(model_check_business_result(json::array({check}), -27, "Invalid model") == result);
}
