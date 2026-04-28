#include "GFDConfig.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

namespace Slic3r { namespace GFD {

namespace {

using json = nlohmann::json;

std::string trim_copy(const std::string& value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

EnvironmentConfig resolve_environment(const std::string& env)
{
    if (lower_copy(trim_copy(env)) == Config::ENV_QA) {
        return {Config::ENV_QA, Config::QA_AUTH_BASE_URL, Config::QA_API_BASE_URL};
    }

    return {Config::ENV_PRODUCTION, Config::PRODUCTION_AUTH_BASE_URL, Config::PRODUCTION_API_BASE_URL};
}

std::string get_value(const AppConfig* config, const char* key, const std::string& fallback = {})
{
    if (config == nullptr)
        return fallback;
    const std::string value = config->get(Config::SECTION, key);
    return value.empty() ? fallback : value;
}

std::string configured_environment(const AppConfig* config) { return get_value(config, Config::KEY_ENVIRONMENT, Config::ENV_PRODUCTION); }

std::string config_string_value(const DynamicPrintConfig& config, const char* key)
{
    const auto* opt = config.option<ConfigOptionString>(key);
    return opt != nullptr ? opt->value : std::string();
}

std::string resolve_device_type_from_vendor_model(const std::string& printer_model)
{
    if (GUI::wxGetApp().preset_bundle == nullptr || printer_model.empty())
        return {};

    for (const auto& vendor_it : GUI::wxGetApp().preset_bundle->vendors) {
        const VendorProfile& vendor = vendor_it.second;
        const auto model_it = std::find_if(vendor.models.begin(), vendor.models.end(),
                                           [&printer_model](const VendorProfile::PrinterModel& model) {
                                               return model.id == printer_model || model.name == printer_model || model.model_id == printer_model;
                                           });
        if (model_it != vendor.models.end() && !model_it->gfd_device_type.empty())
            return model_it->gfd_device_type;
    }

    return {};
}

std::map<std::string, std::string> load_local_machine_device_types()
{
    std::map<std::string, std::string> device_types;

    const boost::filesystem::path profiles_dir = (boost::filesystem::path(resources_dir()) / "profiles").make_preferred();
    if (!boost::filesystem::exists(profiles_dir))
        return device_types;

    for (const auto& vendor_entry : boost::filesystem::directory_iterator(profiles_dir)) {
        const boost::filesystem::path vendor_file = vendor_entry.path();
        if (!boost::filesystem::is_regular_file(vendor_file) || vendor_file.extension() != ".json")
            continue;

        try {
            boost::nowide::ifstream ifs(vendor_file.string());
            json vendor_json;
            ifs >> vendor_json;

            const auto machine_models_it = vendor_json.find("machine_model_list");
            if (machine_models_it == vendor_json.end() || !machine_models_it->is_array())
                continue;

            const boost::filesystem::path vendor_dir = profiles_dir / vendor_file.stem();
            for (const auto& item : *machine_models_it) {
                if (!item.is_object())
                    continue;

                const std::string list_name = item.value("name", std::string());
                const std::string sub_path  = item.value("sub_path", std::string());
                if (sub_path.empty())
                    continue;

                const boost::filesystem::path machine_model_file = (vendor_dir / sub_path).make_preferred();
                if (!boost::filesystem::exists(machine_model_file))
                    continue;

                boost::nowide::ifstream model_ifs(machine_model_file.string());
                json model_json;
                model_ifs >> model_json;

                const std::string gfd_device_type = model_json.value("gfd_device_type", std::string());
                if (gfd_device_type.empty())
                    continue;

                const std::string model_name = model_json.value("name", std::string());
                const std::string model_id   = model_json.value("model_id", std::string());
                if (!list_name.empty())
                    device_types[list_name] = gfd_device_type;
                if (!model_name.empty())
                    device_types[model_name] = gfd_device_type;
                if (!model_id.empty())
                    device_types[model_id] = gfd_device_type;
            }
        } catch (const std::exception& ex) {
            BOOST_LOG_TRIVIAL(warning) << "GFD load local machine model config failed"
                                       << ", file=" << vendor_file.string()
                                       << ", error=" << ex.what();
        }
    }

    return device_types;
}

std::string resolve_device_type_from_local_config(const std::string& printer_model)
{
    if (printer_model.empty())
        return {};

    static std::string cached_resources_dir;
    static std::map<std::string, std::string> cached_device_types;

    if (cached_resources_dir != resources_dir() || cached_device_types.empty()) {
        cached_resources_dir = resources_dir();
        cached_device_types  = load_local_machine_device_types();
    }

    const auto it = cached_device_types.find(printer_model);
    return it != cached_device_types.end() ? it->second : std::string();
}

const std::map<std::string, std::string>& cached_local_machine_device_types()
{
    static std::string cached_resources_dir;
    static std::map<std::string, std::string> cached_device_types;

    if (cached_resources_dir != resources_dir() || cached_device_types.empty()) {
        cached_resources_dir = resources_dir();
        cached_device_types  = load_local_machine_device_types();
    }

    return cached_device_types;
}

std::string resolve_device_type_from_preset(const Preset* preset)
{
    if (preset == nullptr)
        return {};

    std::string gfd_device_type = config_string_value(preset->config, "gfd_device_type");
    if (!gfd_device_type.empty())
        return gfd_device_type;

    gfd_device_type = resolve_device_type_from_vendor_model(config_string_value(preset->config, "printer_model"));
    if (!gfd_device_type.empty())
        return gfd_device_type;

    gfd_device_type = resolve_device_type_from_local_config(config_string_value(preset->config, "printer_model"));
    if (!gfd_device_type.empty())
        return gfd_device_type;

    if (GUI::wxGetApp().preset_bundle == nullptr)
        return {};

    auto& printers = GUI::wxGetApp().preset_bundle->printers;
    const Preset* base_preset = printers.get_preset_base(*preset);
    return base_preset != nullptr ? PresetUtils::system_printer_gfd_device_type(*base_preset) : std::string();
}

void set_value(AppConfig* config, const char* key, const std::string& value)
{
    if (config == nullptr)
        return;
    config->set(Config::SECTION, key, value);
}

} // namespace

EnvironmentConfig Config::current_environment(const AppConfig* config) { return resolve_environment(configured_environment(config)); }

std::string Config::current_environment_name(const AppConfig* config) { return current_environment(config).name; }

std::string Config::public_key_url(const AppConfig* config) { return current_environment(config).auth_base_url + PATH_PUBLIC_KEY; }

std::string Config::login_url(const AppConfig* config) { return current_environment(config).auth_base_url + PATH_LOGIN; }

std::string Config::verify_url(const AppConfig* config) { return current_environment(config).auth_base_url + PATH_VERIFY; }

std::string Config::obs_token_url(const AppConfig* config) { return current_environment(config).api_base_url + PATH_OBS_TOKEN; }

std::string Config::config_add_url(const AppConfig* config) { return current_environment(config).api_base_url + PATH_CONFIG_ADD; }

std::string Config::config_update_url(const AppConfig* config) { return current_environment(config).api_base_url + PATH_CONFIG_UPDATE; }

std::string Config::device_query_url(const AppConfig* config) { return current_environment(config).api_base_url + PATH_DEVICE_QUERY; }

std::string Config::device_slice_type_url(const AppConfig* config)
{
    return current_environment(config).api_base_url + PATH_DEVICE_SLICE_TYPE;
}

std::string Config::device_print_cmd_url(const AppConfig* config)
{
    return current_environment(config).api_base_url + PATH_DEVICE_PRINT_CMD;
}

std::string Config::explicit_device_type(const DynamicPrintConfig& printer_config)
{
    const std::string printer_model       = config_string_value(printer_config, "printer_model");
    const std::string printer_settings_id = config_string_value(printer_config, "printer_settings_id");
    std::string       gfd_device_type     = config_string_value(printer_config, "gfd_device_type");

    if (!gfd_device_type.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GFD current_device_type from printer config"
                                << ", printer_model=" << printer_model
                                << ", printer_settings_id=" << printer_settings_id
                                << ", gfd_device_type=" << gfd_device_type;
        return gfd_device_type;
    }

    gfd_device_type = resolve_device_type_from_vendor_model(printer_model);
    if (!gfd_device_type.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GFD current_device_type resolved from vendor model"
                                << ", printer_model=" << printer_model
                                << ", printer_settings_id=" << printer_settings_id
                                << ", gfd_device_type=" << gfd_device_type;
        return gfd_device_type;
    }

    gfd_device_type = resolve_device_type_from_local_config(printer_model);
    if (!gfd_device_type.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GFD current_device_type resolved from local machine config"
                                << ", printer_model=" << printer_model
                                << ", printer_settings_id=" << printer_settings_id
                                << ", gfd_device_type=" << gfd_device_type;
        return gfd_device_type;
    }

    BOOST_LOG_TRIVIAL(info) << "GFD explicit_device_type unresolved"
                            << ", printer_model=" << printer_model
                            << ", printer_settings_id=" << printer_settings_id
                            << ", gfd_device_type=<empty>";
    return {};
}

std::string Config::current_device_type(const DynamicPrintConfig& printer_config)
{
    const std::string printer_model       = config_string_value(printer_config, "printer_model");
    const std::string printer_settings_id = config_string_value(printer_config, "printer_settings_id");
    std::string       gfd_device_type     = explicit_device_type(printer_config);
    if (!gfd_device_type.empty())
        return gfd_device_type;

    if (GUI::wxGetApp().preset_bundle != nullptr) {
        auto& printers = GUI::wxGetApp().preset_bundle->printers;

        const Preset& selected_preset = printers.get_selected_preset();
        gfd_device_type = resolve_device_type_from_preset(&selected_preset);
        if (!gfd_device_type.empty()) {
            BOOST_LOG_TRIVIAL(info) << "GFD current_device_type resolved from selected preset"
                                    << ", selected_preset=" << selected_preset.name
                                    << ", printer_model=" << config_string_value(selected_preset.config, "printer_model")
                                    << ", gfd_device_type=" << gfd_device_type;
            return gfd_device_type;
        }

        const Preset& edited_preset = printers.get_edited_preset();
        gfd_device_type = resolve_device_type_from_preset(&edited_preset);
        if (!gfd_device_type.empty()) {
            BOOST_LOG_TRIVIAL(info) << "GFD current_device_type resolved from edited preset"
                                    << ", edited_preset=" << edited_preset.name
                                    << ", printer_model=" << config_string_value(edited_preset.config, "printer_model")
                                    << ", gfd_device_type=" << gfd_device_type;
            return gfd_device_type;
        }
    }

    BOOST_LOG_TRIVIAL(info) << "GFD current_device_type unresolved"
                            << ", printer_model=" << printer_model
                            << ", printer_settings_id=" << printer_settings_id
                            << ", gfd_device_type=<empty>";
    return {};
}

bool Config::is_gfd_printer(const DynamicPrintConfig& printer_config) { return !current_device_type(printer_config).empty(); }

std::vector<std::string> Config::local_gfd_device_types()
{
    std::set<std::string> unique_device_types;
    for (const auto& kv : cached_local_machine_device_types())
        if (!kv.second.empty())
            unique_device_types.insert(kv.second);

    std::vector<std::string> device_types(unique_device_types.begin(), unique_device_types.end());
    return device_types;
}

bool Config::should_show_print_button(const DynamicPrintConfig& printer_config)
{
    if (GUI::wxGetApp().preset_bundle == nullptr)
        return false;

    return !current_device_type(printer_config).empty();
}

bool Config::remember_login(const AppConfig* config) { return get_value(config, KEY_LOGIN_REMEMBER, "true") == "true"; }

std::string Config::cached_username(const AppConfig* config) { return get_value(config, KEY_LOGIN_USERNAME); }

std::string Config::cached_password(const AppConfig* config) { return get_value(config, KEY_LOGIN_PASSWORD); }

std::string Config::auth_token(const AppConfig* config) { return get_value(config, KEY_AUTH_TOKEN); }

std::string Config::verify_token(const AppConfig* config) { return get_value(config, KEY_VERIFY_TOKEN); }

std::string Config::verify_expire_ts(const AppConfig* config) { return get_value(config, KEY_VERIFY_EXPIRE_TS); }

void Config::set_remember_login(AppConfig* config, bool remember) { set_value(config, KEY_LOGIN_REMEMBER, remember ? "true" : "false"); }

void Config::set_cached_username(AppConfig* config, const std::string& username) { set_value(config, KEY_LOGIN_USERNAME, username); }

void Config::set_cached_password(AppConfig* config, const std::string& password) { set_value(config, KEY_LOGIN_PASSWORD, password); }

void Config::set_environment(AppConfig* config, const std::string& environment)
{
    set_value(config, KEY_ENVIRONMENT, lower_copy(trim_copy(environment)));
}

void Config::set_auth_token(AppConfig* config, const std::string& token) { set_value(config, KEY_AUTH_TOKEN, token); }

void Config::set_verify_token(AppConfig* config, const std::string& token) { set_value(config, KEY_VERIFY_TOKEN, token); }

void Config::set_verify_expire_ts(AppConfig* config, const std::string& expire_ts) { set_value(config, KEY_VERIFY_EXPIRE_TS, expire_ts); }

void Config::set_user_email(AppConfig* config, const std::string& email) { set_value(config, KEY_USER_EMAIL, email); }

void Config::set_user_uuid(AppConfig* config, const std::string& uuid) { set_value(config, KEY_USER_UUID, uuid); }

void Config::clear_verify_cache(AppConfig* config)
{
    set_value(config, KEY_VERIFY_TOKEN, "");
    set_value(config, KEY_VERIFY_EXPIRE_TS, "");
}

}} // namespace Slic3r::GFD
