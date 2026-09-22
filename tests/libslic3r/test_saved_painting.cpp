#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>

#include <test_utils.hpp>

#include <libslic3r/CutUtils.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/TriangleSelector.hpp>

using namespace Slic3r;

namespace {

double painted_surface_area(const indexed_triangle_set& mesh)
{
    double area = 0.;
    for (const auto& face : mesh.indices) {
        const Vec3d a = mesh.vertices[face[0]].cast<double>();
        const Vec3d b = mesh.vertices[face[1]].cast<double>();
        const Vec3d c = mesh.vertices[face[2]].cast<double>();
        area += 0.5 * (b - a).cross(c - a).norm();
    }
    return area;
}

ModelVolume* make_volume(Model& model, TriangleMesh mesh)
{
    ModelObject* object = model.add_object();
    return object->add_volume(std::move(mesh));
}

void paint_facet(FacetsAnnotation& annotation, const TriangleMesh& mesh, int facet, EnforcerBlockerType state)
{
    TriangleSelector selector(mesh);
    selector.set_facet(facet, state);
    REQUIRE(annotation.set(selector));
}

void paint_all_facets(FacetsAnnotation& annotation, const TriangleMesh& mesh, EnforcerBlockerType state)
{
    TriangleSelector selector(mesh);
    for (size_t facet = 0; facet < mesh.its.indices.size(); ++facet)
        selector.set_facet(static_cast<int>(facet), state);
    REQUIRE(annotation.set(selector));
}

TriangleMesh two_cubes()
{
    TriangleMesh mesh   = make_cube(10., 10., 10.);
    TriangleMesh second = make_cube(10., 10., 10.);
    second.translate(30.f, 0.f, 0.f);
    mesh.merge(second);
    return mesh;
}

} // namespace

TEST_CASE("SavedPainting restores every supported facet annotation", "[SavedPainting]")
{
    Model        model;
    ModelVolume* volume = make_volume(model, make_cube(10., 10., 10.));

    paint_facet(volume->supported_facets, volume->mesh(), 0, EnforcerBlockerType::ENFORCER);
    paint_facet(volume->seam_facets, volume->mesh(), 1, EnforcerBlockerType::BLOCKER);
    paint_facet(volume->mmu_segmentation_facets, volume->mesh(), 2, EnforcerBlockerType::Extruder3);
    paint_facet(volume->fuzzy_skin_facets, volume->mesh(), 3, EnforcerBlockerType::FUZZY_SKIN);

    const std::shared_ptr<const TriangleMesh> original_mesh = volume->mesh_ptr();
    const auto                                saved         = volume->save_painting();
    REQUIRE(saved.has_value());
    CHECK(saved->mesh == original_mesh);

    TriangleMesh replacement = volume->mesh();
    // A repaired mesh is already expressed in the saved mesh's local coordinates.
    replacement.set_init_shift(Vec3d::Zero());
    volume->set_mesh(std::move(replacement));
    volume->restore_painting(saved);

    CHECK(volume->supported_facets.has_facets(*volume, EnforcerBlockerType::ENFORCER));
    CHECK(volume->seam_facets.has_facets(*volume, EnforcerBlockerType::BLOCKER));
    CHECK(volume->mmu_segmentation_facets.has_facets(*volume, EnforcerBlockerType::Extruder3));
    CHECK(volume->fuzzy_skin_facets.has_facets(*volume, EnforcerBlockerType::FUZZY_SKIN));
}

