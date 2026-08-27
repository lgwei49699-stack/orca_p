#include "GFDAuthManager.hpp"

#include "GFDLoginDialog.hpp"
#include "GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/GFDConfig.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <nlohmann/json.hpp>
#include <vector>
#include <wx/weakref.h>

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
    return (lowered.find("token") != std::string::npos &&
           (lowered.find("invalid") != std::string::npos || lowered.find("expired") != std::string::npos ||
             lowered.find("fail") != std::string::npos || lowered.find("error") != std::string::npos)) ||
           lowered.find("unauthorized") != std::string::npos ||
           lowered.find("登录状态无效") != std::string::npos ||
           lowered.find("登录信息失效") != std::string::npos ||
           lowered.find("请重新登录") != std::string::npos ||
           lowered.find("请重新登陆") != std::string::npos ||
           lowered.find("重新登陆") != std::string::npos ||
           lowered.find("token失效") != std::string::npos;
}

GFDLoginDialog::LoginResult try_auto_login_from_cache(wxWindow* parent, std::string& error_message)
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

    return GFDLoginDialog::login_with_credentials(parent, username, password, error_message, true, true);
}

bool has_nonempty_login_identity(const AppConfig* config)
{
    if (config == nullptr)
        return false;

    return !trim_copy(GFD::Config::auth_token(config)).empty() || !trim_copy(GFD::Config::user_uuid(config)).empty();
}

struct PendingLoginRequest
{
    GFDAuthManager::LoginFinishedFn finished;
    bool                            parent_was_set{false};
    wxWeakRef<wxWindow>             parent;
};

bool                             g_async_login_in_progress{false};
std::vector<PendingLoginRequest> g_pending_login_requests;

void finish_async_login_requests(bool success, std::string error_message)
{
    std::vector<PendingLoginRequest> requests = std::move(g_pending_login_requests);
    g_pending_login_requests.clear();
    g_async_login_in_progress = false;
    wxGetApp().CallAfter([requests = std::move(requests), success, error_message = std::move(error_message)]() mutable {
        for (PendingLoginRequest& request : requests) {
            // Callers which provided a window generally capture that window (or its owner) in the
            // completion callback. If it was closed while the coalesced login was in flight, the
            // operation no longer has a recipient and invoking the callback could dereference a
            // destroyed GUI object.
            if (request.parent_was_set && !request.parent)
                continue;
            if (request.finished)
                request.finished(success, error_message);
        }
    });
}

bool resolve_pending_login_parent(wxWindow*& parent)
{
    bool has_parentless_request = false;
    for (const PendingLoginRequest& request : g_pending_login_requests) {
        if (!request.parent_was_set) {
            has_parentless_request = true;
            continue;
        }
        if (request.parent) {
            parent = request.parent.get();
            return true;
        }
    }
    parent = nullptr;
    return has_parentless_request;
}

} // namespace

bool GFDAuthManager::has_valid_session(const AppConfig* config)
{
    if (config == nullptr)
        return false;

    const std::string auth_token       = trim_copy(GFD::Config::auth_token(config));
    const std::string verify_token     = trim_copy(GFD::Config::verify_token(config));
    const std::string verify_expire_ts = GFD::Config::verify_expire_ts(config);

    if (!verify_token.empty() && !verify_expire_ts.empty()) {
        try {
            const bool valid = static_cast<long long>(std::time(nullptr)) <= std::stoll(verify_expire_ts);
            BOOST_LOG_TRIVIAL(info) << "GFD session validity"
                                    << ", env=" << GFD::Config::current_environment_name(config)
                                    << ", token_length=" << verify_token.size()
                                    << ", valid=" << valid
                                    << ", mode=" << (auth_token.empty() ? "verify_cache_only" : "verify_cache");
            return valid;
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "GFD session validity parse failed"
                                       << ", env=" << GFD::Config::current_environment_name(config)
                                       << ", mode=" << (auth_token.empty() ? "verify_cache_only" : "verify_cache");
            return false;
        }
    }

    if (!auth_token.empty()) {
        const bool valid = true;
        BOOST_LOG_TRIVIAL(info) << "GFD session validity"
                                << ", env=" << GFD::Config::current_environment_name(config)
                                << ", token_length=" << auth_token.size()
                                << ", valid=" << valid
                                << ", mode=auth_token_only";
        return valid;
    }

    return false;
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

    GFD::Config::clear_login_identity(config);
    GFD::Config::clear_verify_cache(config);
    config->save();
}

