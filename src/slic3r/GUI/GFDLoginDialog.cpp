#include "GFDLoginDialog.hpp"

#include "GFDAuthManager.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "I18N.hpp"
#include "Jobs/BoostThreadWorker.hpp"
#include "Jobs/PlaterWorker.hpp"
#include "Jobs/Worker.hpp"
#include "MainFrame.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/TextInput.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/GFDConfig.hpp"
#include "slic3r/Utils/Http.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <functional>
#include <initializer_list>
#include <memory>
#include <regex>
#include <utility>

#include <boost/beast/core/detail/base64.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/weakref.h>

namespace Slic3r { namespace GUI {

namespace {

using json = nlohmann::json;

constexpr long LOGIN_CONNECT_TIMEOUT_SECONDS = 10;
constexpr long LOGIN_REQUEST_TIMEOUT_SECONDS = 20;
constexpr int  SMS_COUNTDOWN_SECONDS         = 60;

std::string trim_copy(const std::string& value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string normalize_phone(std::string phone)
{
    phone.erase(std::remove_if(phone.begin(), phone.end(), [](unsigned char ch) { return std::isspace(ch) != 0 || ch == '-'; }),
                phone.end());
    if (phone.rfind("+86", 0) == 0)
        phone.erase(0, 3);
    else if (phone.rfind("0086", 0) == 0)
        phone.erase(0, 4);
    else if (phone.size() == 13 && phone.rfind("86", 0) == 0)
        phone.erase(0, 2);
    return phone;
}

std::string json_string(const json& object, const char* key)
{
    if (!object.is_object() || key == nullptr)
        return {};
    const auto it = object.find(key);
    if (it == object.end() || it->is_null())
        return {};
    if (it->is_string())
        return it->get<std::string>();
    if (it->is_number_integer())
        return std::to_string(it->get<long long>());
    if (it->is_number_unsigned())
        return std::to_string(it->get<unsigned long long>());
    if (it->is_number_float())
        return std::to_string(it->get<double>());
    return {};
}

std::string first_string(const json& object, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const std::string value = json_string(object, key);
        if (!value.empty())
            return value;
    }
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
        const std::string value = first_string(*current, keys);
        if (!value.empty())
            return value;
        current = child_object(*current, {"data", "result", "user", "member", "profile"});
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
    if (!response.is_object() || !response.contains("code"))
        return false;

    const json& code = response["code"];
    return (code.is_number_integer() && code.get<long long>() == 0) || (code.is_string() && code.get<std::string>() == "0");
}

bool get_json_request(const std::string& url, Job::Ctl& ctl, std::string& body, unsigned& status, std::string& error_message)
{
    bool ok                     = false;
    bool cancellation_requested = false;
    Http::get(url)
        .header("mango", "1")
        .header("Biz", "ZXB")
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
            error_message = error.empty() ? body : std::move(error);
            ok            = false;
        })
        .perform_sync();
    if (cancellation_requested && !ok)
        error_message = L("The operation was canceled.");
    return ok;
}

bool post_json_request(const std::string& url,
                       const std::string& request_body,
                       bool               include_mango,
                       Job::Ctl&          ctl,
                       std::string&       body,
                       unsigned&          status,
                       std::string&       error_message)
{
    bool ok                     = false;
    bool cancellation_requested = false;
    Http request                = Http::post(url);
    request.header("Content-Type", "application/json").header("Biz", "ZXB");
    if (include_mango)
        request.header("mango", "1");
    request.set_post_body(request_body)
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
            error_message = error.empty() ? body : std::move(error);
            ok            = false;
        })
        .perform_sync();
    if (cancellation_requested && !ok)
        error_message = L("The operation was canceled.");
    return ok;
}

struct CaptchaRequestResult
{
    bool        ok{false};
    std::string captcha_id;
    std::string background_image;
    std::string slide_image;
    int         slide_y{0};
    std::string error_message;
};

struct SmsRequestResult
{
    bool        ok{false};
    std::string error_message;
};

struct LoginRequestResult
{
    bool        ok{false};
    std::string token;
    std::string uuid;
    std::string nickname;
    std::string avatar;
    std::string error_message;
};

