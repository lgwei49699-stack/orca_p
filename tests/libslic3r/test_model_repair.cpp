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
#include <libslic3r/MeshRepair.hpp>
#include <libslic3r/Format/STL.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/ModelRepair.hpp>
#include <libslic3r/ModelInspection.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>
#include <slic3r/Utils/ModelRepairBackup.hpp>
#include <slic3r/Utils/ModelRepairPolicy.hpp>
#include <boost/filesystem.hpp>

using namespace Slic3r;

namespace {

TriangleMesh cube_with_open_side()
{
    TriangleMesh mesh = make_cube(10., 10., 10.);
    mesh.its.indices.erase(mesh.its.indices.begin(), mesh.its.indices.begin() + 2);
    return TriangleMesh(std::move(mesh.its));
}

TriangleMesh with_degenerate_edge_cap(TriangleMesh mesh, size_t face_index = 0)
{
    auto        its      = std::move(mesh.its);
    const auto  face     = its.indices[face_index];
    const int   midpoint = int(its.vertices.size());
    const Vec3f point    = (its.vertices[face[0]] + its.vertices[face[1]]) * 0.5f;
    its.vertices.push_back(point);
    // Same cube surface, but the unsplit adjacent face still uses (a,b).
    // The collinear (a,b,midpoint) record pairs all three sides of this T-junction.
    its.indices[face_index] = Vec3i32(face[0], midpoint, face[2]);
    its.indices.emplace_back(midpoint, face[1], face[2]);
    its.indices.emplace_back(face[0], face[1], midpoint);
    return TriangleMesh(std::move(its));
}

TriangleMesh cube_with_degenerate_edge_cap() { return with_degenerate_edge_cap(make_cube(10., 10., 10.)); }

TriangleMesh cube_with_unsupported_edge_cap()
{
    auto its = cube_with_degenerate_edge_cap().its;
    // Coincident endpoints create adjacent degenerate faces after welding.
    // This is not the isolated three-distinct-point cap accepted by preparation.
    its.vertices.back() = its.vertices[its.indices.back()[0]];
    return TriangleMesh(std::move(its));
}

TriangleMesh intersecting_tube_with_edge_cap()
{
    indexed_triangle_set tube;
    for (int ring = 0; ring < 4; ++ring) {
        const float x = float(ring * 10);
        tube.vertices.emplace_back(x, 0.f, 0.f);
        tube.vertices.emplace_back(x, 10.f, 0.f);
        tube.vertices.emplace_back(x, 10.f, 10.f);
        tube.vertices.emplace_back(x, 0.f, 10.f);
    }
    tube.vertices[6] = Vec3f(5.f, 5.f, -2.f); // Same folded tube as the partial CGAL regression.
    tube.indices     = {Vec3i32(0, 2, 1), Vec3i32(0, 3, 2)};
    for (int ring = 0; ring < 3; ++ring)
        for (int corner = 0; corner < 4; ++corner) {
            const int a = ring * 4 + corner, b = ring * 4 + (corner + 1) % 4;
            tube.indices.emplace_back(a, b, b + 4);
            tube.indices.emplace_back(a, b + 4, a + 4);
        }
    tube.indices.emplace_back(12, 13, 14);
    tube.indices.emplace_back(12, 14, 15);
    const size_t cap_face = tube.indices.size() - 1; // Far from the folded corner.
    return with_degenerate_edge_cap(TriangleMesh(std::move(tube)), cap_face);
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

TriangleMesh cube_with_internal_partition()
{
    auto its = make_cube(10., 10., 10.).its;
    // x=y diagonal: all four boundary edges already have two exterior faces.
    its.indices.emplace_back(0, 2, 6);
    its.indices.emplace_back(0, 6, 4);
    return TriangleMesh(std::move(its));
}

TriangleMesh tube_with_spurious_membrane(bool spans_outer_ring)
{
    indexed_triangle_set       its;
    const std::array<Vec2f, 8> ring{Vec2f(0, 0), Vec2f(10, 0), Vec2f(10, 10), Vec2f(0, 10),
                                    Vec2f(1, 1), Vec2f(2, 1),  Vec2f(2, 2),   Vec2f(1, 2)};
    for (int level = 0; level < 3; ++level)
        for (const auto& p : ring)
            its.vertices.emplace_back(p.x(), p.y(), float(5 * level));
    for (int level = 0; level < 2; ++level)
        for (int j = 0; j < 4; ++j) {
            const int a = 8 * level + j, b = 8 * level + (j + 1) % 4;
            its.indices.emplace_back(a, b, b + 8);
            its.indices.emplace_back(a, b + 8, a + 8);
            its.indices.emplace_back(a + 4, b + 12, b + 4);
            its.indices.emplace_back(a + 4, a + 12, b + 12);
        }
    for (int j = 0; j < 4; ++j) {
        const int k = (j + 1) % 4;
        its.indices.emplace_back(j, j + 4, k + 4);
        its.indices.emplace_back(j, k + 4, k);
        its.indices.emplace_back(j + 16, k + 20, j + 20);
        its.indices.emplace_back(j + 16, k + 16, k + 20);
    }
    const int first = spans_outer_ring ? 8 : 12;
    // The small disk's centroid is in the hole. The large disk's centroids
    // are INSIDE material, but its triangles cross the hole's walls. Neither
    // can be deleted on a mere centroid/edge-count certificate.
    its.indices.emplace_back(first, first + 1, first + 2);
    its.indices.emplace_back(first, first + 2, first + 3);
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
    REQUIRE(model.objects.front()->volumes.size() == 1);
    double volume = 0.;
    for (const ModelVolume* part : model.objects.front()->volumes) {
        CHECK_FALSE(MeshBoolean::cgal::requires_repair(part->mesh()));
        CHECK(part->mesh().its.indices.size() == 24);
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
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));
    CHECK_FALSE(report.committed);
    CHECK(volume->mesh_ptr() == source);
    CHECK_FALSE(volume->mmu_segmentation_facets.empty());
}

TEST_CASE("Strict repair rejects ambiguous nearby open shells without mutation", "[ModelRepair][CGAL][Acceptance]")
{
    TriangleMesh first;
    REQUIRE(first.ReadSTLFile((std::string(TEST_DATA_DIR) + "/model_repair_acceptance/01-missing-top.stl").c_str(), false));
    TriangleMesh second = first;
    // The facing top boundary edges are closer than the crack tolerance, but
    // these are independent shells, not a crack within one connected surface.
    second.translate(10.00002f, 0.f, 0.f);
    first.merge(second);
    const auto  original = first.its;
    std::string error;
    CHECK_FALSE(MeshBoolean::cgal::repair(first, nullptr, &error));
    CHECK_FALSE(error.empty());
    CHECK(first.its.vertices == original.vertices);
    CHECK(first.its.indices == original.indices);
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
    CHECK_FALSE(error.empty());
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

TEST_CASE("GUI repair backup snapshots changed live objects after commit and bed adjustment", "[ModelRepair][Backup][Transaction]")
{
    Model model = make_model(cube_with_open_side());
    model.objects.front()->add_volume(make_cube(4., 4., 4.));
    ModelObject* untouched = model.add_object();
    untouched->add_volume(make_cube(6., 6., 6.));
    untouched->add_instance();
    ModelObject* second = model.add_object();
    second->add_volume(cube_with_open_side());
    second->add_instance();
    const auto original_mesh  = model.objects[0]->volumes[0]->mesh_ptr();
    const auto untouched_mesh = untouched->volumes[0]->mesh_ptr();

    // Setup is complete before enabling backup. Private staging must not
    // inherit it; test callbacks below never invoke the real backup manager.
    model.set_need_backup();
    auto staged = make_model_repair_staging_copy(model, {{0, size_t(0)}, {2, size_t(0)}});
    CHECK_FALSE(staged->is_need_backup());
    ModelRepairOptions options = gui_model_repair_options();
    options.targets            = {{0, size_t(0)}, {2, size_t(0)}};
    ModelRepairReport report;
    REQUIRE(repair_staged_model(*staged, report, options));
    REQUIRE(report.changed_object_indices == std::vector<size_t>{0, 2});

    // Simulate the final candidate's painting and placement. This tests backup
    // snapshot contents, not the separate painting-remapping algorithm.
    ModelVolume* repaired = staged->objects[0]->volumes[0];
    paint_facet(repaired->mmu_segmentation_facets, repaired->mesh(), 0, EnforcerBlockerType::ENFORCER);
    paint_facet(repaired->supported_facets, repaired->mesh(), 1, EnforcerBlockerType::BLOCKER);
    staged->objects[0]->instances[0]->set_offset(Vec3d(12., 3., 20.));
    staged->objects[2]->instances[0]->set_offset(Vec3d(2., 4., 30.));

    Model                     snapshots;
    std::vector<ModelObject*> queued_objects;
    std::vector<int>          order;
    const auto                save_object = [&](ModelObject& object) {
        snapshots.add_object(object);
        queued_objects.push_back(&object);
        order.push_back(1);
    };
    const auto save_metadata = [&] { order.push_back(2); };
    const auto staged_backup = queue_committed_model_repair_backup(*staged, report, save_object, save_metadata);
    CHECK_FALSE(staged_backup.failed());
    CHECK_FALSE(staged_backup.metadata_queued);
    CHECK(queued_objects.empty());
    CHECK(model.objects[0]->volumes[0]->mesh_ptr() == original_mesh);

    auto prepared = model.prepare_move_object_volume_structures(*staged, report.changed_object_indices);
    model.commit_move_object_volume_structures(std::move(prepared));
    for (size_t index : report.changed_object_indices) {
        model.objects[index]->invalidate_bounding_box();
        model.objects[index]->ensure_on_bed(false);
    }
    const auto backup = queue_committed_model_repair_backup(model, report, save_object, save_metadata);
    CHECK_FALSE(backup.failed());
    CHECK(backup.queued_objects == 2);
    CHECK(backup.metadata_queued);
    CHECK(order == std::vector<int>{1, 1, 2});
    REQUIRE(queued_objects == std::vector<ModelObject*>{model.objects[0], model.objects[2]});
    REQUIRE(snapshots.objects.size() == 2);
    CHECK(model.objects[0]->volumes[0]->mesh_ptr() != original_mesh);
    CHECK(untouched->volumes[0]->mesh_ptr() == untouched_mesh);
    for (size_t i = 0; i < queued_objects.size(); ++i) {
        const ModelObject* live  = queued_objects[i];
        const ModelObject* saved = snapshots.objects[i];
        REQUIRE(saved->volumes.size() == live->volumes.size());
        for (size_t v = 0; v < live->volumes.size(); ++v) {
            CHECK(saved->volumes[v]->mesh().its.vertices == live->volumes[v]->mesh().its.vertices);
            CHECK(saved->volumes[v]->mesh().its.indices == live->volumes[v]->mesh().its.indices);
            CHECK(saved->volumes[v]->get_transformation() == live->volumes[v]->get_transformation());
            CHECK(saved->volumes[v]->mmu_segmentation_facets.get_data() == live->volumes[v]->mmu_segmentation_facets.get_data());
            CHECK(saved->volumes[v]->supported_facets.get_data() == live->volumes[v]->supported_facets.get_data());
        }
        REQUIRE(saved->instances.size() == live->instances.size());
        REQUIRE_FALSE(saved->instances.empty());
        CHECK(saved->instances[0]->get_transformation() == live->instances[0]->get_transformation());
        CHECK(saved->min_z() == Approx(0.).margin(1.e-6));
        CHECK(its_num_open_edges(saved->volumes[0]->mesh().its) == 0);
    }
}

TEST_CASE("GUI repair backup skips failed canceled unchanged and unfinished reports", "[ModelRepair][Backup]")
{
    const int variant = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(variant);
    Model             model = make_model(make_cube(10., 10., 10.));
    ModelRepairReport report;
    report.success = report.committed = report.completed = true;
    report.changed_object_indices                        = {0};
    if (variant != 6)
        model.set_need_backup();
    switch (variant) {
    case 0: report.success = false; break;
    case 1: report.committed = false; break;
    case 2: report.canceled = true; break;
    case 3: report.failed = 1; break;
    case 4: report.completed = false; break;
    case 5: report.changed_object_indices.clear(); break;
    default: break; // Backup disabled, as for a private staging model.
    }
    size_t     calls  = 0;
    const auto backup = queue_committed_model_repair_backup(model, report, [&](ModelObject&) { ++calls; }, [&] { ++calls; });
    CHECK(calls == 0);
    CHECK(backup.queued_objects == 0);
    CHECK_FALSE(backup.metadata_queued);
    CHECK_FALSE(backup.failed());
}

TEST_CASE("GUI repair backup retains accepted partial progress without copying unchanged objects", "[ModelRepair][Backup][Partial]")
{
    Model model = make_model(make_cube(10., 10., 10.));
    model.add_object(*model.objects[0]);
    model.set_need_backup();
    ModelRepairReport report;
    report.success = report.committed = report.completed = true;
    report.unresolved                                    = 1;
    report.changed_object_indices                        = {0};
    std::vector<ModelObject*> queued;
    size_t                    metadata_calls = 0;
    const auto                backup         = queue_committed_model_repair_backup(
        model, report, [&](ModelObject& object) { queued.push_back(&object); }, [&] { ++metadata_calls; });
    CHECK_FALSE(report.mesh_validated());
    CHECK_FALSE(backup.failed());
    CHECK(backup.queued_objects == 1);
    CHECK(backup.metadata_queued);
    CHECK(queued == std::vector<ModelObject*>{model.objects[0]});
    CHECK(metadata_calls == 1);
}

TEST_CASE("GUI repair backup enqueue errors do not undo the scene commit", "[ModelRepair][Backup][Transaction]")
{
    const int failure = GENERATE(0, 1, 2, 3);
    CAPTURE(failure);
    Model model = make_model(cube_with_open_side());
    model.add_object(*model.objects[0]);
    Model staged(model);
    for (ModelObject* object : staged.objects)
        object->volumes[0]->set_mesh(make_cube(10., 10., 10.));
    ModelRepairReport report;
    report.success = report.committed = report.completed = true;
    report.changed_object_indices                        = {0, 1};
    auto prepared = model.prepare_move_object_volume_structures(staged, report.changed_object_indices);
    model.commit_move_object_volume_structures(std::move(prepared));
    const auto committed_mesh = model.objects[0]->volumes[0]->mesh_ptr();
    model.set_need_backup();
    size_t     object_calls   = 0;
    size_t     metadata_calls = 0;
    const auto backup         = queue_committed_model_repair_backup(
        model, report,
        [&](ModelObject&) {
            ++object_calls;
            if (object_calls == 1 && failure == 0)
                throw std::bad_alloc();
            if (object_calls == 1 && failure == 1)
                throw 1;
        },
        [&] {
            ++metadata_calls;
            if (failure == 2)
                throw std::runtime_error("metadata queue unavailable");
            throw 1;
        });
    CHECK(backup.failed());
    CHECK(object_calls == 2); // Continue queuing other changed objects.
    CHECK(backup.queued_objects == (failure < 2 ? 1 : 2));
    CHECK(backup.failed_objects == (failure < 2 ? 1 : 0));
    CHECK(metadata_calls == (failure < 2 ? 0 : 1));
    CHECK_FALSE(backup.metadata_queued);
    CHECK(backup.metadata_failed == (failure >= 2));
    CHECK(model.objects[0]->volumes[0]->mesh_ptr() == committed_mesh);
    CHECK(its_num_open_edges(committed_mesh->its) == 0);
    CHECK(report.success);
    CHECK(report.committed);
}

TEST_CASE("GUI repair backup does not publish metadata for missing object data", "[ModelRepair][Backup]")
{
    const int invalid = GENERATE(0, 1, 2, 3);
    CAPTURE(invalid);
    Model             model = make_model(make_cube(10., 10., 10.));
    ModelRepairReport report;
    report.success = report.committed = report.completed = true;
    report.changed_object_indices                        = {0};
    if (invalid == 0)
        report.changed_object_indices = {1};
    else if (invalid == 1) {
        model.objects.push_back(nullptr);
        report.changed_object_indices = {1};
    } else if (invalid == 2)
        model.objects[0]->clear_volumes();
    else
        model.objects[0]->clear_instances();
    model.set_need_backup();
    size_t     calls  = 0;
    const auto backup = queue_committed_model_repair_backup(model, report, [&](ModelObject&) { ++calls; }, [&] { ++calls; });
    CHECK(calls == 0);
    CHECK(backup.failed_objects == 1);
    CHECK(backup.failed());
    CHECK_FALSE(backup.metadata_queued);
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

    CHECK_FALSE(error.empty());
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
    CHECK_FALSE(error.empty());
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
    options.preserve_annotations = true;

    ModelRepairReport report;
    REQUIRE_FALSE(repair_model(model, report, options));
    CHECK_FALSE(report.committed);
    REQUIRE(report.failures.size() == 1);
    CHECK_FALSE(report.failures.front().message.empty());
    CHECK(model.objects.front()->volumes.front() == volume);
    CHECK(volume->mesh_ptr() == original_mesh);
    CHECK(volume->mmu_segmentation_facets.equals(before.objects.front()->volumes.front()->mmu_segmentation_facets));
    CHECK(volume->supported_facets.equals(before.objects.front()->volumes.front()->supported_facets));
}

TEST_CASE("2.5 repair retains exterior sections of soups with internal partitions", "[ModelRepair][CGAL][SoupTopology][SoupOrientation]")
{
    const int variant = GENERATE(0, 1, 2, 3);
    auto      mesh    = variant == 1 ? make_cube(10., 10., 10.) : cube_with_internal_partition();
    if (variant == 3) {
        auto separate = make_cube(10., 10., 10.);
        separate.translate(30.f, 0.f, 0.f);
        mesh.merge(separate); // Never select only the largest shell.
    }
    if (variant != 0) {
        mesh = with_degenerate_edge_cap(std::move(mesh), 5);
        mesh.its.indices.pop_back(); // An exact T-junction, without a zero-area cap.
        mesh = TriangleMesh(std::move(mesh.its));
    }
    if (variant == 2) {
        std::reverse(mesh.its.indices.begin(), mesh.its.indices.end());
        for (size_t i = 0; i < mesh.its.indices.size(); i += 3)
            std::swap(mesh.its.indices[i][0], mesh.its.indices[i][1]);
    }
    const auto  original = mesh.its;
    std::string error;
    REQUIRE(Slic3r::repair_mesh(mesh, nullptr, &error));
    INFO(error);
    CHECK(its_num_open_edges(mesh.its) == 0);
    CHECK(its_volume(mesh.its) == Approx(variant == 3 ? 2000. : 1000.));
    for (const auto& p : mesh.its.vertices)
        CHECK(std::find(original.vertices.begin(), original.vertices.end(), p) != original.vertices.end());
    const auto slices = slice_mesh_ex(mesh.its, {0.2f, 5.f, 9.8f});
    REQUIRE(slices.size() == 3);
    for (const auto& slice : slices) {
        REQUIRE(slice.size() == (variant == 3 ? 2 : 1));
        for (const auto& polygon : slice)
            CHECK(polygon.holes.empty());
        CHECK(area(slice) == Approx((variant == 3 ? 200. : 100.) / (SCALING_FACTOR * SCALING_FACTOR)));
    }
}

TEST_CASE("Soup topology does not erase membranes outside material or crossing a through-hole",
          "[ModelRepair][CGAL][SoupTopology][SoupOrientation]")
{
    const bool spans_outer_ring = GENERATE(false, true);
    auto       mesh             = tube_with_spurious_membrane(spans_outer_ring);
    if (GENERATE(false, true)) {
        const auto faces = mesh.its.indices;
        for (size_t i = faces.size() - 2; i < faces.size(); ++i) {
            const auto& f = faces[i];
            mesh.its.indices.emplace_back(f[2], f[1], f[0]);
        }
        mesh = TriangleMesh(std::move(mesh.its));
    }
    const auto  original = mesh.its;
    std::string error;
    CHECK_FALSE(MeshBoolean::cgal::repair(mesh, nullptr, &error));
    CHECK_FALSE(error.empty());
    CHECK(mesh.its.vertices == original.vertices);
    CHECK(mesh.its.indices == original.indices);
}

TEST_CASE("Bracket soup repair preserves exterior geometry and real holes", "[ModelRepair][CGAL][SoupTopology][Acceptance]")
{
    TriangleMesh mesh;
    REQUIRE(mesh.ReadSTLFile((std::string(TEST_DATA_DIR) + "/model_repair_topology/bracket.stl").c_str(), false));
    REQUIRE(mesh.facets_count() == 862);
    const auto original = mesh.its;
    const auto bounds   = mesh.bounding_box();
    const auto before   = inspect_model_mesh(mesh);
    REQUIRE(before["issues"]["nonmanifold_edge"]["count"] == 10);
    std::string error;
    REQUIRE(Slic3r::repair_mesh(mesh, nullptr, &error));
    INFO(error);
    CHECK(mesh.bounding_box().min == bounds.min);
    CHECK(mesh.bounding_box().max == bounds.max);
    for (const auto& p : mesh.its.vertices)
        CHECK(std::find(original.vertices.begin(), original.vertices.end(), p) != original.vertices.end());
    const auto after = inspect_model_mesh(mesh);
    for (const char* key :
         {"boundary", "nonmanifold_edge", "nonmanifold_vertex", "self_intersection", "degenerate", "duplicate", "orientation"}) {
        INFO(key);
        CHECK(after["issues"][key]["complete"].get<bool>());
    }
    CHECK(after["mesh_status"] == "passed");
    CHECK(mesh.facets_count() == 856);
    // Horizontal sections across the original bore: a clear passage through
    // X=0..5 at Y=30 must not be capped, and adjacent plate material must remain.
    const auto slices = slice_mesh_ex(mesh.its, {44.9f, 45.1f});
    REQUIRE(slices.size() == 2);
    for (const auto& slice : slices) {
        for (float x : {0.25f, 2.5f, 4.75f})
            CHECK(std::none_of(slice.begin(), slice.end(), [&](const ExPolygon& p) { return p.contains(Point::new_scale(x, 30.)); }));
        CHECK(std::any_of(slice.begin(), slice.end(), [](const ExPolygon& p) { return p.contains(Point::new_scale(2.5, 40.)); }));
        CHECK(std::any_of(slice.begin(), slice.end(), [](const ExPolygon& p) { return p.contains(Point::new_scale(-50., -2.)); }));
    }
    const auto repaired = mesh.its;
    REQUIRE(Slic3r::repair_mesh(mesh, nullptr, &error));
    const auto face_keys = [](const indexed_triangle_set& its) {
        std::vector<std::array<std::array<float, 3>, 3>> keys;
        for (const auto& f : its.indices) {
            std::array<std::array<float, 3>, 3> key;
            for (int i = 0; i < 3; ++i) {
                const auto& p = its.vertices[f[i]];
                key[i]        = {p.x(), p.y(), p.z()};
            }
            std::rotate(key.begin(), std::min_element(key.begin(), key.end()), key.end());
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    };
    CHECK(face_keys(mesh.its) == face_keys(repaired));
}

TEST_CASE("Bracket repair preserves exterior painting and refuses loss of painted internal partitions",
          "[ModelRepair][CGAL][SoupTopology][GUIColor][Painting]")
{
    const bool   painted_partition = GENERATE(false, true);
    TriangleMesh input;
    REQUIRE(input.ReadSTLFile((std::string(TEST_DATA_DIR) + "/model_repair_topology/bracket.stl").c_str(), false));
    Model model  = make_model(std::move(input)); // Include the usual ModelVolume centering.
    auto* volume = model.objects.front()->volumes.front();
    paint_facet(volume->mmu_segmentation_facets, volume->mesh(), painted_partition ? 0 : 2, EnforcerBlockerType::ENFORCER);
    const auto        source_mesh      = volume->mesh_ptr();
    const auto        source_painting  = volume->mmu_segmentation_facets.get_data();
    const auto        source_transform = volume->get_matrix();
    auto              options          = gui_model_repair_options();
    ModelRepairReport report;
    const bool        success = repair_model(model, report, options);
    REQUIRE(model.objects.front()->volumes.size() == 1);
    const auto* result_volume = model.objects.front()->volumes.front();
    if (painted_partition) {
        CHECK_FALSE(success);
        CHECK_FALSE(report.committed);
        CHECK(report.status() == std::string("failed"));
        REQUIRE(result_volume == volume);
        CHECK(volume->mesh_ptr() == source_mesh);
        CHECK(volume->mmu_segmentation_facets.get_data() == source_painting);
    } else {
        REQUIRE(success);
        CHECK(report.committed);
        CHECK(report.status() == std::string("repaired"));
        CHECK(result_volume->mesh().facets_count() == 856);
        CHECK(result_volume->mmu_segmentation_facets.has_facets(*result_volume, EnforcerBlockerType::ENFORCER));
        CHECK(inspect_model_mesh(result_volume->mesh())["mesh_status"] == "passed");
    }
    CHECK(result_volume->get_object() == model.objects.front());
    CHECK(result_volume->get_matrix().isApprox(source_transform));
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
    options.mesh_requires_repair      = [&](const TriangleMesh&) {
        // Inspect the unchanged closed source once; the next call must still
        // validate the repaired candidate before checking its painting remap.
        return inspection_calls++ == 0;
    };
    options.repair_mesh = [retained_component](TriangleMesh& mesh, std::string&) {
        mesh = retained_component;
        return true;
    };
    options.run_validation = [&](const std::function<bool()>& validation) {
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

    CHECK(inspection_calls == 2);
    REQUIRE(painting_validation_threads.size() == 1);
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

TEST_CASE("2.5 follows nonzero winding for equally oriented nested shells", "[ModelRepair][MeshBoolean][CGAL][Cavity]")
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

    CHECK(outer.its.indices.size() == 12);
    CHECK(absolute_shell_volume(outer) == Approx(8000.).epsilon(1.e-6));
    CHECK(its_split(outer.its).size() == 1);
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

TEST_CASE("CGAL repairs canceling winding without volume-based deletion", "[ModelRepair][CGAL][Scale]")
{
    TriangleMesh mesh = cube_with_canceling_signed_volume();
    REQUIRE(its_num_open_edges(mesh.its) > 0);
    REQUIRE(its_volume(mesh.its) == Approx(0.).margin(EPSILON));
    Model model = make_model(std::move(mesh));

    ModelRepairOptions options;

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

TEST_CASE("CGAL removes unreferenced vertices when component removal is disabled", "[ModelRepair][CGAL]")
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

    ModelRepairOptions options;
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));

    REQUIRE(model.objects.front()->volumes.size() == 1);
    const TriangleMesh& retained = model.objects.front()->volumes.front()->mesh();
    CHECK(report.committed);
    CHECK(report.mesh_repaired == 1);
    CHECK(report.removed_parts == 0);
    CHECK(retained.its.vertices.size() == 8);
    CHECK(retained.its.indices.size() == 12);
    CHECK(std::abs(its_volume(retained.its)) > 0.);
}

TEST_CASE("CGAL removes a collinear far face when component removal is disabled", "[ModelRepair][CGAL]")
{
    Model model = make_model(cube_with_collinear_far_face());

    ModelRepairOptions options;

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

TEST_CASE("Headless model repair removes a planar component inside its original volume", "[ModelRepair][CGAL][Split]")
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
    CHECK(report.split_volumes == 0);
    CHECK(report.created_parts == 0);
    CHECK(report.removed_parts == 0);
    CHECK(report.changed_object_indices == std::vector<size_t>{0});
    REQUIRE(report.volume_reports.size() == 1);
    CHECK(report.volume_reports.front().split_parts == 1);
    CHECK(report.volume_reports.front().removed_parts == 0);
}

TEST_CASE("Healthy thin components are not removed by an outer size policy", "[ModelRepair][CGAL][Scale]")
{
    Model model = make_model(cube_with_thin_component());
    const auto        original = model.objects[0]->volumes[0]->mesh_ptr();
    ModelRepairReport report;
    REQUIRE(repair_model(model, report));
    CHECK_FALSE(report.committed);
    CHECK(report.removed_parts == 0);
    REQUIRE(model.objects[0]->volumes.size() == 1);
    CHECK(model.objects[0]->volumes[0]->mesh_ptr() == original);
    CHECK(its_split(original->its).size() == 2);
}

TEST_CASE("Repair preserves a small solid shared by scaled instances", "[ModelRepair][CGAL][Scale]")
{
    Model        model  = make_model(make_cube(0.01, 0.01, 0.01));
    ModelObject* object = model.objects.front();
    object->instances.front()->set_scaling_factor(Vec3d::Constant(1000.));
    object->add_instance()->set_scaling_factor(Vec3d::Constant(0.5));
    ModelRepairReport report;
    REQUIRE(repair_model(model, report));
    CHECK_FALSE(report.committed);
    CHECK(report.failed == 0);
    CHECK(report.removed_parts == 0);
    REQUIRE(object->volumes.size() == 1);
    REQUIRE(object->instances.size() == 2);
    CHECK(object->instances[0]->get_scaling_factor().isApprox(Vec3d::Constant(1000.)));
    CHECK(object->instances[1]->get_scaling_factor().isApprox(Vec3d::Constant(0.5)));
}

TEST_CASE("CGAL fills a zero-volume open shell in the original volume", "[ModelRepair][CGAL][Scale][Split]")
{
    const int    layout = GENERATE(0, 1, 2); // Alone, sibling volume, disconnected shell in one volume.
    const bool   gui    = GENERATE(false, true);
    TriangleMesh fan    = open_tetrahedron_at_origin();
    REQUIRE(its_volume(fan.its) == Approx(0.).margin(EPSILON));
    TriangleMesh solid = make_cube(10., 10., 10.);
    solid.translate(30.f, 0.f, 0.f);
    if (layout == 2)
        fan.merge(solid);
    Model model = make_model(std::move(fan));
    if (layout == 1)
        model.objects.front()->add_volume(std::move(solid));
    ModelRepairOptions options = gui ? gui_model_repair_options() : ModelRepairOptions{};
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    CHECK(report.committed);
    CHECK(report.mesh_validated());
    CHECK(report.removed_parts == 0);
    CHECK(report.mesh_repaired == 1);
    REQUIRE(model.objects.front()->volumes.size() == (layout == 1 ? 2 : 1));
    size_t faces = 0;
    for (const auto* volume : model.objects.front()->volumes) {
        faces += volume->mesh().facets_count();
        CHECK(its_num_open_edges(volume->mesh().its) == 0);
        CHECK_FALSE(MeshBoolean::cgal::requires_repair(volume->mesh()));
    }
    CHECK(faces == (layout == 0 ? 4 : 16));
}

TEST_CASE("Unresolved zero-volume surfaces are not deleted beside a solid", "[ModelRepair][Partial][Scale]")
{
    const bool   intersecting = GENERATE(false, true);
    TriangleMesh mesh         = open_tetrahedron_at_origin();
    if (intersecting) {
        mesh        = make_cube(10., 10., 10.);
        auto second = make_cube(10., 10., 10.);
        second.translate(5.f, 0.f, 0.f);
        second.flip_triangles();
        mesh.merge(second);
    }
    REQUIRE(std::abs(its_volume(mesh.its)) <= EPSILON);
    Model      model         = make_model(std::move(mesh));
    auto*      object        = model.objects.front();
    const auto original      = object->volumes.front()->mesh_ptr();
    const auto sibling       = object->add_volume(make_cube(10., 10., 10.))->mesh_ptr();
    auto       options       = gui_model_repair_options();
    options.targets.push_back({0, size_t(0)});
    bool attempted      = false;
    options.repair_mesh = [&attempted](TriangleMesh&, std::string&) {
        attempted = true;
        return false;
    };
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));
    CHECK(attempted);
    CHECK(report.status() == std::string("unresolved"));
    CHECK(report.can_continue());
    CHECK_FALSE(report.committed);
    CHECK(report.removed_parts == 0);
    REQUIRE(object->volumes.size() == 2);
    CHECK(object->volumes.front()->mesh_ptr() == original);
    CHECK(object->volumes.back()->mesh_ptr() == sibling);
}

TEST_CASE("Headless model repair rejects a purely planar volume without deleting it", "[ModelRepair][CGAL][Split][Acceptance]")
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
    CHECK_FALSE(report.failures.front().message.empty());
}

