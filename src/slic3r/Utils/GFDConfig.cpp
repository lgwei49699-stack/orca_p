#include "GFDConfig.hpp"

#include "libslic3r/AppConfig.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r { namespace GFD {

namespace {

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
