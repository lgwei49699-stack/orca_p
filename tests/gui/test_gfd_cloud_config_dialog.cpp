#include "slic3r/GUI/GFDCloudConfigDialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <wx/app.h>
#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

using namespace Slic3r::GUI;

namespace {
class TestDialog : public GFDCloudConfigDialog
{
public:
    explicit TestDialog(wxWindow* parent) : GFDCloudConfigDialog(parent, "Cloud configuration lifecycle test")
    {
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        input       = new wxTextCtrl(this, wxID_ANY, "test scheme");
        sizer->Add(input, 0, wxEXPAND);
        add_progress_footer(sizer);
        SetSizerAndFit(sizer);
    }
    using GFDCloudConfigDialog::cancel;
    using GFDCloudConfigDialog::submit;
    wxTextCtrl* input;
};

wxGauge* find_gauge(wxWindow* window)
{
    if (auto* gauge = dynamic_cast<wxGauge*>(window))
        return gauge;
    for (wxWindow* child : window->GetChildren())
        if (auto* gauge = find_gauge(child))
            return gauge;
    return nullptr;
}

class Watchdog : public wxTimer
{
    void Notify() override
    {
        std::fprintf(stderr, "FAIL: cloud configuration modal did not return\n");
        std::fflush(stderr);
        std::_Exit(2);
    }
};

class CloudDialogTestApp : public wxApp
{
public:
    bool OnInit() override
    {
        SetExitOnFrameDelete(false);
        CallAfter([this] { run(); });
        return true;
    }
    int OnRun() override
    {
        wxApp::OnRun();
        return m_failures ? 1 : 0;
    }

private:
    int  m_failures{0};
    int  m_checks{0};
    void check(bool condition, const char* message)
    {
        ++m_checks;
        if (!condition) {
            ++m_failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }

    void run()
    {
        wxFrame parent(nullptr, wxID_ANY, "Cloud configuration automated test");
        parent.Show();
        Watchdog watchdog;
        auto     show = [&](TestDialog& dialog, int expected = wxID_OK) {
            watchdog.StartOnce(5000);
            check(dialog.ShowModal() == expected, "modal returns the expected result");
            watchdog.Stop();
            check(parent.IsEnabled(), "parent is enabled before the caller presents the outcome");
            check(!dialog.IsModal() && !dialog.IsShown(), "form and modal loop are closed before outcome handling");
        };
        GFDConfigUploadFinishedFn late_finish;
        GFDConfigUploadProgressFn late_progress;
        {
            TestDialog dialog(&parent);
            int        submissions = 0;
            dialog.set_submit_handler([&](GFDConfigUploadFinishedFn finish, GFDConfigUploadProgressFn progress) {
                ++submissions;
                check(!dialog.input->IsEnabled(), "editing is locked during submission");
#ifdef __WXMSW__
                check(!parent.IsEnabled(), "parent stays disabled during submission");
#else
                // Cocoa enforces application modality in its native event loop, not wxWindow::Enable(false).
                check(dialog.IsModal(), "native modal session stays active during submission");
#endif
                progress(37, "Uploading file: 37%");
                check(find_gauge(&dialog)->GetValue() == 37, "file upload progress is visible");
                check(find_gauge(&dialog)->IsShownOnScreen(), "progress gauge is shown while working");
                dialog.submit();
                dialog.cancel();
                check(!dialog.Close(), "window close is vetoed while working");
                check(dialog.IsModal(), "cancel cannot abandon an in-flight write");
                late_finish   = finish;
                late_progress = progress;
                dialog.CallAfter([&, finish, progress] {
                    progress(-1, "Verifying saved configuration...");
                    GFDConfigUploadResult result;
                    result.ok          = true;
                    result.scheme_name = "server scheme name";
                    result.fineness    = "fine";
                    finish(result, false);
                    progress(100, "late progress");
                    GFDConfigUploadResult stale_result;
                    finish(stale_result, false);
                });
                return true;
            });
            dialog.CallAfter([&] { dialog.submit(); });
            show(dialog);
            check(submissions == 1, "repeated submission does not send another request");
            check(dialog.result().ok && dialog.result().scheme_name == "server scheme name" && dialog.result().fineness == "fine",
                  "outcome contains the server-confirmed scheme and type");
            check(!find_gauge(&dialog)->IsShown(), "gauge is stopped before the outcome");
        }
        // A canceled/destroyed owner must tolerate queued notifications without touching freed wx controls.
        late_progress(60, "late callback after destruction");
        late_finish({}, false);

        {
            TestDialog dialog(&parent);
            dialog.set_submit_handler([&](GFDConfigUploadFinishedFn finish, GFDConfigUploadProgressFn) {
                wxDialog login(&dialog, wxID_ANY, "Simulated login");
                login.CallAfter([&] {
                    check(login.GetParent() == &dialog, "login belongs to the active configuration form");
                    check(dialog.IsModal(), "outer configuration session survives nested login");
                    login.EndModal(wxID_OK);
                });
                check(login.ShowModal() == wxID_OK, "nested login returns before upload resumes");
                dialog.CallAfter([finish] {
                    GFDConfigUploadResult result;
                    result.ok = true;
                    finish(result, false);
                });
                return true;
            });
            dialog.CallAfter([&] { dialog.submit(); });
            show(dialog);
            check(dialog.result().ok, "submission can finish after nested login closes");
        }

        for (int failure = 0; failure < 4; ++failure) {
            TestDialog dialog(&parent);
            dialog.set_submit_handler([failure](GFDConfigUploadFinishedFn finish, GFDConfigUploadProgressFn) {
                if (failure == 0)
                    return false;
                if (failure == 1)
                    throw std::runtime_error("preparation failed");
                GFDConfigUploadResult result;
                result.error_message = "server did not verify save";
                finish(result, failure == 3);
                return true;
            });
            dialog.CallAfter([&] { dialog.submit(); });
            show(dialog);
            check(!dialog.result().ok && !dialog.result().error_message.empty(), "start/prepare/server failure is not reported as success");
            check(dialog.was_canceled() == (failure == 3), "task cancellation is preserved");
        }
        {
            TestDialog dialog(&parent);
            int        submissions = 0;
            dialog.set_submit_handler([&](GFDConfigUploadFinishedFn, GFDConfigUploadProgressFn) {
                ++submissions;
                return false;
            });
            dialog.CallAfter([&] { dialog.cancel(); });
            show(dialog, wxID_CANCEL);
            check(submissions == 0, "cancel before submission sends no write");
        }
        std::printf("Cloud configuration dialog: %d checks, %d failures\n", m_checks, m_failures);
        std::fflush(stdout);
        parent.Hide();
        ExitMainLoop();
    }
};
} // namespace

wxIMPLEMENT_APP(CloudDialogTestApp);
