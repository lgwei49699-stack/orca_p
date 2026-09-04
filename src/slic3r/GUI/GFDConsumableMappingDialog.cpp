#include "GFDConsumableMappingDialog.hpp"

#include "GUI_App.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StaticBox.hpp"

#include "slic3r/Utils/ColorSpaceConvert.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>

#include <nlohmann/json.hpp>

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/wrapsizer.h>

namespace Slic3r { namespace GUI {

class GFDColorSwatch final : public wxPanel
{
public:
    explicit GFDColorSwatch(wxWindow* parent, std::vector<wxColour> colors = {})
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
        , m_colors(std::move(colors))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &GFDColorSwatch::on_paint, this);
    }

    void set_colors(std::vector<wxColour> colors)
    {
        if (colors.empty())
            colors.emplace_back(206, 206, 206);
        m_colors = std::move(colors);
        Refresh(false);
    }

private:
    void on_paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        const wxColour parent_background = StaticBox::GetParentBackgroundColor(GetParent());
        dc.SetBackground(wxBrush(parent_background));
        dc.Clear();

        const wxRect client_rect = GetClientRect();
        if (client_rect.width <= 2 || client_rect.height <= 2)
            return;

        const std::vector<wxColour> colors = m_colors.empty() ? std::vector<wxColour>{wxColour(206, 206, 206)} : m_colors;
        const int parent_luminance = (299 * parent_background.Red() + 587 * parent_background.Green() + 114 * parent_background.Blue()) / 1000;
        const wxColour border = parent_luminance < 128 ? wxColour(190, 190, 190) : wxColour(120, 120, 120);
        const double border_width = FromDIP(2);
        const double inset        = border_width / 2.0;
        const double left         = inset;
        const double top          = inset;
        const double width        = std::max(1.0, static_cast<double>(client_rect.width) - border_width);
        const double height       = std::max(1.0, static_cast<double>(client_rect.height) - border_width);
        const double radius       = FromDIP(5);

        std::unique_ptr<wxGraphicsContext> graphics(wxGraphicsContext::CreateFromUnknownDC(dc));
        if (graphics != nullptr) {
            wxGraphicsBrush fill;
            if (colors.size() == 1) {
                fill = graphics->CreateBrush(wxBrush(colors.front()));
            } else {
                wxGraphicsGradientStops stops(colors.front(), colors.back());
                for (size_t index = 1; index + 1 < colors.size(); ++index)
                    stops.Add(colors[index], static_cast<float>(index) / static_cast<float>(colors.size() - 1));
                fill = graphics->CreateLinearGradientBrush(left, top, left + width, top, stops);
            }

            graphics->SetPen(wxNullGraphicsPen);
            graphics->SetBrush(fill);
            graphics->DrawRoundedRectangle(left, top, width, height, radius);
            graphics->SetBrush(wxNullGraphicsBrush);
            graphics->SetPen(graphics->CreatePen(wxPen(border, FromDIP(2))));
            graphics->DrawRoundedRectangle(left, top, width, height, radius);
            return;
        }

        // Rare fallback for a platform DC without a graphics context.
        wxRect fill_rect = client_rect;
        fill_rect.Deflate(FromDIP(2));
        if (colors.size() == 1) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(colors.front()));
            dc.DrawRectangle(fill_rect);
        } else {
            for (size_t index = 0; index + 1 < colors.size(); ++index) {
                const int segment_left  = fill_rect.x + fill_rect.width * static_cast<int>(index) / static_cast<int>(colors.size() - 1);
                const int segment_right = fill_rect.x + fill_rect.width * static_cast<int>(index + 1) / static_cast<int>(colors.size() - 1);
                dc.GradientFillLinear(wxRect(segment_left,
                                             fill_rect.y,
                                             std::max(1, segment_right - segment_left),
                                             fill_rect.height),
                                      colors[index],
                                      colors[index + 1],
                                      wxEAST);
            }
        }
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(border, FromDIP(2)));
        dc.DrawRoundedRectangle(client_rect, FromDIP(5));
    }

    std::vector<wxColour> m_colors;
};

namespace {

using json = nlohmann::json;

constexpr int MAPPING_LOGICAL_COLUMN_WIDTH = 300;
constexpr int MAPPING_ARROW_COLUMN_WIDTH   = 78;
constexpr int MAPPING_SLOT_COLUMN_WIDTH    = 390;

std::string json_string(const json& value, const char* key)
{
    if (!value.contains(key) || value[key].is_null())
        return {};
    if (value[key].is_string())
        return value[key].get<std::string>();
    if (value[key].is_number_integer())
        return std::to_string(value[key].get<long long>());
    return {};
}

std::optional<bool> json_bool(const json& value, const char* key)
{
    if (!value.contains(key) || value[key].is_null())
        return std::nullopt;
    if (value[key].is_boolean())
        return value[key].get<bool>();
    if (value[key].is_number_integer()) {
        const int number = value[key].get<int>();
        if (number == 0 || number == 1)
            return number == 1;
        return std::nullopt;
    }
    if (value[key].is_string()) {
        std::string text = value[key].get<std::string>();
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (text == "true" || text == "1")
            return true;
        if (text == "false" || text == "0")
            return false;
    }
    return std::nullopt;
}

std::string first_json_string(const json& value, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const std::string result = json_string(value, key);
        if (!result.empty())
            return result;
    }
    return {};
}

std::optional<int> first_json_int(const json& value, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const auto it = value.find(key);
        if (it == value.end() || it->is_null())
            continue;
        try {
            long long number = 0;
            if (it->is_number_integer())
                number = it->get<long long>();
            else if (it->is_string())
                number = std::stoll(it->get<std::string>());
            else
                continue;
            if (number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max())
                return static_cast<int>(number);
        } catch (...) {}
    }
    return std::nullopt;
}

std::optional<bool> first_json_bool(const json& value, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        if (const auto result = json_bool(value, key); result.has_value())
            return result;
    }
    return std::nullopt;
}

