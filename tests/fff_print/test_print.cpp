#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Utils.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

TEST_CASE("Detail path optimization is runtime-only and invalidates cached paths", "[Print][DetailPathOptIn]")
{
    CHECK_FALSE(print_config_def.has("optimize_model_detail_paths"));
    REQUIRE(cli_misc_config_def.has("optimize_model_detail_paths"));
    CHECK_FALSE(cli_misc_config_def.get("optimize_model_detail_paths")->default_value->getBool());
    Print print;
    CHECK_FALSE(print.model_detail_path_optimization());
    // Fixed placement: this regression concerns slicing cache invalidation,
    // not the unrelated synthetic InfiniteBed arrangement helper.
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("layer_height", 0.3);
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {200., 0.}, {200., 200.}, {0., 200.}}));
    config.set_key_value("bed_exclude_area", new ConfigOptionPoints());
    Model        model;
    ModelObject* object = model.add_object();
    object->add_volume(make_cube(10., 10., 2.));
    object->add_instance()->set_offset(Vec3d(80., 80., 0.));
    print.auto_assign_extruders(object);
    print.set_status_silent();
    print.apply(model, config);
    print.process();
    REQUIRE(print.is_step_done(posInfill));
    print.set_model_detail_path_optimization(false);
    CHECK(print.is_step_done(posInfill));
    for (bool enabled : {true, false}) {
        print.set_model_detail_path_optimization(enabled);
        CHECK(print.model_detail_path_optimization() == enabled);
        CHECK_FALSE(print.is_step_done(posSlice));
        CHECK_FALSE(print.is_step_done(posPerimeters));
        CHECK_FALSE(print.is_step_done(posInfill));
        CHECK_FALSE(print.is_step_done(psGCodeExport));
        CHECK_FALSE(print.config().has("optimize_model_detail_paths"));
        print.process();
        REQUIRE(print.is_step_done(posInfill));
        print.set_model_detail_path_optimization(enabled);
        CHECK(print.is_step_done(posInfill));
    }
    Print gui_print;
    CHECK_FALSE(gui_print.model_detail_path_optimization());
}

