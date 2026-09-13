#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <test_utils.hpp>

#include <libslic3r/MeshBoolean.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/ModelRepair.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>
#include <slic3r/Utils/ModelRepairPolicy.hpp>

using namespace Slic3r;

namespace {

TriangleMesh cube_with_open_side()
{
    TriangleMesh mesh = make_cube(10., 10., 10.);
    mesh.its.indices.erase(mesh.its.indices.begin(), mesh.its.indices.begin() + 2);
    return TriangleMesh(std::move(mesh.its));
}

TriangleMesh open_tetrahedron_at_origin()
{
    // All three faces touch the origin: the signed volume is zero even though
    // this is a spatial shell that can be closed by one triangular face.
    indexed_triangle_set its;
    its.vertices = {Vec3f(0.f, 0.f, 0.f), Vec3f(10.f, 0.f, 0.f), Vec3f(0.f, 10.f, 0.f), Vec3f(0.f, 0.f, 10.f)};
    its.indices  = {Vec3i32(0, 2, 1), Vec3i32(0, 1, 3), Vec3i32(0, 3, 2)};
    return TriangleMesh(std::move(its));
}

TriangleMesh overlapping_cubes()
{
    TriangleMesh first  = make_cube(10., 10., 10.);
    TriangleMesh second = make_cube(10., 10., 10.);
    second.translate(5.f, 0.f, 0.f);
    first.merge(second);
    return first;
}

TriangleMesh tetrahedra_sharing_an_edge()
{
    // Four faces meet at edge (0, 1). Each tetrahedron is closed and has
    // positive volume, but the shared edge is not manifold. Soup orientation
    // must duplicate its endpoints; the resulting shells still touch in space.
    indexed_triangle_set its;
    its.vertices = {Vec3f(0.f, 0.f, 0.f),  Vec3f(10.f, 0.f, 0.f),  Vec3f(0.f, 10.f, 0.f),
                    Vec3f(0.f, 0.f, 10.f), Vec3f(0.f, -10.f, 0.f), Vec3f(0.f, 0.f, -10.f)};
    its.indices  = {Vec3i32(0, 2, 1), Vec3i32(0, 1, 3), Vec3i32(0, 3, 2), Vec3i32(1, 2, 3),
                    Vec3i32(0, 4, 1), Vec3i32(0, 1, 5), Vec3i32(0, 5, 4), Vec3i32(1, 4, 5)};
    return TriangleMesh(std::move(its));
}

TriangleMesh two_disjoint_shells(bool reverse_second)
{
    TriangleMesh first  = make_cube(20., 20., 20.);
    TriangleMesh second = make_cube(5., 5., 5.);
    second.translate(30.f, 0.f, 0.f);
    if (reverse_second)
        second.flip_triangles();
    first.merge(second);
    return first;
}

TriangleMesh planar_square()
{
    indexed_triangle_set its;
    its.vertices = {Vec3f(30.f, 0.f, 0.f), Vec3f(40.f, 0.f, 0.f), Vec3f(40.f, 10.f, 0.f), Vec3f(30.f, 10.f, 0.f)};
    its.indices  = {Vec3i32(0, 1, 2), Vec3i32(0, 2, 3)};
    return TriangleMesh(std::move(its));
}

TriangleMesh cube_with_planar_component()
{
    TriangleMesh mesh  = make_cube(10., 10., 10.);
    TriangleMesh plane = planar_square();
    mesh.merge(plane);
    return mesh;
}

TriangleMesh cube_with_thin_component()
{
    TriangleMesh mesh      = make_cube(10., 10., 10.);
    TriangleMesh thin_part = make_cube(10., 10., EPSILON * 0.5);
    thin_part.translate(30.f, 0.f, 0.f);
    mesh.merge(thin_part);
    return mesh;
}

TriangleMesh cube_with_canceling_signed_volume()
{
    TriangleMesh                                   mesh           = make_cube(10., 10., 10.);
    const std::vector<stl_triangle_vertex_indices> original_faces = mesh.its.indices;
    // Find a deterministic subset of reversed faces whose signed tetrahedral
    // contributions cancel. The geometry and connectivity remain a 3D cube.
    for (size_t mask = 1; mask < (size_t(1) << original_faces.size()); ++mask) {
        mesh.its.indices = original_faces;
        for (size_t face_index = 0; face_index < original_faces.size(); ++face_index)
            if ((mask & (size_t(1) << face_index)) != 0)
                std::swap(mesh.its.indices[face_index][1], mesh.its.indices[face_index][2]);
        if (std::abs(its_volume(mesh.its)) <= EPSILON && its_num_open_edges(mesh.its) > 0)
            return mesh;
    }
    throw std::runtime_error("Could not construct a cube with canceling signed volume.");
}

TriangleMesh cube_with_unreferenced_far_vertex()
{
    indexed_triangle_set its = make_cube(10., 10., 10.).its;
    its.vertices.emplace_back(1.e6f, 1.e6f, 1.e6f);
    return TriangleMesh(std::move(its));
}

TriangleMesh cube_with_collinear_far_face()
{
    indexed_triangle_set its = make_cube(10., 10., 10.).its;
    its.vertices.emplace_back(10.f, 2.e7f, 0.f);
    its.indices.emplace_back(0, 1, int(its.vertices.size() - 1));
    return TriangleMesh(std::move(its));
}

double absolute_shell_volume(const TriangleMesh& mesh)
{
    double volume = 0.;
    for (const indexed_triangle_set& shell : its_split(mesh.its))
        volume += std::abs(its_volume(shell));
    return volume;
}

Model make_model(TriangleMesh mesh)
{
    Model        model;
    ModelObject* object = model.add_object();
    object->add_volume(std::move(mesh));
    object->add_instance();
    return model;
}

ModelVolume* add_invalid_volume(ModelObject& object)
{
    TriangleMesh invalid_mesh           = make_cube(10., 10., 10.);
    invalid_mesh.its.indices.front()[0] = int(invalid_mesh.its.vertices.size());

    // Avoid asking the ModelVolume constructor to calculate a convex hull for
    // deliberately invalid input. Model repair must reject the invalid index
    // before splitting, cleanup or CGAL processing.
    ModelVolume* volume = object.add_volume(TriangleMesh{});
    volume->set_mesh(std::move(invalid_mesh));
    return volume;
}

void paint_facet(FacetsAnnotation& annotation, const TriangleMesh& mesh, int facet, EnforcerBlockerType state)
{
    TriangleSelector selector(mesh);
    selector.set_facet(facet, state);
    REQUIRE(annotation.set(selector));
}

} // namespace

TEST_CASE("Import cleanup only removes objects without triangle faces", "[ModelRepair][Import]")
{
    TriangleMesh mesh;
    SECTION("open shell with zero signed volume") { mesh = open_tetrahedron_at_origin(); }
    SECTION("inconsistent winding with canceling signed volume") { mesh = TriangleMesh(cube_with_canceling_signed_volume().its); }
    SECTION("planar surface remains available for explicit repair diagnostics") { mesh = planar_square(); }
    SECTION("thin solid below the old volume cutoff")
    {
        mesh = make_cube(0.0001, 0.0001, 0.0001);
        REQUIRE(mesh.stats().volume > 0.0);
        REQUIRE(mesh.stats().volume < 1.e-10);
    }
    Model        model              = make_model(std::move(mesh));
    ModelObject* object             = model.objects.front();
    ModelVolume* volume             = object->volumes.front();
    const auto   original_mesh      = volume->mesh_ptr();
    const auto   original_transform = volume->get_transformation();
    // An empty volume must not cause a nonempty sibling to be discarded.
    object->add_volume(TriangleMesh{});
    model.add_object();
    model.add_object()->add_volume(TriangleMesh{});

    CHECK(model.remove_empty_objects() == 2);
    REQUIRE(model.objects.size() == 1);
    CHECK(model.objects.front() == object);
    CHECK(object->volumes.front() == volume);
    CHECK(volume->mesh_ptr() == original_mesh);
    CHECK(volume->get_transformation() == original_transform);
    CHECK(model.remove_empty_objects() == 0);
    Model empty;
    CHECK(empty.remove_empty_objects() == 0);
}

TEST_CASE("Import unit guesses do not rescale zero-volume repair candidates", "[ModelRepair][Import]")
{
    const bool   meters             = GENERATE(false, true);
    Model        model              = make_model(open_tetrahedron_at_origin());
    ModelObject* candidate          = model.objects.front();
    ModelVolume* volume             = candidate->volumes.front();
    const auto   original_vertices  = volume->mesh().its.vertices;
    const auto   original_transform = volume->get_transformation();
    REQUIRE(candidate->get_object_stl_stats().volume == 0.0);
    CHECK_FALSE(model.looks_like_imperial_units());
    CHECK_FALSE(model.looks_like_saved_in_meters());

    // Another genuinely small object may trigger a conversion prompt. Its
    // conversion must not also scale the zero-volume repair candidate.
    ModelVolume* small        = model.add_object()->add_volume(make_cube(0.1, 0.1, 0.1));
    const double small_volume = small->mesh().stats().volume;
    CHECK(model.looks_like_imperial_units());
    CHECK(model.looks_like_saved_in_meters());
    if (meters)
        model.convert_from_meters(true);
    else
        model.convert_from_imperial_units(true);
    CHECK(volume->mesh().its.vertices == original_vertices);
    CHECK(volume->get_transformation() == original_transform);
    const double factor = meters ? 1000.0 : 25.4;
    CHECK(small->mesh().stats().volume == Approx(small_volume * factor * factor * factor).epsilon(1.e-5));

    // Explicit full-model conversion still honors the user's units choice.
    if (meters)
        model.convert_from_meters(false);
    else
        model.convert_from_imperial_units(false);
    CHECK(volume->mesh().its.vertices != original_vertices);
}

TEST_CASE("Repair acceptance fixtures recover the reference cube and its slices", "[ModelRepair][CGAL][Acceptance]")
{
    const char* fixture = GENERATE("00-healthy-cube", "01-missing-top", "02-small-crack", "03-wrong-winding",
                                   "04-duplicate-face", "05-degenerate-face", "06-isolated-face", "11-colored-missing-top");
    INFO(fixture);
    TriangleMesh input;
    REQUIRE(input.ReadSTLFile((std::string(TEST_DATA_DIR) + "/model_repair_acceptance/" + fixture + ".stl").c_str(), false));
    Model model = make_model(std::move(input));
    ModelRepairReport report;
    REQUIRE(repair_model(model, report));
    REQUIRE(report.success);
    REQUIRE(model.objects.size() == 1);
    REQUIRE(model.objects.front()->volumes.size() == 1);
    const TriangleMesh& result = model.objects.front()->volumes.front()->mesh();
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(result));
    CHECK(its_volume(result.its) == Approx(1000.).margin(0.1));
    CHECK((result.bounding_box().size() - Vec3d(10., 10., 10.)).norm() < 0.001);
    if (std::string(fixture) == "02-small-crack")
        CHECK(result.its.indices.size() == 12); // close the crack, do not cover it with extra triangles

    // No 2D gap-closing allowance: compare real horizontal sections with the
    // healthy reference. This is contour regression, not full G-code validation.
    std::vector<float> heights;
    for (int layer = 0; layer < 50; ++layer)
        heights.push_back(0.1f + 0.2f * layer);
    const auto reference = slice_mesh_ex(make_cube(10., 10., 10.).its, heights);
    // add_volume() centers the mesh and stores the original position in the
    // volume transform. Slice in object coordinates, like the reference cube.
    TriangleMesh result_in_object = result;
    result_in_object.transform(model.objects.front()->volumes.front()->get_matrix());
    CHECK(result_in_object.bounding_box().min.norm() < 0.001);
    const auto slices = slice_mesh_ex(result_in_object.its, heights);
    REQUIRE(slices.size() == reference.size());
    for (size_t i = 0; i < slices.size(); ++i) {
        INFO(i);
        REQUIRE(reference[i].size() == 1);
        REQUIRE(slices[i].size() == 1);
        CHECK(slices[i].front().holes.empty());
        CHECK(area(slices[i]) == Approx(area(reference[i])).epsilon(1.e-4));
    }
}

TEST_CASE("Unsafe acceptance fixtures fail without replacing the source", "[ModelRepair][CGAL][Acceptance]")
{
    const char* fixture = GENERATE("08-intersecting-shells", "09-nonmanifold-edge", "10-only-triangle");
    INFO(fixture);
    TriangleMesh input;
    REQUIRE(input.ReadSTLFile((std::string(TEST_DATA_DIR) + "/model_repair_acceptance/" + fixture + ".stl").c_str(), false));
    Model model = make_model(std::move(input));
    ModelVolume* volume = model.objects.front()->volumes.front();
    const auto source = volume->mesh_ptr();
    const auto source_id = volume->id();
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report));
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    REQUIRE_FALSE(report.failures.empty());
    CHECK(model.objects.front()->volumes.front() == volume);
    CHECK(volume->mesh_ptr() == source);
    CHECK(volume->id() == source_id);
}

TEST_CASE("Basic cleanup does not discard a thin closed solid", "[ModelRepair][CGAL][Acceptance]")
{
    TriangleMesh input;
    REQUIRE(input.ReadSTLFile((std::string(TEST_DATA_DIR) + "/model_repair_acceptance/07-thin-solid.stl").c_str(), false));
    Model model = make_model(std::move(input));
    ModelRepairReport report;
    REQUIRE(repair_model(model, report));
    REQUIRE(model.objects.front()->volumes.size() == 2);
    double volume = 0.;
    for (const ModelVolume* part : model.objects.front()->volumes) {
        CHECK_FALSE(MeshBoolean::cgal::requires_repair(part->mesh()));
        CHECK(part->mesh().its.indices.size() == 12);
        volume += its_volume(part->mesh().its);
    }
    CHECK(volume == Approx(1000.1).margin(0.001));
}

