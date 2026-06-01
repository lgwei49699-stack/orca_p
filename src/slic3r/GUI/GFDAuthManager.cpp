#include "GFDAuthManager.hpp"

#include "GFDLoginDialog.hpp"
#include "GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/GFDConfig.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

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

bool contains_auth_failure_text(const std::string& value)
{
    const std::string lowered = lower_copy(value);
    return lowered.find("token") != std::string::npos &&
               (lowered.find("invalid") != std::string::npos || lowered.find("expired") != std::string::npos ||
                lowered.find("fail") != std::string::npos || lowered.find("error") != std::string::npos) ||
           lowered.find("unauthorized") != std::string::npos ||
           lowered.find("forbidden") != std::string::npos ||
           lowered.find("登录状态无效") != std::string::npos ||
           lowered.find("请重新登录") != std::string::npos ||
           lowered.find("token失效") != std::string::npos;
}

GFDLoginDialog::LoginResult try_auto_login_from_cache(std::string& error_message)
{
    auto* config = wxGetApp().app_config;
    if (config == nullptr)
        return GFDLoginDialog::LoginResult::Failed;

    if (!GFD::Config::remember_login(config))
        return GFDLoginDialog::LoginResult::Failed;

    const std::string username = trim_copy(GFD::Config::cached_username(config));
    const std::string password = GFD::Config::cached_password(config);
    if (username.empty() || password.empty())
        return GFDLoginDialog::LoginResult::Failed;

    return GFDLoginDialog::login_with_credentials(username, password, error_message, true, true);
}

} // namespace

bool GFDAuthManager::has_valid_session(const AppConfig* config)
{
    if (config == nullptr)
        return false;

    std::string token = current_auth_token(config);
    if (token.empty())
        return false;

    const std::string expire_ts = GFD::Config::verify_expire_ts(config);
    if (expire_ts.empty())
        return false;

    try {
        return static_cast<long long>(std::time(nullptr)) <= std::stoll(expire_ts);
    } catch (...) {
        return false;
    }
}

std::string GFDAuthManager::current_auth_token(const AppConfig* config)
{
    if (config == nullptr)
        return {};

    std::string token = GFD::Config::auth_token(config);
    if (token.empty())
        token = GFD::Config::verify_token(config);
    return token;
}

void GFDAuthManager::clear_session(AppConfig* config)
{
    if (config == nullptr)
        return;

    GFD::Config::set_auth_token(config, "");
    GFD::Config::clear_verify_cache(config);
    config->save();
}

bool GFDAuthManager::ensure_logged_in(wxWindow*, std::string* error_message)
{
    auto* config = wxGetApp().app_config;
    if (has_valid_session(config))
        return true;

    clear_session(config);

    std::string local_error;
    const GFDLoginDialog::LoginResult auto_login_result = try_auto_login_from_cache(local_error);
    if (auto_login_result == GFDLoginDialog::LoginResult::Success)
        return true;
    if (auto_login_result == GFDLoginDialog::LoginResult::Cancelled) {
        if (error_message != nullptr)
            *error_message = local_error.empty() ? "验证码校验已取消或失败" : local_error;
        return false;
    }

    const bool logged_in = wxGetApp().ShowUserLogin();
    if (!logged_in) {
        if (local_error.empty())
            local_error = "登录状态无效，请重新登录";
        if (error_message != nullptr)
            *error_message = local_error;
        return false;
    }

    return has_valid_session(config) || !current_auth_token(config).empty();
}

bool GFDAuthManager::perform_authenticated_request(const RequestFn& request,
                                                   std::string&     body,
                                                   std::string&     error_message,
                                                   wxWindow*        parent)
{
    if (!ensure_logged_in(parent, &error_message))
        return false;

    const auto run_once = [&]() {
        const std::string token = current_auth_token(wxGetApp().app_config);
        if (token.empty()) {
            GFDHttpResult result;
            result.ok            = false;
            result.error_message = "登录状态无效，请重新登录";
            return result;
        }
        return request(token);
    };

    GFDHttpResult result = run_once();
    body                 = result.body;
    error_message        = result.error_message;

    if (!is_auth_failure_response(result.status, result.body, result.error_message))
        return result.ok;

    clear_session(wxGetApp().app_config);
    if (!ensure_logged_in(parent, &error_message))
        return false;

    result        = run_once();
    body          = result.body;
    error_message = result.error_message;
    if (is_auth_failure_response(result.status, result.body, result.error_message)) {
        if (error_message.empty())
            error_message = "登录状态无效，请重新登录";
        return false;
    }

    return result.ok;
}

bool GFDAuthManager::is_auth_failure_response(unsigned status, const std::string& body, const std::string& error_message)
{
    if (status == 401 || status == 403)
        return true;
    if (contains_auth_failure_text(error_message))
        return true;
    if (contains_auth_failure_text(body))
        return true;

    try {
        const auto response = nlohmann::json::parse(body);
        const std::string msg = response.value("msg", std::string());
        const std::string message = response.value("message", std::string());
        if (contains_auth_failure_text(msg) || contains_auth_failure_text(message))
            return true;

        const int code = response.value("code", 0);
        if ((code == 401 || code == 403) && (contains_auth_failure_text(msg) || contains_auth_failure_text(message) || msg.empty()))
            return true;
    } catch (...) {
    }

    return false;
}

}} // namespace Slic3r::GUI
