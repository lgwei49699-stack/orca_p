#ifndef slic3r_GFDModalDialog_hpp_
#define slic3r_GFDModalDialog_hpp_

#include <wx/dialog.h>

namespace Slic3r { namespace GUI {

// The Windows device picker owns its modal loop so ending it does not depend
// on visibility or on which nested event loop is currently active.
class GFDModalDialog : public wxDialog
{
public:
    using wxDialog::wxDialog;

    int  ShowModal() override;
    void EndModal(int ret_code) override;
    bool IsModal() const override { return m_modal_session != nullptr; }
    bool Show(bool show = true) override;
    bool modal_ending() const;

private:
    struct ModalSession;
    ModalSession* m_modal_session{nullptr};
};

#ifdef __WXMSW__
using GFDDeviceSelectionDialogBase = GFDModalDialog;
#else
using GFDDeviceSelectionDialogBase = wxDialog;
#endif

}} // namespace Slic3r::GUI

#endif