const json* find_filament_slot_array(const json& value, int depth = 0)
{
    if (!value.is_object() || depth > 4)
        return nullptr;

    for (const char* key : {"filamentSlots", "filamentInfos", "filamentInfoList", "filamentColorInfoList", "filaments",
                            "filamentList", "ftInfos", "ftInfoList", "ftList", "mulColorInfos", "mulColorFtInfos",
                            "mulColorFtInfoList", "materials", "colors", "slots", "slotInfos", "list"}) {
        const auto it = value.find(key);
        if (it != value.end() && it->is_array() && std::any_of(it->begin(), it->end(), [](const json& item) {
                return item.is_object() &&
                       first_json_int(item, {"slotIndex", "slotNo", "slot", "slotId", "channel", "extruderIndex", "position"})
                           .has_value();
            }))
            return &*it;
    }

    for (const char* key : {"devicePrintInfo", "matlStationInfo", "materialStationInfo", "data", "result"}) {
        const auto it = value.find(key);
        if (it != value.end() && it->is_object()) {
            if (const json* result = find_filament_slot_array(*it, depth + 1); result != nullptr)
                return result;
        }
    }
    return nullptr;
}

const json* find_filament_slot_object(const json& value, int depth = 0)
{
    if (!value.is_object() || depth > 4)
        return nullptr;
    for (const char* key : {"ftInfo", "filamentInfo"}) {
        const auto it = value.find(key);
        if (it != value.end() && it->is_object() &&
            first_json_int(*it, {"slotIndex", "slotNo", "slot", "slotId", "channel", "extruderIndex", "position"}).has_value())
            return &*it;
    }
    for (const char* key : {"devicePrintInfo", "matlStationInfo", "materialStationInfo", "data", "result"}) {
        const auto it = value.find(key);
        if (it != value.end() && it->is_object()) {
            if (const json* result = find_filament_slot_object(*it, depth + 1); result != nullptr)
                return result;
        }
    }
    return nullptr;
}

std::optional<bool> find_multi_color_flag(const json& value, int depth = 0)
{
    if (!value.is_object() || depth > 4)
        return std::nullopt;
    if (const auto result = first_json_bool(
            value, {"mulColorFlag", "multiColorFlag", "multiColourFlag", "isMultiColor", "multiColor"});
        result.has_value())
        return result;
    for (const char* key : {"devicePrintInfo", "matlStationInfo", "materialStationInfo", "data", "result"}) {
        const auto it = value.find(key);
        if (it != value.end() && it->is_object()) {
            if (const auto result = find_multi_color_flag(*it, depth + 1); result.has_value())
                return result;
        }
    }
    return std::nullopt;
}

