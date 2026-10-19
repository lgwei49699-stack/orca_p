#include "GFDCloudConfigDialog.hpp"

#include "I18N.hpp"
#include <algorithm>
#include <wx/gauge.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace Slic3r::GUI {

GFDCloudConfigDialog::GFDCloudConfigDialog(wxWindow* parent, const wxString& title)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX), m_timer(this)
{
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent&) {
            if (m_busy)
                m_gauge->Pulse();
        },
        m_timer.GetId());
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (m_busy && event.CanVeto())
            event.Veto();
        else
            event.Skip();
    });
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE) {
            cancel();
        } else {
            event.Skip();
        }
    });
}

GFDCloudConfigDialog::~GFDCloudConfigDialog()
{
    *m_alive = false;
    m_timer.Stop();
}

void GFDCloudConfigDialog::add_progress_footer(wxBoxSizer* sizer)
{
    m_progress_panel = new wxPanel(this);
    auto* content    = new wxBoxSizer(wxVERTICAL);
    m_status         = new wxStaticText(m_progress_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                        wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    m_status->SetFont(GetFont().Smaller());
    m_status->SetMinSize(wxSize(-1, m_status->GetCharHeight()));
    content->Add(m_status, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    m_gauge = new wxGauge(m_progress_panel, wxID_ANY, 100, wxDefaultPosition, FromDIP(wxSize(200, 4)));
    content->Add(m_gauge, 0, wxALIGN_CENTER_HORIZONTAL);
    m_progress_panel->SetSizer(content);
    sizer->Add(m_progress_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxRESERVE_SPACE_EVEN_IF_HIDDEN, FromDIP(24));
    m_progress_panel->Hide();
}

void GFDCloudConfigDialog::cancel()
{
    if (!m_busy)
        EndModal(wxID_CANCEL);
}

void GFDCloudConfigDialog::submit()
{
    if (m_busy || !m_submit)
        return;
    m_busy = true;
    for (wxWindow* child : GetChildren())
        if (child != m_progress_panel)
            child->Disable();
    m_progress_panel->Show();
    update_progress(-1, _L("Preparing configuration..."));

    const auto alive = m_alive;
    // Start after the click handler returns, with the form already disabled and visible.
    CallAfter([this, alive] {
        if (!*alive)
            return;
        auto finished = [this, alive](GFDConfigUploadResult result, bool canceled) {
            if (*alive)
                finish(std::move(result), canceled);
        };
        auto progress = [this, alive](int percent, const wxString& text) {
            if (*alive && m_busy)
                update_progress(percent, text);
        };
        try {
            if (m_submit(finished, progress))
                return;
        } catch (...) {
            // A synchronous preparation failure must also release the busy form.
        }
        GFDConfigUploadResult result;
        result.error_message = _u8L("Could not start the cloud configuration upload. Please try again.");
        finished(std::move(result), false);
    });
}

void GFDCloudConfigDialog::update_progress(int percent, const wxString& text)
{
    if (m_status->GetLabel() != text) {
        m_status->SetLabel(text);
        m_status->SetToolTip(text);
    }
    if (percent < 0) {
        if (!m_timer.IsRunning()) {
            m_gauge->Pulse();
            m_timer.Start(120);
        }
    } else {
        const bool was_pulsing = m_timer.IsRunning();
        m_timer.Stop();
        const int value = std::clamp(percent, 0, 100);
        if (was_pulsing || m_gauge->GetValue() != value)
            m_gauge->SetValue(value);
    }
}

void GFDCloudConfigDialog::finish(GFDConfigUploadResult result, bool canceled)
{
    if (!m_busy)
        return;
    m_timer.Stop();
    m_gauge->Hide();
    m_busy     = false;
    m_result   = std::move(result);
    m_canceled = canceled;
    // The caller shows the outcome only after this modal loop has returned.
    EndModal(wxID_OK);
}

} // namespace Slic3r::GUI
