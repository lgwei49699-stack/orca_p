#include "GFDModalDialog.hpp"

#include <memory>

#include <wx/evtloop.h>
#include <wx/modalhook.h>
#include <wx/utils.h>

namespace Slic3r { namespace GUI {

struct GFDModalDialog::ModalSession : wxGUIEventLoop
{
    explicit ModalSession(GFDModalDialog& dialog) : dialog(dialog), disabler(std::make_unique<wxWindowDisabler>(&dialog))
    {
        dialog.m_modal_session = this;
    }

    ~ModalSession() override
    {
        // Keep the windows disabled until Run() has actually unwound, including
        // any nested dialog. Restore them before hiding to preserve activation.
        disabler.reset();
        dialog.wxDialog::Show(false);
        dialog.m_modal_session = nullptr;
    }

    void finish(int ret_code)
    {
        if (ending)
            return;
        ending = true;
        dialog.SetReturnCode(ret_code);
        if (IsInsideRun())
            ScheduleExit(ret_code);
    }

    int DoRun() override
    {
        // InitDialog/OnEventLoopEnter can finish the dialog before dispatch starts.
        if (ending) {
            OnExit();
            return dialog.GetReturnCode();
        }
        return wxGUIEventLoop::DoRun();
    }

    GFDModalDialog&                   dialog;
    std::unique_ptr<wxWindowDisabler> disabler;
    bool                              ending{false};
};

int GFDModalDialog::ShowModal()
{
    WX_HOOK_MODAL_DIALOG();
    wxCHECK_MSG(!IsModal(), wxID_CANCEL, "ShowModal() cannot be reentered");

    SetReturnCode(wxID_CANCEL);
    ModalSession session(*this);
    wxDialog::Show(true);
    if (!session.ending)
        session.Run();
    return GetReturnCode();
}

void GFDModalDialog::EndModal(int ret_code)
{
    wxCHECK_RET(m_modal_session != nullptr, "EndModal() requires a modal session");
    m_modal_session->finish(ret_code);
}

bool GFDModalDialog::Show(bool show)
{
    if (m_modal_session != nullptr) {
        if (!show) {
            const bool was_shown = IsShown();
            EndModal(GetReturnCode());
            return was_shown;
        }
        if (m_modal_session->ending)
            return false;
    }
    return wxDialog::Show(show);
}

bool GFDModalDialog::modal_ending() const { return m_modal_session != nullptr && m_modal_session->ending; }

}} // namespace Slic3r::GUI