std::optional<wxColour> parse_color_token(std::string token)
{
    token.erase(token.begin(), std::find_if(token.begin(), token.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    token.erase(std::find_if(token.rbegin(), token.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), token.end());
    if (!token.empty() && token.front() == '#')
        token.erase(token.begin());
    if (token.size() != 6 && token.size() != 8)
        return std::nullopt;
    if (!std::all_of(token.begin(), token.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; }))
        return std::nullopt;

    try {
        const unsigned long rgb = std::stoul(token.substr(0, 6), nullptr, 16);
        return wxColour(static_cast<unsigned char>((rgb >> 16) & 0xff),
                        static_cast<unsigned char>((rgb >> 8) & 0xff),
                        static_cast<unsigned char>(rgb & 0xff));
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<wxColour> parse_colors(const std::string& text)
{
    std::vector<wxColour> colors;
    std::string           normalized = text;
    std::replace(normalized.begin(), normalized.end(), ';', ',');
    std::stringstream     stream(normalized);
    std::string           token;
    while (std::getline(stream, token, ',')) {
        if (const auto color = parse_color_token(token); color.has_value())
            colors.push_back(*color);
    }
    return colors;
}

std::vector<wxColour> filament_colors(const FilamentInfo& filament)
{
    std::vector<wxColour> colors;
    for (const std::string& color : filament.colors) {
        const std::vector<wxColour> parsed = parse_colors(color);
        colors.insert(colors.end(), parsed.begin(), parsed.end());
    }
    if (colors.empty())
        colors = parse_colors(filament.color);
    return colors;
}

wxString color_value_text(const std::vector<wxColour>& colors)
{
    if (colors.empty())
        return _L("未设置颜色");

    wxString text;
    for (const wxColour& color : colors) {
        if (!text.empty())
            text += wxString::FromUTF8(" / ");
        text += wxString::Format("#%02X%02X%02X", color.Red(), color.Green(), color.Blue());
    }
    return text;
}

wxColour secondary_text_color()
{
    return StateColor::darkModeColorFor(wxColour("#6B6B6B"));
}

wxColour card_border_color()
{
    // StaticBox performs StateColor's light/dark conversion while painting.
    return wxColour("#DBDBDB");
}

float color_distance(const wxColour& lhs, const wxColour& rhs)
{
    float lhs_lab[3];
    float rhs_lab[3];
    constexpr float RGB_SCALE = 1.0f / 255.0f;
    RGB2Lab(lhs.Red() * RGB_SCALE,
            lhs.Green() * RGB_SCALE,
            lhs.Blue() * RGB_SCALE,
            &lhs_lab[0],
            &lhs_lab[1],
            &lhs_lab[2]);
    RGB2Lab(rhs.Red() * RGB_SCALE,
            rhs.Green() * RGB_SCALE,
            rhs.Blue() * RGB_SCALE,
            &rhs_lab[0],
            &rhs_lab[1],
            &rhs_lab[2]);
    return DeltaE76(lhs_lab[0], lhs_lab[1], lhs_lab[2], rhs_lab[0], rhs_lab[1], rhs_lab[2]);
}

float minimum_color_distance(const std::vector<wxColour>& logical_colors, const std::vector<wxColour>& slot_colors)
{
    if (logical_colors.empty() || slot_colors.empty())
        return std::numeric_limits<float>::infinity();

    float result = std::numeric_limits<float>::infinity();
    for (const wxColour& logical_color : logical_colors) {
        for (const wxColour& slot_color : slot_colors)
            result = std::min(result, color_distance(logical_color, slot_color));
    }
    return result;
}

wxString slot_position_text(int slot_no)
{
    if (slot_no < 0 || slot_no > 15)
        return wxString::Format("%d", slot_no);
    return wxString::Format("%d%c", slot_no / 4 + 1, static_cast<wxChar>('A' + slot_no % 4));
}

wxString slot_display_text(const GFDDeviceFilamentSlot& slot)
{
    wxString text = _L("槽位 ") + slot_position_text(slot.slot_no) + wxString::Format(_L("（%d）"), slot.slot_no);
    if (!slot.color_title.empty())
        text += wxString::FromUTF8(" · ") + from_u8(slot.color_title);
    if (!slot.category_first_name.empty())
        text += wxString::FromUTF8(" · ") + from_u8(slot.category_first_name);
    else if (!slot.material_name.empty())
        text += wxString::FromUTF8(" · ") + from_u8(slot.material_name);
    return text;
}

void apply_list_button_style(Button* button, bool primary)
{
    if (button == nullptr)
        return;

    const wxColour green("#009688");
    const wxColour grey_bg("#DFDFDF");
    const wxColour grey_hover("#D4D4D4");
    const wxColour grey_text("#6B6A6A");
    // StateColor applies its dark palette lazily. Use the registered inverse key
    // in dark mode so primary button text remains visually white in both themes.
    const wxColour primary_text = wxGetApp().dark_mode() ? wxColour("#000000") : wxColour("#FFFFFF");

    button->SetFont(Label::Body_14);
    button->SetMinSize(button->FromDIP(wxSize(118, 30)));
    button->SetRoundedCorners(true, true, true, true);
    button->SetCornerRadius(button->FromDIP(15));
    button->SetBorderWidth(0);
    button->SetBackgroundColour(StaticBox::GetParentBackgroundColor(button->GetParent()));
    button->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(wxColour(240, 240, 241), StateColor::Disabled),
        std::pair<wxColour, int>(primary ? green : grey_bg, StateColor::Pressed),
        std::pair<wxColour, int>(primary ? green : grey_hover, StateColor::Hovered),
        std::pair<wxColour, int>(primary ? green : grey_bg, StateColor::Normal),
        std::pair<wxColour, int>(primary ? green : grey_bg, StateColor::Enabled)));
    button->SetBorderColor(StateColor(
        std::pair<wxColour, int>(wxColour(240, 240, 241), StateColor::Disabled),
        std::pair<wxColour, int>(primary ? green : grey_bg, StateColor::Normal),
        std::pair<wxColour, int>(primary ? green : grey_bg, StateColor::Focused)));
    button->SetTextColor(StateColor(
        std::pair<wxColour, int>(wxColour("#ACACAC"), StateColor::Disabled),
        std::pair<wxColour, int>(primary ? primary_text : grey_text, StateColor::Hovered),
        std::pair<wxColour, int>(primary ? primary_text : grey_text, StateColor::Normal)));
}

wxString slot_material_text(const GFDDeviceFilamentSlot& slot)
{
    if (!slot.category_first_name.empty())
        return from_u8(slot.category_first_name);
    if (!slot.material_name.empty())
        return from_u8(slot.material_name);
    return _L("未知");
}

wxString slot_detail_text(const GFDDeviceFilamentSlot& slot)
{
    wxString text;
    if (!slot.color_title.empty())
        text = from_u8(slot.color_title);

    const wxString material = slot_material_text(slot);
    if (!material.empty()) {
        if (!text.empty())
            text += wxString::FromUTF8(" · ");
        text += material;
    }
    return text;
}

class GFDSlotPickerDialog final : public DPIDialog
{
public:
    GFDSlotPickerDialog(wxWindow*                                parent,
                        const std::vector<GFDDeviceFilamentSlot>& slots,
                        size_t                                   selected_index,
                        const wxString&                          logical_filament_text)
        : DPIDialog(parent,
                    wxID_ANY,
                    _L("选择打印机耗材槽位"),
                    wxDefaultPosition,
                    wxDefaultSize,
                    wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
        , m_selected_index(selected_index)
    {
        const wxColour background = StateColor::darkModeColorFor(*wxWHITE);
        SetBackgroundColour(background);
        SetMinSize(wxSize(FromDIP(660), FromDIP(400)));

        auto* main_sizer = new wxBoxSizer(wxVERTICAL);
        auto* title      = new wxStaticText(this, wxID_ANY, _L("选择实体耗材槽位"));
        title->SetFont(Label::Head_16);
        main_sizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

        m_subtitle = new wxStaticText(
            this, wxID_ANY, _L("正在为 ") + logical_filament_text + _L(" 选择槽位。当前槽位以绿色描边标识，点击其他槽位即可替换。"));
        m_subtitle->SetForegroundColour(secondary_text_color());
        m_subtitle->Wrap(FromDIP(620));
        main_sizer->Add(m_subtitle, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(20));

        m_scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE);
        m_scroll->SetBackgroundColour(background);
        m_scroll->SetScrollRate(0, FromDIP(10));
        m_scroll->SetMinSize(wxSize(FromDIP(620), FromDIP(250)));
        auto* cards = new wxWrapSizer(wxHORIZONTAL);

        for (size_t index = 0; index < slots.size(); ++index) {
            const GFDDeviceFilamentSlot& slot = slots[index];
            const bool                       selected = index == selected_index;
            const std::vector<wxColour>       colors   = parse_colors(slot.color);
            const wxString                    color_values = color_value_text(colors);

            auto* card = new StaticBox(m_scroll, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(190), FromDIP(116)));
            card->SetMinSize(wxSize(FromDIP(190), FromDIP(116)));
            card->SetMaxSize(wxSize(FromDIP(190), FromDIP(116)));
            card->SetCornerRadius(FromDIP(8));
            card->SetBorderWidth(selected ? FromDIP(2) : FromDIP(1));
            card->SetBorderColorNormal(selected ? wxColour("#009688") : card_border_color());
            card->SetBackgroundColorNormal(background);
            card->SetToolTip(slot_display_text(slot) + _L("\n色值：") + color_values);

            auto* card_sizer = new wxBoxSizer(wxVERTICAL);
            auto* top_sizer = new wxBoxSizer(wxHORIZONTAL);
            auto* slot_title = new wxStaticText(card,
                                                wxID_ANY,
                                                _L("槽位 ") + slot_position_text(slot.slot_no) +
                                                    wxString::Format(_L("（%d）"), slot.slot_no));
            slot_title->SetFont(Label::Head_13);
            top_sizer->Add(slot_title, 0, wxALIGN_CENTER_VERTICAL);
            top_sizer->AddStretchSpacer(1);
            auto* selected_label = new wxStaticText(card, wxID_ANY, selected ? _L("当前") : wxString());
            selected_label->SetFont(Label::Body_11);
            selected_label->SetForegroundColour(wxColour("#009688"));
            top_sizer->Add(selected_label, 0, wxALIGN_CENTER_VERTICAL);
            card_sizer->Add(top_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

            auto* content_sizer = new wxBoxSizer(wxHORIZONTAL);
            auto* swatch = new GFDColorSwatch(card, colors);
            swatch->SetMinSize(wxSize(FromDIP(46), FromDIP(46)));
            swatch->SetMaxSize(wxSize(FromDIP(46), FromDIP(46)));
            swatch->SetToolTip(_L("实体耗材色值：") + color_values);
            content_sizer->Add(swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

            auto* detail_sizer = new wxBoxSizer(wxVERTICAL);
            auto* detail = new wxStaticText(card,
                                            wxID_ANY,
                                            slot_detail_text(slot),
                                            wxDefaultPosition,
                                            wxSize(FromDIP(108), -1),
                                            wxST_ELLIPSIZE_END);
            detail->SetFont(Label::Body_12);
            m_detail_labels.push_back(detail);
            auto* color_value = new wxStaticText(card,
                                                 wxID_ANY,
                                                 color_values,
                                                 wxDefaultPosition,
                                                 wxSize(FromDIP(108), -1),
                                                 wxST_ELLIPSIZE_END);
            color_value->SetFont(Label::Body_11);
            color_value->SetForegroundColour(secondary_text_color());
            m_color_value_labels.push_back(color_value);
            detail_sizer->Add(detail, 0, wxEXPAND);
            detail_sizer->Add(color_value, 0, wxEXPAND | wxTOP, FromDIP(4));
            content_sizer->Add(detail_sizer, 1, wxALIGN_CENTER_VERTICAL);
            card_sizer->Add(content_sizer, 1, wxEXPAND | wxALL, FromDIP(10));
            card->SetSizer(card_sizer);
            m_slot_cards.push_back(card);
            m_color_swatches.push_back(swatch);

            auto choose_slot = [this, index](wxMouseEvent&) {
                m_selected_index = index;
                EndModal(wxID_OK);
            };
            for (wxWindow* target : {static_cast<wxWindow*>(card),
                                     static_cast<wxWindow*>(slot_title),
                                     static_cast<wxWindow*>(selected_label),
                                     static_cast<wxWindow*>(swatch),
                                     static_cast<wxWindow*>(detail),
                                     static_cast<wxWindow*>(color_value)}) {
                target->SetCursor(wxCURSOR_HAND);
                target->Bind(wxEVT_LEFT_DOWN, choose_slot);
                target->SetToolTip(slot_display_text(slot) + _L("\n色值：") + color_values);
            }
            cards->Add(card, 0, wxALL, FromDIP(7));
        }

        m_scroll->SetSizer(cards);
        m_scroll->FitInside();
        main_sizer->Add(m_scroll, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(14));

        auto* button_sizer = new wxBoxSizer(wxHORIZONTAL);
        button_sizer->AddStretchSpacer(1);
        m_cancel_button = new Button(this, _L("取消"), "", 0, 0, wxID_CANCEL);
        apply_list_button_style(m_cancel_button, false);
        m_cancel_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
        button_sizer->Add(m_cancel_button, 0);
        main_sizer->Add(button_sizer, 0, wxEXPAND | wxALL, FromDIP(16));

        SetSizer(main_sizer);
        Layout();
        Fit();
        SetSize(wxSize(FromDIP(700), FromDIP(490)));
        SetEscapeId(wxID_CANCEL);
        wxGetApp().UpdateDlgDarkUI(this);
        CentreOnParent();
    }

    size_t selected_index() const { return m_selected_index; }

protected:
    void on_dpi_changed(const wxRect&) override
    {
        SetMinSize(wxSize(FromDIP(660), FromDIP(400)));
        m_subtitle->Wrap(FromDIP(620));
        m_scroll->SetScrollRate(0, FromDIP(10));
        m_scroll->SetMinSize(wxSize(FromDIP(620), FromDIP(250)));
        for (size_t index = 0; index < m_slot_cards.size(); ++index) {
            StaticBox* card = m_slot_cards[index];
            const wxSize size(FromDIP(190), FromDIP(116));
            card->SetSize(size);
            card->SetMinSize(size);
            card->SetMaxSize(size);
            card->SetCornerRadius(FromDIP(8));
            card->SetBorderWidth(index == m_selected_index ? FromDIP(2) : FromDIP(1));
        }
        for (GFDColorSwatch* swatch : m_color_swatches) {
            swatch->SetMinSize(wxSize(FromDIP(46), FromDIP(46)));
            swatch->SetMaxSize(wxSize(FromDIP(46), FromDIP(46)));
        }
        for (wxStaticText* detail : m_detail_labels)
            detail->SetMinSize(wxSize(FromDIP(108), -1));
        for (wxStaticText* color_value : m_color_value_labels)
            color_value->SetMinSize(wxSize(FromDIP(108), -1));
        apply_list_button_style(m_cancel_button, false);
        Layout();
        m_scroll->Layout();
        m_scroll->FitInside();
        Refresh();
    }

private:
    size_t                         m_selected_index{0};
    wxScrolledWindow*              m_scroll{nullptr};
    wxStaticText*                  m_subtitle{nullptr};
    std::vector<StaticBox*>        m_slot_cards;
    std::vector<GFDColorSwatch*>   m_color_swatches;
    std::vector<wxStaticText*>     m_detail_labels;
    std::vector<wxStaticText*>     m_color_value_labels;
    Button*                        m_cancel_button{nullptr};
};

} // namespace

bool parse_gfd_device_filament_info(const std::string&      response_body,
                                    GFDDeviceFilamentInfo& filament_info,
                                    std::string&           error_message)
{
    filament_info = {};
    error_message.clear();

    try {
        const json response = json::parse(response_body);
        if (!response.is_object()) {
            error_message = "耗材信息响应格式无效";
            return false;
        }

        if (response.value("code", -1) != 0) {
            error_message = response.value("msg", std::string("获取打印机耗材信息失败"));
            return false;
        }
        if (!response.contains("data") || (!response["data"].is_object() && !response["data"].is_array())) {
            error_message = "耗材信息响应缺少 data";
            return false;
        }

        const json& data = response["data"];
        const auto  mul_color_flag = data.is_object() ? find_multi_color_flag(data) : std::optional<bool>();
        const json* slots = data.is_array() ? &data : find_filament_slot_array(data);
        auto append_slot = [&filament_info](const json& item) {
            if (!item.is_object())
                return;

            const auto slot_no =
                first_json_int(item, {"slotIndex", "slotNo", "slot", "slotId", "channel", "extruderIndex", "position"});
            if (!slot_no.has_value())
                return;
            if (const auto has_filament = first_json_bool(item, {"hasFilament", "loaded", "occupied"});
                has_filament.has_value() && !*has_filament)
                return;

            GFDDeviceFilamentSlot slot;
            slot.slot_no = *slot_no;
            slot.filament_sn = first_json_string(item, {"sn", "ftSn", "filamentSn", "materialSn", "id"});
            slot.color = first_json_string(item, {"colorHex", "color", "ftColor", "filamentColor", "materialColor", "hex"});
            slot.color_title =
                first_json_string(item, {"colorTitle", "colourTitle", "colorName", "ftColorTitle", "filamentColorName"});
            slot.material_name =
                first_json_string(item, {"materialName", "filamentName", "ftName", "name", "materialType", "categoryName"});
            slot.category_first_name = first_json_string(item, {"categoryFirstName", "categoryName", "materialCategory"});
            slot.category_second_name =
                first_json_string(item, {"categorySecondName", "subCategoryName", "materialSubcategory"});
            filament_info.slots.push_back(std::move(slot));
        };
        if (slots != nullptr) {
            for (const json& item : *slots)
                append_slot(item);
        } else if (const json* slot = find_filament_slot_object(data); slot != nullptr) {
            append_slot(*slot);
        }

        filament_info.mul_color_flag = mul_color_flag.value_or(filament_info.slots.size() > 1);

        std::stable_sort(filament_info.slots.begin(), filament_info.slots.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.slot_no < rhs.slot_no;
        });
        return true;
    } catch (const std::exception& ex) {
        error_message = std::string("解析打印机耗材信息失败: ") + ex.what();
        return false;
    }
}

