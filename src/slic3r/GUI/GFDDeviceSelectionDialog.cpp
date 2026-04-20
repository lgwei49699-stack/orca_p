#include "GFDDeviceSelectionDialog.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "Widgets/Button.hpp"
#include "format.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/GFDConfig.hpp"
#include "slic3r/Utils/Http.hpp"

#include <algorithm>
#include <set>

#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>

#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace Slic3r { namespace GUI {

namespace {

using json = nlohmann::json;

constexpr const char* ALL_VALUE = "All";
constexpr const char* BIZ_VALUE = "ZXBMan";

std::string trim_copy(const std::string& value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string json_string(const json& object, const char* key)
{
    if (!object.contains(key) || object[key].is_null())
        return {};
    if (object[key].is_string())
        return object[key].get<std::string>();
    if (object[key].is_number_integer())
        return std::to_string(object[key].get<long long>());
    if (object[key].is_number_unsigned())
        return std::to_string(object[key].get<unsigned long long>());
    if (object[key].is_number_float())
        return std::to_string(object[key].get<double>());
    return {};
}

std::string combo_client_data(wxChoice* choice)
{
    if (choice == nullptr || choice->GetSelection() == wxNOT_FOUND)
        return ALL_VALUE;

    if (auto* data = static_cast<wxStringClientData*>(choice->GetClientObject(choice->GetSelection())))
        return into_u8(data->GetData());

    return into_u8(choice->GetStringSelection());
}

void append_choice(wxChoice* choice, const wxString& label, const std::string& value)
{
    choice->Append(label, new wxStringClientData(from_u8(value)));
}

void set_choice_value(wxChoice* choice, const std::string& value)
{
    if (choice == nullptr)
        return;

    const std::string target = value.empty() ? ALL_VALUE : value;
    for (unsigned int i = 0; i < choice->GetCount(); ++i) {
        if (auto* data = static_cast<wxStringClientData*>(choice->GetClientObject(i));
            data != nullptr && into_u8(data->GetData()) == target) {
            choice->SetSelection(i);
            return;
        }
    }

    choice->SetSelection(0);
}

bool is_offline_device(const GFDDeviceInfo& device) { return lower_copy(device.device_status) == "offline"; }

wxString device_status_text(const GFDDeviceInfo& device)
{
    return from_u8(device.status_title.empty() ? device.device_status : device.status_title);
}

std::string device_key(const GFDDeviceInfo& device) { return !device.device_id.empty() ? device.device_id : device.mac; }

} // namespace

GFDDeviceSelectionDialog::GFDDeviceSelectionDialog(wxWindow* parent, std::string gcode_path, std::string default_device_type)
    : wxDialog(parent,
               wxID_ANY,
               _L("打印机选择"),
               wxDefaultPosition,
               wxDefaultSize,
               wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    , m_gcode_path(std::move(gcode_path))
    , m_default_device_type(std::move(default_device_type))
{
    build();
    bind_events();
    wxGetApp().UpdateDlgDarkUI(this);
    load_devices(false);
}

GFDDeviceSelectionDialog::~GFDDeviceSelectionDialog() = default;

void GFDDeviceSelectionDialog::build()
{
    SetSize(wxSize(FromDIP(1120), FromDIP(650)));
    SetMinSize(wxSize(FromDIP(1000), FromDIP(560)));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    auto* filter_sizer = new wxBoxSizer(wxHORIZONTAL);
    filter_sizer->SetMinSize(wxSize(-1, FromDIP(38)));
    filter_sizer->Add(new wxStaticText(this, wxID_ANY, _L("设备MAC:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_mac_input = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(135), FromDIP(30)));
    m_mac_input->SetHint(_L("请输入设备MAC"));
    filter_sizer->Add(m_mac_input, 0, wxRIGHT, FromDIP(10));

    filter_sizer->Add(new wxStaticText(this, wxID_ANY, _L("使用人:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_operator_input = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(110), FromDIP(30)));
    m_operator_input->SetHint(_L("请输入使用人"));
    filter_sizer->Add(m_operator_input, 0, wxRIGHT, FromDIP(10));

    filter_sizer->Add(new wxStaticText(this, wxID_ANY, _L("设备机型:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_type_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(145), FromDIP(30)));
    append_choice(m_type_choice, _L("全部"), ALL_VALUE);
    filter_sizer->Add(m_type_choice, 0, wxRIGHT, FromDIP(10));

    filter_sizer->Add(new wxStaticText(this, wxID_ANY, _L("设备状态:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_status_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(135), FromDIP(30)));
    append_choice(m_status_choice, _L("全部"), ALL_VALUE);
    filter_sizer->Add(m_status_choice, 0, wxRIGHT, FromDIP(10));

    m_search_button = new Button(this, _L("查找"));
    m_search_button->SetMinSize(wxSize(FromDIP(64), FromDIP(30)));
    filter_sizer->Add(m_search_button, 0, wxRIGHT, FromDIP(8));

    m_reset_button = new Button(this, _L("重置"));
    m_reset_button->SetMinSize(wxSize(FromDIP(64), FromDIP(30)));
    filter_sizer->Add(m_reset_button, 0);

    main_sizer->Add(filter_sizer, 0, wxEXPAND | wxALL, FromDIP(16));

    m_device_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxBORDER_SIMPLE);
    m_device_list->EnableCheckBoxes(true);
    m_device_list->AppendColumn(wxEmptyString, wxLIST_FORMAT_CENTER, FromDIP(42));
    m_device_list->AppendColumn(_L("设备mac"), wxLIST_FORMAT_CENTER, FromDIP(210));
    m_device_list->AppendColumn(_L("使用人"), wxLIST_FORMAT_CENTER, FromDIP(180));
    m_device_list->AppendColumn(_L("打印机型号"), wxLIST_FORMAT_CENTER, FromDIP(180));
    m_device_list->AppendColumn(_L("设备状态"), wxLIST_FORMAT_CENTER, FromDIP(150));
    m_device_list->AppendColumn(_L("固件版本号"), wxLIST_FORMAT_CENTER, FromDIP(180));
    main_sizer->Add(m_device_list, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

    if (!m_gcode_path.empty()) {
        auto* gcode_text = new wxStaticText(this, wxID_ANY, format_wxstr(_L("G-code 文件: %1%"), from_u8(m_gcode_path)));
        main_sizer->Add(gcode_text, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    }

    auto* button_sizer = new wxBoxSizer(wxHORIZONTAL);
    button_sizer->AddStretchSpacer(1);

    m_confirm_button = new Button(this, _L("确认"));
    m_confirm_button->SetMinSize(wxSize(FromDIP(70), FromDIP(30)));
    m_confirm_button->Disable();
    button_sizer->Add(m_confirm_button, 0);
    main_sizer->Add(button_sizer, 0, wxEXPAND | wxALL, FromDIP(16));

    SetSizer(main_sizer);
    Layout();
    update_table_column_widths();
    CentreOnParent();
}

void GFDDeviceSelectionDialog::bind_events()
{
    m_search_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { load_devices(true); });
    m_reset_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { reset_filters(); });
    m_confirm_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { accept_selection(); });
    m_mac_input->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!m_suppress_filter_events)
            refresh_table(true);
    });
    m_operator_input->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!m_suppress_filter_events)
            refresh_table(true);
    });
    m_type_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        if (!m_suppress_filter_events)
            refresh_table(true);
    });
    m_status_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        if (!m_suppress_filter_events)
            refresh_table(true);
    });
    m_device_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) { toggle_row_checked(event.GetIndex()); });
    m_device_list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { update_confirm_button_state(); });
    m_device_list->Bind(wxEVT_LIST_ITEM_CHECKED, [this](wxListEvent&) { update_confirm_button_state(); });
    m_device_list->Bind(wxEVT_LIST_ITEM_UNCHECKED, [this](wxListEvent&) { update_confirm_button_state(); });
    m_device_list->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        update_table_column_widths();
        event.Skip();
    });
}

