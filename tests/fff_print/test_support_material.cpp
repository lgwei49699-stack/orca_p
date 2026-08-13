#include <catch2/catch.hpp>

#include <algorithm>
#include <array>

#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Layer.hpp"

#include "test_data.hpp" // get access to init_print, etc

using namespace Slic3r::Test;
using namespace Slic3r;

TEST_CASE("SupportMaterial: Three raft layers created", "[SupportMaterial]")
{
	Slic3r::Print print;
	Slic3r::Test::init_and_process_print({ TestMesh::cube_20x20x20 }, print, {
		{ "support_material", 1 },
		{ "raft_layers",      3 }
		});
    REQUIRE(print.objects().front()->support_layers().size() == 3);
}

TEST_CASE("Tree support prints a tilted object without an empty initial layer", "[SupportMaterial][TreeSupport]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_support", true);
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTreeAuto));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsTreeOrganic));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.2);
    config.set("independent_support_layer_height", true);
    config.set("support_threshold_angle", 30);
    config.set("support_remove_small_overhang", true);
    config.set("support_top_z_distance", 0.2);

    for (int top_interface_layers : {0, 2}) {
        config.set("support_interface_top_layers", top_interface_layers);
        for (double z_rotation : {0.0, 37.0, 122.0}) {
            INFO("Top interface layers: " << top_interface_layers << ", Z rotation: " << z_rotation);
            TriangleMesh tilted_cube = Slic3r::Test::mesh(TestMesh::cube_20x20x20);
            tilted_cube.rotate_x(float(Geometry::deg2rad(15.0)));
            tilted_cube.rotate_z(float(Geometry::deg2rad(z_rotation)));

            Model model;
            ModelObject *model_object = model.add_object();
            model_object->name = "tilted tree support cube";
            model_object->add_volume(std::move(tilted_cube));
            ModelInstance *instance = model_object->add_instance();
            instance->set_offset(Vec3d(100.0, 100.0, 0.0));
            model_object->ensure_on_bed();

            Print print;
            print.auto_assign_extruders(model_object);
            print.apply(model, config);
            print.validate();
            print.set_status_silent();
            print.process();

            ConstSupportLayerPtrsAdaptor support_layers = print.objects().front()->support_layers();
            REQUIRE_FALSE(support_layers.empty());
            REQUIRE(support_layers.front()->print_z == Approx(0.2));
            REQUIRE(support_layers.front()->has_extrusions());
            REQUIRE(std::any_of(support_layers.begin(), support_layers.end(), [](const SupportLayer *layer) {
                return layer->height > EPSILON && layer->has_extrusions();
            }));
            REQUIRE((print.objects().front()->layers().front()->has_extrusions() ||
                     print.objects().front()->support_layers().front()->has_extrusions()));
        }
    }
}

