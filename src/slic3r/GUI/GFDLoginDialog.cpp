#include "GFDLoginDialog.hpp"

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "MainFrame.hpp"
#include "Jobs/BoostThreadWorker.hpp"
#include "Jobs/PlaterWorker.hpp"
#include "Jobs/Worker.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/TextInput.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/GFDConfig.hpp"
#include "GFDAuthManager.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/beast/core/detail/base64.hpp>
#include <nlohmann/json.hpp>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/err.h>

#include <algorithm>
#include <functional>
#include <regex>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/weakref.h>

#include <ctime>

namespace Slic3r { namespace GUI {

using json = nlohmann::json;

namespace {

constexpr long VERIFY_VALID_SECONDS          = 24 * 60 * 60;
constexpr long LOGIN_CONNECT_TIMEOUT_SECONDS = 10;
constexpr long LOGIN_REQUEST_TIMEOUT_SECONDS = 20;

std::string trim_copy(const std::string& value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

long long now_ts() { return static_cast<long long>(std::time(nullptr)); }

std::string sanitize_response_body(const std::string& body)
{
    std::string sanitized = trim_copy(body);
    if (sanitized.size() >= 3 && static_cast<unsigned char>(sanitized[0]) == 0xEF && static_cast<unsigned char>(sanitized[1]) == 0xBB &&
        static_cast<unsigned char>(sanitized[2]) == 0xBF) {
        sanitized.erase(0, 3);
    }
    return sanitized;
}

std::string preview_response_body(const std::string& body, std::size_t max_length = 240)
{
    std::string preview = sanitize_response_body(body);
    if (preview.size() > max_length) {
        preview.resize(max_length);
        preview += "...";
    }
    return preview;
}

bool try_parse_response_json(const std::string& body, json& response, std::string& parse_error)
{
    const std::string sanitized = sanitize_response_body(body);
    if (sanitized.empty()) {
        parse_error = "empty response body";
        return false;
    }

    try {
        response = json::parse(sanitized);
        return true;
    } catch (const std::exception& ex) {
        parse_error = ex.what();
        return false;
    } catch (...) {
        parse_error = "unknown parse error";
        return false;
    }
}

std::string json_string_or_empty(const json& object, const char* key)
{
    if (!object.is_object() || key == nullptr || !object.contains(key) || object[key].is_null())
        return {};
    if (object[key].is_string())
        return object[key].get<std::string>();
    return {};
}

bool has_valid_verify_cache()
{
    const auto*       cfg           = wxGetApp().app_config;
    const std::string token         = GFD::Config::verify_token(cfg);
    const std::string expire_ts_str = GFD::Config::verify_expire_ts(cfg);
    if (token.empty() || expire_ts_str.empty())
        return false;

    try {
        return now_ts() <= std::stoll(expire_ts_str);
    } catch (...) {
        return false;
    }
}

void clear_verify_cache_if_user_changed(const std::string& email)
{
    auto* config = wxGetApp().app_config;
    if (config == nullptr)
        return;

    const std::string cached_email = trim_copy(GFD::Config::user_email(config));
    if (!cached_email.empty() && cached_email != trim_copy(email)) {
        GFD::Config::clear_verify_cache(config);
        GFD::Config::set_auth_token(config, "");
    }
}

bool persist_login_state(const std::string& email,
                         const std::string& password,
                         const std::string& uuid,
                         const std::string& auth_token,
                         const std::string& verify_token,
                         bool               persist_credentials,
                         bool               remember_credentials)
{
    if (wxGetApp().app_config == nullptr)
        return false;

    if (persist_credentials && remember_credentials) {
        GFD::Config::save_login_credential(wxGetApp().app_config, email, password);
    } else if (persist_credentials) {
        GFD::Config::save_login_credential(wxGetApp().app_config, email, "");
    }

    GFD::Config::set_verify_token(wxGetApp().app_config, verify_token);
    if (!verify_token.empty())
        GFD::Config::set_verify_expire_ts(wxGetApp().app_config, std::to_string(now_ts() + VERIFY_VALID_SECONDS));
    else
        GFD::Config::set_verify_expire_ts(wxGetApp().app_config, "");
    GFD::Config::set_user_email(wxGetApp().app_config, email);
    GFD::Config::set_user_uuid(wxGetApp().app_config, uuid);
    GFD::Config::set_auth_token(wxGetApp().app_config, auth_token);
    wxGetApp().app_config->save();
    return true;
}

struct LoginAttemptResult
{
    bool        ok{false};
    std::string uuid;
    std::string auth_token;
    std::string error_message;
};

struct VerifyAttemptResult
{
    bool        ok{false};
    std::string verify_token;
    std::string error_message;
};

bool get_json_request(const std::string& url,
                      Job::Ctl&          ctl,
                      std::string&       body,
                      unsigned&          status,
                      std::string&       error_message)
{
    bool ok                     = false;
    bool cancellation_requested = false;

    Http::get(url)
        .header("Content-Type", "application/json")
        .timeout_connect(LOGIN_CONNECT_TIMEOUT_SECONDS)
        .timeout_max(LOGIN_REQUEST_TIMEOUT_SECONDS)
        .on_progress([&](Http::Progress, bool& cancel) {
            cancellation_requested = ctl.was_canceled();
            cancel                 = cancellation_requested;
        })
        .on_complete([&](std::string response_body, unsigned http_status) {
            body   = std::move(response_body);
            status = http_status;
            ok     = true;
        })
        .on_error([&](std::string response_body, std::string error, unsigned http_status) {
            body          = std::move(response_body);
            status        = http_status;
            error_message = std::move(error);
            ok            = false;
        })
        .perform_sync();

    if (cancellation_requested && !ok)
        error_message = "操作已取消";
    return ok;
}

bool post_json_request(const std::string& url,
                       const std::string& request_body,
                       Job::Ctl&          ctl,
                       std::string&       body,
                       unsigned&          status,
                       std::string&       error_message)
{
    bool ok                     = false;
    bool cancellation_requested = false;

    Http::post(url)
        .header("Content-Type", "application/json")
        .set_post_body(request_body)
        .timeout_connect(LOGIN_CONNECT_TIMEOUT_SECONDS)
        .timeout_max(LOGIN_REQUEST_TIMEOUT_SECONDS)
        .on_progress([&](Http::Progress, bool& cancel) {
            cancellation_requested = ctl.was_canceled();
            cancel                 = cancellation_requested;
        })
        .on_complete([&](std::string response_body, unsigned http_status) {
            body   = std::move(response_body);
            status = http_status;
            ok     = true;
        })
        .on_error([&](std::string response_body, std::string error, unsigned http_status) {
            body          = std::move(response_body);
            status        = http_status;
            error_message = std::move(error);
            ok            = false;
        })
        .perform_sync();

    if (cancellation_requested && !ok)
        error_message = "操作已取消";
    return ok;
}

std::string extract_response_message(const std::string& body, const std::string& fallback = {})
{
    if (body.empty())
        return fallback;

    try {
        const json response = json::parse(body);
        if (response.contains("msg") && response["msg"].is_string())
            return response["msg"].get<std::string>();
        if (response.contains("message") && response["message"].is_string())
            return response["message"].get<std::string>();
    } catch (...) {}

    return fallback.empty() ? body : fallback;
}

bool parse_public_key_response(const std::string& body,
                               const std::string& environment,
                               std::string&       public_key,
                               std::string&       error_message)
{
    json        response;
    std::string parse_error;
    if (!try_parse_response_json(body, response, parse_error)) {
        BOOST_LOG_TRIVIAL(error) << "GFD public key response parse failed"
                                 << ", env=" << environment
                                 << ", error=" << parse_error
                                 << ", body_preview=" << preview_response_body(body);
        error_message = "登录服务返回异常，请稍后重试";
        return false;
    }

    try {
        if (response.contains("msg") && response["msg"].is_string() && response["msg"].get<std::string>() == "success") {
            public_key = response.value("data", std::string());
            return !public_key.empty();
        }
        error_message = extract_response_message(body, "获取公钥失败");
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "GFD public key response handling failed"
                                 << ", env=" << environment
                                 << ", error=" << ex.what()
                                 << ", body_preview=" << preview_response_body(body);
        error_message = "登录服务返回异常，请稍后重试";
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "GFD public key response handling failed"
                                 << ", env=" << environment
                                 << ", error=unknown"
                                 << ", body_preview=" << preview_response_body(body);
        error_message = "登录服务返回异常，请稍后重试";
    }
    return false;
}

bool parse_login_response(const std::string& body, const std::string& environment, LoginAttemptResult& result)
{
    json        response;
    std::string parse_error;
    if (!try_parse_response_json(body, response, parse_error)) {
        BOOST_LOG_TRIVIAL(error) << "GFD login response parse failed"
                                 << ", env=" << environment
                                 << ", error=" << parse_error
                                 << ", body_preview=" << preview_response_body(body);
        result.error_message = "登录服务返回异常，请稍后重试";
        return false;
    }

    try {
        BOOST_LOG_TRIVIAL(info) << "GFD login response parsed"
                                << ", env=" << environment
                                << ", has_data=" << (response.contains("data") && response["data"].is_object())
                                << ", data_has_uuid="
                                << (response.contains("data") && response["data"].is_object() && response["data"].contains("uuid"))
                                << ", data_has_token="
                                << (response.contains("data") && response["data"].is_object() && response["data"].contains("token"))
                                << ", data_has_access_token="
                                << (response.contains("data") && response["data"].is_object() && response["data"].contains("accessToken"));
        if (response.contains("msg") && response["msg"].is_string() && response["msg"].get<std::string>() == "success") {
            if (response.contains("data") && response["data"].is_object()) {
                const json& data = response["data"];
                result.uuid      = json_string_or_empty(data, "uuid");
                result.auth_token = json_string_or_empty(data, "token");
                if (result.auth_token.empty())
                    result.auth_token = json_string_or_empty(data, "accessToken");
            }
            result.ok = !result.uuid.empty() || !result.auth_token.empty();
            if (!result.ok)
                result.error_message = "登录服务未返回有效身份信息";
            return result.ok;
        }
        BOOST_LOG_TRIVIAL(warning) << "GFD login rejected"
                                   << ", env=" << environment
                                   << ", body=" << body;
        result.error_message = extract_response_message(body, "登录失败");
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "GFD login response handling failed"
                                 << ", env=" << environment
                                 << ", error=" << ex.what()
                                 << ", body_preview=" << preview_response_body(body);
        result.error_message = "登录服务返回异常，请稍后重试";
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "GFD login response handling failed"
                                 << ", env=" << environment
                                 << ", error=unknown"
                                 << ", body_preview=" << preview_response_body(body);
        result.error_message = "登录服务返回异常，请稍后重试";
    }
    return false;
}

bool parse_verify_response(const std::string& body, const std::string& environment, VerifyAttemptResult& result)
{
    json        response;
    std::string parse_error;
    if (!try_parse_response_json(body, response, parse_error)) {
        BOOST_LOG_TRIVIAL(error) << "GFD verify response parse failed"
                                 << ", env=" << environment
                                 << ", error=" << parse_error
                                 << ", body_preview=" << preview_response_body(body);
        result.error_message = "验证码服务返回异常，请稍后重试";
        return false;
    }

    try {
        BOOST_LOG_TRIVIAL(info) << "GFD verify response parsed"
                                << ", env=" << environment
                                << ", has_data=" << (response.contains("data") && response["data"].is_object());
        if (response.contains("msg") && response["msg"].is_string() && response["msg"].get<std::string>() == "success") {
            if (response.contains("data") && response["data"].is_object()) {
                const json& data   = response["data"];
                result.verify_token = json_string_or_empty(data, "token");
                if (result.verify_token.empty())
                    result.verify_token = json_string_or_empty(data, "accessToken");
            }
            result.ok = !result.verify_token.empty();
            if (!result.ok)
                result.error_message = "验证码服务未返回有效凭据";
            return result.ok;
        }
        BOOST_LOG_TRIVIAL(warning) << "GFD verify rejected"
                                   << ", env=" << environment
                                   << ", body=" << body;
        result.error_message = extract_response_message(body, "验证码校验失败");
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "GFD verify response handling failed"
                                 << ", env=" << environment
                                 << ", error=" << ex.what()
                                 << ", body_preview=" << preview_response_body(body);
        result.error_message = "验证码服务返回异常，请稍后重试";
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "GFD verify response handling failed"
                                 << ", env=" << environment
                                 << ", error=unknown"
                                 << ", body_preview=" << preview_response_body(body);
        result.error_message = "验证码服务返回异常，请稍后重试";
    }
    return false;
}

void apply_window_button_style(Button* button, ButtonStyle style)
{
    if (button != nullptr)
        button->SetStyle(style, ButtonType::Choice);
}

wxWindow* login_parent_window(wxWindow* preferred_parent = nullptr)
{
    if (preferred_parent != nullptr && preferred_parent->IsShownOnScreen())
        return preferred_parent;
    if (wxGetApp().mainframe != nullptr)
        return static_cast<wxWindow*>(wxGetApp().mainframe);
    return wxTheApp != nullptr ? wxTheApp->GetTopWindow() : nullptr;
}

wxWindow* resolve_verify_parent(wxWindow* preferred_parent)
{
    if (preferred_parent != nullptr && preferred_parent->IsShownOnScreen())
        return preferred_parent;

    if (wxGetApp().mainframe != nullptr && wxGetApp().mainframe->IsShownOnScreen())
        return static_cast<wxWindow*>(wxGetApp().mainframe);

    wxWindow* fallback_parent = wxTheApp != nullptr ? wxTheApp->GetTopWindow() : nullptr;
    if (fallback_parent != nullptr && fallback_parent != preferred_parent && fallback_parent->IsShownOnScreen())
        return fallback_parent;

    // During cold startup the automatic-login dialog is intentionally hidden and may be the only
    // top-level window. A parentless modal verification dialog remains visible on both macOS and Windows.
    return nullptr;
}

wxBoxSizer* create_environment_item(wxWindow* parent,
                                    RadioBox*& radio,
                                    const wxString& label,
                                    int dip_spacing,
                                    const std::function<void()>& on_select)
{
    auto* item_sizer = new wxBoxSizer(wxHORIZONTAL);
    radio            = new RadioBox(parent);
    item_sizer->Add(radio, 0, wxALIGN_CENTER_VERTICAL);
    item_sizer->AddSpacer(dip_spacing);

    auto* text = new wxStaticText(parent, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, 0);
    text->SetFont(::Label::Body_12);
    item_sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL);

    radio->Bind(wxEVT_LEFT_DOWN, [on_select](wxMouseEvent& event) {
        on_select();
        event.Skip(false);
    });
    text->Bind(wxEVT_LEFT_DOWN, [on_select](wxMouseEvent&) { on_select(); });
    return item_sizer;
}

} // namespace

GFDLoginDialog::GFDLoginDialog(wxWindow* parent, bool use_fallback_parent)
    : wxDialog(use_fallback_parent ? login_parent_window(parent) : parent,
               wxID_ANY,
               _L("功夫豆Orca - 登录"),
               wxDefaultPosition,
               wxDefaultSize,
               wxCAPTION | wxCLOSE_BOX)
{
    auto worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(this, std::shared_ptr<ProgressIndicator>{}, "gfd_login_worker");
    worker->set_busy_cursor_enabled(false);
    m_worker = std::move(worker);

    build();
    bind_events();
    load_cached_credentials();
    wxGetApp().UpdateDlgDarkUI(this);
}

GFDLoginDialog::~GFDLoginDialog()
{
    m_destroying     = true;
    m_login_finished = {};
    if (m_worker != nullptr) {
        m_worker->cancel_all();
        m_worker->wait_for_idle();
    }
}

bool GFDLoginDialog::run() { return ShowModal() == wxID_OK; }

GFDLoginDialog::LoginResult GFDLoginDialog::login_with_credentials(wxWindow*          parent,
                                                                   const std::string& username,
                                                                   const std::string& password,
                                                                   std::string&       error_message,
                                                                   bool               persist_credentials,
                                                                   bool               remember_credentials)
{
    GFDLoginDialog dialog(parent);
    return dialog.login_with_credentials_local(username, password, error_message, persist_credentials, remember_credentials);
}

GFDLoginDialog::LoginResult GFDLoginDialog::login_with_credentials(const std::string& username,
                                                                   const std::string& password,
                                                                   std::string&       error_message,
                                                                   bool               persist_credentials,
                                                                   bool               remember_credentials)
{
    return login_with_credentials(nullptr, username, password, error_message, persist_credentials, remember_credentials);
}

bool GFDLoginDialog::login_with_credentials_async(
    const std::string& username, const std::string& password, LoginFinishedFn finished, bool persist_credentials, bool remember_credentials)
{
    if (!finished)
        return false;

    auto* dialog             = new GFDLoginDialog(nullptr, false);
    dialog->m_login_finished = std::move(finished);
    if (!dialog->prepare_login_attempt(username, password, persist_credentials, remember_credentials))
        dialog->finish_async_login();
    return true;
}

GFDLoginDialog::LoginResult GFDLoginDialog::login_with_credentials_local(const std::string& username,
                                                                         const std::string& password,
                                                                         std::string&       error_message,
                                                                         bool               persist_credentials,
                                                                         bool               remember_credentials)
{
    if (!prepare_login_attempt(username, password, persist_credentials, remember_credentials)) {
        error_message = m_last_error;
        return m_login_result;
    }

    ShowModal();
    error_message = m_last_error;
    return m_login_result;
}

bool GFDLoginDialog::prepare_login_attempt(const std::string& username,
                                           const std::string& password,
                                           bool               persist_credentials,
                                           bool               remember_credentials)
{
    const std::string email = trim_copy(username);
    if (email.empty()) {
        m_login_result = LoginResult::Failed;
        m_last_error   = "请输入账户";
        return false;
    }

    static const std::regex email_regex(R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)");
    if (!std::regex_match(email, email_regex)) {
        m_login_result = LoginResult::Failed;
        m_last_error   = "邮箱格式错误";
        return false;
    }

    if (password.empty()) {
        m_login_result = LoginResult::Failed;
        m_last_error   = "请输入密码";
        return false;
    }

    m_username_input->SetValue(from_u8(email));
    m_password_input->GetTextCtrl()->SetValue(from_u8(password));
    m_remember_checkbox->SetValue(remember_credentials);
    m_login_result = LoginResult::Cancelled;
    m_last_error.clear();

    if (!apply_selected_environment()) {
        m_login_result = LoginResult::Failed;
        m_last_error   = "环境配置初始化失败";
        return false;
    }
    if (!start_login_attempt(email, password, persist_credentials, remember_credentials)) {
        if (m_last_error.empty())
            m_last_error = "无法启动登录任务，请稍后重试";
        return false;
    }
    return true;
}

void GFDLoginDialog::finish_async_login()
{
    if (m_destroying || !m_login_finished)
        return;

    LoginFinishedFn           finished = std::move(m_login_finished);
    const LoginResult         result   = m_login_result;
    std::string               error    = m_last_error;
    wxWeakRef<GFDLoginDialog> weak_this(this);
    wxGetApp().CallAfter([weak_this, finished = std::move(finished), result, error = std::move(error)]() mutable {
        if (weak_this)
            weak_this->Destroy();
        finished(result, std::move(error));
    });
}

void GFDLoginDialog::build()
{
    SetBackgroundColour(*wxWHITE);
    SetMinSize(wxSize(FromDIP(410), FromDIP(320)));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->AddSpacer(FromDIP(34));

    auto* username_row   = new wxBoxSizer(wxHORIZONTAL);
    auto* username_label = new wxStaticText(this, wxID_ANY, _L("账户"), wxDefaultPosition, wxDefaultSize, 0);
    username_label->SetFont(::Label::Body_13);
    username_label->SetMinSize(wxSize(FromDIP(40), -1));
    username_row->Add(username_label, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(26));

    m_username_input = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(260), FromDIP(36)),
                                    0, nullptr, 0);
    m_username_input->SetCornerRadius(FromDIP(6));
    username_row->Add(m_username_input, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(12));
    main_sizer->Add(username_row, 0, wxEXPAND);