TEST_CASE("Removing an isolated painted facet fails strict color preservation", "[ModelRepair][CGAL][Acceptance][Painting]")
{
    TriangleMesh input;
    REQUIRE(input.ReadSTLFile((std::string(TEST_DATA_DIR) + "/model_repair_acceptance/06-isolated-face.stl").c_str(), false));
    Model model = make_model(std::move(input));
    ModelVolume* volume = model.objects.front()->volumes.front();
    paint_facet(volume->mmu_segmentation_facets, volume->mesh(), 12, EnforcerBlockerType::Extruder3);
    const auto source = volume->mesh_ptr();
    ModelRepairOptions options;
    options.preserve_annotations = true;
    options.allow_annotation_clearing = false;
    options.remove_non_3d_parts = false;
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));
    CHECK_FALSE(report.committed);
    CHECK(volume->mesh_ptr() == source);
    CHECK_FALSE(volume->mmu_segmentation_facets.empty());
}

TEST_CASE("Boundary cleanup does not weld independent nearby open shells", "[ModelRepair][CGAL][Acceptance]")
{
    TriangleMesh first;
    REQUIRE(first.ReadSTLFile((std::string(TEST_DATA_DIR) + "/model_repair_acceptance/01-missing-top.stl").c_str(), false));
    TriangleMesh second = first;
    // The facing top boundary edges are closer than the crack tolerance, but
    // these are independent shells, not a crack within one connected surface.
    second.translate(10.00002f, 0.f, 0.f);
    first.merge(second);
    std::string error;
    REQUIRE(MeshBoolean::cgal::repair(first, nullptr, &error));
    INFO(error);
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(first));
    CHECK(its_split(first.its).size() == 2);
    CHECK(its_volume(first.its) == Approx(2000.).margin(0.1));
}

TEST_CASE("Basic repair rejects non-finite input before touching geometry", "[ModelRepair][CGAL][Acceptance]")
{
    TriangleMesh mesh = make_cube(10., 10., 10.);
    const auto faces = mesh.its.indices;
    const Vec3f unchanged_vertex = mesh.its.vertices.back();
    const bool use_nan = GENERATE(false, true);
    mesh.its.vertices.front().x() = use_nan ? std::numeric_limits<float>::quiet_NaN() :
                                           std::numeric_limits<float>::infinity();
    std::string error;
    CHECK_FALSE(MeshBoolean::cgal::repair(mesh, nullptr, &error));
    CHECK(error.find("non-finite") != std::string::npos);
    CHECK(mesh.its.indices == faces);
    CHECK(mesh.its.vertices.back() == unchanged_vertex);
    CHECK_FALSE(std::isfinite(mesh.its.vertices.front().x()));
}

TEST_CASE("Manual model repair closes an open model-part volume", "[ModelRepair][CGAL]")
{
    Model model = make_model(cube_with_open_side());

    const size_t before_open_edges = its_num_open_edges(model.objects.front()->volumes.front()->mesh().its);
    REQUIRE(before_open_edges > 0);

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    const TriangleMesh& repaired = model.objects.front()->volumes.front()->mesh();
    CHECK(report.success);
    CHECK(report.attempted);
    CHECK(report.committed);
    CHECK(report.repaired == 1);
    CHECK(report.status() == std::string("repaired"));
    CHECK(report.before_open_edges == before_open_edges);
    CHECK(report.after_open_edges == 0);
    CHECK(its_num_open_edges(repaired.its) == 0);
    CHECK(its_volume(repaired.its) > 0.);
}

TEST_CASE("Manual repair inspects closed defects in the bundled 3DBenchy", "[ModelRepair][CGAL][3DBenchy]")
{
    const std::string path  = std::string(TEST_DATA_DIR) + "/../../resources/handy_models/3DBenchy.3mf";
    Model             model = Model::read_from_file(path, nullptr, nullptr, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances);

    REQUIRE(model.objects.size() == 1);
    REQUIRE(!model.objects.front()->volumes.empty());

    const size_t        before_open_edges = its_num_open_edges(model.objects.front()->volumes.front()->mesh().its);
    const auto          before_mesh       = model.objects.front()->volumes.front()->mesh_ptr();
    const BoundingBoxf3 before_bbox       = before_mesh->bounding_box();
    const double        before_volume     = std::abs(its_volume(before_mesh->its));
    REQUIRE(before_open_edges == 0);
    REQUIRE(MeshBoolean::cgal::requires_repair(*before_mesh));
    REQUIRE(MeshBoolean::cgal::does_self_intersect(*before_mesh));

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    const TriangleMesh& repaired        = model.objects.front()->volumes.front()->mesh();
    const BoundingBoxf3 repaired_bbox   = repaired.bounding_box();
    const double        repaired_volume = std::abs(its_volume(repaired.its));
    const double        volume_delta    = std::abs(repaired_volume - before_volume) / std::max(before_volume, 1.e-9);

    CHECK(report.success);
    CHECK(report.committed);
    CHECK(report.attempted);
    CHECK(report.repaired == 1);
    CHECK(report.failed == 0);
    CHECK(report.status() == std::string("repaired"));
    CHECK(model.objects.front()->volumes.front()->mesh_ptr() != before_mesh);
    CHECK(report.after_open_edges == 0);
    CHECK(its_num_open_edges(repaired.its) == 0);
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(repaired));
    CHECK_FALSE(MeshBoolean::cgal::does_self_intersect(repaired));
    CHECK((repaired_bbox.min - before_bbox.min).cwiseAbs().maxCoeff() <= 0.01);
    CHECK((repaired_bbox.max - before_bbox.max).cwiseAbs().maxCoeff() <= 0.01);
    CHECK(volume_delta <= 1.e-3);
}

TEST_CASE("Manual repair rejects an empty model-part volume", "[ModelRepair][CGAL]")
{
    Model model = make_model(TriangleMesh{});

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report));
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.failed == 1);
    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures.front().message == "Repair failed: model-part volume is empty.");
}

TEST_CASE("CGAL repair diagnosis rejects non-finite vertices", "[ModelRepair][MeshBoolean][CGAL][Validation]")
{
    TriangleMesh mesh = make_cube(10., 10., 10.);

    SECTION("NaN") { mesh.its.vertices.front().x() = std::numeric_limits<float>::quiet_NaN(); }
    SECTION("positive infinity") { mesh.its.vertices.front().y() = std::numeric_limits<float>::infinity(); }
    SECTION("negative infinity") { mesh.its.vertices.front().z() = -std::numeric_limits<float>::infinity(); }

    CHECK(MeshBoolean::cgal::requires_repair(mesh));
}

TEST_CASE("Prepared volume-structure move changes nothing before its allocation-free commit", "[ModelRepair][Transaction]")
{
    Model          destination          = make_model(make_cube(10., 10., 10.));
    Model          source               = Model(destination);
    ModelObject*   destination_object   = destination.objects.front();
    ModelObject*   source_object        = source.objects.front();
    ModelVolume*   destination_volume   = destination_object->volumes.front();
    ModelInstance* destination_instance = destination_object->instances.front();
    ModelInstance* source_instance      = source_object->instances.front();

    source_object->volumes.front()->name = "repaired";
    source_object->add_volume(make_cube(4., 4., 4.));
    source_instance->set_offset(Vec3d(12., 3., 0.));

    const ModelVolumePtrs                    source_volumes             = source_object->volumes;
    const Geometry::Transformation           destination_transformation = destination_instance->get_transformation();
    const Geometry::Transformation           source_transformation      = source_instance->get_transformation();
    Model::PreparedObjectVolumeStructureMove prepared                   = destination.prepare_move_object_volume_structures(source, {0});

    REQUIRE(destination.objects.front() == destination_object);
    REQUIRE(destination_object->volumes.size() == 1);
    CHECK(destination_object->volumes.front() == destination_volume);
    CHECK(destination_instance->get_transformation() == destination_transformation);

    destination.commit_move_object_volume_structures(std::move(prepared));

    CHECK(prepared.empty());
    REQUIRE(destination.objects.front() == destination_object);
    REQUIRE(destination_object->instances.front() == destination_instance);
    REQUIRE(destination_object->volumes.size() == source_volumes.size());
    CHECK(destination_object->volumes == source_volumes);
    CHECK(destination_instance->get_transformation() == source_transformation);
    CHECK(source_instance->get_transformation() == destination_transformation);
    REQUIRE(source_object->volumes.size() == 1);
    CHECK(source_object->volumes.front() == destination_volume);
    for (const ModelVolume* volume : destination_object->volumes)
        CHECK(volume->get_object() == destination_object);
    CHECK(source_object->volumes.front()->get_object() == source_object);
}

TEST_CASE("Model repair staging copies only targeted objects", "[ModelRepair][Transaction][Staging]")
{
    Model model = make_model(make_cube(10., 10., 10.));

    ModelObject* selected = model.add_object();
    selected->name        = "selected";
    selected->add_volume(cube_with_open_side());
    selected->add_volume(make_cube(4., 4., 4.));
    selected->add_instance();

    ModelObject* unselected = model.add_object();
    unselected->name        = "unselected";
    unselected->add_volume(make_cube(8., 8., 8.));
    unselected->add_instance();

    const std::shared_ptr<const TriangleMesh> selected_mesh   = selected->volumes.front()->mesh_ptr();
    const std::shared_ptr<const TriangleMesh> unselected_mesh = unselected->volumes.front()->mesh_ptr();
    const long                                unselected_uses = unselected_mesh.use_count();

    const std::vector<ModelRepairTarget> targets{{1, size_t(0)}, {1, std::nullopt}, {1, size_t(1)}};
    std::unique_ptr<Model>               staged = make_model_repair_staging_copy(model, targets);

    REQUIRE(staged != nullptr);
    REQUIRE(staged->objects.size() == model.objects.size());
    CHECK(staged->objects[0] == nullptr);
    CHECK(staged->objects[2] == nullptr);
    REQUIRE(staged->objects[1] != nullptr);
    CHECK(staged->objects[1] != selected);
    CHECK(staged->objects[1]->id() == selected->id());
    CHECK(staged->objects[1]->get_model() == staged.get());
    REQUIRE(staged->objects[1]->volumes.size() == selected->volumes.size());
    CHECK(staged->objects[1]->volumes[0]->mesh_ptr() == selected_mesh);
    CHECK(staged->objects[1]->volumes[0]->get_object() == staged->objects[1]);
    CHECK(staged->objects[1]->volumes[1]->get_object() == staged->objects[1]);
    CHECK(unselected_mesh.use_count() == unselected_uses);
}

TEST_CASE("Model repair observes cancellation before staging", "[ModelRepair][Transaction][Cancel]")
{
    Model                                     model         = make_model(cube_with_open_side());
    const std::shared_ptr<const TriangleMesh> original_mesh = model.objects.front()->volumes.front()->mesh_ptr();
    bool                                      repair_called = false;

    ModelRepairOptions options;
    options.is_canceled = []() { return true; };
    options.repair_mesh = [&](TriangleMesh&, std::string&) {
        repair_called = true;
        return false;
    };

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    CHECK(report.canceled);
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.attempted);
    CHECK_FALSE(report.committed);
    CHECK_FALSE(repair_called);
    CHECK(model.objects.front()->volumes.front()->mesh_ptr() == original_mesh);
}

TEST_CASE("Preparing multiple volume-structure moves fails atomically", "[ModelRepair][Transaction]")
{
    Model        destination = make_model(make_cube(10., 10., 10.));
    ModelObject* second      = destination.add_object();
    second->add_volume(make_cube(8., 8., 8.));
    second->add_instance();
    Model source(destination);

    ModelObject*                   first_destination       = destination.objects.front();
    ModelVolume*                   original_volume         = first_destination->volumes.front();
    ModelInstance*                 original_instance       = first_destination->instances.front();
    const Geometry::Transformation original_transformation = original_instance->get_transformation();

    source.objects.front()->add_volume(make_cube(4., 4., 4.));
    source.objects.front()->instances.front()->set_offset(Vec3d(9., 2., 0.));
    source.objects[1]->delete_instance(0);

    CHECK_THROWS(destination.prepare_move_object_volume_structures(source, {0, 1}));
    REQUIRE(destination.objects.front() == first_destination);
    REQUIRE(first_destination->volumes.size() == 1);
    CHECK(first_destination->volumes.front() == original_volume);
    CHECK(first_destination->volumes.front()->get_object() == first_destination);
    REQUIRE(first_destination->instances.front() == original_instance);
    CHECK(original_instance->get_transformation() == original_transformation);
}

TEST_CASE("Prepared volume-structure move commits multiple objects together", "[ModelRepair][Transaction]")
{
    Model        destination = make_model(make_cube(10., 10., 10.));
    ModelObject* second      = destination.add_object();
    second->add_volume(make_cube(8., 8., 8.));
    second->add_instance();
    Model source(destination);

    source.objects[0]->add_volume(make_cube(4., 4., 4.));
    source.objects[0]->instances.front()->set_offset(Vec3d(7., 1., 0.));
    source.objects[1]->volumes.front()->name = "second repaired";
    source.objects[1]->instances.front()->set_offset(Vec3d(3., 6., 0.));

    const std::array<ModelObject*, 2>             destination_objects      = {destination.objects[0], destination.objects[1]};
    const std::array<ModelVolumePtrs, 2>          repaired_volumes         = {source.objects[0]->volumes, source.objects[1]->volumes};
    const std::array<Geometry::Transformation, 2> repaired_transformations = {source.objects[0]->instances.front()->get_transformation(),
                                                                              source.objects[1]->instances.front()->get_transformation()};

    Model::PreparedObjectVolumeStructureMove prepared = destination.prepare_move_object_volume_structures(source, {1, 0});
    destination.commit_move_object_volume_structures(std::move(prepared));

    for (size_t object_index = 0; object_index < destination_objects.size(); ++object_index) {
        REQUIRE(destination.objects[object_index] == destination_objects[object_index]);
        CHECK(destination.objects[object_index]->volumes == repaired_volumes[object_index]);
        CHECK(destination.objects[object_index]->instances.front()->get_transformation() == repaired_transformations[object_index]);
        for (const ModelVolume* volume : destination.objects[object_index]->volumes)
            CHECK(volume->get_object() == destination.objects[object_index]);
    }
}

