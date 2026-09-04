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

struct SavedLoginCredential
{
    std::string username;
    std::string password;
};

// GFD: per-device button visibility, loaded from resources/gfd_button_config.json.
struct ButtonVisibility
{
    bool cloud_import   = true;
    bool upload_config  = true;
    bool save_config    = true;
    bool print          = true;
    // Enables the formal 3MF print action for this device type.
    bool print_3mf = false;
    // Device types shown in the "下发打印" dialog dropdown; empty = fall back to cloud-returned types.
    std::vector<std::string> print_device_types;
};

class Config
{
public:
    // Consumer builds may read cloud presets and cache them locally, but must
    // never write filament or slicing parameters back to the service.
    static constexpr bool        PARAMETER_SYNC_READ_ONLY = true;
    static constexpr const char* ENV_PRODUCTION           = "production";
    static constexpr const char* ENV_QA                   = "qa";
    static constexpr const char* PRODUCTION_AUTH_BASE_URL = "https://dcenter.kfb-1.com";
    static constexpr const char* PRODUCTION_API_BASE_URL  = "https://print.wisebeginner3d.com";
    static constexpr const char* QA_AUTH_BASE_URL         = "https://qa-datacenter.gongfudou.com";
    static constexpr const char* QA_API_BASE_URL          = "https://qa-appgw-hwsh.gongfudou.com";
    static constexpr const char* QA_USER_API_BASE_URL     = "https://qa-print.wisebeginner3d.com";

    static constexpr const char* SECTION                      = "gfd";
    static constexpr const char* KEY_ENVIRONMENT              = "environment";
    static constexpr const char* KEY_AUTH_TOKEN               = "auth_token";
    static constexpr const char* KEY_AUTH_MODE                = "auth_mode";
    static constexpr const char* KEY_VERIFY_TOKEN             = "verify_token";
    static constexpr const char* KEY_VERIFY_EXPIRE_TS         = "verify_expire_ts";
    static constexpr const char* KEY_LOGIN_REMEMBER           = "login_remember";
    static constexpr const char* KEY_LOGIN_USERNAME           = "login_username";
    static constexpr const char* KEY_LOGIN_PASSWORD           = "login_password";
    static constexpr const char* KEY_LOGIN_CREDENTIALS        = "login_credentials_v2";
    static constexpr const char* KEY_USER_EMAIL               = "user_email";
    static constexpr const char* KEY_USER_UUID                = "user_uuid";
    static constexpr const char* KEY_USER_PHONE               = "user_phone";
    static constexpr const char* KEY_USER_NICKNAME            = "user_nickname";
    static constexpr const char* KEY_USER_AVATAR              = "user_avatar";
    static constexpr const char* KEY_TOKEN_REFRESH_ATTEMPT_TS = "token_refresh_attempt_ts";
    static constexpr const char* KEY_TOKEN_REFRESH_SUCCESS_TS = "token_refresh_success_ts";
    static constexpr const char* KEY_USER_API_BASE_URL        = "user_api_base_url";
    static constexpr const char* KEY_USER_INFO_URL            = "user_info_url";
    static constexpr const char* KEY_TOKEN_REFRESH_URL        = "token_refresh_url";
    static constexpr const char* KEY_SMS_SEND_URL             = "sms_send_url";
    static constexpr const char* KEY_SMS_LOGIN_URL            = "sms_login_url";
    // Read-only parameter synchronization is a separate trust domain from the
    // consumer account API. Deployments may provide a dedicated token/gateway
    // without exposing any write action in the desktop UI.
    static constexpr const char* KEY_PARAMETER_SYNC_TOKEN        = "parameter_sync_token";
    static constexpr const char* KEY_PARAMETER_SYNC_API_BASE_URL = "parameter_sync_api_base_url";
    static constexpr const char* KEY_PARAMETER_SYNC_BIZ          = "parameter_sync_biz";
    static constexpr const char* AUTH_MODE_USER_SMS              = "user_sms";

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
    static constexpr const char* PATH_USER_DEVICE_QUERY = "/app/print3d/api/v1/devices/list";
    // These consumer routes may be overridden with full URLs in the [gfd]
    // application configuration without rebuilding the desktop client.
    static constexpr const char* PATH_CAPTCHA_GENERATE            = "/app/print3d/api/v1/auth/generateCaptcha";
    static constexpr const char* PATH_SMS_SEND                    = "/app/print3d/api/v1/auth/verifyCaptchaAndSendSms";
    static constexpr const char* PATH_SMS_LOGIN                   = "/app/print3d/api/v1/auth/phoneLogin";
    static constexpr const char* PATH_USER_INFO                   = "/app/print3d/api/v1/users/info";
    static constexpr const char* PATH_TOKEN_REFRESH               = "/app/print3d/api/v1/users/getNewToken";
    static constexpr const char* PATH_DEVICE_FILAMENT_INFO        = "/app/print3d/api/v1/devices/filamentInfo";
    static constexpr const char* PATH_USER_FILAMENT_LIST          = "/app/print3d/api/v1/devices/ftList";
    static constexpr const char* PATH_DEVICE_SLICE_TYPE           = "/app/pds/api/v1/slice-param-config/device-and-slice-type";
    static constexpr const char* PATH_FILAMENT_TEMPERATURE_DETAIL = "/app/print3d/manage/v1/filamentTemperature/detail";
    static constexpr const char* PATH_FILAMENT_TEMPERATURE_UPDATE_SLICE_PARAM =
        "/app/print3d/manage/v1/filamentTemperature/updateSliceParam";
    static constexpr const char* PATH_DEVICE_PRINT_CMD = "/app/pmc/api/v1/deviceCmd/print";