    auto* password_row   = new wxBoxSizer(wxHORIZONTAL);
    auto* password_label = new wxStaticText(this, wxID_ANY, _L("密码"), wxDefaultPosition, wxDefaultSize, 0);
    password_label->SetFont(::Label::Body_13);
    password_label->SetMinSize(wxSize(FromDIP(40), -1));
    password_row->Add(password_label, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(26));

    m_password_input = new TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition,
                                     wxSize(FromDIP(260), FromDIP(36)), wxTE_PASSWORD | wxTE_PROCESS_ENTER);
    m_password_input->SetCornerRadius(FromDIP(6));
    password_row->Add(m_password_input, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(12));
    main_sizer->Add(password_row, 0, wxTOP | wxEXPAND, FromDIP(20));

    auto* environment_row   = new wxBoxSizer(wxHORIZONTAL);
    auto* environment_label = new wxStaticText(this, wxID_ANY, _L("环境"), wxDefaultPosition, wxDefaultSize, 0);
    environment_label->SetFont(::Label::Body_13);
    environment_label->SetMinSize(wxSize(FromDIP(40), -1));
    environment_row->Add(environment_label, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(26));

    auto* environment_options = new wxBoxSizer(wxHORIZONTAL);
    environment_options->Add(create_environment_item(this, m_qa_environment_radio, _L("测试环境"), FromDIP(6),
                                                     [this]() { select_environment(GFD::Config::ENV_QA); }),
                             0, wxALIGN_CENTER_VERTICAL);
    environment_options->AddSpacer(FromDIP(24));
    environment_options->Add(create_environment_item(this, m_production_environment_radio, _L("正式环境"), FromDIP(6),
                                                     [this]() { select_environment(GFD::Config::ENV_PRODUCTION); }),
                             0, wxALIGN_CENTER_VERTICAL);
    environment_row->Add(environment_options, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(12));
    main_sizer->Add(environment_row, 0, wxTOP | wxEXPAND, FromDIP(18));

    auto* remember_row  = new wxBoxSizer(wxHORIZONTAL);
    m_remember_checkbox = new CheckBox(this, wxID_ANY);
    remember_row->Add(m_remember_checkbox, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(31));
    m_remember_label = new wxStaticText(this, wxID_ANY, _L("记住账号密码"), wxDefaultPosition, wxDefaultSize, 0);
    m_remember_label->SetFont(::Label::Body_12);
    remember_row->Add(m_remember_label, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(8));
    m_tip_label = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_tip_label->SetFont(::Label::Body_12);
    m_tip_label->SetForegroundColour(wxColour(255, 0, 0));
    m_tip_label->Wrap(FromDIP(336));
    m_tip_label->SetMinSize(wxSize(FromDIP(336), FromDIP(34)));
    remember_row->AddStretchSpacer(1);
    main_sizer->Add(remember_row, 0, wxTOP | wxEXPAND, FromDIP(18));
    main_sizer->Add(m_tip_label, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(24));

    auto* button_row = new wxBoxSizer(wxHORIZONTAL);
    button_row->AddStretchSpacer(1);

    m_cancel_button = new Button(this, _L("取消"));
    apply_window_button_style(m_cancel_button, ButtonStyle::Regular);
    m_cancel_button->SetMinSize(wxSize(FromDIP(116), FromDIP(44)));
    m_cancel_button->SetSize(wxSize(FromDIP(116), FromDIP(44)));
    m_cancel_button->SetCornerRadius(FromDIP(22));
    button_row->Add(m_cancel_button, 0, wxRIGHT, FromDIP(15));

    m_login_button = new Button(this, _L("登录"));
    apply_window_button_style(m_login_button, ButtonStyle::Confirm);
    m_login_button->SetMinSize(wxSize(FromDIP(128), FromDIP(44)));
    m_login_button->SetSize(wxSize(FromDIP(128), FromDIP(44)));
    m_login_button->SetCornerRadius(FromDIP(22));
    button_row->Add(m_login_button, 0, wxRIGHT, FromDIP(24));

    main_sizer->Add(button_row, 0, wxBOTTOM | wxEXPAND, FromDIP(28));

    SetSizerAndFit(main_sizer);
    if (GetSize().x < FromDIP(410))
        SetSize(wxSize(FromDIP(410), GetSize().y));
    Centre();
}