TEST_CASE("SavedPainting can merge with or clear existing target painting", "[SavedPainting]")
{
    Model        source_model;
    ModelVolume* source = make_volume(source_model, two_cubes());
    paint_facet(source->supported_facets, source->mesh(), 0, EnforcerBlockerType::ENFORCER);
    const auto saved = source->save_painting();
    REQUIRE(saved.has_value());

    Model        target_model;
    ModelVolume* target = make_volume(target_model, two_cubes());
    paint_facet(target->supported_facets, target->mesh(), 12, EnforcerBlockerType::BLOCKER);
    TriangleMesh target_mesh = target->mesh();
    // Avoid treating the volume's original import shift as a remapping offset.
    target_mesh.set_init_shift(Vec3d::Zero());
    target->set_mesh(std::move(target_mesh));
    target->restore_painting(saved, true);

    CHECK(target->supported_facets.has_facets(*target, EnforcerBlockerType::ENFORCER));
    CHECK(target->supported_facets.has_facets(*target, EnforcerBlockerType::BLOCKER));

    target->restore_painting(std::nullopt, false);
    CHECK(target->supported_facets.empty());
}

TEST_CASE("SavedPainting follows a facet whose normal was reversed", "[SavedPainting][Orientation]")
{
    Model        model;
    ModelVolume* volume = make_volume(model, make_cube(10., 10., 10.));
    paint_facet(volume->supported_facets, volume->mesh(), 0, EnforcerBlockerType::ENFORCER);

    const auto saved = volume->save_painting();
    REQUIRE(saved.has_value());

    TriangleMesh replacement = volume->mesh();
    replacement.flip_triangles();
    replacement.set_init_shift(Vec3d::Zero());
    volume->set_mesh(std::move(replacement));
    volume->restore_painting(saved);

    CHECK(volume->supported_facets.has_facets(*volume, EnforcerBlockerType::ENFORCER));
}

TEST_CASE("SavedPainting does not leak onto a nearby opposite thin wall", "[SavedPainting][Orientation][ThinWall]")
{
    indexed_triangle_set source_its;
    source_its.vertices = {Vec3f(0.f, 0.f, 0.f), Vec3f(10.f, 0.f, 0.f), Vec3f(0.f, 10.f, 0.f)};
    source_its.indices  = {Vec3i32(0, 1, 2)};

    TriangleMesh     source_mesh(source_its);
    TriangleSelector source_selector(source_mesh);
    source_selector.set_facet(0, EnforcerBlockerType::ENFORCER);
    const TriangleSelector::TriangleSplittingData source_painting = source_selector.serialize();

    indexed_triangle_set target_its;
    target_its.vertices = {Vec3f(0.f, 0.f, 0.f),    Vec3f(10.f, 0.f, 0.f),    Vec3f(0.f, 10.f, 0.f),
                           Vec3f(0.f, 0.f, 0.005f), Vec3f(10.f, 0.f, 0.005f), Vec3f(0.f, 10.f, 0.005f)};
    // Both candidates reverse the source normal. Only the first one is
    // coplanar; the second represents the opposite side of a thin wall.
    target_its.indices = {Vec3i32(0, 2, 1), Vec3i32(3, 5, 4)};

    const TriangleSelector::TriangleSplittingData remapped = TriangleSelector::remap_painting(source_its, source_painting, target_its,
                                                                                              Transform3d::Identity(), std::nullopt);
    TriangleMesh                                  target_mesh(target_its);
    TriangleSelector                              target_selector(target_mesh);
    target_selector.deserialize(remapped, false);

    CHECK(target_selector.num_facets(EnforcerBlockerType::ENFORCER) == 1);
}

