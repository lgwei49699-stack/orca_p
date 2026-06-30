#include "MainFrame.hpp"

#include <wx/panel.h>
#include <wx/notebook.h>
#include <wx/listbook.h>
#include <wx/simplebook.h>
#include <wx/icon.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/progdlg.h>
#include <wx/tooltip.h>
//#include <wx/glcanvas.h>
#include <wx/filename.h>
#include <wx/debug.h>
#include <wx/utils.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "Tab.hpp"
#include "ProgressStatusBar.hpp"
#include "3DScene.hpp"
#include "ParamsDialog.hpp"
#include "PrintHostDialogs.hpp"
#include "wxExtensions.hpp"
#include "GUI_ObjectList.hpp"
#include "Mouse3DController.hpp"
//#include "RemovableDriveManager.hpp"
#include "InstanceCheck.hpp"
#include "I18N.hpp"
#include "GLCanvas3D.hpp"
#include "Plater.hpp"
#include "WebViewDialog.hpp"
#include "../Utils/Process.hpp"
#include "../Utils/GFDConfig.hpp"
#include "format.hpp"
// BBS
#include "PartPlate.hpp"
#include "Preferences.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "BindDialog.hpp"
#include "../Utils/MacDarkMode.hpp"

#include <fstream>
#include <string_view>

#include "GUI_App.hpp"
#include "UnsavedChangesDialog.hpp"
#include "MsgDialog.hpp"
#include "Notebook.hpp"
#include "GUI_Factories.hpp"
#include "GUI_ObjectList.hpp"
#include "NotificationManager.hpp"
#include "MarkdownTip.hpp"
#include "NetworkTestDialog.hpp"
#include "ConfigWizard.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/SwitchButton.hpp"
#include "Widgets/WebView.hpp"
#include "DailyTips.hpp"
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/clntdata.h>
#include <wx/combobox.h>
#include <wx/listctrl.h>
#include <wx/grid.h>
#include <wx/scrolwin.h>

#ifdef _WIN32
#include <dbt.h>
#include <shlobj.h>
#include <shellapi.h>
#endif // _WIN32
#include <slic3r/GUI/CreatePresetsDialog.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>

#include <nlohmann/json.hpp>

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_SELECT_TAB, wxCommandEvent);
wxDEFINE_EVENT(EVT_HTTP_ERROR, wxCommandEvent);
wxDEFINE_EVENT(EVT_USER_LOGIN, wxCommandEvent);
wxDEFINE_EVENT(EVT_USER_LOGIN_HANDLE, wxCommandEvent);
wxDEFINE_EVENT(EVT_CHECK_PRIVACY_VER, wxCommandEvent);
wxDEFINE_EVENT(EVT_CHECK_PRIVACY_SHOW, wxCommandEvent);
wxDEFINE_EVENT(EVT_SHOW_IP_DIALOG, wxCommandEvent);
wxDEFINE_EVENT(EVT_SET_SELECTED_MACHINE, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_MACHINE_LIST, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_PRESET_CB, SimpleEvent);



// BBS: backup
wxDEFINE_EVENT(EVT_BACKUP_POST, wxCommandEvent);
wxDEFINE_EVENT(EVT_LOAD_URL, wxCommandEvent);
wxDEFINE_EVENT(EVT_LOAD_PRINTER_URL, LoadPrinterViewEvent);

enum class ERescaleTarget
{
    Mainframe,
    SettingsDialog
};

#ifdef __APPLE__
class OrcaSlicerTaskBarIcon : public wxTaskBarIcon
{
public:
    OrcaSlicerTaskBarIcon(wxTaskBarIconType iconType = wxTBI_DEFAULT_TYPE) : wxTaskBarIcon(iconType) {}
    wxMenu *CreatePopupMenu() override {
        wxMenu *menu = new wxMenu;
        if (wxGetApp().app_config->get("single_instance") == "false") {
            // Only allow opening a new PrusaSlicer instance on OSX if "single_instance" is disabled,
            // as starting new instances would interfere with the locking mechanism of "single_instance" support.
            append_menu_item(menu, wxID_ANY, _L("New Window"), _L("Open a new window"),
            [](wxCommandEvent&) { start_new_slicer(); }, "", nullptr);
        }
//        append_menu_item(menu, wxID_ANY, _L("G-code Viewer") + dots, _L("Open G-code Viewer"),
//            [](wxCommandEvent&) { start_new_gcodeviewer_open_file(); }, "", nullptr);
        return menu;
    }
};
/*class GCodeViewerTaskBarIcon : public wxTaskBarIcon
{
public:
    GCodeViewerTaskBarIcon(wxTaskBarIconType iconType = wxTBI_DEFAULT_TYPE) : wxTaskBarIcon(iconType) {}
    wxMenu *CreatePopupMenu() override {
        wxMenu *menu = new wxMenu;
        append_menu_item(menu, wxID_ANY, _L("Open PrusaSlicer"), _L("Open a new PrusaSlicer"),
            [](wxCommandEvent&) { start_new_slicer(nullptr, true); }, "", nullptr);
        //append_menu_item(menu, wxID_ANY, _L("G-code Viewer") + dots, _L("Open new G-code Viewer"),
        //    [](wxCommandEvent&) { start_new_gcodeviewer_open_file(); }, "", nullptr);
        return menu;
    }
};*/
#endif // __APPLE__

// Load the icon either from the exe, or from the ico file.
static wxIcon main_frame_icon(GUI_App::EAppMode app_mode)
{
#if _WIN32
    std::wstring path(size_t(MAX_PATH), wchar_t(0));
    int len = int(::GetModuleFileName(nullptr, path.data(), MAX_PATH));
    if (len > 0 && len < MAX_PATH) {
        path.erase(path.begin() + len, path.end());
        //BBS: remove GCodeViewer as seperate APP logic
        /*if (app_mode == GUI_App::EAppMode::GCodeViewer) {
            // Only in case the slicer was started with --gcodeviewer parameter try to load the icon from prusa-gcodeviewer.exe
            // Otherwise load it from the exe.
            for (const std::wstring_view exe_name : { std::wstring_view(L"prusa-slicer.exe"), std::wstring_view(L"prusa-slicer-console.exe") })
                if (boost::iends_with(path, exe_name)) {
                    path.erase(path.end() - exe_name.size(), path.end());
                    path += L"prusa-gcodeviewer.exe";
                    break;
                }
        }*/
    }
    return wxIcon(path, wxBITMAP_TYPE_ICO);
#else // _WIN32
    return wxIcon(Slic3r::var("OrcaSlicer_128px.png"), wxBITMAP_TYPE_PNG);
#endif // _WIN32
}

// BBS
#ifndef __APPLE__
#define BORDERLESS_FRAME_STYLE (wxRESIZE_BORDER | wxMINIMIZE_BOX | wxMAXIMIZE_BOX | wxCLOSE_BOX)
#else
#define BORDERLESS_FRAME_STYLE (wxMINIMIZE_BOX | wxMAXIMIZE_BOX | wxCLOSE_BOX)
#endif

wxDEFINE_EVENT(EVT_SYNC_CLOUD_PRESET,     SimpleEvent);

#ifdef __APPLE__
static const wxString ctrl = ("Ctrl+");
// FIXME: maybe should be using GUI::shortkey_ctrl_prefix() or equivalent?
static const std::string ctrl_t = u8"\u2318+"; // "⌘" (Mac Command)
#else
static const wxString ctrl = _L("Ctrl+");
// FIXME: maybe should be using GUI::shortkey_ctrl_prefix() or equivalent?
static const wxString ctrl_t = ctrl;
#endif
static const wxString shift = _L("Shift+");

namespace {

namespace fs = boost::filesystem;

void apply_window_button_style(Button* button, ButtonStyle style)
{
    if (button != nullptr)
        button->SetStyle(style, ButtonType::Window);
}

void apply_dialog_action_button_style(Button* button, ButtonStyle style, const wxSize& size)
{
    if (button == nullptr)
        return;

    button->SetStyle(style, ButtonType::Choice);
    button->SetMinSize(size);
    button->SetSize(size);
}

struct GFDPrinterState
{
    std::string selected_printer_model;
    std::string selected_device_type;
    std::string edited_printer_model;
    std::string edited_device_type;
    std::string effective_printer_model;
    std::string effective_device_type;
};

GFDPrinterState current_gfd_printer_state()
{
    GFDPrinterState state;
    if (wxGetApp().preset_bundle == nullptr)
        return state;

    auto read_printer_state = [](const DynamicPrintConfig& config, std::string& printer_model, std::string& device_type) {
        const auto* printer_model_opt = config.option<ConfigOptionString>("printer_model");
        printer_model = printer_model_opt != nullptr ? printer_model_opt->value : std::string();
        device_type   = GFD::Config::explicit_device_type(config);
    };

    read_printer_state(wxGetApp().preset_bundle->printers.get_selected_preset().config,
                       state.selected_printer_model,
                       state.selected_device_type);
    read_printer_state(wxGetApp().preset_bundle->printers.get_edited_preset().config,
                       state.edited_printer_model,
                       state.edited_device_type);

    if (!state.selected_device_type.empty()) {
        state.effective_printer_model = state.selected_printer_model;
        state.effective_device_type   = state.selected_device_type;
    } else {
        state.effective_printer_model = state.edited_printer_model;
        state.effective_device_type   = state.edited_device_type;
    }

    return state;
}

class GFDUploadConfigDialog : public wxDialog
{
public:
    explicit GFDUploadConfigDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, _L("上传配置"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    {
        build();
        bind_events();
        wxGetApp().UpdateDlgDarkUI(this);
    }

    wxString config_name() const
    {
        return m_name_input != nullptr ? m_name_input->GetTextCtrl()->GetValue() : wxString(wxEmptyString);
    }

    wxString remarks() const
    {
        return m_remarks_input != nullptr ? m_remarks_input->GetValue() : wxString(wxEmptyString);
    }

private:
    ::TextInput*  m_name_input{nullptr};
    wxTextCtrl*   m_remarks_input{nullptr};
    wxStaticText* m_tip_label{nullptr};
    Button*       m_cancel_button{nullptr};
    Button*       m_confirm_button{nullptr};

    void build()
    {
        SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        const wxSize client_size(FromDIP(460), FromDIP(350));
        SetClientSize(client_size);
        SetMinClientSize(client_size);

        auto* main_sizer = new wxBoxSizer(wxVERTICAL);
        main_sizer->AddSpacer(FromDIP(20));

        auto* name_label = new wxStaticText(this, wxID_ANY, _L("配置名称"), wxDefaultPosition, wxDefaultSize, 0);
        name_label->SetFont(::Label::Body_13);
        main_sizer->Add(name_label, 0, wxLEFT | wxRIGHT, FromDIP(24));
        main_sizer->AddSpacer(FromDIP(8));

        auto* name_input_wrap = new wxBoxSizer(wxVERTICAL);
        m_name_input = new ::TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(412), FromDIP(38)), wxTE_PROCESS_ENTER);
        m_name_input->SetCornerRadius(FromDIP(8));
        m_name_input->SetMinSize(wxSize(FromDIP(412), FromDIP(38)));
        m_name_input->GetTextCtrl()->SetHint(_L("请输入配置名称"));
        name_input_wrap->Add(m_name_input, 0, wxEXPAND | wxALL, FromDIP(1));
        main_sizer->Add(name_input_wrap, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(23));

        main_sizer->AddSpacer(FromDIP(16));

        auto* remarks_label = new wxStaticText(this, wxID_ANY, _L("备注"), wxDefaultPosition, wxDefaultSize, 0);
        remarks_label->SetFont(::Label::Body_13);
        main_sizer->Add(remarks_label, 0, wxLEFT | wxRIGHT, FromDIP(24));
        main_sizer->AddSpacer(FromDIP(8));

        m_remarks_input = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(412), FromDIP(112)), wxTE_MULTILINE | wxBORDER_SIMPLE);
        m_remarks_input->SetHint(_L("请输入备注"));
        m_remarks_input->SetMinSize(wxSize(FromDIP(412), FromDIP(112)));
        m_remarks_input->SetBackgroundColour(*wxWHITE);
        main_sizer->Add(m_remarks_input, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(24));

        main_sizer->AddSpacer(FromDIP(10));

        m_tip_label = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
        m_tip_label->SetFont(::Label::Body_12);
        m_tip_label->SetForegroundColour(wxColour(220, 38, 38));
        main_sizer->Add(m_tip_label, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(24));
        main_sizer->AddSpacer(FromDIP(18));

        auto* button_row = new wxBoxSizer(wxHORIZONTAL);
        button_row->AddStretchSpacer(1);

        const wxSize action_button_size(FromDIP(76), FromDIP(32));
        button_row->SetMinSize(wxSize(-1, action_button_size.y));

        m_cancel_button = new Button(this, _L("取消"));
        apply_dialog_action_button_style(m_cancel_button, ButtonStyle::Regular, action_button_size);
        button_row->Add(m_cancel_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

        m_confirm_button = new Button(this, _L("确定"));
        apply_dialog_action_button_style(m_confirm_button, ButtonStyle::Confirm, action_button_size);
        button_row->Add(m_confirm_button, 0, wxALIGN_CENTER_VERTICAL);

        main_sizer->Add(button_row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(24));

        SetSizer(main_sizer);
        Layout();
        CentreOnParent();
    }

    void bind_events()
    {
        m_cancel_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
        m_confirm_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_confirm(); });
        m_name_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { on_confirm(); });
        m_name_input->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { clear_tip(); });
        m_remarks_input->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { clear_tip(); });
    }

    void clear_tip()
    {
        if (m_tip_label != nullptr && !m_tip_label->GetLabel().empty())
            m_tip_label->SetLabel(wxEmptyString);
    }

    void on_confirm()
    {
        wxString name = config_name();
        name.Trim(true).Trim(false);
        if (name.empty()) {
            m_tip_label->SetLabel(_L("请输入配置名称"));
            if (m_name_input != nullptr)
                m_name_input->GetTextCtrl()->SetFocus();
            return;
        }

        clear_tip();
        EndModal(wxID_OK);
    }
};

class GFDCloudImportDialog : public wxDialog
{
public:
    GFDCloudImportDialog(wxWindow* parent, Plater* plater)
        : wxDialog(parent, wxID_ANY, _L("云端导入"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
        , m_plater(plater)
    {
        SetDoubleBuffered(true);
        build();
        bind_events();
        load_device_types();
        wxGetApp().UpdateDlgDarkUI(this);
        Freeze();
        fetch_configs_for_selected_device();
        Layout();
        Thaw();
    }

    void refresh_configs() { fetch_configs_for_selected_device(); }

private:
    Plater*                         m_plater{nullptr};
    wxChoice*                       m_device_choice{nullptr};
    wxScrolledWindow*               m_config_list{nullptr};
    wxBoxSizer*                     m_config_list_sizer{nullptr};
    wxStaticText*                   m_tip_label{nullptr};
    Button*                         m_refresh_button{nullptr};
    Button*                         m_cancel_button{nullptr};
    std::vector<GFDCloudConfigInfo> m_configs;

    void build()
    {
        SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        SetMinSize(wxSize(FromDIP(920), FromDIP(560)));
        SetSize(wxSize(FromDIP(920), FromDIP(560)));

        auto* main_sizer = new wxBoxSizer(wxVERTICAL);
        main_sizer->AddSpacer(FromDIP(18));

        auto* filter_row = new wxBoxSizer(wxHORIZONTAL);
        auto* device_label = new wxStaticText(this, wxID_ANY, _L("设备机型"), wxDefaultPosition, wxDefaultSize, 0);
        device_label->SetFont(::Label::Body_14);
        filter_row->Add(device_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

        m_device_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(220), FromDIP(30)));
        filter_row->Add(m_device_choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

        m_refresh_button = new Button(this, _L("刷新"));
        apply_window_button_style(m_refresh_button, ButtonStyle::Regular);
        m_refresh_button->SetMinSize(wxSize(FromDIP(72), FromDIP(30)));
        filter_row->Add(m_refresh_button, 0, wxALIGN_CENTER_VERTICAL);
        filter_row->AddStretchSpacer(1);
        main_sizer->Add(filter_row, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(24));

        main_sizer->AddSpacer(FromDIP(14));

        m_config_list = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(872), FromDIP(390)), wxBORDER_SIMPLE | wxVSCROLL);
        m_config_list->SetDoubleBuffered(true);
        m_config_list->SetScrollRate(0, FromDIP(12));
        m_config_list->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        m_config_list_sizer = new wxBoxSizer(wxVERTICAL);
        m_config_list->SetSizer(m_config_list_sizer);
        main_sizer->Add(m_config_list, 1, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(24));

        main_sizer->AddSpacer(FromDIP(10));

        m_tip_label = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
        m_tip_label->SetFont(::Label::Body_13);
        m_tip_label->SetForegroundColour(wxColour(220, 38, 38));
        main_sizer->Add(m_tip_label, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(24));

        auto* button_row = new wxBoxSizer(wxHORIZONTAL);
        button_row->AddStretchSpacer(1);
        m_cancel_button = new Button(this, _L("取消"));
        apply_window_button_style(m_cancel_button, ButtonStyle::Regular);
        m_cancel_button->SetMinSize(wxSize(FromDIP(76), FromDIP(32)));
        button_row->Add(m_cancel_button, 0);
        main_sizer->Add(button_row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(24));

        SetSizer(main_sizer);
        Layout();
        CentreOnParent();
    }

    void bind_events()
    {
        m_cancel_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
        m_refresh_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { fetch_configs_for_selected_device(); });
        m_device_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { fetch_configs_for_selected_device(); });
    }

    void load_device_types()
    {
        std::vector<std::string> device_types = GFD::Config::local_gfd_device_types();
        std::string current_device_type;
        if (wxGetApp().preset_bundle != nullptr)
            current_device_type = GFD::Config::current_device_type(wxGetApp().preset_bundle->printers.get_selected_preset().config);
        if (!current_device_type.empty() &&
            std::find(device_types.begin(), device_types.end(), current_device_type) == device_types.end())
            device_types.insert(device_types.begin(), current_device_type);

        m_device_choice->Clear();
        for (const std::string& device_type : device_types)
            m_device_choice->Append(from_u8(device_type));

        if (!current_device_type.empty())
            m_device_choice->SetStringSelection(from_u8(current_device_type));
        else if (!device_types.empty())
            m_device_choice->SetSelection(0);

        if (m_tip_label != nullptr)
            m_tip_label->SetLabel(_L("点击刷新获取当前机型的云端配置"));
    }

    std::string selected_device_type() const
    {
        return m_device_choice != nullptr && m_device_choice->GetSelection() != wxNOT_FOUND ?
                   into_u8(m_device_choice->GetStringSelection()) :
                   std::string();
    }

    void fetch_configs_for_selected_device()
    {
        m_configs.clear();
        rebuild_config_rows();

        const std::string device_type = selected_device_type();
        if (device_type.empty()) {
            m_tip_label->SetLabel(_L("未找到可用机型"));
            return;
        }

        m_tip_label->SetLabel(_L("正在获取云端配置..."));
        m_refresh_button->Enable(false);
        Layout();

        std::string error_message;
        try {
            if (m_plater == nullptr || !m_plater->fetch_cloud_configs(device_type, m_configs, error_message)) {
                m_tip_label->SetLabel(from_u8(error_message.empty() ? "获取云端配置失败" : error_message));
                m_refresh_button->Enable(true);
                return;
            }
        } catch (const std::exception& ex) {
            BOOST_LOG_TRIVIAL(error) << "GFD cloud import dialog fetch failed"
                                     << ", device_type=" << device_type
                                     << ", error=" << ex.what();
            m_tip_label->SetLabel(from_u8(std::string("获取云端配置失败: ") + ex.what()));
            m_refresh_button->Enable(true);
            return;
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "GFD cloud import dialog fetch failed with unknown exception"
                                     << ", device_type=" << device_type;
            m_tip_label->SetLabel(_L("获取云端配置失败"));
            m_refresh_button->Enable(true);
            return;
        }

        rebuild_config_rows();

        if (m_configs.empty())
            m_tip_label->SetLabel(_L("当前机型暂无云端配置"));
        else
            m_tip_label->SetLabel(wxEmptyString);

        m_refresh_button->Enable(true);
    }

    wxStaticText* create_row_label(wxWindow* parent, const wxString& text, int min_width)
    {
        auto* label = new wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
        label->SetFont(::Label::Body_14);
        label->SetMinSize(wxSize(FromDIP(min_width), -1));
        if (!text.empty())
            label->SetToolTip(text);
        return label;
    }

    void add_row_label(wxPanel* row_panel, wxBoxSizer* row, const wxString& text, int min_width, int proportion, int border = 8)
    {
        row->Add(create_row_label(row_panel, text, min_width), proportion, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(border));
    }

    void apply_link_button_style(Button* button, const wxColour& background)
    {
        if (button == nullptr)
            return;
        const wxColour green(0, 150, 136);
        const wxColour hover_bg(232, 247, 245);
        button->SetFont(::Label::Body_14);
        button->SetMinSize(wxSize(FromDIP(108), FromDIP(30)));
        button->SetCornerRadius(0);
        button->SetBorderWidth(0);
        button->SetBackgroundColour(background);
        button->SetBackgroundColor(StateColor(
            std::pair<wxColour, int>(hover_bg, StateColor::Pressed),
            std::pair<wxColour, int>(hover_bg, StateColor::Hovered),
            std::pair<wxColour, int>(background, StateColor::Normal),
            std::pair<wxColour, int>(background, StateColor::Enabled)
        ));
        button->SetBorderColor(StateColor(background));
        button->SetTextColor(StateColor(
            std::pair<wxColour, int>(wxColour(120, 120, 120), StateColor::Disabled),
            std::pair<wxColour, int>(green, StateColor::Hovered),
            std::pair<wxColour, int>(green, StateColor::Normal)
        ));
    }

    void clear_config_rows()
    {
        if (m_config_list_sizer == nullptr)
            return;
        m_config_list_sizer->Clear(true);
    }

    void add_header_row()
    {
        if (m_config_list_sizer == nullptr)
            return;

        auto* header_panel = new wxPanel(m_config_list);
        header_panel->SetBackgroundColour(wxColour(245, 245, 245));
        header_panel->SetMinSize(wxSize(-1, FromDIP(38)));
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->AddSpacer(FromDIP(8));
        add_row_label(header_panel, row, _L("配置名称"), 120, 2);
        add_row_label(header_panel, row, _L("机型"), 70, 1);
        add_row_label(header_panel, row, _L("方案文件"), 230, 4);
        add_row_label(header_panel, row, _L("备注"), 130, 2);
        row->Add(create_row_label(header_panel, _L("操作"), 100), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        header_panel->SetSizer(row);
        m_config_list_sizer->Add(header_panel, 0, wxEXPAND);
    }

    void add_config_row(size_t index)
    {
        if (m_config_list_sizer == nullptr || index >= m_configs.size())
            return;

        const GFDCloudConfigInfo& config = m_configs[index];
        auto* row_panel = new wxPanel(m_config_list);
        const wxColour row_bg = index % 2 == 0 ? wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW) : wxColour(250, 250, 250);
        row_panel->SetBackgroundColour(row_bg);
        row_panel->SetMinSize(wxSize(-1, FromDIP(44)));

        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->AddSpacer(FromDIP(8));
        add_row_label(row_panel, row, from_u8(config.name), 120, 2);
        add_row_label(row_panel, row, from_u8(config.device_type), 70, 1);
        const std::string config_file_display = !config.config_file_url.empty() ? config.config_file_url : config.config_file_name;
        add_row_label(row_panel, row, from_u8(config_file_display), 230, 4);
        add_row_label(row_panel, row, from_u8(config.info), 130, 2);

        auto* apply_button = new Button(row_panel, _L("设置此参数"));
        apply_link_button_style(apply_button, row_bg);
        apply_button->Bind(wxEVT_BUTTON, [this, index](wxCommandEvent&) { apply_config(index); });
        row->Add(apply_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        row_panel->SetSizer(row);
        m_config_list_sizer->Add(row_panel, 0, wxEXPAND);
    }

    void rebuild_config_rows()
    {
        if (m_config_list != nullptr)
            m_config_list->Freeze();
        clear_config_rows();
        add_header_row();
        for (size_t i = 0; i < m_configs.size(); ++i)
            add_config_row(i);
        if (m_config_list != nullptr) {
            m_config_list->FitInside();
            m_config_list->Layout();
            m_config_list->Refresh();
            m_config_list->Thaw();
        }
        Layout();
    }

    void apply_config(size_t index)
    {
        if (index >= m_configs.size() || m_plater == nullptr) {
            m_tip_label->SetLabel(_L("请选择要导入的云端配置"));
            return;
        }

        const GFDCloudConfigInfo config = m_configs[index];
        BOOST_LOG_TRIVIAL(info) << "GFD cloud import apply row"
                                << ", id=" << config.id
                                << ", name=" << config.name
                                << ", device_type=" << config.device_type;
        m_tip_label->SetLabel(_L("正在应用云端配置..."));
        m_refresh_button->Enable(false);
        Layout();

        const bool imported = m_plater->import_cloud_config(config);
        m_refresh_button->Enable(true);
        if (imported) {
            m_tip_label->SetLabel(_L("云端配置已应用"));
            EndModal(wxID_CANCEL);
        } else {
            m_tip_label->SetLabel(_L("云端配置应用失败"));
        }
    }
};

struct GFDDynamicFilamentOption
{
    std::string text;
    std::string title;
    std::string name;
    std::string sn;
    std::string id;
    std::string barcode;
    std::string material;
    std::string color;
};

struct GFDDynamicParamRow
{
    std::string name;
    std::string param;
    std::string value;
};

int gfd_filament_option_score(const GFDDynamicFilamentOption& option)
{
    int score = 0;
    if (!option.color.empty())
        score += 30;
    if (!option.name.empty())
        score += 6;
    if (!option.barcode.empty())
        score += 30;
    if (!option.sn.empty())
        score += 8;
    if (!option.text.empty() && option.text != option.barcode && option.text != option.sn)
        score += 8;
    return score;
}

std::string gfd_trim_copy(const std::string& value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string gfd_compact_key_copy(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                    return std::isspace(ch) != 0 || ch == '_' || ch == '-';
                }),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string gfd_join_non_empty(const std::vector<std::string>& values, const std::string& separator)
{
    std::vector<std::string> parts;
    for (const std::string& value : values) {
        const std::string trimmed = gfd_trim_copy(value);
        if (!trimmed.empty())
            parts.emplace_back(trimmed);
    }
    return boost::algorithm::join(parts, separator);
}

std::string gfd_filament_display_text(const std::string& title, const std::string& barcode)
{
    const std::string clean_title = gfd_trim_copy(title);
    const std::string clean_barcode = gfd_trim_copy(barcode);
    if (clean_barcode.empty() || clean_title.find(clean_barcode) != std::string::npos)
        return clean_title;
    return gfd_join_non_empty({clean_title, clean_barcode}, " ");
}

bool gfd_is_identifier_like(const std::string& value)
{
    const std::string trimmed = gfd_trim_copy(value);
    if (trimmed.size() < 8)
        return false;
    return std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_' || ch == '-';
    });
}

bool gfd_is_color_code_like(const std::string& value)
{
    std::string trimmed = gfd_trim_copy(value);
    if (!trimmed.empty() && trimmed.front() == '#')
        trimmed.erase(trimmed.begin());
    if (!(trimmed.size() == 6 || trimmed.size() == 8))
        return false;
    return std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}

std::string gfd_best_filament_title(const std::string& title,
                                    const std::string& name,
                                    const std::string& color,
                                    const std::string& material,
                                    const std::string& barcode,
                                    const std::string& sn,
                                    const std::string& id)
{
    for (const std::string& candidate : {title, name, color, material}) {
        const std::string value = gfd_trim_copy(candidate);
        if (value.empty())
            continue;
        if (value == barcode || value == sn || value == id || gfd_is_identifier_like(value))
            continue;
        if (gfd_is_color_code_like(value))
            continue;
        return value;
    }

    return gfd_join_non_empty({title, name, color, material}, " ");
}

bool gfd_json_has_any_key_recursive(const nlohmann::json& item, std::initializer_list<const char*> keys, int depth = 0)
{
    if (depth > 5 || !item.is_object())
        return false;

    for (const char* key : keys)
        if (item.find(key) != item.end())
            return true;

    for (auto it = item.begin(); it != item.end(); ++it) {
        const std::string normalized_key = gfd_compact_key_copy(it.key());
        for (const char* key : keys) {
            if (normalized_key == gfd_compact_key_copy(key))
                return true;
        }
        if (it.value().is_object() && gfd_json_has_any_key_recursive(it.value(), keys, depth + 1))
            return true;
        if (it.value().is_array()) {
            for (const nlohmann::json& child : it.value())
                if (gfd_json_has_any_key_recursive(child, keys, depth + 1))
                    return true;
        }
    }

    return false;
}

std::string gfd_lower_compact_copy(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string gfd_json_to_string(const nlohmann::json& value)
{
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_number_integer())
        return std::to_string(value.get<long long>());
    if (value.is_number_unsigned())
        return std::to_string(value.get<unsigned long long>());
    if (value.is_number_float())
        return (boost::format("%1%") % value.get<double>()).str();
    if (value.is_boolean())
        return value.get<bool>() ? "true" : "false";
    return {};
}

std::string gfd_json_first_string(const nlohmann::json& item, std::initializer_list<const char*> keys)
{
    if (!item.is_object())
        return {};
    for (const char* key : keys) {
        const auto it = item.find(key);
        if (it != item.end() && !it->is_null()) {
            const std::string value = gfd_json_to_string(*it);
            if (!value.empty())
                return value;
        }
    }
    return {};
}

const nlohmann::json* gfd_json_first_value(const nlohmann::json& item, std::initializer_list<const char*> keys)
{
    if (!item.is_object())
        return nullptr;

    for (const char* key : keys) {
        const auto it = item.find(key);
        if (it != item.end() && !it->is_null())
            return &*it;
    }

    return nullptr;
}

bool gfd_json_key_matches(const std::string& key, std::initializer_list<const char*> candidates)
{
    const std::string normalized_key = gfd_compact_key_copy(key);
    for (const char* candidate : candidates) {
        if (normalized_key == gfd_compact_key_copy(candidate))
            return true;
    }
    return false;
}

std::string gfd_json_first_string_recursive(const nlohmann::json& item, std::initializer_list<const char*> keys, int depth = 0)
{
    if (depth > 5 || !item.is_object())
        return {};

    for (const char* key : keys) {
        const auto it = item.find(key);
        if (it != item.end() && !it->is_null()) {
            const std::string value = gfd_json_to_string(*it);
            if (!value.empty())
                return value;
        }
    }

    for (auto it = item.begin(); it != item.end(); ++it) {
        if (gfd_json_key_matches(it.key(), keys)) {
            const std::string value = gfd_json_to_string(it.value());
            if (!value.empty())
                return value;
        }
    }

    for (auto it = item.begin(); it != item.end(); ++it) {
        if (it.value().is_object()) {
            const std::string value = gfd_json_first_string_recursive(it.value(), keys, depth + 1);
            if (!value.empty())
                return value;
        } else if (it.value().is_array()) {
            for (const nlohmann::json& child : it.value()) {
                const std::string value = gfd_json_first_string_recursive(child, keys, depth + 1);
                if (!value.empty())
                    return value;
            }
        }
    }

    return {};
}

