#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Utils.hpp"

#include "test_data.hpp"

#include <algorithm>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/regex.hpp>

using namespace Slic3r;
using namespace Slic3r::Test;

TEST_CASE("Exported total layer counts match emitted layers", "[PrintGCode][LayerCount]")
{
    const int  layout      = GENERATE(0, 1, 2);
    const bool sequential  = layout == 1;
    const bool prime_tower = layout == 2;
    // No raft, legacy raft, and Cura raft.
    const int raft = GENERATE(0, 1, 2);
    // Healthy, empty tail, internal gap, one object's empty tail, support-only tail.
    const int empty_layer = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(sequential, prime_tower, raft, empty_layer);
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("printable_area", new ConfigOptionPoints({{0., 0.}, {240., 0.}, {240., 240.}, {0., 240.}}));
    config.set_key_value("bed_exclude_area", new ConfigOptionPoints());
    config.set_key_value("print_sequence",
                         new ConfigOptionEnum<PrintSequence>(sequential ? PrintSequence::ByObject : PrintSequence::ByLayer));
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(raft == 2 ? RaftMode::CuraV1 : RaftMode::Legacy));
    config.set_key_value("support_type", new ConfigOptionEnum<SupportType>(stNormalAuto));
    config.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsGrid));
    config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btNoBrim));
    config.set("skirt_loops", 0);
    config.set("enable_support", false);
    config.set("enable_prime_tower", prime_tower);
    if (prime_tower) {
        config.set_num_filaments(2);
        config.set_num_extruders(2);
        config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75}));
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.4}));
        config.set_key_value("filament_colour", new ConfigOptionStrings({"#00FF00", "#FF0000"}));
        config.set("single_extruder_multi_material", true);
        config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats({0., 60., 60., 0.}));
        config.set_key_value("wipe_tower_x", new ConfigOptionFloats({180.}));
        config.set_key_value("wipe_tower_y", new ConfigOptionFloats({160.}));
    }
    config.set("layer_height", 0.2);
    config.set("initial_layer_print_height", 0.2);
    config.set("raft_layers", raft ? 3 : 0);
    config.set("raft_base_layers", 1);
    config.set("raft_interface_layers", 1);
    config.set("raft_surface_layers", 1);
    config.set("before_layer_change_gcode", std::string("G92 E0\n; observed_total={total_layer_count}"));
    config.set("machine_start_gcode", std::string("SET_PRINT_STATS_INFO TOTAL_LAYER={total_layer_count}"));
    config.set("machine_end_gcode", std::string("; end_total={total_layer_count}"));
    // Supply enum maps for configuration serialization, as in other export fixtures.
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
                                       boost::filesystem::unique_path("orca-layer-count-%%%%-%%%%.gcode");
        ExportGuard() { set_resources_dir(std::string(TEST_DATA_DIR) + "/../../resources"); }
        ~ExportGuard()
        {
            set_resources_dir(previous_resources);
            boost::system::error_code ignored;
            boost::filesystem::remove(path, ignored);
        }
    } output;
    Model model;
    // Two different objects and an extra instance: by-layer must merge their
    // Z levels, while sequential printing must count each instance separately.
    for (int i = 0; i < 2; ++i) {
        auto* object = model.add_object();
        object->name = "layer count " + std::to_string(i);
        auto* volume = object->add_volume(make_cube(8. + i, 8., 2.), ModelVolumeType::MODEL_PART, false);
        if (prime_tower)
            volume->config.set("extruder", i + 1);
        object->add_instance()->set_offset(Vec3d(40. + 70. * i, 40., 0.));
        if (i == 0)
            object->add_instance()->set_offset(Vec3d(40., 120., 0.));
    }
    Print print;
    print.set_status_silent();
    for (auto* object : model.objects)
        print.auto_assign_extruders(object);
    print.apply(model, config);
    INFO(print.validate().string);
    REQUIRE(print.validate().string.empty());
    print.process();
    REQUIRE(print.objects().size() == 2);
    if (prime_tower) {
        CAPTURE(print.config().enable_prime_tower.value, print.config().filament_diameter.size(), print.extruders().size());
        REQUIRE(print.extruders().size() == 2);
        REQUIRE(print.has_wipe_tower());
        REQUIRE(print.tool_ordering().has_wipe_tower());
        REQUIRE(print.wipe_tower_data().number_of_toolchanges > 0);
    }
    const size_t object_layers = print.objects().front()->layers().size();
    REQUIRE(object_layers > 3);
    size_t raft_layers = 0;
    for (const auto* layer : print.objects().front()->support_layers())
        if (layer->has_extrusions())
            ++raft_layers;
    CHECK((raft_layers > 0) == (raft != 0));
    if (empty_layer) {
        for (auto* object : print.objects()) {
            if (empty_layer == 3 && object != print.objects().front())
                continue;
            auto* layer = empty_layer == 2 ? object->layers()[object_layers / 2] : object->layers().back();
            for (auto* region : layer->regions()) {
                region->perimeters.clear();
                region->fills.clear();
            }
            REQUIRE_FALSE(layer->has_extrusions());
            if (empty_layer == 4) {
                auto*         support = object->add_support_layer(int(object->support_layers().size()), 0, layer->height, layer->print_z);
                ExtrusionPath path(erSupportMaterial, 0.09, 0.45f, float(layer->height));
                path.polyline = Polyline({Point::new_scale(1., 1.), Point::new_scale(4., 1.)});
                support->support_fills.append(path);
            }
        }
        // Rebuild tool selection from the injected no-path layer, without
        // regenerating its perimeters/infill or changing planned layer count.
        config.set("prime_tower_width", config.opt_float("prime_tower_width") + 1.);
        print.apply(model, config);
        REQUIRE_FALSE(print.is_step_done(psWipeTower));
        REQUIRE(print.objects().front()->is_step_done(posInfill));
        print.process();
        REQUIRE(print.objects().front()->layers().size() == object_layers);
    }
    GCodeProcessorResult result;
    print.export_gcode(output.path.string(), &result);
    std::ifstream input(output.path.string());
    REQUIRE(input.good());
    const std::string gcode{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const std::string layer_tag = ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Layer_Change) + "\n";
    size_t            emitted   = 0;
    for (size_t pos = 0; (pos = gcode.find(layer_tag, pos)) != std::string::npos; pos += layer_tag.size())
        ++emitted;
    const size_t skipped  = empty_layer == 1 || empty_layer == 2 ? (sequential ? 3 : 1) : empty_layer == 3 && sequential ? 2 : 0;
    const size_t expected = (object_layers + raft_layers) * (sequential ? 3 : 1) - skipped;
    if (!prime_tower)
        REQUIRE(emitted == expected);
    // Wipe-tower gap layers may have no model/support extrusions. Their actual
    // emitted tags, not the planned support-layer list, are the source of truth.
    REQUIRE(emitted > 0);
    const auto check_count = [&](const std::string& prefix) {
        size_t occurrences = 0;
        for (size_t pos = 0; (pos = gcode.find(prefix, pos)) != std::string::npos; pos += prefix.size()) {
            ++occurrences;
            CHECK(std::stoul(gcode.substr(pos + prefix.size())) == emitted);
        }
        REQUIRE(occurrences > 0);
        return occurrences;
    };
    CHECK(check_count("\nSET_PRINT_STATS_INFO TOTAL_LAYER=") == 1);
    CHECK(check_count("\n; total layer number: ") == 1);
    CHECK(check_count("\n; total layers count = ") == 1);
    CHECK(check_count("\n; end_total=") == 1);
    CHECK(check_count("\n; observed_total=") == emitted);
}

