#pragma once

#ifndef slic3r_GFDLoginDialog_hpp_
#define slic3r_GFDLoginDialog_hpp_

#include <exception>
#include <memory>
#include <string>

#include <wx/dialog.h>
#include <wx/timer.h>

class Button;
class TextInput;
class wxChoice;
class wxStaticText;

namespace Slic3r { namespace GUI {

class Worker;

// Consumer-edition login. Password and middle-platform two-factor login are
// deliberately not exposed here: the only supported flow is phone + SMS code,
// gated by a slider verification before an SMS can be requested.
class GFDLoginDialog : public wxDialog
{
public:
    explicit GFDLoginDialog(wxWindow* parent = nullptr, bool use_fallback_parent = true);
    ~GFDLoginDialog() override;

    bool run();

private:
    void build();
    void bind_events();
    void load_cached_phone();

    void on_send_code(wxCommandEvent& event);
    void on_login(wxCommandEvent& event);
    void on_environment_changed(wxCommandEvent& event);
    void cancel_request_and_close();
    void set_controls_enabled(bool enabled);
    void set_tip(const wxString& text, bool is_error = true);
    void start_countdown();
    void update_countdown_label();

    bool validate_phone(std::string& phone);
    bool start_captcha_request(const std::string& phone);
    bool start_login_request(const std::string& phone, const std::string& code);

private:
    TextInput*    m_phone_input{nullptr};
    TextInput*    m_code_input{nullptr};
    Button*       m_send_code_button{nullptr};
    Button*       m_login_button{nullptr};
    wxChoice*     m_environment_choice{nullptr};
    wxStaticText* m_tip_label{nullptr};

    std::unique_ptr<Worker> m_worker;
    wxTimer                 m_countdown_timer;
    int                     m_countdown_seconds{0};
    bool                    m_request_active{false};
    bool                    m_close_when_idle{false};
    bool                    m_destroying{false};
};

}} // namespace Slic3r::GUI

#endif
