#pragma once

#ifndef slic3r_GFDConfig_hpp_
#define slic3r_GFDConfig_hpp_

#include <string>
#include <vector>

namespace Slic3r {

class AppConfig;
class DynamicPrintConfig;

namespace GFD {

struct EnvironmentConfig
{
    std::string name;
    std::string auth_base_url;
    std::string api_base_url;
};

// GFD: per-device button visibility, loaded from resources/gfd_button_config.json.
struct ButtonVisibility
{
    bool cloud_import   = true;
    bool dynamic_params = true;
    bool upload_config  = true;
    bool save_config    = true;
    bool print          = true;
    // Device types shown in the "下发打印" dialog dropdown; empty = fall back to cloud-returned types.
    std::vector<std::string> print_device_types;
};

class Config
{
public:
    static constexpr const char* ENV_PRODUCTION           = "production";
    static constexpr const char* ENV_QA                   = "qa";
    static constexpr const char* PRODUCTION_AUTH_BASE_URL = "https://dcenter.kfb-1.com";
    static constexpr const char* PRODUCTION_API_BASE_URL  = "https://print.wisebeginner3d.com";
    static constexpr const char* QA_AUTH_BASE_URL         = "https://qa-datacenter.gongfudou.com";
    static constexpr const char* QA_API_BASE_URL          = "https://qa-appgw-hwsh.gongfudou.com";

    static constexpr const char* SECTION              = "gfd";
    static constexpr const char* KEY_ENVIRONMENT      = "environment";
    static constexpr const char* KEY_AUTH_TOKEN       = "auth_token";
    static constexpr const char* KEY_VERIFY_TOKEN     = "verify_token";
    static constexpr const char* KEY_VERIFY_EXPIRE_TS = "verify_expire_ts";
    static constexpr const char* KEY_LOGIN_REMEMBER   = "login_remember";
    static constexpr const char* KEY_LOGIN_USERNAME   = "login_username";
    static constexpr const char* KEY_LOGIN_PASSWORD   = "login_password";
    static constexpr const char* KEY_USER_EMAIL       = "user_email";
    static constexpr const char* KEY_USER_UUID        = "user_uuid";

    static constexpr const char* OBS_SERVER     = "obs.cn-east-2.myhuaweicloud.com";
    static constexpr const char* OBS_BUCKET     = "micro";
    static constexpr const char* OBS_ACCESS_KEY = "RBH44LBCU4Z4BBGJB4DK";
    static constexpr const char* OBS_SECRET_KEY = "iq2A0AAeYyHk1cxoCfGi9KlxVtWzb6HrUG9LZ5hH";

    static constexpr const char* PATH_PUBLIC_KEY        = "/manager/auth/login/publicKey";
    static constexpr const char* PATH_LOGIN             = "/manager/auth/login/emailPassword";
    static constexpr const char* PATH_VERIFY            = "/manager/auth/login/tfaCode";
    static constexpr const char* PATH_OBS_TOKEN         = "/app/print3d/api/v1/obs/token";
    static constexpr const char* PATH_CONFIG_ADD        = "/app/print3d/manage/v1/slice-param-config/add";
    static constexpr const char* PATH_CONFIG_UPDATE     = "/app/print3d/manage/v1/slice-param-config/edit";
    static constexpr const char* PATH_DEVICE_QUERY      = "/app/print3d/manage/v1/md/query";
    static constexpr const char* PATH_DEVICE_SLICE_TYPE = "/app/print3d/manage/v1/slice-param-config/device-and-slice-type";
    static constexpr const char* PATH_FILAMENT_TEMPERATURE_LIST = "/app/print3d/manage/v1/filamentTemperature/list";
    static constexpr const char* PATH_FILAMENT_TEMPERATURE_DETAIL = "/app/print3d/manage/v1/filamentTemperature/detail";
    static constexpr const char* PATH_FILAMENT_TEMPERATURE_UPDATE_SLICE_PARAM =
        "/app/print3d/manage/v1/filamentTemperature/updateSliceParam";
    static constexpr const char* PATH_DEVICE_PRINT_CMD  = "/app/pmc/api/v1/deviceCmd/print";

    static EnvironmentConfig current_environment(const AppConfig* config);
    static std::string       current_environment_name(const AppConfig* config);

    static std::string public_key_url(const AppConfig* config);
    static std::string login_url(const AppConfig* config);
    static std::string verify_url(const AppConfig* config);

    static std::string obs_token_url(const AppConfig* config);
    static std::string config_add_url(const AppConfig* config);
    static std::string config_update_url(const AppConfig* config);
    static std::string device_query_url(const AppConfig* config);
    static std::string device_slice_type_url(const AppConfig* config);
    static std::string filament_temperature_list_url(const AppConfig* config);
    static std::string filament_temperature_detail_url(const AppConfig* config);
    static std::string filament_temperature_update_slice_param_url(const AppConfig* config);
    static std::string device_print_cmd_url(const AppConfig* config);
    static std::string explicit_device_type(const DynamicPrintConfig& printer_config);
    static std::string current_device_type(const DynamicPrintConfig& printer_config);
    static ButtonVisibility button_visibility(const std::string& device_type);
    static std::vector<std::string> print_device_types(const std::string& device_type);
    // All device types declared across the central config (every "devices" entry's print_device_types, deduped).
    static std::vector<std::string> all_print_device_types();
    static std::vector<std::string> local_gfd_device_types();
    static bool        is_gfd_printer(const DynamicPrintConfig& printer_config);
    static bool        should_show_print_button(const DynamicPrintConfig& printer_config);

    static bool        remember_login(const AppConfig* config);
    static std::string cached_username(const AppConfig* config);
    static std::string cached_password(const AppConfig* config);
    static std::string auth_token(const AppConfig* config);
    static std::string verify_token(const AppConfig* config);
    static std::string verify_expire_ts(const AppConfig* config);
    static std::string user_email(const AppConfig* config);
    static std::string user_uuid(const AppConfig* config);

    static void set_remember_login(AppConfig* config, bool remember);
    static void set_cached_username(AppConfig* config, const std::string& username);
    static void set_cached_password(AppConfig* config, const std::string& password);
    static void set_environment(AppConfig* config, const std::string& environment);
    static void set_auth_token(AppConfig* config, const std::string& token);
    static void set_verify_token(AppConfig* config, const std::string& token);
    static void set_verify_expire_ts(AppConfig* config, const std::string& expire_ts);
    static void set_user_email(AppConfig* config, const std::string& email);
    static void set_user_uuid(AppConfig* config, const std::string& uuid);

    static void clear_verify_cache(AppConfig* config);
    static void clear_login_identity(AppConfig* config);
};

} // namespace GFD
} // namespace Slic3r

#endif