boost::regex perimeters_regex("G1 X[-0-9.]* Y[-0-9.]* E[-0-9.]* ; perimeter");
boost::regex infill_regex("G1 X[-0-9.]* Y[-0-9.]* E[-0-9.]* ; infill");
boost::regex skirt_regex("G1 X[-0-9.]* Y[-0-9.]* E[-0-9.]* ; skirt");

static std::vector<int> m73_progress_values(const std::string& gcode)
{
    std::vector<int> values;
    static const boost::regex m73_regex("(^|\\n)M73 P([0-9]+) R[0-9]+");
    for (boost::sregex_iterator it(gcode.begin(), gcode.end(), m73_regex), end; it != end; ++it)
        values.emplace_back(std::stoi((*it)[2].str()));
    return values;
}

SCENARIO( "PrintGCode basic functionality", "[PrintGCode]") {
    GIVEN("A default configuration and a print test object") {
        WHEN("the output is executed with no support material") {
            Slic3r::Print print;
            Slic3r::Model model;
            Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, {
                { "layer_height",					0.2 },
                { "first_layer_height",				0.2 },
                { "first_layer_extrusion_width",	0 },
                { "gcode_comments",					true },
                { "start_gcode",					"" }
                });
            std::string gcode = Slic3r::Test::gcode(print);
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains slic3r version") {
                REQUIRE(gcode.find(SLIC3R_VERSION) != std::string::npos);
            }
            //THEN("Exported text contains git commit id") {
            //    REQUIRE(gcode.find("; Git Commit") != std::string::npos);
            //    REQUIRE(gcode.find(SLIC3R_BUILD_ID) != std::string::npos);
            //}
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(gcode.find("; external perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; solid infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; top infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; support material extrusion width") == std::string::npos);
                REQUIRE(gcode.find("; first layer extrusion width") == std::string::npos);
            }
            THEN("Exported text does not contain cooling markers (they were consumed)") {
                REQUIRE(gcode.find(";_EXTRUDE_SET_SPEED") == std::string::npos);
            }

            THEN("GCode preamble is emitted.") {
                REQUIRE(gcode.find("G21 ; set units to millimeters") != std::string::npos);
            }

            THEN("Config options emitted for print config, default region config, default object config") {
                REQUIRE(gcode.find("; first_layer_temperature") != std::string::npos);
                REQUIRE(gcode.find("; layer_height") != std::string::npos);
                REQUIRE(gcode.find("; fill_density") != std::string::npos);
            }
            THEN("Infill is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, infill_regex));
            }
            THEN("Perimeters are emitted.") {
				boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, perimeters_regex));
            }
            THEN("Skirt is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, skirt_regex));
            }
            THEN("final Z height is 20mm") {
                double final_z = 0.0;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    final_z = std::max<double>(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                });
                REQUIRE(final_z == Approx(20.));
            }
        }
        WHEN("output is executed with complete objects and two differently-sized meshes") {
            Slic3r::Print print;
            Slic3r::Model model;
            Slic3r::Test::init_print({TestMesh::cube_20x20x20,TestMesh::cube_20x20x20}, print, model, {
                { "first_layer_extrusion_width",    0 },
                { "first_layer_height",             0.3 },
                { "layer_height",                   0.2 },
                { "support_material",               false },
                { "raft_layers",                    0 },
                { "complete_objects",               true },
                { "gcode_comments",                 true },
                { "between_objects_gcode",          "; between-object-gcode" }
                });
            std::string gcode = Slic3r::Test::gcode(print);
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Infill is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, infill_regex));
            }
            THEN("Perimeters are emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, perimeters_regex));
            }
            THEN("Skirt is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, skirt_regex));
            }
            THEN("Between-object-gcode is emitted.") {
                REQUIRE(gcode.find("; between-object-gcode") != std::string::npos);
            }
            THEN("final Z height is 20.1mm") {
                double final_z = 0.0;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                });
                REQUIRE(final_z == Approx(20.1));
            }
            THEN("Z height resets on object change") {
                double final_z = 0.0;
                bool reset = false;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z, &reset] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (final_z > 0 && std::abs(self.z() - 0.3) < 0.01 ) { // saw higher Z before this, now it's lower
                        reset = true;
                    } else {
                        final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                    }
                });
                REQUIRE(reset == true);
            }
            THEN("Shorter object is printed before taller object.") {
                double final_z = 0.0;
                bool reset = false;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z, &reset] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (final_z > 0 && std::abs(self.z() - 0.3) < 0.01 ) { 
                        reset = (final_z > 20.0);
                    } else {
                        final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                    }
                });
                REQUIRE(reset == true);
            }
        }
        WHEN("the output is executed with support material") {
            std::string gcode = ::Test::slice({TestMesh::cube_20x20x20}, {
                { "first_layer_extrusion_width",    0 },
                { "support_material",               true },
                { "raft_layers",                    3 },
                { "gcode_comments",                 true }
                });
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(gcode.find("; external perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; solid infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; top infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; support material extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; first layer extrusion width") == std::string::npos);
            }
            THEN("Raft is emitted.") {
                REQUIRE(gcode.find("; raft") != std::string::npos);
            }
        }
        WHEN("the output is executed with a separate first layer extrusion width") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
                { "first_layer_extrusion_width", "0.5" }
                });
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(gcode.find("; external perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; solid infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; top infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; support material extrusion width") == std::string::npos);
                REQUIRE(gcode.find("; first layer extrusion width") != std::string::npos);
            }
        }
        WHEN("Cooling is enabled and the fan is disabled.") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
				{ "cooling",                    true },
                { "disable_fan_first_layers",   5 }
                });
            THEN("GCode to disable fan is emitted."){
                REQUIRE(gcode.find("M107") != std::string::npos);
            }
        }
        WHEN("end_gcode exists with layer_num and layer_z") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
				{ "end_gcode",              "; Layer_num [layer_num]\n; Layer_z [layer_z]" },
                { "layer_height",           0.1 },
                { "first_layer_height",     0.1 }
                });
            THEN("layer_num and layer_z are processed in the end gcode") {
                REQUIRE(gcode.find("; Layer_num 199") != std::string::npos);
                REQUIRE(gcode.find("; Layer_z 20") != std::string::npos);
            }
        }
        WHEN("current_extruder exists in start_gcode") {
            {
				std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
					{ "start_gcode", "; Extruder [current_extruder]" }
                });
                THEN("current_extruder is processed in the start gcode and set for first extruder") {
                    REQUIRE(gcode.find("; Extruder 0") != std::string::npos);
                }
            }
			{
                DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
                config.set_num_extruders(4);
                config.set_deserialize_strict({
                    { "start_gcode",                    "; Extruder [current_extruder]" },
                    { "infill_extruder",                2 },
                    { "solid_infill_extruder",          2 },
                    { "perimeter_extruder",             2 },
                    { "support_material_extruder",      2 },
                    { "support_material_interface_extruder", 2 }
                });
                std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
                THEN("current_extruder is processed in the start gcode and set for second extruder") {
                    REQUIRE(gcode.find("; Extruder 1") != std::string::npos);
                }
            }
        }

        WHEN("CLI export forces a tool selection command for a single-extruder print") {
            Slic3r::Print print;
            Slic3r::Model model;
            Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, {
                { "layer_height",                    0.2 },
                { "first_layer_height",              0.2 },
                { "gcode_comments",                  true },
                { "start_gcode",                     "" }
            });

            auto export_gcode = [&print](bool force_toolchange_for_single_extruder) {
                boost::filesystem::path temp = boost::filesystem::unique_path();
                GCodeProcessorResult result;
                print.export_gcode(temp.string(), &result, nullptr, force_toolchange_for_single_extruder);
                std::ifstream file(temp.string());
                std::string gcode((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                boost::filesystem::remove(temp);
                return gcode;
            };

            print.set_status_silent();
            print.process();

            const std::string normal_gcode = export_gcode(false);
            const std::string cli_gcode    = export_gcode(true);

            THEN("default export keeps omitting the initial T0") {
                REQUIRE(normal_gcode.find("T0 ; change extruder") == std::string::npos);
            }

            THEN("CLI export emits the initial T0") {
                REQUIRE(cli_gcode.find("T0 ; change extruder") != std::string::npos);
            }
        }

        WHEN("layer_num represents the layer's index from z=0") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20, TestMesh::cube_20x20x20 }, {
				{ "complete_objects",               true },
                { "gcode_comments",                 true },
                { "layer_gcode",                    ";Layer:[layer_num] ([layer_z] mm)" },
                { "layer_height",                   0.1 },
                { "first_layer_height",             0.1 }
                });
			// End of the 1st object.
            std::string token = ";Layer:199 ";
			size_t pos = gcode.find(token);
			THEN("First and second object last layer is emitted") {
				// First object
				REQUIRE(pos != std::string::npos);
				pos += token.size();
				REQUIRE(pos < gcode.size());
				double z = 0;
				REQUIRE((sscanf(gcode.data() + pos, "(%lf mm)", &z) == 1));
				REQUIRE(z == Approx(20.));
				// Second object
				pos = gcode.find(";Layer:399 ", pos);
				REQUIRE(pos != std::string::npos);
				pos += token.size();
				REQUIRE(pos < gcode.size());
				REQUIRE((sscanf(gcode.data() + pos, "(%lf mm)", &z) == 1));
				REQUIRE(z == Approx(20.));
			}
        }

        WHEN("startup contains a long blocking calibration command") {
            const std::string default_gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
                { "gcode_comments",                  true },
                { "start_gcode",                     "G1 X5 F3000\nG1 X10 F3000\nG29\nG1 X20 F3000\nG1 X30 F3000\n" }
            });
            const std::string smooth_gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
                { "gcode_comments",                  true },
                { "smooth_m73_progress",             1 },
                { "start_gcode",                     "G1 X5 F3000\nG1 X10 F3000\nG29\nG1 X20 F3000\nG1 X30 F3000\n" }
            });

            THEN("M73 progress advances gradually only when the smooth progress flag is enabled") {
                const std::vector<int> default_progress_values = m73_progress_values(default_gcode);
                const std::vector<int> smooth_progress_values = m73_progress_values(smooth_gcode);
                REQUIRE(default_progress_values.size() > 2);
                REQUIRE(smooth_progress_values.size() > 2);
                REQUIRE(default_progress_values[0] == 0);
                REQUIRE(smooth_progress_values[0] == 0);
                REQUIRE(default_progress_values[1] > smooth_progress_values[1]);
                REQUIRE(smooth_progress_values[1] <= 2);
            }
        }
    }
}