std::vector<size_t> match_gfd_filaments_to_slots(const std::vector<FilamentInfo>&          logical_filaments,
                                                 const std::vector<GFDDeviceFilamentSlot>& slots)
{
    std::vector<size_t> result;
    if (slots.empty())
        return result;

    result.reserve(logical_filaments.size());
    for (const FilamentInfo& filament : logical_filaments) {
        const std::vector<wxColour> logical_colors = filament_colors(filament);
        size_t                      best_index      = 0;
        float                       best_distance   = std::numeric_limits<float>::infinity();

        for (size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
            const float distance = minimum_color_distance(logical_colors, parse_colors(slots[slot_index].color));
            if (distance < best_distance) {
                best_distance = distance;
                best_index    = slot_index;
            }
        }
        result.push_back(best_index);
    }
    return result;
}

GFDConsumableMappingDialog::GFDConsumableMappingDialog(wxWindow*                         parent,
                                                       const std::string&                 device_mac,
                                                       std::vector<FilamentInfo>          logical_filaments,
                                                       std::vector<GFDDeviceFilamentSlot> slots,
                                                       std::vector<size_t>                default_slot_indices)
    : DPIDialog(parent,
                wxID_ANY,
                _L("多色耗材槽位确认"),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    , m_logical_filaments(std::move(logical_filaments))
    , m_slots(std::move(slots))
{
    const wxColour background = StateColor::darkModeColorFor(*wxWHITE);
    SetBackgroundColour(background);
    SetMinSize(wxSize(FromDIP(900), FromDIP(520)));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);
    auto* title_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* title      = new wxStaticText(this,
                                        wxID_ANY,
                                        _L("确认 3MF 耗材映射"));
    title->SetFont(Label::Head_16);
    title_sizer->Add(title, 0, wxALIGN_CENTER_VERTICAL);
    title_sizer->AddStretchSpacer(1);

    const wxString device_text = device_mac.empty() ? _L("当前打印机") : _L("打印机 MAC · ") + from_u8(device_mac);
    auto*          device_label = new wxStaticText(this, wxID_ANY, device_text);
    device_label->SetFont(Label::Body_12);
    device_label->SetForegroundColour(secondary_text_color());
    title_sizer->Add(device_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));
    main_sizer->Add(title_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    m_tip_panel = new StaticBox(this, wxID_ANY);
    m_tip_panel->SetCornerRadius(FromDIP(7));
    m_tip_panel->SetBorderWidth(0);
    m_tip_panel->SetBackgroundColorNormal(wxColour("#F8F8F8"));
    m_tip_panel->SetMinSize(wxSize(-1, FromDIP(52)));
    auto* tip_sizer = new wxBoxSizer(wxVERTICAL);
    m_tip_text = new wxStaticText(m_tip_panel,
                                  wxID_ANY,
                                  _L("左侧展示 3MF 模型中的逻辑耗材与模型色值，右侧展示打印机已装载的实体槽位、耗材和色值。"
                                     "系统已按颜色自动匹配，点击右侧槽位可重新选择。"));
    m_tip_text->Wrap(FromDIP(820));
    tip_sizer->Add(m_tip_text, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    m_tool_tip_text = new wxStaticText(
        m_tip_panel,
        wxID_ANY,
        _L("T0/T1 是切片文件中的逻辑工具编号，不是打印机槽位号；确认后将按 T 编号映射并继续上传、下发。"));
    m_tool_tip_text->SetFont(Label::Body_11);
    m_tool_tip_text->SetForegroundColour(secondary_text_color());
    tip_sizer->Add(m_tool_tip_text, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(12));
    m_tip_panel->SetSizer(tip_sizer);
    main_sizer->Add(m_tip_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* column_sizer = new wxBoxSizer(wxHORIZONTAL);
    column_sizer->AddSpacer(FromDIP(14));
    m_logical_header = new wxStaticText(this,
                                        wxID_ANY,
                                        _L("3MF 逻辑耗材 / 模型颜色"),
                                        wxDefaultPosition,
                                        wxSize(FromDIP(MAPPING_LOGICAL_COLUMN_WIDTH), -1));
    m_mapping_header = new wxStaticText(this,
                                        wxID_ANY,
                                        _L("映射"),
                                        wxDefaultPosition,
                                        wxSize(FromDIP(MAPPING_ARROW_COLUMN_WIDTH), -1),
                                        wxALIGN_CENTER_HORIZONTAL);
    m_slot_header = new wxStaticText(this,
                                     wxID_ANY,
                                     _L("打印机实体槽位 / 耗材颜色"),
                                     wxDefaultPosition,
                                     wxSize(FromDIP(MAPPING_SLOT_COLUMN_WIDTH), -1));
    m_logical_header->SetFont(Label::Head_12);
    m_mapping_header->SetFont(Label::Head_12);
    m_slot_header->SetFont(Label::Head_12);
    column_sizer->Add(m_logical_header, 0, wxALIGN_CENTER_VERTICAL);
    column_sizer->Add(m_mapping_header, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(10));
    column_sizer->Add(m_slot_header, 1, wxALIGN_CENTER_VERTICAL);
    main_sizer->AddSpacer(FromDIP(16));
    main_sizer->Add(column_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    m_mapping_scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE);
    m_mapping_scroll->SetBackgroundColour(background);
    m_mapping_scroll->SetScrollRate(0, FromDIP(10));
    const int visible_rows = std::max(2, std::min(4, static_cast<int>(m_logical_filaments.size())));
    m_mapping_scroll->SetMinSize(wxSize(FromDIP(840), FromDIP(visible_rows * 104)));
    auto* rows = new wxBoxSizer(wxVERTICAL);

    for (size_t index = 0; index < m_logical_filaments.size(); ++index) {
        const FilamentInfo& filament = m_logical_filaments[index];
        const size_t default_index = index < default_slot_indices.size() && default_slot_indices[index] < m_slots.size() ?
                                         default_slot_indices[index] :
                                         0;
        m_selected_slot_indices.push_back(default_index);

        auto* row = new StaticBox(m_mapping_scroll, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(92)));
        row->SetMinSize(wxSize(-1, FromDIP(92)));
        row->SetCornerRadius(FromDIP(8));
        row->SetBorderWidth(FromDIP(1));
        row->SetBorderColorNormal(card_border_color());
        row->SetBackgroundColorNormal(background);
        m_mapping_row_containers.push_back(row);

        auto* row_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* logical_panel = new wxPanel(row, wxID_ANY);
        logical_panel->SetBackgroundColour(background);
        logical_panel->SetMinSize(wxSize(FromDIP(MAPPING_LOGICAL_COLUMN_WIDTH), FromDIP(72)));
        m_logical_panels.push_back(logical_panel);
        auto* logical_sizer = new wxBoxSizer(wxHORIZONTAL);
        const std::vector<wxColour> logical_colors = filament_colors(filament);
        const wxString logical_color_values = color_value_text(logical_colors);
        auto* logical_swatch = new GFDColorSwatch(logical_panel, logical_colors);
        logical_swatch->SetMinSize(wxSize(FromDIP(48), FromDIP(48)));
        logical_swatch->SetMaxSize(wxSize(FromDIP(48), FromDIP(48)));
        logical_swatch->SetToolTip(_L("3MF 模型色值：") + logical_color_values);
        m_color_swatches.push_back(logical_swatch);
        logical_sizer->Add(logical_swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

        auto* logical_text_sizer = new wxBoxSizer(wxVERTICAL);
        auto* tool_label = new wxStaticText(logical_panel, wxID_ANY, wxString::Format("T%d", filament.id));
        tool_label->SetFont(Label::Head_14);
        const wxString material_name = filament.type.empty() ? _L("未设置材料") : from_u8(filament.type);
        auto* logical_detail = new wxStaticText(logical_panel,
                                                wxID_ANY,
                                                wxString::Format(_L("3MF 耗材 %d · "), filament.id + 1) + material_name,
                                                wxDefaultPosition,
                                                wxSize(FromDIP(220), -1),
                                                wxST_ELLIPSIZE_END);
        logical_detail->SetFont(Label::Body_12);
        m_logical_detail_labels.push_back(logical_detail);
        auto* logical_color_value = new wxStaticText(logical_panel,
                                                     wxID_ANY,
                                                     logical_color_values,
                                                     wxDefaultPosition,
                                                     wxSize(FromDIP(220), -1),
                                                     wxST_ELLIPSIZE_END);
        logical_color_value->SetFont(Label::Body_11);
        logical_color_value->SetForegroundColour(secondary_text_color());
        logical_color_value->SetToolTip(logical_color_values);
        m_logical_color_value_labels.push_back(logical_color_value);
        logical_text_sizer->Add(tool_label, 0, wxEXPAND);
        logical_text_sizer->Add(logical_detail, 0, wxEXPAND | wxTOP, FromDIP(2));
        logical_text_sizer->Add(logical_color_value, 0, wxEXPAND | wxTOP, FromDIP(2));
        logical_sizer->Add(logical_text_sizer, 1, wxALIGN_CENTER_VERTICAL);
        logical_panel->SetSizer(logical_sizer);
        row_sizer->Add(logical_panel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(14));

        auto* mapping_panel = new wxPanel(row, wxID_ANY);
        mapping_panel->SetBackgroundColour(background);
        mapping_panel->SetMinSize(wxSize(FromDIP(MAPPING_ARROW_COLUMN_WIDTH), FromDIP(72)));
        m_mapping_panels.push_back(mapping_panel);
        auto* mapping_sizer = new wxBoxSizer(wxVERTICAL);
        auto* arrow = new wxStaticText(mapping_panel, wxID_ANY, wxString::FromUTF8("→"), wxDefaultPosition,
                                       wxSize(FromDIP(MAPPING_ARROW_COLUMN_WIDTH), -1), wxALIGN_CENTER_HORIZONTAL);
        arrow->SetFont(Label::Head_18);
        arrow->SetForegroundColour(wxColour("#009688"));
        auto* mapping_text = new wxStaticText(mapping_panel, wxID_ANY, _L("映射到"), wxDefaultPosition,
                                              wxSize(FromDIP(MAPPING_ARROW_COLUMN_WIDTH), -1), wxALIGN_CENTER_HORIZONTAL);
        mapping_text->SetFont(Label::Body_10);
        mapping_text->SetForegroundColour(secondary_text_color());
        mapping_sizer->AddStretchSpacer(1);
        mapping_sizer->Add(arrow, 0, wxALIGN_CENTER_HORIZONTAL);
        mapping_sizer->Add(mapping_text, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(1));
        mapping_sizer->AddStretchSpacer(1);
        mapping_panel->SetSizer(mapping_sizer);
        row_sizer->Add(mapping_panel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(10));

        auto* slot_card = new StaticBox(row, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(MAPPING_SLOT_COLUMN_WIDTH), FromDIP(70)));
        slot_card->SetMinSize(wxSize(FromDIP(MAPPING_SLOT_COLUMN_WIDTH), FromDIP(70)));
        slot_card->SetCornerRadius(FromDIP(7));
        slot_card->SetBorderWidth(FromDIP(1));
        slot_card->SetBorderColorNormal(wxColour("#009688"));
        slot_card->SetBackgroundColorNormal(wxColour("#F8F8F8"));
        m_slot_card_containers.push_back(slot_card);

        auto* slot_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* slot_swatch = new GFDColorSwatch(slot_card);
        slot_swatch->SetMinSize(wxSize(FromDIP(48), FromDIP(48)));
        slot_swatch->SetMaxSize(wxSize(FromDIP(48), FromDIP(48)));
        m_color_swatches.push_back(slot_swatch);
        m_slot_color_swatches.push_back(slot_swatch);
        slot_sizer->Add(slot_swatch, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(10));

        auto* slot_text_sizer = new wxBoxSizer(wxVERTICAL);
        auto* slot_title = new wxStaticText(slot_card, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                            wxSize(FromDIP(220), -1), wxST_ELLIPSIZE_END);
        slot_title->SetFont(Label::Head_13);
        auto* slot_material = new wxStaticText(slot_card, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                               wxSize(FromDIP(220), -1), wxST_ELLIPSIZE_END);
        slot_material->SetFont(Label::Body_12);
        auto* slot_color_value = new wxStaticText(slot_card, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                                  wxSize(FromDIP(220), -1), wxST_ELLIPSIZE_END);
        slot_color_value->SetFont(Label::Body_11);
        slot_color_value->SetForegroundColour(secondary_text_color());
        slot_text_sizer->Add(slot_title, 0, wxEXPAND);
        slot_text_sizer->Add(slot_material, 0, wxEXPAND | wxTOP, FromDIP(2));
        slot_text_sizer->Add(slot_color_value, 0, wxEXPAND | wxTOP, FromDIP(2));
        slot_sizer->Add(slot_text_sizer, 1, wxALIGN_CENTER_VERTICAL);

        auto* change_label = new wxStaticText(slot_card, wxID_ANY, _L("更换  ›"));
        change_label->SetFont(Label::Body_12);
        change_label->SetForegroundColour(wxColour("#009688"));
        slot_sizer->Add(change_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(10));
        slot_card->SetSizer(slot_sizer);
        row_sizer->Add(slot_card, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

        m_slot_title_labels.push_back(slot_title);
        m_slot_material_labels.push_back(slot_material);
        m_slot_color_value_labels.push_back(slot_color_value);

        auto select_slot = [this, index](wxMouseEvent&) { select_slot_for_filament(index); };
        for (wxWindow* target : {static_cast<wxWindow*>(slot_card),
                                 static_cast<wxWindow*>(slot_swatch),
                                 static_cast<wxWindow*>(slot_title),
                                 static_cast<wxWindow*>(slot_material),
                                 static_cast<wxWindow*>(slot_color_value),
                                 static_cast<wxWindow*>(change_label)}) {
            target->SetCursor(wxCURSOR_HAND);
            target->Bind(wxEVT_LEFT_DOWN, select_slot);
        }

        row->SetSizer(row_sizer);
        rows->Add(row, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    }

    m_mapping_scroll->SetSizer(rows);
    m_mapping_scroll->FitInside();
    main_sizer->Add(m_mapping_scroll, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    auto* button_sizer = new wxBoxSizer(wxHORIZONTAL);
    button_sizer->AddStretchSpacer(1);

    auto* confirm_button = new Button(this, _L("确认映射并下发"), "", 0, 0, wxID_OK);
    apply_list_button_style(confirm_button, true);
    confirm_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_OK); });
    button_sizer->Add(confirm_button, 0, wxRIGHT, FromDIP(8));

    auto* cancel_button = new Button(this, _L("取消"), "", 0, 0, wxID_CANCEL);
    apply_list_button_style(cancel_button, false);
    cancel_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
    button_sizer->Add(cancel_button, 0);

    main_sizer->Add(button_sizer, 0, wxEXPAND | wxALL, FromDIP(16));

    SetSizer(main_sizer);
    Layout();
    Fit();
    const int dialog_height = std::max(540, std::min(720, 350 + visible_rows * 105));
    SetSize(wxSize(FromDIP(900), FromDIP(dialog_height)));
    SetAffirmativeId(wxID_OK);
    SetEscapeId(wxID_CANCEL);
    wxGetApp().UpdateDlgDarkUI(this);
    for (size_t index = 0; index < m_logical_filaments.size(); ++index)
        refresh_mapping_card(index);
    CentreOnParent();
}

void GFDConsumableMappingDialog::select_slot_for_filament(size_t filament_index)
{
    if (filament_index >= m_logical_filaments.size() || filament_index >= m_selected_slot_indices.size() || m_slots.empty())
        return;

    const FilamentInfo& filament = m_logical_filaments[filament_index];
    const wxString      logical_text = wxString::Format(_L("耗材 %d（T%d）"), filament.id + 1, filament.id);
    GFDSlotPickerDialog dialog(this, m_slots, m_selected_slot_indices[filament_index], logical_text);
    if (dialog.ShowModal() != wxID_OK)
        return;

    m_selected_slot_indices[filament_index] = dialog.selected_index();
    refresh_mapping_card(filament_index);
}

void GFDConsumableMappingDialog::refresh_mapping_card(size_t filament_index)
{
    if (filament_index >= m_logical_filaments.size() || filament_index >= m_selected_slot_indices.size() ||
        filament_index >= m_slot_card_containers.size() || filament_index >= m_slot_color_swatches.size() ||
        filament_index >= m_slot_title_labels.size() || filament_index >= m_slot_material_labels.size() ||
        filament_index >= m_slot_color_value_labels.size())
        return;

    const size_t slot_index = m_selected_slot_indices[filament_index];
    if (slot_index >= m_slots.size())
        return;

    const FilamentInfo&          filament = m_logical_filaments[filament_index];
    const GFDDeviceFilamentSlot& slot     = m_slots[slot_index];
    const std::vector<wxColour> slot_colors = parse_colors(slot.color);
    const wxString              color_values = color_value_text(slot_colors);
    const wxString              title = _L("槽位 ") + slot_position_text(slot.slot_no) + wxString::Format(_L("（%d）"), slot.slot_no);
    const wxString              material = slot_detail_text(slot);
    const wxString tooltip = wxString::Format(wxString::FromUTF8("T%d → "), filament.id) + slot_display_text(slot) +
                             _L("\n实体耗材色值：") + color_values;

    m_slot_color_swatches[filament_index]->set_colors(slot_colors);
    m_slot_title_labels[filament_index]->SetLabel(title);
    m_slot_material_labels[filament_index]->SetLabel(material);
    m_slot_color_value_labels[filament_index]->SetLabel(color_values);

    for (wxWindow* target : {static_cast<wxWindow*>(m_slot_card_containers[filament_index]),
                             static_cast<wxWindow*>(m_slot_color_swatches[filament_index]),
                             static_cast<wxWindow*>(m_slot_title_labels[filament_index]),
                             static_cast<wxWindow*>(m_slot_material_labels[filament_index]),
                             static_cast<wxWindow*>(m_slot_color_value_labels[filament_index])})
        target->SetToolTip(tooltip);

    m_slot_card_containers[filament_index]->Layout();
    m_slot_card_containers[filament_index]->Refresh();
}

std::vector<int> GFDConsumableMappingDialog::selected_consumables() const
{
    // PMC interprets the array index as the zero-based logical tool id (T0, T1, ...).
    // The dialog only contains tools used by the current plate, so the ids may be sparse
    // (for example, a plate may start at T1). Preserve those gaps with -1 and do not
    // deduplicate repeated physical slots: T0->0 and T1->0 must be encoded as [0, 0].
    std::vector<int> consumable;
    for (size_t filament_index = 0; filament_index < m_logical_filaments.size(); ++filament_index) {
        const int tool_id = m_logical_filaments[filament_index].id;
        if (tool_id < 0 || tool_id > GFD_MAX_LOGICAL_TOOL_ID || filament_index >= m_selected_slot_indices.size())
            continue;

        const size_t selection = m_selected_slot_indices[filament_index];
        if (selection >= m_slots.size())
            continue;

        if (consumable.size() <= static_cast<size_t>(tool_id))
            consumable.resize(static_cast<size_t>(tool_id) + 1, -1);
        consumable[static_cast<size_t>(tool_id)] = m_slots[selection].slot_no;
    }

    return consumable;
}

void GFDConsumableMappingDialog::on_dpi_changed(const wxRect&)
{
    SetMinSize(wxSize(FromDIP(900), FromDIP(520)));
    m_tip_panel->SetMinSize(wxSize(-1, FromDIP(52)));
    m_tip_panel->SetCornerRadius(FromDIP(7));
    m_tip_text->Wrap(FromDIP(820));
    m_tool_tip_text->Wrap(FromDIP(820));
    m_logical_header->SetMinSize(wxSize(FromDIP(MAPPING_LOGICAL_COLUMN_WIDTH), -1));
    m_mapping_header->SetMinSize(wxSize(FromDIP(MAPPING_ARROW_COLUMN_WIDTH), -1));
    m_slot_header->SetMinSize(wxSize(FromDIP(MAPPING_SLOT_COLUMN_WIDTH), -1));

    const int visible_rows = std::max(2, std::min(4, static_cast<int>(m_logical_filaments.size())));
    m_mapping_scroll->SetScrollRate(0, FromDIP(10));
    m_mapping_scroll->SetMinSize(wxSize(FromDIP(840), FromDIP(visible_rows * 104)));
    for (StaticBox* row : m_mapping_row_containers) {
        row->SetMinSize(wxSize(-1, FromDIP(92)));
        row->SetCornerRadius(FromDIP(8));
        row->SetBorderWidth(FromDIP(1));
    }
    for (wxWindow* panel : m_logical_panels)
        panel->SetMinSize(wxSize(FromDIP(MAPPING_LOGICAL_COLUMN_WIDTH), FromDIP(72)));
    for (wxWindow* panel : m_mapping_panels)
        panel->SetMinSize(wxSize(FromDIP(MAPPING_ARROW_COLUMN_WIDTH), FromDIP(72)));
    for (StaticBox* card : m_slot_card_containers) {
        card->SetMinSize(wxSize(FromDIP(MAPPING_SLOT_COLUMN_WIDTH), FromDIP(70)));
        card->SetCornerRadius(FromDIP(7));
        card->SetBorderWidth(FromDIP(1));
    }
    for (GFDColorSwatch* swatch : m_color_swatches) {
        swatch->SetMinSize(wxSize(FromDIP(48), FromDIP(48)));
        swatch->SetMaxSize(wxSize(FromDIP(48), FromDIP(48)));
    }
    for (wxStaticText* label : m_logical_detail_labels)
        label->SetMinSize(wxSize(FromDIP(220), -1));
    for (wxStaticText* label : m_logical_color_value_labels)
        label->SetMinSize(wxSize(FromDIP(220), -1));
    for (wxStaticText* label : m_slot_title_labels)
        label->SetMinSize(wxSize(FromDIP(220), -1));
    for (wxStaticText* label : m_slot_material_labels)
        label->SetMinSize(wxSize(FromDIP(220), -1));
    for (wxStaticText* label : m_slot_color_value_labels)
        label->SetMinSize(wxSize(FromDIP(220), -1));
    if (auto* confirm_button = dynamic_cast<Button*>(FindWindow(wxID_OK)); confirm_button != nullptr)
        apply_list_button_style(confirm_button, true);
    if (auto* cancel_button = dynamic_cast<Button*>(FindWindow(wxID_CANCEL)); cancel_button != nullptr)
        apply_list_button_style(cancel_button, false);
    Layout();
    m_mapping_scroll->Layout();
    m_mapping_scroll->FitInside();
    Refresh();
}

}} // namespace Slic3r::GUI