std::string gfd_json_first_nested_string_recursive(const nlohmann::json& payload,
                                                   std::initializer_list<const char*> parent_keys,
                                                   std::initializer_list<const char*> child_keys,
                                                   int depth = 0)
{
    if (depth > 5 || payload.is_null())
        return {};

    if (payload.is_array()) {
        for (const nlohmann::json& item : payload) {
            const std::string value = gfd_json_first_nested_string_recursive(item, parent_keys, child_keys, depth + 1);
            if (!value.empty())
                return value;
        }
        return {};
    }

    if (!payload.is_object())
        return {};

    for (auto it = payload.begin(); it != payload.end(); ++it) {
        if (!gfd_json_key_matches(it.key(), parent_keys))
            continue;

        const std::string direct_value = gfd_json_to_string(it.value());
        if (!direct_value.empty())
            return direct_value;

        if (it.value().is_object()) {
            const std::string child_value = gfd_json_first_string_recursive(it.value(), child_keys);
            if (!child_value.empty())
                return child_value;
        } else if (it.value().is_array()) {
            for (const nlohmann::json& child : it.value()) {
                if (child.is_object()) {
                    const std::string child_value = gfd_json_first_string_recursive(child, child_keys);
                    if (!child_value.empty())
                        return child_value;
                }
            }
        }
    }

    for (auto it = payload.begin(); it != payload.end(); ++it) {
        if (it.value().is_object() || it.value().is_array()) {
            const std::string value = gfd_json_first_nested_string_recursive(it.value(), parent_keys, child_keys, depth + 1);
            if (!value.empty())
                return value;
        }
    }

    return {};
}

void gfd_collect_json_array_candidates(const nlohmann::json& payload, std::vector<nlohmann::json>& arrays, int depth = 0)
{
    if (depth > 8)
        return;
    if (payload.is_array()) {
        arrays.emplace_back(payload);
        for (const nlohmann::json& item : payload)
            gfd_collect_json_array_candidates(item, arrays, depth + 1);
        return;
    }
    if (!payload.is_object())
        return;

    for (const char* key : {"data", "result", "records", "rows", "list", "items"}) {
        const auto it = payload.find(key);
        if (it != payload.end())
            gfd_collect_json_array_candidates(*it, arrays, depth + 1);
    }
}

void gfd_collect_object_values(const nlohmann::json& payload, std::map<std::string, std::string>& values, int depth = 0)
{
    if (depth > 8 || payload.is_null())
        return;

    if (payload.is_array()) {
        for (const nlohmann::json& item : payload) {
            if (item.is_object()) {
                const std::string param = gfd_json_first_string(item, {"param", "key", "paramKey", "parameter", "parameterKey", "code"});
                const std::string value = gfd_json_first_string(item, {"value", "paramValue", "parameterValue", "defaultValue", "default"});
                if (!param.empty() && !value.empty())
                    values[param] = value;
            }
            gfd_collect_object_values(item, values, depth + 1);
        }
        return;
    }

    if (payload.is_string()) {
        const std::string text = payload.get<std::string>();
        std::vector<std::string> lines;
        boost::split(lines, text, boost::is_any_of("\n"));
        for (std::string line : lines) {
            boost::algorithm::trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
                continue;

            std::vector<std::string> parts;
            boost::split(parts, line, boost::is_any_of("|"));
            if (parts.size() >= 3) {
                boost::algorithm::trim(parts[0]);
                std::string value = boost::algorithm::join(std::vector<std::string>(parts.begin() + 2, parts.end()), "|");
                boost::algorithm::trim(value);
                values[parts[0]] = value;
                continue;
            }

            const size_t eq = line.find('=');
            if (eq != std::string::npos && eq > 0) {
                std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);
                boost::algorithm::trim(key);
                boost::algorithm::trim(value);
                values[key] = value;
            }
        }
        return;
    }

    if (!payload.is_object())
        return;

    for (const char* key : {"data", "result", "params", "dynamicParams", "parameters", "paramList", "list", "records", "rows"}) {
        const auto it = payload.find(key);
        if (it != payload.end())
            gfd_collect_object_values(*it, values, depth + 1);
    }

    for (auto it = payload.begin(); it != payload.end(); ++it) {
        if (!it.value().is_object() && !it.value().is_array() && !it.value().is_null()) {
            const std::string value = gfd_json_to_string(it.value());
            if (!value.empty())
                values[it.key()] = value;
        }
    }
}

void gfd_collect_slice_param_values(const std::string& contents, std::map<std::string, std::string>& values)
{
    static const std::regex pattern("-s\\s+([^=\\s]+)=(\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'|[^\\s]+)");
    for (std::sregex_iterator it(contents.begin(), contents.end(), pattern), end; it != end; ++it) {
        std::string value = (*it)[2].str();
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
            value = value.substr(1, value.size() - 2);
        boost::replace_all(value, "\\\"", "\"");
        boost::replace_all(value, "\\\\", "\\");
        values[(*it)[1].str()] = value;
    }
}

void gfd_collect_orca_param_values(const nlohmann::json& payload, std::map<std::string, std::string>& values)
{
    if (!payload.is_object())
        return;

    for (auto it = payload.begin(); it != payload.end(); ++it) {
        if (it.value().is_array()) {
            if (it.value().empty() || it.value().front().is_null()) {
                values[it.key()] = "nil";
                continue;
            }
            const std::string value = gfd_json_to_string(it.value().front());
            values[it.key()]       = value.empty() ? "nil" : value;
            continue;
        }

        if (it.value().is_null()) {
            values[it.key()] = "nil";
            continue;
        }

        const std::string value = gfd_json_to_string(it.value());
        if (!value.empty())
            values[it.key()] = value;
    }
}

void gfd_collect_orca_param_values(const std::string& contents, std::map<std::string, std::string>& values)
{
    if (gfd_trim_copy(contents).empty())
        return;

    gfd_collect_orca_param_values(nlohmann::json::parse(contents), values);
}

void gfd_collect_orca_param_json(const nlohmann::json& payload, nlohmann::json& values)
{
    if (!payload.is_object())
        return;

    for (auto it = payload.begin(); it != payload.end(); ++it)
        values[it.key()] = it.value();
}

void gfd_collect_orca_param_json(const std::string& contents, nlohmann::json& values)
{
    if (gfd_trim_copy(contents).empty())
        return;

    gfd_collect_orca_param_json(nlohmann::json::parse(contents), values);
}

nlohmann::json gfd_config_to_json(const DynamicPrintConfig& config,
                                  const std::string&        name,
                                  const std::string&        from,
                                  const std::string&        type)
{
    nlohmann::json payload = nlohmann::json::object();
    payload[BBL_JSON_KEY_TYPE] = type;
    payload[BBL_JSON_KEY_NAME] = name;
    payload[BBL_JSON_KEY_FROM] = from;
    payload[BBL_JSON_KEY_INSTANTIATION] = "true";

    for (const std::string& opt_key : config.keys()) {
        const ConfigOption* opt = config.option(opt_key);
        if (opt == nullptr)
            continue;

        if (opt->is_scalar()) {
            if (opt->type() == coString && opt_key != "bed_custom_texture" && opt_key != "bed_custom_model")
                payload[opt_key] = static_cast<const ConfigOptionString*>(opt)->value;
            else
                payload[opt_key] = opt->serialize();
        } else {
            const ConfigOptionVectorBase* vec = static_cast<const ConfigOptionVectorBase*>(opt);
            payload[opt_key] = vec->vserialize();
        }
    }

    return payload;
}

DynamicPrintConfig gfd_resolve_preset_config(const PresetCollection& presets, const Preset& preset)
{
    const Preset* parent = presets.get_preset_parent(preset);
    if (parent == nullptr)
        return preset.config;

    DynamicPrintConfig resolved = gfd_resolve_preset_config(presets, *parent);
    for (const std::string& opt_key : preset.config.diff(parent->config)) {
        const ConfigOption* opt_src = preset.config.option(opt_key);
        if (opt_src == nullptr)
            continue;
        ConfigOption* opt_dst = resolved.option(opt_key, true);
        opt_dst->set(opt_src);
    }

    return resolved;
}

const nlohmann::json* gfd_extract_detail_data(const nlohmann::json& payload)
{
    const nlohmann::json* current = &payload;
    for (int depth = 0; current != nullptr && depth < 8; ++depth) {
        if (current->is_object()) {
            if (const auto it = current->find("data"); it != current->end() && !it->is_null()) {
                current = &*it;
                continue;
            }
            if (const auto it = current->find("result"); it != current->end() && !it->is_null()) {
                current = &*it;
                continue;
            }
            return current;
        }
        if (current->is_array())
            current = current->empty() ? nullptr : &current->front();
        else
            return current;
    }
    return current;
}

class GFDDynamicFilamentTab : public TabFilament
{
public:
    GFDDynamicFilamentTab(ParamsPanel* parent, PresetBundle* preset_bundle)
        : TabFilament(parent)
    {
        set_preset_bundle_override(preset_bundle);
        set_detached_from_app_state(true);
    }

    wxBoxSizer* top_sizer() const { return m_top_sizer; }
    wxPanel* top_panel() const { return m_top_panel; }
    wxWindow* preset_choice() const { return m_presets_choice; }
    wxWindow* undo_button() const { return m_undo_btn; }
    wxWindow* save_preset_button() const { return m_btn_save_preset; }
    wxWindow* delete_preset_button() const { return m_btn_delete_preset; }
    wxWindow* search_button() const { return m_btn_search; }
    wxWindow* search_item() const { return m_search_item; }
    wxWindow* static_title() const { return m_static_title; }
    wxWindow* mode_view() const { return m_mode_view; }

    void activate_selected_page(std::function<void()> throw_if_canceled) override
    {
        if (!m_active_page)
            return;

        m_active_page->activate(m_mode, throw_if_canceled);
        update_description_lines();
        if (m_active_page && !(m_active_page->title() == "Dependencies"))
            toggle_options();
        m_active_page->update_visibility(m_mode, true);
    }
};

class GFDDynamicMaterialDialog : public DPIDialog
{
public:
    GFDDynamicMaterialDialog(wxWindow* parent, Plater* plater)
        : DPIDialog(parent, wxID_ANY, _L("材料设置"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
        , m_plater(plater)
    {
        m_bundle = wxGetApp().preset_bundle != nullptr ? std::make_unique<PresetBundle>(*wxGetApp().preset_bundle) : std::make_unique<PresetBundle>();

        build();
        bind_events();
        wxGetApp().UpdateDlgDarkUI(this);

        load_device_types();
        refresh_filament_options();
    }

private:
    Plater*                       m_plater{nullptr};
    std::unique_ptr<PresetBundle> m_bundle;
    ParamsPanel*                  m_panel{nullptr};
    GFDDynamicFilamentTab*        m_filament_tab{nullptr};
    wxComboBox*                   m_filament_choice{nullptr};
    wxChoice*                     m_device_choice{nullptr};
    wxStaticText*                 m_sn_label{nullptr};
    wxStaticText*                 m_tip_label{nullptr};
    Button*                       m_save_button{nullptr};

    std::vector<GFDDynamicFilamentOption> m_filaments;
    nlohmann::json                        m_current_detail;
    std::string                           m_selected_sn;

    void build()
    {
        SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        SetMinSize(wxSize(FromDIP(1040), FromDIP(650)));
        SetSize(wxSize(FromDIP(1120), FromDIP(720)));

        m_panel = new ParamsPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
        m_panel->set_dialog_title_enabled(false);
        m_panel->set_skip_missing_global_mode_region(true);

        m_filament_tab = new GFDDynamicFilamentTab(m_panel, m_bundle.get());
        m_filament_tab->create_preset_tab();
        m_panel->rebuild_panels();
        m_panel->set_active_tab(m_filament_tab);
        detach_tab_from_app_list();

        customize_top_row();

        auto* main_sizer = new wxBoxSizer(wxVERTICAL);
        main_sizer->Add(m_panel, 1, wxEXPAND | wxALL, 0);

        m_tip_label = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
        m_tip_label->SetFont(::Label::Body_12);
        m_tip_label->SetForegroundColour(wxColour(220, 38, 38));
        main_sizer->Add(m_tip_label, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(16));

        SetSizer(main_sizer);
        Layout();
        CentreOnParent();
        m_filament_tab->OnActivate();
    }

    void detach_tab_from_app_list()
    {
        auto& tabs = wxGetApp().tabs_list;
        tabs.erase(std::remove(tabs.begin(), tabs.end(), m_filament_tab), tabs.end());
    }

    void customize_top_row()
    {
        wxBoxSizer* top_sizer = m_filament_tab != nullptr ? m_filament_tab->top_sizer() : nullptr;
        wxPanel*    top_panel = m_filament_tab != nullptr ? m_filament_tab->top_panel() : nullptr;
        if (top_sizer == nullptr || top_panel == nullptr)
            return;

        hide_top_child(m_filament_tab->preset_choice(), false);
        hide_top_child(m_filament_tab->save_preset_button(), false);
        hide_top_child(m_filament_tab->delete_preset_button(), false);
        hide_top_child(m_filament_tab->static_title(), false);
        hide_top_child(m_filament_tab->mode_view(), false);
        hide_top_child(m_filament_tab->search_button(), false);
        hide_top_child(m_filament_tab->search_item(), false);

        top_sizer->Clear(false);
        top_sizer->AddSpacer(FromDIP(SidebarProps::ContentMargin()));

        top_sizer->Add(m_filament_tab->undo_button(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

        auto* filament_label = new wxStaticText(top_panel, wxID_ANY, _L("耗材"));
        filament_label->SetFont(::Label::Body_13);
        top_sizer->Add(filament_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));

        m_filament_choice = new wxComboBox(top_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(280), FromDIP(30)), 0, nullptr, wxCB_READONLY);
        configure_filament_dropdown();
        top_sizer->Add(m_filament_choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(18));

        auto* device_label = new wxStaticText(top_panel, wxID_ANY, _L("机型"));
        device_label->SetFont(::Label::Body_13);
        top_sizer->Add(device_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        m_device_choice = new wxChoice(top_panel, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(96), FromDIP(30)));
        top_sizer->Add(m_device_choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(18));

        auto* sn_caption = new wxStaticText(top_panel, wxID_ANY, _L("耗材SN:"));
        sn_caption->SetFont(::Label::Body_13);
        top_sizer->Add(sn_caption, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        m_sn_label = new wxStaticText(top_panel, wxID_ANY, _L("-"), wxDefaultPosition, wxSize(FromDIP(230), -1), wxST_ELLIPSIZE_END);
        m_sn_label->SetFont(::Label::Body_13);
        top_sizer->Add(m_sn_label, 0, wxALIGN_CENTER_VERTICAL);
        top_sizer->AddStretchSpacer(1);

        m_save_button = new Button(top_panel, _L("保存"));
        apply_dialog_action_button_style(m_save_button, ButtonStyle::Confirm, wxSize(FromDIP(72), FromDIP(30)));
        top_sizer->Add(m_save_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
        top_sizer->AddSpacer(FromDIP(SidebarProps::ContentMargin()));
        top_panel->Layout();
    }

    void hide_top_child(wxWindow* child, bool delete_window)
    {
        if (child == nullptr || m_filament_tab == nullptr || m_filament_tab->top_sizer() == nullptr)
            return;
        m_filament_tab->top_sizer()->Detach(child);
        child->Hide();
        if (delete_window)
            child->Destroy();
    }

    void bind_events()
    {
        if (m_save_button != nullptr)
            m_save_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { save_slice_params(); });
        if (m_filament_choice != nullptr)
            m_filament_choice->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { on_filament_changed(true); });
        if (m_device_choice != nullptr)
            m_device_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { apply_detail_for_selected_device(); });
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { EndModal(wxID_CANCEL); });
    }

    void configure_filament_dropdown()
    {
#ifdef _WIN32
        if (m_filament_choice != nullptr) {
            ::SendMessage((HWND) m_filament_choice->GetHandle(), CB_SETMINVISIBLE, 10, 0);
            ::SendMessage((HWND) m_filament_choice->GetHandle(), CB_SETITEMHEIGHT, 0, FromDIP(28));
        }
#endif
    }

    void set_tip(const wxString& text)
    {
        if (m_tip_label != nullptr)
            m_tip_label->SetLabel(text);
        Layout();
    }

    void load_device_types()
    {
        std::vector<std::string> device_types = GFD::Config::local_gfd_device_types();
        std::string current_device_type;
        if (wxGetApp().preset_bundle != nullptr)
            current_device_type = GFD::Config::current_device_type(wxGetApp().preset_bundle->printers.get_selected_preset().config);
        if (!current_device_type.empty() &&
            std::find(device_types.begin(), device_types.end(), current_device_type) == device_types.end())
            device_types.insert(device_types.begin(), current_device_type);
        if (device_types.empty())
            device_types = {"EP3", "EP3Pro", "EP3Plus", "EP7"};

        m_device_choice->Clear();
        for (const std::string& device_type : device_types)
            m_device_choice->Append(from_u8(device_type));

        if (!current_device_type.empty())
            m_device_choice->SetStringSelection(from_u8(current_device_type));
        if (m_device_choice->GetSelection() == wxNOT_FOUND && !device_types.empty())
            m_device_choice->SetSelection(0);
    }

    std::string selected_device_type() const
    {
        return m_device_choice != nullptr && m_device_choice->GetSelection() != wxNOT_FOUND ?
                   into_u8(m_device_choice->GetStringSelection()) :
                   std::string("EP3");
    }

    int find_initial_filament_index() const
    {
        if (m_filaments.empty())
            return wxNOT_FOUND;
        if (wxGetApp().preset_bundle == nullptr)
            return 0;

        const std::string selected_filament_name = !wxGetApp().preset_bundle->filament_presets.empty() ?
                                                       wxGetApp().preset_bundle->filament_presets.front() :
                                                       wxGetApp().preset_bundle->filaments.get_selected_preset_name();
        const std::string normalized_selected = gfd_lower_compact_copy(Preset::remove_suffix_modified(selected_filament_name));
        if (normalized_selected.empty())
            return 0;

        for (size_t index = 0; index < m_filaments.size(); ++index) {
            const GFDDynamicFilamentOption& item = m_filaments[index];
            const std::string option_text = gfd_lower_compact_copy(item.text + item.name + item.material);
            if (!option_text.empty() && (option_text.find(normalized_selected) != std::string::npos ||
                                         normalized_selected.find(gfd_lower_compact_copy(item.name)) != std::string::npos))
                return static_cast<int>(index);
        }

        return 0;
    }

    bool response_success_or_message(const std::string& body, std::string& message) const
    {
        try {
            const nlohmann::json response = nlohmann::json::parse(body);
            const int code = response.value("code", 0);
            const std::string msg = response.value("msg", std::string());
            message = msg;
            return msg == "success" || msg.empty() || code == 0 || code == 200;
        } catch (...) {
            return true;
        }
    }

    GFDDynamicFilamentOption option_from_json(const nlohmann::json& item) const
    {
        GFDDynamicFilamentOption option;
        option.sn       = gfd_json_first_string_recursive(item, {"sn", "filamentSn", "materialSn", "serialNo", "serialNumber"});
        option.id       = gfd_json_first_string_recursive(item, {"id", "rootId", "rootMaterialId", "materialId", "filamentId"});
        option.color    = gfd_json_first_string_recursive(item, {"colorTitle"});
        option.barcode  = gfd_json_first_string_recursive(item, {"barcode"});
        option.text     = gfd_filament_display_text(option.color, option.barcode);
        if (option.text.empty()) {
            option.barcode  = gfd_json_first_string_recursive(item, {"barCode", "bar_code", "barCodeNo", "barcodeNo"});
            option.color    = gfd_json_first_string_recursive(item, {"colorName", "colourName", "colorCn", "colourCn", "color", "colour"});
            option.title    = gfd_best_filament_title({}, {}, option.color, {}, option.barcode, option.sn, option.id);
            option.text     = gfd_filament_display_text(option.title, option.barcode);
        }
        if (option.text.empty())
            option.text = !option.sn.empty() ? option.sn : option.id;
        return option;
    }

    void parse_filament_options(const std::string& body)
    {
        m_filaments.clear();
        const nlohmann::json response = nlohmann::json::parse(body);
        std::vector<nlohmann::json> arrays;
        gfd_collect_json_array_candidates(response, arrays);

        std::vector<GFDDynamicFilamentOption> best_filaments;
        int                                   best_score = -1;
        for (const nlohmann::json& array : arrays) {
            if (!array.is_array())
                continue;
            std::vector<GFDDynamicFilamentOption> options;
            int                                   array_score = 0;
            std::set<std::string>                 seen;
            for (const nlohmann::json& item : array) {
                if (!item.is_object())
                    continue;
                GFDDynamicFilamentOption option = option_from_json(item);
                if (option.sn.empty() && option.id.empty() && option.text.empty())
                    continue;
                const std::string key = !option.sn.empty() ? option.sn : (!option.id.empty() ? option.id : option.text);
                if (!seen.insert(key).second)
                    continue;
                array_score += gfd_filament_option_score(option);
                if (gfd_json_has_any_key_recursive(item, {"colorTitle"}))
                    array_score += 50;
                if (gfd_json_has_any_key_recursive(item, {"barcode"}))
                    array_score += 50;
                options.emplace_back(std::move(option));
            }
            if (!options.empty() && array_score > best_score) {
                best_score     = array_score;
                best_filaments = std::move(options);
            }
        }
        m_filaments = std::move(best_filaments);
    }

    void refresh_filament_options()
    {
        if (m_plater == nullptr) {
            set_tip(_L("Plater 未初始化"));
            return;
        }

        set_tip(_L("正在获取耗材列表..."));
        std::string body;
        std::string error_message;
        if (!m_plater->fetch_dynamic_filament_list(body, error_message)) {
            set_tip(from_u8(error_message.empty() ? "获取耗材列表失败" : error_message));
            return;
        }

        std::string response_message;
        if (!response_success_or_message(body, response_message)) {
            set_tip(from_u8(response_message.empty() ? "获取耗材列表失败" : response_message));
            return;
        }

        try {
            parse_filament_options(body);
        } catch (const std::exception& ex) {
            set_tip(from_u8(std::string("解析耗材列表失败: ") + ex.what()));
            return;
        }

        m_filament_choice->Clear();
        for (const GFDDynamicFilamentOption& option : m_filaments)
            m_filament_choice->Append(from_u8(option.text), new wxStringClientData(from_u8(option.sn)));

        const int initial_index = find_initial_filament_index();
        if (initial_index != wxNOT_FOUND)
            m_filament_choice->SetSelection(initial_index);
        else
            set_tip(_L("暂无耗材"));

        on_filament_changed(true);
    }

    void on_filament_changed(bool force_fetch)
    {
        const int selection = m_filament_choice != nullptr ? m_filament_choice->GetSelection() : wxNOT_FOUND;
        if (selection == wxNOT_FOUND || selection < 0 || selection >= static_cast<int>(m_filaments.size())) {
            m_selected_sn.clear();
            if (m_sn_label != nullptr)
                m_sn_label->SetLabel(_L("-"));
            return;
        }

        const GFDDynamicFilamentOption& option = m_filaments[selection];
        std::string option_sn = option.sn;
        if (auto* data = static_cast<wxStringClientData*>(m_filament_choice->GetClientObject(selection)))
            option_sn = into_u8(data->GetData());

        if (!force_fetch && option_sn == m_selected_sn)
            return;

        m_selected_sn = option_sn;
        if (m_sn_label != nullptr) {
            m_sn_label->SetLabel(m_selected_sn.empty() ? _L("-") : from_u8(m_selected_sn));
            m_sn_label->SetToolTip(m_sn_label->GetLabel());
        }

        reset_filament_to_base_config();

        if (m_selected_sn.empty()) {
            set_tip(_L("当前耗材缺少 SN，无法获取详情"));
            return;
        }

        std::string body;
        std::string error_message;
        set_tip(_L("正在获取动态参数..."));
        if (m_plater == nullptr || !m_plater->fetch_dynamic_filament_detail(m_selected_sn, body, error_message)) {
            set_tip(from_u8(error_message.empty() ? "获取动态参数失败" : error_message));
            return;
        }

        std::string response_message;
        if (!response_success_or_message(body, response_message)) {
            set_tip(from_u8(response_message.empty() ? "获取动态参数失败" : response_message));
            return;
        }

        try {
            m_current_detail = nlohmann::json::parse(body);
        } catch (const std::exception& ex) {
            m_current_detail = nlohmann::json();
            set_tip(from_u8(std::string("解析动态参数失败: ") + ex.what()));
            return;
        }

        apply_detail_for_selected_device();
    }

    void apply_detail_for_selected_device()
    {
        reset_filament_to_base_config();
        if (m_current_detail.is_null())
        {
            set_tip(_L("未查询到当前耗材动态参数，已使用当前耗材参数"));
            return;
        }

        const nlohmann::json* detail = gfd_extract_detail_data(m_current_detail);
        if (detail == nullptr || !detail->is_object()) {
            set_tip(_L("未查询到当前耗材动态参数，已使用当前耗材参数"));
            return;
        }

        const std::string device_type = selected_device_type();
        const auto device_temps_it = detail->find("deviceTypeTemps");
        if (device_temps_it == detail->end() || !device_temps_it->is_object()) {
            set_tip(_L("未查询到当前耗材动态参数，已使用当前耗材参数"));
            return;
        }

        const auto device_it = device_temps_it->find(device_type);
        if (device_it == device_temps_it->end() || !device_it->is_object()) {
            set_tip(_L("未查询到当前耗材动态参数，已使用当前耗材参数"));
            return;
        }

        const nlohmann::json* slice_param = gfd_json_first_value(*device_it, {"orcaSliceParam", "sliceParam"});
        if (slice_param == nullptr) {
            set_tip(_L("未查询到当前耗材动态参数，已使用当前耗材参数"));
            return;
        }

        nlohmann::json values = nlohmann::json::object();
        try {
            if (slice_param->is_string())
                gfd_collect_orca_param_json(slice_param->get<std::string>(), values);
            else
                gfd_collect_orca_param_json(*slice_param, values);
        } catch (const std::exception& ex) {
            set_tip(from_u8(std::string("解析 Orca 参数失败: ") + ex.what()));
            return;
        }

        apply_orca_json_to_filament(values);
        set_tip(wxEmptyString);
    }

    void reset_filament_to_base_config()
    {
        if (m_bundle == nullptr || m_filament_tab == nullptr)
            return;

        if (wxGetApp().preset_bundle != nullptr)
            *m_bundle = *wxGetApp().preset_bundle;
        m_bundle->filaments.update_saved_preset_from_current_preset();
        m_filament_tab->m_preset_bundle = m_bundle.get();
        m_filament_tab->m_presets       = &m_bundle->filaments;
        m_filament_tab->m_config        = &m_bundle->filaments.get_edited_preset().config;
        m_filament_tab->load_current_preset();
    }

    std::string value_for_deserialize(const nlohmann::json& value, bool is_strings) const
    {
        if (value.is_null())
            return "nil";
        if (value.is_string())
            return is_strings ? ("\"" + escape_string_cstyle(value.get<std::string>()) + "\"") : value.get<std::string>();
        if (value.is_boolean())
            return value.get<bool>() ? "1" : "0";
        if (value.is_number_integer())
            return std::to_string(value.get<long long>());
        if (value.is_number_unsigned())
            return std::to_string(value.get<unsigned long long>());
        if (value.is_number_float())
            return (boost::format("%1%") % value.get<double>()).str();
        return {};
    }

    std::string json_option_to_deserialize_string(const std::string& opt_key, const nlohmann::json& value) const
    {
        const ConfigOption* opt = m_bundle != nullptr ? m_bundle->filaments.get_edited_preset().config.option(opt_key) : nullptr;
        const bool is_strings = opt != nullptr && opt->type() == coStrings;

        if (!value.is_array())
            return value_for_deserialize(value, is_strings);

        std::vector<std::string> parts;
        for (const nlohmann::json& item : value)
            parts.emplace_back(value_for_deserialize(item, is_strings));
        return boost::algorithm::join(parts, is_strings ? ";" : ",");
    }

    void apply_orca_json_to_filament(const nlohmann::json& values)
    {
        if (m_bundle == nullptr || m_filament_tab == nullptr || !values.is_object())
            return;

        Preset&             edited_preset = m_bundle->filaments.get_edited_preset();
        DynamicPrintConfig& config        = edited_preset.config;
        ConfigSubstitutionContext substitutions{ForwardCompatibilitySubstitutionRule::Disable};
        std::vector<std::string> skipped_keys;

        if (const auto it = values.find(BBL_JSON_KEY_SETTING_ID); it != values.end() && it->is_string())
            edited_preset.setting_id = it->get<std::string>();
        if (const auto it = values.find(BBL_JSON_KEY_FILAMENT_ID); it != values.end() && it->is_string())
            edited_preset.filament_id = it->get<std::string>();

        for (auto it = values.begin(); it != values.end(); ++it) {
            const std::string opt_key = it.key();
            if (opt_key == BBL_JSON_KEY_TYPE || opt_key == BBL_JSON_KEY_NAME || opt_key == BBL_JSON_KEY_FROM ||
                opt_key == BBL_JSON_KEY_VERSION || opt_key == BBL_JSON_KEY_INSTANTIATION ||
                opt_key == BBL_JSON_KEY_SETTING_ID || opt_key == BBL_JSON_KEY_FILAMENT_ID)
                continue;
            try {
                config.set_deserialize(opt_key, json_option_to_deserialize_string(opt_key, it.value()), substitutions);
            } catch (const std::exception&) {
                skipped_keys.emplace_back(opt_key);
            }
        }

        if (const auto it = values.find(BBL_JSON_KEY_NAME); it != values.end() && it->is_string())
            config.option<ConfigOptionStrings>("filament_settings_id", true)->values = {it->get<std::string>()};

        m_bundle->filaments.update_saved_preset_from_current_preset();
        m_filament_tab->load_current_preset();

        if (!skipped_keys.empty())
            set_tip(from_u8((boost::format("部分参数未识别，已跳过 %1% 项") % skipped_keys.size()).str()));
    }

    std::string build_orca_slice_param_json() const
    {
        if (m_bundle == nullptr)
            return "{}";

        const Preset& preset = m_bundle->filaments.get_edited_preset();
        DynamicPrintConfig resolved_config = gfd_resolve_preset_config(m_bundle->filaments, preset);
        std::string name = preset.name;
        if (const ConfigOptionStrings* settings_id = preset.config.option<ConfigOptionStrings>("filament_settings_id");
            settings_id != nullptr && !settings_id->values.empty() && !settings_id->values.front().empty())
            name = settings_id->values.front();

        nlohmann::json payload = gfd_config_to_json(resolved_config, name, "system", "filament");
        if (!preset.setting_id.empty())
            payload[BBL_JSON_KEY_SETTING_ID] = preset.setting_id;
        if (!preset.filament_id.empty())
            payload[BBL_JSON_KEY_FILAMENT_ID] = preset.filament_id;
        return payload.dump();
    }