TEST_CASE("Tree support replaces an unextrudable object first layer with a build plate contact", "[SupportMaterial][TreeSupport]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_support", true);
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stTreeAuto));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsDefault));
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.36);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.8, false));
    config.set_key_value("line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set("independent_support_layer_height", true);
    config.set("support_threshold_angle", 30);
    config.set("support_remove_small_overhang", true);
    config.set("support_top_z_distance", 0.2);
    config.set_key_value("wall_generator", new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));
    config.set_key_value("initial_layer_min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_feature_size", new ConfigOptionPercent(75.0));
    config.set("min_length_factor", 2.0);

    const std::array<std::array<double, 9>, 9> orientations{{
        {{0.932623355, 0., 0.360851323, 0., 1., 0., -0.360851323, 0., 0.932623355}},
        {{1.11022302e-16, 0.939692621, 0.342020143, -1., 1.11022302e-16, -2.55708605e-33, -5.55111512e-17, -0.342020143, 0.939692621}},
        {{-6.72514462e-33, 0.307348196, 0.951597124, -1., 4.71364137e-34, -5.55111512e-17, -5.55111512e-17, -0.951597124, 0.307348196}},
        {{-6.58978231e-16, -0.939692621, 0.342020143, 0.711141528, -0.240456878, -0.660649844, 0.703048879, 0.243224727, 0.668254447}},
        {{-0.516378815, -0.804648755, 0.29307559, 0.580505767, -0.580505747, -0.57098698, 0.629576027, -0.12471351, 0.766864112}},
        {{-0.707106781, -0.707106781, 1.11022302e-16, 0.498428932, -0.498428932, -0.709321647, 0.501566147, -0.501566147, 0.704884956}},
        {{-0.857894124, -0.513826501, -1.11022302e-16, 0.368441802, -0.615157171, -0.69701671, 0.358145657, -0.597966539, 0.717054884}},
        {{-0.707106781, -0.707106781, 5.55111512e-17, 0.491682622, -0.491682622, -0.718676838, 0.508181266, -0.508181266, 0.695344233}},
        {{1., 0., 0., 0., 1., 0., 0., 0., 1.}},
    }};

    for (size_t orientation_idx = 0; orientation_idx < orientations.size(); ++orientation_idx) {
        INFO("Orientation index: " << orientation_idx);
        TriangleMesh tilted_cube = make_cube(10.1, 10.1, 10.1);
        Transform3d  transform    = Transform3d::Identity();
        for (size_t row = 0; row < 3; ++row)
            for (size_t column = 0; column < 3; ++column)
                transform.linear()(row, column) = orientations[orientation_idx][3 * row + column];
        tilted_cube.transform(transform);

        Model model;
        ModelObject *model_object = model.add_object();
        model_object->name = "cube resting on an unextrudable edge";
        model_object->add_volume(std::move(tilted_cube));
        ModelInstance *instance = model_object->add_instance();
        instance->set_offset(Vec3d(100.0, 100.0, 0.0));
        model_object->ensure_on_bed();

        Print print;
        print.auto_assign_extruders(model_object);
        print.apply(model, config);
        print.validate();
        print.set_status_silent();
        print.process();

        const PrintObject *object = print.objects().front();
        REQUIRE_FALSE(object->layers().empty());
        if (!object->layers().front()->has_extrusions()) {
            REQUIRE_FALSE(object->support_layers().empty());
            REQUIRE(object->support_layers().front()->print_z == Approx(0.36));
            REQUIRE(object->support_layers().front()->has_extrusions());
        }
        REQUIRE((object->layers().front()->has_extrusions() ||
                 (!object->support_layers().empty() && object->support_layers().front()->has_extrusions())));
    }
}