TEST_CASE("Headless model repair preserves a rejected planar volume next to a solid", "[ModelRepair][CGAL][Split][Acceptance]")
{
    Model              model      = make_model(planar_square());
    ModelObject*       object     = model.objects.front();
    ModelVolume*       solid      = object->add_volume(make_cube(10., 10., 10.));
    const auto         solid_mesh = solid->mesh_ptr();
    ModelRepairOptions options;
    options.targets.push_back({0, size_t(0)});
    ModelRepairReport report;
    REQUIRE_FALSE(repair_model(model, report, options));
    REQUIRE(object->volumes.size() == 2);
    CHECK(object->volumes.back()->mesh_ptr() == solid_mesh);
    CHECK_FALSE(report.committed);
    CHECK(report.removed_parts == 0);
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

TEST_CASE("Geometric postcheck rejection does not roll back unrelated partial-mode progress", "[ModelRepair][Partial][Transaction]")
{
    const bool bad_first = GENERATE(false, true);
    Model      model;
    for (size_t i = 0; i < 2; ++i) {
        auto its = cube_with_open_side().its;
        if ((i == 0) == bad_first)
            its.indices.erase(its.indices.begin(), its.indices.begin() + 2);
        // Construct after editing indices so Release and Debug see the same
        // face count, and the intended rejection branch is actually exercised.
        TriangleMesh mesh(std::move(its));
        REQUIRE(mesh.facets_count() == ((i == 0) == bad_first ? 8 : 10));
        auto* object = model.add_object();
        object->add_volume(std::move(mesh));
        object->add_instance();
    }
    const size_t       bad_index = bad_first ? 0 : 1;
    const auto         original  = model.objects[bad_index]->volumes.front()->mesh_ptr();
    ModelRepairOptions options;
    options.allow_partial       = true;
    const int rejection         = GENERATE(0, 1, 2, 3);
    size_t    good_calls = 0, rejected_calls = 0;
    options.repair_mesh = [rejection, &good_calls, &rejected_calls](TriangleMesh& candidate, std::string&) {
        if (candidate.facets_count() == 10) {
            ++good_calls;
            auto its = candidate.its;
            its.indices.emplace_back(0, 1, 2);
            its.indices.emplace_back(0, 2, 3);
            candidate = TriangleMesh(std::move(its));
        } else {
            ++rejected_calls;
            REQUIRE(candidate.facets_count() == 8);
            if (rejection == 1)
                candidate = tetrahedra_sharing_an_edge();
            else if (rejection == 2)
                candidate = overlapping_cubes();
            else if (rejection == 3) {
                candidate = make_cube(10., 10., 10.);
                candidate.flip_triangles();
            } // rejection == 0: unchanged open candidate, falsely reported as success.
        }
        return true;
    };
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    CHECK(good_calls == 1);
    CHECK(rejected_calls == 1);
    CHECK(report.status() == std::string("partially_repaired"));
    CHECK(report.failed == 0);
    CHECK(report.unresolved == 1);
    CHECK(report.rolled_back == 0);
    CHECK_FALSE(report.mesh_validated());
    if (rejection == 0)
        CHECK(model.objects[bad_index]->volumes.front()->mesh_ptr() == original);
    else
        CHECK(its_num_open_edges(model.objects[bad_index]->volumes.front()->mesh().its) == 0);
    CHECK(its_num_open_edges(model.objects[1 - bad_index]->volumes.front()->mesh().its) == 0);
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

TEST_CASE("CGAL can repair an original closed degenerate edge cap in partial mode", "[ModelRepair][Partial][CGAL]")
{
    Model              model = make_model(cube_with_degenerate_edge_cap());
    ModelRepairOptions options;
    options.allow_partial    = true;
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    REQUIRE(report.mesh_validated());
    const auto& mesh = model.objects.front()->volumes.front()->mesh();
    CHECK(its_num_open_edges(mesh.its) == 0);
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(mesh));
    CHECK(its_volume(mesh.its) == Approx(1000.).epsilon(1.e-5));
    CHECK(mesh.bounding_box().size().isApprox(Vec3d(10., 10., 10.)));
}

TEST_CASE("Partial mode keeps invalid input and worker exceptions fatal", "[ModelRepair][Partial]")
{
    Model              model    = make_model(cube_with_open_side());
    auto*              object   = model.objects.front();
    const auto         original = object->volumes.front()->mesh_ptr();
    ModelRepairOptions options;
    options.allow_partial = true;
    SECTION("invalid index") { add_invalid_volume(*object); }
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
    SECTION("illegal output after reported success")
    {
        options.repair_mesh = [](TriangleMesh& mesh, std::string&) {
            mesh.its.indices.front()[0] = int(mesh.its.vertices.size());
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

TEST_CASE("Rejected repair preserves conflicting painting without fallback cleanup", "[ModelRepair][Partial][Painting]")
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
    CHECK(report.can_continue());
    CHECK(report.failed == 0);
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
    CHECK(report.rolled_back == 0);
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
        CHECK(volume_report.split_parts == 1);
        cancel = true;
    };
    options.is_canceled = [&cancel]() { return cancel; };

    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));

    CHECK(report.canceled);
    CHECK_FALSE(report.success);
    CHECK_FALSE(report.committed);
    CHECK(report.rolled_back == 0);
    CHECK(report.repaired == 0);
    CHECK(report.changed_object_indices.empty());
    REQUIRE(model.objects.front() == object);
    REQUIRE(object->volumes.size() == 1);
    CHECK(object->volumes.front() == original_volume);
    CHECK(object->volumes.front()->mesh_ptr() == original_mesh);
    CHECK(object->volumes.front()->mesh().split().size() == 2);
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

TEST_CASE("App automatic repair avoids redundant full inspection and retains basic guards", "[ModelRepair][AppBasic]")
{
    Model              model;
    auto*              object = model.add_object();
    ModelRepairOptions options;
    options.basic_repair           = true;
    size_t inspected               = 0;
    options.mesh_requires_repair   = [&](const TriangleMesh&) {
        ++inspected;
        return true;
    };
    SECTION("closed intersecting shells attempt repair once and preserve an unchanged source")
    {
        auto mesh = load_model("model_repair_acceptance/08-intersecting-shells.obj");
        REQUIRE(its_num_open_edges(mesh.its) == 0);
        object->add_volume(mesh);
        size_t attempts = 0;
        options.repair_mesh = [&](TriangleMesh&, std::string&) -> bool {
            ++attempts;
            return false;
        };
        ModelRepairReport report;
        CHECK_FALSE(repair_model(model, report, options));
        CHECK(attempts == 1);
        CHECK_FALSE(report.committed);
        CHECK(report.skipped == 0);
        CHECK(inspected == 0);
        CHECK(model.objects.front()->volumes.front()->mesh().its.indices == mesh.its.indices);
    }
    SECTION("cancellation after a skipped part remains observable")
    {
        object->add_volume(make_cube(10., 10., 10.));
        bool canceled       = false;
        options.is_canceled = [&]() { return canceled; };
        options.on_progress = [&](size_t, size_t, const ModelRepairVolumeReport&) { canceled = true; };
        ModelRepairReport report;
        CHECK_FALSE(repair_model(model, report, options));
        CHECK(report.canceled);
        CHECK_FALSE(report.committed);
    }
    SECTION("a successful closed candidate does not repeat full inspection")
    {
        const auto source = cube_with_open_side();
        object->add_volume(source);
        options.repair_mesh = [](TriangleMesh& mesh, std::string&) {
            mesh = make_cube(10., 10., 10.);
            return true;
        };
        ModelRepairReport report;
        CHECK(repair_model(model, report, options));
        CHECK(inspected == 0);
        CHECK(its_num_open_edges(model.objects.front()->volumes.front()->mesh().its) == 0);
    }
    SECTION("an open candidate is rejected without changing the model")
    {
        const auto source = cube_with_open_side();
        object->add_volume(source);
        options.repair_mesh = [](TriangleMesh&, std::string&) { return true; };
        ModelRepairReport report;
        CHECK_FALSE(repair_model(model, report, options));
        CHECK(inspected == 0);
        CHECK(model.objects.front()->volumes.front()->mesh().its.indices == source.its.indices);
    }
    SECTION("invalid candidate storage still fails")
    {
        const auto source = cube_with_open_side();
        object->add_volume(source);
        options.repair_mesh = [](TriangleMesh& mesh, std::string&) {
            mesh.its.vertices.clear();
            return true;
        };
        ModelRepairReport report;
        CHECK_FALSE(repair_model(model, report, options));
        CHECK(model.objects.front()->volumes.front()->mesh().its.indices == source.its.indices);
    }
    SECTION("default callers still independently validate closed candidates")
    {
        options.basic_repair           = false;
        const auto source              = cube_with_open_side();
        object->add_volume(source);
        options.repair_mesh = [](TriangleMesh& mesh, std::string&) {
            mesh = make_cube(10., 10., 10.);
            return true;
        };
        ModelRepairReport report;
        CHECK_FALSE(repair_model(model, report, options));
        CHECK(inspected > 0);
        CHECK(model.objects.front()->volumes.front()->mesh().its.indices == source.its.indices);
    }
}

TEST_CASE("App volume admission and residual checks use the same defect rules", "[ModelRepair][AppBasic]")
{
    const auto shift = GENERATE(Vec3f(5.f, 5.f, 5.f), Vec3f(10.f, 10.f, 0.f), Vec3f(10.f, 10.f, 10.f));
    auto mesh = make_cube(10., 10., 10.);
    auto other = mesh;
    other.translate(shift);
    mesh.merge(other);
    Model model = make_model(mesh);
    ModelRepairOptions options;
    options.basic_repair = true;
    size_t attempts = 0;
    options.repair_mesh = [&](TriangleMesh& candidate, std::string&) {
        ++attempts;
        // Isolate policy/transaction behavior from the geometry algorithm.
        candidate = make_cube(10., 10., 10.);
        auto separate = candidate;
        separate.translate(20.f, 0.f, 0.f);
        candidate.merge(separate);
        return true;
    };
    options.mesh_requires_repair = [](const TriangleMesh&) -> bool {
        FAIL("App must not invoke the full inspector");
        return false;
    };
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    CHECK(attempts == 1);
    CHECK(report.committed);
    ModelInspectionOptions inspection;
    inspection.analyze_components = false;
    CHECK(inspect_model_mesh(model.mesh(), inspection)["mesh_status"] == "passed");
}

TEST_CASE("App reports unresolved coincident closed shells without false repair progress", "[ModelRepair][AppBasic]")
{
    auto mesh = make_cube(10., 10., 10.);
    mesh.merge(TriangleMesh(mesh));
    Model              model = make_model(mesh);
    ModelRepairOptions options;
    options.basic_repair        = true;
    options.allow_partial       = true;
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));
    CHECK(report.can_continue());
    CHECK_FALSE(report.committed);
    CHECK(report.status() == std::string("unresolved"));
    CHECK(report.attempted);
    CHECK(model.objects[0]->volumes[0]->mesh().its.indices.size() == 24);
}