void GFDLoginDialog::bind_events()
{
    m_login_button->Bind(wxEVT_BUTTON, &GFDLoginDialog::on_login, this);
    m_cancel_button->Bind(wxEVT_BUTTON, &GFDLoginDialog::on_cancel, this);
    m_username_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, &GFDLoginDialog::on_login, this);
    m_password_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, &GFDLoginDialog::on_login, this);
    m_username_input->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
        fill_saved_password(into_u8(m_username_input->GetValue()));
    });
    m_qa_environment_radio->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { select_environment(GFD::Config::ENV_QA); });
    m_production_environment_radio->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { select_environment(GFD::Config::ENV_PRODUCTION); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { cancel_request_and_close(); });
}

void GFDLoginDialog::load_cached_credentials()
{
    if (wxGetApp().app_config == nullptr)
        return;

    load_environment_selection();
}

void GFDLoginDialog::load_environment_selection()
{
    select_environment(GFD::Config::current_environment_name(wxGetApp().app_config));
}

void GFDLoginDialog::load_saved_credentials_for_selected_environment()
{
    if (wxGetApp().app_config == nullptr || m_username_input == nullptr || m_password_input == nullptr || m_remember_checkbox == nullptr)
        return;

    const auto credentials = GFD::Config::saved_login_credentials(wxGetApp().app_config, selected_environment());
    m_username_input->Clear();
    for (const auto& credential : credentials)
        m_username_input->Append(from_u8(credential.username));

    if (credentials.empty()) {
        m_username_input->SetValue(wxEmptyString);
        m_password_input->GetTextCtrl()->SetValue(wxEmptyString);
        m_remember_checkbox->SetValue(GFD::Config::remember_login(wxGetApp().app_config));
        return;
    }

    m_username_input->SetSelection(0);
    m_username_input->SetValue(from_u8(credentials.front().username));
    m_password_input->GetTextCtrl()->SetValue(from_u8(credentials.front().password));
    m_remember_checkbox->SetValue(!credentials.front().password.empty());
}