TEST_CASE("Mesh replacement updates region transforms before slicing and export", "[Print][PrintApplyCache]")
{
    // Mesh only, Z placement, Z scale, tilt, and a compatible XY rotation.
    const int  mode             = GENERATE(0, 1, 2, 3, 4);
    const bool use_layer_ranges = GENERATE(false, true);
    CAPTURE(mode, use_layer_ranges);
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("before_layer_change_gcode", std::string("G92 E0"));
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {200., 0.}, {200., 200.}, {0., 200.}}));
    config.set_key_value("bed_exclude_area", new ConfigOptionPoints());
    // Full defaults need enum maps for G-code configuration serialization.
    for (const auto& key : config.keys()) {
        auto*       value = dynamic_cast<ConfigOptionEnumsGeneric*>(config.option(key));
        const auto* def   = print_config_def.get(key);
        if (value && !value->keys_map && def && def->enum_keys_map) {
            auto* mapped   = new ConfigOptionEnumsGeneric(def->enum_keys_map);
            mapped->values = value->values;
            config.set_key_value(key, mapped);
        }
    }
    struct ExportGuard
    {
        std::string             previous_resources = resources_dir();
        boost::filesystem::path path               = boost::filesystem::temp_directory_path() /
                                       boost::filesystem::unique_path("orca-apply-cache-%%%%-%%%%.gcode");
        ExportGuard() { set_resources_dir(std::string(TEST_DATA_DIR) + "/../../resources"); }
        ~ExportGuard()
        {
            set_resources_dir(previous_resources);
            boost::system::error_code ignored;
            boost::filesystem::remove(path, ignored);
        }
    } output;
    Model        model;
    ModelObject* object = model.add_object();
    object->name        = "replaced mesh";
    ModelVolume* volume = object->add_volume(make_cube(10., 10., 2.), ModelVolumeType::MODEL_PART, false);
    // Keep another volume to exercise reused as well as replaced extents.
    TriangleMesh unchanged = make_cube(3., 3., 2.);
    unchanged.translate(15., 0., 0.);
    object->add_volume(std::move(unchanged), ModelVolumeType::MODEL_PART, false);
    if (use_layer_ranges) {
        object->layer_config_ranges[{0., 1.}].set("wall_loops", 3);
        object->layer_config_ranges[{0., 1.}].set("layer_height", 0.2);
    }
    object->add_instance()->set_offset(Vec3d(80., 80., 0.));
    Print print;
    print.auto_assign_extruders(object);
    print.set_status_silent();
    print.apply(model, config);

    // Reconstruction replaces the volume and may also adjust instance Z in
    // the same transaction. Opening an already rebuilt project misses this.
    TriangleMesh replacement = make_cube(10., 10., 2.1);
    replacement.translate(0., 0., -0.05);
    volume->set_mesh(std::move(replacement));
    volume->calculate_convex_hull();
    volume->set_new_unique_id();
    if (mode == 1)
        object->instances.front()->set_offset(Z, 0.05);
    else if (mode == 2)
        object->instances.front()->set_scaling_factor(Z, 1.1);
    else if (mode == 3)
        object->instances.front()->set_rotation(X, 0.1);
    else if (mode == 4)
        object->instances.front()->set_rotation(Z, 0.1);
    object->invalidate_bounding_box();
    print.apply(model, config);
    INFO(print.validate().string);
    REQUIRE(print.validate().string.empty());
    if (mode != 4) {
        // Reusing an XY-rotated cache is intentional. For all other cases the
        // region bounds must match a fresh Print, including unchanged volumes.
        Print fresh;
        fresh.apply(model, config);
        const auto* actual   = print.objects().front()->shared_regions();
        const auto* expected = fresh.objects().front()->shared_regions();
        CHECK(actual->trafo_bboxes.matrix().isApprox(expected->trafo_bboxes.matrix()));
        REQUIRE(actual->layer_ranges.size() == expected->layer_ranges.size());
        for (size_t i = 0; i < actual->layer_ranges.size(); ++i) {
            const auto& a = actual->layer_ranges[i];
            const auto& e = expected->layer_ranges[i];
            REQUIRE(a.volumes.size() == e.volumes.size());
            for (size_t j = 0; j < a.volumes.size(); ++j) {
                CHECK(a.volumes[j].volume_id == e.volumes[j].volume_id);
                CHECK(a.volumes[j].bbox.min().isApprox(e.volumes[j].bbox.min()));
                CHECK(a.volumes[j].bbox.max().isApprox(e.volumes[j].bbox.max()));
            }
        }
    }
    print.process();
    GCodeProcessorResult gcode;
    print.export_gcode(output.path.string(), &gcode);
    REQUIRE(boost::filesystem::file_size(output.path) > 0);
    REQUIRE(print.is_step_done(psGCodeExport));
    REQUIRE(print.objects().front()->is_step_done(posSlice));
    REQUIRE(print.objects().front()->is_step_done(posInfill));
    CHECK(print.apply(model, config) == Print::APPLY_STATUS_UNCHANGED);
    CHECK(print.objects().front()->is_step_done(posSlice));
    CHECK(print.objects().front()->is_step_done(posInfill));
    CHECK(print.is_step_done(psGCodeExport));
    CHECK(print.apply(model, config) == Print::APPLY_STATUS_UNCHANGED);

    SECTION("Real configuration changes still invalidate slices and G-code") { config.set("layer_height", 0.15); }
    SECTION("Real placement changes still invalidate slices and G-code")
    {
        object->instances.front()->set_offset(Z, object->instances.front()->get_offset().z() + 0.1);
        object->invalidate_bounding_box();
    }
    REQUIRE(print.apply(model, config) == Print::APPLY_STATUS_INVALIDATED);
    CHECK_FALSE(print.objects().front()->is_step_done(posSlice));
    CHECK_FALSE(print.is_step_done(psGCodeExport));
}

SCENARIO("PrintObject: Perimeter generation", "[PrintObject]") {
    GIVEN("20mm cube and default config") {
        WHEN("make_perimeters() is called")  {
            Slic3r::Print print;
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, { { "fill_density", 0 } });
			const PrintObject &object = *print.objects().front();
			THEN("67 layers exist in the model") {
                REQUIRE(object.layers().size() == 66);
            }
            THEN("Every layer in region 0 has 1 island of perimeters") {
                for (const Layer *layer : object.layers())
                    REQUIRE(layer->regions().front()->perimeters.entities.size() == 1);
            }
            THEN("Every layer in region 0 has 3 paths in its perimeters list.") {
                for (const Layer *layer : object.layers())
                    REQUIRE(layer->regions().front()->perimeters.items_count() == 3);
            }
        }
    }
}

