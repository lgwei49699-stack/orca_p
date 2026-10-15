#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

DynamicPrintConfig section_preflight_print_config(const char* slicing_mode)
{
    auto config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({{"layer_height", 0.2},
                                   {"initial_layer_print_height", 0.24},
                                   {"slicing_mode", slicing_mode},
                                   {"slice_closing_radius", 0.0},
                                   {"resolution", 0.0125},
                                   {"elefant_foot_compensation", 0.0},
                                   {"xy_contour_compensation", 0.0},
                                   {"xy_hole_compensation", 0.0},
                                   {"hole_to_polyhole", false},
                                   {"enable_support", false},
                                   {"raft_layers", 0}});
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {200., 0.}, {200., 200.}, {0., 200.}}));
    config.set_key_value("bed_exclude_area", new ConfigOptionPoints());
    config.set("before_layer_change_gcode", std::string("G92 E0"));
    return config;
}

void section_preflight_init_print(indexed_triangle_set mesh, Print& print, Model& model, const char* slicing_mode)
{
    auto* object = model.add_object();
    object->name = "section-preflight.stl";
    object->add_volume(TriangleMesh(std::move(mesh)), ModelVolumeType::MODEL_PART, false);
    object->add_instance()->set_offset(Vec3d(80., 80., 0.));
    print.auto_assign_extruders(object);
    print.set_status_silent();
    print.apply(model, section_preflight_print_config(slicing_mode));
    INFO(print.validate().string);
    REQUIRE(print.validate().string.empty());
}

std::vector<float> section_preflight_print_planes(const PrintObject& object)
{
    std::vector<coordf_t> profile;
    PrintObject::update_layer_height_profile(*object.model_object(), object.slicing_parameters(), profile);
    const auto         object_layers = generate_object_layers(object.slicing_parameters(), profile, object.config().precise_z_height.value);
    std::vector<float> planes;
    for (size_t layer = 0; layer < object_layers.size(); layer += 2)
        planes.push_back(float(0.5 * (object_layers[layer] + object_layers[layer + 1])));
    return planes;
}

MeshSlicingDiagnosticResultEx section_preflight_for_print_object(const PrintObject& object, const std::vector<float>& planes)
{
    // Deliberately limited to one unpainted positive volume, no modifiers,
    // compensation or vase mode. Production does not run Print here: this is
    // an offline comparison against the actual PrintObject cut and layer plan.
    REQUIRE(object.model_object()->volumes.size() == 1);
    const auto&         volume = *object.model_object()->volumes.front();
    MeshSlicingParamsEx params;
    params.trafo          = object.trafo_centered() * volume.get_matrix();
    params.closing_radius = float(object.config().slice_closing_radius.value);
    params.resolution     = object.print()->config().resolution <= 0.001 ? 0.0 : 0.0025;
    switch (object.config().slicing_mode.value) {
    case SlicingMode::Regular: params.mode = MeshSlicingParams::SlicingMode::Regular; break;
    case SlicingMode::EvenOdd: params.mode = MeshSlicingParams::SlicingMode::EvenOdd; break;
    case SlicingMode::CloseHoles: params.mode = MeshSlicingParams::SlicingMode::Positive; break;
    }
    params.mode_below = params.mode;
    auto mesh         = volume.mesh().its;
    if (params.trafo.rotation().determinant() < 0.)
        its_flip_triangles(mesh);
    return slice_mesh_ex_with_diagnostics(mesh, planes, params);
}

double section_preflight_area_mm2(const ExPolygons& regions)
{
    double area = 0.;
    for (const auto& region : regions)
        area += region.area() * SCALING_FACTOR * SCALING_FACTOR;
    return area;
}

} // namespace