TEST_CASE("2.5 repair keeps closed disjoint solids in one model volume", "[ModelRepair][Repair250]")
{
    const bool        inward = GENERATE(false, true);
    Model             model  = make_model(two_disjoint_shells(inward));
    const auto*       before = model.objects[0]->volumes[0];
    const auto        matrix = before->get_matrix();
    ModelRepairReport report;
    REQUIRE(repair_model(model, report));
    REQUIRE(model.objects[0]->volumes.size() == 1);
    CHECK(model.objects[0]->volumes[0]->get_matrix().isApprox(matrix));
    CHECK(its_split(model.objects[0]->volumes[0]->mesh().its).size() == 2);
    CHECK_FALSE(MeshBoolean::cgal::requires_repair(model.objects[0]->volumes[0]->mesh()));
    CHECK(report.split_volumes == 0);
    CHECK(report.created_parts == 0);
    CHECK(report.committed == inward);
}

TEST_CASE("2.5 partial repair retains closure without claiming intersecting shells valid", "[ModelRepair][Repair250][Partial]")
{
    auto mesh = overlapping_cubes();
    mesh.its.indices.erase(mesh.its.indices.begin(), mesh.its.indices.begin() + 2);
    Model              model   = make_model(mesh);
    const auto         matrix  = model.objects[0]->volumes[0]->get_matrix();
    ModelRepairOptions options = gui_model_repair_options();
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    CHECK(report.committed);
    CHECK(report.can_continue());
    CHECK_FALSE(report.mesh_validated());
    CHECK(std::string(report.status()) == "partially_repaired");
    REQUIRE(model.objects[0]->volumes.size() == 1);
    const auto& result = model.objects[0]->volumes[0]->mesh();
    CHECK(its_num_open_edges(result.its) == 0);
    CHECK(MeshBoolean::cgal::requires_repair(result));
    CHECK(model.objects[0]->volumes[0]->get_matrix().isApprox(matrix));
}

