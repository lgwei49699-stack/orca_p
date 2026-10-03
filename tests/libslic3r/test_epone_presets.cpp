#include <catch2/catch.hpp>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/GuideMachineMetadata.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using nlohmann::json;
using namespace Slic3r;

// Never load from or write to the real user profile directory in these tests.
class ScopedPresetDirectory
{
public:
    ScopedPresetDirectory()
        : previous_data_dir(data_dir())
        , path(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("epone-presets-%%%%-%%%%-%%%%"))
    {
        REQUIRE(boost::filesystem::create_directory(path));
        set_data_dir(path.string());
    }

    ~ScopedPresetDirectory()
    {
        set_data_dir(previous_data_dir);
        boost::system::error_code error;
        boost::filesystem::remove_all(path, error);
    }

    ScopedPresetDirectory(const ScopedPresetDirectory&)            = delete;
    ScopedPresetDirectory& operator=(const ScopedPresetDirectory&) = delete;

    std::map<std::string, std::string> file_contents() const
    {
        std::map<std::string, std::string> contents;
        for (const auto& entry : boost::filesystem::recursive_directory_iterator(path)) {
            if (!boost::filesystem::is_regular_file(entry.path()))
                continue;
            boost::nowide::ifstream input(entry.path().string(), std::ios::binary);
            REQUIRE(input.is_open());
            contents.emplace(entry.path().string(), std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()));
            REQUIRE_FALSE(input.bad());
        }
        return contents;
    }

    const std::string             previous_data_dir;
    const boost::filesystem::path path;
};

std::vector<Preset> save_user_family(
    const boost::filesystem::path& user_dir, const std::string& model, double height, double layer_height, int temperature)
{
    PresetBundle defaults;
    Preset       printer(Preset::TYPE_PRINTER, model + " 0.4 nozzle - User", false);
    Preset       print(Preset::TYPE_PRINT, "User process @" + model, false);
    Preset       filament(Preset::TYPE_FILAMENT, "User PLA @" + model, false);
    printer.config  = defaults.printers.default_preset().config;
    print.config    = defaults.prints.default_preset().config;
    filament.config = defaults.filaments.default_preset().config;

    printer.config.set_key_value("printer_model", new ConfigOptionString(model));
    printer.config.set_key_value("printer_settings_id", new ConfigOptionString(printer.name));
    printer.config.set_key_value("printable_height", new ConfigOptionFloat(height));
    printer.config.set_key_value("default_print_profile", new ConfigOptionString(print.name));
    printer.config.set_key_value("default_filament_profile", new ConfigOptionStrings({filament.name}));
    print.config.set_key_value("print_settings_id", new ConfigOptionString(print.name));
    print.config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    print.config.set_key_value("compatible_printers", new ConfigOptionStrings({printer.name}));
    filament.config.set_key_value("filament_settings_id", new ConfigOptionStrings({filament.name}));
    filament.config.set_key_value("nozzle_temperature", new ConfigOptionInts({temperature}));
    filament.config.set_key_value("compatible_printers", new ConfigOptionStrings({printer.name}));

    std::vector<Preset>            presets = {printer, print, filament};
    const std::vector<std::string> folders = {PRESET_PRINTER_NAME, PRESET_PRINT_NAME, PRESET_FILAMENT_NAME};
    for (size_t i = 0; i < presets.size(); ++i) {
        Preset& preset    = presets[i];
        preset.file       = (user_dir / folders[i] / (preset.name + ".json")).string();
        preset.version    = Semver("2.3.0");
        preset.user_id    = "epone-regression";
        preset.setting_id = "user-" + preset.name;
        // Avoid the unrelated legacy .info timestamp upgrade during loading.
        preset.updated_time = 1600000000;
        // The former automatic Pro copies had empty inherits and independent values.
        preset.inherits().clear();
        preset.save(nullptr);
    }
    return presets;
}