TEST_CASE("Preparing a volume-structure move rejects null destination ownership", "[ModelRepair][Transaction]")
{
    Model        destination        = make_model(make_cube(10., 10., 10.));
    Model        source             = Model(destination);
    ModelObject* destination_object = destination.objects.front();
    ModelVolume* destination_volume = destination_object->volumes.front();

    destination_object->volumes.front() = nullptr;
    CHECK_THROWS(destination.prepare_move_object_volume_structures(source, {0}));
    destination_object->volumes.front() = destination_volume;

    REQUIRE(destination_object->volumes.size() == 1);
    CHECK(destination_object->volumes.front() == destination_volume);
    CHECK(destination_volume->get_object() == destination_object);
}

TEST_CASE("GUI-style staged repair needs only one isolated model copy", "[ModelRepair][Transaction]")
{
    Model                                     original      = make_model(cube_with_open_side());
    Model                                     staging       = Model(original);
    const std::shared_ptr<const TriangleMesh> original_mesh = original.objects.front()->volumes.front()->mesh_ptr();

    ModelRepairReport report;
    REQUIRE(repair_staged_model(staging, report));

    CHECK(report.success);
    CHECK(report.committed);
    CHECK(original.objects.front()->volumes.front()->mesh_ptr() == original_mesh);
    CHECK(its_num_open_edges(original.objects.front()->volumes.front()->mesh().its) > 0);
    CHECK(staging.objects.front()->volumes.front()->mesh_ptr() != original_mesh);
    CHECK(its_num_open_edges(staging.objects.front()->volumes.front()->mesh().its) == 0);
}

TEST_CASE("CGAL repair fails closed for overlapping self-intersecting shells", "[ModelRepair][CGAL][SelfIntersection]")
{
    TriangleMesh mesh = overlapping_cubes();
    REQUIRE(MeshBoolean::cgal::does_self_intersect(mesh));
    const size_t original_vertices = mesh.its.vertices.size();
    const size_t original_facets   = mesh.its.indices.size();
    const double original_volume   = its_volume(mesh.its);

    std::string error;
    CHECK_FALSE(MeshBoolean::cgal::repair(mesh, nullptr, &error));

    CHECK(error.find("self-intersects") != std::string::npos);
    CHECK(MeshBoolean::cgal::does_self_intersect(mesh));
    CHECK(mesh.its.vertices.size() == original_vertices);
    CHECK(mesh.its.indices.size() == original_facets);
    CHECK(its_volume(mesh.its) == Approx(original_volume));
}

TEST_CASE("Intersecting vertex-split soup is rejected without experimental repair or mutation",
          "[ModelRepair][MeshBoolean][CGAL][SoupOrientation]")
{
    TriangleMesh               mesh   = tetrahedra_sharing_an_edge();
    const indexed_triangle_set before = mesh.its;
    std::string                error;
    REQUIRE_FALSE(MeshBoolean::cgal::repair(mesh, nullptr, &error));
    INFO(error);
    // Require the explicit unsupported-case result, not an exception from an
    // experimental repair attempt or the old orientation failure message.
    CHECK(error == "Repair failed: this model has complex intersecting surfaces that cannot be safely repaired automatically.");
    CHECK(mesh.its.vertices == before.vertices);
    CHECK(mesh.its.indices == before.indices);
}

TEST_CASE("Vertex-split soup failure preserves model painting and ownership", "[ModelRepair][CGAL][SoupOrientation][Painting]")
{
    Model        model  = make_model(tetrahedra_sharing_an_edge());
    ModelVolume* volume = model.objects.front()->volumes.front();
    volume->mmu_segmentation_facets.set_triangle_from_string(0, "1");
    volume->supported_facets.set_triangle_from_string(0, "1");
    const Model        before(model);
    const auto         original_mesh = volume->mesh_ptr();
    ModelRepairOptions options;
    options.split_components     = false;
    options.preserve_annotations = true;

    ModelRepairReport report;
    REQUIRE_FALSE(repair_model(model, report, options));
    CHECK_FALSE(report.committed);
    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures.front().message ==
          "Repair failed: this model has complex intersecting surfaces that cannot be safely repaired automatically.");
    CHECK(model.objects.front()->volumes.front() == volume);
    CHECK(volume->mesh_ptr() == original_mesh);
    CHECK(volume->mmu_segmentation_facets.equals(before.objects.front()->volumes.front()->mmu_segmentation_facets));
    CHECK(volume->supported_facets.equals(before.objects.front()->volumes.front()->supported_facets));
}

TEST_CASE("Soup orientation preserves separated shells and their geometry", "[ModelRepair][MeshBoolean][CGAL][SoupOrientation]")
{
    // Orientation-only cleanup must still work without vertex splitting, and
    // must not union away a separate shell. Color mapping is covered separately.
    TriangleMesh mesh = two_disjoint_shells(false);
    std::swap(mesh.its.indices.front()[1], mesh.its.indices.front()[2]);
    const auto   before_bbox  = mesh.bounding_box();
    const size_t before_faces = mesh.its.indices.size();
    std::string  error;
    REQUIRE(MeshBoolean::cgal::repair(mesh, nullptr, &error));
    INFO(error);
    CHECK(error.empty());
    CHECK(mesh.its.indices.size() == before_faces);
    CHECK(its_volume(mesh.its) == Approx(8125.));
    CHECK(mesh.bounding_box().min == before_bbox.min);
    CHECK(mesh.bounding_box().max == before_bbox.max);
    CHECK(its_num_open_edges(mesh.its) == 0);
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(mesh));
}

TEST_CASE("Headless model repair clears face annotations invalidated by topology changes", "[ModelRepair][CGAL]")
{
    Model        model  = make_model(cube_with_open_side());
    ModelVolume* volume = model.objects.front()->volumes.front();
    volume->supported_facets.set_triangle_from_string(0, "1");
    volume->seam_facets.set_triangle_from_string(0, "1");
    volume->mmu_segmentation_facets.set_triangle_from_string(0, "1");
    volume->fuzzy_skin_facets.set_triangle_from_string(0, "1");

    volume->exterior_facets.set_triangle_from_string(0, "1");

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    volume = model.objects.front()->volumes.front();
    REQUIRE(report.volume_reports.size() == 1);
    CHECK(report.volume_reports.front().annotations_cleared);
    CHECK(volume->supported_facets.empty());
    CHECK(volume->seam_facets.empty());
    CHECK(volume->mmu_segmentation_facets.empty());
    CHECK(volume->fuzzy_skin_facets.empty());
    CHECK(volume->exterior_facets.empty());
}

TEST_CASE("Headless model repair remaps supported painting when requested", "[ModelRepair][CGAL][Painting]")
{
    Model        model  = make_model(cube_with_open_side());
    ModelVolume* volume = model.objects.front()->volumes.front();
    paint_facet(volume->supported_facets, volume->mesh(), 0, EnforcerBlockerType::ENFORCER);
    paint_facet(volume->seam_facets, volume->mesh(), 1, EnforcerBlockerType::BLOCKER);
    paint_facet(volume->mmu_segmentation_facets, volume->mesh(), 2, EnforcerBlockerType::Extruder3);
    paint_facet(volume->fuzzy_skin_facets, volume->mesh(), 3, EnforcerBlockerType::FUZZY_SKIN);

    ModelRepairOptions options;
    options.allow_annotation_clearing = false;
    options.preserve_annotations      = true;

    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));

    volume = model.objects.front()->volumes.front();
    REQUIRE(report.volume_reports.size() == 1);
    CHECK(report.volume_reports.front().annotations_preserved);
    CHECK_FALSE(report.volume_reports.front().annotations_cleared);
    CHECK_FALSE(volume->supported_facets.empty());
    CHECK_FALSE(volume->seam_facets.empty());
    CHECK_FALSE(volume->mmu_segmentation_facets.empty());
    CHECK_FALSE(volume->fuzzy_skin_facets.empty());
}

TEST_CASE("Repair hole faces without source painting use the part base color", "[ModelRepair][CGAL][Painting][HoleColor]")
{
    indexed_triangle_set input;
    input.vertices      = {Vec3f(0, 0, 0), Vec3f(10, 0, 0), Vec3f(0, 10, 0), Vec3f(0, 0, 10)};
    input.indices       = {Vec3i32(0, 2, 1), Vec3i32(0, 1, 3), Vec3i32(0, 3, 2)};
    Model        model  = make_model(TriangleMesh(std::move(input)));
    ModelVolume* volume = model.objects.front()->volumes.front();
    volume->config.set("extruder", 2);
    paint_facet(volume->mmu_segmentation_facets, volume->mesh(), 0, EnforcerBlockerType::Extruder3);

    const auto&  vertices          = volume->mesh().its.vertices;
    const Vec3f  origin            = vertices[1];
    const Vec3f  cross             = (vertices[2] - origin).cross(vertices[3] - origin);
    const double expected_cap_area = 0.5 * double(cross.norm());
    const Vec3f  normal            = cross.normalized();

    ModelRepairOptions options;
    options.preserve_annotations      = true;
    options.allow_annotation_clearing = false;
    options.remove_non_3d_parts       = false;
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    REQUIRE(report.committed);
    REQUIRE(report.after_open_edges == 0);
    REQUIRE_FALSE(report.volume_reports.front().annotations_cleared);
    volume = model.objects.front()->volumes.front();
    CHECK(volume->extruder_id() == 2);

    std::vector<indexed_triangle_set> groups;
    volume->mmu_segmentation_facets.get_facets(*volume, groups);
    REQUIRE(groups.size() > 3);
    CHECK_FALSE(groups[3].indices.empty()); // The original secondary color survives.
    double cap_area = 0.0;
    for (size_t state = 0; state < groups.size(); ++state) {
        const auto& group = groups[state];
        for (const auto& face : group.indices) {
            const Vec3f& a = group.vertices[face[0]];
            const Vec3f& b = group.vertices[face[1]];
            const Vec3f& c = group.vertices[face[2]];
            if (std::abs(normal.dot(a - origin)) < 1e-4f && std::abs(normal.dot(b - origin)) < 1e-4f &&
                std::abs(normal.dot(c - origin)) < 1e-4f) {
                // State 0 is exported using volume->extruder_id(), not palette slot 1.
                CHECK(state == 0);
                cap_area += 0.5 * double((b - a).cross(c - a).norm());
            }
        }
    }
    CHECK(cap_area == Approx(expected_cap_area).margin(1e-3));
}

TEST_CASE("Headless model repair preserves painting when CGAL reverses an inward shell", "[ModelRepair][CGAL][Painting][Orientation]")
{
    TriangleMesh mesh = cube_with_open_side();
    mesh.flip_triangles();
    Model        model  = make_model(std::move(mesh));
    ModelVolume* volume = model.objects.front()->volumes.front();
    paint_facet(volume->supported_facets, volume->mesh(), 2, EnforcerBlockerType::ENFORCER);

    ModelRepairOptions options;
    options.allow_annotation_clearing = false;
    options.preserve_annotations      = true;
    options.split_components          = false;

    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));

    volume = model.objects.front()->volumes.front();
    REQUIRE(report.volume_reports.size() == 1);
    CHECK(report.volume_reports.front().annotations_preserved);
    CHECK_FALSE(report.volume_reports.front().annotations_cleared);
    CHECK(volume->supported_facets.has_facets(*volume, EnforcerBlockerType::ENFORCER));
    CHECK(its_volume(volume->mesh().its) > 0.);
}

TEST_CASE("Headless model repair handles an empty painting remap according to annotation policy", "[ModelRepair][Painting][Hook]")
{
    Model        model  = make_model(cube_with_open_side());
    ModelVolume* volume = model.objects.front()->volumes.front();
    paint_facet(volume->supported_facets, volume->mesh(), 2, EnforcerBlockerType::ENFORCER);
    const std::shared_ptr<const TriangleMesh> original_mesh = volume->mesh_ptr();

    ModelRepairOptions options;
    options.preserve_annotations = true;
    options.split_components     = false;
    options.repair_mesh          = [](TriangleMesh& mesh, std::string&) {
        mesh = make_cube(10., 10., 10.);
        mesh.translate(100.f, 0.f, 0.f);
        return true;
    };

    SECTION("strict preservation rolls the transaction back")
    {
        options.allow_annotation_clearing = false;

        ModelRepairReport report;
        CHECK_FALSE(repair_model(model, report, options));

        CHECK_FALSE(report.success);
        CHECK_FALSE(report.committed);
        REQUIRE_FALSE(report.failures.empty());
        CHECK(report.failures.front().message.find("painted facet annotations could not be mapped") != std::string::npos);
        REQUIRE(model.objects.front()->volumes.front()->mesh_ptr() == original_mesh);
        CHECK_FALSE(model.objects.front()->volumes.front()->supported_facets.empty());
    }

    SECTION("permitted clearing is explicit in the report")
    {
        options.allow_annotation_clearing = true;

        ModelRepairReport report;
        REQUIRE(repair_model(model, report, options));

        volume = model.objects.front()->volumes.front();
        REQUIRE(report.volume_reports.size() == 1);
        CHECK(report.volume_reports.front().annotations_cleared);
        CHECK_FALSE(report.volume_reports.front().annotations_preserved);
        CHECK(volume->supported_facets.empty());
    }
}

