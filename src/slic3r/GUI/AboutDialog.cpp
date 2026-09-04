#include "AboutDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Color.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/Button.hpp"

#include <wx/clipbrd.h>

namespace Slic3r {
namespace GUI {

namespace {

wxString localized_application_name()
{
    return wxGetApp().is_editor() ? _L("WiseBeginner Slicer") : wxString::FromUTF8(GCODEVIEWER_APP_NAME);
}

} // namespace

AboutDialogLogo::AboutDialogLogo(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    this->SetBackgroundColour(*wxWHITE);
    this->logo = ScalableBitmap(this, "WiseBeginnerSlicer_256px", 104);
    this->SetMinSize(this->logo.GetBmpSize());

    this->Bind(wxEVT_PAINT, &AboutDialogLogo::onRepaint, this);
}

void AboutDialogLogo::onRepaint(wxEvent &event)
{
    wxPaintDC dc(this);
    dc.SetBackgroundMode(wxTRANSPARENT);

    wxSize size = this->GetSize();
    int logo_w = this->logo.GetBmpWidth();
    int logo_h = this->logo.GetBmpHeight();
    dc.DrawBitmap(this->logo.bmp(), (size.GetWidth() - logo_w)/2, (size.GetHeight() - logo_h)/2, true);

    event.Skip();
}


// -----------------------------------------
// CopyrightsDialog
// -----------------------------------------
CopyrightsDialog::CopyrightsDialog()
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY,
        localized_application_name() + " - " + _L("Open-source licenses"),
        wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    this->SetFont(wxGetApp().normal_font());
	this->SetBackgroundColour(*wxWHITE);

    wxStaticLine *staticline1 = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );

	auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add( staticline1, 0, wxEXPAND | wxALL, 5 );

    fill_entries();

    m_html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition,
                              wxSize(40 * em_unit(), 20 * em_unit()), wxHW_SCROLLBAR_AUTO);
    m_html->SetMinSize(wxSize(FromDIP(870),FromDIP(520)));
    m_html->SetBackgroundColour(*wxWHITE);
    wxFont font = get_default_font(this);
    const int fs = font.GetPointSize();
    const int fs2 = static_cast<int>(1.2f*fs);
    int size[] = { fs, fs, fs, fs, fs2, fs2, fs2 };

    m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
    m_html->SetBorders(2);
    m_html->SetPage(get_html_text());

    sizer->Add(m_html, 1, wxEXPAND | wxALL, 15);
    m_html->Bind(wxEVT_HTML_LINK_CLICKED, &CopyrightsDialog::onLinkClicked, this);

    SetSizer(sizer);
    sizer->SetSizeHints(this);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void CopyrightsDialog::fill_entries()
{
    m_entries = {
        { "Admesh",                                         "",      "https://admesh.readthedocs.io/" },
        { "Anti-Grain Geometry",                            "",      "http://antigrain.com" },
        { "ArcWelderLib",                                   "",      "https://plugins.octoprint.org/plugins/arc_welder" },
        { "Bambu Studio",                                    "",      "https://github.com/bambulab/BambuStudio" },
        { "Boost",                                          "",      "http://www.boost.org" },
        { "Cereal",                                         "",      "http://uscilab.github.io/cereal" },
        { "CGAL",                                           "",      "https://www.cgal.org" },
        { "Clipper",                                        "",      "http://www.angusj.co" },
        { "libcurl",                                        "",      "https://curl.se/libcurl" },
        { "Eigen3",                                         "",      "http://eigen.tuxfamily.org" },
        { "Expat",                                          "",      "http://www.libexpat.org" },
        { "fast_float",                                     "",      "https://github.com/fastfloat/fast_float" },
        { "GLEW (The OpenGL Extension Wrangler Library)",   "",      "http://glew.sourceforge.net" },
        { "GLFW",                                           "",      "https://www.glfw.org" },
        { "GNU gettext",                                    "",      "https://www.gnu.org/software/gettext" },
        { "ImGUI",                                          "",      "https://github.com/ocornut/imgui" },
        { "ImGuizmo",                                       "",      "https://github.com/CedricGuillemet/ImGuizmo" },
        { "Libigl",                                         "",      "https://libigl.github.io" },
        { "libnest2d",                                      "",      "https://github.com/tamasmeszaros/libnest2d" },
        { "lib_fts",                                        "",      "https://www.forrestthewoods.com" },
        { "Mesa 3D",                                        "",      "https://mesa3d.org" },
        { "Miniz",                                          "",      "https://github.com/richgel999/miniz" },
        { "Nanosvg",                                        "",      "https://github.com/memononen/nanosvg" },
        { "nlohmann/json",                                  "",      "https://json.nlohmann.me" },
        { "OrcaSlicer",                                      "",      "https://github.com/SoftFever/OrcaSlicer" },
        { "Qhull",                                          "",      "http://qhull.org" },
        { "Open Cascade",                                   "",      "https://www.opencascade.com" },
        { "OpenGL",                                         "",      "https://www.opengl.org" },
        { "PoEdit",                                         "",      "https://poedit.net" },
        { "PrusaSlicer",                                    "",      "https://www.prusa3d.com" },
        { "Real-Time DXT1/DXT5 C compression library",      "",      "https://github.com/Cyan4973/RygsDXTc" },
        { "SemVer",                                         "",      "https://semver.org" },
        { "Shinyprofiler",                                  "",      "https://code.google.com/p/shinyprofiler" },
        { "Slic3r",                                          "",      "https://github.com/slic3r/Slic3r" },
        { "SuperSlicer",                                    "",      "https://github.com/supermerill/SuperSlicer" },
        { "TBB",                                            "",      "https://www.intel.cn/content/www/cn/zh/developer/tools/oneapi/onetbb.html" },
        { "wxWidgets",                                      "",      "https://www.wxwidgets.org" },
        { "zlib",                                           "",      "http://zlib.net" },

    };
}

