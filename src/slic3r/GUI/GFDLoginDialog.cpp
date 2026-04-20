#include "GFDLoginDialog.hpp"

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "MainFrame.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/TextInput.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/GFDConfig.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/beast/core/detail/base64.hpp>
#include <nlohmann/json.hpp>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/err.h>

#include <algorithm>
#include <regex>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <ctime>

namespace Slic3r { namespace GUI {

using json = nlohmann::json;

namespace {

constexpr long VERIFY_VALID_SECONDS = 24 * 60 * 60;

std::string trim_copy(const std::string& value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

long long now_ts() { return static_cast<long long>(std::time(nullptr)); }

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

bool post_json_request(const std::string& url, const std::string& request_body, std::string& body, std::string& error_message)
{
    bool ok = false;

    Http::post(url)
        .header("Content-Type", "application/json")
        .set_post_body(request_body)
        .on_complete([&](std::string response_body, unsigned) {
            body = std::move(response_body);
            ok   = true;
        })
        .on_error([&](std::string response_body, std::string error, unsigned) {
            body          = std::move(response_body);
            error_message = std::move(error);
            ok            = false;
        })
        .perform_sync();

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

void apply_window_button_style(Button* button, ButtonStyle style)
{
    if (button != nullptr)
        button->SetStyle(style, ButtonType::Window);
}

wxWindow* login_parent_window()
{
    if (wxGetApp().mainframe != nullptr)
        return static_cast<wxWindow*>(wxGetApp().mainframe);
    return wxTheApp != nullptr ? wxTheApp->GetTopWindow() : nullptr;
}

} // namespace

GFDLoginDialog::GFDLoginDialog()
    : wxDialog(login_parent_window(), wxID_ANY, _L("功夫豆Cura - 登录"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    build();
    bind_events();
    load_cached_credentials();
    wxGetApp().UpdateDlgDarkUI(this);
}

GFDLoginDialog::~GFDLoginDialog() = default;

bool GFDLoginDialog::run() { return ShowModal() == wxID_OK; }

void GFDLoginDialog::build()
{
    SetBackgroundColour(wxColour(240, 240, 240));
    SetMinSize(wxSize(FromDIP(350), FromDIP(280)));
    SetSize(wxSize(FromDIP(350), FromDIP(280)));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->AddSpacer(FromDIP(30));

    auto* username_row   = new wxBoxSizer(wxHORIZONTAL);
    auto* username_label = new wxStaticText(this, wxID_ANY, _L("账户"), wxDefaultPosition, wxDefaultSize, 0);
    username_label->SetFont(::Label::Body_13);
    username_label->SetMinSize(wxSize(FromDIP(34), -1));
    username_row->Add(username_label, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(26));

    m_username_input = new TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition,
                                     wxSize(FromDIP(205), FromDIP(34)), wxTE_PROCESS_ENTER);
    m_username_input->SetCornerRadius(FromDIP(6));
    username_row->Add(m_username_input, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(12));
    main_sizer->Add(username_row, 0, wxEXPAND);

    auto* password_row   = new wxBoxSizer(wxHORIZONTAL);
    auto* password_label = new wxStaticText(this, wxID_ANY, _L("密码"), wxDefaultPosition, wxDefaultSize, 0);
    password_label->SetFont(::Label::Body_13);
    password_label->SetMinSize(wxSize(FromDIP(34), -1));
    password_row->Add(password_label, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(26));

    m_password_input = new TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition,
                                     wxSize(FromDIP(205), FromDIP(34)), wxTE_PASSWORD | wxTE_PROCESS_ENTER);
    m_password_input->SetCornerRadius(FromDIP(6));
    password_row->Add(m_password_input, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(12));
    main_sizer->Add(password_row, 0, wxTOP | wxEXPAND, FromDIP(20));

    auto* remember_row  = new wxBoxSizer(wxHORIZONTAL);
    m_remember_checkbox = new CheckBox(this, wxID_ANY);
    remember_row->Add(m_remember_checkbox, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(31));
    m_remember_label = new wxStaticText(this, wxID_ANY, _L("记住账号密码"), wxDefaultPosition, wxDefaultSize, 0);
    m_remember_label->SetFont(::Label::Body_12);
    remember_row->Add(m_remember_label, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(8));
    remember_row->AddStretchSpacer(1);

    m_tip_label = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_tip_label->SetFont(::Label::Body_12);
    m_tip_label->SetForegroundColour(wxColour(255, 0, 0));
    remember_row->Add(m_tip_label, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(24));
    main_sizer->Add(remember_row, 0, wxTOP | wxEXPAND, FromDIP(18));

    auto* button_row = new wxBoxSizer(wxHORIZONTAL);
    button_row->AddStretchSpacer(1);

    m_cancel_button = new Button(this, _L("取消"));
    apply_window_button_style(m_cancel_button, ButtonStyle::Regular);
    m_cancel_button->SetMinSize(wxSize(FromDIP(58), FromDIP(32)));
    button_row->Add(m_cancel_button, 0, wxRIGHT, FromDIP(15));

    m_login_button = new Button(this, _L("登录"));
    apply_window_button_style(m_login_button, ButtonStyle::Confirm);
    m_login_button->SetMinSize(wxSize(FromDIP(56), FromDIP(32)));
    button_row->Add(m_login_button, 0, wxRIGHT, FromDIP(24));

    main_sizer->AddStretchSpacer(1);
    main_sizer->Add(button_row, 0, wxBOTTOM | wxEXPAND, FromDIP(34));

    SetSizer(main_sizer);
    Layout();
    Centre();
}

void GFDLoginDialog::bind_events()
{
    m_login_button->Bind(wxEVT_BUTTON, &GFDLoginDialog::on_login, this);
    m_cancel_button->Bind(wxEVT_BUTTON, &GFDLoginDialog::on_cancel, this);
    m_username_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, &GFDLoginDialog::on_login, this);
    m_password_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, &GFDLoginDialog::on_login, this);
}

void GFDLoginDialog::load_cached_credentials()
{
    if (wxGetApp().app_config == nullptr)
        return;

    m_username_input->GetTextCtrl()->SetValue(from_u8(GFD::Config::cached_username(wxGetApp().app_config)));
    m_password_input->GetTextCtrl()->SetValue(from_u8(GFD::Config::cached_password(wxGetApp().app_config)));
    m_remember_checkbox->SetValue(GFD::Config::remember_login(wxGetApp().app_config));
}

void GFDLoginDialog::save_cached_credentials()
{
    if (wxGetApp().app_config == nullptr)
        return;

    const bool remember = m_remember_checkbox->GetValue();
    GFD::Config::set_remember_login(wxGetApp().app_config, remember);
    if (remember) {
        GFD::Config::set_cached_username(wxGetApp().app_config, into_u8(m_username_input->GetTextCtrl()->GetValue()));
        GFD::Config::set_cached_password(wxGetApp().app_config, into_u8(m_password_input->GetTextCtrl()->GetValue()));
    } else {
        GFD::Config::set_cached_username(wxGetApp().app_config, "");
        GFD::Config::set_cached_password(wxGetApp().app_config, "");
    }
}

bool GFDLoginDialog::validate_input()
{
    const std::string username = trim_copy(into_u8(m_username_input->GetTextCtrl()->GetValue()));
    if (username.empty()) {
        m_tip_label->SetLabel(_L("请输入账户"));
        return false;
    }
    static const std::regex email_regex(R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)");
    if (!std::regex_match(username, email_regex)) {
        m_tip_label->SetLabel(_L("邮箱格式错误"));
        return false;
    }
    if (into_u8(m_password_input->GetTextCtrl()->GetValue()).empty()) {
        m_tip_label->SetLabel(_L("请输入密码"));
        return false;
    }
    m_tip_label->SetLabel(wxEmptyString);
    return true;
}

void GFDLoginDialog::on_login(wxCommandEvent&)
{
    if (!validate_input())
        return;

    const std::string email    = trim_copy(into_u8(m_username_input->GetTextCtrl()->GetValue()));
    const std::string password = into_u8(m_password_input->GetTextCtrl()->GetValue());

    std::string public_key;
    std::string error_message;
    if (!request_public_key(public_key, error_message)) {
        m_tip_label->SetLabel(from_u8(error_message));
        return;
    }

    const std::string encrypted_password = rsa_encrypt_password(password, public_key, error_message);
    if (encrypted_password.empty()) {
        m_tip_label->SetLabel(from_u8(error_message.empty() ? "密码加密失败" : error_message));
        return;
    }

    std::string uuid;
    if (!request_login(email, encrypted_password, uuid, error_message)) {
        m_tip_label->SetLabel(from_u8(error_message));
        return;
    }

    std::string verify_token = GFD::Config::verify_token(wxGetApp().app_config);
    if (!has_valid_verify_cache()) {
        GFDVerifyDialog verify_dialog;
        if (!verify_dialog.verify_login(uuid, verify_token)) {
            m_tip_label->SetLabel(_L("验证码校验已取消或失败"));
            return;
        }
        GFD::Config::set_verify_token(wxGetApp().app_config, verify_token);
        GFD::Config::set_verify_expire_ts(wxGetApp().app_config, std::to_string(now_ts() + VERIFY_VALID_SECONDS));
    }

    if (wxGetApp().app_config != nullptr) {
        GFD::Config::set_user_email(wxGetApp().app_config, email);
        GFD::Config::set_user_uuid(wxGetApp().app_config, uuid);
        GFD::Config::set_auth_token(wxGetApp().app_config, verify_token);
    }
    save_cached_credentials();
    if (wxGetApp().app_config != nullptr)
        wxGetApp().app_config->save();

    EndModal(wxID_OK);
}

void GFDLoginDialog::on_cancel(wxCommandEvent&) { EndModal(wxID_CANCEL); }

bool GFDLoginDialog::request_public_key(std::string& public_key, std::string& error_message)
{
    std::string body;
    unsigned    status = 0;
    bool        ok     = false;

    Http::get(GFD::Config::public_key_url(wxGetApp().app_config))
        .header("Content-Type", "application/json")
        .on_complete([&](std::string response_body, unsigned http_status) {
            body   = std::move(response_body);
            status = http_status;
            ok     = true;
        })
        .on_error([&](std::string response_body, std::string error, unsigned http_status) {
            body          = std::move(response_body);
            status        = http_status;
            error_message = error.empty() ? extract_response_message(body, "获取公钥失败") : error;
            ok            = false;
        })
        .perform_sync();

    if (!ok)
        return false;

    try {
        const json response = json::parse(body);
        if (response.contains("msg") && response["msg"].is_string() && response["msg"].get<std::string>() == "success") {
            public_key = response.value("data", std::string());
            return !public_key.empty();
        }
        error_message = extract_response_message(body, "获取公钥失败");
    } catch (...) {
        error_message = "公钥响应格式无效";
    }
    (void) status;
    return false;
}

bool GFDLoginDialog::request_login(const std::string& email,
                                   const std::string& password_encrypted,
                                   std::string&       uuid,
                                   std::string&       error_message)
{
    json request_json;
    request_json["email"]    = email;
    request_json["password"] = password_encrypted;

    std::string body;
    if (!post_json_request(GFD::Config::login_url(wxGetApp().app_config), request_json.dump(), body, error_message)) {
        if (error_message.empty())
            error_message = extract_response_message(body, "登录失败");
        return false;
    }

    try {
        const json response = json::parse(body);
        if (response.contains("msg") && response["msg"].is_string() && response["msg"].get<std::string>() == "success") {
            if (response.contains("data") && response["data"].is_object())
                uuid = response["data"].value("uuid", std::string());
            return !uuid.empty();
        }
        error_message = extract_response_message(body, "登录失败");
    } catch (...) {
        error_message = "登录响应格式无效";
    }

    return false;
}

bool GFDLoginDialog::request_verify(const std::string& uuid, const std::string& code, std::string& verify_token, std::string& error_message)
{
    json request_json;
    request_json["uuid"]    = uuid;
    request_json["tfaCode"] = code;

    std::string body;
    if (!post_json_request(GFD::Config::verify_url(wxGetApp().app_config), request_json.dump(), body, error_message)) {
        if (error_message.empty())
            error_message = extract_response_message(body, "验证码校验失败");
        return false;
    }

    try {
        const json response = json::parse(body);
        if (response.contains("msg") && response["msg"].is_string() && response["msg"].get<std::string>() == "success") {
            if (response.contains("data") && response["data"].is_object()) {
                verify_token = response["data"].value("token", std::string());
                if (verify_token.empty())
                    verify_token = response["data"].value("accessToken", std::string());
            }
            return !verify_token.empty();
        }
        error_message = extract_response_message(body, "验证码校验失败");
    } catch (...) {
        error_message = "验证码响应格式无效";
    }

    return false;
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

GFDVerifyDialog::GFDVerifyDialog()
    : wxDialog(login_parent_window(), wxID_ANY, _L("验证码校验"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    build();
    bind_events();
    wxGetApp().UpdateDlgDarkUI(this);
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

    main_sizer->AddStretchSpacer(1);
    main_sizer->Add(button_row, 0, wxALL | wxEXPAND, FromDIP(20));

    SetSizerAndFit(main_sizer);
    CentreOnParent();
}

void GFDVerifyDialog::bind_events()
{
    m_confirm_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_confirm(); });
    m_cancel_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
    m_code_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent& event) {
        on_confirm();
        event.Skip(false);
    });
}

