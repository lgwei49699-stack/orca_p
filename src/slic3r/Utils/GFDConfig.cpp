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

bool starts_with_case_insensitive(const std::string& value, const std::string& prefix)
{
    if (prefix.size() > value.size())
        return false;

    for (size_t index = 0; index < prefix.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(value[index])) != std::tolower(static_cast<unsigned char>(prefix[index])))
            return false;
    }

    return true;
}

bool has_device_alias_boundary(const std::string& value, size_t prefix_length)
{
    if (value.size() <= prefix_length)
        return true;

    // Machine preset names may append a nozzle size (for example
    // "Flashforge Adventurer 5M 0.4 Nozzle"), but another alphabetic token
    // denotes a different model (for example "... 5M Pro").
    size_t suffix_pos = prefix_length;
    while (suffix_pos < value.size() && !std::isalnum(static_cast<unsigned char>(value[suffix_pos])))
        ++suffix_pos;

    if (suffix_pos == value.size())
        return true;

    return !std::isalpha(static_cast<unsigned char>(value[suffix_pos]));
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

std::string credential_environment(const AppConfig* config, const std::string& environment)
{
    return resolve_environment(environment.empty() ? configured_environment(config) : environment).name;
}

json load_login_credential_store(const AppConfig* config)
{
    const std::string value = get_value(config, Config::KEY_LOGIN_CREDENTIALS);
    if (value.empty())
        return json::object();

    try {
        json store = json::parse(value);
        return store.is_object() ? store : json::object();
    } catch (...) {
        return json::object();
    }
}

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
        const VendorProfile& vendor   = vendor_it.second;
        const auto           model_it = std::find_if(vendor.models.begin(), vendor.models.end(),
                                                     [&printer_model](const VendorProfile::PrinterModel& model) {
                                               return model.id == printer_model || model.name == printer_model ||
                                                      model.model_id == printer_model;
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
            json                    vendor_json;
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
                json                    model_json;
                model_ifs >> model_json;

                const std::string gfd_device_type = model_json.value("gfd_device_type", std::string());
                if (gfd_device_type.empty())
                    continue;

                device_types[gfd_device_type] = gfd_device_type;

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
                                       << ", file=" << vendor_file.string() << ", error=" << ex.what();
        }
    }

    return device_types;
}

std::string resolve_device_type_from_local_config(const std::string& printer_model)
{
    if (printer_model.empty())
        return {};

    static std::string                        cached_resources_dir;
    static std::map<std::string, std::string> cached_device_types;

    if (cached_resources_dir != resources_dir() || cached_device_types.empty()) {
        cached_resources_dir = resources_dir();
        cached_device_types  = load_local_machine_device_types();
    }

    const std::string candidate = trim_copy(printer_model);
    const auto        it        = cached_device_types.find(candidate);
    return it != cached_device_types.end() ? it->second : std::string();
}

const std::map<std::string, std::string>& cached_local_machine_device_types()
{
    static std::string                        cached_resources_dir;
    static std::map<std::string, std::string> cached_device_types;

    if (cached_resources_dir != resources_dir() || cached_device_types.empty()) {
        cached_resources_dir = resources_dir();
        cached_device_types  = load_local_machine_device_types();
    }

    return cached_device_types;
}

std::string resolve_device_type_from_local_alias(const std::string& identifier)
{
    const std::string candidate = trim_copy(identifier);
    if (candidate.empty())
        return {};

    const auto& device_types = cached_local_machine_device_types();
    if (const auto it = device_types.find(candidate); it != device_types.end())
        return it->second;

    const auto  lowered_candidate = lower_copy(candidate);
    size_t      best_match_len    = 0;
    std::string resolved_type;
    for (const auto& entry : device_types) {
        const std::string alias = trim_copy(entry.first);
        if (alias.empty() || alias.size() <= best_match_len)
            continue;

        if (!starts_with_case_insensitive(lowered_candidate, lower_copy(alias)))
            continue;
        if (!has_device_alias_boundary(candidate, alias.size()))
            continue;

        best_match_len = alias.size();
        resolved_type  = entry.second;
    }

    return resolved_type;
}

