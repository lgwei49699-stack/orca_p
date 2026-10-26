#ifndef slic3r_CLIRegionalLayerPlan_hpp_
#define slic3r_CLIRegionalLayerPlan_hpp_

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Slicing.hpp"

namespace Slic3r {

// CLI-only transport of a previously computed plan. No search, repair or
// adaptive generation here; ordinary slicing and the GUI never call this code.
struct CLIRegionalLayerPlan
{
    struct Range
    {
        double lower, upper, height;
    };
    double             base, first, minimum, maximum, model_height, scale;
    Vec3d              rotation;
    std::string        model_sha256;
    std::vector<Range> ranges;

    static void require(bool valid, const char* reason)
    {
        if (!valid)
            throw std::runtime_error(std::string("Invalid regional layer-height plan: ") + reason);
    }

    static double number(const nlohmann::json& value, const char* key)
    {
        require(value.contains(key) && value.at(key).is_number(), "missing numeric field");
        const double result = value.at(key).get<double>();
        require(std::isfinite(result), "non-finite field");
        return result;
    }

    static bool equal(double a, double b) { return std::abs(a - b) <= 1e-5; }
    static bool integral(double value) { return std::abs(value - std::round(value)) <= 1e-6; }

    static CLIRegionalLayerPlan read(const std::string& path)
    {
        require(boost::filesystem::is_regular_file(path) && boost::filesystem::file_size(path) <= 65536,
                "plan file missing or larger than 64 KiB");
        boost::nowide::ifstream stream(path, std::ios::binary);
        nlohmann::json          value;
        stream >> value;
        require(value.is_object() && value.value("version", "") == "regional-slope-v1", "unsupported version");
        CLIRegionalLayerPlan plan;
        plan.base         = number(value, "baseMm");
        plan.first        = number(value, "firstMm");
        plan.minimum      = number(value, "minimumMm");
        plan.maximum      = number(value, "maximumMm");
        plan.model_height = number(value, "modelHeightMm");
        plan.scale        = number(value, "modelScale");
        plan.rotation     = Vec3d(number(value, "rotateX"), number(value, "rotateY"), number(value, "rotateZ")) * (M_PI / 180.0);
        plan.model_sha256 = value.at("modelSha256").get<std::string>();
        require(plan.model_sha256.size() == 64 && plan.model_sha256.find_first_not_of("0123456789abcdef") == std::string::npos,
                "invalid model SHA256");
        require(plan.base > 0 && plan.first > 0 && plan.scale > 0 && plan.minimum >= .01 &&
                    plan.minimum < plan.base && equal(plan.maximum, plan.base) && plan.model_height > plan.first &&
                    plan.model_height / plan.minimum <= 1000000,
                "invalid height bounds");
        const auto& entries = value.at("ranges");
        require(entries.is_array() && !entries.empty() && entries.size() <= 128, "expected 1..128 ranges");
        double previous = plan.first;
        for (const auto& entry : entries) {
            Range range{number(entry, "minimumZMm"), number(entry, "maximumZMm"), number(entry, "layerHeightMm")};
            require(range.lower >= previous - 1e-9 && range.upper > range.lower && range.upper <= plan.model_height &&
                        range.height >= plan.minimum - 1e-9 && range.height < plan.base &&
                        integral((range.lower - plan.first) / plan.base) && integral((range.upper - plan.first) / plan.base) &&
                        integral((range.upper - range.lower) / range.height),
                    "overlapping, out-of-bounds or non-phase-preserving range");
            previous = range.upper;
            plan.ranges.push_back(range);
        }
        return plan;
    }

    void apply(Model& model) const
    {
        require(model.objects.size() == 1, "requires one STL object");
        ModelObject& object = *model.objects.front();
        require(object.instances.size() == 1 && object.volumes.size() == 1 && !object.has_custom_layering(),
                "requires one instance/volume without an existing layer profile");
        const ModelInstance& instance = *object.instances.front();
        require((instance.get_scaling_factor() - Vec3d::Constant(scale)).cwiseAbs().maxCoeff() < 1e-7 &&
                    (instance.get_rotation() - rotation).cwiseAbs().maxCoeff() < 1e-7,
                "model pose or scale mismatch");
        const BoundingBoxf3 box = object.instance_bounding_box(instance, false);
        require(std::abs(box.size().z() - model_height) <= 5e-5 && std::abs(box.min.z()) <= 1e-5,
                "transformed model height or bed alignment mismatch");
        for (const Range& range : ranges)
            object.layer_config_ranges[{range.lower, range.upper}].set_key_value("layer_height", new ConfigOptionFloat(range.height));
    }

    void validate(const Print& print) const
    {
        require(print.objects().size() == 1, "requires one printable object");
        const PrintObject& object = *print.objects().front();
        const auto&        params = object.slicing_parameters();
        require(object.instances().size() == 1 && !params.has_raft() && !print.config().spiral_mode.value &&
                    !object.config().precise_z_height.value && equal(params.layer_height, base) &&
                    equal(params.first_object_layer_height, first) && std::abs(params.object_print_z_height() - model_height) <= 5e-5 &&
                    equal(params.object_shrinkage_compensation_z, 1.) && minimum >= params.min_layer_height - 1e-6 &&
                    maximum <= params.max_layer_height + 1e-6,
                "effective machine/process/height differs from plan");
        require(print.config().filament_shrink.values.size() == 1 && equal(print.config().filament_shrink.values.front(), 100.),
                "unsupported filament shrinkage");
    }

    // Independently check the actual layer sequence before exporting G-code.
    // This is a single linear contract check, not a second slicing pass/report.
    void validate_layers(const Print& print) const
    {
        const auto& layers      = print.objects().front()->layers();
        size_t      index       = 0;
        size_t      range_index = 0;
        double      bottom      = 0;
        while (index < 1000000) {
            double height = index == 0 ? first : base;
            while (range_index < ranges.size() && bottom >= ranges[range_index].upper - 1e-6)
                ++range_index;
            if (range_index < ranges.size() && bottom >= ranges[range_index].lower - 1e-6)
                height = ranges[range_index].height;
            if (index > 0 && bottom + .5 * height >= model_height)
                break;
            require(index < layers.size() && equal(layers[index]->height, height) && equal(layers[index]->print_z, bottom + height) &&
                        equal(layers[index]->slice_z, bottom + .5 * height),
                    "generated layers do not implement requested ranges/phase");
            bottom += height;
            ++index;
        }
        require(index == layers.size(), "unexpected generated layer count");
    }
};

} // namespace Slic3r
#endif