TEST_CASE("GUI repair policy rejects lost painting even with partial geometry enabled", "[ModelRepair][GUIColor][Painting][Partial]")
{
    Model        model  = make_model(cube_with_open_side());
    ModelVolume* volume = model.objects.front()->volumes.front();
    paint_facet(volume->mmu_segmentation_facets, volume->mesh(), 2, EnforcerBlockerType::Extruder1);
    const auto source_mesh      = volume->mesh_ptr();
    const auto source_id        = volume->id();
    const auto source_painting  = volume->mmu_segmentation_facets.get_data();
    const auto source_timestamp = volume->mmu_segmentation_facets.timestamp();
    ModelRepairOptions options = gui_model_repair_options();
    REQUIRE(options.allow_partial);
    REQUIRE(options.preserve_annotations);
    REQUIRE_FALSE(options.allow_annotation_clearing);
    // A geometrically valid but displaced candidate cannot retain the source
    // paint. It must not become an uncolored "repaired" or partial result.
    options.repair_mesh = [](TriangleMesh& mesh, std::string&) {
        mesh = make_cube(10., 10., 10.);
        mesh.translate(100.f, 0.f, 0.f);
        return true;
    };
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK_FALSE(report.can_continue());
    REQUIRE_FALSE(report.failures.empty());
    CHECK(report.failures.front().message.find("painted facet annotations could not be mapped") != std::string::npos);
    CHECK(model.objects.front()->volumes.front() == volume);
    CHECK(volume->mesh_ptr() == source_mesh);
    CHECK(volume->id() == source_id);
    CHECK(volume->mmu_segmentation_facets.get_data() == source_painting);
    CHECK(volume->mmu_segmentation_facets.timestamp() == source_timestamp);
}

TEST_CASE("Headless model repair rejects a partial painting remap", "[ModelRepair][Painting][Hook][Coverage]")
{
    Model                             model      = make_model(two_disjoint_shells(false));
    ModelVolume*                      volume     = model.objects.front()->volumes.front();
    std::vector<indexed_triangle_set> components = its_split(volume->mesh().its);
    REQUIRE(components.size() == 2);
    std::sort(components.begin(), components.end(), [](const indexed_triangle_set& lhs, const indexed_triangle_set& rhs) {
        return Slic3r::bounding_box(lhs).min.x() < Slic3r::bounding_box(rhs).min.x();
    });
    const BoundingBoxf3 first_bounds          = Slic3r::bounding_box(components[0]);
    const BoundingBoxf3 second_bounds         = Slic3r::bounding_box(components[1]);
    const float         component_separator_x = float((first_bounds.max.x() + second_bounds.min.x()) * 0.5);
    const TriangleMesh  retained_component(components[0]);

    TriangleSelector selector(volume->mesh());
    selector.set_facet(0, EnforcerBlockerType::ENFORCER);
    const auto second_component_face = std::find_if(volume->mesh().its.indices.begin(), volume->mesh().its.indices.end(),
                                                    [&](const stl_triangle_vertex_indices& face) {
                                                        return (volume->mesh().its.vertices[size_t(face[0])].x() +
                                                                volume->mesh().its.vertices[size_t(face[1])].x() +
                                                                volume->mesh().its.vertices[size_t(face[2])].x()) /
                                                                   3.f >
                                                               component_separator_x;
                                                    });
    REQUIRE(second_component_face != volume->mesh().its.indices.end());
    selector.set_facet(static_cast<int>(second_component_face - volume->mesh().its.indices.begin()), EnforcerBlockerType::ENFORCER);
    REQUIRE(volume->supported_facets.set(selector));
    const indexed_triangle_set painted_source = volume->supported_facets.get_facets_strict(*volume, EnforcerBlockerType::ENFORCER);
    REQUIRE(painted_source.indices.size() == 2);
    CHECK(Slic3r::bounding_box(painted_source).max.x() >= second_bounds.min.x());
    const std::shared_ptr<const TriangleMesh> original_mesh = volume->mesh_ptr();
    const std::thread::id                     caller_thread = std::this_thread::get_id();
    std::vector<std::thread::id>              painting_validation_threads;

    size_t             inspection_calls = 0;
    ModelRepairOptions options;
    options.preserve_annotations      = true;
    options.allow_annotation_clearing = false;
    options.split_components          = false;
    options.mesh_requires_repair      = [&](const TriangleMesh&) {
        // The closed source is inspected once while planning and once as the
        // selected part. Force repair for both; the third call validates the
        // single remaining cube.
        return inspection_calls++ < 2;
    };
    options.repair_mesh = [retained_component](TriangleMesh& mesh, std::string&) {
        mesh = retained_component;
        return true;
    };
    options.run_painting_validation = [&](const std::function<bool()>& validation) {
        bool            restored = false;
        std::thread::id worker_thread;
        std::thread     worker([&]() {
            worker_thread = std::this_thread::get_id();
            restored      = validation();
        });
        worker.join();
        painting_validation_threads.push_back(worker_thread);
        return restored;
    };

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    CHECK(inspection_calls == 3);
    REQUIRE(painting_validation_threads.size() == 2);
    for (const std::thread::id validation_thread : painting_validation_threads)
        CHECK(validation_thread != caller_thread);
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    REQUIRE_FALSE(report.failures.empty());
    CHECK(report.failures.front().message.find("painted facet annotations could not be mapped") != std::string::npos);
    CHECK(model.objects.front()->volumes.front()->mesh_ptr() == original_mesh);
    CHECK(model.objects.front()->volumes.front()->supported_facets.has_facets(*model.objects.front()->volumes.front(),
                                                                              EnforcerBlockerType::ENFORCER));
}

TEST_CASE("Headless model repair ignores non-model-part volumes", "[ModelRepair][CGAL]")
{
    Model                                     model         = make_model(cube_with_open_side());
    ModelVolume*                              volume        = model.objects.front()->volumes.front();
    const std::shared_ptr<const TriangleMesh> original_mesh = volume->mesh_ptr();
    volume->set_type(ModelVolumeType::PARAMETER_MODIFIER);

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    CHECK_FALSE(report.attempted);
    CHECK_FALSE(report.committed);
    CHECK(report.objects == 0);
    CHECK(report.volumes == 0);
    CHECK(report.status() == std::string("not_needed"));
    CHECK(volume->mesh_ptr() == original_mesh);
    CHECK(its_num_open_edges(volume->mesh().its) > 0);
}

TEST_CASE("Headless model repair leaves a closed volume untouched", "[ModelRepair][CGAL]")
{
    Model                                     model         = make_model(make_cube(10., 10., 10.));
    const std::shared_ptr<const TriangleMesh> original_mesh = model.objects.front()->volumes.front()->mesh_ptr();

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    CHECK(report.success);
    CHECK_FALSE(report.attempted);
    CHECK_FALSE(report.committed);
    CHECK(report.repaired == 0);
    CHECK(report.skipped == 1);
    CHECK(report.status() == std::string("not_needed"));
    CHECK(model.objects.front()->volumes.front()->mesh_ptr() == original_mesh);
}

TEST_CASE("Closed-volume inspection hook skips mesh repair for a healthy model", "[ModelRepair][CGAL][Hook]")
{
    Model                                     model            = make_model(make_cube(10., 10., 10.));
    const std::shared_ptr<const TriangleMesh> original_mesh    = model.objects.front()->volumes.front()->mesh_ptr();
    size_t                                    inspection_calls = 0;
    bool                                      inspected_closed = false;
    bool                                      repair_called    = false;

    ModelRepairOptions options;
    options.mesh_requires_repair = [&](const TriangleMesh& mesh) {
        ++inspection_calls;
        inspected_closed = its_num_open_edges(mesh.its) == 0;
        return MeshBoolean::cgal::requires_repair(mesh);
    };
    options.repair_mesh = [&](TriangleMesh&, std::string&) {
        repair_called = true;
        return true;
    };

    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));

    CHECK(inspection_calls == 1);
    CHECK(inspected_closed);
    CHECK_FALSE(repair_called);
    CHECK(report.success);
    CHECK_FALSE(report.attempted);
    CHECK_FALSE(report.committed);
    CHECK(report.repaired == 0);
    CHECK(report.skipped == 1);
    CHECK(report.status() == std::string("not_needed"));
    CHECK(model.objects.front()->volumes.front()->mesh_ptr() == original_mesh);
}

TEST_CASE("Closed-volume inspection failure rolls back without invoking repair", "[ModelRepair][CGAL][Hook][Transaction]")
{
    Model                                     model         = make_model(make_cube(10., 10., 10.));
    ModelObject*                              object        = model.objects.front();
    ModelVolume*                              volume        = object->volumes.front();
    const std::shared_ptr<const TriangleMesh> original_mesh = volume->mesh_ptr();
    bool                                      repair_called = false;

    ModelRepairOptions options;
    options.mesh_requires_repair = [](const TriangleMesh&) -> bool { throw std::runtime_error("inspection failed"); };
    options.repair_mesh          = [&](TriangleMesh&, std::string&) {
        repair_called = true;
        return true;
    };

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    CHECK_FALSE(repair_called);
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.attempted);
    CHECK(report.failed == 1);
    CHECK(report.status() == std::string("failed"));
    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures.front().message.find("inspection failed") != std::string::npos);
    REQUIRE(model.objects.front() == object);
    REQUIRE(object->volumes.front() == volume);
    CHECK(volume->mesh_ptr() == original_mesh);
}

TEST_CASE("Repair hooks cannot commit a mesh that fails full topology validation", "[ModelRepair][CGAL][Hook][Transaction]")
{
    Model                                     model         = make_model(cube_with_open_side());
    const std::shared_ptr<const TriangleMesh> original_mesh = model.objects.front()->volumes.front()->mesh_ptr();

    ModelRepairOptions options;
    options.split_components = false;

    SECTION("self intersection")
    {
        options.repair_mesh = [](TriangleMesh& mesh, std::string&) {
            mesh = overlapping_cubes();
            return true;
        };
    }
    SECTION("degenerate duplicate topology")
    {
        options.repair_mesh = [](TriangleMesh& mesh, std::string&) {
            mesh                                                           = make_cube(10., 10., 10.);
            const std::vector<stl_triangle_vertex_indices> duplicate_faces = mesh.its.indices;
            mesh.its.indices.insert(mesh.its.indices.end(), duplicate_faces.begin(), duplicate_faces.end());
            return true;
        };
    }
    SECTION("closed topology with a collapsed geometric edge")
    {
        options.repair_mesh = [](TriangleMesh& mesh, std::string&) {
            mesh                 = make_cube(10., 10., 10.);
            mesh.its.vertices[1] = mesh.its.vertices[0];
            return true;
        };
    }
    SECTION("incorrect volume orientation")
    {
        options.repair_mesh = [](TriangleMesh& mesh, std::string&) {
            mesh = make_cube(10., 10., 10.);
            mesh.flip_triangles();
            return true;
        };
    }

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    REQUIRE_FALSE(report.failures.empty());
    CHECK(report.failures.front().message.find("full topology") != std::string::npos);
    CHECK(model.objects.front()->volumes.front()->mesh_ptr() == original_mesh);
}

TEST_CASE("Headless model repair orients an inward open shell to positive volume", "[ModelRepair][CGAL]")
{
    TriangleMesh mesh = cube_with_open_side();
    for (stl_triangle_vertex_indices& face : mesh.its.indices)
        std::swap(face[1], face[2]);
    Model model = make_model(std::move(mesh));

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    const TriangleMesh& repaired = model.objects.front()->volumes.front()->mesh();
    CHECK(report.repaired == 1);
    CHECK(its_num_open_edges(repaired.its) == 0);
    CHECK(its_volume(repaired.its) > 0.);
}

TEST_CASE("Headless model repair corrects a closed inward shell", "[ModelRepair][CGAL]")
{
    TriangleMesh mesh = make_cube(10., 10., 10.);
    for (stl_triangle_vertex_indices& face : mesh.its.indices)
        std::swap(face[1], face[2]);
    REQUIRE(its_volume(mesh.its) < 0.);
    Model model = make_model(std::move(mesh));

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    const TriangleMesh& repaired = model.objects.front()->volumes.front()->mesh();
    CHECK(report.attempted);
    CHECK(report.repaired == 1);
    CHECK(its_num_open_edges(repaired.its) == 0);
    CHECK(its_volume(repaired.its) > 0.);
}

TEST_CASE("CGAL repair corrects an inward nested cavity without failing fidelity validation", "[ModelRepair][MeshBoolean][CGAL][Cavity]")
{
    TriangleMesh outer = make_cube(20., 20., 20.);
    TriangleMesh inner = make_cube(5., 5., 5.);
    inner.translate(7.5f, 7.5f, 7.5f);
    REQUIRE(its_volume(outer.its) > 0.);
    REQUIRE(its_volume(inner.its) > 0.);
    outer.merge(inner);

    const size_t before_facets      = outer.its.indices.size();
    const double before_shell_volume = absolute_shell_volume(outer);
    REQUIRE(MeshBoolean::cgal::requires_repair(outer));

    std::string error;
    const bool repair_succeeded = MeshBoolean::cgal::repair(outer, nullptr, &error);
    INFO(error);
    REQUIRE(repair_succeeded);

    CHECK(outer.its.indices.size() == before_facets);
    CHECK(absolute_shell_volume(outer) == Approx(before_shell_volume).epsilon(1.e-6));
    const std::vector<indexed_triangle_set> shells = its_split(outer.its);
    REQUIRE(shells.size() == 2);
    CHECK(std::count_if(shells.begin(), shells.end(), [](const indexed_triangle_set& shell) { return its_volume(shell) < 0.; }) == 1);
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(outer));
}

