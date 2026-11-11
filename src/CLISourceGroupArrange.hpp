#ifndef slic3r_CLISourceGroupArrange_hpp_
#define slic3r_CLISourceGroupArrange_hpp_

#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/ClipperUtils.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <string>
#include <vector>

namespace Slic3r {

struct CLIArrangeSource
{
    std::string              source;
    std::vector<std::string> colors; // Sorted distinct RGB values; empty means unknown.
};

// Source descriptors are in input order, parallel to items. Trials only mutate arrangement
// polygons. The caller applies setters once, after the final wipe-tower pass.
inline bool arrange_cli_source_groups(arrangement::ArrangePolygons&        items,
                                      const std::vector<CLIArrangeSource>& sources,
                                      bool                                 by_color,
                                      const arrangement::ArrangePolygons&  fixed,
                                      const Points&                        packing_bed,
                                      const Polygon&                       printable_bed,
                                      const arrangement::ArrangeParams&    params,
                                      int                                  bed_limit)
{
    using namespace arrangement;
    using Indices = std::vector<size_t>;
    if (items.size() != sources.size() || items.empty() ||
        std::any_of(items.begin(), items.end(), [](const ArrangePolygon& part) { return part.poly.contour.size() < 3; }))
        return false;

    using GroupKey = std::pair<std::string, std::vector<std::string>>;
    std::vector<Indices>       groups;
    std::map<GroupKey, size_t> index_by_group;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& source = sources[i];
        if (by_color && source.colors.size() > 1) {
            groups.push_back({i}); // A multicolor component is one indivisible candidate.
            continue;
        }
        const auto entry = index_by_group.emplace(GroupKey{source.source, source.colors}, groups.size());
        if (entry.second)
            groups.emplace_back();
        groups[entry.first->second].push_back(i);
    }

    // The packer works in bed-local coordinates starting at bin zero. Keep
    // reservations on their physical beds when starting a new anchor plate.
    const auto obstacles_from = [&](int first_bed, bool single_bed) {
        ArrangePolygons obstacles;
        for (const ArrangePolygon& obstacle : fixed) {
            if (obstacle.bed_idx >= first_bed && (!single_bed || obstacle.bed_idx == first_bed)) {
                obstacles.push_back(obstacle);
                obstacles.back().bed_idx -= first_bed;
            }
        }
        return obstacles;
    };

    // XY packing cannot correct a component already outside the Z range. Check
    // its actual top, including any per-model Z offset, once per tower pass.
    std::vector<bool> usable;
    usable.reserve(items.size());
    for (const ArrangePolygon& part : items) {
        const ModelInstance* instance = part.model_instance;
        const auto           bounds   = instance->get_object()->instance_convex_hull_bounding_box(instance);
        usable.push_back(part.height > 0 && bounds.max.z() > 0 && bounds.max.z() <= params.printable_height + EPSILON);
    }

    auto trial = [&](const Indices& indices, const ArrangePolygons& obstacles) {
        ArrangePolygons packed;
        packed.reserve(indices.size());
        for (size_t i : indices)
            if (usable[i])
                packed.push_back(items[i]);
        if (!packed.empty())
            arrange(packed, obstacles, packing_bed, params);

        ArrangePolygons layout;
        layout.reserve(indices.size());
        size_t next = 0;
        for (size_t i : indices) {
            if (!usable[i]) {
                layout.push_back(items[i]);
                layout.back().bed_idx = UNARRANGED;
                continue;
            }
            layout.push_back(std::move(packed[next++]));
            ArrangePolygon& part = layout.back();
            if (part.bed_idx < 0)
                continue;
            const Polygons footprint{part.transformed_poly().contour};
            const auto     overlaps = [&](const ArrangePolygon& obstacle) {
                return !intersection_ex(footprint, Polygons{obstacle.transformed_poly().contour}).empty();
            };
            if (!diff_ex(footprint, Polygons{printable_bed}).empty() ||
                std::any_of(params.excluded_regions.begin(), params.excluded_regions.end(), overlaps) ||
                std::any_of(obstacles.begin(), obstacles.end(),
                            [&](const ArrangePolygon& obstacle) { return part.bed_idx == obstacle.bed_idx && overlaps(obstacle); }))
                part.bed_idx = UNARRANGED;
        }
        return layout;
    };