std::string resolve_device_type_from_preset(const Preset* preset)
{
    if (preset == nullptr)
        return {};

    std::string gfd_device_type = config_string_value(preset->config, "gfd_device_type");
    if (const std::string resolved = resolve_device_type_from_local_alias(gfd_device_type); !resolved.empty())
        return resolved;
    if (!gfd_device_type.empty())
        return gfd_device_type;

    gfd_device_type = resolve_device_type_from_vendor_model(config_string_value(preset->config, "printer_model"));
    if (!gfd_device_type.empty())
        return gfd_device_type;

    gfd_device_type = resolve_device_type_from_local_config(config_string_value(preset->config, "printer_model"));
    if (!gfd_device_type.empty())
        return gfd_device_type;

    gfd_device_type = resolve_device_type_from_local_alias(config_string_value(preset->config, "printer_settings_id"));
    if (!gfd_device_type.empty())
        return gfd_device_type;

    gfd_device_type = resolve_device_type_from_local_alias(preset->name);
    if (!gfd_device_type.empty())
        return gfd_device_type;

    if (GUI::wxGetApp().preset_bundle == nullptr)
        return {};

    auto&         printers    = GUI::wxGetApp().preset_bundle->printers;
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

std::string Config::obs_token_url(const AppConfig* config) { return user_api_base_url(config) + PATH_OBS_TOKEN; }

std::string Config::config_add_url(const AppConfig* config) { return current_environment(config).api_base_url + PATH_CONFIG_ADD; }

std::string Config::config_update_url(const AppConfig* config) { return current_environment(config).api_base_url + PATH_CONFIG_UPDATE; }

std::string Config::user_api_base_url(const AppConfig* config)
{
    const std::string configured = get_value(config, KEY_USER_API_BASE_URL);
    if (!configured.empty())
        return configured;
    return current_environment_name(config) == ENV_QA ? QA_USER_API_BASE_URL : PRODUCTION_API_BASE_URL;
}

std::string Config::captcha_generate_url(const AppConfig* config) { return user_api_base_url(config) + PATH_CAPTCHA_GENERATE; }

std::string Config::sms_send_url(const AppConfig* config)
{
    return get_value(config, KEY_SMS_SEND_URL, user_api_base_url(config) + PATH_SMS_SEND);
}

std::string Config::sms_login_url(const AppConfig* config)
{
    return get_value(config, KEY_SMS_LOGIN_URL, user_api_base_url(config) + PATH_SMS_LOGIN);
}

std::string Config::user_info_url(const AppConfig* config)
{
    return get_value(config, KEY_USER_INFO_URL, user_api_base_url(config) + PATH_USER_INFO);
}

std::string Config::token_refresh_url(const AppConfig* config)
{
    return get_value(config, KEY_TOKEN_REFRESH_URL, user_api_base_url(config) + PATH_TOKEN_REFRESH);
}

std::string Config::parameter_sync_api_base_url(const AppConfig* config)
{
    return get_value(config, KEY_PARAMETER_SYNC_API_BASE_URL, current_environment(config).api_base_url);
}

std::string Config::parameter_sync_biz(const AppConfig* config) { return get_value(config, KEY_PARAMETER_SYNC_BIZ, "ZXBMan"); }

std::string Config::device_query_url(const AppConfig* config) { return user_api_base_url(config) + PATH_USER_DEVICE_QUERY; }

std::string Config::device_filament_info_url(const AppConfig* config) { return user_api_base_url(config) + PATH_DEVICE_FILAMENT_INFO; }

std::string Config::device_slice_type_url(const AppConfig* config) { return user_api_base_url(config) + PATH_DEVICE_SLICE_TYPE; }

std::string Config::user_filament_list_url(const AppConfig* config) { return user_api_base_url(config) + PATH_USER_FILAMENT_LIST; }

std::string Config::filament_temperature_detail_url(const AppConfig* config)
{
    return parameter_sync_api_base_url(config) + PATH_FILAMENT_TEMPERATURE_DETAIL;
}

std::string Config::filament_temperature_update_slice_param_url(const AppConfig* config)
{
    return current_environment(config).api_base_url + PATH_FILAMENT_TEMPERATURE_UPDATE_SLICE_PARAM;
}

std::string Config::device_print_cmd_url(const AppConfig* config) { return user_api_base_url(config) + PATH_DEVICE_PRINT_CMD; }

std::string Config::explicit_device_type(const DynamicPrintConfig& printer_config)
{
    const std::string printer_model       = config_string_value(printer_config, "printer_model");
    const std::string printer_settings_id = config_string_value(printer_config, "printer_settings_id");
    const std::string raw_gfd_device_type = config_string_value(printer_config, "gfd_device_type");
    std::string       gfd_device_type     = resolve_device_type_from_local_alias(raw_gfd_device_type);

    if (!gfd_device_type.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GFD current_device_type from printer config"
                                << ", printer_model=" << printer_model << ", printer_settings_id=" << printer_settings_id
                                << ", gfd_device_type=" << gfd_device_type;
        return gfd_device_type;
    }
    if (!raw_gfd_device_type.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GFD current_device_type from printer config"
                                << ", printer_model=" << printer_model << ", printer_settings_id=" << printer_settings_id
                                << ", gfd_device_type=" << raw_gfd_device_type;
        return raw_gfd_device_type;
    }

    gfd_device_type = resolve_device_type_from_vendor_model(printer_model);
    if (!gfd_device_type.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GFD current_device_type resolved from vendor model"
                                << ", printer_model=" << printer_model << ", printer_settings_id=" << printer_settings_id
                                << ", gfd_device_type=" << gfd_device_type;
        return gfd_device_type;
    }

    gfd_device_type = resolve_device_type_from_local_alias(printer_model);
    if (!gfd_device_type.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GFD current_device_type resolved from local machine config"
                                << ", printer_model=" << printer_model << ", printer_settings_id=" << printer_settings_id
                                << ", gfd_device_type=" << gfd_device_type;
        return gfd_device_type;
    }

    gfd_device_type = resolve_device_type_from_local_alias(printer_settings_id);
    if (!gfd_device_type.empty()) {
        BOOST_LOG_TRIVIAL(info) << "GFD current_device_type resolved from printer settings id"
                                << ", printer_model=" << printer_model << ", printer_settings_id=" << printer_settings_id
                                << ", gfd_device_type=" << gfd_device_type;
        return gfd_device_type;
    }

    BOOST_LOG_TRIVIAL(info) << "GFD explicit_device_type unresolved"
                            << ", printer_model=" << printer_model << ", printer_settings_id=" << printer_settings_id
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
        gfd_device_type               = resolve_device_type_from_preset(&selected_preset);
        if (!gfd_device_type.empty()) {
            BOOST_LOG_TRIVIAL(info) << "GFD current_device_type resolved from selected preset"
                                    << ", selected_preset=" << selected_preset.name
                                    << ", printer_model=" << config_string_value(selected_preset.config, "printer_model")
                                    << ", gfd_device_type=" << gfd_device_type;
            return gfd_device_type;
        }

        const Preset& edited_preset = printers.get_edited_preset();
        gfd_device_type             = resolve_device_type_from_preset(&edited_preset);
        if (!gfd_device_type.empty()) {
            BOOST_LOG_TRIVIAL(info) << "GFD current_device_type resolved from edited preset"
                                    << ", edited_preset=" << edited_preset.name
                                    << ", printer_model=" << config_string_value(edited_preset.config, "printer_model")
                                    << ", gfd_device_type=" << gfd_device_type;
            return gfd_device_type;
        }
    }

    BOOST_LOG_TRIVIAL(info) << "GFD current_device_type unresolved"
                            << ", printer_model=" << printer_model << ", printer_settings_id=" << printer_settings_id
                            << ", gfd_device_type=<empty>";
    return {};
}