TEST_CASE("2.5 unchanged residual intersections do not create a repair transaction", "[ModelRepair][Repair250][Partial]")
{
    Model              model    = make_model(overlapping_cubes());
    auto*              original = model.objects[0]->volumes[0];
    const auto         geometry = original->mesh_ptr();
    const auto         id       = original->id();
    ModelRepairOptions options  = gui_model_repair_options();
    ModelRepairReport  report;
    CHECK_FALSE(repair_model(model, report, options));
    CHECK(report.can_continue());
    CHECK_FALSE(report.committed);
    CHECK(std::string(report.status()) == "unresolved");
    CHECK(model.objects[0]->volumes[0] == original);
    CHECK(original->mesh_ptr() == geometry);
    CHECK(original->id() == id);
}

TEST_CASE("2.5 geometric rejection keeps source volume beside successful repair", "[ModelRepair][Repair250][Partial]")
{
    Model model = make_model(cube_with_open_side());
    model.objects[0]->add_volume(planar_square());
    const auto         original = model.objects[0]->volumes[1]->mesh_ptr();
    ModelRepairOptions options  = gui_model_repair_options();
    options.basic_repair        = GENERATE(false, true);
    ModelRepairReport report;
    REQUIRE(repair_model(model, report, options));
    CHECK(report.can_continue());
    CHECK_FALSE(report.mesh_validated());
    REQUIRE(model.objects[0]->volumes.size() == 2);
    CHECK(its_num_open_edges(model.objects[0]->volumes[0]->mesh().its) == 0);
    CHECK(model.objects[0]->volumes[1]->mesh_ptr() == original);
}

