#ifndef slic3r_CLISourceGroupArrange_hpp_
#define slic3r_CLISourceGroupArrange_hpp_

#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/ClipperUtils.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <vector>

namespace Slic3r {

// Group ids are in input order, parallel to items. Trials only mutate arrangement
// polygons. The caller applies setters once, after the final wipe-tower pass.
inline bool arrange_cli_source_groups(arrangement::ArrangePolygons&       items,
                                      const std::vector<std::string>&     source_ids,
                                      const arrangement::ArrangePolygons& fixed,
                                      const Points&                       packing_bed,
                                      const Polygon&                      printable_bed,
                                      const arrangement::ArrangeParams&   params,
                                      int                                 bed_limit)
{
    using namespace arrangement;
    using Indices = std::vector<size_t>;
    if (items.size() != source_ids.size() || items.empty() ||
        std::any_of(items.begin(), items.end(), [](const ArrangePolygon& part) { return part.poly.contour.size() < 3; }))
        return false;

    std::vector<Indices>          groups;
    std::map<std::string, size_t> index_by_source;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto entry = index_by_source.emplace(source_ids[i], groups.size());
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

    int             first_bed   = 0;
    ArrangePolygons local_fixed = obstacles_from(first_bed, true);
    Indices         all(items.size());
    std::iota(all.begin(), all.end(), size_t{0});
    ArrangePolygons all_layout = trial(all, local_fixed);
    if (arrange_result_fits_single_plate(all_layout)) {
        items = std::move(all_layout);
        return true;
    }

    ArrangePolygons   result = items;
    std::vector<bool> placed(groups.size(), false);
    int               first_order = 0;
    for (size_t anchor = 0; anchor < groups.size(); ++anchor) {
        if (placed[anchor])
            continue;
        if (first_bed != 0)
            local_fixed = obstacles_from(first_bed, true);

        Indices         accepted = groups[anchor];
        ArrangePolygons layout   = groups.size() == 1 ? std::move(all_layout) : trial(accepted, local_fixed);
        placed[anchor]           = true;
        // A split or overflowing anchor owns its entire layout exclusively.
        if (arrange_result_fits_single_plate(layout)) {
            for (size_t next = anchor + 1; next < groups.size(); ++next) {
                if (placed[next])
                    continue;
                Indices candidate = accepted;
                candidate.insert(candidate.end(), groups[next].begin(), groups[next].end());
                ArrangePolygons candidate_layout = trial(candidate, local_fixed);
                if (arrange_result_fits_single_plate(candidate_layout)) {
                    accepted     = std::move(candidate);
                    layout       = std::move(candidate_layout);
                    placed[next] = true;
                }
            }
        } else if (std::any_of(fixed.begin(), fixed.end(), [&](const ArrangePolygon& obstacle) { return obstacle.bed_idx > first_bed; })) {
            // A one-bed trial only saw the anchor bed's obstacles. Before
            // adopting a split layout, restore the remaining beds' reservations.
            layout = trial(accepted, obstacles_from(first_bed, false));
        }
        int next_bed = first_bed;
        for (size_t j = 0; j < accepted.size(); ++j) {
            ArrangePolygon& part = layout[j];
            if (part.bed_idx >= 0) {
                if (part.bed_idx >= bed_limit - first_bed)
                    return false;
                part.bed_idx += first_bed;
                part.itemid += first_order;
                next_bed = std::max(next_bed, part.bed_idx + 1);
            }
            result[accepted[j]] = std::move(part);
        }
        first_bed = next_bed;
        first_order += static_cast<int>(layout.size());
    }
    items = std::move(result);
    return true;
}

} // namespace Slic3r

#endif