bool parse_captcha_response(const std::string& body, CaptchaRequestResult& result)
{
    try {
        const json response = json::parse(body);
        if (!response_succeeded(response)) {
            result.error_message = response_message(response, L("Failed to load the slider CAPTCHA."));
            return false;
        }
        const json* data = child_object(response, {"data", "result"});
        if (data == nullptr) {
            result.error_message = L("The slider CAPTCHA data is empty.");
            return false;
        }
        result.captcha_id         = first_string(*data, {"captchaId"});
        result.background_image   = first_string(*data, {"backgroundImage"});
        result.slide_image        = first_string(*data, {"slideImage"});
        const std::string slide_y = first_string(*data, {"slideY"});
        try {
            result.slide_y = slide_y.empty() ? 0 : std::stoi(slide_y);
        } catch (...) {
            result.slide_y = 0;
        }
        result.ok = !result.captcha_id.empty() && !result.background_image.empty() && !result.slide_image.empty();
        if (!result.ok)
            result.error_message = L("The slider CAPTCHA data is incomplete.");
        return result.ok;
    } catch (const std::exception&) {
        result.error_message = L("The slider CAPTCHA service returned an invalid response. Please try again later.");
        return false;
    }
}

bool parse_sms_response(const std::string& body, SmsRequestResult& result)
{
    try {
        const json response = json::parse(body);
        if (!response_succeeded(response)) {
            result.error_message = response_message(response, L("Failed to send the verification code."));
            return false;
        }
        result.ok = true;
        return true;
    } catch (const std::exception&) {
        result.error_message = L("The verification code service returned an invalid response. Please try again later.");
        return false;
    }
}

bool parse_login_response(const std::string& body, LoginRequestResult& result)
{
    try {
        const json response = json::parse(body);
        if (!response_succeeded(response)) {
            result.error_message = response_message(response, L("Login failed."));
            return false;
        }
        result.token = nested_string(response, {"accessToken", "access_token", "token", "loginToken", "jwt"});
        if (result.token.empty()) {
            result.error_message = L("The login service did not return valid credentials.");
            return false;
        }
        result.uuid     = nested_string(response, {"uuid", "userUuid", "userId", "uid", "id"});
        result.nickname = nested_string(response, {"nickname", "nickName", "userName", "name"});
        result.avatar   = nested_string(response, {"avatar", "avatarUrl", "headImg", "headImage"});
        result.ok       = true;
        return true;
    } catch (const std::exception&) {
        result.error_message = L("The login service returned an invalid response. Please try again later.");
        return false;
    }
}

wxWindow* login_parent_window(wxWindow* preferred_parent)
{
    if (preferred_parent != nullptr && preferred_parent->IsShownOnScreen())
        return preferred_parent;
    if (wxGetApp().mainframe != nullptr)
        return static_cast<wxWindow*>(wxGetApp().mainframe);
    return wxTheApp != nullptr ? wxTheApp->GetTopWindow() : nullptr;
}

void apply_window_button_style(Button* button, ButtonStyle style)
{
    if (button != nullptr)
        button->SetStyle(style, ButtonType::Choice);
}

wxImage decode_base64_png(std::string encoded)
{
    const auto comma = encoded.find(',');
    if (comma != std::string::npos && encoded.substr(0, comma).find("base64") != std::string::npos)
        encoded.erase(0, comma + 1);
    encoded.erase(std::remove_if(encoded.begin(), encoded.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), encoded.end());

    std::string decoded(boost::beast::detail::base64::decoded_size(encoded.size()), '\0');
    const auto  decoded_result = boost::beast::detail::base64::decode(decoded.data(), encoded.data(), encoded.size());
    decoded.resize(decoded_result.first);
    if (decoded.empty())
        return {};

    wxMemoryInputStream stream(decoded.data(), decoded.size());
    return wxImage(stream, wxBITMAP_TYPE_PNG);
}

class GFDCaptchaCanvas final : public wxPanel
{
public:
    GFDCaptchaCanvas(wxWindow* parent, const wxImage& background, const wxImage& slide, int slide_y)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, background.GetSize()), m_background(background), m_slide(slide), m_slide_y(slide_y)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(background.GetSize());
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(*wxWHITE_BRUSH);
            dc.Clear();
            if (m_background.IsOk())
                dc.DrawBitmap(m_background, 0, 0, false);
            if (m_slide.IsOk())
                dc.DrawBitmap(m_slide, m_position, std::max(0, m_slide_y), true);
        });
    }

    void set_position(int position)
    {
        m_position = std::clamp(position, 0, max_position());
        Refresh();
    }

    int max_position() const
    {
        return m_background.IsOk() && m_slide.IsOk() ? std::max(0, m_background.GetWidth() - m_slide.GetWidth()) : 0;
    }

private:
    wxBitmap m_background;
    wxBitmap m_slide;
    int      m_slide_y{0};
    int      m_position{0};
};

class GFDCaptchaSlider final : public wxPanel
{
public:
    using PositionCallback = std::function<void(int)>;