void check_user_presets(PresetBundle& bundle, const std::vector<Preset>& expected)
{
    REQUIRE_FALSE(bundle.has_errors());
    size_t                                     user_count  = 0;
    const std::vector<const PresetCollection*> collections = {&bundle.printers, &bundle.prints, &bundle.filaments};
    for (const PresetCollection* collection : collections) {
        for (const Preset& preset : collection->get_presets()) {
            if (preset.is_user())
                ++user_count;
        }
    }
    CHECK(user_count == expected.size());

    for (const Preset& saved : expected) {
        CAPTURE(saved.name);
        PresetCollection& collection = saved.type == Preset::TYPE_PRINTER ? bundle.printers :
                                       saved.type == Preset::TYPE_PRINT   ? bundle.prints :
                                                                            bundle.filaments;
        const Preset*     loaded     = collection.find_preset(saved.name, false);
        REQUIRE(loaded != nullptr);
        CHECK(loaded->is_user());
        CHECK(loaded->loaded);
        CHECK(loaded->inherits().empty());
        CHECK(loaded->user_id == saved.user_id);
        CHECK(loaded->setting_id == saved.setting_id);
        const std::vector<std::string> keys = saved.type == Preset::TYPE_PRINTER ?
                                                  std::vector<std::string>{"printer_model", "printer_settings_id", "printable_height",
                                                                           "default_print_profile", "default_filament_profile"} :
                                              saved.type == Preset::TYPE_PRINT ?
                                                  std::vector<std::string>{"print_settings_id", "layer_height", "compatible_printers"} :
                                                  std::vector<std::string>{"filament_settings_id", "nozzle_temperature",
                                                                           "compatible_printers"};
        for (const std::string& key : keys) {
            CAPTURE(key);
            CHECK(loaded->config.opt_serialize(key) == saved.config.opt_serialize(key));
        }
    }
}

std::string resource_path(const std::string& relative) { return std::string(TEST_DATA_DIR) + "/../../resources/" + relative; }

json read_resource(const std::string& relative)
{
    INFO(relative);
    boost::nowide::ifstream input(resource_path(relative));
    REQUIRE(input.is_open());
    json value;
    input >> value;
    return value;
}

json read_custom_profile(const std::string& kind, const std::string& name)
{
    return read_resource("profiles/Custom/" + kind + "/" + name + ".json");
}

} // namespace

TEST_CASE("Guide machine metadata resolves inheritance without selected printer state", "[Preset][Guide]")
{
    ScopedPresetDirectory directory;
    json                  catalog = json::array();
    auto                  save    = [&](const std::string& name, const json& value) {
        const std::string       file = name + ".json";
        boost::nowide::ofstream output((directory.path / file).string());
        output << value.dump();
        output.close();
        REQUIRE(output.good());
        catalog.push_back({{"name", name}, {"sub_path", file}});
    };
    save("base", {{"printer_model", "Base"}, {"nozzle_diameter", json::array({"0.4"})}});
    save("child", {{"inherits", "base"}, {"printer_model", "Child"}});
    save("grandchild", {{"inherits", "child"}, {"nozzle_diameter", json::array({"0.60"})}});
    save("orphan", {{"inherits", "absent"}});
    save("cycle-a", {{"inherits", "cycle-b"}});
    save("cycle-b", {{"inherits", "cycle-a"}});
    save("null-nozzle", {{"inherits", "base"}, {"nozzle_diameter", nullptr}});
    save("empty-nozzle", {{"inherits", "base"}, {"nozzle_diameter", json::array()}});
    save("zero-nozzle", {{"inherits", "base"}, {"nozzle_diameter", json::array({"0"})}});
    save("negative-nozzle", {{"inherits", "base"}, {"nozzle_diameter", json::array({-0.4})}});
    save("invalid-nozzle", {{"inherits", "base"}, {"nozzle_diameter", json::array({"0.4mm"})}});
    save("empty-model", {{"inherits", "base"}, {"printer_model", " "}});
    save("numeric", {{"inherits", "base"}, {"nozzle_diameter", json::array({0.25})}});
    catalog.push_back({{"name", "missing-file"}, {"sub_path", "missing.json"}});
    catalog.push_back({{"name", "bad-entry"}});

    const auto                   original_files = directory.file_contents();
    GuideMachineMetadataResolver resolver(directory.path, catalog);
    REQUIRE(resolver.catalog_errors().size() == 1);
    std::string error;
    for (const auto name : {"orphan", "cycle-a", "null-nozzle", "empty-nozzle", "zero-nozzle", "negative-nozzle", "invalid-nozzle",
                            "empty-model", "missing-file"}) {
        CAPTURE(name);
        CHECK_FALSE(resolver.resolve(name, error));
        CHECK(error.find(name) != std::string::npos);
        CHECK(error.find(directory.path.string()) != std::string::npos);
        // A failure cannot poison the resolver or block the following valid entry.
        const auto valid = resolver.resolve("grandchild", error);
        REQUIRE(valid);
        CHECK(error.empty());
        CHECK(valid->model == "Child");
        CHECK(valid->nozzle == "0.60");
    }
    const auto direct = resolver.resolve("base", error);
    REQUIRE(direct);
    CHECK(direct->nozzle == "0.4");
    const auto numeric = resolver.resolve("numeric", error);
    REQUIRE(numeric);
    CHECK(numeric->nozzle == "0.25");
    CHECK(directory.file_contents() == original_files);
}