void GFDDeviceSelectionDialog::load_devices(bool keep_current_filters)
{
    sync_checked_keys_from_table();

    std::string body;
    std::string error_message;
    if (!request_devices(body, error_message)) {
        show_error(this, from_u8(error_message.empty() ? "查询设备失败" : error_message));
        return;
    }

    std::vector<GFDDeviceInfo> devices;
    if (!parse_devices(body, devices, error_message)) {
        show_error(this, from_u8(error_message.empty() ? "解析设备数据失败" : error_message));
        return;
    }

    m_devices = std::move(devices);
    m_all_devices = m_devices;

    refresh_filter_choices(keep_current_filters ? current_type_filter() : m_default_device_type, current_status_filter());
    refresh_table(true, false);
}

void GFDDeviceSelectionDialog::refresh_filter_choices(const std::string& selected_type, const std::string& selected_status)
{
    std::set<std::string>                            types;
    std::vector<std::pair<std::string, std::string>> statuses;

    const std::vector<GFDDeviceInfo>& filter_source = !m_all_devices.empty() ? m_all_devices : m_devices;
    for (const GFDDeviceInfo& device : filter_source) {
        if (!device.device_type.empty())
            types.insert(device.device_type);
        if (!device.device_status.empty()) {
            const auto it = std::find_if(statuses.begin(), statuses.end(),
                                         [&device](const auto& item) { return item.first == device.device_status; });
            if (it == statuses.end())
                statuses.emplace_back(device.device_status, device.status_title.empty() ? device.device_status : device.status_title);
        }
    }

    m_suppress_filter_events = true;
    m_type_choice->Clear();
    append_choice(m_type_choice, _L("全部"), ALL_VALUE);
    for (const std::string& type : types)
        append_choice(m_type_choice, from_u8(type), type);
    set_choice_value(m_type_choice, selected_type);

    m_status_choice->Clear();
    append_choice(m_status_choice, _L("全部"), ALL_VALUE);
    for (const auto& status : statuses)
        append_choice(m_status_choice, from_u8(status.second), status.first);
    set_choice_value(m_status_choice, selected_status);
    m_suppress_filter_events = false;
}

