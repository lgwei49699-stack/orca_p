#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Slic3r::ObjColorMapping {

using RGB8 = std::array<unsigned char, 3>;

inline unsigned char to_byte(float value)
{
    // GUI filament colors use RGB8. Round once, rather than truncating on every import.
    return std::isfinite(value) ? static_cast<unsigned char>(std::lround(std::clamp(value, 0.f, 1.f) * 255.f)) : 0;
}

inline RGB8 to_rgb8(const std::array<float, 4>& color) { return {to_byte(color[0]), to_byte(color[1]), to_byte(color[2])}; }

// Preserve discrete printable colors without a lossy Lab/cluster round trip.
// Alpha is not a filament property. Labels are stable in first-occurrence order.
inline bool collect_exact(const std::vector<std::array<float, 4>>& input,
                          std::size_t                              limit,
                          std::vector<std::array<float, 4>>&       colors,
                          std::vector<int>&                        labels)
{
    std::vector<RGB8> palette;
    colors.clear();
    labels.clear();
    labels.reserve(input.size());
    for (const auto& value : input) {
        const RGB8 color = to_rgb8(value);
        const auto found = std::find(palette.begin(), palette.end(), color);
        if (found != palette.end()) {
            labels.push_back(static_cast<int>(found - palette.begin()));
        } else {
            if (palette.size() >= limit) {
                colors.clear();
                labels.clear();
                return false;
            }
            labels.push_back(static_cast<int>(palette.size()));
            palette.push_back(color);
            colors.push_back({color[0] / 255.f, color[1] / 255.f, color[2] / 255.f, 1.f});
        }
    }
    return !colors.empty();
}

struct FilamentPlan
{
    std::vector<RGB8> added;
    std::vector<int>  slots; // One-based; zero means capacity exhausted and requires user/approximate mapping.
};

inline FilamentPlan plan_filaments(const std::vector<RGB8>& colors, const std::vector<RGB8>& existing, std::size_t limit)
{
    FilamentPlan      plan;
    std::vector<RGB8> palette = existing;
    for (const RGB8& color : colors) {
        const auto found = std::find(palette.begin(), palette.end(), color);
        if (found != palette.end()) {
            plan.slots.push_back(static_cast<int>(found - palette.begin()) + 1);
        } else if (palette.size() < limit) {
            palette.push_back(color);
            plan.added.push_back(color);
            plan.slots.push_back(static_cast<int>(palette.size()));
        } else {
            plan.slots.push_back(0);
        }
    }
    return plan;
}

// Manual remapping may leave proposed new slots unused. Only commit referenced slots,
// preserving proposal order and renumbering all references consistently.
inline FilamentPlan compact_filaments(std::size_t existing_count, const std::vector<RGB8>& proposed, const std::vector<int>& slots)
{
    FilamentPlan plan;
    plan.slots = slots;
    std::vector<int> remap(proposed.size(), 0);
    for (std::size_t i = 0; i < proposed.size(); ++i) {
        const int old_slot = static_cast<int>(existing_count + i + 1);
        if (std::find(slots.begin(), slots.end(), old_slot) != slots.end()) {
            plan.added.push_back(proposed[i]);
            remap[i] = static_cast<int>(existing_count + plan.added.size());
        }
    }
    for (int& slot : plan.slots) {
        if (slot > static_cast<int>(existing_count)) {
            const std::size_t index = static_cast<std::size_t>(slot) - existing_count - 1;
            slot                    = index < remap.size() ? remap[index] : 0;
        }
    }
    return plan;
}

} // namespace Slic3r::ObjColorMapping
