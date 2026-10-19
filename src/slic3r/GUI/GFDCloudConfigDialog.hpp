#ifndef slic3r_GUI_GFDCloudConfigDialog_hpp_
#define slic3r_GUI_GFDCloudConfigDialog_hpp_

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <wx/dialog.h>
#include <wx/timer.h>

class wxBoxSizer;
class wxGauge;
class wxStaticText;
class wxPanel;

namespace Slic3r::GUI {

struct GFDConfigUploadResult
{
    bool        ok{false};
    bool        auth_failed{false};
    std::string error_message;
    std::string scheme_name;
    std::string fineness;
};

using GFDConfigUploadFinishedFn = std::function<void(GFDConfigUploadResult, bool)>;
// Called on the GUI thread. -1 means an indeterminate stage; 0..100 is file upload progress only.
using GFDConfigUploadProgressFn = std::function<void(int, const wxString&)>;

class GFDCloudConfigDialog : public wxDialog
{
public:
    using SubmitFn = std::function<bool(GFDConfigUploadFinishedFn, GFDConfigUploadProgressFn)>;

    GFDCloudConfigDialog(wxWindow* parent, const wxString& title);
    ~GFDCloudConfigDialog() override;

    void                         set_submit_handler(SubmitFn submit) { m_submit = std::move(submit); }
    const GFDConfigUploadResult& result() const { return m_result; }
    bool                         was_canceled() const { return m_canceled; }

protected:
    void add_progress_footer(wxBoxSizer* sizer);
    void submit();
    void cancel();

private:
    void update_progress(int percent, const wxString& text);
    void finish(GFDConfigUploadResult result, bool canceled);

    SubmitFn              m_submit;
    GFDConfigUploadResult m_result;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
    wxTimer               m_timer;
    wxPanel*              m_progress_panel{nullptr};
    wxStaticText*         m_status{nullptr};
    wxGauge*              m_gauge{nullptr};
    bool                  m_busy{false};
    bool                  m_canceled{false};
};

} // namespace Slic3r::GUI

#endif
