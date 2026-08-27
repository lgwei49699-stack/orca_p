#pragma once

#ifndef slic3r_GFDLoginDialog_hpp_
#define slic3r_GFDLoginDialog_hpp_

#include <functional>
#include <memory>
#include <string>

#include "wx/dialog.h"

class Button;
class CheckBox;
class ComboBox;
class TextInput;
class wxStaticText;

namespace Slic3r { namespace GUI {

class GFDVerifyDialog;
class RadioBox;
class Worker;

class GFDLoginDialog : public wxDialog
{
public:
    enum class LoginResult
    {
        Success,
        Failed,
        Cancelled
    };
    using LoginFinishedFn = std::function<void(LoginResult, std::string)>;

    explicit GFDLoginDialog(wxWindow* parent = nullptr, bool use_fallback_parent = true);
    ~GFDLoginDialog() override;

    bool               run();
    static LoginResult login_with_credentials(wxWindow*          parent,
                                              const std::string& username,
                                              const std::string& password,
                                              std::string&       error_message,
                                              bool               persist_credentials  = false,
                                              bool               remember_credentials = false);
    static LoginResult login_with_credentials(const std::string& username,
                                              const std::string& password,
                                              std::string&       error_message,
                                              bool               persist_credentials  = false,
                                              bool               remember_credentials = false);
    static bool        login_with_credentials_async(const std::string& username,
                                                    const std::string& password,
                                                    LoginFinishedFn    finished,
                                                    bool               persist_credentials  = false,
                                                    bool               remember_credentials = false);
    LoginResult        login_with_credentials_local(const std::string& username,
                                                    const std::string& password,
                                                    std::string&       error_message,
                                                    bool               persist_credentials  = false,
                                                    bool               remember_credentials = false);

private:
    void build();
    void bind_events();
    void load_cached_credentials();
    void load_environment_selection();
    void load_saved_credentials_for_selected_environment();
    void fill_saved_password(const std::string& username);
    void save_cached_credentials();
    void select_environment(const std::string& environment);
    std::string selected_environment() const;
    bool apply_selected_environment();

    void on_login(wxCommandEvent& event);
    void on_cancel(wxCommandEvent& event);
    void cancel_request_and_close();
    void set_login_controls_enabled(bool enabled);
    bool prepare_login_attempt(const std::string& username,
                               const std::string& password,
                               bool               persist_credentials,
                               bool               remember_credentials);
    bool start_login_attempt(const std::string& email,
                             const std::string& password,
                             bool               persist_credentials,
                             bool               remember_credentials);
    void finish_async_login();

    bool validate_input();

    static std::string rsa_encrypt_password(const std::string& password, const std::string& public_key_base64, std::string& error_message);

private:
    ComboBox*     m_username_input{nullptr};
    TextInput*    m_password_input{nullptr};
    RadioBox*     m_qa_environment_radio{nullptr};
    RadioBox*     m_production_environment_radio{nullptr};
    CheckBox*     m_remember_checkbox{nullptr};
    Button*       m_login_button{nullptr};
    Button*       m_cancel_button{nullptr};
    wxStaticText* m_tip_label{nullptr};
    wxStaticText* m_remember_label{nullptr};
    std::unique_ptr<Worker> m_worker;
    bool                    m_request_active{false};
    bool                    m_close_when_idle{false};
    bool                    m_destroying{false};
    LoginResult             m_login_result{LoginResult::Cancelled};
    std::string             m_last_error;
    LoginFinishedFn         m_login_finished;
};

class GFDVerifyDialog : public wxDialog
{
public:
    explicit GFDVerifyDialog(wxWindow* parent = nullptr);
    ~GFDVerifyDialog() override;

    bool verify_login(const std::string& uuid, std::string& verify_token);

private:
    void build();
    void bind_events();
    void set_tip(const wxString& text);
    void on_confirm();
    void cancel_request_and_close();
    void set_verify_controls_enabled(bool enabled);

private:
    std::string   m_uuid;
    std::string   m_verify_token;
    TextInput*    m_code_input{nullptr};
    Button*       m_confirm_button{nullptr};
    Button*       m_cancel_button{nullptr};
    wxStaticText* m_tip_label{nullptr};
    std::unique_ptr<Worker> m_worker;
    bool                    m_request_active{false};
    bool                    m_close_when_idle{false};
    bool                    m_destroying{false};
};

}} // namespace Slic3r::GUI

#endif