    void save_slice_params()
    {
        if (m_selected_sn.empty()) {
            set_tip(_L("请选择耗材"));
            return;
        }

        std::string body;
        std::string error_message;
        set_tip(_L("正在保存..."));
        if (m_save_button != nullptr)
            m_save_button->Enable(false);
        const bool ok = m_plater != nullptr &&
                        m_plater->update_dynamic_filament_slice_param(m_selected_sn, selected_device_type(), build_orca_slice_param_json(), body, error_message);
        if (m_save_button != nullptr)
            m_save_button->Enable(true);

        if (!ok) {
            set_tip(from_u8(error_message.empty() ? "保存失败" : error_message));
            return;
        }

        std::string response_message;
        if (!response_success_or_message(body, response_message)) {
            set_tip(from_u8(response_message.empty() ? "保存失败" : response_message));
            return;
        }

        set_tip(wxEmptyString);
        GUI::show_info(this, _L("保存成功"), _L("提示"));
    }

    void on_dpi_changed(const wxRect& suggested_rect) override
    {
        Fit();
        SetSize(wxSize(FromDIP(1120), FromDIP(720)));
        if (m_panel != nullptr)
            m_panel->msw_rescale();
        Refresh();
    }
};

class GFDDynamicParamsDialog : public wxDialog
{
public:
    GFDDynamicParamsDialog(wxWindow* parent, Plater* plater)
        : wxDialog(parent, wxID_ANY, _L("动态参数"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
        , m_plater(plater)
    {
        build();
        bind_events();
        wxGetApp().UpdateDlgDarkUI(this);
        load_device_types();
        load_default_dynamic_params();
        refresh_filament_options();
    }

private:
    Plater*           m_plater{nullptr};
    wxComboBox*       m_filament_choice{nullptr};
    wxChoice*         m_device_choice{nullptr};
    wxStaticText*     m_sn_label{nullptr};
    wxGrid*           m_grid{nullptr};
    wxStaticText*     m_tip_label{nullptr};
    Button*           m_save_button{nullptr};
    Button*           m_cancel_button{nullptr};

    std::vector<GFDDynamicFilamentOption> m_filaments;
    std::vector<GFDDynamicParamRow>       m_params;
    nlohmann::json                        m_current_detail;
    std::string                           m_selected_sn;
    std::string                           m_load_error;

    void build()
    {
        SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        SetMinSize(wxSize(FromDIP(860), FromDIP(500)));
        SetSize(wxSize(FromDIP(860), FromDIP(500)));

        auto* main_sizer = new wxBoxSizer(wxVERTICAL);
        main_sizer->AddSpacer(FromDIP(18));

        auto* filter_row = new wxBoxSizer(wxHORIZONTAL);
        auto* filament_label = new wxStaticText(this, wxID_ANY, _L("耗材"));
        filament_label->SetFont(::Label::Body_13);
        filter_row->Add(filament_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        m_filament_choice = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(280), FromDIP(30)), 0, nullptr, wxCB_READONLY);
        configure_filament_dropdown();
        filter_row->Add(m_filament_choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(18));

        auto* device_label = new wxStaticText(this, wxID_ANY, _L("机型"));
        device_label->SetFont(::Label::Body_13);
        filter_row->Add(device_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        m_device_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(92), FromDIP(30)));
        filter_row->Add(m_device_choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(18));

        auto* sn_caption = new wxStaticText(this, wxID_ANY, _L("耗材SN:"));
        sn_caption->SetFont(::Label::Body_13);
        filter_row->Add(sn_caption, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        m_sn_label = new wxStaticText(this, wxID_ANY, _L("-"), wxDefaultPosition, wxSize(FromDIP(270), -1), 0);
        m_sn_label->SetFont(::Label::Body_13);
        filter_row->Add(m_sn_label, 0, wxALIGN_CENTER_VERTICAL);
        filter_row->AddStretchSpacer(1);
        main_sizer->Add(filter_row, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(22));

        main_sizer->AddSpacer(FromDIP(14));

        m_grid = new wxGrid(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(310)), wxBORDER_SIMPLE);
        m_grid->CreateGrid(0, 3);
        m_grid->SetColLabelValue(0, _L("参数名"));
        m_grid->SetColLabelValue(1, _L("参数"));
        m_grid->SetColLabelValue(2, _L("参数值"));
        m_grid->SetColLabelSize(FromDIP(24));
        m_grid->SetRowLabelSize(0);
        m_grid->SetDefaultRowSize(FromDIP(30), true);
        m_grid->EnableDragGridSize(false);
        m_grid->EnableDragRowSize(false);
        m_grid->SetCellHighlightPenWidth(1);
        update_grid_column_widths();
        main_sizer->Add(m_grid, 1, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(22));

        main_sizer->AddSpacer(FromDIP(10));
        m_tip_label = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
        m_tip_label->SetFont(::Label::Body_12);
        m_tip_label->SetForegroundColour(wxColour(220, 38, 38));
        main_sizer->Add(m_tip_label, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(22));

        auto* button_row = new wxBoxSizer(wxHORIZONTAL);
        button_row->AddStretchSpacer(1);
        const wxSize action_button_size(FromDIP(70), FromDIP(32));
        m_cancel_button = new Button(this, _L("取消"));
        apply_dialog_action_button_style(m_cancel_button, ButtonStyle::Regular, action_button_size);
        button_row->Add(m_cancel_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
        m_save_button = new Button(this, _L("保存"));
        apply_dialog_action_button_style(m_save_button, ButtonStyle::Confirm, action_button_size);
        button_row->Add(m_save_button, 0, wxALIGN_CENTER_VERTICAL);
        main_sizer->Add(button_row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(22));

        SetSizer(main_sizer);
        Layout();
        CentreOnParent();
    }

    void bind_events()
    {
        m_cancel_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
        m_save_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { save_slice_params(); });
        m_filament_choice->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { on_filament_changed(true); });
        m_device_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { apply_detail_for_selected_device(); });
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            update_grid_column_widths();
        });
    }

    void update_grid_column_widths()
    {
        if (m_grid == nullptr)
            return;

        const int available_width = std::max(FromDIP(360), m_grid->GetClientSize().x - FromDIP(4));
        const int name_width      = available_width * 35 / 100;
        const int param_width     = available_width * 40 / 100;
        const int value_width     = std::max(FromDIP(120), available_width - name_width - param_width);

        m_grid->SetColSize(0, name_width);
        m_grid->SetColSize(1, param_width);
        m_grid->SetColSize(2, value_width);
    }

    void configure_filament_dropdown()
    {
#ifdef _WIN32
        if (m_filament_choice != nullptr) {
            ::SendMessage((HWND) m_filament_choice->GetHandle(), CB_SETMINVISIBLE, 10, 0);
            ::SendMessage((HWND) m_filament_choice->GetHandle(), CB_SETITEMHEIGHT, 0, FromDIP(28));
        }
#endif
    }

    void load_device_types()
    {
        std::vector<std::string> device_types = GFD::Config::local_gfd_device_types();
        std::string current_device_type;
        if (wxGetApp().preset_bundle != nullptr)
            current_device_type = GFD::Config::current_device_type(wxGetApp().preset_bundle->printers.get_selected_preset().config);
        if (!current_device_type.empty() &&
            std::find(device_types.begin(), device_types.end(), current_device_type) == device_types.end())
            device_types.insert(device_types.begin(), current_device_type);
        if (device_types.empty())
            device_types = {"EP3", "EP3Pro", "EP3Plus", "EP7"};

        m_device_choice->Clear();
        for (const std::string& device_type : device_types)
            m_device_choice->Append(from_u8(device_type));

        if (!current_device_type.empty())
            m_device_choice->SetStringSelection(from_u8(current_device_type));
        if (m_device_choice->GetSelection() == wxNOT_FOUND && !device_types.empty())
            m_device_choice->SetSelection(0);
    }

    std::string selected_device_type() const
    {
        return m_device_choice != nullptr && m_device_choice->GetSelection() != wxNOT_FOUND ?
                   into_u8(m_device_choice->GetStringSelection()) :
                   std::string("EP3");
    }

    int find_initial_filament_index() const
    {
        if (m_filaments.empty())
            return wxNOT_FOUND;
        if (wxGetApp().preset_bundle == nullptr)
            return 0;

        const std::string selected_filament_name = !wxGetApp().preset_bundle->filament_presets.empty() ?
                                                       wxGetApp().preset_bundle->filament_presets.front() :
                                                       wxGetApp().preset_bundle->filaments.get_selected_preset_name();
        const std::string normalized_selected = gfd_lower_compact_copy(Preset::remove_suffix_modified(selected_filament_name));
        if (normalized_selected.empty())
            return 0;

        for (size_t index = 0; index < m_filaments.size(); ++index) {
            const GFDDynamicFilamentOption& item = m_filaments[index];
            const std::string option_text = gfd_lower_compact_copy(item.text + item.name + item.material);
            if (!option_text.empty() && (option_text.find(normalized_selected) != std::string::npos ||
                                         normalized_selected.find(gfd_lower_compact_copy(item.name)) != std::string::npos))
                return static_cast<int>(index);
        }

        return 0;
    }

    void set_tip(const wxString& text)
    {
        if (m_tip_label != nullptr)
            m_tip_label->SetLabel(text);
        Layout();
    }

    fs::path dynamic_params_config_path() const
    {
        return (fs::path(resources_dir()) / "param_dynamic.cfg").make_preferred();
    }

    void load_default_dynamic_params()
    {
        m_params.clear();
        m_load_error.clear();

        const fs::path config_path = dynamic_params_config_path();
        std::string contents;
        try {
            load_string_file(config_path, contents);
        } catch (const std::exception& ex) {
            m_load_error = (boost::format("配置文件读取失败：%1% (%2%)") % config_path.string() % ex.what()).str();
            rebuild_grid();
            return;
        }

        std::vector<std::string> lines;
        boost::split(lines, contents, boost::is_any_of("\n"));
        for (std::string line : lines) {
            boost::algorithm::trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;
            if (line.front() == '[' && line.back() == ']')
                continue;

            std::vector<std::string> parts;
            boost::split(parts, line, boost::is_any_of("|"));
            if (parts.size() < 3)
                continue;
            for (std::string& part : parts)
                boost::algorithm::trim(part);
            m_params.push_back({parts[1], parts[0], boost::algorithm::join(std::vector<std::string>(parts.begin() + 2, parts.end()), "|")});
        }

        if (m_params.empty())
            m_load_error = "未找到动态参数。";
        rebuild_grid();
    }

    void rebuild_grid()
    {
        if (m_grid == nullptr)
            return;
        m_grid->Freeze();
        if (m_grid->GetNumberRows() > 0)
            m_grid->DeleteRows(0, m_grid->GetNumberRows());
        if (!m_params.empty())
            m_grid->AppendRows(static_cast<int>(m_params.size()));
        for (int row = 0; row < static_cast<int>(m_params.size()); ++row) {
            m_grid->SetCellValue(row, 0, from_u8(m_params[row].name));
            m_grid->SetCellValue(row, 1, from_u8(m_params[row].param));
            m_grid->SetCellValue(row, 2, from_u8(m_params[row].value));
            m_grid->SetReadOnly(row, 0, true);
            m_grid->SetReadOnly(row, 1, true);
            m_grid->SetReadOnly(row, 2, false);
        }
        m_grid->Thaw();
        update_grid_column_widths();
        if (!m_load_error.empty())
            set_tip(from_u8(m_load_error));
        else if (m_tip_label != nullptr && !m_tip_label->GetLabel().empty())
            set_tip(wxEmptyString);
    }

    void commit_grid_values()
    {
        if (m_grid == nullptr)
            return;
        if (m_grid->IsCellEditControlShown())
            m_grid->HideCellEditControl();
        m_grid->SaveEditControlValue();
        for (int row = 0; row < static_cast<int>(m_params.size()) && row < m_grid->GetNumberRows(); ++row)
            m_params[row].value = into_u8(m_grid->GetCellValue(row, 2));
    }

    void set_all_dynamic_param_values(const std::string& value)
    {
        for (GFDDynamicParamRow& row : m_params)
            row.value = value;
        rebuild_grid();
    }

    void apply_dynamic_param_values(const std::map<std::string, std::string>& values)
    {
        for (GFDDynamicParamRow& row : m_params) {
            if (const auto it = values.find(row.param); it != values.end()) {
                const std::string value = gfd_trim_copy(it->second);
                row.value = value.empty() ? "-" : value;
            }
        }
        rebuild_grid();
    }

    bool response_success_or_message(const std::string& body, std::string& message) const
    {
        try {
            const nlohmann::json response = nlohmann::json::parse(body);
            const int code = response.value("code", 0);
            const std::string msg = response.value("msg", std::string());
            message = msg;
            return msg == "success" || msg.empty() || code == 0 || code == 200;
        } catch (...) {
            return true;
        }
    }

    GFDDynamicFilamentOption option_from_json(const nlohmann::json& item) const
    {
        GFDDynamicFilamentOption option;
        option.sn       = gfd_json_first_string_recursive(item, {"sn", "filamentSn", "materialSn", "serialNo", "serialNumber"});
        option.id       = gfd_json_first_string_recursive(item, {"id", "rootId", "rootMaterialId", "materialId", "filamentId"});
        option.color    = gfd_json_first_string_recursive(item, {"colorTitle"});
        option.barcode  = gfd_json_first_string_recursive(item, {"barcode"});
        option.text     = gfd_filament_display_text(option.color, option.barcode);
        if (option.text.empty()) {
            option.barcode  = gfd_json_first_string_recursive(item, {"barCode", "bar_code", "barCodeNo", "barcodeNo"});
            option.color    = gfd_json_first_string_recursive(item, {"colorName", "colourName", "colorCn", "colourCn", "color", "colour"});
            option.title    = gfd_best_filament_title({}, {}, option.color, {}, option.barcode, option.sn, option.id);
            option.text     = gfd_filament_display_text(option.title, option.barcode);
        }
        if (option.text.empty())
            option.text = !option.sn.empty() ? option.sn : option.id;
        return option;
    }

    void parse_filament_options(const std::string& body)
    {
        m_filaments.clear();
        const nlohmann::json response = nlohmann::json::parse(body);
        std::vector<nlohmann::json> arrays;
        gfd_collect_json_array_candidates(response, arrays);

        std::vector<GFDDynamicFilamentOption> best_filaments;
        int                                   best_score = -1;
        for (const nlohmann::json& array : arrays) {
            if (!array.is_array())
                continue;
            std::vector<GFDDynamicFilamentOption> options;
            int                                   array_score = 0;
            std::set<std::string>                 seen;
            for (const nlohmann::json& item : array) {
                if (!item.is_object())
                    continue;
                GFDDynamicFilamentOption option = option_from_json(item);
                if (option.sn.empty() && option.id.empty() && option.text.empty())
                    continue;
                const std::string key = !option.sn.empty() ? option.sn : (!option.id.empty() ? option.id : option.text);
                if (!seen.insert(key).second)
                    continue;
                array_score += gfd_filament_option_score(option);
                if (gfd_json_has_any_key_recursive(item, {"colorTitle"}))
                    array_score += 50;
                if (gfd_json_has_any_key_recursive(item, {"barcode"}))
                    array_score += 50;
                options.emplace_back(std::move(option));
            }
            if (!options.empty() && array_score > best_score) {
                best_score     = array_score;
                best_filaments = std::move(options);
            }
        }
        m_filaments = std::move(best_filaments);
    }

    void refresh_filament_options()
    {
        if (m_plater == nullptr) {
            set_tip(_L("Plater 未初始化"));
            return;
        }

        set_tip(_L("正在获取耗材列表..."));
        std::string body;
        std::string error_message;
        if (!m_plater->fetch_dynamic_filament_list(body, error_message)) {
            set_tip(from_u8(error_message.empty() ? "获取耗材列表失败" : error_message));
            return;
        }

        std::string response_message;
        if (!response_success_or_message(body, response_message)) {
            set_tip(from_u8(response_message.empty() ? "获取耗材列表失败" : response_message));
            return;
        }

        try {
            parse_filament_options(body);
        } catch (const std::exception& ex) {
            set_tip(from_u8(std::string("解析耗材列表失败: ") + ex.what()));
            return;
        }

        m_filament_choice->Clear();
        for (const GFDDynamicFilamentOption& option : m_filaments)
            m_filament_choice->Append(from_u8(option.text), new wxStringClientData(from_u8(option.sn)));

        const int initial_index = find_initial_filament_index();
        if (initial_index != wxNOT_FOUND)
            m_filament_choice->SetSelection(initial_index);
        else
            set_tip(_L("暂无耗材"));

        on_filament_changed(true);
    }

    void on_filament_changed(bool force_fetch)
    {
        const int selection = m_filament_choice != nullptr ? m_filament_choice->GetSelection() : wxNOT_FOUND;
        if (selection == wxNOT_FOUND || selection < 0 || selection >= static_cast<int>(m_filaments.size())) {
            m_selected_sn.clear();
            m_sn_label->SetLabel(_L("-"));
            set_all_dynamic_param_values("-");
            return;
        }

        const GFDDynamicFilamentOption& option = m_filaments[selection];
        std::string option_sn = option.sn;
        if (auto* data = static_cast<wxStringClientData*>(m_filament_choice->GetClientObject(selection)))
            option_sn = into_u8(data->GetData());

        if (!force_fetch && option_sn == m_selected_sn)
            return;

        m_selected_sn = option_sn;
        m_sn_label->SetLabel(m_selected_sn.empty() ? _L("-") : from_u8(m_selected_sn));
        m_sn_label->SetToolTip(m_sn_label->GetLabel());
        load_default_dynamic_params();
        set_all_dynamic_param_values("-");

        if (m_selected_sn.empty()) {
            set_tip(_L("当前耗材缺少 SN，无法获取详情"));
            return;
        }

        std::string body;
        std::string error_message;
        set_tip(_L("正在获取动态参数..."));
        if (m_plater == nullptr || !m_plater->fetch_dynamic_filament_detail(m_selected_sn, body, error_message)) {
            set_tip(from_u8(error_message.empty() ? "获取动态参数失败" : error_message));
            return;
        }

        std::string response_message;
        if (!response_success_or_message(body, response_message)) {
            set_tip(from_u8(response_message.empty() ? "获取动态参数失败" : response_message));
            return;
        }

        try {
            m_current_detail = nlohmann::json::parse(body);
        } catch (const std::exception& ex) {
            m_current_detail = nlohmann::json();
            set_tip(from_u8(std::string("解析动态参数失败: ") + ex.what()));
            return;
        }

        apply_detail_for_selected_device();
    }

    void apply_detail_for_selected_device()
    {
        if (m_current_detail.is_null())
            return;

        load_default_dynamic_params();
        const nlohmann::json* detail = gfd_extract_detail_data(m_current_detail);
        if (detail == nullptr || !detail->is_object()) {
            set_all_dynamic_param_values("-");
            return;
        }

        const std::string device_type = selected_device_type();
        const auto device_temps_it = detail->find("deviceTypeTemps");
        if (device_temps_it == detail->end() || !device_temps_it->is_object()) {
            set_all_dynamic_param_values("-");
            set_tip(_L("未找到当前耗材的机型参数"));
            return;
        }

        const auto device_it = device_temps_it->find(device_type);
        if (device_it == device_temps_it->end() || !device_it->is_object()) {
            set_all_dynamic_param_values("-");
            set_tip(from_u8((boost::format("未找到机型 %1% 的动态参数") % device_type).str()));
            return;
        }

        const nlohmann::json* slice_param = gfd_json_first_value(*device_it, {"orcaSliceParam", "sliceParam"});
        if (slice_param == nullptr) {
            set_all_dynamic_param_values("-");
            set_tip(from_u8((boost::format("机型 %1% 暂无动态参数") % device_type).str()));
            return;
        }

        std::map<std::string, std::string> values;
        try {
            if (slice_param->is_string())
                gfd_collect_orca_param_values(slice_param->get<std::string>(), values);
            else
                gfd_collect_orca_param_values(*slice_param, values);
        } catch (const std::exception& ex) {
            set_all_dynamic_param_values("-");
            set_tip(from_u8(std::string("解析 Orca 参数失败: ") + ex.what()));
            return;
        }
        set_all_dynamic_param_values("-");
        apply_dynamic_param_values(values);
        set_tip(wxEmptyString);
    }

    std::string format_orca_param_value(const std::string& raw_value) const
    {
        const std::string value = gfd_trim_copy(raw_value);
        if (value.empty() || value == "-")
            return "nil";
        return value;
    }

    std::string build_orca_slice_param_json()
    {
        commit_grid_values();
        nlohmann::json slice_params = nlohmann::json::object();
        for (const GFDDynamicParamRow& row : m_params) {
            if (row.param.empty())
                continue;
            slice_params[row.param] = nlohmann::json::array({format_orca_param_value(row.value)});
        }
        return slice_params.dump();
    }

    void save_slice_params()
    {
        if (m_selected_sn.empty()) {
            set_tip(_L("请选择耗材"));
            return;
        }

        std::string body;
        std::string error_message;
        set_tip(_L("正在保存..."));
        m_save_button->Enable(false);
        const bool ok = m_plater != nullptr &&
                        m_plater->update_dynamic_filament_slice_param(m_selected_sn, selected_device_type(), build_orca_slice_param_json(), body, error_message);
        m_save_button->Enable(true);

        if (!ok) {
            set_tip(from_u8(error_message.empty() ? "保存失败" : error_message));
            return;
        }

        std::string response_message;
        if (!response_success_or_message(body, response_message)) {
            set_tip(from_u8(response_message.empty() ? "保存失败" : response_message));
            return;
        }

        set_tip(wxEmptyString);
        GUI::show_info(this, _L("保存成功"), _L("提示"));
    }
};

} // namespace

MainFrame::MainFrame() :
DPIFrame(NULL, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, BORDERLESS_FRAME_STYLE, "mainframe")
    , m_printhost_queue_dlg(new PrintHostQueueDialog(this))
    // BBS
    , m_recent_projects(18)
    , m_settings_dialog(this)
    , diff_dialog(this)
{
#ifdef __WXOSX__
    set_miniaturizable(GetHandle());
#endif

    if (!wxGetApp().app_config->has("user_mode")) {
        wxGetApp().app_config->set("user_mode", "simple");
        wxGetApp().app_config->set_bool("developer_mode", false);
        wxGetApp().app_config->save();
    }

    wxGetApp().app_config->set_bool("internal_developer_mode", false);

    wxString max_recent_count_str = wxGetApp().app_config->get("max_recent_count");
    long max_recent_count = 18;
    if (max_recent_count_str.ToLong(&max_recent_count))
        set_max_recent_count((int)max_recent_count);

    //reset log level
    auto         loglevel = wxGetApp().app_config->get("log_severity_level");
    unsigned int log_level = Slic3r::level_string_to_boost(loglevel);
    if (log_level < 3)
        log_level = 3;
    Slic3r::set_logging_level(log_level);

    // BBS
    m_recent_projects.SetMenuPathStyle(wxFH_PATH_SHOW_ALWAYS);
    MarkdownTip::Recreate(this);

    // Fonts were created by the DPIFrame constructor for the monitor, on which the window opened.
    wxGetApp().update_fonts(this);

#ifndef __APPLE__
    m_topbar         = new BBLTopbar(this);
#else
    auto panel_topbar = new wxPanel(this, wxID_ANY);
    panel_topbar->SetBackgroundColour(wxColour(38, 46, 48));
    auto sizer_tobar = new wxBoxSizer(wxVERTICAL);
    panel_topbar->SetSizer(sizer_tobar);
    panel_topbar->Layout();
#endif

    //wxAuiToolBar* toolbar = new wxAuiToolBar();
/*
#ifndef __WXOSX__ // Don't call SetFont under OSX to avoid name cutting in ObjectList
    this->SetFont(this->normal_font());
#endif
    // Font is already set in DPIFrame constructor
*/

#ifdef __APPLE__
	m_reset_title_text_colour_timer = new wxTimer();
	m_reset_title_text_colour_timer->SetOwner(this);
	Bind(wxEVT_TIMER, [this](auto& e) {
		set_title_colour_after_set_title(GetHandle());
		m_reset_title_text_colour_timer->Stop();
	});
	this->Bind(wxEVT_FULLSCREEN, [this](wxFullScreenEvent& e) {
		set_tag_when_enter_full_screen(e.IsFullScreen());
		if (!e.IsFullScreen()) {
            if (m_reset_title_text_colour_timer) {
                m_reset_title_text_colour_timer->Stop();
                m_reset_title_text_colour_timer->Start(500);
            }
		}
		e.Skip();
	});
#endif

#ifdef __APPLE__
    // Initialize the docker task bar icon.
    switch (wxGetApp().get_app_mode()) {
    default:
    case GUI_App::EAppMode::Editor:
        m_taskbar_icon = std::make_unique<OrcaSlicerTaskBarIcon>(wxTBI_DOCK);
        m_taskbar_icon->SetIcon(wxIcon(Slic3r::var("OrcaSlicer-mac_256px.ico"), wxBITMAP_TYPE_ICO), "OrcaSlicer");
        break;
    case GUI_App::EAppMode::GCodeViewer:
        break;
    }
#endif // __APPLE__

    // Load the icon either from the exe, or from the ico file.
    SetIcon(main_frame_icon(wxGetApp().get_app_mode()));

    // initialize tabpanel and menubar
    init_tabpanel();
    if (wxGetApp().is_gcode_viewer())
        init_menubar_as_gcodeviewer();
    else
        init_menubar_as_editor();

    // BBS
#if 0
    // This is needed on Windows to fake the CTRL+# of the window menu when using the numpad
    wxAcceleratorEntry entries[6];
    entries[0].Set(wxACCEL_CTRL, WXK_NUMPAD1, wxID_HIGHEST + 1);
    entries[1].Set(wxACCEL_CTRL, WXK_NUMPAD2, wxID_HIGHEST + 2);
    entries[2].Set(wxACCEL_CTRL, WXK_NUMPAD3, wxID_HIGHEST + 3);
    entries[3].Set(wxACCEL_CTRL, WXK_NUMPAD4, wxID_HIGHEST + 4);
    entries[4].Set(wxACCEL_CTRL, WXK_NUMPAD5, wxID_HIGHEST + 5);
    entries[5].Set(wxACCEL_CTRL, WXK_NUMPAD6, wxID_HIGHEST + 6);
    wxAcceleratorTable accel(6, entries);
    SetAcceleratorTable(accel);
#endif // _WIN32

    // BBS
    //wxAcceleratorEntry entries[13];
    //int index = 0;
    //entries[index++].Set(wxACCEL_CTRL, (int)'N', wxID_HIGHEST + wxID_NEW);
    //entries[index++].Set(wxACCEL_CTRL, (int)'O', wxID_HIGHEST + wxID_OPEN);
    //entries[index++].Set(wxACCEL_CTRL, (int)'S', wxID_HIGHEST + wxID_SAVE);
    //entries[index++].Set(wxACCEL_CTRL | wxACCEL_SHIFT, (int)'S', wxID_HIGHEST + wxID_SAVEAS);
    //entries[index++].Set(wxACCEL_CTRL, (int)'X', wxID_HIGHEST + wxID_CUT);
    ////entries[index++].Set(wxACCEL_CTRL, (int)'I', wxID_HIGHEST + wxID_ADD);
    //entries[index++].Set(wxACCEL_CTRL, (int)'A', wxID_HIGHEST + wxID_SELECTALL);
    //entries[index++].Set(wxACCEL_NORMAL, (int)27 /* escape */, wxID_HIGHEST + wxID_CANCEL);
    //entries[index++].Set(wxACCEL_CTRL, (int)'Z', wxID_HIGHEST + wxID_UNDO);
    //entries[index++].Set(wxACCEL_CTRL, (int)'Y', wxID_HIGHEST + wxID_REDO);
    //entries[index++].Set(wxACCEL_CTRL, (int)'C', wxID_HIGHEST + wxID_COPY);
    //entries[index++].Set(wxACCEL_CTRL, (int)'V', wxID_HIGHEST + wxID_PASTE);
    //entries[index++].Set(wxACCEL_CTRL, (int)'P', wxID_HIGHEST + wxID_PREFERENCES);
    //entries[index++].Set(wxACCEL_CTRL, (int)'I', wxID_HIGHEST + wxID_FILE6);
    //wxAcceleratorTable accel(sizeof(entries) / sizeof(entries[0]), entries);
    //SetAcceleratorTable(accel);

    //Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_plater->new_project(); }, wxID_HIGHEST + wxID_NEW);
    //Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_plater->load_project(); }, wxID_HIGHEST + wxID_OPEN);
    //// BBS: close save project
    //Bind(wxEVT_MENU, [this](wxCommandEvent&) { if (m_plater) m_plater->save_project(); }, wxID_HIGHEST + wxID_SAVE);
    //Bind(wxEVT_MENU, [this](wxCommandEvent&) { if (m_plater) m_plater->save_project(true); }, wxID_HIGHEST + wxID_SAVEAS);
    ////Bind(wxEVT_MENU, [this](wxCommandEvent&) { if (m_plater) m_plater->add_model(); }, wxID_HIGHEST + wxID_ADD);
    ////Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_plater->remove_selected(); }, wxID_HIGHEST + wxID_DELETE);
    //Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    //        if (!can_add_models())
    //            return;
    //        if (m_plater) {
    //            m_plater->add_model();
    //        }
    //    }, wxID_HIGHEST + wxID_FILE6);
    //Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_plater->select_all(); }, wxID_HIGHEST + wxID_SELECTALL);
    //Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_plater->deselect_all(); }, wxID_HIGHEST + wxID_CANCEL);
    //Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    //    if (m_plater->is_view3D_shown())
    //        m_plater->undo();
    //    }, wxID_HIGHEST + wxID_UNDO);
    //Bind(wxEVT_MENU, [this](wxCommandEvent&) {
    //    if (m_plater->is_view3D_shown())
    //        m_plater->redo();
    //    }, wxID_HIGHEST + wxID_REDO);
    //Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_plater->copy_selection_to_clipboard(); }, wxID_HIGHEST + wxID_COPY);
    //Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_plater->paste_from_clipboard(); }, wxID_HIGHEST + wxID_PASTE);
    //Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_plater->cut_selection_to_clipboard(); }, wxID_HIGHEST + wxID_CUT);
    Bind(wxEVT_SIZE, [this](wxSizeEvent&) {
            BOOST_LOG_TRIVIAL(trace) << "mainframe: size changed, is maximized = " << this->IsMaximized();
#ifndef __APPLE__
            if (this->IsMaximized()) {
                m_topbar->SetWindowSize();
            } else {
                m_topbar->SetMaximizedSize();
            }
#endif
        Refresh();
        Layout();
        });

    //BBS
    Bind(EVT_SELECT_TAB, [this](wxCommandEvent&evt) {
        TabPosition pos = (TabPosition)evt.GetInt();
        m_tabpanel->SetSelection(pos);
    });

    Bind(EVT_SYNC_CLOUD_PRESET, &MainFrame::on_select_default_preset, this);

//    Bind(wxEVT_MENU,
//        [this](wxCommandEvent&)
//        {
//            PreferencesDialog dlg(this);
//            dlg.ShowModal();
//#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
//            if (dlg.seq_top_layer_only_changed() || dlg.seq_seq_top_gcode_indices_changed())
//#else
//            if (dlg.seq_top_layer_only_changed())
//#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
//                plater()->refresh_print();
//        }, wxID_HIGHEST + wxID_PREFERENCES);


    // set default tooltip timer in msec
    // SetAutoPop supposedly accepts long integers but some bug doesn't allow for larger values
    // (SetAutoPop is not available on GTK.)
    wxToolTip::SetAutoPop(32767);

    m_loaded = true;

    // initialize layout
    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
#ifndef __APPLE__
     sizer->Add(m_topbar, 0, wxEXPAND);
#else
     sizer->Add(panel_topbar, 0, wxEXPAND);
#endif // __WINDOWS__


    sizer->Add(m_main_sizer, 1, wxEXPAND);
    SetSizerAndFit(sizer);
    // initialize layout from config
    update_layout();
    sizer->SetSizeHints(this);

#ifdef WIN32
    // SetMaximize causes the window to overlap the taskbar, due to the fact this window has wxMAXIMIZE_BOX off
    // https://forums.wxwidgets.org/viewtopic.php?t=50634
    // Fix it here
    this->Bind(wxEVT_MAXIMIZE, [this](auto &e) {
        wxDisplay display(this);
        auto      size = display.GetClientArea().GetSize();
        auto      pos  = display.GetClientArea().GetPosition();
        HWND      hWnd = GetHandle();
        RECT      borderThickness;
        SetRectEmpty(&borderThickness);
        AdjustWindowRectEx(&borderThickness, GetWindowLongPtr(hWnd, GWL_STYLE), FALSE, 0);
        const auto max_size = size + wxSize{-borderThickness.left + borderThickness.right, -borderThickness.top + borderThickness.bottom};
        const auto current_size = GetSize();
        SetSize({std::min(max_size.x, current_size.x), std::min(max_size.y, current_size.y)});
        Move(pos + wxPoint{borderThickness.left, borderThickness.top});
        e.Skip();
    });
#endif // WIN32
    // BBS
    Fit();

    const wxSize min_size = wxGetApp().get_min_size(); //wxSize(76*wxGetApp().em_unit(), 49*wxGetApp().em_unit());

    SetMinSize(min_size/*wxSize(760, 490)*/);
    SetSize(wxSize(FromDIP(1200), FromDIP(800)));

    Layout();

    update_title();

    // declare events
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": mainframe received close_widow event";
        if (event.CanVeto() && m_plater->get_view3D_canvas3D()->get_gizmos_manager().is_in_editing_mode(true)) {
            // prevents to open the save dirty project dialog
            event.Veto();
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< "cancelled by gizmo in editing";
            return;
        }

        //BBS:
        //if (event.CanVeto() && !wxGetApp().check_and_save_current_preset_changes(_L("Application is closing"), _L("Closing Application while some presets are modified."))) {
        //    event.Veto();
        //    return;
        //}
        auto check = [](bool yes_or_no) {
            if (yes_or_no)
                return true;
            return wxGetApp().check_and_save_current_preset_changes(_L("Application is closing"), _L("Closing Application while some presets are modified."));
        };

        // BBS: close save project
        int result;
        if (event.CanVeto() && ((result = m_plater->close_with_confirm(check)) == wxID_CANCEL)) {
            event.Veto();
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< "cancelled by close_with_confirm selection";
            return;
        }
        if (event.CanVeto() && !wxGetApp().check_print_host_queue()) {
            event.Veto();
            return;
        }

    #if 0 // BBS
        //if (m_plater != nullptr) {
        //    int saved_project = m_plater->save_project_if_dirty(_L("Closing Application. Current project is modified."));
        //    if (saved_project == wxID_CANCEL) {
        //        event.Veto();
        //        return;
        //    }
        //    // check unsaved changes only if project wasn't saved
        //    else if (plater()->is_project_dirty() && saved_project == wxID_NO && event.CanVeto() &&
        //             (plater()->is_presets_dirty() && !wxGetApp().check_and_save_current_preset_changes(_L("Application is closing"), _L("Closing Application while some presets are modified.")))) {
        //        event.Veto();
        //        return;
        //    }
        //}
    #endif

        MarkdownTip::ExitTip();

        m_plater->reset();
        this->shutdown();
        // propagate event

        wxGetApp().remove_mall_system_dialog();
        event.Skip();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": mainframe finished process close_widow event";
    });

    //FIXME it seems this method is not called on application start-up, at least not on Windows. Why?
    // The same applies to wxEVT_CREATE, it is not being called on startup on Windows.
    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
        if (m_plater != nullptr && event.GetActive())
            m_plater->on_activate();
        event.Skip();
    });

