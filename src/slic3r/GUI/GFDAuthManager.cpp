#include "GFDAuthManager.hpp"

#include "GFDLoginDialog.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "I18N.hpp"
#include "Jobs/BoostThreadWorker.hpp"
#include "Jobs/PlaterWorker.hpp"
#include "Jobs/Worker.hpp"
#include "MainFrame.hpp"
#include "Widgets/Label.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/GFDConfig.hpp"
#include "slic3r/Utils/Http.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <exception>
#include <initializer_list>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <utility>
#include <vector>
#include <wx/dialog.h>
#include <wx/gauge.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/weakref.h>

namespace Slic3r { namespace GUI {

namespace {

using json = nlohmann::json;

constexpr long      SESSION_CONNECT_TIMEOUT_SECONDS = 10;
constexpr long      SESSION_REQUEST_TIMEOUT_SECONDS = 20;
constexpr long long TOKEN_REFRESH_INTERVAL_SECONDS  = 5LL * 24LL * 60LL * 60LL;

void notify_gfd_account_state_changed();

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
           lowered.find("unauthorized") != std::string::npos || lowered.find("登录状态无效") != std::string::npos ||
           lowered.find("登录信息失效") != std::string::npos || lowered.find("请重新登录") != std::string::npos ||
           lowered.find("请重新登陆") != std::string::npos || lowered.find("重新登陆") != std::string::npos ||
           lowered.find("token失效") != std::string::npos;
}

std::optional<long long> json_integer(const json& object, const char* key)
{
    if (!object.is_object() || key == nullptr)
        return std::nullopt;

    const auto it = object.find(key);
    if (it == object.end() || it->is_null())
        return std::nullopt;
    if (it->is_number_integer())
        return it->get<long long>();
    if (it->is_number_unsigned()) {
        const auto value = it->get<unsigned long long>();
        return value <= static_cast<unsigned long long>(std::numeric_limits<long long>::max()) ?
                   std::optional<long long>(static_cast<long long>(value)) :
                   std::nullopt;
    }
    if (!it->is_string())
        return std::nullopt;

    const std::string value = trim_copy(it->get<std::string>());
    if (value.empty())
        return std::nullopt;
    try {
        size_t          parsed = 0;
        const long long result = std::stoll(value, &parsed);
        return parsed == value.size() ? std::optional<long long>(result) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::string json_string(const json& object, const char* key)
{
    if (!object.is_object() || key == nullptr)
        return {};
    const auto it = object.find(key);
    if (it == object.end() || it->is_null())
        return {};
    if (it->is_string())
        return trim_copy(it->get<std::string>());
    if (it->is_number_integer())
        return std::to_string(it->get<long long>());
    if (it->is_number_unsigned())
        return std::to_string(it->get<unsigned long long>());
    return {};
}

const json* child_object(const json& object, std::initializer_list<const char*> keys)
{
    if (!object.is_object())
        return nullptr;
    for (const char* key : keys) {
        const auto it = object.find(key);
        if (it != object.end() && it->is_object())
            return &*it;
    }
    return nullptr;
}

std::string nested_string(const json& response, std::initializer_list<const char*> keys)
{
    const json* current = &response;
    for (int depth = 0; current != nullptr && depth < 5; ++depth) {
        for (const char* key : keys) {
            const std::string value = json_string(*current, key);
            if (!value.empty())
                return value;
        }
        current = child_object(*current, {"data", "res", "result", "user", "member", "profile"});
    }
    return {};
}

std::string response_message(const json& response, const std::string& fallback)
{
    const std::string message = nested_string(response, {"msg", "message", "error", "errorMessage"});
    return message.empty() ? fallback : message;
}

bool response_succeeded(const json& response)
{
    if (!response.is_object())
        return false;

    bool has_status_code = false;
    for (const char* key : {"code", "ret"}) {
        const auto it = response.find(key);
        if (it == response.end() || it->is_null())
            continue;
        has_status_code  = true;
        const auto value = json_integer(response, key);
        if (!value.has_value() || *value != 0)
            return false;
    }
    return has_status_code;
}

bool token_refresh_due(const std::string& last_success_ts, long long now)
{
    if (last_success_ts.empty())
        return true;
    try {
        size_t          parsed       = 0;
        const long long last_success = std::stoll(last_success_ts, &parsed);
        if (parsed != last_success_ts.size() || last_success <= 0 || last_success > now)
            return true;
        return now - last_success >= TOKEN_REFRESH_INTERVAL_SECONDS;
    } catch (...) {
        return true;
    }
}

GFDHttpResult get_authenticated_json(const std::string& url, const std::string& token, Job::Ctl& ctl)
{
    GFDHttpResult result;
    bool          cancellation_requested = false;
    Http::get(url)
        .header("Accept", "application/json")
        .header("Authorization", token)
        .header("Biz", "ZXB")
        .timeout_connect(SESSION_CONNECT_TIMEOUT_SECONDS)
        .timeout_max(SESSION_REQUEST_TIMEOUT_SECONDS)
        .on_progress([&](Http::Progress, bool& cancel) {
            cancellation_requested = ctl.was_canceled();
            cancel                 = cancellation_requested;
        })
        .on_complete([&](std::string body, unsigned status) {
            result.ok     = true;
            result.status = status;
            result.body   = std::move(body);
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            result.ok            = false;
            result.status        = status;
            result.body          = std::move(body);
            result.error_message = std::move(error);
        })
        .perform_sync();

    if (cancellation_requested && !result.ok)
        result.error_message = L("The operation was canceled.");
    return result;
}

enum class StartupSessionState { Valid, Invalid, Offline, Failed, Aborted };

bool is_transient_failure(unsigned status) { return status == 0 || status == 408 || status == 429 || status >= 500; }

bool is_transient_business_failure(const json& response)
{
    for (const char* key : {"code", "ret"}) {
        const auto code = json_integer(response, key);
        if (code.has_value() && (*code == 408 || *code == 429 || (*code >= 500 && *code <= 599)))
            return true;
    }
    return false;
}

struct StartupSessionSnapshot
{
    std::string environment;
    std::string token;
    std::string user_info_url;
    std::string token_refresh_url;
    bool        refresh_due{false};
};

struct StartupSessionCheckResult
{
    StartupSessionState state{StartupSessionState::Failed};
    std::string         error_message;
    std::string         user_uuid;
    std::string         phone;
    std::string         nickname;
    std::string         avatar;
    std::string         refreshed_token;
    std::string         refresh_attempt_ts;
    std::string         refresh_success_ts;
};

void check_startup_session(const StartupSessionSnapshot& snapshot, StartupSessionCheckResult& result, Job::Ctl& ctl)
{
    const auto classify_failure = [&result](const GFDHttpResult& response, const std::string& fallback) {
        if (is_transient_failure(response.status)) {
            result.state         = StartupSessionState::Offline;
            result.error_message = response.error_message.empty() ? fallback : response.error_message;
        } else if (GFDAuthManager::is_auth_failure_response(response.status, response.body, response.error_message)) {
            result.state         = StartupSessionState::Invalid;
            result.error_message = "登录状态已失效，请重新登录";
        } else {
            result.state         = StartupSessionState::Failed;
            result.error_message = response.error_message.empty() ? fallback : response.error_message;
        }
    };

    GFDHttpResult user_info = get_authenticated_json(snapshot.user_info_url, snapshot.token, ctl);
    if (ctl.was_canceled())
        return;
    if (!user_info.ok) {
        classify_failure(user_info, "暂时无法验证登录状态");
        return;
    }
    if (GFDAuthManager::is_auth_failure_response(user_info.status, user_info.body, user_info.error_message)) {
        result.state         = StartupSessionState::Invalid;
        result.error_message = "登录状态已失效，请重新登录";
        return;
    }

    json user_response;
    try {
        user_response = json::parse(user_info.body);
    } catch (...) {
        result.state         = StartupSessionState::Failed;
        result.error_message = "用户信息接口返回了无效数据";
        return;
    }
    if (!response_succeeded(user_response)) {
        if (GFDAuthManager::is_auth_failure_response(user_info.status, user_info.body, {})) {
            result.state         = StartupSessionState::Invalid;
            result.error_message = "登录状态已失效，请重新登录";
        } else if (is_transient_business_failure(user_response)) {
            result.state         = StartupSessionState::Offline;
            result.error_message = response_message(user_response, "暂时无法验证登录状态");
        } else {
            result.state         = StartupSessionState::Failed;
            result.error_message = response_message(user_response, "暂时无法验证登录状态");
        }
        return;
    }

    result.state     = StartupSessionState::Valid;
    result.user_uuid = nested_string(user_response, {"userSn", "userUuid", "uuid", "userId", "uid", "id"});
    result.phone     = nested_string(user_response, {"phone", "mobile"});
    result.nickname  = nested_string(user_response, {"nickname", "nickName", "wxNickName", "userName", "name"});
    result.avatar    = nested_string(user_response, {"avatarUrl", "avatar", "headImg", "headImage"});

    if (!snapshot.refresh_due)
        return;

    result.refresh_attempt_ts = std::to_string(static_cast<long long>(std::time(nullptr)));
    GFDHttpResult refresh     = get_authenticated_json(snapshot.token_refresh_url, snapshot.token, ctl);
    if (ctl.was_canceled())
        return;
    if (!refresh.ok) {
        if (!is_transient_failure(refresh.status) &&
            GFDAuthManager::is_auth_failure_response(refresh.status, refresh.body, refresh.error_message)) {
            result.state         = StartupSessionState::Invalid;
            result.error_message = "登录状态已失效，请重新登录";
            return;
        }
        // The user-info request already proved that the old token is valid.
        // A transient refresh failure must not log the user out.
        result.error_message = refresh.error_message.empty() ? "暂时无法刷新登录状态" : refresh.error_message;
        return;
    }
    if (GFDAuthManager::is_auth_failure_response(refresh.status, refresh.body, refresh.error_message)) {
        result.state         = StartupSessionState::Invalid;
        result.error_message = "登录状态已失效，请重新登录";
        return;
    }

    json refresh_response;
    try {
        refresh_response = json::parse(refresh.body);
    } catch (...) {
        result.error_message = "刷新登录状态接口返回了无效数据";
        return;
    }
    if (!response_succeeded(refresh_response)) {
        result.error_message = response_message(refresh_response, "暂时无法刷新登录状态");
        return;
    }

    const std::string refreshed_token = nested_string(refresh_response, {"token", "accessToken", "access_token"});
    if (refreshed_token.empty()) {
        result.error_message = "刷新登录状态接口未返回有效凭据";
        return;
    }

    result.refreshed_token    = refreshed_token;
    result.refresh_success_ts = std::to_string(static_cast<long long>(std::time(nullptr)));
}

class GFDSessionCheckDialog final : public wxDialog
{
public:
    GFDSessionCheckDialog(wxWindow* parent, StartupSessionSnapshot snapshot)
        : wxDialog(parent, wxID_ANY, _L("WiseBeginner Slicer"), wxDefaultPosition, wxDefaultSize, wxCAPTION)
        , m_snapshot(std::move(snapshot))
        , m_timer(this)
    {
        SetBackgroundColour(*wxWHITE);
        SetMinSize(FromDIP(wxSize(420, 180)));

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        auto* title = new wxStaticText(this, wxID_ANY, _L("Checking login status"));
        title->SetFont(Label::Head_16);
        sizer->Add(title, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(28));

        auto* message = new wxStaticText(this, wxID_ANY, _L("Verifying your WiseBeginner account. Please wait..."));
        message->SetFont(Label::Body_14);
        message->SetForegroundColour(wxColour(107, 114, 128));
        sizer->Add(message, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(14));

        m_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, FromDIP(wxSize(320, 6)), wxGA_HORIZONTAL);
        sizer->Add(m_gauge, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, FromDIP(24));
        SetSizerAndFit(sizer);
        CentreOnScreen();

        auto worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(this, std::shared_ptr<ProgressIndicator>{},
                                                                        "gfd_startup_session_worker");
        worker->set_busy_cursor_enabled(false);
        m_worker = std::move(worker);

        Bind(
            wxEVT_TIMER,
            [this](wxTimerEvent&) {
                if (m_gauge != nullptr)
                    m_gauge->Pulse();
            },
            m_timer.GetId());
    }

    ~GFDSessionCheckDialog() override
    {
        m_timer.Stop();
        if (m_worker != nullptr) {
            m_worker->cancel_all();
            m_worker->wait_for_idle();
        }
    }

    StartupSessionCheckResult run()
    {
        auto       result = std::make_shared<StartupSessionCheckResult>();
        const bool queued = m_worker != nullptr &&
                            queue_job(
                                *m_worker,
                                [snapshot = m_snapshot, result](Job::Ctl& ctl) { check_startup_session(snapshot, *result, ctl); },
                                [this, result](bool canceled, std::exception_ptr& eptr) {
                                    if (eptr != nullptr) {
                                        try {
                                            std::rethrow_exception(eptr);
                                        } catch (const std::exception& ex) {
                                            result->error_message = ex.what();
                                        } catch (...) {
                                            result->error_message = "登录状态校验发生未知错误";
                                        }
                                        result->state = StartupSessionState::Failed;
                                        eptr          = nullptr;
                                    }
                                    if (canceled)
                                        result->state = StartupSessionState::Failed;

                                    apply_result_if_current(*result);
                                    m_timer.Stop();
                                    if (IsModal())
                                        EndModal(wxID_OK);
                                });

        if (!queued) {
            result->error_message = "无法启动登录状态校验";
            return *result;
        }

        m_timer.Start(80);
        dialogStack.push_front(this);
        const int  modal_result = ShowModal();
        const auto iter         = std::find(dialogStack.begin(), dialogStack.end(), this);
        if (iter != dialogStack.end())
            dialogStack.erase(iter);
        if (modal_result != wxID_OK) {
            result->state         = StartupSessionState::Aborted;
            result->error_message = "登录状态校验已取消";
        }
        return *result;
    }

private:
    void apply_result_if_current(StartupSessionCheckResult& result)
    {
        AppConfig* config = wxGetApp().app_config;
        if (config == nullptr || GFD::Config::current_environment_name(config) != m_snapshot.environment ||
            GFDAuthManager::current_auth_token(config) != m_snapshot.token) {
            result.state         = StartupSessionState::Aborted;
            result.error_message = "登录状态已在校验期间发生变化";
            return;
        }
        if (result.state != StartupSessionState::Valid)
            return;

        bool       changed         = false;
        const auto update_nonempty = [&changed](const std::string& value, const std::string& current, const auto& setter) {
            if (!value.empty() && value != current) {
                setter(value);
                changed = true;
            }
        };
        update_nonempty(result.user_uuid, GFD::Config::user_uuid(config),
                        [config](const std::string& value) { GFD::Config::set_user_uuid(config, value); });
        update_nonempty(result.phone, GFD::Config::user_phone(config),
                        [config](const std::string& value) { GFD::Config::set_user_phone(config, value); });
        update_nonempty(result.nickname, GFD::Config::user_nickname(config),
                        [config](const std::string& value) { GFD::Config::set_user_nickname(config, value); });
        update_nonempty(result.avatar, GFD::Config::user_avatar(config),
                        [config](const std::string& value) { GFD::Config::set_user_avatar(config, value); });

        if (!result.refreshed_token.empty() && result.refreshed_token != m_snapshot.token) {
            GFD::Config::set_auth_token(config, result.refreshed_token);
            changed = true;
        }
        if (!result.refresh_attempt_ts.empty()) {
            GFD::Config::set_token_refresh_attempt_ts(config, result.refresh_attempt_ts);
            changed = true;
        }
        if (!result.refresh_success_ts.empty()) {
            GFD::Config::set_token_refresh_success_ts(config, result.refresh_success_ts);
            changed = true;
        }
        if (changed) {
            config->save();
            notify_gfd_account_state_changed();
        }
    }

private:
    StartupSessionSnapshot  m_snapshot;
    std::unique_ptr<Worker> m_worker;
    wxGauge*                m_gauge{nullptr};
    wxTimer                 m_timer;
};

bool has_nonempty_login_identity(const AppConfig* config)
{
    if (config == nullptr)
        return false;

    if (GFD::Config::auth_mode(config) != GFD::Config::AUTH_MODE_USER_SMS)
        return false;

    return !trim_copy(GFD::Config::auth_token(config)).empty() || !trim_copy(GFD::Config::user_uuid(config)).empty();
}

void preserve_legacy_parameter_sync_token(AppConfig* config)
{
    if (config == nullptr || !GFD::Config::parameter_sync_token(config).empty() ||
        GFD::Config::auth_mode(config) == GFD::Config::AUTH_MODE_USER_SMS)
        return;

    const std::string legacy_token = trim_copy(GFD::Config::auth_token(config));
    if (!legacy_token.empty())
        GFD::Config::set_parameter_sync_token(config, legacy_token);
}

struct PendingLoginRequest
{
    GFDAuthManager::LoginFinishedFn finished;
    bool                            parent_was_set{false};
    wxWeakRef<wxWindow>             parent;
};

bool                             g_async_login_in_progress{false};
std::vector<PendingLoginRequest> g_pending_login_requests;

void notify_gfd_account_state_changed()
{
    if (wxTheApp == nullptr)
        return;
    wxTheApp->CallAfter([]() {
        if (wxGetApp().mainframe != nullptr)
            wxGetApp().mainframe->update_gfd_account_button();
    });
}

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
    if (GFD::Config::auth_mode(config) != GFD::Config::AUTH_MODE_USER_SMS)
        return false;

    const std::string auth_token = trim_copy(GFD::Config::auth_token(config));
    const bool        valid      = !auth_token.empty();
    BOOST_LOG_TRIVIAL(info) << "GFD session local validity"
                            << ", env=" << GFD::Config::current_environment_name(config) << ", token_length=" << auth_token.size()
                            << ", valid=" << valid;
    return valid;
}

std::string GFDAuthManager::current_auth_token(const AppConfig* config)
{
    if (config == nullptr || GFD::Config::auth_mode(config) != GFD::Config::AUTH_MODE_USER_SMS)
        return {};

    return trim_copy(GFD::Config::auth_token(config));
}

void GFDAuthManager::clear_session(AppConfig* config)
{
    if (config == nullptr)
        return;

    GFD::Config::clear_login_identity(config);
    GFD::Config::clear_verify_cache(config);
    config->save();
    notify_gfd_account_state_changed();
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
    notify_gfd_account_state_changed();
}

bool GFDAuthManager::ensure_logged_in(wxWindow* parent, std::string* error_message)
{
    auto* config = wxGetApp().app_config;
    if (has_valid_session(config))
        return true;

    preserve_legacy_parameter_sync_token(config);
    clear_session(config);

    const bool logged_in = wxGetApp().ShowGFDLogin(parent);
    if (!logged_in) {
        if (error_message != nullptr)
            *error_message = "登录状态无效，请重新登录";
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

    preserve_legacy_parameter_sync_token(config);
    clear_session(config);
    auto show_manual_login = [](std::string login_error) mutable {
        wxWindow* login_parent = nullptr;
        if (!resolve_pending_login_parent(login_parent)) {
            if (login_error.empty())
                login_error = "登录操作已取消";
            finish_async_login_requests(false, std::move(login_error));
            return;
        }
        const bool logged_in = wxGetApp().ShowGFDLogin(login_parent);
        const bool success   = logged_in && (GFDAuthManager::has_valid_session(wxGetApp().app_config) ||
                                           has_nonempty_login_identity(wxGetApp().app_config));
        if (!success && login_error.empty())
            login_error = "登录状态无效，请重新登录";
        finish_async_login_requests(success, std::move(login_error));
    };

    // SMS codes are one-time credentials, so a user login must never replay the old
    // middle-platform account/password cache in the background.
    wxGetApp().CallAfter([show_manual_login = std::move(show_manual_login)]() mutable { show_manual_login({}); });
    return true;
}

bool GFDAuthManager::ensure_startup_session(wxWindow* parent, std::string* error_message)
{
    AppConfig* config = wxGetApp().app_config;
    if (config == nullptr) {
        if (error_message != nullptr)
            *error_message = "无法读取应用配置";
        return false;
    }

    for (;;) {
        if (!has_valid_session(config) && !ensure_logged_in(parent, error_message))
            return false;

        const std::string token = current_auth_token(config);
        if (token.empty()) {
            clear_session(config);
            continue;
        }

        const long long        now = static_cast<long long>(std::time(nullptr));
        StartupSessionSnapshot snapshot{
            GFD::Config::current_environment_name(config),
            token,
            GFD::Config::user_info_url(config),
            GFD::Config::token_refresh_url(config),
            token_refresh_due(GFD::Config::token_refresh_attempt_ts(config).empty() ? GFD::Config::token_refresh_success_ts(config) :
                                                                                      GFD::Config::token_refresh_attempt_ts(config),
                              now),
        };

        GFDSessionCheckDialog     dialog(parent, std::move(snapshot));
        StartupSessionCheckResult result = dialog.run();
        if (result.state == StartupSessionState::Valid) {
            if (!result.error_message.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "GFD startup token refresh skipped after a valid user-info response"
                                           << ", env=" << GFD::Config::current_environment_name(config)
                                           << ", error=" << result.error_message;
            }
            return true;
        }
        if (result.state == StartupSessionState::Offline) {
            // A transport/server outage does not prove that a session expired.
            // Keep the locally cached identity so slicing remains available.
            BOOST_LOG_TRIVIAL(warning) << "GFD startup session validation unavailable; keep cached session"
                                       << ", env=" << GFD::Config::current_environment_name(config) << ", error=" << result.error_message;
            return true;
        }
        if (result.state == StartupSessionState::Failed) {
            BOOST_LOG_TRIVIAL(error) << "GFD startup session validation failed"
                                     << ", env=" << GFD::Config::current_environment_name(config) << ", error=" << result.error_message;
            wxMessageDialog retry_dialog(parent, _L("Unable to verify your login status. Check your network connection and try again."),
                                         _L("Login verification failed"), wxYES_NO | wxICON_WARNING);
            retry_dialog.SetYesNoLabels(_L("Retry"), _L("Cancel"));
            if (retry_dialog.ShowModal() == wxID_YES)
                continue;
            if (error_message != nullptr)
                *error_message = result.error_message;
            return false;
        }
        if (result.state == StartupSessionState::Aborted) {
            if (error_message != nullptr)
                *error_message = result.error_message;
            return false;
        }

        BOOST_LOG_TRIVIAL(info) << "GFD startup session rejected by service; require SMS login"
                                << ", env=" << GFD::Config::current_environment_name(config);
        clear_session(config);
        if (error_message != nullptr)
            *error_message = result.error_message;
        // Loop after a successful SMS login so the newly issued token is also
        // validated and the user profile is populated before presets load.
    }
}

bool GFDAuthManager::perform_authenticated_request(const RequestFn& request, std::string& body, std::string& error_message, wxWindow* parent)
{
    if (!ensure_logged_in(parent, &error_message))
        return false;

    bool       missing_token = false;
    const auto run_once      = [&]() {
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
            bool has_business_code = false;
            for (const char* key : {"code", "ret"}) {
                const auto field = response.find(key);
                if (field != response.end() && !field->is_null())
                    has_business_code = true;
                const auto code = json_integer(response, key);
                if (code.has_value() && (*code == 401 || *code == 403 || *code == 4009 || *code == 10334))
                    return true;
            }
            if (has_business_code)
                return false;

            const std::string msg     = string_field("msg");
            const std::string message = string_field("message");
            const std::string error   = string_field("error");
            if (contains_auth_failure_text(msg) || contains_auth_failure_text(message) || contains_auth_failure_text(error))
                return true;
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
