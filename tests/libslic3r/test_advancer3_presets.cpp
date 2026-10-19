#include <catch2/catch.hpp>

#include "libslic3r/GuideMachineMetadata.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace {

using namespace Slic3r;
using nlohmann::json;

const std::string PROFILE_ROOT = std::string(TEST_DATA_DIR) + "/../../resources/profiles";

json read_json(const std::string& path)
{
    INFO(path);
    boost::nowide::ifstream input(path);
    REQUIRE(input.is_open());
    json result;
    input >> result;
    return result;
}

class IsolatedPresetDirectory
{
public:
    IsolatedPresetDirectory()
        : previous(data_dir())
        , path(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("advancer3-presets-%%%%-%%%%"))
    {
        REQUIRE(boost::filesystem::create_directory(path));
        set_data_dir(path.string());
    }

    ~IsolatedPresetDirectory()
    {
        set_data_dir(previous);
        boost::system::error_code error;
        boost::filesystem::remove_all(path, error);
    }

private:
    const std::string             previous;
    const boost::filesystem::path path;
};

void load_vendor(PresetBundle& bundle, const std::string& vendor, const PresetBundle* library = nullptr)
{
    INFO(vendor);
    const auto loaded = bundle.load_vendor_configs_from_json(PROFILE_ROOT, vendor, PresetBundle::LoadSystem,
                                                             ForwardCompatibilitySubstitutionRule::Disable, library);
    REQUIRE(loaded.second > 0);
    REQUIRE_FALSE(bundle.has_errors());
}

// Compare the effective configs returned by the shared GUI/CLI loader, not just
// the JSON overrides: this catches accidentally inheriting Custom's base values.
void check_same_print_parameters(const Preset& actual, const Preset& source)
{
    CAPTURE(actual.name, source.name);
    auto actual_config = actual.config;
    auto source_config = source.config;
    for (const char* key : {"inherits", "printer_model", "printer_settings_id", "gfd_device_type", "print_settings_id",
                            "filament_settings_id", "compatible_printers", "default_print_profile", "default_filament_profile"}) {
        actual_config.erase(key);
        source_config.erase(key);
    }
    const auto differences = actual_config.diff(source_config);
    CAPTURE(differences);
    CHECK(differences.empty());
    CHECK(actual_config.keys() == source_config.keys());
}

} // namespace

TEST_CASE("Advancer3 is discoverable in Custom with independent cloud identity", "[Preset][Guide][Advancer3]")
{
    const auto                   catalog = read_json(PROFILE_ROOT + "/Custom.json");
    GuideMachineMetadataResolver resolver(PROFILE_ROOT + "/Custom", catalog.at("machine_list"));
    REQUIRE(resolver.catalog_errors().empty());
    std::string error;
    for (const std::string nozzle : {"0.25", "0.4", "0.6", "0.8"}) {
        const auto metadata = resolver.resolve("Advancer3 " + nozzle + " nozzle", error);
        INFO(error);
        REQUIRE(metadata);
        CHECK(metadata->model == "Advancer3");
        CHECK(metadata->nozzle == nozzle);
    }

    const auto model = read_json(PROFILE_ROOT + "/Custom/machine/Advancer3.json");
    CHECK(model.at("gfd_device_type") == "Advancer3");
    CHECK(model.at("nozzle_diameter") == "0.25;0.4;0.6;0.8");
    CHECK(model.at("default_materials") ==
          "Generic PLA @Advancer3;Generic PLA @Advancer3 0.25 nozzle;Generic PETG @Advancer3;"
          "Generic PETG @Advancer3 0.25 nozzle;Generic ABS @Advancer3;Generic ABS @Advancer3 0.25 nozzle");
    for (const char* key : {"bed_model", "bed_texture", "hotend_model"})
        CHECK(boost::filesystem::is_regular_file(PROFILE_ROOT + "/Custom/" + model.at(key).get<std::string>()));
    CHECK(boost::filesystem::is_regular_file(PROFILE_ROOT + "/Custom/Advancer3_cover.png"));

    const auto buttons = read_json(PROFILE_ROOT + "/../gfd_button_config.json").at("devices").at("Advancer3");
    CHECK(buttons.at("print_device_types") == json::array({"Advancer3"}));
    CHECK(buttons.at("print") == true);
    CHECK(buttons.at("cloud_import") == true);
    CHECK(buttons.at("upload_config") == true);
    CHECK(buttons.at("save_config") == true);
    CHECK(buttons.at("print_3mf") == false);
}