    static EnvironmentConfig current_environment(const AppConfig* config);
    static std::string       current_environment_name(const AppConfig* config);

    static std::string public_key_url(const AppConfig* config);
    static std::string login_url(const AppConfig* config);
    static std::string verify_url(const AppConfig* config);

    static std::string              obs_token_url(const AppConfig* config);
    static std::string              config_add_url(const AppConfig* config);
    static std::string              config_update_url(const AppConfig* config);
    static std::string              device_query_url(const AppConfig* config);
    static std::string              user_api_base_url(const AppConfig* config);
    static std::string              captcha_generate_url(const AppConfig* config);
    static std::string              sms_send_url(const AppConfig* config);
    static std::string              sms_login_url(const AppConfig* config);
    static std::string              user_info_url(const AppConfig* config);
    static std::string              token_refresh_url(const AppConfig* config);
    static std::string              parameter_sync_api_base_url(const AppConfig* config);
    static std::string              parameter_sync_biz(const AppConfig* config);
    static std::string              device_filament_info_url(const AppConfig* config);
    static std::string              device_slice_type_url(const AppConfig* config);
    static std::string              user_filament_list_url(const AppConfig* config);
    static std::string              filament_temperature_detail_url(const AppConfig* config);
    static std::string              filament_temperature_update_slice_param_url(const AppConfig* config);
    static std::string              device_print_cmd_url(const AppConfig* config);
    static std::string              explicit_device_type(const DynamicPrintConfig& printer_config);
    static std::string              current_device_type(const DynamicPrintConfig& printer_config);
    static ButtonVisibility         button_visibility(const std::string& device_type);
    static std::vector<std::string> print_device_types(const std::string& device_type);
    static std::vector<std::string> local_gfd_device_types();
    static bool                     is_gfd_printer(const DynamicPrintConfig& printer_config);
    static bool                     should_show_print_button(const DynamicPrintConfig& printer_config);

    static bool                              remember_login(const AppConfig* config);
    static std::string                       cached_username(const AppConfig* config);
    static std::string                       cached_password(const AppConfig* config);
    static std::string                       auth_token(const AppConfig* config);
    static std::string                       auth_mode(const AppConfig* config);
    static std::string                       verify_token(const AppConfig* config);
    static std::string                       verify_expire_ts(const AppConfig* config);
    static std::string                       user_email(const AppConfig* config);
    static std::string                       user_uuid(const AppConfig* config);
    static std::string                       user_phone(const AppConfig* config);
    static std::string                       user_nickname(const AppConfig* config);
    static std::string                       user_avatar(const AppConfig* config);
    static std::string                       token_refresh_attempt_ts(const AppConfig* config);
    static std::string                       token_refresh_success_ts(const AppConfig* config);
    static std::string                       parameter_sync_token(const AppConfig* config);
    static std::vector<SavedLoginCredential> saved_login_credentials(const AppConfig* config, const std::string& environment = {});

    static void set_remember_login(AppConfig* config, bool remember);
    static void set_cached_username(AppConfig* config, const std::string& username);
    static void set_cached_password(AppConfig* config, const std::string& password);
    static void set_environment(AppConfig* config, const std::string& environment);
    static void set_auth_token(AppConfig* config, const std::string& token);
    static void set_auth_mode(AppConfig* config, const std::string& mode);
    static void set_verify_token(AppConfig* config, const std::string& token);
    static void set_verify_expire_ts(AppConfig* config, const std::string& expire_ts);
    static void set_user_email(AppConfig* config, const std::string& email);
    static void set_user_uuid(AppConfig* config, const std::string& uuid);
    static void set_user_phone(AppConfig* config, const std::string& phone);
    static void set_user_nickname(AppConfig* config, const std::string& nickname);
    static void set_user_avatar(AppConfig* config, const std::string& avatar);
    static void set_token_refresh_attempt_ts(AppConfig* config, const std::string& refresh_ts);
    static void set_token_refresh_success_ts(AppConfig* config, const std::string& refresh_ts);
    static void set_parameter_sync_token(AppConfig* config, const std::string& token);
    static void save_login_credential(AppConfig*         config,
                                      const std::string& username,
                                      const std::string& password,
                                      const std::string& environment = {});
    static void remove_login_credential(AppConfig* config, const std::string& username, const std::string& environment = {});

    static void clear_verify_cache(AppConfig* config);
    static void clear_login_identity(AppConfig* config);
    static void clear_cached_credentials(AppConfig* config);
};

} // namespace GFD
} // namespace Slic3r

#endif
