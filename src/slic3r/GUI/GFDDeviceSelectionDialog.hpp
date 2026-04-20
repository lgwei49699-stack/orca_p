#ifndef slic3r_GFDDeviceSelectionDialog_hpp_
#define slic3r_GFDDeviceSelectionDialog_hpp_

#include <set>
#include <string>
#include <vector>

#include <wx/dialog.h>

class Button;
class wxChoice;
class wxListCtrl;
class wxTextCtrl;

namespace Slic3r { namespace GUI {

struct GFDDeviceInfo
{
    std::string mac;
    std::string operator_name;
    std::string device_type;
    std::string device_status;
    std::string status_title;
    std::string last_version;
    std::string device_sn;
    std::string device_id;
};

class GFDDeviceSelectionDialog : public wxDialog
{
public:
    GFDDeviceSelectionDialog(wxWindow* parent, std::string gcode_path, std::string default_device_type);
    ~GFDDeviceSelectionDialog() override;

    const std::vector<GFDDeviceInfo>& selected_devices() const { return m_selected_devices; }
    const std::string&                gcode_path() const { return m_gcode_path; }

private:
    void build();
    void bind_events();
    void load_devices(bool keep_current_filters);
    void refresh_filter_choices(const std::string& selected_type, const std::string& selected_status);
    void refresh_table(bool apply_filters = true, bool sync_checked_keys = true);
    void reset_filters();
    void accept_selection();
    void update_confirm_button_state();
    void toggle_row_checked(long row);
    void sync_checked_keys_from_table();
    void update_table_column_widths();

    std::string build_query_url() const;
    bool        request_devices(std::string& body, std::string& error_message) const;
    bool        parse_devices(const std::string& body, std::vector<GFDDeviceInfo>& devices, std::string& error_message) const;

    std::string current_mac_filter() const;
    std::string current_operator_filter() const;
    std::string current_type_filter() const;
    std::string current_status_filter() const;
    bool        match_filters(const GFDDeviceInfo& device) const;

    std::vector<GFDDeviceInfo> checked_devices(bool skip_offline, bool* has_offline) const;
    std::vector<GFDDeviceInfo> selected_row_devices(bool skip_offline, bool* has_offline) const;

private:
    std::string m_gcode_path;
    std::string m_default_device_type;

    std::vector<GFDDeviceInfo> m_devices;
    std::vector<GFDDeviceInfo> m_all_devices;
    std::vector<size_t>        m_visible_indices;
    std::vector<GFDDeviceInfo> m_selected_devices;
    std::set<std::string>      m_checked_device_keys;
    bool                       m_suppress_filter_events{false};

    wxTextCtrl* m_mac_input{nullptr};
    wxTextCtrl* m_operator_input{nullptr};
    wxChoice*   m_type_choice{nullptr};
    wxChoice*   m_status_choice{nullptr};
    wxListCtrl* m_device_list{nullptr};
    Button*     m_search_button{nullptr};
    Button*     m_reset_button{nullptr};
    Button*     m_confirm_button{nullptr};
};

}} // namespace Slic3r::GUI

#endif