bool Config::is_gfd_printer(const DynamicPrintConfig& printer_config) { return !current_device_type(printer_config).empty(); }

namespace {

void parse_button_visibility(const json& node, ButtonVisibility& visibility)
{
    auto read_bool = [&node](const char* key, bool& out) {
        if (!node.contains(key) || node[key].is_null())
            return;
        const json& value = node[key];
        if (value.is_boolean())
            out = value.get<bool>();
        else if (value.is_number_integer())
            out = value.get<long long>() != 0;
        else if (value.is_string()) {
            const std::string v = value.get<std::string>();
            out                 = !(v == "0" || lower_copy(trim_copy(v)) == "false");
        }
    };

    read_bool("cloud_import", visibility.cloud_import);
    read_bool("upload_config", visibility.upload_config);
    read_bool("save_config", visibility.save_config);
    read_bool("print", visibility.print);
    read_bool("print_3mf", visibility.print_3mf);

    if (node.contains("print_device_types")) {
        const json& types = node["print_device_types"];
        if (types.is_array()) {
            visibility.print_device_types.clear();
            for (const auto& el : types) {
                if (!el.is_string())
                    continue;
                const std::string entry = trim_copy(el.get<std::string>());
                if (!entry.empty())
                    visibility.print_device_types.push_back(entry);
            }
        } else if (types.is_string()) {
            visibility.print_device_types.clear();
            const std::string entry = trim_copy(types.get<std::string>());
            if (!entry.empty())
                visibility.print_device_types.push_back(entry);
        }
    }
}

struct ButtonConfigData
{
    ButtonVisibility                        default_visibility;
    std::map<std::string, ButtonVisibility> by_device; // key = lowercased device type
};

const ButtonConfigData& cached_button_config()
{
    static const ButtonConfigData data = []() {
        ButtonConfigData              result;
        const boost::filesystem::path path = (boost::filesystem::path(resources_dir()) / "gfd_button_config.json").make_preferred();
        try {
            if (boost::filesystem::exists(path)) {
                boost::nowide::ifstream ifs(path.string());
                json                    root;
                ifs >> root;
                if (root.contains("default") && root["default"].is_object())
                    parse_button_visibility(root["default"], result.default_visibility);
                if (root.contains("devices") && root["devices"].is_object()) {
                    for (auto it = root["devices"].begin(); it != root["devices"].end(); ++it) {
                        if (!it.value().is_object())
                            continue;
                        ButtonVisibility visibility = result.default_visibility;
                        parse_button_visibility(it.value(), visibility);
                        result.by_device[lower_copy(trim_copy(it.key()))] = visibility;
                    }
                }
                BOOST_LOG_TRIVIAL(info) << "GFD button config loaded"
                                        << ", path=" << path.string() << ", device_entries=" << result.by_device.size();
            } else {
                BOOST_LOG_TRIVIAL(info) << "GFD button config not found, using defaults"
                                        << ", path=" << path.string();
            }
        } catch (const std::exception& ex) {
            BOOST_LOG_TRIVIAL(error) << "GFD button config parse failed"
                                     << ", path=" << path.string() << ", error=" << ex.what();
        }
        return result;
    }();
    return data;
}

} // namespace