SCENARIO("Print: Skirt generation", "[Print]") {
    GIVEN("20mm cube and default config") {
        WHEN("Skirts is set to 2 loops")  {
            Slic3r::Print print;
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
            	{ "skirt_height", 	1 },
        		{ "skirt_distance", 1 },
        		{ "skirts", 		2 }
            });
            THEN("Skirt Extrusion collection has 2 loops in it") {
                REQUIRE(print.skirt().items_count() == 2);
                REQUIRE(print.skirt().flatten().entities.size() == 2);
            }
        }
    }
}

SCENARIO("Print: Changing number of solid surfaces does not cause all surfaces to become internal.", "[Print]") {
    GIVEN("sliced 20mm cube and config with top_solid_surfaces = 2 and bottom_solid_surfaces = 1") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
		config.set_deserialize_strict({
			{ "top_solid_layers",		2 },
			{ "bottom_solid_layers",	1 },
			{ "layer_height",			0.25 }, // get a known number of layers
			{ "first_layer_height",		0.25 }
			});
        Slic3r::Print print;
        Slic3r::Model model;
        Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        // Precondition: Ensure that the model has 2 solid top layers (39, 38)
        // and one solid bottom layer (0).
		auto test_is_solid_infill = [&print](size_t obj_id, size_t layer_id) {
		    const Layer &layer = *(print.objects().at(obj_id)->get_layer((int)layer_id));
		    // iterate over all of the regions in the layer
		    for (const LayerRegion *region : layer.regions()) {
		        // for each region, iterate over the fill surfaces
		        for (const Surface &surface : region->fill_surfaces.surfaces)
		            CHECK(surface.is_solid());
		    }
		};
        print.process();
        test_is_solid_infill(0,  0); // should be solid
        test_is_solid_infill(0, 79); // should be solid
        test_is_solid_infill(0, 78); // should be solid
        WHEN("Model is re-sliced with top_solid_layers == 3") {
			config.set("top_solid_layers", 3);
			print.apply(model, config);
            print.process();
            THEN("Print object does not have 0 solid bottom layers.") {
                test_is_solid_infill(0, 0);
            }
            AND_THEN("Print object has 3 top solid layers") {
                test_is_solid_infill(0, 79);
                test_is_solid_infill(0, 78);
                test_is_solid_infill(0, 77);
            }
        }
    }
}

SCENARIO("Print: Brim generation", "[Print]") {
    GIVEN("20mm cube and default config, 1mm first layer width") {
        WHEN("Brim is set to 3mm")  {
	        Slic3r::Print print;
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
	        	{ "first_layer_extrusion_width", 	1 },
	        	{ "brim_width", 					3 }
	        });
            THEN("Brim Extrusion collection has 3 loops in it") {
                size_t total_items = 0;
                for (const auto& pair : print.get_brimMap()) {
                    total_items += pair.second.items_count();
                }
                REQUIRE(total_items == 3);
            }
        }
        WHEN("Brim is set to 6mm")  {
	        Slic3r::Print print;
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
	        	{ "first_layer_extrusion_width", 	1 },
	        	{ "brim_width", 					6 }
	        });
            THEN("Brim Extrusion collection has 6 loops in it") {
                size_t total_items = 0;
                for (const auto& pair : print.get_brimMap()) {
                    total_items += pair.second.items_count();
                }
                REQUIRE(total_items == 6);
            }
        }
        WHEN("Brim is set to 6mm, extrusion width 0.5mm")  {
	        Slic3r::Print print;
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
	        	{ "first_layer_extrusion_width", 	1 },
	        	{ "brim_width", 					6 },
	        	{ "first_layer_extrusion_width", 	0.5 }
	        });
			print.process();
            THEN("Brim Extrusion collection has 12 loops in it") {
                size_t total_items = 0;
                for (const auto& pair : print.get_brimMap()) {
                    total_items += pair.second.items_count();
                }
                REQUIRE(total_items == 14);
            }
        }
    }
}
