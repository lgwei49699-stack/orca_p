#include <catch2/catch.hpp>

#include <cmath>
#include <utility>
#include <vector>

#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;

namespace {

SlicingParameters make_slicing_parameters(double object_height, double shrinkage_compensation_z = 1.0)
{
    SlicingParameters params;
    params.valid                            = true;
    params.layer_height                     = 0.2;
    params.min_layer_height                 = 0.1;
    params.max_layer_height                 = 0.3;
    params.first_print_layer_height         = 0.2;
    params.first_object_layer_height        = 0.2;
    params.object_print_z_min               = 0.0;
    params.object_print_z_max               = object_height * shrinkage_compensation_z;
    params.object_print_z_uncompensated_max = object_height;
    params.object_shrinkage_compensation_z  = shrinkage_compensation_z;
    return params;
}

Model make_cube_model(double size)
{
    Model        model;
    ModelObject* object = model.add_object();
    object->add_volume(make_cube(size, size, size));
    object->add_instance();
    return model;
}

} // namespace

TEST_CASE("Adaptive layer height uses an explicit representative instance", "[Slicing][VariableLayerHeight]")
{
    Model        model  = make_cube_model(10.0);
    ModelObject* object = model.objects.front();

    ModelInstance* identity_instance = object->instances.front();
    ModelInstance* rotated_instance  = object->add_instance();
    rotated_instance->set_rotation(X, PI / 4.0);

    const SlicingParameters params = make_slicing_parameters(10.0 * std::sqrt(2.0));

    const std::vector<double> default_profile     = layer_height_profile_adaptive(params, *object, 0.5f);
    const std::vector<double> identity_profile    = layer_height_profile_adaptive(params, *object, *identity_instance, 0.5f);
    const std::vector<double> rotated_profile     = layer_height_profile_adaptive(params, *object, *rotated_instance, 0.5f);
    const std::vector<double> transformed_profile = layer_height_profile_adaptive(params, *object, rotated_instance->get_matrix(), 0.5f);

    REQUIRE(default_profile == identity_profile);
    REQUIRE(rotated_profile == transformed_profile);
    REQUIRE(default_profile != rotated_profile);
}

TEST_CASE("Adaptive layer height profile stays in uncompensated model Z coordinates", "[Slicing][VariableLayerHeight]")
{
    Model              model  = make_cube_model(10.0);
    const ModelObject& object = *model.objects.front();

    const SlicingParameters unscaled_params  = make_slicing_parameters(10.0);
    const SlicingParameters shrinkage_params = make_slicing_parameters(10.0, 1.2);

    const std::vector<double> unscaled_profile  = layer_height_profile_adaptive(unscaled_params, object, 0.5f);
    const std::vector<double> shrinkage_profile = layer_height_profile_adaptive(shrinkage_params, object, 0.5f);

    REQUIRE(shrinkage_profile == unscaled_profile);
    REQUIRE(*(shrinkage_profile.end() - 2) == Approx(10.0));
}

TEST_CASE("Layer ranges are clipped in uncompensated model Z coordinates", "[Slicing][VariableLayerHeight]")
{
    const SlicingParameters unscaled_params  = make_slicing_parameters(10.0);
    const SlicingParameters shrinkage_params = make_slicing_parameters(10.0, 1.2);

    t_layer_config_ranges ranges;
    ModelConfig           range_config;
    range_config.set_key_value("layer_height", new ConfigOptionFloat(0.15));
    ranges[{0.0, 20.0}].assign_config(std::move(range_config));

    const std::vector<coordf_t> unscaled_profile  = layer_height_profile_from_ranges(unscaled_params, ranges);
    const std::vector<coordf_t> shrinkage_profile = layer_height_profile_from_ranges(shrinkage_params, ranges);

    REQUIRE(shrinkage_profile == unscaled_profile);
    REQUIRE(*(shrinkage_profile.end() - 2) == Approx(10.0));
}