ButtonVisibility Config::button_visibility(const std::string& device_type)
{
    const ButtonConfigData& data       = cached_button_config();
    const std::string       key        = lower_copy(trim_copy(device_type));
    ButtonVisibility        visibility = data.default_visibility;
    if (!key.empty()) {
        const auto it = data.by_device.find(key);
        if (it != data.by_device.end())
            visibility = it->second;
    }
    if (PARAMETER_SYNC_READ_ONLY) {
        visibility.upload_config = false;
        visibility.save_config   = false;
    }
    return visibility;
}

std::vector<std::string> Config::print_device_types(const std::string& device_type)
{
    return button_visibility(device_type).print_device_types;
}

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

bool Config::remember_login(const AppConfig* config)
{
    const auto credentials = saved_login_credentials(config);
    if (!credentials.empty() && !credentials.front().password.empty())
        return true;
    return get_value(config, KEY_LOGIN_REMEMBER, "true") == "true";
}

std::string Config::cached_username(const AppConfig* config)
{
    const auto credentials = saved_login_credentials(config);
    return credentials.empty() ? trim_copy(get_value(config, KEY_LOGIN_USERNAME)) : credentials.front().username;
}

std::string Config::cached_password(const AppConfig* config)
{
    const auto credentials = saved_login_credentials(config);
    return credentials.empty() ? std::string() : credentials.front().password;
}

std::string Config::auth_token(const AppConfig* config) { return get_value(config, KEY_AUTH_TOKEN); }

std::string Config::auth_mode(const AppConfig* config) { return get_value(config, KEY_AUTH_MODE); }

std::string Config::verify_token(const AppConfig* config) { return get_value(config, KEY_VERIFY_TOKEN); }

std::string Config::verify_expire_ts(const AppConfig* config) { return get_value(config, KEY_VERIFY_EXPIRE_TS); }

std::string Config::user_email(const AppConfig* config) { return get_value(config, KEY_USER_EMAIL); }

std::string Config::user_uuid(const AppConfig* config) { return get_value(config, KEY_USER_UUID); }

std::string Config::user_phone(const AppConfig* config) { return get_value(config, KEY_USER_PHONE); }

std::string Config::user_nickname(const AppConfig* config) { return get_value(config, KEY_USER_NICKNAME); }

