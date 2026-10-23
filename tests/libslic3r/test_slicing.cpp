#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
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

TEST_CASE("Bounded adaptive plans retain first layer and constrain regular profile", "[Slicing][VariableLayerHeight]")
{
    Model model = make_cube_model(10.0);
    auto& object = *model.objects.front();
    auto& instance = *object.instances.front();
    instance.set_rotation(X, PI / 4.0);
    auto params = make_slicing_parameters(10.0 * std::sqrt(2.0));
    params.first_object_layer_height = GENERATE(0.1, 0.24);
    const auto legacy = layer_height_profile_adaptive(params, object, instance, .5f);
    const HeightProfileSmoothingParams smoothing(5, false);
    for (bool smooth : {false, true}) {
        const auto profile = layer_height_profile_adaptive_bounded(params, object, instance, .5f, .16, .2,
                                                                  smooth ? &smoothing : nullptr);
        REQUIRE(profile.size() > 4);
        REQUIRE(profile[1] == params.first_object_layer_height);
        REQUIRE(profile[3] == params.first_object_layer_height);
        for (size_t i = 5; i < profile.size(); i += 2) {
            REQUIRE(profile[i] >= .16 - EPSILON);
            REQUIRE(profile[i] <= .2 + EPSILON);
        }
    }
    REQUIRE(params.min_layer_height == .1);
    REQUIRE(params.max_layer_height == .3);
    REQUIRE(layer_height_profile_adaptive(params, object, instance, .5f) == legacy);
}

TEST_CASE("Bounded adaptive plans reject invalid or conflicting ranges", "[Slicing][VariableLayerHeight]")
{
    Model model = make_cube_model(10.0);
    auto& object = *model.objects.front();
    auto& instance = *object.instances.front();
    auto params = make_slicing_parameters(10.0);
    for (const auto& bounds : std::vector<std::pair<double, double>>{{0, .2}, {.2, .1}, {.4, .5},
            {std::numeric_limits<double>::quiet_NaN(), .2}, {.1, std::numeric_limits<double>::infinity()}}) {
        REQUIRE_THROWS(layer_height_profile_adaptive_bounded(params, object, instance, .5f, bounds.first, bounds.second));
    }
    auto profile = layer_height_profile_adaptive_bounded(params, object, instance, .5f, .01, .2);
    for (size_t i = 5; i < profile.size(); i += 2) REQUIRE(profile[i] >= .1 - EPSILON);
    ModelConfig range;
    range.set_key_value("layer_height", new ConfigOptionFloat(.08));
    object.layer_config_ranges[{1., 2.}].assign_config(std::move(range));
    REQUIRE_THROWS(layer_height_profile_adaptive_bounded(params, object, instance, .5f, .16, .2));
}

TEST_CASE("Bounded top alignment preserves body layers and machine bounds", "[Slicing][VariableLayerHeight][BoundedTop]")
{
    const double scale               = GENERATE(1.0, 1.02);
    const double target              = GENERATE(3.97, 4.03);
    auto         params              = make_slicing_parameters(target / scale, scale);
    params.first_object_layer_height = .24;
    const std::vector<double> profile{0, .24, .24 / scale, .24, .24 / scale, .18, target / scale, .18};
    const auto                old_layers = generate_object_layers(params, profile, false);
    const auto                aligned    = layer_height_profile_align_top_bounded(params, profile, .16, .2);
    const auto                layers     = generate_object_layers(params, aligned, false);
    REQUIRE(aligned != profile);
    REQUIRE(layers.size() == old_layers.size());
    REQUIRE(layers.back() == Approx(target).margin(1e-8));
    REQUIRE(aligned[aligned.size() - 2] == Approx(target / scale).margin(1e-8));
    for (size_t i = 0; i < layers.size() - 10; ++i)
        REQUIRE(layers[i] == Approx(old_layers[i]).margin(1e-8));
    REQUIRE(layers[1] == .24);
    for (size_t i = 2; i < layers.size(); i += 2) {
        REQUIRE(layers[i + 1] - layers[i] >= .16 - 1e-8);
        REQUIRE(layers[i + 1] - layers[i] <= .2 + 1e-8);
    }
    REQUIRE(generate_object_layers(params, aligned, true).back() == Approx(target).margin(1e-8));
    REQUIRE(generate_object_layers(params, profile, false) == old_layers);
    REQUIRE(layer_height_profile_align_top_bounded(params, aligned, .16, .2) == aligned);
}

TEST_CASE("Bounded top alignment saturates without moving saturated layers again", "[Slicing][VariableLayerHeight][BoundedTop]")
{
    const bool increase               = GENERATE(false, true);
    auto       params                 = make_slicing_parameters(increase ? 1.39 : 1.27);
    params.first_object_layer_height  = .24;
    const std::vector<double> heights = increase ? std::vector<double>{.24, .18, .2, .18, .18, .19, .16} :
                                                   std::vector<double>{.24, .18, .16, .18, .18, .19, .2};
    std::vector<double>       profile;
    double                    z = 0;
    for (double height : heights) {
        profile.insert(profile.end(), {z, height, z + height, height});
        z += height;
    }
    profile[profile.size() - 2] = params.object_print_z_height();
    const auto aligned          = layer_height_profile_align_top_bounded(params, profile, .16, .2);
    REQUIRE(aligned != profile);
    const auto result = generate_object_layers(params, aligned, false);
    REQUIRE(result.back() == Approx(params.object_print_z_height()).margin(1e-8));
    REQUIRE(result[5] - result[4] == Approx(increase ? .2 : .16).margin(1e-8));
    for (size_t i = 2; i < result.size(); i += 2) {
        REQUIRE(result[i + 1] - result[i] >= .16 - 1e-8);
        REQUIRE(result[i + 1] - result[i] <= .2 + 1e-8);
    }
}