TEST_CASE("Normal support does not treat an empty extrusion layer as bearing geometry", "[SupportMaterial][NormalSupport]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_support", true);
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stNormalAuto));
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.36);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.8, false));
    config.set_key_value("line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set_key_value("outer_wall_line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set_key_value("inner_wall_line_width", new ConfigOptionFloatOrPercent(0.45, false));
    config.set("independent_support_layer_height", true);
    config.set("support_threshold_angle", 30);
    config.set("support_remove_small_overhang", true);
    config.set("support_top_z_distance", 0.2);
    config.set_key_value("wall_generator", new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));
    config.set_key_value("initial_layer_min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_feature_size", new ConfigOptionPercent(75.0));
    config.set("min_length_factor", 2.0);
    config.set("wall_loops", 2);
    config.set_key_value("sparse_infill_density", new ConfigOptionPercent(15.0));

    const std::array<std::array<double, 9>, 9> orientations{{
        {{0.932623355, 0., 0.360851323, 0., 1., 0., -0.360851323, 0., 0.932623355}},
        {{1.11022302e-16, 0.939692621, 0.342020143, -1., 1.11022302e-16, -2.55708605e-33, -5.55111512e-17, -0.342020143, 0.939692621}},
        {{-6.72514462e-33, 0.307348196, 0.951597124, -1., 4.71364137e-34, -5.55111512e-17, -5.55111512e-17, -0.951597124, 0.307348196}},
        {{-6.58978231e-16, -0.939692621, 0.342020143, 0.711141528, -0.240456878, -0.660649844, 0.703048879, 0.243224727, 0.668254447}},
        {{-0.516378815, -0.804648755, 0.29307559, 0.580505767, -0.580505747, -0.57098698, 0.629576027, -0.12471351, 0.766864112}},
        {{-0.707106781, -0.707106781, 1.11022302e-16, 0.498428932, -0.498428932, -0.709321647, 0.501566147, -0.501566147, 0.704884956}},
        {{-0.857894124, -0.513826501, -1.11022302e-16, 0.368441802, -0.615157171, -0.69701671, 0.358145657, -0.597966539, 0.717054884}},
        {{-0.707106781, -0.707106781, 5.55111512e-17, 0.491682622, -0.491682622, -0.718676838, 0.508181266, -0.508181266, 0.695344233}},
        {{1., 0., 0., 0., 1., 0., 0., 0., 1.}},
    }};

    for (size_t orientation_idx = 0; orientation_idx < orientations.size(); ++orientation_idx) {
        TriangleMesh tilted_cube = make_cube(10.1, 10.1, 10.1);
        Transform3d  transform    = Transform3d::Identity();
        for (size_t row = 0; row < 3; ++row)
            for (size_t column = 0; column < 3; ++column)
                transform.linear()(row, column) = orientations[orientation_idx][3 * row + column];
        tilted_cube.transform(transform);

        Model model;
        ModelObject *model_object = model.add_object();
        model_object->name = "tilted cube";
        model_object->add_volume(std::move(tilted_cube));
        model_object->add_instance();
        model_object->ensure_on_bed();

        Print print;
        print.auto_assign_extruders(model_object);
        print.apply(model, config);
        print.validate();
        print.set_status_silent();
        print.process();

        const PrintObject *object = print.objects().front();
        INFO("Orientation index: " << orientation_idx);
        REQUIRE_FALSE(object->layers().empty());
        INFO("Object first layer has extrusions: " << object->layers().front()->has_extrusions());
        INFO("Support layer count: " << object->support_layers().size());
        if (!object->layers().front()->has_extrusions()) {
            ConstSupportLayerPtrsAdaptor support_layers = object->support_layers();
            REQUIRE_FALSE(support_layers.empty());
            REQUIRE(support_layers.front()->has_extrusions());
        }
        REQUIRE((object->layers().front()->has_extrusions() ||
                 (!object->support_layers().empty() && object->support_layers().front()->has_extrusions())));
    }
}

TEST_CASE("Normal support uses extrusion coverage as the bearing surface", "[SupportMaterial][NormalSupport]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("enable_support", true);
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stNormalAuto));
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.2);
    config.set_key_value("initial_layer_line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set_key_value("line_width", new ConfigOptionFloatOrPercent(0.42, false));
    config.set("independent_support_layer_height", true);
    config.set("support_threshold_angle", 15);
    config.set("support_remove_small_overhang", true);
    config.set("support_top_z_distance", 0.2);
    config.set("support_object_xy_distance", 0.1);
    config.set_key_value("wall_generator", new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));
    config.set_key_value("initial_layer_min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_bead_width", new ConfigOptionPercent(85.0));
    config.set_key_value("min_feature_size", new ConfigOptionPercent(75.0));

    // The 0.1 mm ribs are geometric slices but are below the 0.3 mm minimum
    // feature size, so they produce no extrusion. Their 1.2 mm pitch is small
    // enough that expanding the raw geometry by the overhang threshold makes
    // the 10x10 mm platform above look fully supported.
    TriangleMesh mesh;
    for (size_t rib_idx = 0; rib_idx <= 10; ++rib_idx) {
        TriangleMesh rib = make_cube(12.0, 0.1, 1.0);
        rib.translate(0.0, float(rib_idx) * 1.2f, 0.0);
        mesh.merge(rib);
    }
    TriangleMesh platform = make_cube(10.0, 10.0, 10.0);
    platform.translate(1.0, 1.0, 1.0);
    mesh.merge(platform);

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->name = "platform over unextrudable ribs";
    model_object->add_volume(std::move(mesh));
    model_object->add_instance();
    model_object->ensure_on_bed();

    Print print;
    print.auto_assign_extruders(model_object);
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    print.process();

    const PrintObject *object = print.objects().front();
    REQUIRE_FALSE(object->layers().empty());
    REQUIRE_FALSE(object->layers().front()->has_extrusions());
    REQUIRE_FALSE(object->support_layers().empty());
    REQUIRE(object->support_layers().front()->has_extrusions());
    REQUIRE(object->support_layers().front()->print_z == Approx(0.2));
}