std::string Config::user_avatar(const AppConfig* config) { return get_value(config, KEY_USER_AVATAR); }

std::string Config::token_refresh_attempt_ts(const AppConfig* config) { return get_value(config, KEY_TOKEN_REFRESH_ATTEMPT_TS); }

std::string Config::token_refresh_success_ts(const AppConfig* config) { return get_value(config, KEY_TOKEN_REFRESH_SUCCESS_TS); }

std::string Config::parameter_sync_token(const AppConfig* config) { return get_value(config, KEY_PARAMETER_SYNC_TOKEN); }

std::vector<SavedLoginCredential> Config::saved_login_credentials(const AppConfig* config, const std::string& environment)
{
    std::vector<SavedLoginCredential> result;
    const std::string                 env    = credential_environment(config, environment);
    const json                        store  = load_login_credential_store(config);
    const auto                        env_it = store.find(env);
    if (env_it != store.end() && env_it->is_array()) {
        for (const auto& item : *env_it) {
            if (!item.is_object())
                continue;
            const std::string username = trim_copy(item.value("username", std::string()));
            const std::string password = item.value("password", std::string());
            if (username.empty())
                continue;
            if (std::none_of(result.begin(), result.end(),
                             [&username](const SavedLoginCredential& credential) { return credential.username == username; }))
                result.push_back({username, password});
        }
    }

    // One-time compatibility with the original single-account keys. They
    // belonged to whichever environment was active when they were written.
    if (result.empty() && env == credential_environment(config, {})) {
        const std::string username = trim_copy(get_value(config, KEY_LOGIN_USERNAME));
        const std::string password = get_value(config, KEY_LOGIN_PASSWORD);
        if (get_value(config, KEY_LOGIN_REMEMBER, "true") == "true" && !username.empty() && !password.empty())
            result.push_back({username, password});
    }
    return result;
}

void Config::set_remember_login(AppConfig* config, bool remember) { set_value(config, KEY_LOGIN_REMEMBER, remember ? "true" : "false"); }

void Config::set_cached_username(AppConfig* config, const std::string& username) { set_value(config, KEY_LOGIN_USERNAME, username); }

void Config::set_cached_password(AppConfig* config, const std::string& password) { set_value(config, KEY_LOGIN_PASSWORD, password); }

void Config::set_environment(AppConfig* config, const std::string& environment)
{
    if (config == nullptr)
        return;

    // Before changing environments, attach the legacy single-account cache to
    // the environment it originally belonged to.
    const std::string current_env       = credential_environment(config, {});
    json              store             = load_login_credential_store(config);
    const bool        has_current_store = store.contains(current_env) && store[current_env].is_array() && !store[current_env].empty();
    if (!has_current_store && get_value(config, KEY_LOGIN_REMEMBER, "true") == "true") {
        const std::string username = trim_copy(get_value(config, KEY_LOGIN_USERNAME));
        const std::string password = get_value(config, KEY_LOGIN_PASSWORD);
        if (!username.empty() && !password.empty()) {
            store[current_env] = json::array();
            store[current_env].push_back({{"username", username}, {"password", password}});
            set_value(config, KEY_LOGIN_CREDENTIALS, store.dump());
        }
    }

    set_value(config, KEY_ENVIRONMENT, lower_copy(trim_copy(environment)));
}

void Config::set_auth_token(AppConfig* config, const std::string& token) { set_value(config, KEY_AUTH_TOKEN, token); }

void Config::set_auth_mode(AppConfig* config, const std::string& mode) { set_value(config, KEY_AUTH_MODE, mode); }

void Config::set_verify_token(AppConfig* config, const std::string& token) { set_value(config, KEY_VERIFY_TOKEN, token); }

void Config::set_verify_expire_ts(AppConfig* config, const std::string& expire_ts) { set_value(config, KEY_VERIFY_EXPIRE_TS, expire_ts); }

void Config::set_user_email(AppConfig* config, const std::string& email) { set_value(config, KEY_USER_EMAIL, email); }

void Config::set_user_uuid(AppConfig* config, const std::string& uuid) { set_value(config, KEY_USER_UUID, uuid); }

void Config::set_user_phone(AppConfig* config, const std::string& phone) { set_value(config, KEY_USER_PHONE, phone); }

void Config::set_user_nickname(AppConfig* config, const std::string& nickname) { set_value(config, KEY_USER_NICKNAME, nickname); }