TEST_CASE("Section preflight matches real PrintObject planes and healthy or cavity regions", "[PrintObject][SectionPreflight]")
{
    const bool  cavity = GENERATE(false, true);
    const char* mode   = GENERATE("regular", "even_odd", "close_holes");
    CAPTURE(cavity, mode);
    auto mesh = its_make_cube(10., 10., 4.);
    if (cavity) {
        auto inner = its_make_cube(4., 4., 2.);
        for (auto& vertex : inner.vertices)
            vertex += Vec3f(3.f, 3.f, 1.f);
        its_flip_triangles(inner);
        its_merge(mesh, inner);
    }

    Print print;
    Model model;
    section_preflight_init_print(std::move(mesh), print, model, mode);
    REQUIRE(print.objects().size() == 1);
    auto&      object = *print.get_object(0);
    const auto planes = section_preflight_print_planes(object);
    REQUIRE_FALSE(planes.empty());
    REQUIRE(planes.front() == Approx(0.12));
    const auto preflight = section_preflight_for_print_object(object, planes);
    REQUIRE_NOTHROW(object.slice());
    REQUIRE(object.layers().size() == planes.size());
    REQUIRE(object.layers().front()->print_z == Approx(0.24));
    for (size_t layer = 0; layer < planes.size(); ++layer) {
        CAPTURE(layer);
        const auto& actual = *object.layers()[layer];
        REQUIRE(actual.slice_z == Approx(planes[layer]));
        REQUIRE(actual.print_z > actual.slice_z);
        REQUIRE_FALSE(preflight.slices[layer].empty());
        REQUIRE(section_preflight_area_mm2(preflight.slices[layer]) == Approx(section_preflight_area_mm2(actual.lslices)).margin(1e-6));
        const bool   cuts_cavity = cavity && planes[layer] > 1.f && planes[layer] < 3.f;
        const double expected    = cuts_cavity && std::string(mode) != "close_holes" ? 84. : 100.;
        REQUIRE(section_preflight_area_mm2(actual.lslices) == Approx(expected).margin(1e-6));
    }
}

TEST_CASE("Section preflight predicts regions discarded from a real PrintObject", "[PrintObject][SectionPreflight]")
{
    const char* mode = GENERATE("regular", "even_odd");
    CAPTURE(mode);
    auto mesh  = its_make_cube(10., 10., 1.);
    auto upper = its_make_cube(10., 10., 3.);
    for (auto& vertex : upper.vertices)
        vertex.z() += 1.f;
    its_merge(mesh, upper);
    its_flip_triangles(upper);
    its_merge(mesh, upper);

    // The sound base intentionally remains printable. The coincident,
    // opposite-facing upper shells form closed loops but no effective region,
    // which the real PrintObject then removes as trailing empty layers.
    Print print;
    Model model;
    section_preflight_init_print(std::move(mesh), print, model, mode);
    auto&      object    = *print.get_object(0);
    const auto planes    = section_preflight_print_planes(object);
    const auto preflight = section_preflight_for_print_object(object, planes);
    REQUIRE_NOTHROW(object.slice());
    REQUIRE_FALSE(object.layers().empty());
    REQUIRE(object.layers().size() < planes.size());
    for (size_t layer = 0; layer < planes.size(); ++layer) {
        CAPTURE(layer);
        if (layer < object.layers().size()) {
            REQUIRE_FALSE(preflight.slices[layer].empty());
            REQUIRE(object.layers()[layer]->slice_z == Approx(planes[layer]));
            REQUIRE(section_preflight_area_mm2(preflight.slices[layer]) ==
                    Approx(section_preflight_area_mm2(object.layers()[layer]->lslices)).margin(1e-6));
        } else {
            REQUIRE(planes[layer] > 1.f);
            REQUIRE(preflight.diagnostics[layer].closed_loops == 2);
            REQUIRE(preflight.diagnostics[layer].closed_loop_area_mm2 == Approx(200.));
            REQUIRE(preflight.diagnostics[layer].remaining_open_chains == 0);
            REQUIRE(preflight.slices[layer].empty());
        }
    }
}

