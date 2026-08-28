#include <catch2/catch.hpp>

#include "slic3r/GUI/IMSlider.hpp"

#include <string>

namespace Slic3r::GUI {

class IMSliderTestAccess
{
public:
    static void set_wipe_tower(IMSlider &slider, bool enabled) { slider.m_is_wipe_tower = enabled; }
    static bool has_layer_mapping(const IMSlider &slider) { return !slider.m_layers_values.empty(); }

    static size_t layer_number(IMSlider &slider, int tick)
    {
        const std::string label = slider.get_label(tick, ltHeightWithLayer);
        return std::stoul(label.substr(0, label.find('\n')));
    }
};

} // namespace Slic3r::GUI

TEST_CASE("IMSlider invalidates the layer mapping when loading new slice data", "[IMSlider]")
{
    using namespace Slic3r::GUI;

    IMSlider slider(0, 3, 0, 3);
    IMSliderTestAccess::set_wipe_tower(slider, true);

    // Build the special layer mapping used when wipe-tower values and layer times differ.
    slider.SetSliderValues({0.2, 0.2, 0.4, 0.6});
    slider.SetLayersTimes(std::vector<float>{1.0f, 1.0f, 1.0f}, 3.0f);
    // The duplicate first Z proves that the special mapping is active: raw tick 1 is layer 2.
    REQUIRE(IMSliderTestAccess::has_layer_mapping(slider));
    REQUIRE(IMSliderTestAccess::layer_number(slider, 1) == 1);

    SECTION("new slider values")
    {
        // Loading a new slice must not use the mapping derived from the previous Z values.
        slider.SetSliderValues({0.3, 0.5, 0.7});
        REQUIRE_FALSE(IMSliderTestAccess::has_layer_mapping(slider));
        REQUIRE(IMSliderTestAccess::layer_number(slider, 1) == 2);

        // Equal value/time counts do not need a special mapping and must still use current values.
        slider.SetLayersTimes(std::vector<float>{1.0f, 1.0f, 1.0f}, 3.0f);
        REQUIRE_FALSE(IMSliderTestAccess::has_layer_mapping(slider));
        REQUIRE(IMSliderTestAccess::layer_number(slider, 1) == 2);
    }

    SECTION("new FFF layer times")
    {
        slider.SetLayersTimes(std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f}, 4.0f);
        REQUIRE_FALSE(IMSliderTestAccess::has_layer_mapping(slider));
        REQUIRE(IMSliderTestAccess::layer_number(slider, 1) == 2);
    }

    SECTION("new SLA layer times")
    {
        slider.SetLayersTimes(std::vector<double>{1.0, 1.0, 1.0, 1.0});
        REQUIRE_FALSE(IMSliderTestAccess::has_layer_mapping(slider));
        REQUIRE(IMSliderTestAccess::layer_number(slider, 1) == 2);
    }
}
