#include <catch2/catch.hpp>
#include <test_utils.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/ModelInspection.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <limits>

using namespace Slic3r;

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
    REQUIRE(result["check_complete"] == false);
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

TEST_CASE("Whole shell orientation is a separate design convention", "[ModelRepair][Inspection]")
{
    auto mesh = make_cube(10., 10., 10.);
    mesh.flip_triangles();
    const auto result = inspect_model_mesh(mesh);
    REQUIRE(result["issues"]["orientation"]["count"] == 0);
    REQUIRE(result["component_analysis"]["actual_orientation_known"] == 1);
    REQUIRE(result["component_analysis"]["orientation_suspect_count"] == 1);
    REQUIRE(result["mesh_status"] == "needs_review");
    REQUIRE(result["next_action"] == "review_structure");
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