// OSX specific issue:
// When we move application between Retina and non-Retina displays, The legend on a canvas doesn't redraw
// So, redraw explicitly canvas, when application is moved
//FIXME maybe this is useful for __WXGTK3__ as well?
#if __APPLE__
    Bind(wxEVT_MOVE, [](wxMoveEvent& event) {
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
        event.Skip();
    });
#endif

    update_ui_from_settings();    // FIXME (?)

    if (m_plater != nullptr) {
        // BBS
        update_slice_print_status(eEventSliceUpdate, true, true);

        // BBS: backup project
        if (wxGetApp().app_config->get("backup_switch") == "true") {
            std::string backup_interval;
            if (!wxGetApp().app_config->get("app", "backup_interval", backup_interval))
                backup_interval = "10";
            Slic3r::set_backup_interval(boost::lexical_cast<long>(backup_interval));
        } else {
            Slic3r::set_backup_interval(0);
        }
        Slic3r::set_backup_callback([this](int action) {
            if (action == 0) {
                wxPostEvent(this, wxCommandEvent(EVT_BACKUP_POST));
            }
            else if (action == 1) {
                if (!m_plater->up_to_date(false, true)) {
                    m_plater->export_3mf(m_plater->model().get_backup_path() + "/.3mf", SaveStrategy::Backup);
                    m_plater->up_to_date(true, true);
                }
            }
         });
        Bind(EVT_BACKUP_POST, [](wxCommandEvent& e) {
            Slic3r::run_backup_ui_tasks();
            });
;    }
    this->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &evt) {
#ifdef __APPLE__
        if (evt.CmdDown() && (evt.GetKeyCode() == 'H')) {
            //call parent_menu hide behavior
            return;}
        if (evt.CmdDown() && (!evt.ShiftDown()) && (evt.GetKeyCode() == 'M')) {
            this->Iconize();
            return;
        }
        if (evt.CmdDown() && evt.GetKeyCode() == 'Q') { wxPostEvent(this, wxCloseEvent(wxEVT_CLOSE_WINDOW)); return;}
        if (evt.CmdDown() && evt.RawControlDown() && evt.GetKeyCode() == 'F') {
            EnableFullScreenView(true);
            if (IsFullScreen()) {
                ShowFullScreen(false);
            } else {
                ShowFullScreen(true);
            }
            return;}
#endif
        if (evt.CmdDown() && evt.GetKeyCode() == 'R') { if (m_slice_enable) { wxGetApp().plater()->update(true, true); wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_PLATE)); this->m_tabpanel->SetSelection(tpPreview); } return; }
        if (evt.CmdDown() && evt.ShiftDown() && evt.GetKeyCode() == 'G') {
            m_plater->apply_background_progress();
            m_print_enable = get_enable_print_status();
            m_print_btn->Enable(m_print_enable);
            if (m_print_enable) {
                if (wxGetApp().preset_bundle->use_bbl_network())
                    wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_PRINT_PLATE));
                else
                    wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SEND_GCODE));
            }
            evt.Skip();
            return;
        }
        else if (evt.CmdDown() && evt.GetKeyCode() == 'G') { if (can_export_gcode()) { wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_SLICED_FILE)); } evt.Skip(); return; }
        if (evt.CmdDown() && evt.GetKeyCode() == 'J') { m_printhost_queue_dlg->Show(); return; }
        if (evt.CmdDown() && evt.GetKeyCode() == 'N') { m_plater->new_project(); return;}
        if (evt.CmdDown() && evt.GetKeyCode() == 'O') { m_plater->load_project(); return;}
        if (evt.CmdDown() && evt.ShiftDown() && evt.GetKeyCode() == 'S') { if (can_save_as()) m_plater->save_project(true); return;}
        else if (evt.CmdDown() && evt.GetKeyCode() == 'S') { if (can_save()) m_plater->save_project(); return;}
        if (evt.CmdDown() && evt.GetKeyCode() == 'F') {
            if (m_plater && (m_tabpanel->GetSelection() == TabPosition::tp3DEditor || m_tabpanel->GetSelection() == TabPosition::tpPreview)) {
                m_plater->sidebar().can_search();
            }
        }
#ifdef __APPLE__
        if (evt.CmdDown() && evt.GetKeyCode() == ',')
#else
        if (evt.CmdDown() && evt.GetKeyCode() == 'P')
#endif
        {
            // Orca: Use GUI_App::open_preferences instead of direct call so windows associations are updated on exit
            wxGetApp().open_preferences();
            plater()->get_current_canvas3D()->force_set_focus();
            return;
        }

        if (evt.CmdDown() && evt.GetKeyCode() == 'I') {
            if (!can_add_models()) return;
            if (m_plater) { m_plater->add_file(); }
            return;
        }
        evt.Skip();
    });

#ifdef _MSW_DARK_MODE
    wxGetApp().UpdateDarkUIWin(this);
#endif // _MSW_DARK_MODE

    wxGetApp().persist_window_geometry(this, true);
    wxGetApp().persist_window_geometry(&m_settings_dialog, true);
    // bind events from DiffDlg

    bind_diff_dialog();
}

void MainFrame::bind_diff_dialog()
{
    auto get_tab = [](Preset::Type type) {
        Tab* null_tab = nullptr;
        for (Tab* tab : wxGetApp().tabs_list)
            if (tab->type() == type)
                return tab;
        return null_tab;
    };

    auto transfer = [this, get_tab](Preset::Type type) {
        get_tab(type)->transfer_options(diff_dialog.get_left_preset_name(type),
                                        diff_dialog.get_right_preset_name(type),
                                        diff_dialog.get_selected_options(type));
    };

    auto process_options = [this](std::function<void(Preset::Type)> process) {
        const Preset::Type diff_dlg_type = diff_dialog.view_type();
        if (diff_dlg_type == Preset::TYPE_INVALID) {
            for (const Preset::Type& type : diff_dialog.types_list() )
                process(type);
        }
        else
            process(diff_dlg_type);
    };

    diff_dialog.Bind(EVT_DIFF_DIALOG_TRANSFER,      [process_options, transfer](SimpleEvent&)         { process_options(transfer); });
}


#ifdef __WIN32__

// Orca: Fix maximized window overlaps taskbar when taskbar auto hide is enabled (#8085)
// Adopted from https://gist.github.com/MortenChristiansen/6463580
static void AdjustWorkingAreaForAutoHide(const HWND hWnd, MINMAXINFO* mmi)
{
    const auto taskbarHwnd = FindWindowA("Shell_TrayWnd", nullptr);
    if (!taskbarHwnd) {
        return;
    }
    const auto monitorContainingApplication = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONULL);
    const auto monitorWithTaskbarOnIt = MonitorFromWindow(taskbarHwnd, MONITOR_DEFAULTTONULL);
    if (monitorContainingApplication != monitorWithTaskbarOnIt) {
        return;
    }
    APPBARDATA abd;
    abd.cbSize = sizeof(APPBARDATA);
    abd.hWnd   = taskbarHwnd;

    // Find if task bar has auto-hide enabled
    const auto uState = (UINT) SHAppBarMessage(ABM_GETSTATE, &abd);
    if ((uState & ABS_AUTOHIDE) != ABS_AUTOHIDE) {
        return;
    }

    RECT borderThickness;
    SetRectEmpty(&borderThickness);
    AdjustWindowRectEx(&borderThickness, GetWindowLongPtr(hWnd, GWL_STYLE) & ~WS_CAPTION, FALSE, 0);

    // Determine taskbar position
    SHAppBarMessage(ABM_GETTASKBARPOS, &abd);
    const auto& rc = abd.rc;
    if (rc.top == rc.left && rc.bottom > rc.right) {
        // Left
        const auto offset = borderThickness.left + 2;
        mmi->ptMaxPosition.x += offset;
        mmi->ptMaxTrackSize.x -= offset;
        mmi->ptMaxSize.x -= offset;
    } else if (rc.top == rc.left && rc.bottom < rc.right) {
        // Top
        const auto offset = borderThickness.top + 2;
        mmi->ptMaxPosition.y += offset;
        mmi->ptMaxTrackSize.y -= offset;
        mmi->ptMaxSize.y -= offset;
    } else if (rc.top > rc.left) {
        // Bottom
        const auto offset = borderThickness.bottom + 2;
        mmi->ptMaxSize.y -= offset;
        mmi->ptMaxTrackSize.y -= offset;
    } else {
        // Right
        const auto offset = borderThickness.right + 2;
        mmi->ptMaxSize.x -= offset;
        mmi->ptMaxTrackSize.x -= offset;
    }
}

WXLRESULT MainFrame::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    HWND hWnd = GetHandle();
    /* When we have a custom titlebar in the window, we don't need the non-client area of a normal window
     * to be painted. In order to achieve this, we handle the "WM_NCCALCSIZE" which is responsible for the
     * size of non-client area of a window and set the return value to 0. Also we have to tell the
     * application to not paint this area on activate and deactivation events so we also handle
     * "WM_NCACTIVATE" message. */
    switch (nMsg) {
    case WM_NCACTIVATE: {
        /* Returning 0 from this message disable the window from receiving activate events which is not
        desirable. However When a visual style is not active (?) for this window, "lParam" is a handle to an
        optional update region for the nonclient area of the window. If this parameter is set to -1,
        DefWindowProc does not repaint the nonclient area to reflect the state change. */
        lParam = -1;
        break;
    }
    /* To remove the standard window frame, you must handle the WM_NCCALCSIZE message, specifically when
    its wParam value is TRUE and the return value is 0 */
    case WM_NCCALCSIZE:
        if (wParam) {
            /* Detect whether window is maximized or not. We don't need to change the resize border when win is
             *  maximized because all resize borders are gone automatically */
            WINDOWPLACEMENT wPos;
            // GetWindowPlacement fail if this member is not set correctly.
            wPos.length = sizeof(wPos);
            GetWindowPlacement(hWnd, &wPos);
            if (wPos.showCmd != SW_SHOWMAXIMIZED) {
                RECT borderThickness;
                SetRectEmpty(&borderThickness);
                AdjustWindowRectEx(&borderThickness, GetWindowLongPtr(hWnd, GWL_STYLE) & ~WS_CAPTION, FALSE, NULL);
                borderThickness.left *= -1;
                borderThickness.top *= -1;
                NCCALCSIZE_PARAMS *sz = reinterpret_cast<NCCALCSIZE_PARAMS *>(lParam);
                // Add 1 pixel to the top border to make the window resizable from the top border
                sz->rgrc[0].top += 1; // borderThickness.top;
                sz->rgrc[0].left += borderThickness.left;
                sz->rgrc[0].right -= borderThickness.right;
                sz->rgrc[0].bottom -= borderThickness.bottom;
                return 0;
            }
        }
        break;

    case WM_GETMINMAXINFO: {
        auto mmi = (MINMAXINFO*) lParam;
        HandleGetMinMaxInfo(mmi);
        AdjustWorkingAreaForAutoHide(hWnd, mmi);
        return 0;
    }
    }
    return wxFrame::MSWWindowProc(nMsg, wParam, lParam);
}

#endif

void  MainFrame::show_log_window()
{
    m_log_window = new wxLogWindow(this, _L("Logging"), true, false);
    m_log_window->Show();
}

//BBS GUI refactor: remove unused layout new/dlg
void MainFrame::update_layout()
{
    auto restore_to_creation = [this]() {
        auto clean_sizer = [](wxSizer* sizer) {
            while (!sizer->GetChildren().IsEmpty()) {
                sizer->Detach(0);
            }
        };

        // On Linux m_plater needs to be removed from m_tabpanel before to reparent it
        int plater_page_id = m_tabpanel->FindPage(m_plater);
        if (plater_page_id != wxNOT_FOUND)
            m_tabpanel->RemovePage(plater_page_id);

        if (m_plater->GetParent() != this)
            m_plater->Reparent(this);

        if (m_tabpanel->GetParent() != this)
            m_tabpanel->Reparent(this);

        plater_page_id = (m_plater_page != nullptr) ? m_tabpanel->FindPage(m_plater_page) : wxNOT_FOUND;
        if (plater_page_id != wxNOT_FOUND) {
            m_tabpanel->DeletePage(plater_page_id);
            m_plater_page = nullptr;
        }

        clean_sizer(m_main_sizer);
        clean_sizer(m_settings_dialog.GetSizer());

        if (m_settings_dialog.IsShown())
            m_settings_dialog.Close();

        m_tabpanel->Hide();
        m_plater->Hide();

        Layout();
    };

    //BBS GUI refactor: remove unused layout new/dlg
    //ESettingsLayout layout = wxGetApp().is_gcode_viewer() ? ESettingsLayout::GCodeViewer : ESettingsLayout::Old;
    ESettingsLayout layout =  ESettingsLayout::Old;

    if (m_layout == layout)
        return;

    wxBusyCursor busy;

    Freeze();

    // Remove old settings
    if (m_layout != ESettingsLayout::Unknown)
        restore_to_creation();

    ESettingsLayout old_layout = m_layout;
    m_layout = layout;

    // From the very beginning the Print settings should be selected
    //m_last_selected_tab = m_layout == ESettingsLayout::Dlg ? 0 : 1;
    m_last_selected_tab = 1;

    // Set new settings
    switch (m_layout)
    {
    case ESettingsLayout::Old:
    {
        m_plater->Reparent(m_tabpanel);
        m_tabpanel->InsertPage(tp3DEditor, m_plater, _L("Prepare"), std::string("tab_3d_active"), std::string("tab_3d_active"), false);
        m_tabpanel->InsertPage(tpPreview, m_plater, _L("Preview"), std::string("tab_preview_active"), std::string("tab_preview_active"), false);
        m_main_sizer->Add(m_tabpanel, 1, wxEXPAND | wxTOP, 0);

        m_tabpanel->Bind(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, [this](wxCommandEvent& evt)
        {
            // jump to 3deditor under preview_only mode
            if (evt.GetId() == tp3DEditor){
                m_plater->update(true);

                if (!preview_only_hint())
                    return;
            }
            evt.Skip();
        });

        m_plater->Show();
        m_tabpanel->Show();

        break;
    }
    case ESettingsLayout::GCodeViewer:
    {
        m_main_sizer->Add(m_plater, 1, wxEXPAND);
        //BBS: add bed exclude area
        m_plater->set_bed_shape({ { 0.0, 0.0 }, { 200.0, 0.0 }, { 200.0, 200.0 }, { 0.0, 200.0 } }, {}, 0.0, {}, {}, true);
        m_plater->get_collapse_toolbar().set_enabled(false);
        m_plater->enable_sidebar(false);
        m_plater->Show();
        break;
    }
    default:
        break;
    }

    //BBS GUI refactor: remove unused layout new/dlg
//#ifdef __APPLE__
//    // Using SetMinSize() on Mac messes up the window position in some cases
//    // cf. https://groups.google.com/forum/#!topic/wx-users/yUKPBBfXWO0
//    // So, if we haven't possibility to set MinSize() for the MainFrame,
//    // set the MinSize() as a half of regular  for the m_plater and m_tabpanel, when settings layout is in slNew mode
//    // Otherwise, MainFrame will be maximized by height
//    if (m_layout == ESettingsLayout::New) {
//        wxSize size = wxGetApp().get_min_size();
//        size.SetHeight(int(0.5 * size.GetHeight()));
//        m_plater->SetMinSize(size);
//        m_tabpanel->SetMinSize(size);
//    }
//#endif

#ifdef __APPLE__
    m_plater->sidebar().change_top_border_for_mode_sizer(m_layout != ESettingsLayout::Old);
#endif

    Layout();
    Thaw();
}

// Called when closing the application and when switching the application language.
void MainFrame::shutdown()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "MainFrame::shutdown enter";
    // BBS: backup
    Slic3r::set_backup_callback(nullptr);
#ifdef _WIN32
	if (m_hDeviceNotify) {
		::UnregisterDeviceNotification(HDEVNOTIFY(m_hDeviceNotify));
		m_hDeviceNotify = nullptr;
	}
 	if (m_ulSHChangeNotifyRegister) {
        SHChangeNotifyDeregister(m_ulSHChangeNotifyRegister);
        m_ulSHChangeNotifyRegister = 0;
 	}
#endif // _WIN32

    if (m_plater != nullptr) {
        m_plater->get_ui_job_worker().cancel_all();

        // Unbinding of wxWidgets event handling in canvases needs to be done here because on MAC,
        // when closing the application using Command+Q, a mouse event is triggered after this lambda is completed,
        // causing a crash
        m_plater->unbind_canvas_event_handlers();

        // Cleanup of canvases' volumes needs to be done here or a crash may happen on some Linux Debian flavours
        m_plater->reset_canvas_volumes();
    }

    // Weird things happen as the Paint messages are floating around the windows being destructed.
    // Avoid the Paint messages by hiding the main window.
    // Also the application closes much faster without these unnecessary screen refreshes.
    // In addition, there were some crashes due to the Paint events sent to already destructed windows.
    this->Show(false);

    if (m_settings_dialog.IsShown())
        // call Close() to trigger call to lambda defined into GUI_App::persist_window_geometry()
        m_settings_dialog.Close();

    if (m_plater != nullptr) {
        // Stop the background thread (Windows and Linux).
        // Disconnect from a 3DConnextion driver (OSX).
        m_plater->get_mouse3d_controller().shutdown();
        // Store the device parameter database back to appconfig.
        m_plater->get_mouse3d_controller().save_config(*wxGetApp().app_config);
    }

    // stop agent
    NetworkAgent* agent = wxGetApp().getAgent();
    if (agent)
        agent->track_enable(false);

    // Stop the background thread of the removable drive manager, so that no new updates will be sent to the Plater.
    //wxGetApp().removable_drive_manager()->shutdown();
	//stop listening for messages from other instances
	wxGetApp().other_instance_message_handler()->shutdown(this);
    // Save the slic3r.ini.Usually the ini file is saved from "on idle" callback,
    // but in rare cases it may not have been called yet.
    if(wxGetApp().app_config->dirty())
        wxGetApp().app_config->save();
//         if (m_plater)
//             m_plater->print = undef;
//         Slic3r::GUI::deregister_on_request_update_callback();

    // set to null tabs and a plater
    // to avoid any manipulations with them from App->wxEVT_IDLE after of the mainframe closing
    wxGetApp().tabs_list.clear();
    wxGetApp().model_tabs_list.clear();
    wxGetApp().shutdown();
    // BBS: why clear ?
    //wxGetApp().plater_ = nullptr;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "MainFrame::shutdown exit";
}

void MainFrame::update_filament_tab_ui()
{
    wxGetApp().get_tab(Preset::Type::TYPE_FILAMENT)->reload_config();
    wxGetApp().get_tab(Preset::Type::TYPE_FILAMENT)->update_dirty();
    wxGetApp().get_tab(Preset::Type::TYPE_FILAMENT)->update_tab_ui();
}

void MainFrame::update_title()
{
    return;
}

void MainFrame::show_publish_button(bool show)
{
    // m_publish_btn->Show(show);
    // Layout();
}

void MainFrame::update_title_colour_after_set_title()
{
#ifdef __APPLE__
    set_title_colour_after_set_title(GetHandle());
#endif
}

void MainFrame::show_option(bool show)
{
    if (!show) {
        if (m_slice_btn->IsShown()) {
            m_slice_btn->Hide();
            m_print_btn->Hide();
            m_slice_option_btn->Hide();
            m_print_option_btn->Hide();
            if (m_gfd_print_btn != nullptr)
                m_gfd_print_btn->Hide();
            if (m_plater != nullptr && m_plater->gfd_config_panel() != nullptr)
                m_plater->gfd_config_panel()->Hide();
            Layout();
        }
    } else {
        if (!m_slice_btn->IsShown()) {
            m_slice_btn->Show();
            m_print_btn->Show();
            m_slice_option_btn->Show();
            m_print_option_btn->Show();
            update_gfd_print_button();
            update_gfd_config_buttons();
            Layout();
        }
    }
}