void Config::set_user_avatar(AppConfig* config, const std::string& avatar) { set_value(config, KEY_USER_AVATAR, avatar); }

void Config::set_token_refresh_attempt_ts(AppConfig* config, const std::string& refresh_ts)
{
    set_value(config, KEY_TOKEN_REFRESH_ATTEMPT_TS, refresh_ts);
}

void Config::set_token_refresh_success_ts(AppConfig* config, const std::string& refresh_ts)
{
    set_value(config, KEY_TOKEN_REFRESH_SUCCESS_TS, refresh_ts);
}

void Config::set_parameter_sync_token(AppConfig* config, const std::string& token) { set_value(config, KEY_PARAMETER_SYNC_TOKEN, token); }

void Config::save_login_credential(AppConfig*         config,
                                   const std::string& username,
                                   const std::string& password,
                                   const std::string& environment)
{
    if (config == nullptr)
        return;

    const std::string normalized_username = trim_copy(username);
    if (normalized_username.empty())
        return;

    const std::string env         = credential_environment(config, environment);
    auto              credentials = saved_login_credentials(config, env);
    credentials.erase(std::remove_if(credentials.begin(), credentials.end(),
                                     [&normalized_username](const SavedLoginCredential& item) {
                                         return item.username == normalized_username;
                                     }),
                      credentials.end());
    credentials.insert(credentials.begin(), {normalized_username, password});
    if (credentials.size() > 10)
        credentials.resize(10);

    json store = load_login_credential_store(config);
    store[env] = json::array();
    for (const auto& item : credentials)
        store[env].push_back({{"username", item.username}, {"password", item.password}});
    set_value(config, KEY_LOGIN_CREDENTIALS, store.dump());

    if (env == credential_environment(config, {})) {
        set_value(config, KEY_LOGIN_REMEMBER, password.empty() ? "false" : "true");
        set_value(config, KEY_LOGIN_USERNAME, normalized_username);
        set_value(config, KEY_LOGIN_PASSWORD, password);
    }
}

void Config::remove_login_credential(AppConfig* config, const std::string& username, const std::string& environment)
{
    if (config == nullptr)
        return;

    const std::string normalized_username = trim_copy(username);
    const std::string env                 = credential_environment(config, environment);
    auto              credentials         = saved_login_credentials(config, env);
    credentials.erase(std::remove_if(credentials.begin(), credentials.end(),
                                     [&normalized_username](const SavedLoginCredential& item) {
                                         return item.username == normalized_username;
                                     }),
                      credentials.end());

    json store = load_login_credential_store(config);
    store[env] = json::array();
    for (const auto& item : credentials)
        store[env].push_back({{"username", item.username}, {"password", item.password}});
    set_value(config, KEY_LOGIN_CREDENTIALS, store.dump());

    if (env == credential_environment(config, {}) && get_value(config, KEY_LOGIN_USERNAME) == normalized_username) {
        set_value(config, KEY_LOGIN_REMEMBER, "false");
        set_value(config, KEY_LOGIN_USERNAME, "");
        set_value(config, KEY_LOGIN_PASSWORD, "");
    }
}

void Config::clear_verify_cache(AppConfig* config)
{
    set_value(config, KEY_VERIFY_TOKEN, "");
    set_value(config, KEY_VERIFY_EXPIRE_TS, "");
}

void Config::clear_login_identity(AppConfig* config)
{
    set_value(config, KEY_AUTH_TOKEN, "");
    set_value(config, KEY_AUTH_MODE, "");
    set_value(config, KEY_USER_EMAIL, "");
    set_value(config, KEY_USER_UUID, "");
    set_value(config, KEY_USER_PHONE, "");
    set_value(config, KEY_USER_NICKNAME, "");
    set_value(config, KEY_USER_AVATAR, "");
    set_value(config, KEY_TOKEN_REFRESH_ATTEMPT_TS, "");
    set_value(config, KEY_TOKEN_REFRESH_SUCCESS_TS, "");
}

void Config::clear_cached_credentials(AppConfig* config)
{
    set_value(config, KEY_LOGIN_REMEMBER, "false");
    set_value(config, KEY_LOGIN_USERNAME, "");
    set_value(config, KEY_LOGIN_PASSWORD, "");
    set_value(config, KEY_LOGIN_CREDENTIALS, "");
}

}} // namespace Slic3r::GFD