void GFDAuthManager::logout(AppConfig* config, bool forget_credentials)
{
    if (config == nullptr)
        return;

    GFD::Config::clear_login_identity(config);
    GFD::Config::clear_verify_cache(config);
    if (forget_credentials)
        GFD::Config::clear_cached_credentials(config);
    config->save();
}

bool GFDAuthManager::ensure_logged_in(wxWindow* parent, std::string* error_message)
{
    auto* config = wxGetApp().app_config;
    if (has_valid_session(config))
        return true;

    clear_session(config);

    std::string local_error;
    const GFDLoginDialog::LoginResult auto_login_result = try_auto_login_from_cache(parent, local_error);
    if (auto_login_result == GFDLoginDialog::LoginResult::Success)
        return true;
    if (auto_login_result == GFDLoginDialog::LoginResult::Cancelled) {
        if (error_message != nullptr)
            *error_message = local_error.empty() ? "验证码校验已取消或失败" : local_error;
        return false;
    }

    const bool logged_in = wxGetApp().ShowGFDLogin(parent);
    if (!logged_in) {
        if (local_error.empty())
            local_error = "登录状态无效，请重新登录";
        if (error_message != nullptr)
            *error_message = local_error;
        return false;
    }

    return has_valid_session(config) || has_nonempty_login_identity(config);
}

bool GFDAuthManager::ensure_logged_in_async(wxWindow* parent, LoginFinishedFn finished)
{
    if (!finished)
        return false;

    auto* config = wxGetApp().app_config;
    if (has_valid_session(config)) {
        wxGetApp().CallAfter([finished = std::move(finished)]() mutable { finished(true, {}); });
        return true;
    }

    g_pending_login_requests.push_back({std::move(finished), parent != nullptr, wxWeakRef<wxWindow>(parent)});
    if (g_async_login_in_progress)
        return true;
    g_async_login_in_progress = true;

    clear_session(config);
    auto show_manual_login = [](std::string auto_login_error) mutable {
        wxWindow* login_parent = nullptr;
        if (!resolve_pending_login_parent(login_parent)) {
            if (auto_login_error.empty())
                auto_login_error = "登录操作已取消";
            finish_async_login_requests(false, std::move(auto_login_error));
            return;
        }
        const bool logged_in = wxGetApp().ShowGFDLogin(login_parent);
        const bool success   = logged_in && (GFDAuthManager::has_valid_session(wxGetApp().app_config) ||
                                           has_nonempty_login_identity(wxGetApp().app_config));
        if (!success && auto_login_error.empty())
            auto_login_error = "登录状态无效，请重新登录";
        finish_async_login_requests(success, std::move(auto_login_error));
    };

    const bool        remember_login = config != nullptr && GFD::Config::remember_login(config);
    const std::string username       = config != nullptr ? trim_copy(GFD::Config::cached_username(config)) : std::string();
    const std::string password       = config != nullptr ? GFD::Config::cached_password(config) : std::string();
    if (!remember_login || username.empty() || password.empty()) {
        wxGetApp().CallAfter([show_manual_login = std::move(show_manual_login)]() mutable { show_manual_login({}); });
        return true;
    }

    // Keep the hidden automatic-login dialog independent from the requesting window. A short-lived
    // device/config dialog may be closed while login is running; parenting to it would destroy the
    // worker and leave all coalesced login callbacks pending forever.
    const bool started = GFDLoginDialog::login_with_credentials_async(
        username, password,
        [show_manual_login = std::move(show_manual_login)](GFDLoginDialog::LoginResult result, std::string error_message) mutable {
            if (result == GFDLoginDialog::LoginResult::Success) {
                finish_async_login_requests(true, {});
                return;
            }
            if (result == GFDLoginDialog::LoginResult::Cancelled) {
                if (error_message.empty())
                    error_message = "验证码校验已取消或失败";
                finish_async_login_requests(false, std::move(error_message));
                return;
            }
            show_manual_login(std::move(error_message));
        },
        true, true);
    if (!started) {
        g_async_login_in_progress = false;
        finish_async_login_requests(false, "无法启动自动登录任务，请稍后重试");
        // The completion callback has already been scheduled for every coalesced request.
        return true;
    }
    return true;
}