    GFDCaptchaSlider(wxWindow* parent, int max_position, PositionCallback on_position, PositionCallback on_release)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
        , m_max_position(std::max(1, max_position))
        , m_on_position(std::move(on_position))
        , m_on_release(std::move(on_release))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(*wxWHITE);
        SetMinSize(FromDIP(wxSize(310, 46)));
        SetCursor(wxCursor(wxCURSOR_HAND));

        Bind(wxEVT_PAINT, &GFDCaptchaSlider::on_paint, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
            if (!IsEnabled())
                return;
            m_dragging = true;
            if (!HasCapture())
                CaptureMouse();
            update_from_mouse(event.GetX());
            Refresh();
        });
        Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
            if (m_dragging && event.LeftIsDown())
                update_from_mouse(event.GetX());
        });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
            if (!m_dragging)
                return;
            update_from_mouse(event.GetX());
            m_dragging = false;
            if (HasCapture())
                ReleaseMouse();
            Refresh();
            if (m_value > 0 && m_on_release)
                m_on_release(m_value);
        });
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
            m_dragging = false;
            Refresh();
        });
    }

    bool Enable(bool enable = true) override
    {
        const bool changed = wxPanel::Enable(enable);
        SetCursor(wxCursor(enable ? wxCURSOR_HAND : wxCURSOR_ARROW));
        Refresh();
        return changed;
    }

    void reset()
    {
        m_value = 0;
        if (m_on_position)
            m_on_position(m_value);
        Refresh();
    }

private:
    void update_from_mouse(int mouse_x)
    {
        const int width          = GetClientSize().x;
        const int thumb_size     = FromDIP(32);
        const int edge_padding   = FromDIP(3);
        const int track_left     = thumb_size / 2 + edge_padding;
        const int available      = std::max(1, width - thumb_size - 2 * edge_padding);
        const double normalized  = std::clamp(static_cast<double>(mouse_x - track_left) / available, 0.0, 1.0);
        const int updated_value  = static_cast<int>(std::lround(normalized * m_max_position));
        if (updated_value == m_value)
            return;
        m_value = updated_value;
        if (m_on_position)
            m_on_position(m_value);
        Refresh();
    }

    void on_paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();

        wxGCDC    graphics(dc);
        const int width      = GetClientSize().x;
        const int height     = GetClientSize().y;
        const int thumb_size = FromDIP(32);
        const int track_h    = FromDIP(8);
        const int edge_padding = FromDIP(3);
        const int track_left = thumb_size / 2 + edge_padding;
        const int track_w    = std::max(1, width - thumb_size - 2 * edge_padding);
        const int center_y   = height / 2;
        const int center_x   = track_left + static_cast<int>(std::lround(static_cast<double>(track_w) * m_value / m_max_position));

        graphics.SetPen(*wxTRANSPARENT_PEN);
        graphics.SetBrush(wxBrush(wxColour(203, 213, 225)));
        graphics.DrawRoundedRectangle(track_left, center_y - track_h / 2, track_w, track_h, track_h / 2.0);

        if (center_x > track_left) {
            graphics.SetBrush(wxBrush(IsEnabled() ? wxColour(0, 150, 136) : wxColour(148, 163, 184)));
            graphics.DrawRoundedRectangle(track_left, center_y - track_h / 2, center_x - track_left, track_h, track_h / 2.0);
        }

        graphics.SetBrush(wxBrush(wxColour(148, 163, 184)));
        graphics.DrawEllipse(center_x - thumb_size / 2 + FromDIP(1), center_y - thumb_size / 2 + FromDIP(2), thumb_size, thumb_size);

        const wxColour accent = IsEnabled() ? (m_dragging ? wxColour(0, 121, 107) : wxColour(0, 150, 136)) : wxColour(148, 163, 184);
        graphics.SetPen(wxPen(accent, FromDIP(2)));
        graphics.SetBrush(*wxWHITE_BRUSH);
        graphics.DrawEllipse(center_x - thumb_size / 2, center_y - thumb_size / 2, thumb_size, thumb_size);

        const int arrow_offset = FromDIP(4);
        const int arrow_half_h = FromDIP(4);
        graphics.SetPen(wxPen(accent, FromDIP(2), wxPENSTYLE_SOLID));
        for (int offset : {-arrow_offset, arrow_offset}) {
            const int x = center_x + offset;
            graphics.DrawLine(x - FromDIP(2), center_y - arrow_half_h, x + FromDIP(2), center_y);
            graphics.DrawLine(x + FromDIP(2), center_y, x - FromDIP(2), center_y + arrow_half_h);
        }
    }