    struct Layout
    {
        int             first_bed;
        Indices         indices;
        ArrangePolygons packed;
    };
    // Replace a layout only after every member fits the same physical bed.
    const auto try_add = [&](Layout& bed, const Indices& add, const ArrangePolygons& obstacles) {
        Indices candidate = bed.indices;
        candidate.insert(candidate.end(), add.begin(), add.end());
        ArrangePolygons packed = trial(candidate, obstacles);
        if (!arrange_result_fits_single_plate(packed))
            return false;
        bed.indices = std::move(candidate);
        bed.packed  = std::move(packed);
        return true;
    };

    int             first_bed   = 0;
    ArrangePolygons local_fixed = obstacles_from(0, true);
    ArrangePolygons all_layout;
    if (!by_color || std::all_of(sources.begin(), sources.end(), [&](const auto& s) { return s.colors == sources.front().colors; })) {
        Indices all(items.size());
        std::iota(all.begin(), all.end(), size_t{0});
        all_layout = trial(all, local_fixed);
        if (arrange_result_fits_single_plate(all_layout)) {
            items = std::move(all_layout);
            return true;
        }
    }

    std::vector<Layout> layouts;
    std::vector<bool> placed(groups.size(), false);
    for (size_t anchor = 0; anchor < groups.size(); ++anchor) {
        if (placed[anchor])
            continue;
        if (first_bed != 0)
            local_fixed = obstacles_from(first_bed, true);
        const Indices& bundle = groups[anchor];
        const auto&    key    = sources[bundle.front()];
        Layout         bed{first_bed, bundle, groups.size() == 1 ? std::move(all_layout) : trial(bundle, local_fixed)};
        placed[anchor] = true;
        if (arrange_result_fits_single_plate(bed.packed)) {
            const auto add_matching_groups = [&](const std::vector<std::string>& colors) {
                for (size_t next = anchor + 1; next < groups.size(); ++next) {
                    if (!placed[next] && sources[groups[next].front()].colors == colors && try_add(bed, groups[next], local_fixed))
                        placed[next] = true;
                }
            };
            add_matching_groups(key.colors);

            // All remaining exact-color partners were tried first. A lone pure bundle
            // can now return to a mixed sibling (including one later in the input).
            if (by_color && key.colors.size() == 1 && bed.indices.size() == bundle.size()) {
                const auto is_home = [&](size_t i) {
                    const auto& sibling = sources[i];
                    return sibling.source == key.source && sibling.colors.size() > 1 &&
                           std::binary_search(sibling.colors.begin(), sibling.colors.end(), key.colors.front());
                };
                bool returned = false;
                for (Layout& previous : layouts) {
                    if (arrange_result_fits_single_plate(previous.packed) &&
                        std::any_of(previous.indices.begin(), previous.indices.end(), is_home) &&
                        try_add(previous, bundle, obstacles_from(previous.first_bed, true))) {
                        returned = true;
                        break;
                    }
                }
                if (returned)
                    continue;
                for (size_t next = anchor + 1; next < groups.size(); ++next) {
                    size_t sibling = groups[next].front();
                    if (!placed[next] && is_home(sibling) && try_add(bed, groups[next], local_fixed)) {
                        placed[next] = true;
                        add_matching_groups(sources[sibling].colors);
                        break;
                    }
                }
            }
        } else if (std::any_of(fixed.begin(), fixed.end(), [&](const auto& obstacle) { return obstacle.bed_idx > first_bed; })) {
            bed.packed = trial(bundle, obstacles_from(first_bed, false));
        }
        for (const auto& part : bed.packed) {
            if (part.bed_idx < 0)
                continue;
            if (part.bed_idx >= bed_limit - bed.first_bed)
                return false;
            first_bed = std::max(first_bed, bed.first_bed + part.bed_idx + 1);
        }
        layouts.push_back(std::move(bed));
    }

    int first_order = 0;
    for (auto& bed : layouts) {
        for (size_t j = 0; j < bed.indices.size(); ++j) {
            auto& part = bed.packed[j];
            if (part.bed_idx >= 0) {
                part.bed_idx += bed.first_bed;
                part.itemid += first_order;
            }
            items[bed.indices[j]] = std::move(part);
        }
        first_order += static_cast<int>(bed.indices.size());
    }
    return true;
}

} // namespace Slic3r

#endif