void MainFrame::init_tabpanel() {
    // wxNB_NOPAGETHEME: Disable Windows Vista theme for the Notebook background. The theme performance is terrible on
    // Windows 10 with multiple high resolution displays connected.
    // BBS
    wxBoxSizer *side_tools = create_side_tools();
    m_tabpanel = new Notebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, side_tools,
                              wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    m_tabpanel->SetBackgroundColour(*wxWHITE);

#ifndef __WXOSX__ // Don't call SetFont under OSX to avoid name cutting in ObjectList
    m_tabpanel->SetFont(Slic3r::GUI::wxGetApp().normal_font());
#endif
    m_tabpanel->Hide();
    m_settings_dialog.set_tabpanel(m_tabpanel);

#ifdef __WXMSW__
    m_tabpanel->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
#else
    m_tabpanel->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
#endif
        //BBS
        wxWindow* panel = m_tabpanel->GetCurrentPage();
        int sel = m_tabpanel->GetSelection();
        //wxString page_text = m_tabpanel->GetPageText(sel);
        m_last_selected_tab = m_tabpanel->GetSelection();
        if (panel == m_plater) {
            if (sel == tp3DEditor) {
                wxPostEvent(m_plater, SimpleEvent(EVT_GLVIEWTOOLBAR_3D));
                m_param_panel->OnActivate();
            }
            else if (sel == tpPreview) {
                wxPostEvent(m_plater, SimpleEvent(EVT_GLVIEWTOOLBAR_PREVIEW));
                m_param_panel->OnActivate();
            }
        }
        else if (panel == m_webview) {
            m_webview->refresh_view();
        }
        //else if (panel == m_param_panel)
        //    m_param_panel->OnActivate();
        else if (panel == m_monitor) {
            //monitor
        }
#ifndef __APPLE__
        if (sel == tp3DEditor) {
            m_topbar->EnableUndoRedoItems();
        }
        else {
            m_topbar->DisableUndoRedoItems();
        }
#endif

        if (panel)
            panel->SetFocus();

        /*switch (sel) {
        case TabPosition::tpHome:
            show_option(false);
            break;
        case TabPosition::tp3DEditor:
            show_option(true);
            break;
        case TabPosition::tpPreview:
            show_option(true);
            break;
        case TabPosition::tpMonitor:
            show_option(false);
            break;
        default:
            show_option(false);
            break;
        }*/
    });

    if (wxGetApp().is_editor()) {
        m_webview         = new WebViewPanel(m_tabpanel);
        Bind(EVT_LOAD_URL, [this](wxCommandEvent &evt) {
            wxString url = evt.GetString();
            select_tab(MainFrame::tpHome);
            m_webview->load_url(url);
        });
        m_tabpanel->AddPage(m_webview, "", "tab_home_active", "tab_home_active", false);
        m_param_panel = new ParamsPanel(m_tabpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    }

    m_plater = new Plater(this, this);
    m_plater->SetBackgroundColour(*wxWHITE);
    m_plater->Hide();

    wxGetApp().plater_ = m_plater;

    create_preset_tabs();

        //BBS add pages
    m_monitor = new MonitorPanel(m_tabpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_monitor->SetBackgroundColour(*wxWHITE);
    m_tabpanel->AddPage(m_monitor, _L("Device"), std::string("tab_monitor_active"), std::string("tab_monitor_active"), false);

    m_printer_view = new PrinterWebView(m_tabpanel);
    Bind(EVT_LOAD_PRINTER_URL, [this](LoadPrinterViewEvent &evt) {
        wxString url = evt.GetString();
        wxString key = evt.GetAPIkey();
        //select_tab(MainFrame::tpMonitor);
        m_printer_view->load_url(url, key);
    });
    m_printer_view->Hide();

    if (wxGetApp().is_enable_multi_machine()) {
        m_multi_machine = new MultiMachinePage(m_tabpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        m_multi_machine->SetBackgroundColour(*wxWHITE);
        // TODO: change the bitmap
        m_tabpanel->AddPage(m_multi_machine, _L("Multi-device"), std::string("tab_multi_active"), std::string("tab_multi_active"), false);
    }

    m_project = new ProjectPanel(m_tabpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_project->SetBackgroundColour(*wxWHITE);
    m_tabpanel->AddPage(m_project, _L("Project"), std::string("tab_auxiliary_active"), std::string("tab_auxiliary_active"), false);

    m_calibration = new CalibrationPanel(m_tabpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_calibration->SetBackgroundColour(*wxWHITE);
    m_tabpanel->AddPage(m_calibration, _L("Calibration"), std::string("tab_calibration_active"), std::string("tab_calibration_active"), false);

    if (m_plater) {
        // load initial config
        auto full_config = wxGetApp().preset_bundle->full_config();
        m_plater->on_config_change(full_config);

        // Show a correct number of filament fields.
        // nozzle_diameter is undefined when SLA printer is selected
        // BBS
        if (full_config.has("filament_colour")) {
            m_plater->on_filaments_change(full_config.option<ConfigOptionStrings>("filament_colour")->values.size());
        }
    }

    bind_gfd_config_buttons();
    update_gfd_config_buttons();
}

// SoftFever
void MainFrame::show_device(bool bBBLPrinter) {
    auto idx = -1;
    if (bBBLPrinter) {
        if (m_tabpanel->FindPage(m_monitor) != wxNOT_FOUND)
            return;
        // Remove printer view
        if ((idx = m_tabpanel->FindPage(m_printer_view)) != wxNOT_FOUND) {
            m_printer_view->Show(false);
            m_tabpanel->RemovePage(idx);
        }

        // Create/insert monitor page
        if (!m_monitor) {
            m_monitor = new MonitorPanel(m_tabpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
            m_monitor->SetBackgroundColour(*wxWHITE);
        }
        m_monitor->Show(false);
        m_tabpanel->InsertPage(tpMonitor, m_monitor, _L("Device"), std::string("tab_monitor_active"), std::string("tab_monitor_active"));

        if (wxGetApp().is_enable_multi_machine()) {
            if (!m_multi_machine) {
                m_multi_machine = new MultiMachinePage(m_tabpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
                m_multi_machine->SetBackgroundColour(*wxWHITE);
            }
            // TODO: change the bitmap
            m_multi_machine->Show(false);
            m_tabpanel->InsertPage(tpMultiDevice, m_multi_machine, _L("Multi-device"), std::string("tab_multi_active"),
                                   std::string("tab_multi_active"), false);
        }
        if (!m_calibration) {
            m_calibration = new CalibrationPanel(m_tabpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
            m_calibration->SetBackgroundColour(*wxWHITE);
        }
        m_calibration->Show(false);
        // Calibration is always the last page, so don't use InsertPage here. Otherwise, if multi_machine page is not enabled,
        // the calibration tab won't be properly added as well, due to the TabPosition::tpCalibration no longer matches the real tab position.
        m_tabpanel->AddPage(m_calibration, _L("Calibration"), std::string("tab_calibration_active"),
                               std::string("tab_calibration_active"), false);

#ifdef _MSW_DARK_MODE
        wxGetApp().UpdateDarkUIWin(this);
#endif // _MSW_DARK_MODE

    } else {
        if (m_tabpanel->FindPage(m_printer_view) != wxNOT_FOUND)
            return;

        if ((idx = m_tabpanel->FindPage(m_calibration)) != wxNOT_FOUND) {
            m_calibration->Show(false);
            m_tabpanel->RemovePage(idx);
        }
        if ((idx = m_tabpanel->FindPage(m_multi_machine)) != wxNOT_FOUND) {
            m_multi_machine->Show(false);
            m_tabpanel->RemovePage(idx);
        }
        if ((idx = m_tabpanel->FindPage(m_monitor)) != wxNOT_FOUND) {
            m_monitor->Show(false);
            m_tabpanel->RemovePage(idx);
        }
        if (m_printer_view == nullptr) {
            m_printer_view = new PrinterWebView(m_tabpanel);
            Bind(EVT_LOAD_PRINTER_URL, [this](LoadPrinterViewEvent& evt) {
                wxString url = evt.GetString();
                wxString key = evt.GetAPIkey();
                // select_tab(MainFrame::tpMonitor);
                m_printer_view->load_url(url, key);
            });
        }
        m_printer_view->Show(false);
        m_tabpanel->InsertPage(tpMonitor, m_printer_view, _L("Device"), std::string("tab_monitor_active"),
                               std::string("tab_monitor_active"));
    }
}

bool MainFrame::preview_only_hint()
{
    if (m_plater && (m_plater->only_gcode_mode() || (m_plater->using_exported_file()))) {
        BOOST_LOG_TRIVIAL(info) << boost::format("skipped tab switch from %1% to %2% in preview mode")%m_tabpanel->GetSelection() %tp3DEditor;

        ConfirmBeforeSendDialog confirm_dlg(this, wxID_ANY, _L("Warning"));
        confirm_dlg.Bind(EVT_SECONDARY_CHECK_CONFIRM, [this](wxCommandEvent& e) {
            preview_only_to_editor = true;
        });
        confirm_dlg.update_btn_label(_L("Yes"), _L("No"));
        auto filename = m_plater->get_preview_only_filename();

        confirm_dlg.update_text(filename + " " + _L("will be closed before creating a new model. Do you want to continue?"));
        confirm_dlg.on_show();
        if (preview_only_to_editor) {
            m_plater->new_project();
            preview_only_to_editor = false;
        }

        return false;
    }

    return true;
}

#ifdef WIN32
void MainFrame::register_win32_callbacks()
{
    //static GUID GUID_DEVINTERFACE_USB_DEVICE  = { 0xA5DCBF10, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED };
    //static GUID GUID_DEVINTERFACE_DISK        = { 0x53f56307, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b };
    //static GUID GUID_DEVINTERFACE_VOLUME      = { 0x71a27cdd, 0x812a, 0x11d0, 0xbe, 0xc7, 0x08, 0x00, 0x2b, 0xe2, 0x09, 0x2f };
    static GUID GUID_DEVINTERFACE_HID           = { 0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 };

    // Register USB HID (Human Interface Devices) notifications to trigger the 3DConnexion enumeration.
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter = { 0 };
    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_HID;
    m_hDeviceNotify = ::RegisterDeviceNotification(this->GetHWND(), &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

// or register for file handle change?
//      DEV_BROADCAST_HANDLE NotificationFilter = { 0 };
//      NotificationFilter.dbch_size = sizeof(DEV_BROADCAST_HANDLE);
//      NotificationFilter.dbch_devicetype = DBT_DEVTYP_HANDLE;

    // Using Win32 Shell API to register for media insert / removal events.
    LPITEMIDLIST ppidl;
    if (SHGetSpecialFolderLocation(this->GetHWND(), CSIDL_DESKTOP, &ppidl) == NOERROR) {
        SHChangeNotifyEntry shCNE;
        shCNE.pidl       = ppidl;
        shCNE.fRecursive = TRUE;
        // Returns a positive integer registration identifier (ID).
        // Returns zero if out of memory or in response to invalid parameters.
        m_ulSHChangeNotifyRegister = SHChangeNotifyRegister(this->GetHWND(),        // Hwnd to receive notification
            SHCNE_DISKEVENTS,                                                       // Event types of interest (sources)
            SHCNE_MEDIAINSERTED | SHCNE_MEDIAREMOVED,
            //SHCNE_UPDATEITEM,                                                     // Events of interest - use SHCNE_ALLEVENTS for all events
            WM_USER_MEDIACHANGED,                                                   // Notification message to be sent upon the event
            1,                                                                      // Number of entries in the pfsne array
            &shCNE);                                                                // Array of SHChangeNotifyEntry structures that
                                                                                    // contain the notifications. This array should
                                                                                    // always be set to one when calling SHChnageNotifyRegister
                                                                                    // or SHChangeNotifyDeregister will not work properly.
        assert(m_ulSHChangeNotifyRegister != 0);    // Shell notification failed
    } else {
        // Failed to get desktop location
        assert(false);
    }

    {
        static constexpr int device_count = 1;
        RAWINPUTDEVICE devices[device_count] = { 0 };
        // multi-axis mouse (SpaceNavigator, etc.)
        devices[0].usUsagePage = 0x01;
        devices[0].usUsage = 0x08;
        if (! RegisterRawInputDevices(devices, device_count, sizeof(RAWINPUTDEVICE)))
            BOOST_LOG_TRIVIAL(error) << "RegisterRawInputDevices failed";
    }
}
#endif // _WIN32

void MainFrame::create_preset_tabs()
{
    wxGetApp().update_label_colours_from_appconfig();

    //BBS: GUI refactor
    //m_param_panel = new ParamsPanel(m_tabpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    m_param_dialog = new ParamsDialog(m_plater);

    add_created_tab(new TabPrint(m_param_panel), "cog");
    add_created_tab(new TabPrintPlate(m_param_panel), "cog");
    add_created_tab(new TabPrintObject(m_param_panel), "cog");
    add_created_tab(new TabPrintPart(m_param_panel), "cog");
    add_created_tab(new TabPrintLayer(m_param_panel), "cog");
    add_created_tab(new TabFilament(m_param_dialog->panel()), "spool");
    /* BBS work around to avoid appearance bug */
    //add_created_tab(new TabSLAPrint(m_param_panel));
    //add_created_tab(new TabSLAMaterial(m_param_panel));
    add_created_tab(new TabPrinter(m_param_dialog->panel()), "printer");

    m_param_panel->rebuild_panels();
    m_param_dialog->panel()->rebuild_panels();
    //m_tabpanel->AddPage(m_param_panel, "Parameters", "notebook_presets_active");
    //m_tabpanel->InsertPage(tpSettings, m_param_panel, _L("Parameters"), std::string("cog"));
}

void MainFrame::add_created_tab(Tab* panel,  const std::string& bmp_name /*= ""*/)
{
    panel->create_preset_tab();

    if (panel->type() == Preset::TYPE_PLATE) {
        wxGetApp().tabs_list.pop_back();
        wxGetApp().plate_tab = panel;
    }
    // BBS: model config
    if (panel->type() == Preset::TYPE_MODEL) {
        wxGetApp().tabs_list.pop_back();
        wxGetApp().model_tabs_list.push_back(panel);
    }
}

bool MainFrame::is_active_and_shown_tab(wxPanel* panel)
{
    if (panel == m_param_panel)
        panel = m_plater;
    else
        return m_param_dialog->IsShown();

    if (m_tabpanel->GetCurrentPage() != panel)
        return false;
    return true;
}

bool MainFrame::can_start_new_project() const
{
    /*return m_plater && (!m_plater->get_project_filename(".3mf").IsEmpty() ||
                        GetTitle().StartsWith('*')||
                        wxGetApp().has_current_preset_changes() ||
                        !m_plater->model().objects.empty());*/
    return (m_plater && !m_plater->is_background_process_slicing());
}

bool MainFrame::can_open_project() const
{
    return (m_plater && !m_plater->is_background_process_slicing());
}

bool  MainFrame::can_add_models() const
{
    return (m_plater && !m_plater->is_background_process_slicing() && !m_plater->only_gcode_mode() && !m_plater->using_exported_file());
}

bool MainFrame::can_save() const
{
    return (m_plater != nullptr) &&
        !m_plater->get_view3D_canvas3D()->get_gizmos_manager().is_in_editing_mode(false) &&
        m_plater->is_project_dirty() && !m_plater->using_exported_file() && !m_plater->only_gcode_mode();
}

bool MainFrame::can_save_as() const
{
    return (m_plater != nullptr) &&
        !m_plater->get_view3D_canvas3D()->get_gizmos_manager().is_in_editing_mode(false) && !m_plater->using_exported_file() && !m_plater->only_gcode_mode();
}

void MainFrame::save_project()
{
    save_project_as(m_plater->get_project_filename(".3mf"));
}

bool MainFrame::save_project_as(const wxString& filename)
{
    bool ret = (m_plater != nullptr) ? m_plater->export_3mf(into_path(filename)) : false;
    if (ret) {
//        wxGetApp().update_saved_preset_from_current_preset();
        m_plater->reset_project_dirty_after_save();
    }
    return ret;
}

bool MainFrame::can_upload() const
{
    return true;
}

bool MainFrame::can_export_model() const
{
    return (m_plater != nullptr) && !m_plater->model().objects.empty();
}

bool MainFrame::can_export_toolpaths() const
{
    return (m_plater != nullptr) && (m_plater->printer_technology() == ptFFF) && m_plater->is_preview_shown() && m_plater->is_preview_loaded() && m_plater->has_toolpaths_to_export();
}

bool MainFrame::can_export_supports() const
{
    if ((m_plater == nullptr) || (m_plater->printer_technology() != ptSLA) || m_plater->model().objects.empty())
        return false;

    bool can_export = false;
    const PrintObjects& objects = m_plater->sla_print().objects();
    for (const SLAPrintObject* object : objects)
    {
        if (object->has_mesh(slaposPad) || object->has_mesh(slaposSupportTree))
        {
            can_export = true;
            break;
        }
    }
    return can_export;
}

bool MainFrame::can_export_gcode() const
{
    if (m_plater == nullptr)
        return false;

    if (m_plater->model().objects.empty())
        return false;

    if (m_plater->is_export_gcode_scheduled())
        return false;

    // TODO:: add other filters
    PartPlateList &part_plate_list = m_plater->get_partplate_list();
    PartPlate *current_plate = part_plate_list.get_curr_plate();
    if (!current_plate->is_slice_result_ready_for_print())
        return false;

    return true;
}

bool MainFrame::can_export_all_gcode() const
{
    if (m_plater == nullptr)
        return false;

    if (m_plater->model().objects.empty())
        return false;

    if (m_plater->is_export_gcode_scheduled())
        return false;

    // TODO:: add other filters
    PartPlateList& part_plate_list = m_plater->get_partplate_list();
    return part_plate_list.is_all_slice_results_ready_for_print();
}

bool MainFrame::can_print_3mf() const
{
    if (m_plater && !m_plater->model().objects.empty()) {
        if (wxGetApp().preset_bundle->printers.get_edited_preset().is_custom_defined())
            return false;
    }
    return true;
}

bool MainFrame::can_send_gcode() const
{
    if (m_plater && !m_plater->model().objects.empty())
    {
        auto cfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        if (const auto *print_host_opt = cfg.option<ConfigOptionString>("print_host"); print_host_opt)
            return !print_host_opt->value.empty();
    }
    return true;
}

/*bool MainFrame::can_export_gcode_sd() const
{
    if (m_plater == nullptr)
        return false;

    if (m_plater->model().objects.empty())
        return false;

    if (m_plater->is_export_gcode_scheduled())
        return false;

    // TODO:: add other filters

    return wxGetApp().removable_drive_manager()->status().has_removable_drives;
}

bool MainFrame::can_eject() const
{
	return wxGetApp().removable_drive_manager()->status().has_eject;
}*/

bool MainFrame::can_slice() const
{
#ifdef SUPPORT_BACKGROUND_PROCESSING
    bool bg_proc = wxGetApp().app_config->get("background_processing") == "1";
    return (m_plater != nullptr) ? !m_plater->model().objects.empty() && !bg_proc : false;
#else
    return (m_plater != nullptr) ? !m_plater->model().objects.empty() : false;
#endif
}

bool MainFrame::can_change_view() const
{
    switch (m_layout)
    {
    default:                   { return false; }
    //BBS GUI refactor: remove unused layout new/dlg
    case ESettingsLayout::Old: {
        int page_id = m_tabpanel->GetSelection();
        return page_id != wxNOT_FOUND && dynamic_cast<const Slic3r::GUI::Plater*>(m_tabpanel->GetPage((size_t)page_id)) != nullptr;
    }
    case ESettingsLayout::GCodeViewer: { return true; }
    }
}

bool MainFrame::can_clone() const {
    return can_select() && !m_plater->is_selection_empty();
}

bool MainFrame::can_select() const
{
    return (m_plater != nullptr) && (m_tabpanel->GetSelection() == TabPosition::tp3DEditor) && !m_plater->model().objects.empty();
}

bool MainFrame::can_deselect() const
{
    return (m_plater != nullptr) && (m_tabpanel->GetSelection() == TabPosition::tp3DEditor) && !m_plater->is_selection_empty();
}

bool MainFrame::can_delete() const
{
    return (m_plater != nullptr) && (m_tabpanel->GetSelection() == TabPosition::tp3DEditor) && !m_plater->is_selection_empty();
}

bool MainFrame::can_delete_all() const
{
    return (m_plater != nullptr) && (m_tabpanel->GetSelection() == TabPosition::tp3DEditor) && !m_plater->model().objects.empty();
}

bool MainFrame::can_reslice() const
{
    return (m_plater != nullptr) && !m_plater->model().objects.empty();
}

wxBoxSizer* MainFrame::create_side_tools()
{
    enable_multi_machine = wxGetApp().is_enable_multi_machine();
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

    m_slice_select = eSlicePlate;
    m_print_select = ePrintPlate;

    // m_publish_btn = new Button(this, _L("Upload"), "bar_publish", 0, FromDIP(16));
    m_slice_btn = new SideButton(this, _L("Slice plate"), "");
    m_slice_option_btn = new SideButton(this, "", "sidebutton_dropdown", 0, 14);
    m_print_btn = new SideButton(this, _L("Print plate"), "");
    m_print_option_btn = new SideButton(this, "", "sidebutton_dropdown", 0, 14);
    m_gfd_print_btn = new SideButton(this, _L("Print"), "");

    update_side_button_style();
    // m_publish_btn->Hide();
    m_slice_option_btn->Enable();
    m_print_option_btn->Enable();
    // sizer->Add(m_publish_btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(1));
    // sizer->Add(FromDIP(15), 0, 0, 0, 0);
    sizer->Add(m_slice_option_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    sizer->Add(m_slice_btn       , 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(15));
    sizer->Add(m_print_option_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    sizer->Add(m_print_btn       , 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(15));
    sizer->Add(m_gfd_print_btn   , 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(19));

    sizer->Layout();

    // m_publish_btn->Bind(wxEVT_BUTTON, [this](auto& e) {
    //     CallAfter([this] {
    //         wxGetApp().open_publish_page_dialog();

    //         if (!wxGetApp().getAgent()) {
    //             BOOST_LOG_TRIVIAL(info) << "publish: no agent";
    //             return;
    //         }

    //         // record
    //         json j;
    //         NetworkAgent* agent = GUI::wxGetApp().getAgent();
    //     });
    // });

    m_slice_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            //this->m_plater->select_view_3D("Preview");
            m_plater->exit_gizmo();
            m_plater->update(true, true);
            if (m_slice_select == eSliceAll)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_ALL));
            else
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_PLATE));

            this->m_tabpanel->SetSelection(tpPreview);
        });

    m_print_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            //this->m_plater->select_view_3D("Preview");
            if (m_print_select == ePrintAll || m_print_select == ePrintPlate || m_print_select == ePrintMultiMachine)
            {
                m_plater->apply_background_progress();
                // check valid of print
                m_print_enable = get_enable_print_status();
                m_print_btn->Enable(m_print_enable);
                if (m_print_enable) {
                    if (m_print_select == ePrintAll)
                        wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_PRINT_ALL));
                    if (m_print_select == ePrintPlate)
                        wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_PRINT_PLATE));
                    if(m_print_select == ePrintMultiMachine)
                         wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_PRINT_MULTI_MACHINE));
                }
            }
            else if (m_print_select == eExportGcode)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_GCODE));
            else if (m_print_select == eSendGcode)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SEND_GCODE));
            else if (m_print_select == eUploadGcode)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_UPLOAD_GCODE));
            else if (m_print_select == eExportSlicedFile)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_SLICED_FILE));
            else if (m_print_select == eExportAllSlicedFile)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_ALL_SLICED_FILE));
            else if (m_print_select == eSendToPrinter)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SEND_TO_PRINTER));
            else if (m_print_select == eSendToPrinterAll)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SEND_TO_PRINTER_ALL));
            /* else if (m_print_select == ePrintMultiMachine)
                 wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_PRINT_MULTI_MACHINE));*/
        });

    m_gfd_print_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            m_plater->apply_background_progress();
            const bool enable_gfd_print = get_enable_gfd_print_status();
            m_gfd_print_btn->Enable(enable_gfd_print);
            BOOST_LOG_TRIVIAL(info) << "GFD print button clicked"
                                    << ", enable=" << enable_gfd_print;
            if (enable_gfd_print)
                wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_PRINT_PLATE));
            else
                GUI::show_error(this, _L("请先完成切片后再打印。"));
        });

    m_slice_option_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            SidePopup* p = new SidePopup(this);
            SideButton* slice_all_btn = new SideButton(p, _L("Slice all"), "");
            slice_all_btn->SetCornerRadius(0);
            SideButton* slice_plate_btn = new SideButton(p, _L("Slice plate"), "");
            slice_plate_btn->SetCornerRadius(0);

            slice_all_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                m_slice_btn->SetLabel(_L("Slice all"));
                m_slice_select = eSliceAll;
                m_slice_enable = get_enable_slice_status();
                m_slice_btn->Enable(m_slice_enable);
                this->Layout();
                p->Dismiss();
                });

            slice_plate_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                m_slice_btn->SetLabel(_L("Slice plate"));
                m_slice_select = eSlicePlate;
                m_slice_enable = get_enable_slice_status();
                m_slice_btn->Enable(m_slice_enable);
                this->Layout();
                p->Dismiss();
                });
            p->append_button(slice_all_btn);
            p->append_button(slice_plate_btn);
            p->Popup(m_slice_btn);
        }
    );

    m_print_option_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            SidePopup* p = new SidePopup(this);
            const auto preset_bundle = wxGetApp().preset_bundle;

            const bool is_gfd_printer = preset_bundle &&
                                        GFD::Config::is_gfd_printer(preset_bundle->printers.get_edited_preset().config);

            if (preset_bundle && !preset_bundle->is_bbl_vendor() && !is_gfd_printer) {
                // ThirdParty Buttons
                SideButton* export_gcode_btn = new SideButton(p, _L("Export G-code file"), "");
                export_gcode_btn->SetCornerRadius(0);
                export_gcode_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                    m_print_btn->SetLabel(_L("Export G-code file"));
                    m_print_select = eExportGcode;
                    m_print_enable = get_enable_print_status();
                    m_print_btn->Enable(m_print_enable);
                    this->Layout();
                    p->Dismiss();
                    });

                // upload and print
                SideButton* send_gcode_btn = new SideButton(p, _L("Print"), "");
                send_gcode_btn->SetCornerRadius(0);
                send_gcode_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                    m_print_btn->SetLabel(_L("Print"));
                    m_print_select = eSendGcode;
                    m_print_enable = get_enable_print_status();
                    m_print_btn->Enable(m_print_enable);
                    this->Layout();
                    p->Dismiss();
                    });

                p->append_button(send_gcode_btn);
                p->append_button(export_gcode_btn);
            }
            else {
                //Orca Slicer Buttons
                SideButton* print_plate_btn = new SideButton(p, _L("Print plate"), "");
                print_plate_btn->SetCornerRadius(0);

                SideButton* send_to_printer_btn = new SideButton(p, _L("Send"), "");
                send_to_printer_btn->SetCornerRadius(0);

                SideButton* export_sliced_file_btn = new SideButton(p, _L("Export plate sliced file"), "");
                export_sliced_file_btn->SetCornerRadius(0);

                SideButton* export_all_sliced_file_btn = new SideButton(p, _L("Export all sliced file"), "");
                export_all_sliced_file_btn->SetCornerRadius(0);

                print_plate_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                    m_print_btn->SetLabel(_L("Print plate"));
                    m_print_select = ePrintPlate;
                    m_print_enable = get_enable_print_status();
                    m_print_btn->Enable(m_print_enable);
                    this->Layout();
                    p->Dismiss();
                    });

                SideButton* print_all_btn = new SideButton(p, _L("Print all"), "");
                print_all_btn->SetCornerRadius(0);
                print_all_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                    m_print_btn->SetLabel(_L("Print all"));
                    m_print_select = ePrintAll;
                    m_print_enable = get_enable_print_status();
                    m_print_btn->Enable(m_print_enable);
                    this->Layout();
                    p->Dismiss();
                    });

                send_to_printer_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                    m_print_btn->SetLabel(_L("Send"));
                    m_print_select = eSendToPrinter;
                    m_print_enable = get_enable_print_status();
                    m_print_btn->Enable(m_print_enable);
                    this->Layout();
                    p->Dismiss();
                    });

                SideButton* send_to_printer_all_btn = new SideButton(p, _L("Send all"), "");
                send_to_printer_all_btn->SetCornerRadius(0);
                send_to_printer_all_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                    m_print_btn->SetLabel(_L("Send all"));
                    m_print_select = eSendToPrinterAll;
                    m_print_enable = get_enable_print_status();
                    m_print_btn->Enable(m_print_enable);
                    this->Layout();
                    p->Dismiss();
                    });

                export_sliced_file_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                    m_print_btn->SetLabel(_L("Export plate sliced file"));
                    m_print_select = eExportSlicedFile;
                    m_print_enable = get_enable_print_status();
                    m_print_btn->Enable(m_print_enable);
                    this->Layout();
                    p->Dismiss();
                    });

                export_all_sliced_file_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                    m_print_btn->SetLabel(_L("Export all sliced file"));
                    m_print_select = eExportAllSlicedFile;
                    m_print_enable = get_enable_print_status();
                    m_print_btn->Enable(m_print_enable);
                    this->Layout();
                    p->Dismiss();
                    });

                bool support_send = true;
                bool support_print_all = true;

                const auto preset_bundle = wxGetApp().preset_bundle;
                if (preset_bundle) {
                    if (preset_bundle->use_bbl_network()) {
                        // BBL network support everything
                    } else {
                        support_send = false; // All 3rd print hosts do not have the send options

                        auto cfg = preset_bundle->printers.get_edited_preset().config;
                        const auto host_type = cfg.option<ConfigOptionEnum<PrintHostType>>("host_type")->value;

                        // Only simply print support uploading all plates
                        support_print_all = host_type == PrintHostType::htSimplyPrint;
                    }
                }

                p->append_button(print_plate_btn);
                if (support_print_all) {
                    p->append_button(print_all_btn);
                }
                if (support_send) {
                    p->append_button(send_to_printer_btn);
                    p->append_button(send_to_printer_all_btn);
                }
                if (enable_multi_machine) {
                    SideButton* print_multi_machine_btn = new SideButton(p, _L("Send to Multi-device"), "");
                    print_multi_machine_btn->SetCornerRadius(0);
                    print_multi_machine_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                        m_print_btn->SetLabel(_L("Send to Multi-device"));
                        m_print_select = ePrintMultiMachine;
                        m_print_enable = get_enable_print_status();
                        m_print_btn->Enable(m_print_enable);
                        this->Layout();
                        p->Dismiss();
                    });
                    p->append_button(print_multi_machine_btn);
                }
                p->append_button(export_sliced_file_btn);
                p->append_button(export_all_sliced_file_btn);
                SideButton* export_gcode_btn = new SideButton(p, _L("Export G-code file"), "");
                export_gcode_btn->SetCornerRadius(0);
                export_gcode_btn->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) {
                    m_print_btn->SetLabel(_L("Export G-code file"));
                    m_print_select = eExportGcode;
                    m_print_enable = get_enable_print_status();
                    m_print_btn->Enable(m_print_enable);
                    this->Layout();
                    p->Dismiss();
                });
                p->append_button(export_gcode_btn);
            }

            p->Popup(m_print_btn);
        }
    );

    /*
    Button * aux_btn = new Button(this, _L("Auxiliary"));
    aux_btn->SetBackgroundColour(0x3B4446);
    aux_btn->Bind(wxEVT_BUTTON, [](auto e) {
        wxGetApp().sidebar().show_auxiliary_dialog();
    });
    sizer->Add(aux_btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 1 * em / 10);
    */
    sizer->Add(FromDIP(19), 0, 0, 0, 0);
    update_gfd_print_button();
    update_gfd_config_buttons();

    return sizer;
}

bool MainFrame::get_enable_slice_status()
{
    bool enable = true;

    bool on_slicing = m_plater->is_background_process_slicing();
    if (on_slicing) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": on slicing, return false directly!");
        return false;
    }
    else if  (m_plater->only_gcode_mode() || m_plater->using_exported_file()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": in gcode/exported 3mf mode, return false directly!");
        return false;
    }

    PartPlateList &part_plate_list = m_plater->get_partplate_list();
    PartPlate *current_plate = part_plate_list.get_curr_plate();

    if (m_slice_select == eSliceAll)
    {
        /*if (part_plate_list.is_all_slice_results_valid())
        {
            enable = false;
        }
        else if (!part_plate_list.is_all_plates_ready_for_slice())
        {
            enable = false;
        }*/
        //always enable slice_all button
        enable = true;
    }
    else if (m_slice_select == eSlicePlate)
    {
        if (current_plate->is_slice_result_valid())
        {
            enable = false;
        }
        else if (!current_plate->can_slice())
        {
            enable = false;
        }
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": m_slice_select %1%, enable= %2% ")%m_slice_select %enable;
    return enable;
}

bool MainFrame::get_enable_print_status()
{
    bool enable = true;

    PartPlateList &part_plate_list = m_plater->get_partplate_list();
    PartPlate *current_plate = part_plate_list.get_curr_plate();
    bool is_all_plates = wxGetApp().plater()->get_preview_canvas3D()->is_all_plates_selected();
    const auto* printer_cfg = wxGetApp().preset_bundle ? &wxGetApp().preset_bundle->printers.get_edited_preset().config : nullptr;
    const auto* printer_model_opt = printer_cfg ? printer_cfg->option<ConfigOptionString>("printer_model") : nullptr;
    const auto* printer_settings_id_opt = printer_cfg ? printer_cfg->option<ConfigOptionString>("printer_settings_id") : nullptr;
    if (m_print_select == ePrintAll)
    {
        if (!part_plate_list.is_all_slice_results_ready_for_print())
        {
            enable = false;
        }
    }
    else if (m_print_select == ePrintPlate)
    {
        if (!current_plate->is_slice_result_ready_for_print()) {
            enable = false;
        }
        enable = enable && !is_all_plates;
    }
    else if (m_print_select == eExportGcode)
    {
        if (!current_plate->is_slice_result_valid())
        {
            enable = false;
        }
        enable = enable && !is_all_plates;
    }
    else if (m_print_select == eSendGcode)
    {
        if (!current_plate->is_slice_result_valid())
            enable = false;
        if (!can_send_gcode())
            enable = false;
        enable = enable && !is_all_plates;
    }
    else if (m_print_select == eUploadGcode)
    {
        if (!current_plate->is_slice_result_valid())
            enable = false;
        if (!can_send_gcode())
            enable = false;
        enable = enable && !is_all_plates;
    }
    else if (m_print_select == eExportSlicedFile)
    {
        if (!current_plate->is_slice_result_ready_for_export())
        {
            enable = false;
        }
        enable = enable && !is_all_plates;
	}
	else if (m_print_select == eSendToPrinter)
	{
		if (!current_plate->is_slice_result_ready_for_print())
		{
			enable = false;
		}
        enable = enable && !is_all_plates;
	}
    else if (m_print_select == eSendToPrinterAll)
    {
        if (!part_plate_list.is_all_slice_results_ready_for_print())
        {
            enable = false;
        }
    }
    else if (m_print_select == eExportAllSlicedFile)
    {
        if (!part_plate_list.is_all_slice_result_ready_for_export())
        {
            enable = false;
        }
    }
    else if (m_print_select == ePrintMultiMachine)
    {
        if (!current_plate->is_slice_result_ready_for_print())
        {
            enable = false;
        }
        enable = enable && !is_all_plates;
    }

    BOOST_LOG_TRIVIAL(info) << "Print enable status"
                            << ", print_select=" << static_cast<int>(m_print_select)
                            << ", printer_model="
                            << (printer_model_opt ? printer_model_opt->value : std::string("<null>"))
                            << ", printer_settings_id="
                            << (printer_settings_id_opt ? printer_settings_id_opt->value : std::string("<null>"))
                            << ", is_slice_result_valid=" << current_plate->is_slice_result_valid()
                            << ", is_slice_result_ready_for_print=" << current_plate->is_slice_result_ready_for_print()
                            << ", has_printable_instances=" << current_plate->has_printable_instances()
                            << ", is_all_plates=" << is_all_plates
                            << ", enable=" << enable;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": m_print_select %1%, enable= %2% ")%m_print_select %enable;

    return enable;
}