TEST_CASE("Guide loads inherited EP machine nozzles and preserves the source profiles", "[Preset][Guide][EPONE]")
{
    const auto                   catalog = read_resource("profiles/Custom.json");
    GuideMachineMetadataResolver resolver(resource_path("profiles/Custom"), catalog.at("machine_list"));
    REQUIRE(resolver.catalog_errors().empty());
    for (const auto& entry : std::vector<std::pair<std::string, std::string>>{{"EP3 Plus 0.4 nozzle", "EP3Plus"},
                                                                              {"EPONE Pro 0.4 nozzle", "EPONE Pro"},
                                                                              {"EPONE Plus 0.4 nozzle", "EPONE Plus"}}) {
        CAPTURE(entry.first);
        const json before = read_custom_profile("machine", entry.first);
        REQUIRE_FALSE(before.contains("nozzle_diameter"));
        std::string error;
        const auto  metadata = resolver.resolve(entry.first, error);
        INFO(error);
        REQUIRE(metadata);
        CHECK(metadata->model == entry.second);
        CHECK(metadata->nozzle == "0.4");
        CHECK(read_custom_profile("machine", entry.first) == before);
    }
}

TEST_CASE("Guide preserves decimal nozzle identifiers for filament matching", "[Preset][Guide]")
{
    struct Machine
    {
        const char* vendor;
        const char* model;
        const char* nozzle;
    };
    for (const auto& machine : std::vector<Machine>{{"Blocks", "BLOCKS Pro S100", "1.0"},
                                                    {"Voron", "Voron Trident 350", "1.0"},
                                                    {"Ginger Additive", "Ginger G1", "5.0"}}) {
        CAPTURE(machine.vendor, machine.model, machine.nozzle);
        const std::string            root    = std::string("profiles/") + machine.vendor;
        const auto                   catalog = read_resource(root + ".json");
        GuideMachineMetadataResolver resolver(resource_path(root), catalog.at("machine_list"));
        std::string                  error;
        const auto                   metadata = resolver.resolve(std::string(machine.model) + " " + machine.nozzle + " nozzle", error);
        INFO(error);
        REQUIRE(metadata);
        CHECK(metadata->model == machine.model);
        CHECK(metadata->nozzle == machine.nozzle);
        const auto model   = read_resource(root + "/machine/" + machine.model + ".json");
        const auto options = ";" + model.at("nozzle_diameter").get<std::string>() + ";";
        CHECK(options.find(";" + metadata->nozzle + ";") != std::string::npos);
    }
}

