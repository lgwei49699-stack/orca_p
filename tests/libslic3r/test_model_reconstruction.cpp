#include <catch2/catch.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/ModelReconstruction.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <cstdlib>

using namespace Slic3r;

namespace {
Model reconstruction_model()
{
    Model model;
    auto* object = model.add_object();
    object->name = "reconstruction source";
    object->add_volume(make_cube(10., 10., 10.));
    object->add_instance();
    return model;
}
} // namespace

TEST_CASE("Reconstruction eligibility never clears unsupported data", "[ModelReconstruction]")
{
    Model model  = reconstruction_model();
    auto& object = *model.objects.front();
    REQUIRE(model_reconstruction_ineligibility(object).empty());
    SECTION("multiple instances") { object.add_instance(); }
    SECTION("modifier") { object.volumes.front()->set_type(ModelVolumeType::PARAMETER_MODIFIER); }
    SECTION("multiple volumes") { object.add_volume(make_cube(1., 1., 1.)); }
    SECTION("layer profile") { object.layer_height_profile.set({0., 0.2, 10., 0.1}); }
    SECTION("connector") { object.volumes.front()->cut_info.is_connector = true; }
    REQUIRE_FALSE(model_reconstruction_ineligibility(object).empty());
    const auto                 identity = model_reconstruction_source_identity(object);
    ModelReconstructionOptions options;
    auto                       result = reconstruct_model_candidate(model, 0, options);
    CHECK(result.status == "unsupported");
    CHECK_FALSE(result.candidate_model);
    CHECK(model_reconstruction_source_identity(object) == identity);
    CHECK_FALSE(result.report.at("print_ready").get<bool>());
}

TEST_CASE("Reconstruction source identity follows relevant edits", "[ModelReconstruction]")
{
    Model      model  = reconstruction_model();
    auto&      object = *model.objects.front();
    const auto before = model_reconstruction_source_identity(object);
    SECTION("name") { object.name = "renamed"; }
    SECTION("instance placement") { object.instances.front()->set_offset(Vec3d(1., 2., 3.)); }
    SECTION("volume placement") { object.volumes.front()->set_offset(Vec3d(1., 2., 3.)); }
    SECTION("mesh") { object.volumes.front()->set_mesh(make_cube(12., 10., 10.)); }
    SECTION("config") { object.config.set_key_value("perimeters", new ConfigOptionInt(4)); }
    CHECK(model_reconstruction_source_identity(object) != before);
}

TEST_CASE("Reconstruction failure and pre-cancel preserve the source", "[ModelReconstruction]")
{
    Model                      model    = reconstruction_model();
    auto&                      object   = *model.objects.front();
    const auto                 identity = model_reconstruction_source_identity(object);
    const auto                 mesh     = object.volumes.front()->mesh_ptr();
    ModelReconstructionOptions options;
    SECTION("missing worker") { options.executable = "/nonexistent-reconstruction-worker"; }
    SECTION("pre-cancel")
    {
        options.process.is_canceled = [] { return true; };
    }
    SECTION("invalid parameters") { options.mesh.alpha_mm = -1.; }
    SECTION("excessive input") { options.mesh.max_input_faces = 4; }
    const auto result = reconstruct_model_candidate(model, 0, options);
    REQUIRE_FALSE(result.candidate_model);
    CHECK(result.status != "candidate_ready");
    CHECK(model_reconstruction_source_identity(object) == identity);
    CHECK(object.volumes.front()->mesh_ptr() == mesh);
    CHECK_FALSE(result.report.at("mesh_validated").get<bool>());
    CHECK(result.report.at("trial_slice") == "not_run");
}

TEST_CASE("Installing a matching convex hull invalidates an existing footprint", "[ModelReconstruction]")
{
    Model         model    = reconstruction_model();
    auto&         object   = *model.objects.front();
    auto&         volume   = *object.volumes.front();
    auto&         instance = *object.instances.front();
    const Polygon before   = volume.get_convex_hull_2d(instance.get_matrix());
    REQUIRE(before.is_valid());

    volume.set_mesh(make_cube(2., 2., 2.));
    volume.set_convex_hull(make_cube(2., 2., 2.));
    const Polygon installed = volume.get_convex_hull_2d(instance.get_matrix());
    REQUIRE(installed.is_valid());
    CHECK(installed.points != before.points);
    volume.invalidate_convex_hull_2d();
    CHECK(volume.get_convex_hull_2d(instance.get_matrix()) == installed);
}