SCENARIO("SupportMaterial: support_layers_z and contact_distance", "[SupportMaterial]")
{
    // Box h = 20mm, hole bottom at 5mm, hole height 10mm (top edge at 15mm).
    TriangleMesh mesh = Slic3r::Test::mesh(Slic3r::Test::TestMesh::cube_with_hole);
    mesh.rotate_x(float(M_PI / 2));
//    mesh.write_binary("d:\\temp\\cube_with_hole.stl");

	auto check = [](Slic3r::Print &print, bool &first_support_layer_height_ok, bool &layer_height_minimum_ok, bool &layer_height_maximum_ok, bool &top_spacing_ok)
	{
        ConstSupportLayerPtrsAdaptor support_layers = print.objects().front()->support_layers();

		first_support_layer_height_ok = support_layers.front()->print_z == print.config().initial_layer_print_height.value;

		layer_height_minimum_ok = true;
		layer_height_maximum_ok = true;
		double min_layer_height = print.config().min_layer_height.values.front();
		double max_layer_height = print.config().nozzle_diameter.values.front();
		if (print.config().max_layer_height.values.front() > EPSILON)
			max_layer_height = std::min(max_layer_height, print.config().max_layer_height.values.front());
		for (size_t i = 1; i < support_layers.size(); ++ i) {
			if (support_layers[i]->print_z - support_layers[i - 1]->print_z < min_layer_height - EPSILON)
				layer_height_minimum_ok = false;
			if (support_layers[i]->print_z - support_layers[i - 1]->print_z > max_layer_height + EPSILON)
				layer_height_maximum_ok = false;
		}

#if 0
		double expected_top_spacing = print.default_object_config().layer_height + print.config().nozzle_diameter.get_at(0);
		bool wrong_top_spacing = 0;
        std::vector<coordf_t> top_z { 1.1 };
		for (coordf_t top_z_el : top_z) {
			// find layer index of this top surface.
			size_t layer_id = -1;
			for (size_t i = 0; i < support_z.size(); ++ i) {
				if (abs(support_z[i] - top_z_el) < EPSILON) {
					layer_id = i;
					i = static_cast<int>(support_z.size());
				}
			}

			// check that first support layer above this top surface (or the next one) is spaced with nozzle diameter
			if (abs(support_z[layer_id + 1] - support_z[layer_id] - expected_top_spacing) > EPSILON && 
				abs(support_z[layer_id + 2] - support_z[layer_id] - expected_top_spacing) > EPSILON) {
				wrong_top_spacing = 1;
			}
		}
		d = ! wrong_top_spacing;
#else
		top_spacing_ok = true;
#endif
	};

    GIVEN("A print object having one modelObject") {
        WHEN("First layer height = 0.4") {
			Slic3r::Print print;
			Slic3r::Test::init_and_process_print({ mesh }, print, {
				{ "support_material",	1 },
				{ "layer_height",		0.2 },
				{ "first_layer_height", 0.4 },
                { "dont_support_bridges", false },
			});
			bool a, b, c, d;
            check(print, a, b, c, d);
            THEN("First layer height is honored")					{ REQUIRE(a == true); }
            THEN("No null or negative support layers")				{ REQUIRE(b == true); }
            THEN("No layers thicker than nozzle diameter")			{ REQUIRE(c == true); }
//            THEN("Layers above top surfaces are spaced correctly")	{ REQUIRE(d == true); }
        }
        WHEN("Layer height = 0.2 and, first layer height = 0.3") {
			Slic3r::Print print;
			Slic3r::Test::init_and_process_print({ mesh }, print, {
				{ "support_material",	1 },
				{ "layer_height",		0.2 },
				{ "first_layer_height", 0.3 },
                { "dont_support_bridges", false },
            });
            bool a, b, c, d;
            check(print, a, b, c, d);
            THEN("First layer height is honored")					{ REQUIRE(a == true); }
            THEN("No null or negative support layers")				{ REQUIRE(b == true); }
            THEN("No layers thicker than nozzle diameter")			{ REQUIRE(c == true); }
//            THEN("Layers above top surfaces are spaced correctly")	{ REQUIRE(d == true); }
        }
        WHEN("Layer height = nozzle_diameter[0]") {
			Slic3r::Print print;
			Slic3r::Test::init_and_process_print({ mesh }, print, {
				{ "support_material",	1 },
				{ "layer_height",		0.2 },
				{ "first_layer_height", 0.3 },
                { "dont_support_bridges", false },
            });
            bool a, b, c, d;
            check(print, a, b, c, d);
            THEN("First layer height is honored")					{ REQUIRE(a == true); }
            THEN("No null or negative support layers")				{ REQUIRE(b == true); }
            THEN("No layers thicker than nozzle diameter")			{ REQUIRE(c == true); }
//            THEN("Layers above top surfaces are spaced correctly")	{ REQUIRE(d == true); }
        }
    }
}