private:
    int              m_max_position{1};
    int              m_value{0};
    bool             m_dragging{false};
    PositionCallback m_on_position;
    PositionCallback m_on_release;
};

class GFDSliderCaptchaDialog final : public wxDialog
{
public:
    GFDSliderCaptchaDialog(wxWindow* parent, const CaptchaRequestResult& challenge, std::string phone)
        : wxDialog(parent, wxID_ANY, _L("Security Verification"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
        , m_phone(std::move(phone))
        , m_captcha_id(challenge.captcha_id)
    {
        auto worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(this, std::shared_ptr<ProgressIndicator>{},
                                                                        "gfd_slider_captcha_worker");
        worker->set_busy_cursor_enabled(false);
        m_worker = std::move(worker);

        SetBackgroundColour(*wxWHITE);
        SetMinSize(FromDIP(wxSize(390, 300)));

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        auto* title = new wxStaticText(this, wxID_ANY, _L("Drag the slider to complete verification"));
        title->SetFont(Label::Body_14);
        sizer->Add(title, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(20));

        m_tip = new wxStaticText(this, wxID_ANY, _L("Drag the slider below to align the puzzle piece with the gap."));
        m_tip->SetFont(Label::Body_12);
        m_tip->SetForegroundColour(wxColour(107, 114, 128));
        sizer->Add(m_tip, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, FromDIP(8));

        wxImage background = decode_base64_png(challenge.background_image);
        wxImage slide      = decode_base64_png(challenge.slide_image);
        if (!background.IsOk() || !slide.IsOk()) {
            m_valid = false;
            m_tip->SetLabel(_L("The slider CAPTCHA image is invalid. Please request a new one."));
            m_tip->SetForegroundColour(wxColour(220, 38, 38));
            // wxBitmap requires a valid source image. Keep a harmless placeholder
            // so a malformed server image produces an error state, not an assert.
            background.Create(310, 155);
            background.SetRGB(wxRect(0, 0, 310, 155), 245, 247, 250);
            slide.Create(50, 50);
            slide.SetRGB(wxRect(0, 0, 50, 50), 229, 231, 235);
        }

        m_canvas = new GFDCaptchaCanvas(this, background, slide, challenge.slide_y);
        sizer->Add(m_canvas, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, FromDIP(24));

        m_slider = new GFDCaptchaSlider(
            this,
            m_canvas->max_position(),
            [this](int position) {
                m_position = position;
                m_canvas->set_position(position);
            },
            [this](int position) { start_verification(position); });
        m_slider->Enable(m_valid);
        sizer->Add(m_slider, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(12));
        sizer->AddSpacer(FromDIP(16));
        SetSizerAndFit(sizer);
        CentreOnParent();

        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
            if (!m_request_active) {
                EndModal(wxID_CANCEL);
                return;
            }
            m_close_when_idle = true;
            set_tip(_L("Canceling login..."), false);
            if (m_worker != nullptr)
                m_worker->cancel_all();
        });
    }

    ~GFDSliderCaptchaDialog() override
    {
        m_destroying = true;
        if (m_worker != nullptr) {
            m_worker->cancel_all();
            m_worker->wait_for_idle();
        }
    }

    bool run()
    {
        return m_valid && ShowModal() == wxID_OK && m_verified;
    }

private:
    void set_tip(const wxString& text, bool is_error)
    {
        m_tip->SetLabel(text);
        m_tip->SetForegroundColour(is_error ? wxColour(220, 38, 38) : wxColour(0, 150, 136));
        m_tip->Wrap(FromDIP(310));
        Layout();
    }

    void reset_after_failure(const wxString& error)
    {
        set_tip(error, true);
        m_position = 0;
        m_slider->reset();
        m_slider->Enable(m_valid);
    }