void GFDDeviceSelectionDialog::refresh_table(bool apply_filters, bool sync_checked_keys)
{
    if (sync_checked_keys)
        sync_checked_keys_from_table();

    m_device_list->DeleteAllItems();
    m_visible_indices.clear();

    for (size_t i = 0; i < m_devices.size(); ++i) {
        const GFDDeviceInfo& device = m_devices[i];
        if (apply_filters && !match_filters(device))
            continue;

        const long row = m_device_list->InsertItem(m_device_list->GetItemCount(), wxEmptyString);
        m_visible_indices.push_back(i);
        m_device_list->SetItem(row, 1, from_u8(device.mac));
        m_device_list->SetItem(row, 2, from_u8(device.operator_name));
        m_device_list->SetItem(row, 3, from_u8(device.device_type));
        m_device_list->SetItem(row, 4, device_status_text(device));
        m_device_list->SetItem(row, 5, from_u8(device.last_version));
        m_device_list->SetItemData(row, static_cast<long>(m_visible_indices.size() - 1));
        if (m_checked_device_keys.find(device_key(device)) != m_checked_device_keys.end())
            m_device_list->CheckItem(row, true);
        if (is_offline_device(device))
            m_device_list->SetItemTextColour(row, wxColour(150, 150, 150));
    }

    update_confirm_button_state();
}

void GFDDeviceSelectionDialog::reset_filters()
{
    m_checked_device_keys.clear();
    m_suppress_filter_events = true;
    m_mac_input->Clear();
    m_operator_input->Clear();
    set_choice_value(m_type_choice, ALL_VALUE);
    set_choice_value(m_status_choice, ALL_VALUE);
    m_suppress_filter_events = false;
    m_devices = m_all_devices;
    refresh_table(true, false);
}

void GFDDeviceSelectionDialog::accept_selection()
{
    BOOST_LOG_TRIVIAL(info) << "GFD device selection confirm clicked"
                            << ", visible_rows=" << m_visible_indices.size()
                            << ", list_items=" << (m_device_list != nullptr ? m_device_list->GetItemCount() : 0);

    sync_checked_keys_from_table();

    bool has_offline = false;
    m_selected_devices = checked_devices(true, &has_offline);
    BOOST_LOG_TRIVIAL(info) << "GFD checked devices on confirm"
                            << ", checked_count=" << m_selected_devices.size()
                            << ", has_offline=" << has_offline;

    if (m_selected_devices.empty()) {
        bool selected_has_offline = false;
        m_selected_devices        = selected_row_devices(true, &selected_has_offline);
        has_offline              = has_offline || selected_has_offline;
        BOOST_LOG_TRIVIAL(info) << "GFD selected row fallback on confirm"
                                << ", selected_row_count=" << m_selected_devices.size()
                                << ", has_offline=" << selected_has_offline;
    }

    if (m_selected_devices.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "GFD device selection confirm rejected"
                                   << ", reason=" << (has_offline ? "offline device only" : "no selected device");
        show_error(this, has_offline ? _L("离线设备不能选择") : _L("请选择打印机"));
        return;
    }

    for (const GFDDeviceInfo& device : m_selected_devices) {
        BOOST_LOG_TRIVIAL(info) << "GFD device selection accepted"
                                << ", mac=" << device.mac
                                << ", operator=" << device.operator_name
                                << ", device_type=" << device.device_type
                                << ", status=" << device.device_status
                                << ", status_title=" << device.status_title
                                << ", sn=" << device.device_sn
                                << ", device_id=" << device.device_id;
    }

    EndModal(wxID_OK);
}