void GFDVerifyDialog::on_confirm()
{
    const std::string code = trim_copy(into_u8(m_code_input->GetTextCtrl()->GetValue()));
    if (code.empty()) {
        set_tip(_L("请输入验证码"));
        return;
    }

    std::string error_message;
    std::string body;
    json        request_json;
    request_json["uuid"]    = m_uuid;
    request_json["tfaCode"] = code;
    if (!post_json_request(GFD::Config::verify_url(wxGetApp().app_config), request_json.dump(), body, error_message)) {
        if (error_message.empty())
            error_message = extract_response_message(body, "验证码校验失败");
        set_tip(from_u8(error_message));
        return;
    }

    try {
        const json response = json::parse(body);
        if (response.contains("msg") && response["msg"].is_string() && response["msg"].get<std::string>() == "success") {
            if (response.contains("data") && response["data"].is_object()) {
                m_verify_token = response["data"].value("token", std::string());
                if (m_verify_token.empty())
                    m_verify_token = response["data"].value("accessToken", std::string());
            }
        } else {
            error_message = extract_response_message(body, "验证码校验失败");
        }
    } catch (...) {
        error_message = "验证码响应格式无效";
    }

    if (m_verify_token.empty()) {
        set_tip(from_u8(error_message.empty() ? "验证码校验失败" : error_message));
        return;
    }

    EndModal(wxID_OK);
}

bool GFDVerifyDialog::verify_login(const std::string& uuid, std::string& verify_token)
{
    m_uuid = uuid;
    m_verify_token.clear();
    if (ShowModal() != wxID_OK)
        return false;

    verify_token = m_verify_token;
    return !verify_token.empty();
}

void GFDVerifyDialog::set_tip(const wxString& text) { m_tip_label->SetLabel(text); }

}} // namespace Slic3r::GUI