void GFDLoginDialog::fill_saved_password(const std::string& username)
{
    if (wxGetApp().app_config == nullptr || m_password_input == nullptr || m_remember_checkbox == nullptr)
        return;

    const std::string normalized_username = trim_copy(username);
    const auto credentials = GFD::Config::saved_login_credentials(wxGetApp().app_config, selected_environment());
    const auto credential_it = std::find_if(credentials.begin(), credentials.end(), [&normalized_username](const auto& credential) {
        return credential.username == normalized_username;
    });
    if (credential_it != credentials.end() && !credential_it->password.empty()) {
        m_password_input->GetTextCtrl()->SetValue(from_u8(credential_it->password));
        m_remember_checkbox->SetValue(true);
    }
}

void GFDLoginDialog::save_cached_credentials()
{
    if (wxGetApp().app_config == nullptr)
        return;

    const bool remember = m_remember_checkbox->GetValue();
    const std::string username = trim_copy(into_u8(m_username_input->GetTextCtrl()->GetValue()));
    if (remember) {
        GFD::Config::save_login_credential(wxGetApp().app_config, username,
                                           into_u8(m_password_input->GetTextCtrl()->GetValue()), selected_environment());
    } else {
        GFD::Config::save_login_credential(wxGetApp().app_config, username, "", selected_environment());
    }
}

