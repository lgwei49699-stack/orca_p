#include <catch2/catch.hpp>

#include <test_utils.hpp>

#include <libslic3r/CutUtils.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/TriangleSelector.hpp>

using namespace Slic3r;

namespace {

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