TEST_CASE("Headless model repair removes a degenerate facet before volume diagnosis", "[ModelRepair][CGAL]")
{
    TriangleMesh mesh = make_cube(10., 10., 10.);
    mesh.its.indices.emplace_back(0, 0, 1);
    Model model = make_model(std::move(mesh));

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    const TriangleMesh& repaired = model.objects.front()->volumes.front()->mesh();
    CHECK(report.attempted);
    CHECK(report.repaired == 1);
    CHECK(repaired.its.indices.size() == 12);
    CHECK(its_num_open_edges(repaired.its) == 0);
    CHECK(its_volume(repaired.its) > 0.);
}

TEST_CASE("Headless model repair corrects a locally reversed closed face", "[ModelRepair][CGAL]")
{
    TriangleMesh mesh = make_cube(10., 10., 10.);
    std::swap(mesh.its.indices.front()[1], mesh.its.indices.front()[2]);
    REQUIRE(its_num_open_edges(mesh.its) > 0);
    Model model = make_model(std::move(mesh));

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    const TriangleMesh& repaired = model.objects.front()->volumes.front()->mesh();
    CHECK(report.attempted);
    CHECK(report.repaired == 1);
    CHECK(its_num_open_edges(repaired.its) == 0);
    CHECK(its_volume(repaired.its) > 0.);
}

TEST_CASE("Headless model repair does not delete a 3D shell whose signed volume cancels", "[ModelRepair][CGAL]")
{
    TriangleMesh mesh = cube_with_canceling_signed_volume();
    REQUIRE(its_num_open_edges(mesh.its) > 0);
    REQUIRE(its_volume(mesh.its) == Approx(0.).margin(EPSILON));
    Model model = make_model(std::move(mesh));

    ModelRepairOptions options;
    options.split_components = false;

    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));

    REQUIRE(model.objects.front()->volumes.size() == 1);
    const TriangleMesh& repaired = model.objects.front()->volumes.front()->mesh();
    CHECK(report.committed);
    CHECK(report.repaired == 1);
    CHECK(report.mesh_repaired == 1);
    CHECK(report.removed_parts == 0);
    CHECK(its_num_open_edges(repaired.its) == 0);
    CHECK(its_volume(repaired.its) > 0.);
}

TEST_CASE("Headless model repair ignores unreferenced vertices when diagnosing a 3D part", "[ModelRepair][CGAL]")
{
    TriangleMesh         mesh            = cube_with_unreferenced_far_vertex();
    indexed_triangle_set diagnostic_mesh = mesh.its;
    its_compactify_vertices(diagnostic_mesh, true);
    REQUIRE(diagnostic_mesh.vertices.size() == 8);
    REQUIRE(Slic3r::bounding_box(diagnostic_mesh).max.x() < 100.);
    const Vec3d diagnostic_reference = diagnostic_mesh.vertices[size_t(diagnostic_mesh.indices.front()[0])].cast<double>();
    double      diagnostic_volume    = 0.;
    for (const stl_triangle_vertex_indices& face : diagnostic_mesh.indices) {
        const Vec3d a = diagnostic_mesh.vertices[size_t(face[0])].cast<double>() - diagnostic_reference;
        const Vec3d b = diagnostic_mesh.vertices[size_t(face[1])].cast<double>() - diagnostic_reference;
        const Vec3d c = diagnostic_mesh.vertices[size_t(face[2])].cast<double>() - diagnostic_reference;
        diagnostic_volume += std::abs(a.dot(b.cross(c))) / 6.;
    }
    REQUIRE(diagnostic_volume > EPSILON);

    Model model = make_model(std::move(mesh));
    REQUIRE(model.objects.front()->volumes.front()->mesh().bounding_box().max.x() > 1.e5);
    REQUIRE_FALSE(model.objects.front()->volumes.front()->is_splittable());

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    REQUIRE(model.objects.front()->volumes.size() == 1);
    const TriangleMesh& retained = model.objects.front()->volumes.front()->mesh();
    CHECK(report.committed);
    CHECK(report.mesh_repaired == 1);
    CHECK(report.removed_parts == 0);
    CHECK(retained.its.vertices.size() == 8);
    CHECK(retained.its.indices.size() == 12);
    CHECK(std::abs(its_volume(retained.its)) > 0.);
}

TEST_CASE("Headless model repair ignores a collinear far face when diagnosing a 3D part", "[ModelRepair][CGAL]")
{
    Model model = make_model(cube_with_collinear_far_face());

    ModelRepairOptions options;
    options.split_components = false;

    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));

    REQUIRE(model.objects.front()->volumes.size() == 1);
    const TriangleMesh& repaired = model.objects.front()->volumes.front()->mesh();
    CHECK(report.committed);
    CHECK(report.mesh_repaired == 1);
    CHECK(report.removed_parts == 0);
    CHECK(repaired.its.vertices.size() == 8);
    CHECK(repaired.its.indices.size() == 12);
    CHECK(its_num_open_edges(repaired.its) == 0);
    CHECK(std::abs(its_volume(repaired.its)) > 0.);
}

TEST_CASE("Headless model repair removes duplicated closed facets", "[ModelRepair][CGAL]")
{
    TriangleMesh                                   mesh       = make_cube(10., 10., 10.);
    const std::vector<stl_triangle_vertex_indices> duplicates = mesh.its.indices;
    mesh.its.indices.insert(mesh.its.indices.end(), duplicates.begin(), duplicates.end());
    REQUIRE(its_num_open_edges(mesh.its) == 0);
    Model model = make_model(std::move(mesh));

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    const TriangleMesh& repaired = model.objects.front()->volumes.front()->mesh();
    CHECK(report.attempted);
    CHECK(report.repaired == 1);
    CHECK(repaired.its.indices.size() == 12);
    CHECK(its_num_open_edges(repaired.its) == 0);
    CHECK(its_volume(repaired.its) > 0.);
}

TEST_CASE("Headless model repair splits closed disjoint components into volumes", "[ModelRepair][CGAL][Split]")
{
    Model model = make_model(two_disjoint_shells(false));

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    REQUIRE(model.objects.front()->volumes.size() == 2);
    CHECK(report.success);
    CHECK(report.attempted);
    CHECK(report.committed);
    CHECK(report.repaired == 1);
    CHECK(report.mesh_repaired == 0);
    CHECK(report.split_volumes == 1);
    CHECK(report.created_parts == 1);
    CHECK(report.removed_parts == 0);
    CHECK(report.changed_object_indices == std::vector<size_t>{0});
    REQUIRE(report.volume_reports.size() == 1);
    CHECK(report.volume_reports.front().repaired_parts == 0);
    CHECK(report.volume_reports.front().split_parts == 2);
    CHECK(report.volume_reports.front().removed_parts == 0);
    for (const ModelVolume* volume : model.objects.front()->volumes) {
        CHECK(its_num_open_edges(volume->mesh().its) == 0);
        CHECK(its_volume(volume->mesh().its) > 0.);
    }
}

TEST_CASE("Headless model repair normalizes orientation while splitting disjoint shells", "[ModelRepair][CGAL][Split]")
{
    Model model = make_model(two_disjoint_shells(true));

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    REQUIRE(model.objects.front()->volumes.size() == 2);
    CHECK(report.attempted);
    CHECK(report.repaired == 1);
    CHECK(report.mesh_repaired == 0);
    CHECK(report.split_volumes == 1);
    REQUIRE(report.volume_reports.size() == 1);
    CHECK(report.volume_reports.front().repaired_parts == 0);
    CHECK(report.volume_reports.front().split_parts == 2);
    for (const ModelVolume* volume : model.objects.front()->volumes) {
        CHECK(its_num_open_edges(volume->mesh().its) == 0);
        CHECK(its_volume(volume->mesh().its) > 0.);
    }
}

TEST_CASE("Headless model repair does not split intersecting disconnected shells", "[ModelRepair][CGAL][Split][SelfIntersection]")
{
    Model                                     model         = make_model(overlapping_cubes());
    const std::shared_ptr<const TriangleMesh> original_mesh = model.objects.front()->volumes.front()->mesh_ptr();

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report));

    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.split_volumes == 0);
    CHECK(report.failed == 1);
    REQUIRE(model.objects.front()->volumes.size() == 1);
    CHECK(model.objects.front()->volumes.front()->mesh_ptr() == original_mesh);
}

TEST_CASE("Headless model repair removes a planar component after splitting", "[ModelRepair][CGAL][Split]")
{
    Model model = make_model(cube_with_planar_component());

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    REQUIRE(model.objects.front()->volumes.size() == 1);
    const TriangleMesh& surviving_mesh = model.objects.front()->volumes.front()->mesh();
    CHECK(surviving_mesh.its.indices.size() == 12);
    CHECK(its_num_open_edges(surviving_mesh.its) == 0);
    CHECK(its_volume(surviving_mesh.its) > 0.);
    CHECK(report.repaired == 1);
    CHECK(report.split_volumes == 1);
    CHECK(report.created_parts == 0);
    CHECK(report.removed_parts == 1);
    CHECK(report.changed_object_indices == std::vector<size_t>{0});
    REQUIRE(report.volume_reports.size() == 1);
    CHECK(report.volume_reports.front().split_parts == 2);
    CHECK(report.volume_reports.front().removed_parts == 1);
}

TEST_CASE("Headless model repair retains a thin solid component after splitting", "[ModelRepair][CGAL][Split]")
{
    Model model = make_model(cube_with_thin_component());

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    REQUIRE(model.objects.front()->volumes.size() == 2);
    for (const ModelVolume* volume : model.objects.front()->volumes) {
        CHECK(volume->mesh().its.indices.size() == 12);
        CHECK(its_num_open_edges(volume->mesh().its) == 0);
        CHECK(its_volume(volume->mesh().its) > 0.);
    }
    CHECK(report.repaired == 1);
    CHECK(report.mesh_repaired == 0);
    CHECK(report.split_volumes == 1);
    CHECK(report.created_parts == 1);
    CHECK(report.removed_parts == 0);
    CHECK(report.changed_object_indices == std::vector<size_t>{0});
    REQUIRE(report.volume_reports.size() == 1);
    CHECK(report.volume_reports.front().repaired_parts == 0);
    CHECK(report.volume_reports.front().split_parts == 2);
    CHECK(report.volume_reports.front().removed_parts == 0);
}

TEST_CASE("Headless model repair retains small local solids under instance and volume scaling", "[ModelRepair][CGAL][Scale]")
{
    const double edge          = GENERATE(0.01, 0.00001);
    const bool   scale_volume  = GENERATE(false, true);
    Model        model         = make_model(make_cube(edge, edge, edge));
    ModelObject* object        = model.objects.front();
    ModelVolume* volume        = object->volumes.front();
    const auto   original_mesh = volume->mesh_ptr();
    if (scale_volume)
        volume->set_scaling_factor(Vec3d::Constant(10. / edge));
    else
        object->instances.front()->set_scaling_factor(Vec3d::Constant(10. / edge));
    const Vec3d before_size = object->bounding_box_exact().size();
    REQUIRE(before_size.isApprox(Vec3d::Constant(10.), 1e-5));

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));
    CHECK(report.removed_parts == 0);
    CHECK_FALSE(report.committed);
    REQUIRE(object->volumes.size() == 1);
    CHECK(object->volumes.front()->mesh_ptr() == original_mesh);
    CHECK(object->bounding_box_exact().size().isApprox(before_size));
}

TEST_CASE("Headless model repair cannot discard a solid shared by differently scaled instances", "[ModelRepair][CGAL][Scale]")
{
    Model        model  = make_model(make_cube(0.01, 0.01, 0.01));
    ModelObject* object = model.objects.front();
    object->instances.front()->set_scaling_factor(Vec3d::Constant(1000.));
    object->add_instance()->set_scaling_factor(Vec3d::Constant(0.5));
    ModelRepairReport report;
    REQUIRE(repair_model(model, report));
    CHECK(report.removed_parts == 0);
    REQUIRE(object->volumes.size() == 1);
    REQUIRE(object->instances.size() == 2);
    CHECK(object->instances[0]->get_scaling_factor().isApprox(Vec3d::Constant(1000.)));
    CHECK(object->instances[1]->get_scaling_factor().isApprox(Vec3d::Constant(0.5)));
}

TEST_CASE("Headless model repair retains an open three-dimensional fan for hole filling", "[ModelRepair][CGAL][Scale]")
{
    indexed_triangle_set fan;
    fan.vertices            = {Vec3f(0.f, 0.f, 10.f), Vec3f(0.f, 0.f, 0.f), Vec3f(10.f, 0.f, 0.f), Vec3f(0.f, 10.f, 0.f)};
    fan.indices             = {Vec3i32(0, 1, 2), Vec3i32(0, 2, 3), Vec3i32(0, 3, 1)};
    Model             model = make_model(TriangleMesh(std::move(fan)));
    ModelRepairReport report;
    REQUIRE(repair_model(model, report));
    CHECK(report.removed_parts == 0);
    CHECK(report.mesh_repaired == 1);
    REQUIRE(model.objects.front()->volumes.size() == 1);
    CHECK(its_num_open_edges(model.objects.front()->volumes.front()->mesh().its) == 0);
    CHECK(its_volume(model.objects.front()->volumes.front()->mesh().its) > 0.);
}