void GFDLoginDialog::select_environment(const std::string& environment)
{
    if (m_request_active)
        return;

    const bool use_qa = environment == GFD::Config::ENV_QA;
    if (m_qa_environment_radio != nullptr)
        m_qa_environment_radio->SetValue(use_qa);
    if (m_production_environment_radio != nullptr)
        m_production_environment_radio->SetValue(!use_qa);
    load_saved_credentials_for_selected_environment();
}

std::string GFDLoginDialog::selected_environment() const
{
    if (m_qa_environment_radio != nullptr && m_qa_environment_radio->GetValue())
        return GFD::Config::ENV_QA;
    return GFD::Config::ENV_PRODUCTION;
}

bool GFDLoginDialog::apply_selected_environment()
{
    auto* config = wxGetApp().app_config;
    if (config == nullptr)
        return false;

    const std::string environment = selected_environment();
    const bool        environment_changed = GFD::Config::current_environment_name(config) != environment;
    GFD::Config::set_environment(config, environment);
    if (environment_changed)
        GFDAuthManager::logout(config, false);
    else
        config->save();
    return true;
}

bool GFDLoginDialog::validate_input()
{
    const std::string username = trim_copy(into_u8(m_username_input->GetTextCtrl()->GetValue()));
    if (username.empty()) {
        m_tip_label->SetLabel(_L("请输入账户"));
        m_tip_label->Wrap(FromDIP(336));
        Layout();
        return false;
    }
    static const std::regex email_regex(R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)");
    if (!std::regex_match(username, email_regex)) {
        m_tip_label->SetLabel(_L("邮箱格式错误"));
        m_tip_label->Wrap(FromDIP(336));
        Layout();
        return false;
    }
    if (into_u8(m_password_input->GetTextCtrl()->GetValue()).empty()) {
        m_tip_label->SetLabel(_L("请输入密码"));
        m_tip_label->Wrap(FromDIP(336));
        Layout();
        return false;
    }
    m_tip_label->SetLabel(wxEmptyString);
    Layout();
    return true;
}

