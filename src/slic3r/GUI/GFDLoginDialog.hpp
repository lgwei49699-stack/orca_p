#pragma once

#ifndef slic3r_GFDLoginDialog_hpp_
#define slic3r_GFDLoginDialog_hpp_

#include <string>

#include "wx/dialog.h"

class Button;
class CheckBox;
class TextInput;
class wxStaticText;

namespace Slic3r { namespace GUI {

class GFDVerifyDialog;

class GFDLoginDialog : public wxDialog
{
public:
    enum class LoginResult
    {
        Success,
        Failed,
        Cancelled
    };

    GFDLoginDialog();
    ~GFDLoginDialog() override;

    bool run();
    static LoginResult login_with_credentials(const std::string& username,
                                              const std::string& password,
                                              std::string&       error_message,
                                              bool               persist_credentials = false,
                                              bool               remember_credentials = false);
    LoginResult login_with_credentials_local(const std::string& username,
                                             const std::string& password,
                                             std::string&       error_message,
                                             bool               persist_credentials = false,
                                             bool               remember_credentials = false);

private:
    void build();
    void bind_events();
    void load_cached_credentials();
    void save_cached_credentials();

    void on_login(wxCommandEvent& event);
    void on_cancel(wxCommandEvent& event);

    bool validate_input();
    bool request_public_key(std::string& public_key, std::string& error_message);
    bool request_login(const std::string& email,
                       const std::string& password_encrypted,
                       std::string&       uuid,
                       std::string&       auth_token,
                       std::string&       error_message);
    bool request_verify(const std::string& uuid, const std::string& code, std::string& verify_token, std::string& error_message);

    static std::string rsa_encrypt_password(const std::string& password, const std::string& public_key_base64, std::string& error_message);

private:
    TextInput*    m_username_input{nullptr};
    TextInput*    m_password_input{nullptr};
    CheckBox*     m_remember_checkbox{nullptr};
    Button*       m_login_button{nullptr};
    Button*       m_cancel_button{nullptr};
    wxStaticText* m_tip_label{nullptr};
    wxStaticText* m_remember_label{nullptr};
};

class GFDVerifyDialog : public wxDialog
{
public:
    explicit GFDVerifyDialog(wxWindow* parent = nullptr);
    ~GFDVerifyDialog() override = default;

    bool verify_login(const std::string& uuid, std::string& verify_token);

private:
    void build();
    void bind_events();
    void set_tip(const wxString& text);
    void on_confirm();

private:
    std::string   m_uuid;
    std::string   m_verify_token;
    TextInput*    m_code_input{nullptr};
    Button*       m_confirm_button{nullptr};
    Button*       m_cancel_button{nullptr};
    wxStaticText* m_tip_label{nullptr};
};

}} // namespace Slic3r::GUI

#endif