TEST_CASE("Headless model repair rejects cleanup that would empty an object", "[ModelRepair][CGAL][Split][Acceptance]")
{
    const bool   multiple_planes = GENERATE(false, true);
    Model        model           = make_model(planar_square());
    ModelObject* object          = model.objects.front();
    ModelVolume* source          = object->volumes.front();
    const auto   source_mesh     = source->mesh_ptr();
    const auto   source_id       = source->id();
    if (multiple_planes)
        object->add_volume(planar_square());
    const size_t plane_count = object->volumes.size();
    // Neither an unrelated solid nor a non-printable modifier may hide that
    // cleanup would discard every model part of the repaired object.
    object->add_volume(make_cube(2., 2., 2.), ModelVolumeType::PARAMETER_MODIFIER);
    ModelObject* unrelated = model.add_object();
    unrelated->add_volume(make_cube(10., 10., 10.));
    unrelated->add_instance();
    const auto unrelated_mesh = unrelated->volumes.front()->mesh_ptr();

    ModelRepairReport report;
    REQUIRE_FALSE(repair_model(model, report));
    REQUIRE(object->volumes.size() == plane_count + 1);
    CHECK(object->volumes.front() == source);
    CHECK(source->mesh_ptr() == source_mesh);
    CHECK(source->id() == source_id);
    CHECK(unrelated->volumes.front()->mesh_ptr() == unrelated_mesh);
    CHECK_FALSE(report.success);
    CHECK(report.attempted);
    CHECK_FALSE(report.committed);
    CHECK(report.repaired == 0);
    CHECK(report.mesh_repaired == 0);
    CHECK(report.removed_parts == 0);
    CHECK(report.after_facets == report.before_facets);
    CHECK(report.changed_object_indices.empty());
    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures.front().message == "Repair failed: basic cleanup removed all geometry.");
}

TEST_CASE("Headless model repair still removes a planar volume next to a solid", "[ModelRepair][CGAL][Split][Acceptance]")
{
    Model              model      = make_model(planar_square());
    ModelObject*       object     = model.objects.front();
    ModelVolume*       solid      = object->add_volume(make_cube(10., 10., 10.));
    const auto         solid_mesh = solid->mesh_ptr();
    ModelRepairOptions options;
    options.targets.push_back({0, size_t(0)});
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    REQUIRE(object->volumes.size() == 1);
    CHECK(object->volumes.front()->mesh_ptr() == solid_mesh);
    CHECK(report.committed);
    CHECK(report.removed_parts == 1);
}

TEST_CASE("Headless model repair preserves valid nested shell orientation with default splitting", "[ModelRepair][CGAL][Split]")
{
    TriangleMesh outer = make_cube(20., 20., 20.);
    TriangleMesh inner = make_cube(5., 5., 5.);
    inner.translate(7.5f, 7.5f, 7.5f);
    inner.flip_triangles();
    outer.merge(inner);
    Model model = make_model(std::move(outer));

    ModelRepairReport report;
    REQUIRE(repair_model(model, report));

    REQUIRE(model.objects.front()->volumes.size() == 1);
    CHECK_FALSE(report.attempted);
    CHECK_FALSE(report.committed);
    CHECK(report.split_volumes == 0);
    CHECK(report.skipped == 1);
    CHECK(report.status() == std::string("not_needed"));
    const std::vector<indexed_triangle_set> shells = its_split(model.objects.front()->volumes.front()->mesh().its);
    REQUIRE(shells.size() == 2);
    CHECK(std::count_if(shells.begin(), shells.end(), [](const indexed_triangle_set& shell) { return its_volume(shell) < 0.; }) == 1);
}

TEST_CASE("Headless model repair rolls back all staged meshes when one volume fails", "[ModelRepair][CGAL]")
{
    Model                                     model         = make_model(cube_with_open_side());
    ModelObject*                              object        = model.objects.front();
    const std::shared_ptr<const TriangleMesh> original_mesh = object->volumes.front()->mesh_ptr();
    add_invalid_volume(*object);

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report));

    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.failed == 1);
    CHECK(report.rolled_back == 1);
    CHECK(report.repaired == 0);
    CHECK(report.status() == std::string("failed"));
    CHECK(object->volumes.front()->mesh_ptr() == original_mesh);
    CHECK(its_num_open_edges(object->volumes.front()->mesh().its) > 0);
}

TEST_CASE("Partial repair retains only independently prepared basic geometry", "[ModelRepair][Partial]")
{
    indexed_triangle_set input          = tetrahedra_sharing_an_edge().its;
    const auto           expected_faces = input.indices;
    input.indices.push_back(input.indices.front());
    input.indices.emplace_back(0, 0, 1);
    Model      model         = make_model(TriangleMesh(std::move(input)));
    const auto original_mesh = model.objects.front()->volumes.front()->mesh_ptr();
    // add_volume() centers the mesh and records its position in the volume
    // transform. Compare in that same local frame, and check the transform too.
    const auto         expected_vertices  = original_mesh->its.vertices;
    const auto         expected_transform = model.objects.front()->volumes.front()->get_transformation();
    const auto         expected_instance  = model.objects.front()->instances.front()->get_transformation();
    ModelRepairOptions options;
    options.allow_partial    = true;
    options.split_components = false;
    options.repair_mesh      = [](TriangleMesh& candidate, std::string& error) {
        candidate = make_cube(99., 99., 99.); // Must NEVER be retained on failure.
        error     = "Injected geometric rejection.";
        return false;
    };
    const bool        staged = GENERATE(false, true);
    ModelRepairReport report;
    REQUIRE((staged ? repair_staged_model(model, report, options) : repair_model(model, report, options)));
    CHECK(report.status() == std::string("partially_repaired"));
    CHECK(report.success);
    CHECK(report.committed);
    CHECK(report.can_continue());
    CHECK_FALSE(report.mesh_validated());
    CHECK(report.unresolved == 1);
    CHECK(report.failed == 0);
    CHECK(report.basic_removed_faces == 2);
    CHECK(report.mesh_repaired == 0);
    REQUIRE(report.volume_reports.size() == 1);
    CHECK(report.volume_reports.front().result == ModelRepairVolumeStatus::PartiallyRepaired);
    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures.front().recoverable);
    const TriangleMesh& result = model.objects.front()->volumes.front()->mesh();
    CHECK(result.its.indices == expected_faces);
    CHECK(result.its.vertices == expected_vertices);
    CHECK(model.objects.front()->volumes.front()->get_transformation() == expected_transform);
    CHECK(model.objects.front()->instances.front()->get_transformation() == expected_instance);
    CHECK(original_mesh->its.indices.size() == expected_faces.size() + 2);
}

TEST_CASE("Partial repair groups touching patches apart from a repairable shell", "[ModelRepair][Partial][CGAL][Split]")
{
    const bool bad_first = GENERATE(false, true);
    const int  painting  = GENERATE(0, 1, 2); // None, whole facet, subdivided facet on the retained group.
    const bool painted   = painting != 0;
    CAPTURE(bad_first, painting);
    TriangleMesh bad = tetrahedra_sharing_an_edge();
    bad.translate(50.f, 0.f, 0.f);
    TriangleMesh input = bad_first ? bad : cube_with_open_side();
    input.merge(bad_first ? cube_with_open_side() : bad);
    Model model  = make_model(std::move(input));
    auto* source = model.objects.front()->volumes.front();
    source->config.set("extruder", 3);
    source->set_rotation(Vec3d(0.2, 0.3, 0.4));
    source->set_scaling_factor(Vec3d(1.5, 0.75, 2.0));
    const auto   transform       = source->get_transformation();
    const auto   instance        = model.objects.front()->instances.front()->get_transformation();
    const auto   original        = source->mesh_ptr();
    auto*        unselected      = model.objects.front()->add_volume(make_cube(1., 1., 1.), ModelVolumeType::NEGATIVE_VOLUME);
    const auto   unselected_mesh = unselected->mesh_ptr();
    const auto   unselected_id   = unselected->id();
    const size_t bad_offset      = bad_first ? 0 : 10;
    if (painted) {
        paint_facet(source->supported_facets, source->mesh(), int(bad_offset), EnforcerBlockerType::ENFORCER);
        paint_facet(source->mmu_segmentation_facets, source->mesh(), bad_first ? 8 : 0, EnforcerBlockerType::ENFORCER);
        paint_facet(source->seam_facets, source->mesh(), int(bad_offset + 1), EnforcerBlockerType::BLOCKER);
        paint_facet(source->fuzzy_skin_facets, source->mesh(), int(bad_offset + 2), EnforcerBlockerType::FUZZY_SKIN);
        if (painting == 2) {
            source->supported_facets.reset();
            // One split side, then ENFORCER/NONE children. Use the existing
            // 3MF per-facet encoding so the subtree must survive verbatim.
            source->supported_facets.set_triangle_from_string(int(bad_offset), "041");
        }
    }
    std::array<std::array<std::string, 8>, 3> expected_annotations;
    for (size_t face = 0; face < 8; ++face) {
        expected_annotations[0][face] = source->supported_facets.get_triangle_as_string(int(bad_offset + face));
        expected_annotations[1][face] = source->seam_facets.get_triangle_as_string(int(bad_offset + face));
        expected_annotations[2][face] = source->fuzzy_skin_facets.get_triangle_as_string(int(bad_offset + face));
    }
    auto expected_mmu = source->mmu_segmentation_facets.get_facets_strict(*source, EnforcerBlockerType::ENFORCER);
    // Strict extraction returns selected triangles but retains live vertices
    // from other states. Bounds must describe only the painted surface, not
    // the whole original volume (including the distant unresolved group).
    its_compactify_vertices(expected_mmu);
    ModelRepairOptions options;
    options.allow_partial             = true;
    options.preserve_annotations      = painted;
    options.allow_annotation_clearing = false;
    options.remove_non_3d_parts       = false;
    ModelRepairReport report;
    const bool        repaired_ok = repair_model(model, report, options);
    INFO((report.failures.empty() ? "No reported failure" : report.failures.front().message));
    REQUIRE(repaired_ok);
    REQUIRE(model.objects.front()->volumes.size() == 3);
    // The object-volume transaction installs staging volume wrappers; stable
    // identity and immutable mesh ownership, not raw wrapper addresses, persist.
    CHECK(model.objects.front()->volumes.back()->id() == unselected_id);
    CHECK(model.objects.front()->volumes.back()->mesh_ptr() == unselected_mesh);
    CHECK(report.status() == std::string("partially_repaired"));
    CHECK(report.committed);
    CHECK_FALSE(report.mesh_validated());
    CHECK(report.unresolved == 1);
    CHECK(report.failed == 0);
    CHECK(report.mesh_repaired == 1);
    CHECK(report.split_volumes == 1);
    CHECK(report.created_parts == 1);
    REQUIRE(report.volume_reports.size() == 1);
    CHECK(report.volume_reports.front().annotations_preserved == painted);
    CHECK_FALSE(report.volume_reports.front().annotations_cleared);
    CHECK(model.objects.front()->instances.front()->get_transformation() == instance);
    for (size_t i = 0; i < 2; ++i) {
        const auto* part = model.objects.front()->volumes[i];
        CHECK(part->get_transformation() == transform);
        CHECK(part->extruder_id() == 3);
        if ((i == 0) == bad_first) {
            REQUIRE(part->mesh().facets_count() == 8);
            CHECK(part->mesh().its.vertices.size() == 6); // Do not duplicate shared-edge vertices.
            for (size_t f = 0; f < 8; ++f)
                for (size_t c = 0; c < 3; ++c)
                    CHECK(part->mesh().its.vertices[part->mesh().its.indices[f][c]] ==
                          original->its.vertices[original->its.indices[bad_offset + f][c]]);
            if (painted) {
                CHECK_FALSE(part->supported_facets.empty());
                CHECK_FALSE(part->seam_facets.empty());
                CHECK_FALSE(part->fuzzy_skin_facets.empty());
                CHECK(part->mmu_segmentation_facets.empty());
                for (size_t face = 0; face < 8; ++face) {
                    CHECK(part->supported_facets.get_triangle_as_string(int(face)) == expected_annotations[0][face]);
                    CHECK(part->seam_facets.get_triangle_as_string(int(face)) == expected_annotations[1][face]);
                    CHECK(part->fuzzy_skin_facets.get_triangle_as_string(int(face)) == expected_annotations[2][face]);
                }
            }
        } else {
            CHECK(part->mesh().facets_count() == 12);
            CHECK_FALSE(MeshBoolean::cgal::requires_repair(part->mesh()));
            if (painted) {
                CHECK_FALSE(part->mmu_segmentation_facets.empty());
                CHECK(part->supported_facets.empty());
                CHECK(part->seam_facets.empty());
                CHECK(part->fuzzy_skin_facets.empty());
                auto       restored_mmu = part->mmu_segmentation_facets.get_facets_strict(*part, EnforcerBlockerType::ENFORCER);
                const auto surface_area = [](const indexed_triangle_set& its) {
                    double area = 0.;
                    for (const auto& face : its.indices) {
                        const Vec3d a = its.vertices[face[0]].cast<double>();
                        const Vec3d b = its.vertices[face[1]].cast<double>();
                        const Vec3d c = its.vertices[face[2]].cast<double>();
                        area += 0.5 * (b - a).cross(c - a).norm();
                    }
                    return area;
                };
                REQUIRE_FALSE(restored_mmu.empty());
                its_compactify_vertices(restored_mmu);
                CHECK(surface_area(restored_mmu) == Approx(surface_area(expected_mmu)).epsilon(1.e-4));
                CHECK(bounding_box(restored_mmu).min.isApprox(bounding_box(expected_mmu).min));
                CHECK(bounding_box(restored_mmu).max.isApprox(bounding_box(expected_mmu).max));
            }
        }
    }
    // A second attempt must not report splitting unchanged geometry as progress.
    const auto repaired = model.objects.front()->volumes[bad_first ? 1 : 0]->mesh_ptr();
    CHECK_FALSE(repair_model(model, report, options));
    CHECK(report.status() == std::string("unresolved"));
    CHECK_FALSE(report.committed);
    CHECK(report.split_volumes == 0);
    CHECK(model.objects.front()->volumes[bad_first ? 1 : 0]->mesh_ptr() == repaired);
}