TEST_CASE("SavedPainting does not expand across touching facets", "[SavedPainting][Boundary][Provenance]")
{
    const bool reversed       = GENERATE(false, true);
    const bool merge_existing = GENERATE(false, true);
    const int  axis           = GENERATE(0, 1, 2);
    const auto state          = GENERATE(EnforcerBlockerType::ENFORCER, EnforcerBlockerType::BLOCKER, EnforcerBlockerType::Extruder3,
                                         EnforcerBlockerType::Extruder16);
    CAPTURE(reversed, merge_existing, axis, int(state));
    const auto point = [axis](float u, float v) {
        Vec3f p(20.f, 30.f, 40.f);
        p((axis + 1) % 3) += u;
        p((axis + 2) % 3) += v;
        return p;
    };
    indexed_triangle_set source_its;
    source_its.vertices = {point(0.f, 0.f), point(10.f, 0.f), point(0.f, 10.f)};
    source_its.indices  = {Vec3i32(0, 1, 2)};
    const TriangleMesh source_mesh(source_its);
    TriangleSelector   source_selector(source_mesh);
    source_selector.set_facet(0, state);
    auto painting = source_selector.serialize();

    indexed_triangle_set target_its = source_its;
    target_its.vertices.insert(target_its.vertices.end(), {point(10.f, 10.f), point(-10.f, 0.f), point(0.f, -10.f)});
    // The painted face is reindexed. Face 0 shares an entire edge; face 2
    // shares only one vertex. Neither has any painted area in the source.
    target_its.indices = {Vec3i32(1, 3, 2), Vec3i32(0, 1, 2), Vec3i32(0, 4, 5)};
    if (reversed)
        for (Vec3i32& face : target_its.indices)
            std::swap(face(1), face(2));
    const Vec3f shift(4.f, -8.f, 16.f);
    for (Vec3f& p : target_its.vertices)
        p -= shift;
    Transform3d target_transform   = Transform3d::Identity();
    target_transform.translation() = shift.cast<double>();

    const TriangleMesh target_mesh(target_its);
    TriangleSelector   existing_selector(target_mesh);
    const auto         other_state = state == EnforcerBlockerType::BLOCKER ? EnforcerBlockerType::Extruder3 : EnforcerBlockerType::BLOCKER;
    if (merge_existing)
        existing_selector.set_facet(0, other_state);
    const auto                                                                           existing = existing_selector.serialize();
    std::optional<std::reference_wrapper<const TriangleSelector::TriangleSplittingData>> existing_ref;
    if (merge_existing)
        existing_ref = std::cref(existing);
    existing_selector.set_facet(1, state);
    const auto expected = existing_selector.serialize();

    // Repeated repairs must not turn a boundary touch into an ever-growing
    // strip, or overwrite a different color already present on the neighbor.
    for (int round = 0; round < 3; ++round) {
        CAPTURE(round);
        painting = TriangleSelector::remap_painting(source_its, painting, target_its, target_transform, existing_ref);
        CHECK(painting.triangles_to_split == expected.triangles_to_split);
        CHECK(painting.bitstream == expected.bitstream);
        TriangleSelector restored(target_mesh);
        restored.deserialize(painting, false);
        auto painted = restored.get_facets_strict(state);
        REQUIRE(!painted.indices.empty());
        // get_facets_strict keeps the selector's live vertices, including
        // vertices used only by other states. Measure the selected surface,
        // not that backing vertex pool. Compact only this extracted copy;
        // no triangles are removed and no coordinates are changed.
        its_compactify_vertices(painted);
        CHECK(painted.vertices.size() == 3);
        TriangleMesh painted_mesh(std::move(painted));
        painted_mesh.translate(shift);
        CHECK(painted_surface_area(painted_mesh.its) == Approx(50.).epsilon(1.e-6));
        CHECK(bounding_box(painted_mesh.its).min.isApprox(bounding_box(source_mesh.its).min));
        CHECK(bounding_box(painted_mesh.its).max.isApprox(bounding_box(source_mesh.its).max));
        source_its       = target_its;
        target_transform = Transform3d::Identity();
    }
}