    void start_verification(int position)
    {
        if (!m_valid || m_request_active || position <= 0 || m_worker == nullptr || wxGetApp().app_config == nullptr)
            return;

        auto result = std::make_shared<SmsRequestResult>();
        json request;
        request["captchaId"]     = m_captcha_id;
        request["slidePosition"] = std::to_string(position) + ".0";
        request["phone"]         = m_phone;
        const std::string url     = GFD::Config::sms_send_url(wxGetApp().app_config);
        const std::string body    = request.dump();

        m_request_active = true;
        m_slider->Enable(false);
        set_tip(_L("Sending verification code..."), false);

        const bool queued = queue_job(
            *m_worker,
            [result, url, body](Job::Ctl& ctl) {
                std::string response_body;
                unsigned    status = 0;
                if (!post_json_request(url, body, false, ctl, response_body, status, result->error_message)) {
                    if (result->error_message.empty())
                        result->error_message = L("Failed to send the verification code.");
                    return;
                }
                if (!ctl.was_canceled())
                    parse_sms_response(response_body, *result);
            },
            [this, result](bool canceled, std::exception_ptr& eptr) {
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
                        result->error_message = L("An error occurred while sending the verification code.");
                    }
                    eptr = nullptr;
                }

                m_request_active = false;
                if (canceled || m_close_when_idle) {
                    if (IsModal())
                        EndModal(wxID_CANCEL);
                    return;
                }
                if (!result->ok) {
                    reset_after_failure(_L(result->error_message.empty() ? L("Verification failed.") : result->error_message));
                    return;
                }

                m_verified = true;
                if (IsModal())
                    EndModal(wxID_OK);
            });

        if (!queued) {
            m_request_active = false;
            reset_after_failure(_L("Unable to send the verification code. Please try again later."));
        }
    }

private:
    GFDCaptchaCanvas*       m_canvas{nullptr};
    GFDCaptchaSlider*       m_slider{nullptr};
    wxStaticText*           m_tip{nullptr};
    std::unique_ptr<Worker> m_worker;
    std::string             m_phone;
    std::string             m_captcha_id;
    int                     m_position{0};
    bool                    m_valid{true};
    bool                    m_verified{false};
    bool                    m_request_active{false};
    bool                    m_close_when_idle{false};
    bool                    m_destroying{false};
};

} // namespace

GFDLoginDialog::GFDLoginDialog(wxWindow* parent, bool use_fallback_parent)
    : wxDialog(use_fallback_parent ? login_parent_window(parent) : parent,
               wxID_ANY,
               _L("WiseBeginner Slicer") + " - " + _L("User Login"),
               wxDefaultPosition,
               wxDefaultSize,
               wxCAPTION | wxCLOSE_BOX)
    , m_countdown_timer(this)
{
    auto worker = std::make_unique<PlaterWorker<BoostThreadWorker>>(this, std::shared_ptr<ProgressIndicator>{}, "gfd_user_login_worker");
    worker->set_busy_cursor_enabled(false);
    m_worker = std::move(worker);

    build();
    bind_events();
    load_cached_phone();
    wxGetApp().UpdateDlgDarkUI(this);
}

GFDLoginDialog::~GFDLoginDialog()
{
    m_destroying = true;
    m_countdown_timer.Stop();
    if (m_worker != nullptr) {
        m_worker->cancel_all();
        m_worker->wait_for_idle();
    }
}

bool GFDLoginDialog::run()
{
    dialogStack.push_front(this);
    const int  result = ShowModal();
    const auto iter   = std::find(dialogStack.begin(), dialogStack.end(), this);
    if (iter != dialogStack.end())
        dialogStack.erase(iter);
    return result == wxID_OK;
}