TEST_CASE("EPONE Plus resources are registered under independent device and preset names", "[Preset][EPONE]")
{
    const json                                             catalogue = read_resource("profiles/Custom.json");
    const std::vector<std::pair<std::string, std::string>> entries   = {{"machine_model", "EPONE Plus"},
                                                                        {"machine", "EPONE Plus 0.4 nozzle"},
                                                                        {"process", "0.20mm Standard @EPONE Plus"},
                                                                        {"process", "0.24mm Draft @EPONE Plus"},
                                                                        {"filament", "EPONE-PLA Overhang @EPONE Plus"}};
    for (const auto& entry : entries) {
        CAPTURE(entry.first, entry.second);
        const std::string folder  = entry.first == "machine_model" ? "machine" : entry.first;
        size_t            matches = 0;
        for (const auto& item : catalogue.at(entry.first + "_list")) {
            if (item.at("name") == entry.second) {
                ++matches;
                CHECK(item.at("sub_path") == folder + "/" + entry.second + ".json");
            }
        }
        CHECK(matches == 1);
        const json profile = read_custom_profile(folder, entry.second);
        CHECK(profile.at("type") == entry.first);
        CHECK(profile.at("name") == entry.second);
    }

    const json model      = read_custom_profile("machine", "EPONE Plus");
    const json base_model = read_custom_profile("machine", "EPONE");
    CHECK(model.at("model_id") == "EPONEPlus");
    CHECK(model.at("gfd_device_type") == "EPONEPlus");
    CHECK(model.at("nozzle_diameter") == "0.4");
    for (const std::string key : {"bed_model", "bed_texture"}) {
        CHECK(model.at(key) == base_model.at(key));
        CHECK(boost::filesystem::is_regular_file(resource_path("profiles/Custom/" + model.at(key).get<std::string>())));
    }
    CHECK(boost::filesystem::is_regular_file(resource_path("profiles/Custom/EPONE Plus_cover.png")));
    CHECK(model.at("default_materials") == "EPONE-PLA Overhang @EPONE Plus;Generic PLA @System");

    const json machine = read_custom_profile("machine", "EPONE Plus 0.4 nozzle");
    CHECK(machine.at("printer_model") == "EPONE Plus");
    CHECK(machine.at("printer_settings_id") == "EPONE Plus 0.4 nozzle");
    CHECK(machine.at("default_print_profile") == "0.20mm Standard @EPONE Plus");
    CHECK(machine.at("default_filament_profile") == json::array({"EPONE-PLA Overhang @EPONE Plus"}));

    const json buttons      = read_resource("gfd_button_config.json");
    json       plus_buttons = buttons.at("devices").at("EPONEPlus");
    json       base_buttons = buttons.at("devices").at("EPONE");
    CHECK(plus_buttons.at("print_device_types") == json::array({"EPONEPlus"}));
    plus_buttons.erase("print_device_types");
    base_buttons.erase("print_device_types");
    CHECK(plus_buttons == base_buttons);
}

TEST_CASE("EPONE Plus inherits every physical print parameter from EPONE", "[Preset][EPONE]")
{
    const std::vector<std::pair<std::string, std::string>> profiles = {{"machine", "EPONE 0.4 nozzle"},
                                                                       {"process", "0.20mm Standard @EPONE"},
                                                                       {"process", "0.24mm Draft @EPONE"},
                                                                       {"filament", "EPONE-PLA Overhang @EPONE"}};
    for (const auto& entry : profiles) {
        const std::string plus_name = entry.first == "machine" ? "EPONE Plus 0.4 nozzle" : entry.second + " Plus";
        CAPTURE(entry.first, plus_name);
        json       base = read_custom_profile(entry.first, entry.second);
        const json plus = read_custom_profile(entry.first, plus_name);
        REQUIRE(plus.at("inherits") == entry.second);
        CHECK(plus.at("setting_id") != base.at("setting_id"));
        if (entry.first != "machine")
            CHECK(plus.at("compatible_printers") == json::array({"EPONE Plus 0.4 nozzle", "MyMarlin 0.4 nozzle"}));

        // The direct parent is the matching EPONE profile. Only identity and
        // default/compatibility references may differ; all other values stay inherited.
        json effective = base;
        effective.update(plus);
        for (const std::string key : {"name", "inherits", "setting_id", "printer_model", "printer_settings_id", "print_settings_id",
                                      "filament_settings_id", "compatible_printers", "default_print_profile", "default_filament_profile"}) {
            base.erase(key);
            effective.erase(key);
        }
        CHECK(effective == base);
    }
}

