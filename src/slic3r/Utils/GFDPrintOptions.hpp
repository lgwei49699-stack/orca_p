#ifndef slic3r_GFDPrintOptions_hpp_
#define slic3r_GFDPrintOptions_hpp_

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace GFD {

inline bool is_advancer3(const std::string& device_type) { return device_type == "Advancer3"; }

struct DeviceSlot
{
    int         slot;
    std::string material;
    std::string color;
};

struct SlotMapping
{
    int         tool;
    int         slot;
    std::string material;
    std::string source_color;
    std::string target_color;
};

struct Advancer3PrintSettings
{
    bool                     leveling_before_print{true};
    std::vector<SlotMapping> mappings;
};

struct PrintOptions
{
    std::vector<int>                      consumable;
    std::optional<Advancer3PrintSettings> advancer3;
};

inline bool is_rgb_color(const std::string& color)
{
    return color.size() == 7 && color.front() == '#' && std::all_of(color.begin() + 1, color.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
           });
}

// PMC reports physical IFS slots numbered 1..4. Empty slots have no usable material;
// unlike the generic filament API, this response does not contain ftSn.
inline std::vector<DeviceSlot> parse_advancer3_slots(const std::string& body, const std::string& device_mac)
{
    const auto response = nlohmann::json::parse(body);
    if (response.at("code") != 0)
        throw std::runtime_error("PMC device status request failed");
    const auto& data = response.at("data");
    if (device_mac.empty() || data.at("deviceId").get<std::string>() != device_mac)
        throw std::runtime_error("PMC device identity does not match the selected printer");
    if (data.value("deviceState", std::string()) == "offline")
        throw std::runtime_error("PMC device is offline");
    const auto& entries = data.at("matlStationInfo").at("slotInfos");
    if (!entries.is_array() || entries.empty() || entries.size() > 4)
        throw std::runtime_error("PMC returned no valid IFS slot list");
    std::vector<DeviceSlot> slots;
    std::set<int>           seen;
    for (const auto& entry : entries) {
        const auto& id = entry.at("slotId");
        if (!id.is_number_integer() || id < 1 || id > 4)
            throw std::runtime_error("PMC returned an invalid IFS slot number");
        const int slot = id.get<int>();
        if (!seen.insert(slot).second)
            throw std::runtime_error("PMC returned duplicate IFS slots");
        if (!entry.at("hasFilament").get<bool>())
            continue;
        DeviceSlot loaded{slot, entry.at("materialName").get<std::string>(), entry.at("materialColor").get<std::string>()};
        if (loaded.material.empty() || !is_rgb_color(loaded.color))
            throw std::runtime_error("PMC returned incomplete loaded material information");
        slots.push_back(std::move(loaded));
    }
    if (slots.empty())
        throw std::runtime_error("PMC returned no loaded IFS slots");
    std::sort(slots.begin(), slots.end(), [](const DeviceSlot& a, const DeviceSlot& b) { return a.slot < b.slot; });
    return slots;
}

inline void validate_advancer3_settings(const Advancer3PrintSettings& settings)
{
    if (settings.mappings.empty())
        throw std::runtime_error("No Advancer3 tool mappings selected");
    std::set<int> tools;
    for (const auto& mapping : settings.mappings) {
        if (mapping.tool < 0 || mapping.tool > 255 || !tools.insert(mapping.tool).second || mapping.slot < 1 || mapping.slot > 4 ||
            mapping.material.empty() || !is_rgb_color(mapping.source_color) || !is_rgb_color(mapping.target_color))
            throw std::runtime_error("Invalid Advancer3 tool mapping");
    }
}

inline nlohmann::json build_print_command(const std::string&  device_type,
                                          const std::string&  device_id,
                                          const std::string&  device_mac,
                                          const std::string&  file_url,
                                          const std::string&  file_type,
                                          const std::string&  file_name,
                                          const std::string&  request_id,
                                          const PrintOptions& options)
{
    nlohmann::json request{{"url", file_url},
                           {"deviceId", device_id},
                           {"printSn", request_id},
                           {"requestId", request_id},
                           {"fileType", file_type}};
    if (!is_advancer3(device_type)) {
        if (!options.consumable.empty())
            request["consumable"] = options.consumable;
        return request;
    }
    if (device_mac.empty() || file_type != "gcode" || !options.advancer3)
        throw std::runtime_error("Advancer3 requires a device MAC, G-code and confirmed print settings");
    validate_advancer3_settings(*options.advancer3);
    // The Flashforge adapter appends the URL extension itself.
    auto       name      = file_name.substr(file_name.find_last_of("/\\") == std::string::npos ? 0 : file_name.find_last_of("/\\") + 1);
    const auto extension = name.find_last_of('.');
    if (extension != std::string::npos)
        name.resize(extension);
    if (name.empty())
        throw std::runtime_error("Advancer3 requires a print file name");
    request["deviceId"]            = device_mac;
    request["fileName"]            = name;
    request["source"]              = "slicer";
    request["levelingBeforePrint"] = options.advancer3->leveling_before_print;
    request["ms"]                  = nlohmann::json::array();
    for (const auto& mapping : options.advancer3->mappings) {
        request["ms"].push_back({{"tNum", mapping.tool},
                                 {"slot", mapping.slot},
                                 {"mt", mapping.material},
                                 {"sRgb", mapping.source_color},
                                 {"tRgb", mapping.target_color}});
    }
    return request;
}

}} // namespace Slic3r::GFD

#endif