void GFDLoginDialog::build()
{
    SetBackgroundColour(wxColour(245, 247, 250));
    SetMinSize(FromDIP(wxSize(520, 500)));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);
    auto* title      = new wxStaticText(this, wxID_ANY, _L("Sign in to your WiseBeginner account"));
    title->SetFont(Label::Head_20);
    main_sizer->Add(title, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(34));

    auto* subtitle = new wxStaticText(this, wxID_ANY, _L("Sign in to sync read-only parameters and send print jobs to your printers."));
    subtitle->SetFont(Label::Body_12);
    subtitle->SetForegroundColour(wxColour(107, 114, 128));
    main_sizer->Add(subtitle, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(8));

    auto* card = new wxPanel(this, wxID_ANY);
    card->SetBackgroundColour(*wxWHITE);
    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    auto* card_title = new wxStaticText(card, wxID_ANY, _L("Sign in with an SMS verification code"));
    card_title->SetFont(Label::Body_14);
    card_sizer->Add(card_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(24));

    if (wxGetApp().get_mode() == comDevelop) {
        auto* environment_row   = new wxBoxSizer(wxHORIZONTAL);
        auto* environment_label = new wxStaticText(card, wxID_ANY, _L("Network Environment"));
        environment_label->SetFont(Label::Body_12);
        environment_row->Add(environment_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

        wxArrayString environments;
        environments.Add(_L("Production Environment"));
        environments.Add(_L("Test Environment"));
        m_environment_choice = new wxChoice(card, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(230, 32)), environments);
        const bool use_test_environment =
            GFD::Config::current_environment_name(wxGetApp().app_config) == GFD::Config::ENV_QA;
        m_environment_choice->SetSelection(use_test_environment ? 1 : 0);
        environment_row->Add(m_environment_choice, 1, wxALIGN_CENTER_VERTICAL);
        card_sizer->Add(environment_row, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(24));
    }

    auto* phone_label = new wxStaticText(card, wxID_ANY, _L("Phone Number"));
    phone_label->SetFont(Label::Body_12);
    card_sizer->Add(phone_label, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(24));

    auto* phone_row    = new wxBoxSizer(wxHORIZONTAL);
    auto* country_code = new wxStaticText(card, wxID_ANY, "+86");
    country_code->SetFont(Label::Body_13);
    country_code->SetMinSize(wxSize(FromDIP(42), country_code->GetBestSize().y));
    phone_row->Add(country_code, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_phone_input = new TextInput(card, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(300, 36)),
                                  wxTE_PROCESS_ENTER);
    m_phone_input->SetCornerRadius(FromDIP(6));
    m_phone_input->GetTextCtrl()->SetHint(_L("Enter your phone number"));
    phone_row->Add(m_phone_input, 1, wxALIGN_CENTER_VERTICAL);
    card_sizer->Add(phone_row, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(24));

    auto* code_label = new wxStaticText(card, wxID_ANY, _L("SMS Verification Code"));
    code_label->SetFont(Label::Body_12);
    card_sizer->Add(code_label, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(24));

    auto* code_row = new wxBoxSizer(wxHORIZONTAL);
    m_code_input   = new TextInput(card, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(210, 36)),
                                   wxTE_PROCESS_ENTER);
    m_code_input->SetCornerRadius(FromDIP(6));
    m_code_input->GetTextCtrl()->SetHint(_L("Enter the verification code"));
    code_row->Add(m_code_input, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    m_send_code_button = new Button(card, _L("Get Verification Code"));
    apply_window_button_style(m_send_code_button, ButtonStyle::Regular);
    m_send_code_button->SetMinSize(FromDIP(wxSize(122, 36)));
    m_send_code_button->SetCornerRadius(FromDIP(6));
    code_row->Add(m_send_code_button, 0, wxALIGN_CENTER_VERTICAL);
    card_sizer->Add(code_row, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(24));

    m_tip_label = new wxStaticText(card, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(354, 34)));
    m_tip_label->SetFont(Label::Body_12);
    m_tip_label->Wrap(FromDIP(354));
    card_sizer->Add(m_tip_label, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(24));

    m_login_button = new Button(card, _L("Log In"));
    apply_window_button_style(m_login_button, ButtonStyle::Confirm);
    m_login_button->SetMinSize(FromDIP(wxSize(354, 40)));
    m_login_button->SetCornerRadius(FromDIP(6));
    card_sizer->Add(m_login_button, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM | wxEXPAND, FromDIP(24));

    card->SetSizer(card_sizer);
    main_sizer->Add(card, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, FromDIP(28));

    SetSizerAndFit(main_sizer);
    if (GetSize().x < FromDIP(520))
        SetSize(FromDIP(wxSize(520, GetSize().y)));
    CentreOnParent();
}

void GFDLoginDialog::bind_events()
{
    if (m_environment_choice != nullptr)
        m_environment_choice->Bind(wxEVT_CHOICE, &GFDLoginDialog::on_environment_changed, this);
    m_send_code_button->Bind(wxEVT_BUTTON, &GFDLoginDialog::on_send_code, this);
    m_login_button->Bind(wxEVT_BUTTON, &GFDLoginDialog::on_login, this);
    m_phone_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, &GFDLoginDialog::on_send_code, this);
    m_code_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, &GFDLoginDialog::on_login, this);
    Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent&) {
            if (m_countdown_seconds > 0)
                --m_countdown_seconds;
            update_countdown_label();
        },
        m_countdown_timer.GetId());
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { cancel_request_and_close(); });
}

void GFDLoginDialog::on_environment_changed(wxCommandEvent& event)
{
    AppConfig* config = wxGetApp().app_config;
    if (config == nullptr || m_request_active || m_environment_choice == nullptr)
        return;

    const std::string current_environment = GFD::Config::current_environment_name(config);
    const std::string selected_environment =
        event.GetSelection() == 1 ? GFD::Config::ENV_QA : GFD::Config::ENV_PRODUCTION;
    if (selected_environment == current_environment)
        return;

    GFD::Config::set_environment(config, selected_environment);
    GFDAuthManager::logout(config, false);

    m_countdown_timer.Stop();
    m_countdown_seconds = 0;
    if (m_code_input != nullptr)
        m_code_input->GetTextCtrl()->Clear();
    update_countdown_label();
    set_tip(selected_environment == GFD::Config::ENV_QA ? _L("Switched to the test environment.")
                                                         : _L("Switched to the production environment."),
            false);

    BOOST_LOG_TRIVIAL(info) << "GFD environment switched from login dialog"
                            << ", from=" << current_environment << ", to=" << selected_environment;
}

