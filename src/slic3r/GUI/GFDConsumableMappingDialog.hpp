#ifndef slic3r_GFDConsumableMappingDialog_hpp_
#define slic3r_GFDConsumableMappingDialog_hpp_

#include "GUI_Utils.hpp"

#include "libslic3r/ProjectTask.hpp"

#include <cstddef>
#include <string>
#include <vector>

class wxStaticText;
class wxScrolledWindow;
class StaticBox;

namespace Slic3r { namespace GUI {

class GFDColorSwatch;

// Keep malformed or hostile G-code/3MF tool ids from expanding the PMC mapping
// array without bound. This remains well above the number of physical GFD slots.
constexpr int GFD_MAX_LOGICAL_TOOL_ID = 255;

struct GFDDeviceFilamentSlot
{
    int         slot_no{-1};
    std::string filament_sn;
    std::string color;
    std::string color_title;
    std::string material_name;
    std::string category_first_name;
    std::string category_second_name;
};

struct GFDDeviceFilamentInfo
{
    bool                               mul_color_flag{false};
    std::vector<GFDDeviceFilamentSlot> slots;
};

struct GFDConsumableMapping
{
    int         tool_id{-1};
    int         slot_no{-1};
    std::string material_type;
    std::string logical_color;
    std::string slot_color;
};

bool parse_gfd_device_filament_info(const std::string&     response_body,
                                    GFDDeviceFilamentInfo& filament_info,
                                    std::string&            error_message);

std::vector<size_t> match_gfd_filaments_to_slots(const std::vector<FilamentInfo>&          logical_filaments,
                                                 const std::vector<GFDDeviceFilamentSlot>& slots);

class GFDConsumableMappingDialog : public DPIDialog
{
public:
    GFDConsumableMappingDialog(wxWindow*                          parent,
                               const std::string&                 device_mac,
                               const std::string&                 device_type,
                               std::vector<FilamentInfo>          logical_filaments,
                               std::vector<GFDDeviceFilamentSlot> slots,
                               std::vector<size_t>                default_slot_indices);

    std::vector<GFDConsumableMapping> selected_mappings() const;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void select_slot_for_filament(size_t filament_index);
    void refresh_mapping_card(size_t filament_index);

    std::vector<FilamentInfo>          m_logical_filaments;
    std::vector<GFDDeviceFilamentSlot> m_slots;
    bool                               m_numeric_slot_labels{false};
    std::vector<size_t>                m_selected_slot_indices;
    wxScrolledWindow*                  m_mapping_scroll{nullptr};
    StaticBox*                         m_tip_panel{nullptr};
    wxStaticText*                      m_tip_text{nullptr};
    wxStaticText*                      m_tool_tip_text{nullptr};
    wxStaticText*                      m_logical_header{nullptr};
    wxStaticText*                      m_mapping_header{nullptr};
    wxStaticText*                      m_slot_header{nullptr};
    std::vector<StaticBox*>            m_mapping_row_containers;
    std::vector<StaticBox*>            m_slot_card_containers;
    std::vector<wxWindow*>             m_logical_panels;
    std::vector<wxWindow*>             m_mapping_panels;
    std::vector<GFDColorSwatch*>       m_color_swatches;
    std::vector<GFDColorSwatch*>       m_slot_color_swatches;
    std::vector<wxStaticText*>         m_logical_detail_labels;
    std::vector<wxStaticText*>         m_logical_color_value_labels;
    std::vector<wxStaticText*>         m_slot_title_labels;
    std::vector<wxStaticText*>         m_slot_material_labels;
    std::vector<wxStaticText*>         m_slot_color_value_labels;
};

}} // namespace Slic3r::GUI

#endif