bool MainFrame::get_enable_gfd_print_status()
{
    if (m_plater == nullptr || wxGetApp().preset_bundle == nullptr)
        return false;

    const GFDPrinterState printer_state = current_gfd_printer_state();
    if (printer_state.effective_device_type.empty())
        return false;

    PartPlate* current_plate = m_plater->get_partplate_list().get_curr_plate();
    if (current_plate == nullptr)
        return false;

    const bool is_all_plates = m_plater->get_preview_canvas3D()->is_all_plates_selected();
    const bool has_valid_gcode = current_plate->is_valid_gcode_file();
    const bool enable = current_plate->is_slice_result_valid() && !is_all_plates &&
                        (current_plate->has_printable_instances() || has_valid_gcode);

    BOOST_LOG_TRIVIAL(info) << "GFD print enable status"
                            << ", enable=" << enable
                            << ", effective_printer_model=" << printer_state.effective_printer_model
                            << ", effective_gfd_device_type="
                            << (printer_state.effective_device_type.empty() ? std::string("<empty>") : printer_state.effective_device_type)
                            << ", is_slice_result_valid=" << current_plate->is_slice_result_valid()
                            << ", has_printable_instances=" << current_plate->has_printable_instances()
                            << ", has_valid_gcode=" << has_valid_gcode
                            << ", is_all_plates=" << is_all_plates;
    return enable;
}