TEST_CASE("Section preflight identifies a fully empty region result before PrintObject reports a slicing error",
          "[PrintObject][SectionPreflight]")
{
    auto mesh     = its_make_cube(10., 10., 4.);
    auto reversed = mesh;
    its_flip_triangles(reversed);
    its_merge(mesh, reversed);

    Print print;
    Model model;
    section_preflight_init_print(std::move(mesh), print, model, "regular");
    auto&      object = *print.get_object(0);
    const auto planes = section_preflight_print_planes(object);
    REQUIRE_FALSE(planes.empty());
    const auto preflight = section_preflight_for_print_object(object, planes);
    for (size_t layer = 0; layer < planes.size(); ++layer) {
        CAPTURE(layer);
        REQUIRE(preflight.diagnostics[layer].closed_loops == 2);
        REQUIRE(preflight.diagnostics[layer].closed_loop_area_mm2 == Approx(200.));
        REQUIRE(preflight.diagnostics[layer].remaining_open_chains == 0);
        REQUIRE(preflight.slices[layer].empty());
    }
    REQUIRE_THROWS_AS(object.slice(), Slic3r::SlicingError);
    REQUIRE(object.layers().empty());
    REQUIRE_FALSE(object.is_step_done(posSlice));
}

SCENARIO("PrintObject: object layer heights", "[PrintObject]") {
    GIVEN("20mm cube and default initial config, initial layer height of 2mm") {
        WHEN("generate_object_layers() is called for 2mm layer heights and nozzle diameter of 3mm") {
            Slic3r::Print print;
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
        		{ "first_layer_height", 2 },
				{ "layer_height", 		2 },
	            { "nozzle_diameter", 	3 }
	        });
            ConstLayerPtrsAdaptor layers = print.objects().front()->layers();
            THEN("The output vector has 10 entries") {
                REQUIRE(layers.size() == 10);
            }
            AND_THEN("Each layer is approximately 2mm above the previous Z") {
                coordf_t last = 0.0;
                for (size_t i = 0; i < layers.size(); ++ i) {
                    REQUIRE((layers[i]->print_z - last) == Approx(2.0));
                    last = layers[i]->print_z;
                }
            }
        }
        WHEN("generate_object_layers() is called for 10mm layer heights and nozzle diameter of 11mm") {
            Slic3r::Print print;
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
        		{ "first_layer_height", 2 },
				{ "layer_height", 		10 },
	            { "nozzle_diameter", 	11 }
	        });
            ConstLayerPtrsAdaptor layers = print.objects().front()->layers();
			THEN("The output vector has 3 entries") {
                REQUIRE(layers.size() == 3);
            }
            AND_THEN("Layer 0 is at 2mm") {
                REQUIRE(layers.front()->print_z == Approx(2.0));
            }
            AND_THEN("Layer 1 is at 12mm") {
                REQUIRE(layers[1]->print_z == Approx(12.0));
            }
        }
        WHEN("generate_object_layers() is called for 15mm layer heights and nozzle diameter of 16mm") {
            Slic3r::Print print;
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
        		{ "first_layer_height", 2 },
				{ "layer_height", 		15 },
	            { "nozzle_diameter", 	16 }
	        });
            ConstLayerPtrsAdaptor layers = print.objects().front()->layers();
			THEN("The output vector has 2 entries") {
                REQUIRE(layers.size() == 2);
            }
            AND_THEN("Layer 0 is at 2mm") {
                REQUIRE(layers[0]->print_z == Approx(2.0));
            }
            AND_THEN("Layer 1 is at 17mm") {
                REQUIRE(layers[1]->print_z == Approx(17.0));
            }
        }
#if 0
        WHEN("generate_object_layers() is called for 15mm layer heights and nozzle diameter of 5mm") {
            Slic3r::Print print;
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
        		{ "first_layer_height", 2 },
				{ "layer_height", 		15 },
	            { "nozzle_diameter", 	5 }
	        });
			const std::vector<Slic3r::Layer*> &layers = print.objects().front()->layers();
			THEN("The layer height is limited to 5mm.") {
                CHECK(layers.size() == 5);
                coordf_t last = 2.0;
                for (size_t i = 1; i < layers.size(); i++) {
                    REQUIRE((layers[i]->print_z - last) == Approx(5.0));
                    last = layers[i]->print_z;
                }
            }
        }
#endif
    }
}