TEST_CASE("Partial grouping preserves nested cavity winding beside an open shell", "[ModelRepair][Partial][CGAL][Split]")
{
    TriangleMesh input = make_cube(20., 20., 20.);
    auto         inner = make_cube(5., 5., 5.);
    inner.translate(7.5f, 7.5f, 7.5f);
    inner.flip_triangles();
    input.merge(inner);
    auto open = cube_with_open_side();
    open.translate(50.f, 0.f, 0.f);
    input.merge(open);
    Model              model    = make_model(std::move(input));
    const auto         original = model.objects.front()->volumes.front()->mesh_ptr();
    ModelRepairOptions options;
    options.allow_partial = true;
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    REQUIRE(model.objects.front()->volumes.size() == 2);
    const auto& cavity = model.objects.front()->volumes.front()->mesh();
    REQUIRE(cavity.facets_count() == 24);
    for (size_t f = 0; f < 24; ++f)
        for (size_t c = 0; c < 3; ++c)
            CHECK(cavity.its.vertices[cavity.its.indices[f][c]] == original->its.vertices[original->its.indices[f][c]]);
    const auto shells = its_split(cavity.its);
    REQUIRE(shells.size() == 2);
    CHECK(std::count_if(shells.begin(), shells.end(), [](const auto& shell) { return its_volume(shell) < 0.; }) == 1);
    CHECK(report.mesh_repaired == 1);
    CHECK(report.mesh_validated());
}

TEST_CASE("Partial grouping without progress or with fatal interruption does not change ownership", "[ModelRepair][Partial][Split]")
{
    const int outcome = GENERATE(0, 1, 2); // All rejected, worker exception after progress, cancellation after progress.
    auto      input   = cube_with_open_side();
    auto      bad     = tetrahedra_sharing_an_edge();
    bad.translate(50.f, 0.f, 0.f);
    input.merge(bad);
    Model              model     = make_model(std::move(input));
    auto*              object    = model.objects.front();
    auto*              volume    = object->volumes.front();
    const auto         original  = volume->mesh_ptr();
    const auto         id        = volume->id();
    const auto         transform = volume->get_transformation();
    bool               canceled  = false;
    ModelRepairOptions options;
    options.allow_partial = true;
    options.is_canceled   = [&]() { return canceled; };
    options.repair_mesh   = [&](TriangleMesh& candidate, std::string& error) {
        if (outcome != 0 && candidate.facets_count() == 10)
            return MeshBoolean::cgal::repair(candidate, nullptr, &error);
        if (outcome == 1)
            throw std::runtime_error("Injected worker failure after successful group repair.");
        canceled  = outcome == 2;
        candidate = make_cube(99., 99., 99.); // A rejected candidate must not survive.
        error     = "Injected geometric rejection.";
        return false;
    };
    ModelRepairReport report;
    REQUIRE_FALSE(repair_model(model, report, options));
    CHECK_FALSE(report.committed);
    CHECK(report.status() == std::string(outcome == 0 ? "unresolved" : outcome == 1 ? "failed" : "canceled"));
    CHECK(report.can_continue() == (outcome == 0));
    REQUIRE(object->volumes.size() == 1);
    CHECK(object->volumes.front() == volume);
    CHECK(volume->mesh_ptr() == original);
    CHECK(volume->id() == id);
    CHECK(volume->get_transformation() == transform);
}

TEST_CASE("Partial repair continues past unresolved objects in either order", "[ModelRepair][Partial]")
{
    const bool unresolved_first = GENERATE(false, true);
    Model      model;
    for (size_t i = 0; i < 2; ++i) {
        auto* object = model.add_object();
        object->add_volume((i == 0) == unresolved_first ? tetrahedra_sharing_an_edge() : cube_with_open_side());
        object->add_instance();
    }
    const size_t       bad_index      = unresolved_first ? 0 : 1;
    const auto         unchanged_mesh = model.objects[bad_index]->volumes.front()->mesh_ptr();
    const auto         unchanged_id   = model.objects[bad_index]->volumes.front()->id();
    ModelRepairOptions options;
    options.allow_partial    = true;
    options.split_components = false;
    options.repair_mesh      = [](TriangleMesh& mesh, std::string& error) {
        if (mesh.its.indices.size() == 10) {
            mesh = make_cube(10., 10., 10.);
            return true;
        }
        mesh  = TriangleMesh{};
        error = "Cannot safely repair this part.";
        return false;
    };
    size_t progress_count = 0;
    options.on_progress   = [&](size_t done, size_t total, const ModelRepairVolumeReport&) {
        CHECK(total == 2);
        ++progress_count;
        CHECK(done == progress_count);
    };
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    CHECK(report.status() == std::string("partially_repaired"));
    CHECK(report.repaired == 1);
    CHECK(report.unresolved == 1);
    CHECK(report.failed == 0);
    CHECK(progress_count == 2);
    CHECK(model.objects[bad_index]->volumes.front()->mesh_ptr() == unchanged_mesh);
    CHECK(model.objects[bad_index]->volumes.front()->id() == unchanged_id);
    CHECK(model.objects[1 - bad_index]->volumes.front()->mesh().facets_count() == 12);
    REQUIRE(report.changed_object_indices.size() == 1);
    CHECK(report.changed_object_indices.front() == 1 - bad_index);
}

TEST_CASE("Partial repair distinguishes unresolved unchanged input from fatal input", "[ModelRepair][Partial]")
{
    Model              model  = make_model(tetrahedra_sharing_an_edge());
    auto*              volume = model.objects.front()->volumes.front();
    const auto         mesh   = volume->mesh_ptr();
    const auto         id     = volume->id();
    ModelRepairOptions options;
    options.allow_partial = true;
    options.repair_mesh   = [](TriangleMesh& candidate, std::string& error) {
        candidate = TriangleMesh{};
        error     = "Unresolved geometry.";
        return false;
    };
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.can_continue());
    CHECK_FALSE(report.mesh_validated());
    CHECK(report.status() == std::string("unresolved"));
    CHECK(report.unresolved == 1);
    CHECK(report.failed == 0);
    CHECK(volume->mesh_ptr() == mesh);
    CHECK(volume->id() == id);
}

TEST_CASE("Partial repair preserves an unresolved volume beside a repaired volume", "[ModelRepair][Partial]")
{
    const bool   unresolved_first = GENERATE(false, true);
    Model        model;
    ModelObject* object = model.add_object();
    object->add_instance();
    for (size_t i = 0; i < 2; ++i)
        object->add_volume((i == 0) == unresolved_first ? tetrahedra_sharing_an_edge() : cube_with_open_side());
    const size_t       bad_index   = unresolved_first ? 0 : 1;
    const auto         original    = object->volumes[bad_index]->mesh_ptr();
    const auto         original_id = object->volumes[bad_index]->id();
    ModelRepairOptions options;
    options.allow_partial    = true;
    options.split_components = false;
    options.repair_mesh      = [](TriangleMesh& mesh, std::string& error) {
        if (mesh.its.indices.size() == 10) {
            mesh = make_cube(10., 10., 10.);
            return true;
        }
        error = "Unresolved geometry.";
        return false;
    };
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    CHECK(report.status() == std::string("partially_repaired"));
    CHECK(report.repaired == 1);
    CHECK(report.unresolved == 1);
    REQUIRE(object->volumes.size() == 2);
    CHECK(object->volumes[bad_index]->mesh_ptr() == original);
    CHECK(object->volumes[bad_index]->id() == original_id);
    CHECK(object->volumes[1 - bad_index]->mesh().facets_count() == 12);
    CHECK(report.changed_object_indices == std::vector<size_t>{0});
}

TEST_CASE("Retainable basic cleanup does not remove reversed interfaces", "[ModelRepair][Partial]")
{
    indexed_triangle_set input = tetrahedra_sharing_an_edge().its;
    const auto           face  = input.indices.front();
    input.indices.emplace_back(face[0], face[2], face[1]);
    Model              model    = make_model(TriangleMesh(std::move(input)));
    const auto         original = model.objects.front()->volumes.front()->mesh_ptr();
    ModelRepairOptions options;
    options.allow_partial    = true;
    options.split_components = false;
    options.repair_mesh      = [](TriangleMesh&, std::string& error) {
        error = "Unresolved geometry.";
        return false;
    };
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));
    CHECK(report.can_continue());
    CHECK(report.status() == std::string("unresolved"));
    CHECK(report.basic_removed_faces == 0);
    CHECK(model.objects.front()->volumes.front()->mesh_ptr() == original);
}

TEST_CASE("Basic cleanup alone is fully validated and does not call advanced repair", "[ModelRepair][Partial][CGAL]")
{
    indexed_triangle_set input = make_cube(10., 10., 10.).its;
    input.indices.push_back(input.indices.front());
    input.indices.emplace_back(0, 0, 1);
    Model              model = make_model(TriangleMesh(std::move(input)));
    ModelRepairOptions options;
    options.allow_partial = true;
    options.repair_mesh   = [](TriangleMesh&, std::string&) {
        FAIL("A fully cleaned cube must not require advanced repair");
        return false;
    };
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    CHECK(report.mesh_validated());
    CHECK(report.status() == std::string("repaired"));
    CHECK(report.basic_removed_faces == 2);
    CHECK(report.unresolved == 0);
    CHECK(model.objects.front()->volumes.front()->mesh().facets_count() == 12);
}

TEST_CASE("Partial mode keeps invalid input and worker exceptions fatal", "[ModelRepair][Partial]")
{
    Model              model    = make_model(cube_with_open_side());
    auto*              object   = model.objects.front();
    const auto         original = object->volumes.front()->mesh_ptr();
    ModelRepairOptions options;
    options.allow_partial = true;
    SECTION("invalid index") { add_invalid_volume(*object); }
    SECTION("planar-only object") { model.add_object()->add_volume(planar_square()); }
    SECTION("non-finite coordinates")
    {
        auto mesh                     = make_cube(10., 10., 10.);
        mesh.its.vertices.front().x() = std::numeric_limits<float>::infinity();
        object->add_volume(TriangleMesh{})->set_mesh(std::move(mesh));
    }
    SECTION("resource exhaustion")
    {
        options.repair_mesh = [](TriangleMesh&, std::string&) -> bool { throw std::bad_alloc(); };
    }
    SECTION("preparation worker exception")
    {
        options.run_mesh_preparation = [](const std::function<void()>&) { throw std::runtime_error("worker failed"); };
    }
    SECTION("post-repair validation")
    {
        options.repair_mesh = [](TriangleMesh& mesh, std::string&) {
            mesh = tetrahedra_sharing_an_edge();
            return true;
        };
    }
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));
    CHECK_FALSE(report.committed);
    CHECK_FALSE(report.can_continue());
    CHECK_FALSE(report.mesh_validated());
    CHECK(report.failed != 0);
    CHECK(report.status() == std::string("failed"));
    CHECK(object->volumes.front()->mesh_ptr() == original);
}

TEST_CASE("Partial mode cancellation still discards all staged changes", "[ModelRepair][Partial]")
{
    indexed_triangle_set input = tetrahedra_sharing_an_edge().its;
    input.indices.push_back(input.indices.front());
    Model              model    = make_model(TriangleMesh(std::move(input)));
    const auto         original = model.objects.front()->volumes.front()->mesh_ptr();
    bool               canceled = false;
    ModelRepairOptions options;
    options.allow_partial = true;
    options.repair_mesh   = [](TriangleMesh&, std::string& error) {
        error = "Unresolved geometry.";
        return false;
    };
    options.is_canceled = [&]() { return canceled; };
    options.on_progress = [&](size_t, size_t, const ModelRepairVolumeReport&) { canceled = true; };
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));
    CHECK(report.canceled);
    CHECK_FALSE(report.committed);
    CHECK_FALSE(report.can_continue());
    CHECK(report.basic_removed_faces == 0);
    CHECK(model.objects.front()->volumes.front()->mesh_ptr() == original);
}

TEST_CASE("Partial fallback cannot discard conflicting painted source faces", "[ModelRepair][Partial][Painting]")
{
    indexed_triangle_set input = tetrahedra_sharing_an_edge().its;
    input.indices.push_back(input.indices.front());
    Model            model  = make_model(TriangleMesh(std::move(input)));
    auto*            volume = model.objects.front()->volumes.front();
    TriangleSelector selector(volume->mesh());
    selector.set_facet(0, EnforcerBlockerType::Extruder2);
    selector.set_facet(8, EnforcerBlockerType::Extruder3);
    REQUIRE(volume->mmu_segmentation_facets.set(selector));
    const auto         original           = volume->mesh_ptr();
    const auto         painting_timestamp = volume->mmu_segmentation_facets.timestamp();
    ModelRepairOptions options;
    options.allow_partial             = true;
    options.preserve_annotations      = true;
    options.allow_annotation_clearing = GENERATE(false, true);
    options.repair_mesh               = [](TriangleMesh&, std::string& error) {
        error = "Unresolved geometry.";
        return false;
    };
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));
    CHECK_FALSE(report.committed);
    CHECK_FALSE(report.can_continue());
    CHECK(report.failed != 0);
    CHECK(volume->mesh_ptr() == original);
    CHECK(volume->mmu_segmentation_facets.timestamp() == painting_timestamp);
}