void GFDLoginDialog::on_login(wxCommandEvent&)
{
    if (m_request_active)
        return;
    if (!validate_input())
        return;

    if (!apply_selected_environment()) {
        m_tip_label->SetLabel(_L("环境配置初始化失败"));
        m_tip_label->Wrap(FromDIP(336));
        Layout();
        return;
    }

    const std::string email    = trim_copy(into_u8(m_username_input->GetTextCtrl()->GetValue()));
    const std::string password = into_u8(m_password_input->GetTextCtrl()->GetValue());
    start_login_attempt(email, password, true, m_remember_checkbox->GetValue());
}

void GFDLoginDialog::on_cancel(wxCommandEvent&) { cancel_request_and_close(); }

void GFDLoginDialog::cancel_request_and_close()
{
    m_login_result = LoginResult::Cancelled;
    if (!m_request_active) {
        if (IsModal())
            EndModal(wxID_CANCEL);
        else
            Hide();
        return;
    }

    m_close_when_idle = true;
    m_last_error      = "登录已取消";
    m_tip_label->SetLabel(_L("正在取消登录..."));
    m_tip_label->Wrap(FromDIP(336));
    m_cancel_button->Enable(false);
    Layout();
    m_worker->cancel_all();
}

void GFDLoginDialog::set_login_controls_enabled(bool enabled)
{
    if (m_username_input != nullptr)
        m_username_input->Enable(enabled);
    if (m_password_input != nullptr)
        m_password_input->Enable(enabled);
    if (m_qa_environment_radio != nullptr) {
        if (enabled)
            m_qa_environment_radio->Enable();
        else
            m_qa_environment_radio->Disable();
    }
    if (m_production_environment_radio != nullptr) {
        if (enabled)
            m_production_environment_radio->Enable();
        else
            m_production_environment_radio->Disable();
    }
    if (m_remember_checkbox != nullptr)
        m_remember_checkbox->Enable(enabled);
    if (m_login_button != nullptr)
        m_login_button->Enable(enabled);
    if (m_cancel_button != nullptr)
        m_cancel_button->Enable(true);
}

bool GFDLoginDialog::start_login_attempt(const std::string& email,
                                         const std::string& password,
                                         bool               persist_credentials,
                                         bool               remember_credentials)
{
    if (m_request_active || m_worker == nullptr || wxGetApp().app_config == nullptr)
        return false;

    clear_verify_cache_if_user_changed(email);
    const std::string request_environment = GFD::Config::current_environment_name(wxGetApp().app_config);
    const std::string public_key_url      = GFD::Config::public_key_url(wxGetApp().app_config);
    const std::string login_url           = GFD::Config::login_url(wxGetApp().app_config);
    auto              result              = std::make_shared<LoginAttemptResult>();

    m_request_active = true;
    m_close_when_idle = false;
    m_last_error.clear();
    set_login_controls_enabled(false);
    m_tip_label->SetLabel(_L("正在登录，请稍候..."));
    m_tip_label->Wrap(FromDIP(336));
    Layout();

    const bool queued = queue_job(
        *m_worker,
        [result, email, password, public_key_url, login_url, request_environment](Job::Ctl& ctl) {
            std::string public_key;
            std::string body;
            unsigned    status = 0;
            if (!get_json_request(public_key_url, ctl, body, status, result->error_message)) {
                if (result->error_message.empty())
                    result->error_message = extract_response_message(body, "获取公钥失败");
                return;
            }
            if (ctl.was_canceled() || !parse_public_key_response(body, request_environment, public_key, result->error_message))
                return;

            const std::string encrypted_password = rsa_encrypt_password(password, public_key, result->error_message);
            if (encrypted_password.empty()) {
                if (result->error_message.empty())
                    result->error_message = "密码加密失败";
                return;
            }
            if (ctl.was_canceled())
                return;

            json request_json;
            request_json["email"]    = email;
            request_json["password"] = encrypted_password;
            body.clear();
            status = 0;
            if (!post_json_request(login_url, request_json.dump(), ctl, body, status, result->error_message)) {
                if (result->error_message.empty())
                    result->error_message = extract_response_message(body, "登录失败");
                return;
            }
            if (!ctl.was_canceled())
                parse_login_response(body, request_environment, *result);
        },
        [this, result, email, password, persist_credentials, remember_credentials, request_environment](bool                canceled,
                                                                                                        std::exception_ptr& eptr) mutable {
            if (m_destroying) {
                eptr = nullptr;
                return;
            }
            if (eptr != nullptr) {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception& ex) {
                    result->error_message = ex.what();
                } catch (...) {
                    result->error_message = "登录任务发生未知异常";
                }
                eptr = nullptr;
            }

            if (canceled || m_close_when_idle) {
                m_request_active = false;
                m_login_result   = LoginResult::Cancelled;
                if (m_last_error.empty())
                    m_last_error = "登录已取消";
                if (IsModal())
                    EndModal(wxID_CANCEL);
                finish_async_login();
                return;
            }

            if (request_environment != GFD::Config::current_environment_name(wxGetApp().app_config)) {
                result->ok            = false;
                result->error_message = "云环境已切换，请重新登录";
            }

            if (!result->ok) {
                m_request_active = false;
                m_login_result   = LoginResult::Failed;
                m_last_error     = result->error_message.empty() ? "登录失败" : result->error_message;
                set_login_controls_enabled(true);
                m_tip_label->SetLabel(from_u8(m_last_error));
                m_tip_label->Wrap(FromDIP(336));
                Layout();
                finish_async_login();
                return;
            }

            std::string verify_token;
            if (has_valid_verify_cache())
                verify_token = GFD::Config::verify_token(wxGetApp().app_config);
            if (verify_token.empty()) {
                wxWindow* verify_parent = resolve_verify_parent(this);
                BOOST_LOG_TRIVIAL(info) << "GFD login password step succeeded, opening verify dialog"
                                        << ", env=" << request_environment << ", uuid_empty=" << result->uuid.empty()
                                        << ", verify_parent_visible=" << (verify_parent != nullptr && verify_parent->IsShownOnScreen());
                GFDVerifyDialog verify_dialog(verify_parent);
                if (!verify_dialog.verify_login(result->uuid, verify_token)) {
                    m_request_active = false;
                    m_login_result   = LoginResult::Cancelled;
                    m_last_error     = "验证码校验已取消或失败";
                    if (m_close_when_idle) {
                        if (IsModal())
                            EndModal(wxID_CANCEL);
                        return;
                    }
                    set_login_controls_enabled(true);
                    m_tip_label->SetLabel(from_u8(m_last_error));
                    m_tip_label->Wrap(FromDIP(336));
                    Layout();
                    finish_async_login();
                    return;
                }
            }

            if (m_close_when_idle) {
                m_request_active = false;
                m_login_result   = LoginResult::Cancelled;
                m_last_error     = "登录已取消";
                if (IsModal())
                    EndModal(wxID_CANCEL);
                finish_async_login();
                return;
            }

            if (!persist_login_state(email, password, result->uuid, result->auth_token, verify_token, persist_credentials,
                                     remember_credentials)) {
                m_request_active = false;
                m_login_result   = LoginResult::Failed;
                m_last_error     = "保存登录状态失败";
                set_login_controls_enabled(true);
                m_tip_label->SetLabel(from_u8(m_last_error));
                m_tip_label->Wrap(FromDIP(336));
                Layout();
                finish_async_login();
                return;
            }

            m_request_active = false;
            m_login_result   = LoginResult::Success;
            m_last_error.clear();
            if (persist_credentials)
                save_cached_credentials();
            if (IsModal())
                EndModal(wxID_OK);
            finish_async_login();
        });

    if (!queued) {
        m_request_active = false;
        m_login_result   = LoginResult::Failed;
        m_last_error     = "无法启动登录任务，请稍后重试";
        set_login_controls_enabled(true);
        m_tip_label->SetLabel(from_u8(m_last_error));
        m_tip_label->Wrap(FromDIP(336));
        Layout();
    }
    return queued;
}