TEST_CASE("Loading EPONE Plus user presets preserves files and never creates Pro Plus copies", "[Preset][EPONE][UserLoading]")
{
    ScopedPresetDirectory directory;
    const std::string     user     = "epone-regression";
    const auto            user_dir = directory.path / PRESET_USER_DIR / user;
    std::vector<Preset>   expected = save_user_family(user_dir, "EPONE Plus", 109.0, 0.24, 235);

    SECTION("Only Plus presets exist")
    {
        // The EPONE-to-Pro migration must not synthesize Pro Plus presets.
    }
    SECTION("Existing independent Pro copies retain their user-edited values")
    {
        const auto pro = save_user_family(user_dir, "EPONE Pro", 107.0, 0.16, 225);
        expected.insert(expected.end(), pro.begin(), pro.end());
    }

    const auto original_files = directory.file_contents();
    REQUIRE(original_files.size() == expected.size() * 2); // JSON and .info per preset.
    for (int restart = 0; restart < 2; ++restart) {
        CAPTURE(restart);
        PresetBundle bundle;
        REQUIRE_NOTHROW(bundle.load_user_presets(user, ForwardCompatibilitySubstitutionRule::Disable));
        check_user_presets(bundle, expected);
        CHECK(directory.file_contents() == original_files);

        // A second load must also preserve current selections and all saved values.
        for (size_t family = 0; family < expected.size(); family += 3) {
            bundle.printers.select_preset_by_name(expected[family].name, true);
            bundle.prints.select_preset_by_name(expected[family + 1].name, true);
            bundle.filaments.select_preset_by_name(expected[family + 2].name, true);
            bundle.filament_presets = {expected[family + 2].name};
            REQUIRE_NOTHROW(bundle.load_user_presets(user, ForwardCompatibilitySubstitutionRule::Disable));
            CHECK(bundle.printers.get_selected_preset().name == expected[family].name);
            CHECK(bundle.prints.get_selected_preset().name == expected[family + 1].name);
            CHECK(bundle.filaments.get_selected_preset().name == expected[family + 2].name);
            REQUIRE(bundle.filament_presets.size() == 1);
            CHECK(bundle.filament_presets.front() == expected[family + 2].name);
            check_user_presets(bundle, expected);
            CHECK(directory.file_contents() == original_files);
        }
    }
}

TEST_CASE("Adding EPONE Plus preserves the existing EPONE-to-Pro migration", "[Preset][EPONE][UserLoading]")
{
    ScopedPresetDirectory directory;
    const std::string     user           = "epone-regression";
    const auto            user_dir       = directory.path / PRESET_USER_DIR / user;
    const auto            epone          = save_user_family(user_dir, "EPONE", 103.0, 0.12, 210);
    const auto            plus           = save_user_family(user_dir, "EPONE Plus", 109.0, 0.24, 235);
    const auto            original_files = directory.file_contents();

    PresetBundle bundle;
    REQUIRE_NOTHROW(bundle.load_user_presets(user, ForwardCompatibilitySubstitutionRule::Disable));
    REQUIRE_FALSE(bundle.has_errors());
    const Preset* printer  = bundle.printers.find_preset("EPONE Pro 0.4 nozzle - User", false);
    const Preset* print    = bundle.prints.find_preset("User process @EPONE Pro", false);
    const Preset* filament = bundle.filaments.find_preset("User PLA @EPONE Pro", false);
    REQUIRE(printer != nullptr);
    REQUIRE(print != nullptr);
    REQUIRE(filament != nullptr);
    CHECK(printer->config.opt_string("printer_model") == "EPONE Pro");
    CHECK(printer->config.opt_serialize("printable_height") == epone[0].config.opt_serialize("printable_height"));
    CHECK(print->config.opt_serialize("layer_height") == epone[1].config.opt_serialize("layer_height"));
    CHECK(filament->config.opt_serialize("nozzle_temperature") == epone[2].config.opt_serialize("nozzle_temperature"));
    CHECK(bundle.prints.find_preset("User process @EPONE Pro Plus", false) == nullptr);
    CHECK(bundle.filaments.find_preset("User PLA @EPONE Pro Plus", false) == nullptr);

    std::vector<Preset> expected = epone;
    expected.insert(expected.end(), plus.begin(), plus.end());
    expected.insert(expected.end(), {*printer, *print, *filament});
    check_user_presets(bundle, expected);
    const auto migrated_files = directory.file_contents();
    for (const auto& original : original_files) {
        REQUIRE(migrated_files.count(original.first) == 1);
        CHECK(migrated_files.at(original.first) == original.second);
    }
    REQUIRE_NOTHROW(bundle.load_user_presets(user, ForwardCompatibilitySubstitutionRule::Disable));
    check_user_presets(bundle, expected);
    CHECK(directory.file_contents() == migrated_files);
}