wxString CopyrightsDialog::get_html_text()
{
    wxColour bgr_clr = wxGetApp().get_window_default_clr();//wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);

    const auto text_clr = wxGetApp().get_label_clr_default();// wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    const auto text_clr_str = encode_color(ColorRGB(text_clr.Red(), text_clr.Green(), text_clr.Blue()));
    const auto bgr_clr_str = encode_color(ColorRGB(bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue()));

    wxString text = wxString::Format(
        "<html>"
            "<body bgcolor= %s link= %s>"
            "<font color=%s>"
                "<font size=\"5\">%s</font><br/>"
                "<font size=\"5\">%s</font>"
                "<a href=\"%s\">%s.</a><br/>"
                "<font size=\"5\">%s.</font><br/>"
                "<br /><br />"
                "<font size=\"5\">%s</font><br/>"
                "<font size=\"5\">%s:</font><br/>"
                "<br />"
                "<font size=\"3\">",
         bgr_clr_str, text_clr_str, text_clr_str,
        _L("License"),
        _L("WiseBeginner Slicer is distributed under "),
        "https://www.gnu.org/licenses/agpl-3.0.html",_L("GNU Affero General Public License, version 3"),
        _L("WiseBeginner Slicer is based on OrcaSlicer and related open-source projects"),
        _L("Open-source components"),
        _L("Copyright and other rights for open-source components belong to their respective owners"));

    for (auto& entry : m_entries) {
        text += format_wxstr(
                    "%s<br/>"
                    , entry.lib_name);

         text += wxString::Format(
                    "<a href=\"%s\">%s</a><br/><br/>"
                    , entry.link, entry.link);
    }

    text += wxString(
                "</font>"
            "</font>"
            "</body>"
        "</html>");

    return text;
}

void CopyrightsDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    const wxFont& font = GetFont();
    const int fs = font.GetPointSize();
    const int fs2 = static_cast<int>(1.2f*fs);
    int font_size[] = { fs, fs, fs, fs, fs2, fs2, fs2 };

    m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), font_size);

    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CLOSE });

    const wxSize& size = wxSize(40 * em, 20 * em);

    m_html->SetMinSize(size);
    m_html->Refresh();

    SetMinSize(size);
    Fit();

    Refresh();
}

void CopyrightsDialog::onLinkClicked(wxHtmlLinkEvent &event)
{
    wxGetApp().open_browser_with_warning_dialog(event.GetLinkInfo().GetHref());
    event.Skip(false);
}

void CopyrightsDialog::onCloseDialog(wxEvent &)
{
     this->EndModal(wxID_CLOSE);
}

AboutDialog::AboutDialog()
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY,
        wxString::Format(_L("About %s"), localized_application_name()), wxDefaultPosition,
        wxDefaultSize, /*wxCAPTION*/wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
	SetBackgroundColour(*wxWHITE);

    wxPanel* m_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(560), FromDIP(125)), wxTAB_TRAVERSAL);
    m_panel->SetBackgroundColour(*wxWHITE);

    auto* panel_versizer = new wxBoxSizer(wxVERTICAL);
    auto* header_sizer   = new wxBoxSizer(wxHORIZONTAL);
    m_panel->SetSizer(panel_versizer);

    wxBoxSizer *ver_sizer = new wxBoxSizer(wxVERTICAL);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_panel, 0, wxEXPAND | wxALL, 0);
    main_sizer->Add(ver_sizer, 0, wxEXPAND | wxALL, 0);

    // Product identity
    m_logo_bitmap = ScalableBitmap(m_panel, "WiseBeginnerSlicer_256px", 104);
    m_logo = new wxStaticBitmap(m_panel, wxID_ANY, m_logo_bitmap.bmp(), wxDefaultPosition, wxDefaultSize, 0);
    header_sizer->Add(m_logo, 0, wxALIGN_CENTER_VERTICAL);
    header_sizer->AddSpacer(FromDIP(14));

    auto* brand_sizer = new wxBoxSizer(wxVERTICAL);
    auto* brand_title = new wxStaticText(m_panel, wxID_ANY, _L("WiseBeginner Slicer"));
    wxFont brand_font = GetFont();
    brand_font.SetPointSize(20);
    brand_font.SetWeight(wxFONTWEIGHT_BOLD);
    brand_title->SetFont(brand_font);
    brand_title->SetForegroundColour(wxColour("#27313A"));

    auto* brand_subtitle = new wxStaticText(m_panel, wxID_ANY, _L("Smart slicing for everyday 3D printing"));
    brand_subtitle->SetFont(Label::Body_12);
    brand_subtitle->SetForegroundColour(wxColour("#6B7280"));

    brand_sizer->AddStretchSpacer();
    brand_sizer->Add(brand_title, 0, wxBOTTOM, FromDIP(4));
    brand_sizer->Add(brand_subtitle, 0);
    brand_sizer->AddStretchSpacer();
    header_sizer->Add(brand_sizer, 0, wxEXPAND);
    header_sizer->AddStretchSpacer();

    // version
    {
        auto _build_string_font = Label::Body_12;

        auto* version_sizer = new wxBoxSizer(wxVERTICAL);
        wxStaticText* version = new wxStaticText(m_panel, wxID_ANY, wxString::FromUTF8(SoftFever_VERSION));
        wxStaticText* credits_string = new wxStaticText(
            m_panel, wxID_ANY, _L("Build") + " " + wxString::FromUTF8(GIT_COMMIT_HASH));
        credits_string->SetFont(_build_string_font);
        wxFont version_font = GetFont();
        #ifdef __WXMSW__
			version_font.SetPointSize(version_font.GetPointSize()-1);
        #else
            version_font.SetPointSize(11);
        #endif
        version_font.SetPointSize(20);
        version->SetFont(version_font);
        version->SetForegroundColour(wxColour("#949494"));
        credits_string->SetForegroundColour(wxColour("#949494"));
        version->SetBackgroundColour(wxColour("#FFFFFF"));
        credits_string->SetBackgroundColour(wxColour("#FFFFFF"));

        version_sizer->AddStretchSpacer();
        version_sizer->Add(version, 0, wxALIGN_RIGHT);
        version_sizer->AddSpacer(FromDIP(5));
        version_sizer->Add(credits_string, 0, wxALIGN_RIGHT);
        version_sizer->AddStretchSpacer();
        header_sizer->Add(version_sizer, 0, wxEXPAND);
    }

    panel_versizer->AddSpacer(FromDIP(8));
    panel_versizer->Add(header_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    panel_versizer->AddSpacer(FromDIP(8));
    auto* divider = new wxPanel(m_panel, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(2)));
    divider->SetBackgroundColour(wxColour("#009789"));
    panel_versizer->Add(divider, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    wxBoxSizer *text_sizer_horiz = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *text_sizer = new wxBoxSizer(wxVERTICAL);
    text_sizer_horiz->Add( 0, 0, 0, wxLEFT, FromDIP(20));

    std::vector<wxString> text_list;
    text_list.push_back(_L("WiseBeginner Slicer is designed for everyday 3D printing, including model slicing, print configuration, and sending print jobs."));
    text_list.push_back(_L("After signing in, you can read and locally cache cloud filament and slicing presets, and send jobs to printers linked to your account."));
    text_list.push_back(_L("WiseBeginner Slicer is based on OrcaSlicer. OrcaSlicer is based on Bambu Studio, PrusaSlicer, and SuperSlicer; Bambu Studio is based on PrusaSlicer by Prusa Research, and PrusaSlicer originates from Slic3r by Alessandro Ranellucci."));
    text_list.push_back(_L("We thank these open-source projects and all their contributors."));

    text_sizer->Add( 0, 0, 0, wxTOP, FromDIP(28));
    const std::string language = wxGetApp().app_config->get("language");
    bool is_zh = language.rfind("zh_", 0) == 0;
    for (int i = 0; i < text_list.size(); i++)
    {
        auto staticText = new wxStaticText( this, wxID_ANY, wxEmptyString,wxDefaultPosition,wxSize(FromDIP(520), -1), wxALIGN_LEFT );
        staticText->SetForegroundColour(wxColour(107, 107, 107));
        staticText->SetBackgroundColour(*wxWHITE);
        staticText->SetMinSize(wxSize(FromDIP(520), -1));
        staticText->SetFont(Label::Body_12);
        if (is_zh) {
            wxString find_txt = "";
            wxString count_txt = "";
            for (auto  o = 0; o < text_list[i].length(); o++) {
                auto size = staticText->GetTextExtent(count_txt);
                if (size.x < FromDIP(506)) {
                    find_txt += text_list[i][o];
                    count_txt += text_list[i][o];
                } else {
                    find_txt += std::string("\n") + text_list[i][o];
                    count_txt = text_list[i][o];
                }
            }
            staticText->SetLabel(find_txt);
        } else {
            staticText->SetLabel(text_list[i]);
            staticText->Wrap(FromDIP(520));
        }

        text_sizer->Add( staticText, 0, wxUP | wxDOWN, FromDIP(3));
    }

    text_sizer_horiz->Add(text_sizer, 1, wxALL,0);
    ver_sizer->Add(text_sizer_horiz, 0, wxALL,0);
    ver_sizer->Add( 0, 0, 0, wxTOP, FromDIP(30));

    wxBoxSizer *copyright_ver_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *copyright_hor_sizer = new wxBoxSizer(wxHORIZONTAL);

    copyright_hor_sizer->Add(copyright_ver_sizer, 0, wxLEFT, FromDIP(20));

    wxStaticText *copyright_text = new wxStaticText(
        this, wxID_ANY, _L("WiseBeginner modifications © 2026 WiseBeginner3D."), wxDefaultPosition, wxDefaultSize);
    copyright_text->SetForegroundColour(wxColour(107, 107, 107));

    wxStaticText *open_source_notice = new wxStaticText(
        this, wxID_ANY, _L("Open-source components retain their original copyrights and licenses."), wxDefaultPosition, wxDefaultSize);
    open_source_notice->SetForegroundColour(wxColour(107, 107, 107));

    copyright_ver_sizer->Add(copyright_text, 0, wxALL, 0);
    copyright_ver_sizer->Add(open_source_notice, 0, wxTOP, FromDIP(2));

    m_html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_NEVER /*NEVER*/);
      {
          wxFont font = get_default_font(this);
          const int fs = font.GetPointSize()-1;
          int size[] = {fs,fs,fs,fs,fs,fs,fs};
          m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
          m_html->SetMinSize(wxSize(FromDIP(-1), FromDIP(16)));
          m_html->SetBorders(2);
          const auto text = wxString::Format(
              "<html><body><p style=\"text-align:left\"><a style=\"color:#009789\" "
              "href=\"https://github.com/SoftFever/OrcaSlicer\">%s</a></p></body></html>",
              _L("OrcaSlicer upstream open-source project"));
          m_html->SetPage(text);
          copyright_ver_sizer->Add(m_html, 0, wxEXPAND, 0);
          m_html->Bind(wxEVT_HTML_LINK_CLICKED, &AboutDialog::onLinkClicked, this);
      }
    Button* button_portions = new Button(this, _L("Open-source licenses"));
    button_portions->SetStyle(ButtonStyle::Regular, ButtonType::Window);

    wxBoxSizer *copyright_button_ver = new wxBoxSizer(wxVERTICAL);
    copyright_button_ver->Add( 0, 0, 0, wxTOP, FromDIP(10));
    copyright_button_ver->Add(button_portions, 0, wxALL,0);

    copyright_hor_sizer->AddStretchSpacer();
    copyright_hor_sizer->Add(copyright_button_ver, 0, wxRIGHT, FromDIP(20));

    ver_sizer->Add(copyright_hor_sizer, 0, wxEXPAND ,0);
    ver_sizer->Add( 0, 0, 0, wxTOP, FromDIP(30));
    button_portions->Bind(wxEVT_BUTTON, &AboutDialog::onCopyrightBtn, this);

    wxGetApp().UpdateDlgDarkUI(this);
	SetSizer(main_sizer);
    Layout();
    Fit();
    CenterOnParent();
}

void AboutDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_logo_bitmap.msw_rescale();
    m_logo->SetBitmap(m_logo_bitmap.bmp());

    const wxFont& font = GetFont();
    const int fs = font.GetPointSize() - 1;
    int font_size[] = { fs, fs, fs, fs, fs, fs, fs };
    m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), font_size);

    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CLOSE, m_copy_rights_btn_id });

    m_html->SetMinSize(wxSize(-1, 16 * em));
    m_html->Refresh();

    const wxSize& size = wxSize(65 * em, 30 * em);

    SetMinSize(size);
    Fit();
    Refresh();
}

void AboutDialog::onLinkClicked(wxHtmlLinkEvent &event)
{
    wxGetApp().open_browser_with_warning_dialog(event.GetLinkInfo().GetHref());
    event.Skip(false);
}

void AboutDialog::onCloseDialog(wxEvent &)
{
    this->EndModal(wxID_CLOSE);
}

void AboutDialog::onCopyrightBtn(wxEvent &)
{
    CopyrightsDialog dlg;
    dlg.ShowModal();
}

void AboutDialog::onCopyToClipboard(wxEvent&)
{
    wxTheClipboard->Open();
    wxTheClipboard->SetData(new wxTextDataObject(_L("Version") + " " + GUI_App::format_display_version()));
    wxTheClipboard->Close();
}

} // namespace GUI
} // namespace Slic3r