std::string GFDLoginDialog::rsa_encrypt_password(const std::string& password,
                                                 const std::string& public_key_base64,
                                                 std::string&       error_message)
{
    std::string key_text = trim_copy(public_key_base64);
    key_text.erase(std::remove(key_text.begin(), key_text.end(), '\n'), key_text.end());
    key_text.erase(std::remove(key_text.begin(), key_text.end(), '\r'), key_text.end());
    key_text.erase(std::remove(key_text.begin(), key_text.end(), ' '), key_text.end());

    std::string decoded;
    decoded.resize(boost::beast::detail::base64::decoded_size(key_text.size()));
    const auto result = boost::beast::detail::base64::decode(decoded.data(), key_text.data(), key_text.size());
    decoded.resize(result.first);

    const unsigned char* key_ptr = reinterpret_cast<const unsigned char*>(decoded.data());
    RSA*                 rsa     = d2i_RSA_PUBKEY(nullptr, &key_ptr, static_cast<long>(decoded.size()));
    if (rsa == nullptr) {
        BIO* bio = BIO_new_mem_buf(decoded.data(), static_cast<int>(decoded.size()));
        if (bio != nullptr) {
            rsa = PEM_read_bio_RSA_PUBKEY(bio, nullptr, nullptr, nullptr);
            BIO_free(bio);
        }
    }

    if (rsa == nullptr) {
        error_message = "公钥无效";
        return {};
    }

    std::string encrypted(RSA_size(rsa), '\0');
    const int   encrypted_size = RSA_public_encrypt(static_cast<int>(password.size()),
                                                  reinterpret_cast<const unsigned char*>(password.data()),
                                                  reinterpret_cast<unsigned char*>(&encrypted[0]), rsa, RSA_PKCS1_PADDING);
    RSA_free(rsa);

    if (encrypted_size <= 0) {
        unsigned long openssl_error = ERR_get_error();
        if (openssl_error != 0)
            error_message = ERR_error_string(openssl_error, nullptr);
        if (error_message.empty())
            error_message = "RSA 加密失败";
        return {};
    }

    encrypted.resize(static_cast<size_t>(encrypted_size));
    std::string encoded;
    encoded.resize(boost::beast::detail::base64::encoded_size(encrypted.size()));
    encoded.resize(boost::beast::detail::base64::encode(encoded.data(), encrypted.data(), encrypted.size()));
    return encoded;
}

GFDVerifyDialog::GFDVerifyDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("验证码校验"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    auto worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(this, std::shared_ptr<ProgressIndicator>{}, "gfd_verify_worker");
    worker->set_busy_cursor_enabled(false);
    m_worker = std::move(worker);

    build();
    bind_events();
    wxGetApp().UpdateDlgDarkUI(this);
}

GFDVerifyDialog::~GFDVerifyDialog()
{
    m_destroying = true;
    if (m_worker != nullptr) {
        m_worker->cancel_all();
        m_worker->wait_for_idle();
    }
}