TEST_CASE("Advancer3 loads without Flashforge and preserves AD5X effective parameters", "[Preset][Advancer3]")
{
    IsolatedPresetDirectory directory;
    PresetBundle            library;
    load_vendor(library, "OrcaFilamentLibrary");
    PresetBundle custom;
    load_vendor(custom, "Custom", &library);
    CHECK(custom.vendors.count("Flashforge") == 0);
    PresetBundle flashforge;
    load_vendor(flashforge, "Flashforge", &library);
    const auto catalog = read_json(PROFILE_ROOT + "/Custom.json");
    for (const std::string nozzle : {"0.25", "0.4", "0.6", "0.8"}) {
        const std::string printer_name   = "Advancer3 " + nozzle + " nozzle";
        const Preset*     printer        = custom.printers.find_preset(printer_name, false);
        const Preset*     source_printer = flashforge.printers.find_preset("Flashforge AD5X " + nozzle + " nozzle", false);
        REQUIRE(printer != nullptr);
        REQUIRE(source_printer != nullptr);
        REQUIRE(printer->vendor != nullptr);
        CHECK(printer->vendor->id == "Custom");
        CHECK(printer->config.opt_string("gfd_device_type") == "Advancer3");
        check_same_print_parameters(*printer, *source_printer);

        const auto* default_process = custom.prints.find_preset(printer->config.opt_string("default_print_profile"), false);
        REQUIRE(default_process != nullptr);
        CHECK(is_compatible_with_printer({*default_process, default_process->vendor}, {*printer, printer->vendor}));
        const auto defaults = printer->config.opt<ConfigOptionStrings>("default_filament_profile")->values;
        REQUIRE(defaults.size() == 1);
        CHECK(defaults.front() == "Generic PLA @Advancer3" + std::string(nozzle == "0.25" ? " 0.25 nozzle" : ""));

        size_t compatible_processes = 0;
        for (const auto& entry : catalog.at("process_list")) {
            const std::string name = entry.at("name");
            const auto        pos  = name.find("@Advancer3");
            if (pos == std::string::npos)
                continue;
            const auto* preset = custom.prints.find_preset(name, false);
            REQUIRE(preset != nullptr);
            if (!is_compatible_with_printer({*preset, preset->vendor}, {*printer, printer->vendor}))
                continue;
            ++compatible_processes;
            std::string source_name = name;
            source_name.replace(pos, std::string("@Advancer3").size(), "@FF AD5X");
            const auto* original = flashforge.prints.find_preset(source_name, false);
            REQUIRE(original != nullptr);
            CHECK(preset->config.opt<ConfigOptionStrings>("compatible_printers")->values == std::vector<std::string>{printer_name});
            CHECK_FALSE(is_compatible_with_printer({*preset, preset->vendor}, {*source_printer, source_printer->vendor}));
            check_same_print_parameters(*preset, *original);
        }
        CHECK(compatible_processes == 3);
        for (const std::string material : {"PLA", "PETG", "ABS"}) {
            for (const bool small_nozzle : {false, true}) {
                const std::string name   = "Generic " + material + " @Advancer3" + (small_nozzle ? " 0.25 nozzle" : "");
                const auto*       preset = custom.filaments.find_preset(name, false);
                REQUIRE(preset != nullptr);
                CHECK(is_compatible_with_printer({*preset, preset->vendor}, {*printer, printer->vendor}) ==
                      (small_nozzle == (nozzle == "0.25")));
                CHECK_FALSE(is_compatible_with_printer({*preset, preset->vendor}, {*source_printer, source_printer->vendor}));
                const auto* original = flashforge.filaments.find_preset("Flashforge Generic " + material +
                                                                            (small_nozzle ? " @FF AD5M 0.25 Nozzle" : ""),
                                                                        false);
                REQUIRE(original != nullptr);
                check_same_print_parameters(*preset, *original);
            }
        }
    }

    // Exercise the public multi-vendor loader in an isolated installation.
    // Copy only registered JSON files; neither the real user directory nor the
    // source resources may receive the loader's user/cache directories.
    for (const std::string vendor : {"OrcaFilamentLibrary", "Custom", "Flashforge"}) {
        boost::filesystem::copy_file(PROFILE_ROOT + "/" + vendor + ".json", data_dir() + "/" + vendor + ".json");
        const auto catalog = read_json(PROFILE_ROOT + "/" + vendor + ".json");
        for (const char* list : {"machine_model_list", "machine_list", "process_list", "filament_list"}) {
            if (!catalog.contains(list))
                continue;
            for (const auto& entry : catalog.at(list)) {
                const std::string             relative = vendor + "/" + entry.at("sub_path").get<std::string>();
                const boost::filesystem::path target   = boost::filesystem::path(data_dir()) / relative;
                boost::filesystem::create_directories(target.parent_path());
                boost::filesystem::copy_file(PROFILE_ROOT + "/" + relative, target);
            }
        }
    }
    PresetBundle installed;
    installed.set_is_validation_mode(true);
    AppConfig settings;
    settings.set("preset_folder", "default");
    REQUIRE_NOTHROW(installed.load_presets(settings, ForwardCompatibilitySubstitutionRule::Disable));
    REQUIRE_FALSE(installed.has_errors());
    for (const std::string nozzle : {"0.25", "0.4", "0.6", "0.8"}) {
        CHECK(installed.printers.find_preset("Advancer3 " + nozzle + " nozzle", false) != nullptr);
        CHECK(installed.printers.find_preset("Flashforge AD5X " + nozzle + " nozzle", false) != nullptr);
    }
}