TEST_CASE("2.5 repair does not count triangle reordering as an improvement", "[ModelRepair][Repair250][Partial]")
{
    const bool         cancel_during_validation = GENERATE(false, true);
    Model              model                    = make_model(overlapping_cubes());
    const auto         original                 = model.objects[0]->volumes[0]->mesh_ptr();
    ModelRepairOptions options                  = gui_model_repair_options();
    options.repair_mesh                         = [](TriangleMesh& mesh, std::string&) {
        std::reverse(mesh.its.indices.begin(), mesh.its.indices.end());
        return true;
    };
    const auto                   caller_thread = std::this_thread::get_id();
    std::vector<std::thread::id> validation_threads;
    bool                         canceled = false;
    options.is_canceled                   = [&]() { return canceled; };
    options.run_validation                = [&](const std::function<bool()>& validation) {
        bool        result = false;
        std::thread worker([&]() {
            validation_threads.push_back(std::this_thread::get_id());
            result = validation();
        });
        worker.join();
        if (cancel_during_validation && validation_threads.size() == 2)
            canceled = true;
        return result;
    };
    ModelRepairReport report;
    CHECK_FALSE(repair_model(model, report, options));
    // Both the bad-face comparison and exact surface comparison use the runner.
    REQUIRE(validation_threads.size() == 2);
    for (const auto thread : validation_threads)
        CHECK(thread != caller_thread);
    CHECK(report.canceled == cancel_during_validation);
    CHECK(report.can_continue() == !cancel_during_validation);
    CHECK_FALSE(report.committed);
    CHECK(model.objects[0]->volumes[0]->mesh_ptr() == original);
}