TEST_CASE("Isolated reconstruction preserves placement and staged metadata", "[.][ReconstructionIntegration]")
{
    // Run explicitly with the freshly built application, never a cached binary.
    const char* executable = std::getenv("ORCA_RECONSTRUCTION_BINARY");
    REQUIRE(executable != nullptr);
    Model model    = reconstruction_model();
    auto& object   = *model.objects.front();
    auto& volume   = *object.volumes.front();
    auto& instance = *object.instances.front();
    volume.set_mesh(make_cube(2., 2., 2.));
    volume.set_offset(Vec3d(0.2, 0.3, 0.4));
    instance.set_offset(Vec3d(20., 30., 0.));
    instance.set_scaling_factor(Vec3d(1.1, 0.9, 1.5));
    instance.set_mirror(Vec3d(-1., 1., 1.));
    instance.set_assemble_offset(Vec3d(1., 2., 3.));
    volume.source.input_file = "source.stl";
    object.config.set_key_value("perimeters", new ConfigOptionInt(4));
    volume.calculate_convex_hull();
    volume.invalidate_convex_hull_2d();
    const Polygon source_footprint = volume.get_convex_hull_2d(instance.get_matrix());
    REQUIRE(source_footprint.is_valid());
    REQUIRE(instance.convex_hull_2d().is_valid());
    const auto                 identity           = model_reconstruction_source_identity(object);
    const auto                 volume_transform   = volume.get_matrix();
    const auto                 instance_transform = instance.get_matrix();
    const auto                 assembly           = instance.get_assemble_transformation().get_matrix();
    ModelReconstructionOptions options;
    options.executable = executable;
    auto result        = reconstruct_model_candidate(model, 0, options);
    INFO(result.report.dump(2));
    REQUIRE(result.status == "candidate_ready");
    REQUIRE(result.candidate_model);
    const auto& candidate = *result.candidate_model->objects.front();
    CHECK(candidate.volumes.front()->get_matrix().matrix() == volume_transform.matrix());
    CHECK(candidate.instances.front()->get_matrix().matrix() == instance_transform.matrix());
    CHECK(candidate.instances.front()->get_assemble_transformation().get_matrix().matrix() == assembly.matrix());
    CHECK(candidate.config.opt_int("perimeters") == 4);
    CHECK(candidate.volumes.front()->source.input_file == "source.stl");
    CHECK(candidate.volumes.front()->id() != volume.id());
    CHECK(result.report["coordinate_validation"]["local"]["validation"]["complete"] == true);
    CHECK(result.report["coordinate_validation"]["physical_round_trip"]["validation"]["complete"] == true);
    CHECK(result.report["convex_hull_validation"]["validation"]["complete"] == true);
    CHECK_FALSE(result.report.at("convex_hull_mesh_identity").get<std::string>().empty());
    auto& candidate_volume   = *candidate.volumes.front();
    auto& candidate_instance = *candidate.instances.front();
    REQUIRE(candidate_volume.get_convex_hull_shared_ptr());
    CHECK_FALSE(candidate_volume.get_convex_hull().empty());
    const Polygon candidate_footprint = candidate_volume.get_convex_hull_2d(candidate_instance.get_matrix());
    REQUIRE(candidate_footprint.is_valid());
    CHECK(candidate_footprint.points != source_footprint.points);
    candidate_volume.invalidate_convex_hull_2d();
    CHECK(candidate_volume.get_convex_hull_2d(candidate_instance.get_matrix()) == candidate_footprint);
    CHECK(volume.get_convex_hull_2d(instance.get_matrix()) == source_footprint);
    CHECK(model_reconstruction_source_identity(object) == identity);
}