bool GFDAuthManager::perform_authenticated_request(const RequestFn& request, std::string& body, std::string& error_message, wxWindow* parent)
{
    if (!ensure_logged_in(parent, &error_message))
        return false;

    bool missing_token = false;
    const auto run_once = [&]() {
        const std::string token = current_auth_token(wxGetApp().app_config);
        BOOST_LOG_TRIVIAL(info) << "GFD authenticated request"
                                << ", env=" << GFD::Config::current_environment_name(wxGetApp().app_config)
                                << ", token_length=" << token.size();
        missing_token = token.empty();
        if (missing_token) {
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

    if (!missing_token && !is_retryable_auth_failure_response(result.status, result.body, result.error_message))
        return result.ok;

    BOOST_LOG_TRIVIAL(warning) << "GFD authenticated request requires re-login"
                               << ", env=" << GFD::Config::current_environment_name(wxGetApp().app_config)
                               << ", http_status=" << result.status << ", error=" << result.error_message;
    clear_session(wxGetApp().app_config);
    if (!ensure_logged_in(parent, &error_message))
        return false;

    result        = run_once();
    body          = result.body;
    error_message = result.error_message;
    if (missing_token || is_retryable_auth_failure_response(result.status, result.body, result.error_message)) {
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

    const std::string trimmed_body = trim_copy(body);
    bool              body_is_json = false;
    try {
        const auto response = nlohmann::json::parse(trimmed_body);
        body_is_json        = true;
        if (response.is_object()) {
            const auto string_field = [&response](const char* key) {
                const auto field = response.find(key);
                return field != response.end() && field->is_string() ? field->get<std::string>() : std::string();
            };
            const std::string msg     = string_field("msg");
            const std::string message = string_field("message");
            const std::string error   = string_field("error");
            if (contains_auth_failure_text(msg) || contains_auth_failure_text(message) || contains_auth_failure_text(error))
                return true;

            if (response.contains("code")) {
                const auto& code = response["code"];
                if ((code.is_number_integer() && (code == 401 || code == 403)) ||
                    (code.is_string() && (code.get<std::string>() == "401" || code.get<std::string>() == "403")))
                    return true;
            }
            // Do not scan the complete JSON document: successful payload fields may legitimately contain
            // words such as "token" or "forbidden".
        } else if (response.is_string()) {
            const std::string message = response.get<std::string>();
            if (message.size() <= 512 && contains_auth_failure_text(message))
                return true;
        }
    } catch (...) {}

    // Some gateways return a short plain-text authentication error instead of JSON.
    if (!body_is_json && !trimmed_body.empty() && trimmed_body.size() <= 512 && contains_auth_failure_text(trimmed_body))
        return true;
    const std::string trimmed_error = trim_copy(error_message);
    if (body_is_json && trimmed_error == trimmed_body)
        return false;
    return !trimmed_error.empty() && trimmed_error.size() <= 512 && contains_auth_failure_text(trimmed_error);
}

bool GFDAuthManager::is_retryable_auth_failure_response(unsigned status, const std::string& body, const std::string& error_message)
{
    // A missing response or a transient/server failure has an unknown outcome and must never be replayed automatically.
    if (status == 0 || status == 408 || status == 429 || status >= 500)
        return false;
    return is_auth_failure_response(status, body, error_message);
}

}} // namespace Slic3r::GUI