void GFDLoginDialog::load_cached_phone()
{
    if (wxGetApp().app_config == nullptr || m_phone_input == nullptr)
        return;
    std::string phone = GFD::Config::user_phone(wxGetApp().app_config);
    if (phone.empty())
        phone = GFD::Config::cached_username(wxGetApp().app_config);
    m_phone_input->GetTextCtrl()->SetValue(from_u8(normalize_phone(phone)));
}

bool GFDLoginDialog::validate_phone(std::string& phone)
{
    phone = normalize_phone(into_u8(m_phone_input->GetTextCtrl()->GetValue()));
    static const std::regex phone_regex(R"(^1[3-9][0-9]{9}$)");
    if (!std::regex_match(phone, phone_regex)) {
        set_tip(_L("Enter a valid mainland China phone number."));
        return false;
    }
    return true;
}

void GFDLoginDialog::on_send_code(wxCommandEvent&)
{
    if (m_request_active || m_countdown_seconds > 0)
        return;

    std::string phone;
    if (!validate_phone(phone))
        return;

    start_captcha_request(phone);
}

void GFDLoginDialog::on_login(wxCommandEvent&)
{
    if (m_request_active)
        return;

    std::string phone;
    if (!validate_phone(phone))
        return;
    const std::string code = trim_copy(into_u8(m_code_input->GetTextCtrl()->GetValue()));
    if (code.size() < 4 || code.size() > 8 ||
        !std::all_of(code.begin(), code.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        set_tip(_L("Enter a valid SMS verification code."));
        return;
    }
    start_login_request(phone, code);
}

void GFDLoginDialog::cancel_request_and_close()
{
    if (!m_request_active) {
        if (IsModal())
            EndModal(wxID_CANCEL);
        else
            Hide();
        return;
    }
    m_close_when_idle = true;
    set_tip(_L("Canceling login..."), false);
    if (m_worker != nullptr)
        m_worker->cancel_all();
}

void GFDLoginDialog::set_controls_enabled(bool enabled)
{
    m_phone_input->Enable(enabled);
    m_code_input->Enable(enabled);
    if (m_environment_choice != nullptr)
        m_environment_choice->Enable(enabled);
    m_login_button->Enable(enabled);
    m_send_code_button->Enable(enabled && m_countdown_seconds == 0);
}

void GFDLoginDialog::set_tip(const wxString& text, bool is_error)
{
    m_tip_label->SetLabel(text);
    m_tip_label->SetForegroundColour(is_error ? wxColour(220, 38, 38) : wxColour(0, 150, 136));
    m_tip_label->Wrap(FromDIP(354));
    Layout();
}

void GFDLoginDialog::start_countdown()
{
    m_countdown_seconds = SMS_COUNTDOWN_SECONDS;
    if (!m_countdown_timer.IsRunning())
        m_countdown_timer.Start(1000);
    update_countdown_label();
}

void GFDLoginDialog::update_countdown_label()
{
    if (m_countdown_seconds <= 0) {
        m_countdown_seconds = 0;
        m_countdown_timer.Stop();
        m_send_code_button->SetLabel(_L("Get Verification Code"));
        m_send_code_button->Enable(!m_request_active);
        return;
    }
    m_send_code_button->SetLabel(wxString::Format(_L("Try again in %d seconds"), m_countdown_seconds));
    m_send_code_button->Enable(false);
}

bool GFDLoginDialog::start_captcha_request(const std::string& phone)
{
    if (m_worker == nullptr || wxGetApp().app_config == nullptr)
        return false;

    auto              result = std::make_shared<CaptchaRequestResult>();
    const std::string url    = GFD::Config::captcha_generate_url(wxGetApp().app_config);
    m_request_active         = true;
    set_controls_enabled(false);
    set_tip(_L("Loading slider CAPTCHA..."), false);

    const bool queued = queue_job(
        *m_worker,
        [result, url](Job::Ctl& ctl) {
            std::string body;
            unsigned    status = 0;
            if (!get_json_request(url, ctl, body, status, result->error_message)) {
                if (result->error_message.empty())
                    result->error_message = L("Failed to load the slider CAPTCHA.");
                return;
            }
            if (!ctl.was_canceled())
                parse_captcha_response(body, *result);
        },
        [this, result, phone](bool canceled, std::exception_ptr& eptr) {
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
                    result->error_message = L("An error occurred while loading the slider CAPTCHA.");
                }
                eptr = nullptr;
            }

            m_request_active = false;
            if (canceled || m_close_when_idle) {
                if (IsModal())
                    EndModal(wxID_CANCEL);
                return;
            }
            set_controls_enabled(true);
            if (!result->ok) {
                set_tip(_L(result->error_message.empty() ? L("Failed to load the slider CAPTCHA.") : result->error_message));
                return;
            }

            // Leave the CAPTCHA-loading worker's completion callback before
            // entering another modal loop. The slider dialog owns a separate
            // worker for verification and SMS delivery.
            const wxWeakRef<GFDLoginDialog> dialog(this);
            CallAfter([dialog, result, phone]() {
                GFDLoginDialog* const login_dialog = dialog.get();
                if (login_dialog == nullptr || login_dialog->m_destroying || login_dialog->m_close_when_idle)
                    return;

                GFDSliderCaptchaDialog slider_dialog(login_dialog, *result, phone);
                if (!slider_dialog.run()) {
                    login_dialog->set_tip(_L("Complete the slider CAPTCHA first."));
                    return;
                }

                login_dialog->start_countdown();
                login_dialog->set_tip(_L("The verification code has been sent. Please check your messages."), false);
                login_dialog->m_code_input->GetTextCtrl()->SetFocus();
            });
        });

    if (!queued) {
        m_request_active = false;
        set_controls_enabled(true);
        set_tip(_L("Unable to start the slider CAPTCHA request. Please try again later."));
    }
    return queued;
}