TEST_CASE("SavedPainting still transfers to coplanar retriangulated facets", "[SavedPainting][Boundary]")
{
    const bool reversed = GENERATE(false, true);
    CAPTURE(reversed);
    indexed_triangle_set source_its;
    source_its.vertices = {Vec3f(0.f, 0.f, 0.f), Vec3f(10.f, 0.f, 0.f), Vec3f(0.f, 10.f, 0.f)};
    source_its.indices  = {Vec3i32(0, 1, 2)};
    const TriangleMesh source_mesh(source_its);
    TriangleSelector   source_selector(source_mesh);
    source_selector.set_facet(0, EnforcerBlockerType::ENFORCER);

    // Four triangles occupy the original painted triangle. Use separate
    // vertex indices so all four must be found without neighbor propagation.
    const std::array<Vec3f, 6>   vertices{source_its.vertices[0], source_its.vertices[1], source_its.vertices[2],
                                        Vec3f(5.f, 0.f, 0.f),   Vec3f(5.f, 5.f, 0.f),   Vec3f(0.f, 5.f, 0.f)};
    const std::array<Vec3i32, 4> faces{Vec3i32(0, 3, 5), Vec3i32(3, 1, 4), Vec3i32(5, 4, 2), Vec3i32(3, 4, 5)};
    indexed_triangle_set         target_its;
    for (const Vec3i32& face : faces) {
        const int offset = int(target_its.vertices.size());
        for (int corner = 0; corner < 3; ++corner)
            target_its.vertices.push_back(vertices[face(corner)]);
        target_its.indices.emplace_back(offset, offset + (reversed ? 2 : 1), offset + (reversed ? 1 : 2));
    }
    const auto remapped = TriangleSelector::remap_painting(source_its, source_selector.serialize(), target_its, Transform3d::Identity(),
                                                           std::nullopt);
    const TriangleMesh target_mesh(target_its);
    TriangleSelector   target_selector(target_mesh);
    target_selector.deserialize(remapped, false);
    CHECK(target_selector.num_facets(EnforcerBlockerType::ENFORCER) == 4);
    auto painted = target_selector.get_facets_strict(EnforcerBlockerType::ENFORCER);
    REQUIRE(!painted.indices.empty());
    its_compactify_vertices(painted);
    CHECK(painted_surface_area(painted) == Approx(50.).epsilon(1.e-6));
    CHECK(bounding_box(painted).min.isApprox(bounding_box(source_its).min));
    CHECK(bounding_box(painted).max.isApprox(bounding_box(source_its).max));
}

TEST_CASE("Splitting a volume remaps painting to displaced components", "[SavedPainting][ModelVolume]")
{
    Model        model;
    ModelVolume* volume = make_volume(model, two_cubes());

    TriangleSelector selector(volume->mesh());
    selector.set_facet(0, EnforcerBlockerType::Extruder1);
    selector.set_facet(12, EnforcerBlockerType::Extruder2);
    REQUIRE(volume->mmu_segmentation_facets.set(selector));

    REQUIRE(volume->split(4, true) == 2);
    REQUIRE(model.objects.front()->volumes.size() == 2);
    CHECK(model.objects.front()->volumes[0]->is_mm_painted());
    CHECK(model.objects.front()->volumes[1]->is_mm_painted());
}

TEST_CASE("Plane cut keeps painted facet annotations when requested", "[SavedPainting][Cut]")
{
    Model        model;
    ModelObject* object = model.add_object();
    ModelVolume* volume = object->add_volume(make_cube(10., 10., 10.));
    object->add_instance();
    volume->set_offset(volume->get_offset() + Vec3d(20., 0., 0.));

    paint_all_facets(volume->supported_facets, volume->mesh(), EnforcerBlockerType::ENFORCER);
    paint_all_facets(volume->seam_facets, volume->mesh(), EnforcerBlockerType::BLOCKER);
    paint_all_facets(volume->mmu_segmentation_facets, volume->mesh(), EnforcerBlockerType::Extruder3);
    paint_all_facets(volume->fuzzy_skin_facets, volume->mesh(), EnforcerBlockerType::FUZZY_SKIN);

    const ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower |
                                                ModelObjectCutAttribute::KeepPaint;
    Cut cut(object, 0, Geometry::translation_transform(Vec3d(0., 0., 5.)), attributes);
    const ModelObjectPtrs& results = cut.perform_with_plane();

    REQUIRE(results.size() == 2);
    for (const ModelObject* result : results) {
        REQUIRE(result != nullptr);
        REQUIRE(result->volumes.size() == 1);
        const ModelVolume* result_volume = result->volumes.front();
        CHECK(result_volume->supported_facets.has_facets(*result_volume, EnforcerBlockerType::ENFORCER));
        CHECK(result_volume->seam_facets.has_facets(*result_volume, EnforcerBlockerType::BLOCKER));
        CHECK(result_volume->mmu_segmentation_facets.has_facets(*result_volume, EnforcerBlockerType::Extruder3));
        CHECK(result_volume->fuzzy_skin_facets.has_facets(*result_volume, EnforcerBlockerType::FUZZY_SKIN));
    }
}