void MainFrame::update_side_button_style()
{
    // BBS
    int em = em_unit();

    /*m_slice_btn->SetLayoutStyle(1);
    m_slice_btn->SetTextLayout(SideButton::EHorizontalOrientation::HO_Center, FromDIP(15));
    m_slice_btn->SetMinSize(wxSize(-1, FromDIP(24)));
    m_slice_btn->SetCornerRadius(FromDIP(12));
    m_slice_btn->SetExtraSize(wxSize(FromDIP(38), FromDIP(10)));
    m_slice_btn->SetBottomColour(wxColour(0x3B4446));*/
    StateColor m_btn_bg_enable = StateColor(
        std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(48, 221, 112), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    // m_publish_btn->SetMinSize(wxSize(FromDIP(125), FromDIP(24)));
    // m_publish_btn->SetCornerRadius(FromDIP(12));
    // m_publish_btn->SetBackgroundColor(m_btn_bg_enable);
    // m_publish_btn->SetBorderColor(m_btn_bg_enable);
    // m_publish_btn->SetBackgroundColour(wxColour(59,68,70));
    // m_publish_btn->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));

    m_slice_btn->SetTextLayout(SideButton::EHorizontalOrientation::HO_Left, FromDIP(15));
    m_slice_btn->SetCornerRadius(FromDIP(12));
    m_slice_btn->SetExtraSize(wxSize(FromDIP(38), FromDIP(10)));
    m_slice_btn->SetMinSize(wxSize(-1, FromDIP(24)));

    m_slice_option_btn->SetTextLayout(SideButton::EHorizontalOrientation::HO_Center);
    m_slice_option_btn->SetCornerRadius(FromDIP(12));
    m_slice_option_btn->SetExtraSize(wxSize(FromDIP(10), FromDIP(10)));
    m_slice_option_btn->SetIconOffset(FromDIP(2));
    m_slice_option_btn->SetMinSize(wxSize(FromDIP(24), FromDIP(24)));

    m_print_btn->SetTextLayout(SideButton::EHorizontalOrientation::HO_Left, FromDIP(15));
    m_print_btn->SetCornerRadius(FromDIP(12));
    m_print_btn->SetExtraSize(wxSize(FromDIP(38), FromDIP(10)));
    m_print_btn->SetMinSize(wxSize(-1, FromDIP(24)));

    m_gfd_print_btn->SetTextLayout(SideButton::EHorizontalOrientation::HO_Left, FromDIP(15));
    m_gfd_print_btn->SetLayoutStyle(1);
    m_gfd_print_btn->SetCornerRadius(FromDIP(12));
    m_gfd_print_btn->SetExtraSize(wxSize(FromDIP(38), FromDIP(10)));
    m_gfd_print_btn->SetMinSize(wxSize(-1, FromDIP(24)));

    m_print_option_btn->SetTextLayout(SideButton::EHorizontalOrientation::HO_Center);
    m_print_option_btn->SetCornerRadius(FromDIP(12));
    m_print_option_btn->SetExtraSize(wxSize(FromDIP(10), FromDIP(10)));
    m_print_option_btn->SetIconOffset(FromDIP(2));
    m_print_option_btn->SetMinSize(wxSize(FromDIP(24), FromDIP(24)));
}

void MainFrame::update_slice_print_status(SlicePrintEventType event, bool can_slice, bool can_print)
{
    bool enable_print = true, enable_slice = true;

    if (!can_slice)
    {
        if (m_slice_select == eSlicePlate)
            enable_slice = false;
    }
    if (!can_print)
        enable_print = false;


    //process print logic
    if (enable_print)
    {
        enable_print = get_enable_print_status();
    }

    //process slice logic
    if (enable_slice)
    {
        enable_slice = get_enable_slice_status();
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" m_slice_select %1%: can_slice= %2%, can_print %3%, enable_slice %4%, enable_print %5% ")%m_slice_select % can_slice %can_print %enable_slice %enable_print;
    m_print_btn->Enable(enable_print);
    m_slice_btn->Enable(enable_slice);
    m_slice_enable = enable_slice;
    m_print_enable = enable_print;
    update_gfd_print_button();
    update_gfd_config_buttons();

    if (wxGetApp().mainframe)
        wxGetApp().plater()->update_title_dirty_status();
}


void MainFrame::on_dpi_changed(const wxRect& suggested_rect)
{
    wxGetApp().update_fonts(this);
    this->SetFont(this->normal_font());

#ifdef _MSW_DARK_MODE
    // update common mode sizer
    if (!wxGetApp().tabs_as_menu())
        dynamic_cast<Notebook*>(m_tabpanel)->Rescale();
#endif

#ifndef __APPLE__
    // BBS
    m_topbar->Rescale();
#endif

    m_tabpanel->Rescale();

    update_side_button_style();

    m_slice_btn->Rescale();
    m_print_btn->Rescale();
    m_gfd_print_btn->Rescale();
    m_slice_option_btn->Rescale();
    m_print_option_btn->Rescale();
    update_gfd_config_buttons();

    // update Plater
    wxGetApp().plater()->msw_rescale();

    // update Tabs
    //BBS GUI refactor: remove unused layout new/dlg
    //if (m_layout != ESettingsLayout::Dlg) // Do not update tabs if the Settings are in the separated dialog
    m_param_panel->msw_rescale();
    m_project->msw_rescale();
    if(m_monitor)
        m_monitor->msw_rescale();
    if(m_multi_machine)
        m_multi_machine->msw_rescale();
    if(m_calibration)
        m_calibration->msw_rescale();

    // BBS
#if 0
    for (size_t id = 0; id < m_menubar->GetMenuCount(); id++)
        msw_rescale_menu(m_menubar->GetMenu(id));
#endif

    // Workarounds for correct Window rendering after rescale

    /* Even if Window is maximized during moving,
     * first of all we should imitate Window resizing. So:
     * 1. cancel maximization, if it was set
     * 2. imitate resizing
     * 3. set maximization, if it was set
     */
    const bool is_maximized = this->IsMaximized();
    if (is_maximized)
        this->Maximize(false);

    /* To correct window rendering (especially redraw of a status bar)
     * we should imitate window resizing.
     */
    const wxSize& sz = this->GetSize();
    this->SetSize(sz.x + 1, sz.y + 1);
    this->SetSize(sz);

    this->Maximize(is_maximized);
}

void MainFrame::on_sys_color_changed()
{
    wxBusyCursor wait;

    // update label colors in respect to the system mode
    wxGetApp().init_label_colours();

#ifndef __WINDOWS__
    wxGetApp().force_colors_update();
    wxGetApp().update_ui_from_settings();
#endif //__APPLE__

#ifdef __WXMSW__
    wxGetApp().UpdateDarkUI(m_tabpanel);
 //   m_statusbar->update_dark_ui();
#ifdef _MSW_DARK_MODE
    // update common mode sizer
    if (!wxGetApp().tabs_as_menu())
        dynamic_cast<Notebook*>(m_tabpanel)->Rescale();
#endif
#endif

    // BBS
    m_tabpanel->Rescale();
    m_param_panel->msw_rescale();

    // update Plater
    wxGetApp().plater()->sys_color_changed();
    if(m_monitor)
        m_monitor->on_sys_color_changed();
    if(m_calibration)
        m_calibration->on_sys_color_changed();
    // update Tabs
    for (auto tab : wxGetApp().tabs_list)
        tab->sys_color_changed();
    for (auto tab : wxGetApp().model_tabs_list)
        tab->sys_color_changed();
    wxGetApp().plate_tab->sys_color_changed();

    MenuFactory::sys_color_changed(m_menubar);

    WebView::RecreateAll();

    this->Refresh();
}

// On macOS, we use system menu bar, which handles the key accelerators automatically and breaks key handling in normal typing
// See https://github.com/SoftFever/OrcaSlicer/issues/8152
// So we disable some of the accelerators on macOS, by replacing the accelerator seperator to a hyphen.
#ifdef __APPLE__
static const wxString sep = " - ";
#else
static const wxString sep = "\t";
#endif

static wxMenu* generate_help_menu()
{
    wxMenu* helpMenu = new wxMenu();

    // shortcut key
    append_menu_item(helpMenu, wxID_ANY, _L("Keyboard Shortcuts") + sep + "&?", _L("Show the list of the keyboard shortcuts"),
        [](wxCommandEvent&) { wxGetApp().keyboard_shortcuts(); });
    // Show Beginner's Tutorial
    append_menu_item(helpMenu, wxID_ANY, _L("Setup Wizard"), _L("Setup Wizard"), [](wxCommandEvent &) {wxGetApp().ShowUserGuide();});

    helpMenu->AppendSeparator();
    // Open Config Folder
    append_menu_item(helpMenu, wxID_ANY, _L("Show Configuration Folder"), _L("Show Configuration Folder"),
        [](wxCommandEvent&) { Slic3r::GUI::desktop_open_datadir_folder(); });

    append_menu_item(helpMenu, wxID_ANY, _L("Show Tip of the Day"), _L("Show Tip of the Day"), [](wxCommandEvent&) {
        wxGetApp().plater()->get_dailytips()->open();
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        });

    // Report a bug
    //append_menu_item(helpMenu, wxID_ANY, _L("Report Bug(TODO)"), _L("Report a bug of OrcaSlicer"),
    //    [](wxCommandEvent&) {
    //        //TODO
    //    });
    // Check New Version
    append_menu_item(helpMenu, wxID_ANY, _L("Check for Update"), _L("Check for Update"),
        [](wxCommandEvent&) {
            wxGetApp().check_new_version_sf(true, 1);
        }, "", nullptr, []() {
            return true;
        });

    append_menu_item(helpMenu, wxID_ANY, _L("Open Network Test"), _L("Open Network Test"), [](wxCommandEvent&) {
            NetworkTestDialog dlg(wxGetApp().mainframe);
            dlg.ShowModal();
        });

    // About
#ifndef __APPLE__
    wxString about_title = wxString::Format(_L("&About %s"), SLIC3R_APP_FULL_NAME);
    append_menu_item(helpMenu, wxID_ANY, about_title, about_title,
            [](wxCommandEvent&) { Slic3r::GUI::about(); });
#endif

    return helpMenu;
}


static void add_common_publish_menu_items(wxMenu* publish_menu, MainFrame* mainFrame)
{
#ifndef __WINDOWS__
    append_menu_item(publish_menu, wxID_ANY, _L("Upload Models"), _L("Upload Models"),
        [](wxCommandEvent&) {
            if (!wxGetApp().getAgent()) {
                BOOST_LOG_TRIVIAL(info) << "publish: no agent";
                return;
            }

            json j;
            NetworkAgent* agent = GUI::wxGetApp().getAgent();

            //if (GUI::wxGetApp().plater()->model().objects.empty()) return;
            wxGetApp().open_publish_page_dialog();
        });

    append_menu_item(publish_menu, wxID_ANY, _L("Download Models"), _L("Download Models"),
        [](wxCommandEvent&) {
            if (!wxGetApp().getAgent()) {
                BOOST_LOG_TRIVIAL(info) << "publish: no agent";
                return;
}

            //if (GUI::wxGetApp().plater()->model().objects.empty()) return;
            wxGetApp().open_mall_page_dialog();
        });
#endif
}

static void add_common_view_menu_items(wxMenu* view_menu, MainFrame* mainFrame, std::function<bool(void)> can_change_view)
{
    // The camera control accelerators are captured by GLCanvas3D::on_char().
    append_menu_item(view_menu, wxID_ANY, _L("Default View") + "\t" + ctrl + "0", _L("Default View"), [mainFrame](wxCommandEvent&) {
        mainFrame->select_view("plate");
        mainFrame->plater()->get_current_canvas3D()->zoom_to_bed();
        },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    //view_menu->AppendSeparator();
    //TRN To be shown in the main menu View->Top
    append_menu_item(view_menu, wxID_ANY, _L("Top") + "\t" + ctrl + "1", _L("Top View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("top"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    //TRN To be shown in the main menu View->Bottom
    append_menu_item(view_menu, wxID_ANY, _L("Bottom") + "\t" + ctrl + "2", _L("Bottom View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("bottom"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    append_menu_item(view_menu, wxID_ANY, _L("Front") + "\t" + ctrl + "3", _L("Front View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("front"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    append_menu_item(view_menu, wxID_ANY, _L("Rear") + "\t" + ctrl + "4", _L("Rear View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("rear"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    append_menu_item(view_menu, wxID_ANY, _L("Left") + "\t" + ctrl + "5", _L("Left View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("left"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
    append_menu_item(view_menu, wxID_ANY, _L("Right") + "\t" + ctrl + "6", _L("Right View"), [mainFrame](wxCommandEvent&) { mainFrame->select_view("right"); },
        "", nullptr, [can_change_view]() { return can_change_view(); }, mainFrame);
}

void MainFrame::init_menubar_as_editor()
{
#ifdef __APPLE__
    m_menubar = new wxMenuBar();
#endif

    // File menu
    wxMenu* fileMenu = new wxMenu;
    {
#ifdef __APPLE__
        // New Window
        append_menu_item(fileMenu, wxID_ANY, _L("New Window"), _L("Start a new window"),
                         [](wxCommandEvent&) { start_new_slicer(); }, "", nullptr,
                         [this] { return m_plater != nullptr && wxGetApp().app_config->get("app", "single_instance") == "false"; }, this);
#endif
        // New Project
        append_menu_item(fileMenu, wxID_ANY, _L("New Project") + "\t" + ctrl + "N", _L("Start a new project"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->new_project(); }, "", nullptr,
            [this](){return can_start_new_project(); }, this);
        // Open Project

#ifndef __APPLE__
        append_menu_item(fileMenu, wxID_ANY, _L("Open Project") + dots + "\t" + ctrl + "O", _L("Open a project file"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->load_project(); }, "menu_open", nullptr,
            [this](){return can_open_project(); }, this);
#else
        append_menu_item(fileMenu, wxID_ANY, _L("Open Project") + dots + "\t" + ctrl + "O", _L("Open a project file"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->load_project(); }, "", nullptr,
            [this](){return can_open_project(); }, this);
#endif

        // Recent Project
        wxMenu* recent_projects_menu = new wxMenu();
        wxMenuItem* recent_projects_submenu = append_submenu(fileMenu, recent_projects_menu, wxID_ANY, _L("Recent files"), "");
        m_recent_projects.UseMenu(recent_projects_menu);
        Bind(wxEVT_MENU, [this](wxCommandEvent& evt) {
            size_t file_id = evt.GetId() - wxID_FILE1;
            wxString filename = m_recent_projects.GetHistoryFile(file_id);
                open_recent_project(file_id, filename);
            }, wxID_FILE1, wxID_FILE1 + 49); // [5050, 5100)

        std::vector<std::string> recent_projects = wxGetApp().app_config->get_recent_projects();
        std::reverse(recent_projects.begin(), recent_projects.end());
        for (const std::string& project : recent_projects)
        {
            m_recent_projects.AddFileToHistory(from_u8(project));
        }
        m_recent_projects.LoadThumbnails();

        Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(can_open_project() && (m_recent_projects.GetCount() > 0)); }, recent_projects_submenu->GetId());

        // BBS: close save project
#ifndef __APPLE__
        append_menu_item(fileMenu, wxID_ANY, _L("Save Project") + "\t" + ctrl + "S", _L("Save current project to file"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->save_project(); }, "menu_save", nullptr,
            [this](){return m_plater != nullptr && can_save(); }, this);
#else
        append_menu_item(fileMenu, wxID_ANY, _L("Save Project") + "\t" + ctrl + "S", _L("Save current project to file"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->save_project(); }, "", nullptr,
            [this](){return m_plater != nullptr && can_save(); }, this);
#endif

#ifndef __APPLE__
        append_menu_item(fileMenu, wxID_ANY, _L("Save Project as") + dots + "\t" + ctrl + shift + "S", _L("Save current project as"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->save_project(true); }, "menu_save", nullptr,
            [this](){return m_plater != nullptr && can_save_as(); }, this);
#else
        append_menu_item(fileMenu, wxID_ANY, _L("Save Project as") + dots + "\t" + ctrl + shift + "S", _L("Save current project as"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->save_project(true); }, "", nullptr,
            [this](){return m_plater != nullptr && can_save_as(); }, this);
#endif


        fileMenu->AppendSeparator();

        // BBS
        wxMenu *import_menu = new wxMenu();
#ifndef __APPLE__
        append_menu_item(import_menu, wxID_ANY, _L("Import 3MF/STL/STEP/SVG/OBJ/AMF") + dots + "\t" + ctrl + "I", _L("Load a model"),
            [this](wxCommandEvent&) { if (m_plater) {
            m_plater->add_file();
        } }, "menu_import", nullptr,
            [this](){return can_add_models(); }, this);
#else
        append_menu_item(import_menu, wxID_ANY, _L("Import 3MF/STL/STEP/SVG/OBJ/AMF") + dots + "\t" + ctrl + "I", _L("Load a model"),
            [this](wxCommandEvent&) { if (m_plater) { m_plater->add_model(); } }, "", nullptr,
            [this](){return can_add_models(); }, this);
#endif
        append_menu_item(import_menu, wxID_ANY, _L("Import Zip Archive") + dots, _L("Load models contained within a zip archive"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->import_zip_archive(); }, "menu_import", nullptr,
            [this]() { return can_add_models(); });
        append_menu_item(import_menu, wxID_ANY, _L("Import Configs") + dots /*+ "\t" + ctrl + "I"*/, _L("Load configs"),
            [this](wxCommandEvent&) { load_config_file(); }, "menu_import", nullptr,
            [this](){return true; }, this);

        append_submenu(fileMenu, import_menu, wxID_ANY, _L("Import"), "");


        wxMenu* export_menu = new wxMenu();
        // BBS export as STL
        append_menu_item(export_menu, wxID_ANY, _L("Export all objects as one STL") + dots, _L("Export all objects as one STL"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_stl(); }, "menu_export_stl", nullptr,
            [this](){return can_export_model(); }, this);
        append_menu_item(export_menu, wxID_ANY, _L("Export all objects as STLs") + dots, _L("Export all objects as STLs"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_stl(false, false, true); }, "menu_export_stl", nullptr,
            [this](){return can_export_model(); }, this);
        append_menu_item(export_menu, wxID_ANY, _L("Export Generic 3MF") + dots/* + "\t" + ctrl + "G"*/, _L("Export 3mf file without using some 3mf-extensions"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_core_3mf(); }, "menu_export_sliced_file", nullptr,
            [this](){return can_export_model(); }, this);
        // BBS export .gcode.3mf
        append_menu_item(export_menu, wxID_ANY, _L("Export plate sliced file") + dots + "\t" + ctrl + "G", _L("Export current sliced file"),
            [this](wxCommandEvent&) { if (m_plater) wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_SLICED_FILE)); }, "menu_export_sliced_file", nullptr,
            [this](){return can_export_gcode(); }, this);

        append_menu_item(export_menu, wxID_ANY, _L("Export all plate sliced file") + dots/* + "\t" + ctrl + "G"*/, _L("Export all plate sliced file"),
            [this](wxCommandEvent&) { if (m_plater) wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_ALL_SLICED_FILE)); }, "menu_export_sliced_file", nullptr,
            [this]() {return can_export_all_gcode(); }, this);

        append_menu_item(export_menu, wxID_ANY, _L("Export G-code") + dots/* + "\t" + ctrl + "G"*/, _L("Export current plate as G-code"),
            [this](wxCommandEvent&) { if (m_plater) m_plater->export_gcode(false); }, "menu_export_gcode", nullptr,
            [this]() {return can_export_gcode(); }, this);
        append_menu_item(
            export_menu, wxID_ANY, _L("Export Preset Bundle") + dots /* + "\t" + ctrl + "E"*/, _L("Export current configuration to files"),
            [this](wxCommandEvent &) { export_config(); },
            "menu_export_config", nullptr,
            []() { return true; }, this);

        append_submenu(fileMenu, export_menu, wxID_ANY, _L("Export"), "");

        fileMenu->AppendSeparator();

#ifndef __APPLE__
        append_menu_item(fileMenu, wxID_EXIT, _L("Quit"), wxString::Format(_L("Quit")),
            [this](wxCommandEvent&) { Close(false); }, "menu_exit", nullptr);
#else
        append_menu_item(fileMenu, wxID_EXIT, _L("Quit"), wxString::Format(_L("Quit")),
            [this](wxCommandEvent&) { Close(false); }, "", nullptr);
#endif
    }

    // Edit menu
    wxMenu* editMenu = nullptr;
    if (m_plater != nullptr)
    {
        editMenu = new wxMenu();

    auto handle_key_event = [](wxKeyEvent& evt) {
        if (wxGetApp().imgui()->update_key_data(evt)) {
            wxGetApp().plater()->get_current_canvas3D()->render();
            return true;
        }
        return false;
    };
#ifndef __APPLE__
        // BBS undo
        append_menu_item(editMenu, wxID_ANY, _L("Undo") + "\t" + ctrl + "Z",
            _L("Undo"), [this](wxCommandEvent&) { m_plater->undo(); },
            "menu_undo", nullptr, [this](){return m_plater->can_undo(); }, this);
        // BBS redo
        append_menu_item(editMenu, wxID_ANY, _L("Redo") + "\t" + ctrl + "Y",
            _L("Redo"), [this](wxCommandEvent&) { m_plater->redo(); },
            "menu_redo", nullptr, [this](){return m_plater->can_redo(); }, this);
        editMenu->AppendSeparator();
        // BBS Cut TODO
        append_menu_item(editMenu, wxID_ANY, _L("Cut") + "\t" + ctrl + "X",
            _L("Cut selection to clipboard"), [this](wxCommandEvent&) {m_plater->cut_selection_to_clipboard(); },
            "menu_cut", nullptr, [this]() {return m_plater->can_copy_to_clipboard(); }, this);
        // BBS Copy
        append_menu_item(editMenu, wxID_ANY, _L("Copy") + "\t" + ctrl + "C",
            _L("Copy selection to clipboard"), [this](wxCommandEvent&) { m_plater->copy_selection_to_clipboard(); },
            "menu_copy", nullptr, [this](){return m_plater->can_copy_to_clipboard(); }, this);
        // BBS Paste
        append_menu_item(editMenu, wxID_ANY, _L("Paste") + "\t" + ctrl + "V",
            _L("Paste clipboard"), [this](wxCommandEvent&) { m_plater->paste_from_clipboard(); },
            "menu_paste", nullptr, [this](){return m_plater->can_paste_from_clipboard(); }, this);
        // BBS Delete selected
        append_menu_item(editMenu, wxID_ANY, _L("Delete selected") + "\t" + _L("Del"),
            _L("Deletes the current selection"),[this](wxCommandEvent&) { m_plater->remove_selected(); },
            "menu_remove", nullptr, [this](){return can_delete(); }, this);
        //BBS: delete all
        append_menu_item(editMenu, wxID_ANY, _L("Delete all") + "\t" + ctrl + "D",
            _L("Deletes all objects"),[this](wxCommandEvent&) { m_plater->delete_all_objects_from_model(); },
            "menu_remove", nullptr, [this](){return can_delete_all(); }, this);
        editMenu->AppendSeparator();
        // BBS Clone Selected
        append_menu_item(editMenu, wxID_ANY, _L("Clone selected") /*+ "\t" + ctrl + "M"*/,
            _L("Clone copies of selections"),[this](wxCommandEvent&) {
                m_plater->clone_selection();
            },
            "menu_remove", nullptr, [this](){return can_clone(); }, this);
        editMenu->AppendSeparator();
        append_menu_item(editMenu, wxID_ANY, _L("Duplicate Current Plate"),
            _L("Duplicate the current plate"),[this](wxCommandEvent&) {
                m_plater->duplicate_plate();
            },
            "menu_remove", nullptr, [this](){return true;}, this);
        editMenu->AppendSeparator();
#else
        // BBS undo
        append_menu_item(editMenu, wxID_ANY, _L("Undo") + sep + ctrl_t + "Z",
            _L("Undo"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'Z';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->undo(); },
            "", nullptr, [this](){return m_plater->can_undo(); }, this);
        // BBS redo
        append_menu_item(editMenu, wxID_ANY, _L("Redo") + sep + ctrl_t + "Y",
            _L("Redo"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'Y';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->redo(); },
            "", nullptr, [this](){return m_plater->can_redo(); }, this);
        editMenu->AppendSeparator();
        // BBS Cut TODO
        append_menu_item(editMenu, wxID_ANY, _L("Cut") + sep + ctrl_t + "X",
            _L("Cut selection to clipboard"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'X';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->cut_selection_to_clipboard(); },
            "", nullptr, [this]() {return m_plater->can_copy_to_clipboard(); }, this);
        // BBS Copy
        append_menu_item(editMenu, wxID_ANY, _L("Copy") + sep + ctrl_t + "C",
            _L("Copy selection to clipboard"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'C';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->copy_selection_to_clipboard(); },
            "", nullptr, [this](){return m_plater->can_copy_to_clipboard(); }, this);
        // BBS Paste
        append_menu_item(editMenu, wxID_ANY, _L("Paste") + sep + ctrl_t + "V",
            _L("Paste clipboard"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'V';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->paste_from_clipboard(); },
            "", nullptr, [this](){return m_plater->can_paste_from_clipboard(); }, this);
#if 0
        // BBS Delete selected
        append_menu_item(editMenu, wxID_ANY, _L("Delete selected") + "\t" + _L("Backspace"),
            _L("Deletes the current selection"),[this](wxCommandEvent&) {
                m_plater->remove_selected();
            },
            "", nullptr, [this](){return can_delete(); }, this);
#endif
        //BBS: delete all
        append_menu_item(editMenu, wxID_ANY, _L("Delete all") + "\t" + ctrl + "D",
            _L("Deletes all objects"),[this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'D';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->delete_all_objects_from_model(); },
            "", nullptr, [this](){return can_delete_all(); }, this);
        editMenu->AppendSeparator();
        // BBS Clone Selected
        append_menu_item(editMenu, wxID_ANY, _L("Clone selected") + "\t" + ctrl + "K",
            _L("Clone copies of selections"),[this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'M';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->clone_selection();
            },
            "", nullptr, [this](){return can_clone(); }, this);
        editMenu->AppendSeparator();
        append_menu_item(editMenu, wxID_ANY, _L("Duplicate Current Plate"),
            _L("Duplicate the current plate"),[this, handle_key_event](wxCommandEvent&) {
                m_plater->duplicate_plate();
            },
            "", nullptr, [this](){return true;}, this);
        editMenu->AppendSeparator();

#endif

        // BBS Select All
        append_menu_item(editMenu, wxID_ANY, _L("Select all") + sep + ctrl_t + "A",
            _L("Selects all objects"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.SetControlDown(true);
                e.m_keyCode = 'A';
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->select_all(); },
            "", nullptr, [this](){return can_select(); }, this);
        // BBS Deslect All
        append_menu_item(editMenu, wxID_ANY, _L("Deselect all") + sep + _L("Esc"),
            _L("Deselects all objects"), [this, handle_key_event](wxCommandEvent&) {
                wxKeyEvent e;
                e.SetEventType(wxEVT_KEY_DOWN);
                e.m_keyCode = WXK_ESCAPE;
                if (handle_key_event(e)) {
                    return;
                }
                m_plater->deselect_all(); },
            "", nullptr, [this](){return can_deselect(); }, this);
        //editMenu->AppendSeparator();
        //append_menu_check_item(editMenu, wxID_ANY, _L("Show Model Mesh(TODO)"),
        //    _L("Display triangles of models."), [this](wxCommandEvent& evt) {
        //        wxGetApp().app_config->set_bool("show_model_mesh", evt.GetInt() == 1);
        //    }, nullptr, [this]() {return can_select(); }, [this]() { return wxGetApp().app_config->get("show_model_mesh").compare("true") == 0; }, this);
        //append_menu_check_item(editMenu, wxID_ANY, _L("Show Model Shadow(TODO)"), _L("Display shadow of objects."),
        //    [this](wxCommandEvent& evt) {
        //        wxGetApp().app_config->set_bool("show_model_shadow", evt.GetInt() == 1);
        //    }, nullptr, [this]() {return can_select(); }, [this]() { return wxGetApp().app_config->get("show_model_shadow").compare("true") == 0; }, this);
        //editMenu->AppendSeparator();
        //append_menu_check_item(editMenu, wxID_ANY, _L("Show Printable Box(TODO)"), _L("Display printable box."),
        //    [this](wxCommandEvent& evt) {
        //        wxGetApp().app_config->set_bool("show_printable_box", evt.GetInt() == 1);
        //    }, nullptr, [this]() {return can_select(); }, [this]() { return wxGetApp().app_config->get("show_printable_box").compare("true") == 0; }, this);
    }

    // BBS

    //publish menu

    /*if (m_plater) {
        publishMenu = new wxMenu();
        add_common_publish_menu_items(publishMenu, this);
        publishMenu->AppendSeparator();
    }*/

    // View menu
    wxMenu* viewMenu = nullptr;
    if (m_plater) {
        viewMenu = new wxMenu();
        add_common_view_menu_items(viewMenu, this, std::bind(&MainFrame::can_change_view, this));
        viewMenu->AppendSeparator();

        //BBS perspective view
        wxWindowID camera_id_base = wxWindow::NewControlId(int(wxID_CAMERA_COUNT));
        auto perspective_item = append_menu_radio_item(viewMenu, wxID_CAMERA_PERSPECTIVE + camera_id_base, _L("Use Perspective View"), _L("Use Perspective View"),
            [this](wxCommandEvent&) {
                wxGetApp().app_config->set_bool("use_perspective_camera", true);
                wxGetApp().update_ui_from_settings();
            }, nullptr);
        //BBS orthogonal view
        auto orthogonal_item = append_menu_radio_item(viewMenu, wxID_CAMERA_ORTHOGONAL + camera_id_base, _L("Use Orthogonal View"), _L("Use Orthogonal View"),
            [this](wxCommandEvent&) {
                wxGetApp().app_config->set_bool("use_perspective_camera", false);
                wxGetApp().update_ui_from_settings();
            }, nullptr);
        this->Bind(wxEVT_UPDATE_UI, [viewMenu, camera_id_base](wxUpdateUIEvent& evt) {
                if (wxGetApp().app_config->get("use_perspective_camera").compare("true") == 0)
                    viewMenu->Check(wxID_CAMERA_PERSPECTIVE + camera_id_base, true);
                else
                    viewMenu->Check(wxID_CAMERA_ORTHOGONAL + camera_id_base, true);
            }, perspective_item->GetId());
        append_menu_check_item(viewMenu, wxID_ANY, _L("Auto Perspective"), _L("Automatically switch between orthographic and perspective when changing from top/bottom/side views."),
            [this](wxCommandEvent&) {
                wxGetApp().app_config->set_bool("auto_perspective", !wxGetApp().app_config->get_bool("auto_perspective"));
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this, [this]() { return m_tabpanel->GetSelection() == TabPosition::tp3DEditor || m_tabpanel->GetSelection() == TabPosition::tpPreview; },
            [this]() { return wxGetApp().app_config->get_bool("auto_perspective"); }, this);

        viewMenu->AppendSeparator();
        append_menu_check_item(viewMenu, wxID_ANY, _L("Show &G-code Window") + sep + "C", _L("Show G-code window in Preview scene."),
            [this](wxCommandEvent &) {
                wxGetApp().toggle_show_gcode_window();
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this, [this]() { return m_tabpanel->GetSelection() == tpPreview; },
            [this]() { return wxGetApp().show_gcode_window(); }, this);

        append_menu_check_item(
            viewMenu, wxID_ANY, _L("Show 3D Navigator"), _L("Show 3D navigator in Prepare and Preview scene."),
            [this](wxCommandEvent&) {
                wxGetApp().toggle_show_3d_navigator();
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this, [this]() { return m_tabpanel->GetSelection() == TabPosition::tp3DEditor || m_tabpanel->GetSelection() == TabPosition::tpPreview; },
            [this]() { return wxGetApp().show_3d_navigator(); }, this);

        append_menu_item(
            viewMenu, wxID_ANY, _L("Reset Window Layout"), _L("Reset to default window layout"),
            [this](wxCommandEvent&) { m_plater->reset_window_layout(); }, "", this,
            [this]() {
                return (m_tabpanel->GetSelection() == TabPosition::tp3DEditor || m_tabpanel->GetSelection() == TabPosition::tpPreview) &&
                       m_plater->is_sidebar_enabled();
            },
            this);

        viewMenu->AppendSeparator();
        append_menu_check_item(viewMenu, wxID_ANY, _L("Show &Labels") + "\t" + ctrl + "E", _L("Show object labels in 3D scene."),
            [this](wxCommandEvent&) { m_plater->show_view3D_labels(!m_plater->are_view3D_labels_shown()); m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT)); }, this,
            [this]() { return m_plater->is_view3D_shown(); }, [this]() { return m_plater->are_view3D_labels_shown(); }, this);

        append_menu_check_item(viewMenu, wxID_ANY, _L("Show &Overhang"), _L("Show object overhang highlight in 3D scene."),
            [this](wxCommandEvent &) {
                m_plater->show_view3D_overhang(!m_plater->is_view3D_overhang_shown());
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this, [this]() { return m_plater->is_view3D_shown(); }, [this]() { return m_plater->is_view3D_overhang_shown(); }, this);

        append_menu_check_item(
            viewMenu, wxID_ANY, _L("Show Selected Outline (beta)"), _L("Show outline around selected object in 3D scene."),
            [this](wxCommandEvent&) {
                wxGetApp().toggle_show_outline();
                m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT));
            },
            this, [this]() { return m_tabpanel->GetSelection() == TabPosition::tp3DEditor; },
            [this]() { return wxGetApp().show_outline(); }, this);

        /*viewMenu->AppendSeparator();
        append_menu_check_item(viewMenu, wxID_ANY, _L("Show &Wireframe") + "\t" + ctrl + shift + _L("Enter"), _L("Show wireframes in 3D scene."),
            [this](wxCommandEvent&) { m_plater->toggle_show_wireframe(); m_plater->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT)); }, this,
            [this]() { return m_plater->is_wireframe_enabled(); }, [this]() { return m_plater->is_show_wireframe(); }, this);*/

        //viewMenu->AppendSeparator();
        ////BBS orthogonal view
        //append_menu_check_item(viewMenu, wxID_ANY, _L("Show Edges(TODO)"), _L("Show Edges."),
        //    [this](wxCommandEvent& evt) {
        //        wxGetApp().app_config->set("show_build_edges", evt.GetInt() == 1 ? "true" : "false");
        //    }, nullptr, [this]() {return can_select(); }, [this]() {
        //        std::string show_build_edges = wxGetApp().app_config->get("show_build_edges");
        //        return show_build_edges.compare("true") == 0;
        //    }, this);
    }

    wxWindowID config_id_base = wxWindow::NewControlId(int(ConfigMenuCnt));
    //TODO remove
    //auto config_wizard_name = _(ConfigWizard::name(true) + "(Debug)");
    //const auto config_wizard_tooltip = from_u8((boost::format(_utf8(L("Run %s"))) % config_wizard_name).str());
    //auto config_item = new wxMenuItem(m_topbar->GetTopMenu(), ConfigMenuWizard + config_id_base, config_wizard_name, config_wizard_tooltip);
#ifdef __APPLE__
    wxWindowID bambu_studio_id_base = wxWindow::NewControlId(int(2));
    wxMenu* parent_menu = m_menubar->OSXGetAppleMenu();
    //auto preference_item = new wxMenuItem(parent_menu, OrcaSlicerMenuPreferences + bambu_studio_id_base, _L("Preferences") + "\t" + ctrl + ",", "");
#else
    wxMenu* parent_menu = m_topbar->GetTopMenu();
    auto preference_item = new wxMenuItem(parent_menu, ConfigMenuPreferences + config_id_base, _L("Preferences") + "\t" + ctrl + "P", "");

#endif
    //auto printer_item = new wxMenuItem(parent_menu, ConfigMenuPrinter + config_id_base, _L("Printer"), "");
    //auto language_item = new wxMenuItem(parent_menu, ConfigMenuLanguage + config_id_base, _L("Switch Language"), "");
//    parent_menu->Bind(wxEVT_MENU, [this, config_id_base](wxEvent& event) {
//        switch (event.GetId() - config_id_base) {
//        //case ConfigMenuLanguage:
//        //{
//        //    /* Before change application language, let's check unsaved changes on 3D-Scene
//        //     * and draw user's attention to the application restarting after a language change
//        //     */
//        //    {
//        //        // the dialog needs to be destroyed before the call to switch_language()
//        //        // or sometimes the application crashes into wxDialogBase() destructor
//        //        // so we put it into an inner scope
//        //        wxString title = _L("Language selection");
//        //        wxMessageDialog dialog(nullptr,
//        //            _L("Switching the language requires application restart.\n") + "\n\n" +
//        //            _L("Do you want to continue?"),
//        //            title,
//        //            wxICON_QUESTION | wxOK | wxCANCEL);
//        //        if (dialog.ShowModal() == wxID_CANCEL)
//        //            return;
//        //    }
//
//        //    wxGetApp().switch_language();
//        //    break;
//        //}
//        //case ConfigMenuWizard:
//        //{
//        //    wxGetApp().run_wizard(ConfigWizard::RR_USER);
//        //    break;
//        //}
//        case ConfigMenuPrinter:
//        {
//            wxGetApp().params_dialog()->Popup();
//            wxGetApp().get_tab(Preset::TYPE_PRINTER)->restore_last_select_item();
//            break;
//        }
//        case ConfigMenuPreferences:
//        {
//            CallAfter([this] {
//                PreferencesDialog dlg(this);
//                dlg.ShowModal();
//#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
//                if (dlg.seq_top_layer_only_changed() || dlg.seq_seq_top_gcode_indices_changed())
//#else
//                if (dlg.seq_top_layer_only_changed())
//#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
//                    plater()->refresh_print();
//#if ENABLE_CUSTOMIZABLE_FILES_ASSOCIATION_ON_WIN
//#ifdef _WIN32
//                /*
//                if (wxGetApp().app_config()->get("associate_3mf") == "true")
//                    wxGetApp().associate_3mf_files();
//                if (wxGetApp().app_config()->get("associate_stl") == "true")
//                    wxGetApp().associate_stl_files();
//                /*if (wxGetApp().app_config()->get("associate_step") == "true")
//                    wxGetApp().associate_step_files();*/
//#endif // _WIN32
//#endif
//            });
//            break;
//        }
//        default:
//            break;
//        }
//    });

#ifdef __APPLE__
    wxString about_title = wxString::Format(_L("&About %s"), SLIC3R_APP_FULL_NAME);
    //auto about_item = new wxMenuItem(parent_menu, OrcaSlicerMenuAbout + bambu_studio_id_base, about_title, "");
        //parent_menu->Bind(wxEVT_MENU, [this, bambu_studio_id_base](wxEvent& event) {
        //    switch (event.GetId() - bambu_studio_id_base) {
        //        case OrcaSlicerMenuAbout:
        //            Slic3r::GUI::about();
        //            break;
        //        case OrcaSlicerMenuPreferences:
        //            CallAfter([this] {
        //                PreferencesDialog dlg(this);
        //                dlg.ShowModal();
        //#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        //                if (dlg.seq_top_layer_only_changed() || dlg.seq_seq_top_gcode_indices_changed())
        //#else
        //                if (dlg.seq_top_layer_only_changed())
        //#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        //                    plater()->refresh_print();
        //            });
        //            break;
        //        default:
        //            break;
        //    }
        //});
    //parent_menu->Insert(0, about_item);
    append_menu_item(
        parent_menu, wxID_ANY, _L(about_title), "",
        [this](wxCommandEvent &) { Slic3r::GUI::about();},
        "", nullptr, []() { return true; }, this, 0);
    append_menu_item(
        parent_menu, wxID_ANY, _L("Preferences") + "\t" + ctrl + ",", "",
        [this](wxCommandEvent &) {
            PreferencesDialog dlg(this);
            dlg.ShowModal();
            plater()->get_current_canvas3D()->force_set_focus();
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
            if (dlg.seq_top_layer_only_changed() || dlg.seq_seq_top_gcode_indices_changed())
#else
            if (dlg.seq_top_layer_only_changed())
#endif
                plater()->refresh_print();
        },
        "", nullptr, []() { return true; }, this, 1);
    //parent_menu->Insert(1, preference_item);
#endif
    // Help menu
    auto helpMenu = generate_help_menu();

#ifndef __APPLE__
    m_topbar->SetFileMenu(fileMenu);
    if (editMenu)
        m_topbar->AddDropDownSubMenu(editMenu, _L("Edit"));
    if (viewMenu)
        m_topbar->AddDropDownSubMenu(viewMenu, _L("View"));
    //BBS add Preference

    append_menu_item(
        m_topbar->GetTopMenu(), wxID_ANY, _L("Preferences") + "\t" + ctrl + "P", "",
        [this](wxCommandEvent &) {
            // Orca: Use GUI_App::open_preferences instead of direct call so windows associations are updated on exit
            wxGetApp().open_preferences();
            plater()->get_current_canvas3D()->force_set_focus();
        },
        "", nullptr, []() { return true; }, this);
    //m_topbar->AddDropDownMenuItem(preference_item);
    //m_topbar->AddDropDownMenuItem(printer_item);
    //m_topbar->AddDropDownMenuItem(language_item);
    //m_topbar->AddDropDownMenuItem(config_item);
    m_topbar->AddDropDownSubMenu(helpMenu, _L("Help"));

    // SoftFever calibrations

    // Temperature
    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Temperature"), _L("Temperature Calibration"),
        [this](wxCommandEvent&) {
            if (!m_temp_calib_dlg)
                m_temp_calib_dlg = new Temp_Calibration_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_temp_calib_dlg->ShowModal();
        }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    // Flow rate (with submenu)
    auto flowrate_menu = new wxMenu();
    append_menu_item(
        flowrate_menu, wxID_ANY, _L("Pass 1"), _L("Flow rate test - Pass 1"),
        [this](wxCommandEvent&) { if (m_plater) m_plater->calib_flowrate(false, 1); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    append_menu_item(flowrate_menu, wxID_ANY, _L("Pass 2"), _L("Flow rate test - Pass 2"),
        [this](wxCommandEvent&) { if (m_plater) m_plater->calib_flowrate(false, 2); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    flowrate_menu->AppendSeparator();
    append_menu_item(flowrate_menu, wxID_ANY, _L("YOLO (Recommended)"), _L("Orca YOLO flowrate calibration, 0.01 step"),
        [this](wxCommandEvent&) { if (m_plater) m_plater->calib_flowrate(true, 1); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    append_menu_item(flowrate_menu, wxID_ANY, _L("YOLO (perfectionist version)"), _L("Orca YOLO flowrate calibration, 0.005 step"),
        [this](wxCommandEvent&) { if (m_plater) m_plater->calib_flowrate(true, 2); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    m_topbar->GetCalibMenu()->AppendSubMenu(flowrate_menu, _L("Flow rate"));

    // Pressure Advance
    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Pressure advance"), _L("Pressure advance"),
        [this](wxCommandEvent&) {
            if (!m_pa_calib_dlg)
                m_pa_calib_dlg = new PA_Calibration_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_pa_calib_dlg->ShowModal();
        }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    // Retraction test
    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Retraction test"), _L("Retraction test"),
        [this](wxCommandEvent&) {
            if (!m_retraction_calib_dlg)
                m_retraction_calib_dlg = new Retraction_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_retraction_calib_dlg->ShowModal();
        }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    // Max Volumetric Speed
    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Max flowrate"), _L("Max flowrate"),
        [this](wxCommandEvent&) {
            if (!m_vol_test_dlg)
                m_vol_test_dlg = new MaxVolumetricSpeed_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_vol_test_dlg->ShowModal();
        }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    // Cornering (with submenu)
    auto cornering_menu = new wxMenu();
    append_menu_item(
        cornering_menu, wxID_ANY, _L("Junction Deviation"), _L("Junction Deviation calibration"),
        [this](wxCommandEvent&) {
            if (!m_junction_deviation_calib_dlg)
                m_junction_deviation_calib_dlg = new Junction_Deviation_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_junction_deviation_calib_dlg->ShowModal();
        },
        "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    m_topbar->GetCalibMenu()->AppendSubMenu(cornering_menu, _L("Cornering"));

    // Input Shaping (with submenu)
    auto input_shaping_menu = new wxMenu();
    append_menu_item(
        input_shaping_menu, wxID_ANY, _L("Input Shaping Frequency"), _L("Input Shaping Frequency"),
        [this](wxCommandEvent&) {
            if (!m_IS_freq_calib_dlg)
                m_IS_freq_calib_dlg = new Input_Shaping_Freq_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_IS_freq_calib_dlg->ShowModal();
        },
        "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    append_menu_item(
        input_shaping_menu, wxID_ANY, _L("Input Shaping Damping/zeta factor"), _L("Input Shaping Damping/zeta factor"),
        [this](wxCommandEvent&) {
            if (!m_IS_damp_calib_dlg)
                m_IS_damp_calib_dlg = new Input_Shaping_Damp_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_IS_damp_calib_dlg->ShowModal();
        },
        "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    m_topbar->GetCalibMenu()->AppendSubMenu(input_shaping_menu, _L("Input Shaping"));

    // VFA
    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("VFA"), _L("VFA"),
        [this](wxCommandEvent&) {
            if (!m_vfa_test_dlg)
                m_vfa_test_dlg = new VFA_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_vfa_test_dlg->ShowModal();
        }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    // help
    append_menu_item(m_topbar->GetCalibMenu(), wxID_ANY, _L("Tutorial"), _L("Calibration help"),
        [this](wxCommandEvent&) {
            std::string url = "https://github.com/SoftFever/OrcaSlicer/wiki/Calibration";
            if (const std::string country_code = wxGetApp().app_config->get_country_code(); country_code == "CN") {
                // Use gitee mirror for China users
                url = "https://gitee.com/n0isyfox/orca-slicer-docs/wikis/%E6%A0%A1%E5%87%86/%E6%89%93%E5%8D%B0%E5%8F%82%E6%95%B0%E6%A0%A1%E5%87%86";
            }
            wxLaunchDefaultBrowser(url, wxBROWSER_NEW_WINDOW);
        }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

#else
    m_menubar->Append(fileMenu, wxString::Format("&%s", _L("File")));
    if (editMenu)
        m_menubar->Append(editMenu, wxString::Format("&%s", _L("Edit")));
    if (viewMenu)
        m_menubar->Append(viewMenu, wxString::Format("&%s", _L("View")));
    /*if (publishMenu)
        m_menubar->Append(publishMenu, wxString::Format("&%s", _L("3D Models")));*/

    // SoftFever calibrations
    auto calib_menu = new wxMenu();

    // Temperature
    append_menu_item(calib_menu, wxID_ANY, _L("Temperature"), _L("Temperature"),
        [this](wxCommandEvent&) {
            if (!m_temp_calib_dlg)
                m_temp_calib_dlg = new Temp_Calibration_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_temp_calib_dlg->ShowModal();
        }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    // Flowrate (with submenu)
    auto flowrate_menu = new wxMenu();
    append_menu_item(flowrate_menu, wxID_ANY, _L("Pass 1"), _L("Flow rate test - Pass 1"),
        [this](wxCommandEvent&) { if (m_plater) m_plater->calib_flowrate(false, 1); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    append_menu_item(flowrate_menu, wxID_ANY, _L("Pass 2"), _L("Flow rate test - Pass 2"),
        [this](wxCommandEvent&) { if (m_plater) m_plater->calib_flowrate(false, 2); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    append_submenu(calib_menu,flowrate_menu,wxID_ANY,_L("Flow rate"),_L("Flow rate"),"",
                   [this]() {return m_plater->is_view3D_shown();; });
    flowrate_menu->AppendSeparator();
    append_menu_item(flowrate_menu, wxID_ANY, _L("YOLO (Recommended)"), _L("Orca YOLO flowrate calibration, 0.01 step"),
        [this](wxCommandEvent&) { if (m_plater) m_plater->calib_flowrate(true, 1); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    append_menu_item(flowrate_menu, wxID_ANY, _L("YOLO (perfectionist version)"), _L("Orca YOLO flowrate calibration, 0.005 step"),
        [this](wxCommandEvent&) { if (m_plater) m_plater->calib_flowrate(true, 2); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    // Pressure Advance
    append_menu_item(calib_menu, wxID_ANY, _L("Pressure advance"), _L("Pressure advance"),
        [this](wxCommandEvent&) {
            if (!m_pa_calib_dlg)
                m_pa_calib_dlg = new PA_Calibration_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_pa_calib_dlg->ShowModal();
        }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    // Retraction test
    append_menu_item(calib_menu, wxID_ANY, _L("Retraction test"), _L("Retraction test"),
        [this](wxCommandEvent&) {
            if (!m_retraction_calib_dlg)
                m_retraction_calib_dlg = new Retraction_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_retraction_calib_dlg->ShowModal();
        }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    // Max Volumetric Speed
    append_menu_item(calib_menu, wxID_ANY, _L("Max flowrate"), _L("Max flowrate"),
        [this](wxCommandEvent&) {
            if (!m_vol_test_dlg)
                m_vol_test_dlg = new MaxVolumetricSpeed_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_vol_test_dlg->ShowModal();
        }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    // Cornering (with submenu)
    auto cornering_menu = new wxMenu();
    append_menu_item(
        cornering_menu, wxID_ANY, _L("Junction Deviation"), _L("Junction Deviation calibration"),
        [this](wxCommandEvent&) {
            if (!m_junction_deviation_calib_dlg)
                m_junction_deviation_calib_dlg = new Junction_Deviation_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_junction_deviation_calib_dlg->ShowModal();
        },
        "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    calib_menu->AppendSubMenu(cornering_menu, _L("Cornering"));

    // Input Shaping (with submenu)
    auto input_shaping_menu = new wxMenu();
    append_menu_item(
        input_shaping_menu, wxID_ANY, _L("Input Shaping Frequency"), _L("Input Shaping Frequency"),
        [this](wxCommandEvent&) {
            if (!m_IS_freq_calib_dlg)
                m_IS_freq_calib_dlg = new Input_Shaping_Freq_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_IS_freq_calib_dlg->ShowModal();
        },
        "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    append_menu_item(
        input_shaping_menu, wxID_ANY, _L("Input Shaping Damping/zeta factor"), _L("Input Shaping Damping/zeta factor"),
        [this](wxCommandEvent&) {
            if (!m_IS_damp_calib_dlg)
                m_IS_damp_calib_dlg = new Input_Shaping_Damp_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_IS_damp_calib_dlg->ShowModal();
        },
        "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    calib_menu->AppendSubMenu(input_shaping_menu, _L("Input Shaping"));

    // VFA
    append_menu_item(calib_menu, wxID_ANY, _L("VFA"), _L("VFA"),
        [this](wxCommandEvent&) {
            if (!m_vfa_test_dlg)
                m_vfa_test_dlg = new VFA_Test_Dlg((wxWindow*)this, wxID_ANY, m_plater);
            m_vfa_test_dlg->ShowModal();
        }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);
    // help
    append_menu_item(calib_menu, wxID_ANY, _L("Tutorial"), _L("Calibration help"),
        [this](wxCommandEvent&) { wxLaunchDefaultBrowser("https://github.com/SoftFever/OrcaSlicer/wiki/Calibration", wxBROWSER_NEW_WINDOW); }, "", nullptr,
        [this]() {return m_plater->is_view3D_shown();; }, this);

    m_menubar->Append(calib_menu,wxString::Format("&%s", _L("Calibration")));
    if (helpMenu)
        m_menubar->Append(helpMenu, wxString::Format("&%s", _L("Help")));
    SetMenuBar(m_menubar);

#endif

#ifdef _MSW_DARK_MODE
    if (wxGetApp().tabs_as_menu())
        m_menubar->EnableTop(6, false);
#endif

#ifdef __APPLE__
    // This fixes a bug on Mac OS where the quit command doesn't emit window close events
    // wx bug: https://trac.wxwidgets.org/ticket/18328
    wxMenu* apple_menu = m_menubar->OSXGetAppleMenu();
    if (apple_menu != nullptr) {
        apple_menu->Bind(wxEVT_MENU, [this](wxCommandEvent &) {
            Close();
        }, wxID_EXIT);
    }
#endif // __APPLE__
}

void MainFrame::set_max_recent_count(int max)
{
    max = max < 0 ? 0 : max > 10000 ? 10000 : max;
    size_t count = m_recent_projects.GetCount();
    m_recent_projects.SetMaxFiles(max);
    if (count != m_recent_projects.GetCount()) {
        count = m_recent_projects.GetCount();
        std::vector<std::string> recent_projects;
        for (size_t i = 0; i < count; ++i) {
            recent_projects.push_back(into_u8(m_recent_projects.GetHistoryFile(i)));
        }
        wxGetApp().app_config->set_recent_projects(recent_projects);
        wxGetApp().app_config->save();
        m_webview->SendRecentList(-1);
    }
}

void MainFrame::open_menubar_item(const wxString& menu_name,const wxString& item_name)
{
    if (m_menubar == nullptr)
        return;
    // Get menu object from menubar
    int     menu_index = m_menubar->FindMenu(menu_name);
    wxMenu* menu       = m_menubar->GetMenu(menu_index);
    if (menu == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "Mainframe open_menubar_item function couldn't find menu: " << menu_name;
        return;
    }
    // Get item id from menu
    int     item_id   = menu->FindItem(item_name);
    if (item_id == wxNOT_FOUND)
    {
        // try adding three dots char
        item_id = menu->FindItem(item_name + dots);
    }
    if (item_id == wxNOT_FOUND)
    {
        BOOST_LOG_TRIVIAL(error) << "Mainframe open_menubar_item function couldn't find item: " << item_name;
        return;
    }
    // wxEVT_MENU will trigger item
    wxPostEvent((wxEvtHandler*)menu, wxCommandEvent(wxEVT_MENU, item_id));
}

void MainFrame::init_menubar_as_gcodeviewer()
{
    //BBS do not show gcode viewer mebu
#if 0
    wxMenu* fileMenu = new wxMenu;
    {
        append_menu_item(fileMenu, wxID_ANY, _L("&Open G-code") + dots + "\t" + ctrl + "O", _L("Open a G-code file"),
            [this](wxCommandEvent&) { if (m_plater != nullptr) m_plater->load_gcode(); }, "open", nullptr,
            [this]() {return m_plater != nullptr; }, this);
#ifdef __APPLE__
        append_menu_item(fileMenu, wxID_ANY, _L("Re&load from Disk") + dots + "\t" + ctrl + shift + "R",
            _L("Reload the plater from disk"), [this](wxCommandEvent&) { m_plater->reload_gcode_from_disk(); },
            "", nullptr, [this]() { return !m_plater->get_last_loaded_gcode().empty(); }, this);
#else
        append_menu_item(fileMenu, wxID_ANY, _L("Re&load from Disk") + sep + "F5",
            _L("Reload the plater from disk"), [this](wxCommandEvent&) { m_plater->reload_gcode_from_disk(); },
            "", nullptr, [this]() { return !m_plater->get_last_loaded_gcode().empty(); }, this);
#endif // __APPLE__
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_ANY, _L("Export &Toolpaths as OBJ") + dots, _L("Export toolpaths as OBJ"),
            [this](wxCommandEvent&) { if (m_plater != nullptr) m_plater->export_toolpaths_to_obj(); }, "export_plater", nullptr,
            [this]() {return can_export_toolpaths(); }, this);
        append_menu_item(fileMenu, wxID_ANY, _L("Open &Slicer") + dots, _L("Open Slicer"),
            [](wxCommandEvent&) { start_new_slicer(); }, "", nullptr,
            []() {return true; }, this);
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_EXIT, _L("&Quit"), wxString::Format(_L("Quit %s"), SLIC3R_APP_NAME),
            [this](wxCommandEvent&) { Close(false); });
    }

    // View menu
    wxMenu* viewMenu = nullptr;
    if (m_plater != nullptr) {
        viewMenu = new wxMenu();
        add_common_view_menu_items(viewMenu, this, std::bind(&MainFrame::can_change_view, this));
    }

    // helpmenu
    auto helpMenu = generate_help_menu();

    m_menubar = new wxMenuBar();
    m_menubar->Append(fileMenu, _L("&File"));
    if (viewMenu != nullptr) m_menubar->Append(viewMenu, _L("&View"));
    // Add additional menus from C++
    wxGetApp().add_config_menu(m_menubar);
    m_menubar->Append(helpMenu, _L("&Help"));
    SetMenuBar(m_menubar);

#ifdef __APPLE__
    // This fixes a bug on Mac OS where the quit command doesn't emit window close events
    // wx bug: https://trac.wxwidgets.org/ticket/18328
    wxMenu* apple_menu = m_menubar->OSXGetAppleMenu();
    if (apple_menu != nullptr) {
        apple_menu->Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            Close();
            }, wxID_EXIT);
    }
#endif // __APPLE__
#endif
}

void MainFrame::update_menubar()
{
    if (wxGetApp().is_gcode_viewer())
        return;

    const bool is_fff = plater()->printer_technology() == ptFFF;
}

void MainFrame::reslice_now()
{
    if (m_plater)
        m_plater->reslice();
}

struct ConfigsOverwriteConfirmDialog : MessageDialog
{
    ConfigsOverwriteConfirmDialog(wxWindow *parent, wxString name, bool exported)
        : MessageDialog(parent,
                        wxString::Format(exported ? _L("A file exists with the same name: %s, do you want to overwrite it?") :
                                                  _L("A config exists with the same name: %s, do you want to overwrite it?"),
                                         name),
                        exported ? _L("Overwrite file") : _L("Overwrite config"),
                        wxYES_NO | wxNO_DEFAULT)
    {
        add_button(wxID_YESTOALL, false, _L("Yes to All"));
        add_button(wxID_NOTOALL, false, _L("No to All"));
    }
};

void MainFrame::export_config()
{
    ExportConfigsDialog export_configs_dlg(nullptr);
    export_configs_dlg.ShowModal();
    return;

    // Generate a cummulative configuration for the selected print, filaments and printer.
    wxDirDialog dlg(this, _L("Choose a directory"),
        from_u8(!m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir()), wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    wxString path;
    if (dlg.ShowModal() == wxID_OK)
        path = dlg.GetPath();
    if (!path.IsEmpty()) {
        // Export the config bundle.
        wxGetApp().app_config->update_config_dir(into_u8(path));
        try {
            auto files = wxGetApp().preset_bundle->export_current_configs(into_u8(path), [this](std::string const & name) {
                    ConfigsOverwriteConfirmDialog dlg(this, from_u8(name), true);
                    int res = dlg.ShowModal();
                    int ids[]{wxID_NO, wxID_YES, wxID_NOTOALL, wxID_YESTOALL};
                    return std::find(ids, ids + 4, res) - ids;
            }, false);
            if (!files.empty())
                m_last_config = from_u8(files.back());
            MessageDialog dlg(this, wxString::Format(_L_PLURAL("There is %d config exported. (Only non-system configs)",
                "There are %d configs exported. (Only non-system configs)", files.size()), files.size()),
                              _L("Export result"), wxOK);
            dlg.ShowModal();
        } catch (const std::exception &ex) {
            show_error(this, ex.what());
        }
    }
}

// Load a config file containing a Print, Filament & Printer preset.
void MainFrame::load_config_file()
{
    //BBS do not load config file
 //   if (!wxGetApp().check_and_save_current_preset_changes(_L("Loading profile file"), "", false))
 //       return;
    wxFileDialog dlg(this, _L("Select profile to load:"),
        !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(),
        "config.json", "Config files (*.json;*.zip;*.orca_printer;*.orca_filament)|*.json;*.zip;*.orca_printer;*.orca_filament", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
     wxArrayString files;
    if (dlg.ShowModal() != wxID_OK)
        return;
    dlg.GetPaths(files);
    std::vector<std::string> cfiles;
    for (auto file : files) {
        cfiles.push_back(into_u8(file));
        m_last_config = file;
    }
    bool update = false;
    wxGetApp().preset_bundle->import_presets(cfiles, [this](std::string const & name) {
            ConfigsOverwriteConfirmDialog dlg(this, from_u8(name), false);
            int           res = dlg.ShowModal();
            int           ids[]{wxID_NO, wxID_YES, wxID_NOTOALL, wxID_YESTOALL};
            return std::find(ids, ids + 4, res) - ids;
        },
        ForwardCompatibilitySubstitutionRule::Enable);
    if (!cfiles.empty()) {
        wxGetApp().app_config->update_config_dir(get_dir_name(cfiles.back()));
        wxGetApp().load_current_presets();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " presets has been import,and size is" << cfiles.size();
        NetworkAgent* agent = wxGetApp().getAgent();
        if (agent) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " user is: " << agent->get_user_id();
        }
    }
    wxGetApp().preset_bundle->update_compatible(PresetSelectCompatibleType::Always);
    update_side_preset_ui();
    auto msg = wxString::Format(_L_PLURAL("There is %d config imported. (Only non-system and compatible configs)",
        "There are %d configs imported. (Only non-system and compatible configs)", cfiles.size()), cfiles.size());
    if(cfiles.empty())
        msg += _L("\nHint: Make sure you have added the corresponding printer before importing the configs.");
    MessageDialog dlg2(this,msg ,
                        _L("Import result"), wxOK);
    dlg2.ShowModal();
}

// Load a config file containing a Print, Filament & Printer preset from command line.
bool MainFrame::load_config_file(const std::string &path)
{
    try {
        ConfigSubstitutions config_substitutions = wxGetApp().preset_bundle->load_config_file(path, ForwardCompatibilitySubstitutionRule::Enable);
        if (!config_substitutions.empty())
            show_substitutions_info(config_substitutions, path);
    } catch (const std::exception &ex) {
        show_error(this, ex.what());
        return false;
    }
    wxGetApp().load_current_presets();
    return true;
}

//BBS: export current config bundle as BBL default reference
//void MainFrame::export_current_configbundle()
//{
    // BBS do not export profile
   // if (!wxGetApp().check_and_save_current_preset_changes(_L("Exporting current profile bundle"),
   //     _L("Some presets are modified and the unsaved changes will not be exported into profile bundle."), false, true))
   //     return;

   // // validate current configuration in case it's dirty
   // auto err = wxGetApp().preset_bundle->full_config().validate();
   // if (! err.empty()) {
   //     show_error(this, err);
   //     return;
   // }
   // // Ask user for a file name.
   // wxFileDialog dlg(this, _L("Save BBL Default bundle as:"),
   //     !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(),
   //     "BBL_config_bundle.ini",
   //     file_wildcards(FT_INI), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
   // wxString file;
   // if (dlg.ShowModal() == wxID_OK)
   //     file = dlg.GetPath();
   // if (!file.IsEmpty()) {
   //     // Export the config bundle.
   //     wxGetApp().app_config->update_config_dir(get_dir_name(file));
   //     try {
   //         wxGetApp().preset_bundle->export_current_configbundle(file.ToUTF8().data());
   //     } catch (const std::exception &ex) {
			//show_error(this, ex.what());
   //     }
   // }
//}

//BBS: export all the system preset configs to seperate files
/*void MainFrame::export_system_configs()
{
    // Ask user for a file name.
    wxDirDialog dlg(this, _L("choose a directory"),
        !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(), wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    wxString path;
    if (dlg.ShowModal() == wxID_OK)
        path = dlg.GetPath();
    if (!path.IsEmpty()) {
        // Export the config bundle.
        wxGetApp().app_config->update_config_dir(path.ToStdString());
        try {
            wxGetApp().preset_bundle->export_system_configs(path.ToUTF8().data());
        } catch (const std::exception &ex) {
            show_error(this, ex.what());
        }
    }
}*/

//void MainFrame::export_configbundle(bool export_physical_printers /*= false*/)
//{
////    ; //BBS do not export config bundle
//}

// Loading a config bundle with an external file name used to be used
// to auto - install a config bundle on a fresh user account,
// but that behavior was not documented and likely buggy.
//void MainFrame::load_configbundle(wxString file/* = wxEmptyString, const bool reset_user_profile*/)
//{
//    ; //BBS do not import config bundle
//}

// Load a provied DynamicConfig into the Print / Filament / Printer tabs, thus modifying the active preset.
// Also update the plater with the new presets.
void MainFrame::load_config(const DynamicPrintConfig& config)
{
	PrinterTechnology printer_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();
	const auto       *opt_printer_technology = config.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
	if (opt_printer_technology != nullptr && opt_printer_technology->value != printer_technology) {
		printer_technology = opt_printer_technology->value;
		this->plater()->set_printer_technology(printer_technology);
	}
#if 0
	for (auto tab : wxGetApp().tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
			if (tab->type() == Slic3r::Preset::TYPE_PRINTER)
				static_cast<TabPrinter*>(tab)->update_pages();
			tab->load_config(config);
		}
    if (m_plater)
        m_plater->on_config_change(config);
#else
	// Load the currently selected preset into the GUI, update the preset selection box.
    //FIXME this is not quite safe for multi-extruder printers,
    // as the number of extruders is not adjusted for the vector values.
    // (see PresetBundle::update_multi_material_filament_presets())
    // Better to call PresetBundle::load_config() instead?
    for (auto tab : wxGetApp().tabs_list)
        if (tab->supports_printer_technology(printer_technology)) {
            // Only apply keys, which are present in the tab's config. Ignore the other keys.
			for (const std::string &opt_key : tab->get_config()->diff(config))
				// Ignore print_settings_id, printer_settings_id, filament_settings_id etc.
				if (! boost::algorithm::ends_with(opt_key, "_settings_id"))
					tab->get_config()->option(opt_key)->set(config.option(opt_key));
        }

    wxGetApp().load_current_presets();
#endif
}

//BBS: GUI refactor
void MainFrame::select_tab(wxPanel* panel)
{
    if (!panel)
        return;
    if (panel == m_param_panel) {
        panel = m_plater;
    } else if (dynamic_cast<ParamsPanel*>(panel)) {
        wxGetApp().params_dialog()->Popup();
        return;
    }
    int page_idx = m_tabpanel->FindPage(panel);
    if (page_idx == tp3DEditor && m_tabpanel->GetSelection() == tpPreview)
        return;
    //BBS GUI refactor: remove unused layout new/dlg
    /*if (page_idx != wxNOT_FOUND && m_layout == ESettingsLayout::Dlg)
        page_idx++;*/
    select_tab(size_t(page_idx));
}

//BBS
void MainFrame::jump_to_monitor(std::string dev_id)
{
    if(!m_monitor)
        return;
    m_tabpanel->SetSelection(tpMonitor);
    ((MonitorPanel*)m_monitor)->select_machine(dev_id);
}

void MainFrame::jump_to_multipage()
{
    if(!m_multi_machine)
        return;
    m_tabpanel->SetSelection(tpMultiDevice);
    ((MultiMachinePage*)m_multi_machine)->jump_to_send_page();
}


//BBS GUI refactor: remove unused layout new/dlg
void MainFrame::select_tab(size_t tab/* = size_t(-1)*/)
{
    //bool tabpanel_was_hidden = false;

    // Controls on page are created on active page of active tab now.
    // We should select/activate tab before its showing to avoid an UI-flickering
    auto select = [this, tab](bool was_hidden) {
        // when tab == -1, it means we should show the last selected tab
        //BBS GUI refactor: remove unused layout new/dlg
        //size_t new_selection = tab == (size_t)(-1) ? m_last_selected_tab : (m_layout == ESettingsLayout::Dlg && tab != 0) ? tab - 1 : tab;
        size_t new_selection = tab == (size_t)(-1) ? m_last_selected_tab : tab;

        if (m_tabpanel->GetSelection() != (int)new_selection)
            m_tabpanel->SetSelection(new_selection);
#ifdef _MSW_DARK_MODE
        /*if (wxGetApp().tabs_as_menu()) {
            if (Tab* cur_tab = dynamic_cast<Tab*>(m_tabpanel->GetPage(new_selection)))
                update_marker_for_tabs_menu((m_layout == ESettingsLayout::Old ? m_menubar : m_settings_dialog.menubar()), cur_tab->title(), m_layout == ESettingsLayout::Old);
            else if (tab == 0 && m_layout == ESettingsLayout::Old)
                m_plater->get_current_canvas3D()->render();
        }*/
#endif
        if (tab == MainFrame::tp3DEditor && m_layout == ESettingsLayout::Old)
            m_plater->canvas3D()->render();
        else if (was_hidden) {
            Tab* cur_tab = dynamic_cast<Tab*>(m_tabpanel->GetPage(new_selection));
            if (cur_tab)
                cur_tab->OnActivate();
        }
    };

    select(false);
}

void MainFrame::request_select_tab(TabPosition pos)
{
    wxCommandEvent* evt = new wxCommandEvent(EVT_SELECT_TAB);
    evt->SetInt(pos);
    wxQueueEvent(this, evt);
}

int MainFrame::get_calibration_curr_tab() {
    if (m_calibration)
        return m_calibration->get_tabpanel()->GetSelection();
    return -1;
}

// Set a camera direction, zoom to all objects.
void MainFrame::select_view(const std::string& direction)
{
     if (m_plater)
         m_plater->select_view(direction);
}

// #ys_FIXME_to_delete
void MainFrame::on_presets_changed(SimpleEvent &event)
{
    auto *tab = dynamic_cast<Tab*>(event.GetEventObject());
    wxASSERT(tab != nullptr);
    if (tab == nullptr) {
        return;
    }

    // Update preset combo boxes(Print settings, Filament, Material, Printer) from their respective tabs.
    auto presets = tab->get_presets();
    if (m_plater != nullptr && presets != nullptr) {

        // FIXME: The preset type really should be a property of Tab instead
        Slic3r::Preset::Type preset_type = tab->type();
        if (preset_type == Slic3r::Preset::TYPE_INVALID) {
            wxASSERT(false);
            return;
        }

        m_plater->on_config_change(*tab->get_config());

        m_plater->sidebar().update_presets(preset_type);
    }
}

// #ys_FIXME_to_delete
void MainFrame::on_value_changed(wxCommandEvent& event)
{
    auto *tab = dynamic_cast<Tab*>(event.GetEventObject());
    wxASSERT(tab != nullptr);
    if (tab == nullptr)
        return;

    auto opt_key = event.GetString();
    if (m_plater) {
        m_plater->on_config_change(*tab->get_config()); // propagate config change events to the plater
        if (opt_key == "extruders_count") {
            auto value = event.GetInt();
            m_plater->on_filaments_change(value);
        }
    }
}

void MainFrame::on_config_changed(DynamicPrintConfig* config) const
{
    if (m_plater)
        m_plater->on_config_change(*config); // propagate config change events to the plater
}

void MainFrame::set_print_button_to_default(PrintSelectType select_type)
{
    if (select_type == PrintSelectType::ePrintPlate) {
        m_print_btn->SetLabel(_L("Print plate"));
        m_print_select = ePrintPlate;
        m_print_enable = get_enable_print_status();
        m_print_btn->Enable(m_print_enable);
        update_gfd_print_button();
        update_gfd_config_buttons();
        this->Layout();
    } else if (select_type == PrintSelectType::eSendGcode) {
        m_print_btn->SetLabel(_L("Print"));
        m_print_select = eSendGcode;
        m_print_enable = get_enable_print_status() && can_send_gcode();
        m_print_btn->Enable(m_print_enable);
        update_gfd_print_button();
        update_gfd_config_buttons();
        this->Layout();
    } else if (select_type == PrintSelectType::eExportGcode) {
        m_print_btn->SetLabel(_L("Export G-code file"));
        m_print_select = eExportGcode;
        m_print_enable = get_enable_print_status() && can_send_gcode();
        m_print_btn->Enable(m_print_enable);
        update_gfd_print_button();
        update_gfd_config_buttons();
        this->Layout();
    } else {
        // unsupport
        return;
    }
}

void MainFrame::bind_gfd_config_buttons()
{
    if (m_plater == nullptr)
        return;

    auto bind_gfd_config_button = [this](Button* button, const char* action, const std::function<void()>& handler) {
        if (button == nullptr)
            return;
        button->Bind(wxEVT_BUTTON, [this, action, handler](wxCommandEvent&) {
            BOOST_LOG_TRIVIAL(info) << "GFD config action clicked"
                                    << ", action=" << action;
            handler();
        });
    };

    bind_gfd_config_button(m_plater->gfd_cloud_import_button(), "cloud_import", [this]() {
        BOOST_LOG_TRIVIAL(info) << "GFD cloud import dialog opening";
        GFDCloudImportDialog dialog(this, m_plater);
        BOOST_LOG_TRIVIAL(info) << "GFD cloud import dialog ready";
        dialog.ShowModal();
    });
    bind_gfd_config_button(m_plater->gfd_dynamic_params_button(), "dynamic_params", [this]() {
        BOOST_LOG_TRIVIAL(info) << "GFD dynamic params dialog opening";
        GFDDynamicMaterialDialog dialog(this, m_plater);
        BOOST_LOG_TRIVIAL(info) << "GFD dynamic params dialog ready";
        dialog.ShowModal();
    });
    bind_gfd_config_button(m_plater->gfd_upload_config_button(), "upload_config", [this]() {
        GFDUploadConfigDialog dialog(this);
        if (dialog.ShowModal() != wxID_OK)
            return;

        const wxString name = dialog.config_name().Trim(true).Trim(false);
        if (name.empty()) {
            GUI::show_error(this, _L("请输入配置名称。"));
            return;
        }

        const wxString remarks = dialog.remarks().Trim(true).Trim(false);
        m_plater->upload_current_config_to_cloud(into_u8(name), into_u8(remarks));
    });
    bind_gfd_config_button(m_plater->gfd_save_config_button(), "save_config", [this]() {
        m_plater->save_active_imported_cloud_config();
    });
}

void MainFrame::update_gfd_config_buttons()
{
    if (m_plater == nullptr || m_plater->gfd_config_panel() == nullptr)
        return;

    wxPanel* panel = m_plater->gfd_config_panel();
    const bool was_shown = panel->IsShown();
    const bool was_save_shown = m_plater->gfd_save_config_button() != nullptr && m_plater->gfd_save_config_button()->IsShown();
    bool should_show = false;
    const GFDPrinterState printer_state = current_gfd_printer_state();
    should_show = !printer_state.effective_device_type.empty();

    const bool parameter_panel_shown = m_plater->is_sidebar_enabled() && !m_plater->is_sidebar_collapsed() &&
                                       (m_plater->is_view3D_shown() || m_plater->is_preview_shown());
    should_show = should_show && parameter_panel_shown;

    // GFD: per-device button visibility, driven by the central resources/gfd_button_config.json file.
    const auto button_vis = GFD::Config::button_visibility(printer_state.effective_device_type);
    const bool show_save_config = should_show && button_vis.save_config && m_plater->has_dirty_active_imported_cloud_config();

    panel->Show(should_show);
    if (m_plater->gfd_cloud_import_button() != nullptr)
        m_plater->gfd_cloud_import_button()->Show(should_show && button_vis.cloud_import);
    if (m_plater->gfd_upload_config_button() != nullptr)
        m_plater->gfd_upload_config_button()->Show(should_show && button_vis.upload_config);
    if (m_plater->gfd_dynamic_params_button() != nullptr)
        m_plater->gfd_dynamic_params_button()->Show(should_show && button_vis.dynamic_params);
    if (m_plater->gfd_save_config_button() != nullptr) {
        m_plater->gfd_save_config_button()->Enable(show_save_config);
        if (panel->GetSizer() != nullptr)
            panel->GetSizer()->Show(m_plater->gfd_save_config_button(), show_save_config, true);
        if (show_save_config)
            m_plater->gfd_save_config_button()->Show();
        else
            m_plater->gfd_save_config_button()->Hide();
    }

    const bool state_changed = (was_shown != should_show) || (was_save_shown != show_save_config);
    if (state_changed && should_show) {
        panel->Layout();
        m_plater->update_gfd_config_panel_position();
        panel->Raise();
        panel->Refresh();
        if (panel->GetParent() != nullptr) {
            panel->GetParent()->Layout();
            panel->GetParent()->Refresh();
        }
    } else if (state_changed && panel->GetParent() != nullptr) {
        panel->GetParent()->Layout();
        panel->GetParent()->Refresh();
    }

    BOOST_LOG_TRIVIAL(info) << "GFD config buttons visibility"
                            << ", selected_printer_model=" << printer_state.selected_printer_model
                            << ", selected_gfd_device_type=" << (printer_state.selected_device_type.empty() ? std::string("<empty>") : printer_state.selected_device_type)
                            << ", edited_printer_model=" << printer_state.edited_printer_model
                            << ", edited_gfd_device_type=" << (printer_state.edited_device_type.empty() ? std::string("<empty>") : printer_state.edited_device_type)
                            << ", effective_printer_model=" << printer_state.effective_printer_model
                            << ", effective_gfd_device_type=" << (printer_state.effective_device_type.empty() ? std::string("<empty>") : printer_state.effective_device_type)
                            << ", parameter_panel_shown=" << parameter_panel_shown
                            << ", show_save_config=" << show_save_config
                            << ", save_button_is_shown=" << (m_plater->gfd_save_config_button() != nullptr && m_plater->gfd_save_config_button()->IsShown())
                            << ", show=" << should_show;

    if (state_changed) {
        m_plater->Layout();
        Layout();
    }
}

void MainFrame::update_gfd_print_button()
{
    if (m_gfd_print_btn == nullptr)
        return;

    const bool was_shown = m_gfd_print_btn->IsShown();
    const bool can_show_side_tools = m_slice_btn != nullptr && m_slice_btn->IsShown();
    bool should_show_gfd_print_button = false;
    bool slice_ready = false;
    const GFDPrinterState printer_state = current_gfd_printer_state();
    const auto print_button_vis = GFD::Config::button_visibility(printer_state.effective_device_type);
    should_show_gfd_print_button = !printer_state.effective_device_type.empty() && print_button_vis.print;

    if (m_plater != nullptr) {
        PartPlate* current_plate = m_plater->get_partplate_list().get_curr_plate();
        const bool is_all_plates = m_plater->get_preview_canvas3D()->is_all_plates_selected();
        slice_ready = current_plate != nullptr && current_plate->is_slice_result_valid() && !is_all_plates &&
                      (current_plate->has_printable_instances() || current_plate->is_valid_gcode_file());
    }

    const bool show = can_show_side_tools && should_show_gfd_print_button && slice_ready;
    if (show) {
        m_gfd_print_btn->SetLabel(_L("Print"));
        m_gfd_print_btn->Enable(get_enable_gfd_print_status());
        m_gfd_print_btn->Show();
    } else {
        m_gfd_print_btn->Hide();
    }

    BOOST_LOG_TRIVIAL(info) << "GFD print button visibility"
                            << ", can_show_side_tools=" << can_show_side_tools
                            << ", selected_printer_model=" << printer_state.selected_printer_model
                            << ", selected_gfd_device_type=" << (printer_state.selected_device_type.empty() ? std::string("<empty>") : printer_state.selected_device_type)
                            << ", edited_printer_model=" << printer_state.edited_printer_model
                            << ", edited_gfd_device_type=" << (printer_state.edited_device_type.empty() ? std::string("<empty>") : printer_state.edited_device_type)
                            << ", effective_printer_model=" << printer_state.effective_printer_model
                            << ", effective_gfd_device_type=" << (printer_state.effective_device_type.empty() ? std::string("<empty>") : printer_state.effective_device_type)
                            << ", slice_ready=" << slice_ready
                            << ", show=" << show;

    if (was_shown != show)
        Layout();
}

void MainFrame::add_to_recent_projects(const wxString& filename)
{
    if (wxFileExists(filename))
    {
        m_recent_projects.AddFileToHistory(filename);
        std::vector<std::string> recent_projects;
        size_t count = m_recent_projects.GetCount();
        for (size_t i = 0; i < count; ++i)
        {
            recent_projects.push_back(into_u8(m_recent_projects.GetHistoryFile(i)));
        }
        wxGetApp().app_config->set_recent_projects(recent_projects);
        m_webview->SendRecentList(0);
    }
}

std::wstring MainFrame::FileHistory::GetThumbnailUrl(int index) const
{
    if (m_thumbnails[index].empty()) return L"";
    std::wstringstream wss;
    wss << L"data:image/png;base64,";
    wss << wxBase64Encode(m_thumbnails[index].data(), m_thumbnails[index].size());
    return wss.str();
}

void MainFrame::FileHistory::AddFileToHistory(const wxString &file)
{
    if (this->m_fileMaxFiles == 0)
        return;
    wxFileHistory::AddFileToHistory(file);
    if (m_load_called)
        m_thumbnails.push_front(bbs_3mf_get_thumbnail(into_u8(file).c_str()));
    else
        m_thumbnails.push_front("");
}

void MainFrame::FileHistory::RemoveFileFromHistory(size_t i)
{
    if (i >= m_thumbnails.size()) // FIX zero max
        return;
    wxFileHistory::RemoveFileFromHistory(i);
    m_thumbnails.erase(m_thumbnails.begin() + i);
}

size_t MainFrame::FileHistory::FindFileInHistory(const wxString & file)
{
    return m_fileHistory.Index(file);
}

void MainFrame::FileHistory::LoadThumbnails()
{
    tbb::parallel_for(tbb::blocked_range<size_t>(0, GetCount()), [this](tbb::blocked_range<size_t> range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            auto thumbnail = bbs_3mf_get_thumbnail(into_u8(GetHistoryFile(i)).c_str());
            if (!thumbnail.empty()) {
                m_thumbnails[i] = thumbnail;
            }
        }
    });
    m_load_called = true;
}

inline void MainFrame::FileHistory::SetMaxFiles(int max)
{
    m_fileMaxFiles  = max;
    size_t numFiles = m_fileHistory.size();
    while (numFiles > m_fileMaxFiles)
        RemoveFileFromHistory(--numFiles);
}

void MainFrame::get_recent_projects(boost::property_tree::wptree &tree, int images)
{
    for (size_t i = 0; i < m_recent_projects.GetCount(); ++i) {
        boost::property_tree::wptree item;
        std::wstring proj = m_recent_projects.GetHistoryFile(i).ToStdWstring();
        item.put(L"project_name", proj.substr(proj.find_last_of(L"/\\") + 1));
        item.put(L"path", proj);
        boost::system::error_code ec;
        std::time_t t = boost::filesystem::last_write_time(proj, ec);
        if (!ec) {
            std::wstring time = wxDateTime(t).FormatISOCombined(' ').ToStdWstring();
            item.put(L"time", time);
            if (i <= images) {
                auto thumbnail = m_recent_projects.GetThumbnailUrl(i);
                if (!thumbnail.empty()) item.put(L"image", thumbnail);
            }
        } else {
            item.put(L"time", _L("File is missing"));
        }
        tree.push_back({L"", item});
    }
}

void MainFrame::open_recent_project(size_t file_id, wxString const & filename)
{
    if (file_id == size_t(-1)) {
        file_id = m_recent_projects.FindFileInHistory(filename);
    }
    if (wxFileExists(filename)) {
        CallAfter([this, filename] {
            if (wxGetApp().can_load_project())
                m_plater->load_project(filename);
        });
    }
    else
    {
        MessageDialog msg(this, _L("The project is no longer available."), _L("Error"), wxOK | wxYES_DEFAULT);
        if (msg.ShowModal() == wxID_YES)
        {
            m_recent_projects.RemoveFileFromHistory(file_id);
            std::vector<std::string> recent_projects;
            size_t count = m_recent_projects.GetCount();
            for (size_t i = 0; i < count; ++i)
            {
                recent_projects.push_back(into_u8(m_recent_projects.GetHistoryFile(i)));
            }
            wxGetApp().app_config->set_recent_projects(recent_projects);
            m_webview->SendRecentList(-1);
        }
    }
}

void MainFrame::remove_recent_project(size_t file_id, wxString const &filename)
{
    if (file_id == size_t(-1)) {
        if (filename.IsEmpty())
            while (m_recent_projects.GetCount() > 0)
                m_recent_projects.RemoveFileFromHistory(0);
        else
            file_id = m_recent_projects.FindFileInHistory(filename);
    }
    if (file_id != size_t(-1))
        m_recent_projects.RemoveFileFromHistory(file_id);
    std::vector<std::string> recent_projects;
    size_t count = m_recent_projects.GetCount();
    for (size_t i = 0; i < count; ++i)
    {
        recent_projects.push_back(into_u8(m_recent_projects.GetHistoryFile(i)));
    }
    wxGetApp().app_config->set_recent_projects(recent_projects);
    m_webview->SendRecentList(-1);
}

void MainFrame::load_url(wxString url)
{
    BOOST_LOG_TRIVIAL(trace) << "load_url:" << url;
    auto evt = new wxCommandEvent(EVT_LOAD_URL, this->GetId());
    evt->SetString(url);
    wxQueueEvent(this, evt);
}

void MainFrame::load_printer_url(wxString url, wxString apikey)
{
    BOOST_LOG_TRIVIAL(trace) << "load_printer_url:" << url;
    auto evt = new LoadPrinterViewEvent(EVT_LOAD_PRINTER_URL, this->GetId());
    evt->SetString(url);
    evt->SetAPIkey(apikey);
    wxQueueEvent(this, evt);
}

void MainFrame::load_printer_url()
{
    PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
    if (preset_bundle.use_bbl_device_tab())
        return;

    auto     cfg = preset_bundle.printers.get_edited_preset().config;
    wxString url = cfg.opt_string("print_host_webui").empty() ? cfg.opt_string("print_host") : cfg.opt_string("print_host_webui");
    wxString apikey;
    const auto host_type = cfg.option<ConfigOptionEnum<PrintHostType>>("host_type")->value;
    if (cfg.has("printhost_apikey") && (host_type == htPrusaLink || host_type == htPrusaConnect))
        apikey = cfg.opt_string("printhost_apikey");
    if (!url.empty()) {
        if (!url.Lower().starts_with("http"))
            url = wxString::Format("http://%s", url);

        load_printer_url(url, apikey);
    }
}

bool MainFrame::is_printer_view() const { return m_tabpanel->GetSelection() == TabPosition::tpMonitor; }


void MainFrame::refresh_plugin_tips()
{
    if (m_webview != nullptr)
        m_webview->ShowNetpluginTip();
}

void MainFrame::RunScript(wxString js)
{
    if (m_webview != nullptr)
        m_webview->RunScript(js);
}

void MainFrame::technology_changed()
{
    // update menu titles
    PrinterTechnology pt = plater()->printer_technology();
    if (int id = m_menubar->FindMenu(pt == ptFFF ? _omitL("Material Settings") : _L("Filament Settings")); id != wxNOT_FOUND)
        m_menubar->SetMenuLabel(id, pt == ptSLA ? _omitL("Material Settings") : _L("Filament Settings"));
}

//
// Called after the Preferences dialog is closed and the program settings are saved.
// Update the UI based on the current preferences.
void MainFrame::update_ui_from_settings()
{
    if (m_plater)
        m_plater->update_ui_from_settings();
    for (auto tab: wxGetApp().tabs_list)
        tab->update_ui_from_settings();
}


void MainFrame::show_sync_dialog()
{
    SimpleEvent* evt = new SimpleEvent(EVT_SYNC_CLOUD_PRESET);
    wxQueueEvent(this, evt);
}

void MainFrame::update_side_preset_ui()
{
    // select last preset
    for (auto tab : wxGetApp().tabs_list) {
        tab->update_tab_ui();
    }

    //BBS: update the preset
    m_plater->sidebar().update_presets(Preset::TYPE_PRINTER);
    m_plater->sidebar().update_presets(Preset::TYPE_FILAMENT);
    update_gfd_print_button();
    update_gfd_config_buttons();
    Layout();


    //take off multi machine
    if(m_multi_machine){m_multi_machine->clear_page();}
}

void MainFrame::on_select_default_preset(SimpleEvent& evt)
{
    MessageDialog dialog(this,
                    _L("Do you want to synchronize your personal data from Bambu Cloud?\n"
                        "It contains the following information:\n"
                        "1. The Process presets\n"
                        "2. The Filament presets\n"
                        "3. The Printer presets"),
                    _L("Synchronization"),
                    wxCENTER |
                    wxYES_DEFAULT | wxYES_NO |
                    wxICON_INFORMATION);

    /* get setting list */
    NetworkAgent* agent = wxGetApp().getAgent();
    switch ( dialog.ShowModal() )
    {
        case wxID_YES: {
            wxGetApp().app_config->set_bool("sync_user_preset", true);
            wxGetApp().start_sync_user_preset(true);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: true";
            break;
        }
        case wxID_NO:
            wxGetApp().app_config->set_bool("sync_user_preset", false);
            wxGetApp().stop_sync_user_preset();
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: false";
            break;
        default:
            break;
    }

    update_side_preset_ui();
}

std::string MainFrame::get_base_name(const wxString &full_name, const char *extension) const
{
    boost::filesystem::path filename = boost::filesystem::path(full_name.wx_str()).filename();
    if (extension != nullptr)
		filename = filename.replace_extension(extension);
    return filename.string();
}

std::string MainFrame::get_dir_name(const wxString &full_name) const
{
    return boost::filesystem::path(into_u8(full_name)).parent_path().string();
}


// ----------------------------------------------------------------------------
// SettingsDialog
// ----------------------------------------------------------------------------

SettingsDialog::SettingsDialog(MainFrame* mainframe)
:DPIDialog(NULL, wxID_ANY, wxString(SLIC3R_APP_NAME) + " - " + _L("Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE, "settings_dialog"),
//: DPIDialog(mainframe, wxID_ANY, wxString(SLIC3R_APP_NAME) + " - " + _L("Settings"), wxDefaultPosition, wxDefaultSize,
//        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMINIMIZE_BOX | wxMAXIMIZE_BOX, "settings_dialog"),
    m_main_frame(mainframe)
{
    if (wxGetApp().is_gcode_viewer())
        return;

#if defined(__WXMSW__)
    // ys_FIXME! temporary workaround for correct font scaling
    // Because of from wxWidgets 3.1.3 auto rescaling is implemented for the Fonts,
    // From the very beginning set dialog font to the wxSYS_DEFAULT_GUI_FONT
    this->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
#else
    this->SetFont(wxGetApp().normal_font());
    this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif // __WXMSW__

    // Load the icon either from the exe, or from the ico file.
#if _WIN32
    {
        TCHAR szExeFileName[MAX_PATH];
        GetModuleFileName(nullptr, szExeFileName, MAX_PATH);
        SetIcon(wxIcon(szExeFileName, wxBITMAP_TYPE_ICO));
    }
#else
    SetIcon(wxIcon(var("OrcaSlicer_128px.png"), wxBITMAP_TYPE_PNG));
#endif // _WIN32

    //just hide the Frame on closing
    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& evt) { this->Hide(); });

#ifdef _MSW_DARK_MODE
    if (wxGetApp().tabs_as_menu()) {
        // menubar
        //m_menubar = new wxMenuBar();
        //add_tabs_as_menu(m_menubar, mainframe, this);
        //this->SetMenuBar(m_menubar);
    }
#endif

    // initialize layout
    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->SetSizeHints(this);
    SetSizer(sizer);
    Fit();

    const wxSize min_size = wxSize(85 * em_unit(), 50 * em_unit());
#ifdef __APPLE__
    // Using SetMinSize() on Mac messes up the window position in some cases
    // cf. https://groups.google.com/forum/#!topic/wx-users/yUKPBBfXWO0
    SetSize(min_size);
#else
    SetMinSize(min_size);
    SetSize(GetMinSize());
#endif
    Layout();
}

void SettingsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    if (wxGetApp().is_gcode_viewer())
        return;

    const int& em = em_unit();
    const wxSize& size = wxSize(85 * em, 50 * em);

    // BBS
    m_tabpanel->Rescale();

    // update Tabs
    for (auto tab : wxGetApp().tabs_list)
        tab->msw_rescale();

    SetMinSize(size);
    Fit();
    Refresh();
}


} // GUI
} // Slic3r