bool GFDLoginDialog::start_login_request(const std::string& phone, const std::string& code)
{
    if (m_worker == nullptr || wxGetApp().app_config == nullptr)
        return false;

    auto result = std::make_shared<LoginRequestResult>();
    json request;
    request["phone"]       = phone;
    request["code"]        = code;
    const std::string url  = GFD::Config::sms_login_url(wxGetApp().app_config);
    const std::string body = request.dump();

    m_request_active = true;
    set_controls_enabled(false);
    set_tip(_L("Logging in..."), false);

    const bool queued = queue_job(
        *m_worker,
        [result, url, body](Job::Ctl& ctl) {
            std::string response_body;
            unsigned    status = 0;
            if (!post_json_request(url, body, true, ctl, response_body, status, result->error_message)) {
                if (result->error_message.empty())
                    result->error_message = L("Login failed.");
                return;
            }
            if (!ctl.was_canceled())
                parse_login_response(response_body, *result);
        },
        [this, result, phone](bool canceled, std::exception_ptr& eptr) {
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
                    result->error_message = L("An error occurred while logging in.");
                }
                eptr = nullptr;
            }

            m_request_active = false;
            if (canceled || m_close_when_idle) {
                if (IsModal())
                    EndModal(wxID_CANCEL);
                return;
            }
            if (!result->ok) {
                set_controls_enabled(true);
                set_tip(_L(result->error_message.empty() ? L("Login failed.") : result->error_message));
                return;
            }

            AppConfig* config = wxGetApp().app_config;
            if (GFD::Config::parameter_sync_token(config).empty() && GFD::Config::auth_mode(config) != GFD::Config::AUTH_MODE_USER_SMS) {
                const std::string legacy_token = trim_copy(GFD::Config::auth_token(config));
                if (!legacy_token.empty())
                    GFD::Config::set_parameter_sync_token(config, legacy_token);
            }
            GFD::Config::clear_verify_cache(config);
            GFD::Config::set_auth_token(config, result->token);
            GFD::Config::set_auth_mode(config, GFD::Config::AUTH_MODE_USER_SMS);
            GFD::Config::set_user_email(config, "");
            GFD::Config::set_user_uuid(config, result->uuid);
            GFD::Config::set_user_phone(config, phone);
            GFD::Config::set_user_nickname(config, result->nickname.empty() ? phone : result->nickname);
            GFD::Config::set_user_avatar(config, result->avatar);
            GFD::Config::set_cached_username(config, phone);
            GFD::Config::set_cached_password(config, "");
            GFD::Config::set_remember_login(config, false);
            config->save();

            if (IsModal())
                EndModal(wxID_OK);
        });

    if (!queued) {
        m_request_active = false;
        set_controls_enabled(true);
        set_tip(_L("Unable to start the login request. Please try again later."));
    }
    return queued;
}

}} // namespace Slic3r::GUI