TEST_CASE("Contour cut restores painting from the original object", "[SavedPainting][Cut][Contour]")
{
    Model        source_model;
    ModelObject* source_object = source_model.add_object();
    ModelVolume* source_volume = source_object->add_volume(make_cube(10., 10., 10.));
    source_object->add_instance();
    paint_all_facets(source_volume->supported_facets, source_volume->mesh(), EnforcerBlockerType::ENFORCER);

    const Transform3d cut_matrix = Geometry::translation_transform(Vec3d(0., 0., 5.));

    // PartSelection first cuts to temporary volumes without preserving paint.
    Cut                    initial_cut(source_object, 0, cut_matrix);
    const ModelObjectPtrs& initial_results = initial_cut.perform_with_plane();
    REQUIRE(initial_results.size() == 1);
    REQUIRE(initial_results.front()->volumes.size() == 2);
    CHECK(initial_results.front()->volumes[0]->supported_facets.empty());
    CHECK(initial_results.front()->volumes[1]->supported_facets.empty());

    Model                  part_model;
    ModelObject*           part_object = part_model.add_object(*initial_results.front());
    std::vector<Cut::Part> parts{{true, false}, {false, false}};

    const ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower |
                                                ModelObjectCutAttribute::KeepPaint;
    Cut                    contour_cut(part_object, 0, cut_matrix, attributes);
    const ModelObjectPtrs& results = contour_cut.perform_by_contour(source_object, std::move(parts), 0);

    REQUIRE(results.size() == 2);
    for (const ModelObject* result : results) {
        REQUIRE(result != nullptr);
        REQUIRE(result->volumes.size() == 1);
        const ModelVolume* result_volume = result->volumes.front();
        CHECK(result_volume->supported_facets.has_facets(*result_volume, EnforcerBlockerType::ENFORCER));
    }
}

TEST_CASE("Groove cut keeps painted facet annotations", "[SavedPainting][Cut][Groove]")
{
    Model        model;
    ModelObject* object = model.add_object();
    ModelVolume* volume = object->add_volume(make_cube(40., 40., 40.));
    object->add_instance();
    paint_all_facets(volume->seam_facets, volume->mesh(), EnforcerBlockerType::ENFORCER);

    Cut::Groove groove;
    groove.depth       = 5.f;
    groove.width       = 10.f;
    groove.flaps_angle = float(PI) / 3.f;
    groove.angle       = 0.f;

    const ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower |
                                                ModelObjectCutAttribute::KeepPaint;
    Cut                    cut(object, 0, Geometry::translation_transform(Vec3d(0., 0., 20.)), attributes);
    const ModelObjectPtrs& results = cut.perform_with_groove(groove, Transform3d::Identity());

    REQUIRE(results.size() == 2);
    for (const ModelObject* result : results) {
        REQUIRE(result != nullptr);
        REQUIRE_FALSE(result->volumes.empty());
        CHECK(std::any_of(result->volumes.begin(), result->volumes.end(), [](const ModelVolume* result_volume) {
            return result_volume->seam_facets.has_facets(*result_volume, EnforcerBlockerType::ENFORCER);
        }));
    }
}