#if 0
// Test 8.
TEST_CASE("SupportMaterial: forced support is generated", "[SupportMaterial]")
{
    // Create a mesh & modelObject.
    TriangleMesh mesh = TriangleMesh::make_cube(20, 20, 20);

    Model model = Model();
    ModelObject *object = model.add_object();
    object->add_volume(mesh);
    model.add_default_instances();
    model.align_instances_to_origin();

    Print print = Print();

    std::vector<coordf_t> contact_z = {1.9};
    std::vector<coordf_t> top_z = {1.1};
    print.default_object_config.support_material_enforce_layers = 100;
    print.default_object_config.support_material = 0;
    print.default_object_config.layer_height = 0.2;
    print.default_object_config.set_deserialize("first_layer_height", "0.3");

    print.add_model_object(model.objects[0]);
    print.objects.front()->_slice();

    SupportMaterial *support = print.objects.front()->_support_material();
    auto support_z = support->support_layers_z(contact_z, top_z, print.default_object_config.layer_height);

    bool check = true;
    for (size_t i = 1; i < support_z.size(); i++) {
        if (support_z[i] - support_z[i - 1] <= 0)
            check = false;
    }

    REQUIRE(check == true);
}

// TODO
bool test_6_checks(Print& print)
{
	bool has_bridge_speed = true;

	// Pre-Processing.
	PrintObject* print_object = print.objects.front();
	print_object->infill();
	SupportMaterial* support_material = print.objects.front()->_support_material();
	support_material->generate(print_object);
	// TODO but not needed in test 6 (make brims and make skirts).

	// Exporting gcode.
	// TODO validation found in Simple.pm


	return has_bridge_speed;
}

// Test 6.
SCENARIO("SupportMaterial: Checking bridge speed", "[SupportMaterial]")
{
    GIVEN("Print object") {
        // Create a mesh & modelObject.
        TriangleMesh mesh = TriangleMesh::make_cube(20, 20, 20);

        Model model = Model();
        ModelObject *object = model.add_object();
        object->add_volume(mesh);
        model.add_default_instances();
        model.align_instances_to_origin();

        Print print = Print();
        print.config.brim_width = 0;
        print.config.skirts = 0;
        print.config.skirts = 0;
        print.default_object_config.support_material = 1;
        print.default_region_config.top_solid_layers = 0; // so that we don't have the internal bridge over infill.
        print.default_region_config.bridge_speed = 99;
        print.config.cooling = 0;
        print.config.set_deserialize("first_layer_speed", "100%");

        WHEN("support_material_contact_distance = 0.2") {
            print.default_object_config.support_material_contact_distance = 0.2;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);
            REQUIRE(check == true); // bridge speed is used.
        }

        WHEN("support_material_contact_distance = 0") {
            print.default_object_config.support_material_contact_distance = 0;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);
            REQUIRE(check == true); // bridge speed is not used.
        }

        WHEN("support_material_contact_distance = 0.2 & raft_layers = 5") {
            print.default_object_config.support_material_contact_distance = 0.2;
            print.default_object_config.raft_layers = 5;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);
            REQUIRE(check == true); // bridge speed is used.
        }

        WHEN("support_material_contact_distance = 0 & raft_layers = 5") {
            print.default_object_config.support_material_contact_distance = 0;
            print.default_object_config.raft_layers = 5;
            print.add_model_object(model.objects[0]);

            bool check = test_6_checks(print);

            REQUIRE(check == true); // bridge speed is not used.
        }
    }
}

#endif