void GFDVerifyDialog::build()
{
    SetBackgroundColour(*wxWHITE);
    SetMinSize(wxSize(FromDIP(360), FromDIP(180)));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, _L("请输入验证码"), wxDefaultPosition, wxDefaultSize, 0);
    title->SetFont(::Label::Head_14);
    main_sizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    m_code_input = new TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(300), FromDIP(32)),
                                 wxTE_PROCESS_ENTER);
    m_code_input->SetCornerRadius(FromDIP(6));
    main_sizer->Add(m_code_input, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(20));

    m_tip_label = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_tip_label->SetForegroundColour(wxColour(220, 38, 38));
    main_sizer->Add(m_tip_label, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* button_row = new wxBoxSizer(wxHORIZONTAL);
    button_row->AddStretchSpacer(1);

    m_cancel_button = new Button(this, _L("取消"));
    apply_window_button_style(m_cancel_button, ButtonStyle::Regular);
    m_cancel_button->SetMinSize(wxSize(FromDIP(96), FromDIP(32)));
    button_row->Add(m_cancel_button, 0, wxRIGHT, FromDIP(10));

    m_confirm_button = new Button(this, _L("确认"));
    apply_window_button_style(m_confirm_button, ButtonStyle::Confirm);
    m_confirm_button->SetMinSize(wxSize(FromDIP(96), FromDIP(32)));
    button_row->Add(m_confirm_button, 0);

    main_sizer->Add(button_row, 0, wxALL | wxEXPAND, FromDIP(20));

    SetSizerAndFit(main_sizer);
    CentreOnParent();
}

void GFDVerifyDialog::bind_events()
{
    m_confirm_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_confirm(); });
    m_cancel_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { cancel_request_and_close(); });
    m_code_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent& event) {
        on_confirm();
        event.Skip(false);
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { cancel_request_and_close(); });
}

void GFDVerifyDialog::on_confirm()
{
    if (m_request_active)
        return;
    const std::string code = trim_copy(into_u8(m_code_input->GetTextCtrl()->GetValue()));
    if (code.empty()) {
        set_tip(_L("请输入验证码"));
        return;
    }

    if (m_worker == nullptr || wxGetApp().app_config == nullptr) {
        set_tip(_L("验证码服务不可用，请稍后重试"));
        return;
    }

    const std::string request_environment = GFD::Config::current_environment_name(wxGetApp().app_config);
    const std::string verify_url          = GFD::Config::verify_url(wxGetApp().app_config);
    auto              result              = std::make_shared<VerifyAttemptResult>();

    json request_json;
    request_json["uuid"]    = m_uuid;
    request_json["tfaCode"] = code;
    const std::string request_body = request_json.dump();

    m_request_active = true;
    m_close_when_idle = false;
    set_verify_controls_enabled(false);
    set_tip(_L("正在校验，请稍候..."));

    const bool queued = queue_job(
        *m_worker,
        [result, verify_url, request_body, request_environment](Job::Ctl& ctl) {
            std::string body;
            unsigned    status = 0;
            if (!post_json_request(verify_url, request_body, ctl, body, status, result->error_message)) {
                if (result->error_message.empty())
                    result->error_message = extract_response_message(body, "验证码校验失败");
                return;
            }
            if (!ctl.was_canceled())
                parse_verify_response(body, request_environment, *result);
        },
        [this, result, request_environment](bool canceled, std::exception_ptr& eptr) {
            if (m_destroying) {
                eptr = nullptr;
                return;
            }
            m_request_active = false;

            if (eptr != nullptr) {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception& ex) {
                    result->error_message = ex.what();
                } catch (...) {
                    result->error_message = "验证码任务发生未知异常";
                }
                eptr = nullptr;
            }

            if (canceled || m_close_when_idle) {
                if (IsModal())
                    EndModal(wxID_CANCEL);
                return;
            }

            if (request_environment != GFD::Config::current_environment_name(wxGetApp().app_config)) {
                result->ok            = false;
                result->error_message = "云环境已切换，请重新校验";
            }

            if (!result->ok) {
                set_verify_controls_enabled(true);
                set_tip(from_u8(result->error_message.empty() ? "验证码校验失败" : result->error_message));
                return;
            }

            m_verify_token = std::move(result->verify_token);
            if (IsModal())
                EndModal(wxID_OK);
        });

    if (!queued) {
        m_request_active = false;
        set_verify_controls_enabled(true);
        set_tip(_L("无法启动验证码校验任务，请稍后重试"));
    }
}

void GFDVerifyDialog::cancel_request_and_close()
{
    if (!m_request_active) {
        if (IsModal())
            EndModal(wxID_CANCEL);
        else
            Hide();
        return;
    }

    m_close_when_idle = true;
    set_tip(_L("正在取消校验..."));
    m_cancel_button->Enable(false);
    m_worker->cancel_all();
}

void GFDVerifyDialog::set_verify_controls_enabled(bool enabled)
{
    if (m_code_input != nullptr)
        m_code_input->Enable(enabled);
    if (m_confirm_button != nullptr)
        m_confirm_button->Enable(enabled);
    if (m_cancel_button != nullptr)
        m_cancel_button->Enable(true);
}

bool GFDVerifyDialog::verify_login(const std::string& uuid, std::string& verify_token)
{
    m_uuid = uuid;
    m_verify_token.clear();
    BOOST_LOG_TRIVIAL(info) << "GFD verify dialog show"
                            << ", env=" << GFD::Config::current_environment_name(wxGetApp().app_config)
                            << ", uuid_empty=" << m_uuid.empty()
                            << ", parent_visible=" << (GetParent() != nullptr && GetParent()->IsShownOnScreen());
    const int modal_result = ShowModal();
    BOOST_LOG_TRIVIAL(info) << "GFD verify dialog closed"
                            << ", env=" << GFD::Config::current_environment_name(wxGetApp().app_config)
                            << ", modal_result=" << modal_result
                            << ", token_length=" << m_verify_token.size();
    if (modal_result != wxID_OK)
        return false;

    verify_token = m_verify_token;
    return !verify_token.empty();
}

void GFDVerifyDialog::set_tip(const wxString& text) { m_tip_label->SetLabel(text); }

}} // namespace Slic3r::GUI