TEST_CASE("Plane cut clears painted facet annotations by default", "[SavedPainting][Cut]")
{
    Model        model;
    ModelObject* object = model.add_object();
    ModelVolume* volume = object->add_volume(make_cube(10., 10., 10.));
    object->add_instance();
    paint_all_facets(volume->supported_facets, volume->mesh(), EnforcerBlockerType::ENFORCER);

    const ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower;
    Cut cut(object, 0, Geometry::translation_transform(Vec3d(0., 0., 5.)), attributes);
    const ModelObjectPtrs& results = cut.perform_with_plane();

    REQUIRE(results.size() == 2);
    for (const ModelObject* result : results) {
        REQUIRE(result != nullptr);
        REQUIRE(result->volumes.size() == 1);
        CHECK(result->volumes.front()->supported_facets.empty());
    }
}

TEST_CASE("Plane cut cannot borrow annotations from another source volume", "[SavedPainting][Cut][Provenance]")
{
    const double offset        = GENERATE(0., 5., 10., 15.); // coincident, overlapping, touching, separated
    const bool   keep_as_parts = GENERATE(false, true);
    Model        model;
    ModelObject* object = model.add_object();
    object->add_instance();
    ModelVolume* painted = object->add_volume(make_cube(10., 10., 10.));
    ModelVolume* plain   = object->add_volume(make_cube(10., 10., 10.));
    plain->translate(Vec3d(offset, 0., 0.));
    paint_all_facets(painted->supported_facets, painted->mesh(), EnforcerBlockerType::ENFORCER);
    paint_all_facets(painted->seam_facets, painted->mesh(), EnforcerBlockerType::BLOCKER);
    paint_all_facets(painted->mmu_segmentation_facets, painted->mesh(), EnforcerBlockerType::Extruder3);
    paint_all_facets(painted->fuzzy_skin_facets, painted->mesh(), EnforcerBlockerType::FUZZY_SKIN);
    ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower |
                                          ModelObjectCutAttribute::KeepPaint;
    if (keep_as_parts)
        attributes = attributes | ModelObjectCutAttribute::KeepAsParts;
    Cut         cut(object, 0, Geometry::translation_transform(Vec3d(0., 0., 5.)), attributes);
    const auto& results = cut.perform_with_plane();
    REQUIRE(results.size() == (keep_as_parts ? 1 : 2));
    size_t painted_parts = 0;
    size_t plain_parts   = 0;
    for (const ModelObject* result : results)
        for (const ModelVolume* volume : result->volumes) {
            if (volume->cut_info.painting_source_id == painted->id()) {
                ++painted_parts;
                CHECK(volume->supported_facets.has_facets(*volume, EnforcerBlockerType::ENFORCER));
                CHECK(volume->seam_facets.has_facets(*volume, EnforcerBlockerType::BLOCKER));
                CHECK(volume->mmu_segmentation_facets.has_facets(*volume, EnforcerBlockerType::Extruder3));
                CHECK(volume->fuzzy_skin_facets.has_facets(*volume, EnforcerBlockerType::FUZZY_SKIN));
            } else {
                REQUIRE(volume->cut_info.painting_source_id == plain->id());
                ++plain_parts;
                CHECK(volume->supported_facets.empty());
                CHECK(volume->seam_facets.empty());
                CHECK(volume->mmu_segmentation_facets.empty());
                CHECK(volume->fuzzy_skin_facets.empty());
            }
        }
    CHECK(painted_parts == 2);
    CHECK(plain_parts == 2);
}