TEST_CASE("Headless model repair does not commit a split when a later volume fails", "[ModelRepair][CGAL][Split]")
{
    Model        model  = make_model(two_disjoint_shells(false));
    ModelObject* object = model.objects.front();
    add_invalid_volume(*object);
    ModelVolume*                              original_volume = object->volumes.front();
    const std::shared_ptr<const TriangleMesh> original_mesh   = original_volume->mesh_ptr();

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report));

    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.failed == 1);
    CHECK(report.rolled_back == 1);
    CHECK(report.repaired == 0);
    CHECK(report.changed_object_indices.empty());
    REQUIRE(model.objects.front() == object);
    REQUIRE(object->volumes.size() == 2);
    CHECK(object->volumes.front() == original_volume);
    CHECK(object->volumes.front()->mesh_ptr() == original_mesh);
    CHECK(object->volumes.front()->mesh().split().size() == 2);
}

TEST_CASE("Headless model repair may target one object without touching another", "[ModelRepair][CGAL]")
{
    Model        model;
    ModelObject* first_object = model.add_object();
    first_object->add_volume(cube_with_open_side());
    first_object->add_instance();
    ModelObject* second_object = model.add_object();
    second_object->add_volume(cube_with_open_side());
    second_object->add_instance();

    const std::shared_ptr<const TriangleMesh> first_mesh = first_object->volumes.front()->mesh_ptr();

    ModelRepairOptions options;
    options.targets.push_back({1, std::nullopt});

    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));

    first_object  = model.objects[0];
    second_object = model.objects[1];
    CHECK(report.objects == 1);
    CHECK(report.volumes == 1);
    CHECK(report.repaired == 1);
    CHECK(first_object->volumes.front()->mesh_ptr() == first_mesh);
    CHECK(its_num_open_edges(first_object->volumes.front()->mesh().its) > 0);
    CHECK(its_num_open_edges(second_object->volumes.front()->mesh().its) == 0);
}

TEST_CASE("Headless model repair cancellation rolls back staged volumes", "[ModelRepair][CGAL]")
{
    Model        model  = make_model(cube_with_open_side());
    ModelObject* object = model.objects.front();
    object->add_volume(cube_with_open_side());

    const std::shared_ptr<const TriangleMesh> first_mesh  = object->volumes[0]->mesh_ptr();
    const std::shared_ptr<const TriangleMesh> second_mesh = object->volumes[1]->mesh_ptr();
    bool                                      cancel      = false;

    ModelRepairOptions options;
    options.on_progress = [&cancel](size_t completed, size_t total, const ModelRepairVolumeReport&) {
        CHECK(total == 2);
        if (completed == 1)
            cancel = true;
    };
    options.is_canceled = [&cancel]() { return cancel; };

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    CHECK(report.canceled);
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.rolled_back == 1);
    CHECK(report.status() == std::string("canceled"));
    CHECK(object->volumes[0]->mesh_ptr() == first_mesh);
    CHECK(object->volumes[1]->mesh_ptr() == second_mesh);
}

TEST_CASE("Headless model repair normalizes a throwing progress callback", "[ModelRepair][Transaction]")
{
    Model                                     model         = make_model(cube_with_open_side());
    ModelObject*                              object        = model.objects.front();
    ModelVolume*                              volume        = object->volumes.front();
    const std::shared_ptr<const TriangleMesh> original_mesh = volume->mesh_ptr();

    ModelRepairOptions options;
    options.on_progress = [](size_t, size_t, const ModelRepairVolumeReport&) { throw std::runtime_error("progress failed"); };

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.repaired == 0);
    CHECK(report.rolled_back == 1);
    REQUIRE_FALSE(report.failures.empty());
    CHECK(report.failures.back().message.find("progress failed") != std::string::npos);
    REQUIRE(model.objects.front() == object);
    REQUIRE(object->volumes.front() == volume);
    CHECK(volume->mesh_ptr() == original_mesh);
}

TEST_CASE("Headless model repair cancellation does not commit a staged split", "[ModelRepair][CGAL][Split]")
{
    Model                                     model           = make_model(two_disjoint_shells(false));
    ModelObject*                              object          = model.objects.front();
    ModelVolume*                              original_volume = object->volumes.front();
    const std::shared_ptr<const TriangleMesh> original_mesh   = original_volume->mesh_ptr();
    bool                                      cancel          = false;

    ModelRepairOptions options;
    options.on_progress = [&cancel](size_t completed, size_t total, const ModelRepairVolumeReport& volume_report) {
        CHECK(completed == 1);
        CHECK(total == 1);
        CHECK(volume_report.split_parts == 2);
        cancel = true;
    };
    options.is_canceled = [&cancel]() { return cancel; };

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    CHECK(report.canceled);
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.rolled_back == 1);
    CHECK(report.repaired == 0);
    CHECK(report.changed_object_indices.empty());
    REQUIRE(model.objects.front() == object);
    REQUIRE(object->volumes.size() == 1);
    CHECK(object->volumes.front() == original_volume);
    CHECK(object->volumes.front()->mesh_ptr() == original_mesh);
    CHECK(object->volumes.front()->mesh().split().size() == 2);
}

TEST_CASE("Cancellation during split-part inspection finishes progress and clears staged counters",
          "[ModelRepair][CGAL][Split][Transaction]")
{
    Model                                     model           = make_model(two_disjoint_shells(false));
    ModelObject*                              object          = model.objects.front();
    ModelVolume*                              original_volume = object->volumes.front();
    const std::shared_ptr<const TriangleMesh> original_mesh   = original_volume->mesh_ptr();
    bool                                      cancel          = false;
    size_t                                    inspections     = 0;
    size_t                                    progress_calls  = 0;

    ModelRepairOptions options;
    options.mesh_requires_repair = [&](const TriangleMesh&) {
        ++inspections;
        cancel = true;
        return false;
    };
    options.is_canceled = [&cancel]() { return cancel; };
    options.on_progress = [&](size_t completed, size_t total, const ModelRepairVolumeReport& volume_report) {
        ++progress_calls;
        CHECK(completed == 1);
        CHECK(total == 1);
        CHECK(volume_report.result == ModelRepairVolumeStatus::RolledBack);
    };

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    CHECK(inspections == 1);
    CHECK(progress_calls == 1);
    CHECK(report.canceled);
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.rolled_back == 1);
    CHECK(report.split_volumes == 0);
    CHECK(report.created_parts == 0);
    CHECK(report.removed_parts == 0);
    CHECK(report.changed_object_indices.empty());
    REQUIRE(model.objects.front() == object);
    REQUIRE(object->volumes.size() == 1);
    CHECK(object->volumes.front() == original_volume);
    CHECK(object->volumes.front()->mesh_ptr() == original_mesh);
}

TEST_CASE("Headless model repair may delegate only private mesh work to a worker", "[ModelRepair][CGAL][Worker]")
{
    Model                                     model         = make_model(cube_with_open_side());
    const std::shared_ptr<const TriangleMesh> original_mesh = model.objects.front()->volumes.front()->mesh_ptr();
    const std::thread::id                     caller_thread = std::this_thread::get_id();
    std::thread::id                           worker_thread;
    std::thread::id                           validation_worker_thread;
    std::vector<std::thread::id>              progress_threads;
    std::vector<std::pair<size_t, size_t>>    progress_values;
    bool                                      hook_called            = false;
    bool                                      received_shared_source = false;
    bool                                      copied_on_worker       = false;

    ModelRepairOptions options;
    size_t             validation_calls = 0;
    options.mesh_requires_repair        = [&](const TriangleMesh& mesh) {
        bool        requires_repair = true;
        std::thread validation_worker([&]() {
            validation_worker_thread = std::this_thread::get_id();
            requires_repair          = MeshBoolean::cgal::requires_repair(mesh);
        });
        validation_worker.join();
        ++validation_calls;
        return requires_repair;
    };
    options.repair_mesh_from_source = [&](const TriangleMesh& source, TriangleMesh& repaired, std::string& error) {
        hook_called            = true;
        received_shared_source = &source == original_mesh.get();
        bool        success    = false;
        std::thread worker([&]() {
            worker_thread     = std::this_thread::get_id();
            TriangleMesh mesh = source;
            copied_on_worker  = &mesh != original_mesh.get();
            success           = MeshBoolean::cgal::repair(mesh, nullptr, &error);
            if (success)
                repaired = std::move(mesh);
        });
        worker.join();
        return success;
    };
    options.on_progress = [&](size_t completed, size_t total, const ModelRepairVolumeReport&) {
        progress_threads.push_back(std::this_thread::get_id());
        progress_values.emplace_back(completed, total);
    };

    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));

    CHECK(hook_called);
    CHECK(received_shared_source);
    CHECK(copied_on_worker);
    CHECK(worker_thread != caller_thread);
    CHECK(validation_calls == 1);
    CHECK(validation_worker_thread != caller_thread);
    REQUIRE_FALSE(progress_values.empty());
    REQUIRE(progress_threads.size() == progress_values.size());
    size_t previous_completed = 0;
    for (size_t progress_index = 0; progress_index < progress_values.size(); ++progress_index) {
        CHECK(progress_threads[progress_index] == caller_thread);
        CHECK(progress_values[progress_index].first >= previous_completed);
        CHECK(progress_values[progress_index].first <= progress_values[progress_index].second);
        previous_completed = progress_values[progress_index].first;
    }
    CHECK(report.committed);
    CHECK(model.objects.front()->volumes.front()->mesh_ptr() != original_mesh);
    CHECK(its_num_open_edges(model.objects.front()->volumes.front()->mesh().its) == 0);
}

TEST_CASE("Headless model repair observes cancellation after delegated mesh work", "[ModelRepair][CGAL][Worker]")
{
    Model                                     model         = make_model(cube_with_open_side());
    ModelObject*                              object        = model.objects.front();
    ModelVolume*                              volume        = object->volumes.front();
    const std::shared_ptr<const TriangleMesh> original_mesh = volume->mesh_ptr();
    const ObjectID                            original_id   = volume->id();
    std::atomic_bool                          cancel{false};

    ModelRepairOptions options;
    options.repair_mesh = [&](TriangleMesh& mesh, std::string& error) {
        std::mutex              mutex;
        std::condition_variable condition;
        bool                    worker_started = false;
        bool                    may_repair     = false;
        bool                    success        = false;

        std::thread worker([&]() {
            {
                std::unique_lock<std::mutex> lock(mutex);
                worker_started = true;
                condition.notify_one();
                condition.wait(lock, [&may_repair]() { return may_repair; });
            }
            success = MeshBoolean::cgal::repair(mesh, nullptr, &error);
        });
        std::thread canceler([&]() {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&worker_started]() { return worker_started; });
            cancel.store(true);
            may_repair = true;
            lock.unlock();
            condition.notify_one();
        });

        worker.join();
        canceler.join();
        return success;
    };
    options.is_canceled = [&cancel]() { return cancel.load(); };

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    CHECK(report.canceled);
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.rolled_back == 1);
    CHECK(report.status() == std::string("canceled"));
    REQUIRE(model.objects.front() == object);
    REQUIRE(object->volumes.front() == volume);
    CHECK(volume->id() == original_id);
    CHECK(volume->mesh_ptr() == original_mesh);
    CHECK(its_num_open_edges(volume->mesh().its) > 0);
}

TEST_CASE("Headless delegated repair failure cannot mutate source mesh, ID, or painting", "[ModelRepair][CGAL][Worker]")
{
    Model                                     model                 = make_model(cube_with_open_side());
    ModelObject*                              object                = model.objects.front();
    ModelVolume*                              volume                = object->volumes.front();
    const std::shared_ptr<const TriangleMesh> original_mesh         = volume->mesh_ptr();
    const ObjectID                            original_id           = volume->id();
    bool                                      received_private_mesh = false;
    paint_facet(volume->supported_facets, volume->mesh(), 0, EnforcerBlockerType::ENFORCER);

    ModelRepairOptions options;
    options.preserve_annotations = true;
    options.repair_mesh          = [&](TriangleMesh& mesh, std::string& error) {
        received_private_mesh = static_cast<const TriangleMesh*>(&mesh) != original_mesh.get();
        std::thread worker([&]() {
            mesh  = TriangleMesh{};
            error = "Injected worker failure.";
        });
        worker.join();
        return false;
    };

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    CHECK(received_private_mesh);
    CHECK_FALSE(report.committed);
    REQUIRE(model.objects.front() == object);
    REQUIRE(object->volumes.front() == volume);
    CHECK(volume->id() == original_id);
    CHECK(volume->mesh_ptr() == original_mesh);
    CHECK_FALSE(volume->supported_facets.empty());
}

TEST_CASE("Headless model repair may reject topology changes that would clear annotations", "[ModelRepair][CGAL]")
{
    Model        model  = make_model(cube_with_open_side());
    ModelVolume* volume = model.objects.front()->volumes.front();
    volume->supported_facets.set_triangle_from_string(0, "1");
    const std::shared_ptr<const TriangleMesh> original_mesh = volume->mesh_ptr();

    ModelRepairOptions options;
    options.allow_annotation_clearing = false;

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures.front().message.find("invalidate painted facet annotations") != std::string::npos);
    CHECK_FALSE(report.volume_reports.front().annotations_cleared);
    CHECK(volume->mesh_ptr() == original_mesh);
    CHECK_FALSE(volume->supported_facets.empty());
}

TEST_CASE("Headless model repair rejects unmappable exterior annotations", "[ModelRepair][CGAL][Painting]")
{
    Model        model  = make_model(cube_with_open_side());
    ModelVolume* volume = model.objects.front()->volumes.front();
    volume->exterior_facets.set_triangle_from_string(0, "1");
    const std::shared_ptr<const TriangleMesh> original_mesh = volume->mesh_ptr();

    ModelRepairOptions options;
    options.allow_annotation_clearing = false;
    options.preserve_annotations      = true;

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures.front().message.find("exterior-facet annotations") != std::string::npos);
    CHECK(volume->mesh_ptr() == original_mesh);
    CHECK_FALSE(volume->exterior_facets.empty());
}