TEST_CASE("Bounded top alignment only moves the terminal layer when feasible", "[Slicing][VariableLayerHeight][BoundedTop]")
{
    const double scale               = GENERATE(1.0, 1.02);
    const double target              = GENERATE(4.0, 4.03, 4.05);
    auto         params              = make_slicing_parameters(target / scale, scale);
    params.first_object_layer_height = .24;
    const double              height = target > 4.04 ? .19 : .2;
    const std::vector<double> profile{0, .24, .24 / scale, .24, .24 / scale, height, target / scale, height};
    const auto                original = generate_object_layers(params, profile, false);
    const auto                aligned  = layer_height_profile_align_top_bounded(params, profile, .16, .2);
    const auto                result   = generate_object_layers(params, aligned, false);
    REQUIRE(result.size() == original.size());
    REQUIRE(result.back() == Approx(target).margin(1e-8));
    for (size_t i = 0; i + 1 < result.size(); ++i)
        REQUIRE(result[i] == Approx(original[i]).margin(1e-8));
    if (target == 4.0) {
        REQUIRE(result[result.size() - 2] == Approx(3.84));
        REQUIRE(result.back() - result[result.size() - 2] == Approx(.16));
    }
    REQUIRE(layer_height_profile_align_top_bounded(params, aligned, .16, .2) == aligned);
}

TEST_CASE("Bounded top alignment leaves infeasible short and raft plans unchanged", "[Slicing][VariableLayerHeight][BoundedTop]")
{
    for (double target : {.37, 4.03}) {
        auto                      params = make_slicing_parameters(target);
        const std::vector<double> profile{0, .2, target, .2};
        REQUIRE(layer_height_profile_align_top_bounded(params, profile, .2, .2) == profile);
    }
    auto params              = make_slicing_parameters(4.03);
    params.base_raft_layers  = 2;
    params.soluble_interface = false;
    const std::vector<double> profile{0, .2, 4.03, .2};
    REQUIRE(layer_height_profile_align_top_bounded(params, profile, .16, .2) == profile);
    REQUIRE_THROWS(layer_height_profile_align_top_bounded(params, profile, .3, .2));
    REQUIRE_THROWS(layer_height_profile_align_top_bounded(params, profile, .4, .5));
}

TEST_CASE("Bounded top alignment may add one layer without moving the body", "[Slicing][VariableLayerHeight][BoundedTop]")
{
    auto params                      = make_slicing_parameters(1.6691842);
    params.first_object_layer_height = .24;
    const std::vector<double> heights{.24, .2, .2, .2, .2, .2, .19355551, .16622239};
    std::vector<double>       profile;
    double                    z = 0;
    for (double height : heights) {
        profile.insert(profile.end(), {z, height, z + height, height});
        z += height;
    }
    profile[profile.size() - 2] = params.object_print_z_height();
    const auto original         = generate_object_layers(params, profile, false);
    const auto aligned          = layer_height_profile_align_top_bounded(params, profile, .16, .2);
    const auto result           = generate_object_layers(params, aligned, false);
    REQUIRE(result.size() == original.size() + 2);
    REQUIRE(result.back() == Approx(params.object_print_z_height()).margin(1e-8));
    for (size_t i = 0; i < original.size() - 10; ++i)
        REQUIRE(result[i] == Approx(original[i]).margin(1e-8));
    for (size_t i = 2; i < result.size(); i += 2) {
        REQUIRE(result[i + 1] - result[i] >= .16 - 1e-8);
        REQUIRE(result[i + 1] - result[i] <= .2 + 1e-8);
    }
}

TEST_CASE("Bounded adaptive top alignment does not override custom layer ranges", "[Slicing][VariableLayerHeight][BoundedTop]")
{
    Model       model  = make_cube_model(10.03);
    auto&       object = *model.objects.front();
    ModelConfig range;
    range.set_key_value("layer_height", new ConfigOptionFloat(.18));
    object.layer_config_ranges[{1., 2.}].assign_config(std::move(range));
    auto params              = make_slicing_parameters(10.03);
    auto bounded             = params;
    bounded.min_layer_height = .16;
    bounded.max_layer_height = .2;
    auto expected            = layer_height_profile_adaptive(bounded, object, *object.instances.front(), .5f);
    // Existing bounded generation clamps native float intermediates as well;
    // compare against that pre-alignment contract, not the raw float profile.
    for (size_t i = 5; i < expected.size(); i += 2)
        expected[i] = std::clamp(expected[i], .16, .2);
    REQUIRE(layer_height_profile_adaptive_bounded(params, object, *object.instances.front(), .5f, .16, .2) == expected);
}

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