TEST_CASE("Contour cut preserves source identity through preview splitting and merged annotations",
          "[SavedPainting][Cut][Contour][Provenance]")
{
    Model        model;
    ModelObject* object = model.add_object();
    object->add_instance();
    // The second painted island forces the preview through ModelVolume::split.
    ModelVolume* painted = object->add_volume(two_cubes());
    ModelVolume* plain   = object->add_volume(make_cube(10., 10., 10.));
    plain->translate(Vec3d(10., 0., 0.));
    paint_all_facets(painted->mmu_segmentation_facets, painted->mesh(), EnforcerBlockerType::Extruder3);
    const Transform3d plane = Geometry::translation_transform(Vec3d(0., 0., 5.));
    Cut               preview(object, 0, plane);
    Model             part_model;
    ModelObject*      part_object = part_model.add_object(*preview.perform_with_plane().front());
    for (int i = int(part_object->volumes.size()) - 1; i >= 0; --i)
        part_object->volumes[size_t(i)]->split(1, false);
    std::vector<Cut::Part> parts;
    size_t                 painted_upper_faces = 0;
    size_t                 painted_lower_faces = 0;
    for (const ModelVolume* volume : part_object->volumes) {
        const bool upper = volume->is_from_upper();
        parts.push_back({upper, false});
        if (volume->cut_info.painting_source_id == painted->id())
            (upper ? painted_upper_faces : painted_lower_faces) += volume->mesh().its.indices.size();
    }
    REQUIRE(painted_upper_faces > 0);
    REQUIRE(painted_lower_faces > 0);
    const ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower |
                                                ModelObjectCutAttribute::KeepPaint;
    Cut         cut(part_object, 0, plane, attributes);
    const auto& results = cut.perform_by_contour(object, std::move(parts), 0);
    REQUIRE(results.size() == 2);
    for (size_t i = 0; i < results.size(); ++i) {
        REQUIRE(results[i]->volumes.size() == 1);
        const ModelVolume* volume = results[i]->volumes.front();
        REQUIRE(volume->mmu_segmentation_facets.has_facets(*volume, EnforcerBlockerType::Extruder3));
        const size_t painted_faces = i == 0 ? painted_upper_faces : painted_lower_faces;
        REQUIRE(volume->mesh().its.indices.size() > painted_faces);
        for (const auto& mapping : volume->mmu_segmentation_facets.get_data().triangles_to_split) {
            CHECK(mapping.triangle_idx >= 0);
            CHECK(size_t(mapping.triangle_idx) < painted_faces);
        }
    }
}

TEST_CASE("Groove subcuts keep the original painting source identity", "[SavedPainting][Cut][Groove][Provenance]")
{
    Model        model;
    ModelObject* object = model.add_object();
    object->add_instance();
    ModelVolume* painted = object->add_volume(make_cube(40., 40., 40.));
    ModelVolume* plain   = object->add_volume(make_cube(40., 40., 40.));
    plain->translate(Vec3d(40., 0., 0.));
    paint_all_facets(painted->mmu_segmentation_facets, painted->mesh(), EnforcerBlockerType::Extruder3);
    Cut::Groove groove;
    groove.depth                              = 5.f;
    groove.width                              = 10.f;
    groove.flaps_angle                        = float(PI) / 3.f;
    const ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower |
                                                ModelObjectCutAttribute::KeepPaint;
    Cut         cut(object, 0, Geometry::translation_transform(Vec3d(0., 0., 20.)), attributes);
    const auto& results       = cut.perform_with_groove(groove, Transform3d::Identity(), true);
    size_t      painted_count = 0;
    size_t      plain_count   = 0;
    for (const ModelObject* result : results)
        for (const ModelVolume* volume : result->volumes) {
            if (volume->cut_info.painting_source_id == plain->id()) {
                ++plain_count;
                CHECK(volume->mmu_segmentation_facets.empty());
            } else {
                REQUIRE(volume->cut_info.painting_source_id == painted->id());
                if (volume->mmu_segmentation_facets.has_facets(*volume, EnforcerBlockerType::Extruder3))
                    ++painted_count;
            }
        }
    CHECK(painted_count > 0);
    CHECK(plain_count > 0);
}
