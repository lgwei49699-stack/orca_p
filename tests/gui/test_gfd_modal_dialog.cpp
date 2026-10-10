#include "slic3r/GUI/GFDModalDialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <utility>

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/weakref.h>
#include <wx/timer.h>

using Slic3r::GUI::GFDModalDialog;

namespace {

class CallbackTimer : public wxTimer
{
public:
    explicit CallbackTimer(std::function<void()> callback) : m_callback(std::move(callback)) {}
    void Notify() override { m_callback(); }

private:
    std::function<void()> m_callback;
};

class ModalTestApp : public wxApp
{
public:
    bool OnInit() override
    {
        SetExitOnFrameDelete(false);
        CallAfter([this]() { run_tests(); });
        return true;
    }

    int OnRun() override
    {
        wxApp::OnRun();
        return m_failures == 0 ? 0 : 1;
    }

    void OnEventLoopEnter(wxEventLoopBase* loop) override
    {
        wxApp::OnEventLoopEnter(loop);
        if (m_on_loop_enter) {
            auto callback   = std::move(m_on_loop_enter);
            m_on_loop_enter = {};
            callback();
        }
    }

private:
    void check(bool condition, const char* message)
    {
        ++m_checks;
        if (!condition) {
            ++m_failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }

    void run_tests()
    {
        wxFrame parent(nullptr, wxID_ANY, "GFD modal regression parent");
        wxFrame disabled(nullptr, wxID_ANY, "Already disabled window");
        parent.Show();
        disabled.Show();
        disabled.Enable(false);

        CallbackTimer watchdog([]() {
            std::fprintf(stderr, "FAIL: modal loop did not return within 5 seconds\n");
            std::fflush(stderr);
            std::_Exit(2);
        });

        auto run_case = [&](const char* name, const std::function<void(GFDModalDialog&)>& action, int expected) {
            std::printf("CASE: %s\n", name);
            std::fflush(stdout);
            GFDModalDialog dialog(&parent, wxID_ANY, "GFD modal regression");
            dialog.CallAfter([&]() {
                check(dialog.IsModal(), "dialog is modal during dispatch");
                check(!parent.IsEnabled(), "parent is disabled during modal dispatch");
                action(dialog);
            });
            watchdog.StartOnce(5000);
            check(dialog.ShowModal() == expected, "ShowModal returns requested result");
            watchdog.Stop();
            check(!dialog.IsModal(), "modal session released after return");
            check(!dialog.IsShown(), "dialog hidden after return");
            check(parent.IsEnabled(), "parent restored after return");
            check(!disabled.IsEnabled(), "previously disabled window stays disabled");
        };

        run_case(
            "normal completion",
            [&](GFDModalDialog& dialog) {
                dialog.EndModal(wxID_OK);
                check(dialog.modal_ending(), "end request is retained until unwind");
                dialog.EndModal(wxID_CANCEL);
            },
            wxID_OK);

        run_case(
            "already hidden before completion",
            [&](GFDModalDialog& dialog) {
                // Bypass wxDialog::Show so visibility changes without ending its loop.
                dialog.wxTopLevelWindow::Show(false);
                check(!dialog.IsShown(), "fault injection hid the dialog");
                dialog.EndModal(wxID_OK);
            },
            wxID_OK);

        run_case("Hide while modal", [](GFDModalDialog& dialog) { dialog.Hide(); }, wxID_CANCEL);

        run_case(
            "inner confirmation returns before outer completion",
            [&](GFDModalDialog& dialog) {
                wxDialog      inner(&dialog, wxID_ANY, "Success confirmation");
                CallbackTimer finish_inner([&]() { inner.EndModal(wxID_OK); });
                finish_inner.StartOnce(20);
                check(inner.ShowModal() == wxID_OK, "success confirmation returns normally");
                check(dialog.IsShown() && dialog.IsEnabled(), "selector is available after confirmation returns");
                check(dialog.IsModal() && !dialog.modal_ending(), "confirmation alone does not finish selector");
                check(!parent.IsEnabled(), "main window stays disabled until selector completes");
                dialog.EndModal(wxID_OK);
            },
            wxID_OK);

        run_case(
            "outer completion while nested dialog active",
            [&](GFDModalDialog& dialog) {
                wxDialog      inner(&dialog, wxID_ANY, "Nested confirmation");
                CallbackTimer finish_inner([&]() {
                    dialog.EndModal(wxID_OK);
                    check(!parent.IsEnabled(), "outer completion does not enable parent under nested dialog");
                    check(dialog.IsModal(), "outer session lives until nested dialog returns");
                    inner.EndModal(wxID_OK);
                });
                finish_inner.StartOnce(20);
                check(inner.ShowModal() == wxID_OK, "nested dialog returns normally");
                check(!parent.IsEnabled(), "parent remains disabled until outer dispatch unwinds");
            },
            wxID_OK);

        run_case("window close cancels", [](GFDModalDialog& dialog) { dialog.Close(); }, wxID_CANCEL);

        std::printf("CASE: completion during OnEventLoopEnter\n");
        std::fflush(stdout);
        {
            GFDModalDialog dialog(&parent, wxID_ANY, "Loop entry completion");
            m_on_loop_enter = [&]() { dialog.EndModal(wxID_OK); };
            watchdog.StartOnce(5000);
            check(dialog.ShowModal() == wxID_OK, "completion before dispatch returns requested result");
            watchdog.Stop();
            check(!dialog.IsModal() && !dialog.IsShown(), "loop entry completion releases and hides dialog");
            check(parent.IsEnabled(), "loop entry completion restores parent");
        }

#ifdef __WXMSW__
        // Windows Show() invokes InitDialog before starting the modal loop.
        std::printf("CASE: completion during InitDialog\n");
        std::fflush(stdout);
        {
            class InitCompletionDialog : public GFDModalDialog
            {
            public:
                using GFDModalDialog::GFDModalDialog;
                void InitDialog() override { EndModal(wxID_OK); }
            };
            InitCompletionDialog dialog(&parent, wxID_ANY, "Init completion");
            watchdog.StartOnce(5000);
            check(dialog.ShowModal() == wxID_OK, "completion before Run returns without dispatch");
            watchdog.Stop();
            check(!dialog.IsModal() && !dialog.IsShown(), "early completion releases and hides dialog");
            check(parent.IsEnabled(), "early completion restores parent");
            check(!disabled.IsEnabled(), "early completion preserves other disabled window");
        }
#endif

        std::printf("CASE: repeat ShowModal on same dialog\n");
        std::fflush(stdout);
        {
            GFDModalDialog dialog(&parent, wxID_ANY, "Repeat modal");
            for (int i = 0; i < 5; ++i) {
                dialog.CallAfter([&]() { dialog.EndModal(wxID_OK); });
                watchdog.StartOnce(5000);
                check(dialog.ShowModal() == wxID_OK, "repeated modal call returns normally");
                watchdog.Stop();
                check(parent.IsEnabled(), "repeated modal call restores parent");
            }
        }

        std::printf("CASE: app quit while native and custom modal dialogs are on the stack\n");
        std::fflush(stdout);
        for (bool custom : {false, true}) {
            auto*              owner = new wxFrame(nullptr, wxID_ANY, "Owner close regression");
            wxWeakRef<wxFrame> owner_alive(owner);
            owner->Show();
            auto* child = new wxPanel(owner); // Like MainFrame -> Plater -> stack dialog.
            owner->Bind(wxEVT_CLOSE_WINDOW, [owner](wxCloseEvent& event) {
                if (!Slic3r::GUI::veto_close_with_modal_dialog(owner, event))
                    event.Skip();
            });
            {
                wxDialog       native(child, wxID_ANY, "Native stack dialog");
                GFDModalDialog managed(child, wxID_ANY, "Custom stack dialog");
                wxDialog&      outer = custom ? static_cast<wxDialog&>(managed) : native;
                outer.CallAfter([&]() {
                    check(!owner->Close(), "app quit is vetoed while outer modal is active");
                    check(owner_alive.get() != nullptr, "owner remains alive under modal loop");
                    wxDialog inner(&outer, wxID_ANY, "Nested stack dialog");
                    inner.CallAfter([&]() {
                        check(!owner->Close(), "app quit is also vetoed under a nested modal");
                        check(outer.IsModal() && inner.IsModal(), "quit does not prematurely end either modal");
                        wxCloseEvent unrelated(wxEVT_CLOSE_WINDOW);
                        check(!Slic3r::GUI::veto_close_with_modal_dialog(&disabled, unrelated),
                              "another window's dialog does not block an unrelated owner");
                        wxCloseEvent forced(wxEVT_CLOSE_WINDOW);
                        forced.SetCanVeto(false);
                        check(!Slic3r::GUI::veto_close_with_modal_dialog(owner, forced),
                              "forced OS shutdown retains non-vetoable semantics");
                        inner.EndModal(wxID_CANCEL);
                    });
                    check(inner.ShowModal() == wxID_CANCEL, "nested modal returns normally after quit was rejected");
                    outer.EndModal(wxID_CANCEL);
                });
                watchdog.StartOnce(5000);
                check(outer.ShowModal() == wxID_CANCEL, "outer modal returns normally after quit was rejected");
                watchdog.Stop();
                wxCloseEvent modeless(wxEVT_CLOSE_WINDOW);
                check(!Slic3r::GUI::veto_close_with_modal_dialog(owner, modeless), "existing non-modal dialogs do not block normal close");
            }
            check(owner->Close(), "normal close succeeds after modal stack unwinds");
            wxYield();
            check(!owner_alive, "normal close destroys owner after dialogs unwind");
        }

        std::printf("%d checks, %d failures\n", m_checks, m_failures);
        std::fflush(stdout);
        ExitMainLoop();
    }

    int                   m_checks{0};
    int                   m_failures{0};
    std::function<void()> m_on_loop_enter;
};

} // namespace

wxIMPLEMENT_APP_NO_MAIN(ModalTestApp);

int main(int argc, char** argv) { return wxEntry(argc, argv); }