void GFDDeviceSelectionDialog::update_confirm_button_state()
{
    bool has_offline = false;
    const auto checked = checked_devices(true, &has_offline);
    bool selected_has_offline = false;
    const auto selected_rows = checked.empty() ? selected_row_devices(true, &selected_has_offline) : std::vector<GFDDeviceInfo>();
    m_confirm_button->Enable(!checked.empty() || !selected_rows.empty());
    BOOST_LOG_TRIVIAL(info) << "GFD device selection state updated"
                            << ", checked_online_count=" << checked.size()
                            << ", selected_row_online_count=" << selected_rows.size()
                            << ", has_offline=" << has_offline
                            << ", selected_has_offline=" << selected_has_offline
                            << ", confirm_enabled=" << m_confirm_button->IsEnabled();
}

void GFDDeviceSelectionDialog::toggle_row_checked(long row)
{
    if (row < 0 || row >= m_device_list->GetItemCount())
        return;
    m_device_list->CheckItem(row, !m_device_list->IsItemChecked(row));
    BOOST_LOG_TRIVIAL(info) << "GFD device row toggled"
                            << ", row=" << row
                            << ", checked=" << m_device_list->IsItemChecked(row);
    update_confirm_button_state();
}

void GFDDeviceSelectionDialog::sync_checked_keys_from_table()
{
    if (m_device_list == nullptr || static_cast<size_t>(m_device_list->GetItemCount()) != m_visible_indices.size())
        return;

    for (long row = 0; row < m_device_list->GetItemCount(); ++row) {
        const long visible_index = m_device_list->GetItemData(row);
        if (visible_index < 0 || static_cast<size_t>(visible_index) >= m_visible_indices.size())
            continue;

        const GFDDeviceInfo& device = m_devices[m_visible_indices[visible_index]];
        const std::string    key    = device_key(device);
        if (key.empty())
            continue;

        if (m_device_list->IsItemChecked(row))
            m_checked_device_keys.insert(key);
        else
            m_checked_device_keys.erase(key);
    }
}

void GFDDeviceSelectionDialog::update_table_column_widths()
{
    if (m_device_list == nullptr || m_device_list->GetColumnCount() < 6)
        return;

    const int client_width = std::max(FromDIP(940), m_device_list->GetClientSize().GetWidth() - FromDIP(8));
    const int check_width  = FromDIP(42);
    int       rest_width   = std::max(FromDIP(850), client_width - check_width);

    const int mac_width      = rest_width * 24 / 100;
    const int operator_width = rest_width * 20 / 100;
    const int type_width     = rest_width * 20 / 100;
    const int status_width   = rest_width * 16 / 100;
    const int version_width  = rest_width - mac_width - operator_width - type_width - status_width;

    m_device_list->SetColumnWidth(0, check_width);
    m_device_list->SetColumnWidth(1, mac_width);
    m_device_list->SetColumnWidth(2, operator_width);
    m_device_list->SetColumnWidth(3, type_width);
    m_device_list->SetColumnWidth(4, status_width);
    m_device_list->SetColumnWidth(5, version_width);
}

std::string GFDDeviceSelectionDialog::build_query_url() const
{
    return GFD::Config::device_query_url(wxGetApp().app_config);
}

bool GFDDeviceSelectionDialog::request_devices(std::string& body, std::string& error_message) const
{
    const std::string token = GFD::Config::auth_token(wxGetApp().app_config).empty() ? GFD::Config::verify_token(wxGetApp().app_config) :
                                                                                       GFD::Config::auth_token(wxGetApp().app_config);
    if (token.empty()) {
        error_message = "登录状态无效，请重新登录";
        return false;
    }

    bool ok = false;
    Http::get(build_query_url())
        .header("Authorization", token)
        .header("Biz", BIZ_VALUE)
        .on_complete([&](std::string response_body, unsigned) {
            body = std::move(response_body);
            ok   = true;
        })
        .on_error([&](std::string response_body, std::string error, unsigned) {
            body          = std::move(response_body);
            error_message = error.empty() ? response_body : error;
            ok            = false;
        })
        .perform_sync();

    return ok;
}

bool GFDDeviceSelectionDialog::parse_devices(const std::string& body, std::vector<GFDDeviceInfo>& devices, std::string& error_message) const
{
    devices.clear();
    try {
        const json        response = json::parse(body);
        const std::string msg      = json_string(response, "msg");
        if (!msg.empty() && msg != "success") {
            error_message = msg;
            return false;
        }

        if (!response.contains("data") || !response["data"].is_array())
            return true;

        for (const json& item : response["data"]) {
            if (!item.is_object())
                continue;
            GFDDeviceInfo device;
            device.mac           = json_string(item, "mac");
            device.operator_name = json_string(item, "operator");
            device.device_type   = json_string(item, "deviceType");
            device.device_status = json_string(item, "deviceStatus");
            device.status_title  = json_string(item, "deviceStatusTitle");
            device.last_version  = json_string(item, "lastVersionFormat");
            device.device_sn     = json_string(item, "sn");
            device.device_id     = json_string(item, "deviceId");
            devices.push_back(std::move(device));
        }
        return true;
    } catch (const std::exception& ex) {
        error_message = ex.what();
        return false;
    }
}

std::string GFDDeviceSelectionDialog::current_mac_filter() const { return trim_copy(into_u8(m_mac_input->GetValue())); }

std::string GFDDeviceSelectionDialog::current_operator_filter() const { return trim_copy(into_u8(m_operator_input->GetValue())); }

std::string GFDDeviceSelectionDialog::current_type_filter() const { return combo_client_data(m_type_choice); }

std::string GFDDeviceSelectionDialog::current_status_filter() const { return combo_client_data(m_status_choice); }

bool GFDDeviceSelectionDialog::match_filters(const GFDDeviceInfo& device) const
{
    const std::string mac_filter    = lower_copy(current_mac_filter());
    const std::string op_filter     = lower_copy(current_operator_filter());
    const std::string type_filter   = current_type_filter();
    const std::string status_filter = current_status_filter();

    const bool mac_match    = mac_filter.empty() || lower_copy(device.mac).find(mac_filter) != std::string::npos;
    const bool op_match     = op_filter.empty() || lower_copy(device.operator_name).find(op_filter) != std::string::npos;
    const bool type_match   = type_filter == ALL_VALUE || type_filter.empty() || device.device_type == type_filter;
    const bool status_match = status_filter == ALL_VALUE || status_filter.empty() || device.device_status == status_filter;
    return mac_match && op_match && type_match && status_match;
}

std::vector<GFDDeviceInfo> GFDDeviceSelectionDialog::checked_devices(bool skip_offline, bool* has_offline) const
{
    if (has_offline != nullptr)
        *has_offline = false;

    std::vector<GFDDeviceInfo> devices;
    for (long row = 0; row < m_device_list->GetItemCount(); ++row) {
        if (!m_device_list->IsItemChecked(row))
            continue;

        const long visible_index = m_device_list->GetItemData(row);
        if (visible_index < 0 || static_cast<size_t>(visible_index) >= m_visible_indices.size())
            continue;

        const GFDDeviceInfo& device = m_devices[m_visible_indices[visible_index]];
        if (is_offline_device(device)) {
            if (has_offline != nullptr)
                *has_offline = true;
            if (skip_offline)
                continue;
        }
        devices.push_back(device);
    }
    return devices;
}

std::vector<GFDDeviceInfo> GFDDeviceSelectionDialog::selected_row_devices(bool skip_offline, bool* has_offline) const
{
    if (has_offline != nullptr)
        *has_offline = false;

    std::vector<GFDDeviceInfo> devices;
    long row = -1;
    for (;;) {
        row = m_device_list->GetNextItem(row, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (row < 0)
            break;

        const long visible_index = m_device_list->GetItemData(row);
        if (visible_index < 0 || static_cast<size_t>(visible_index) >= m_visible_indices.size())
            continue;

        const GFDDeviceInfo& device = m_devices[m_visible_indices[visible_index]];
        if (is_offline_device(device)) {
            if (has_offline != nullptr)
                *has_offline = true;
            if (skip_offline)
                continue;
        }
        devices.push_back(device);
    }

    return devices;
}

}} // namespace Slic3r::GUI
