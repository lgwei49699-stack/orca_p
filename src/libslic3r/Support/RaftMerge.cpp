#include "RaftMerge.hpp"
#include "RaftGeometry.hpp"

#include "../ClipperUtils.hpp"
#include "../Fill/FillRectilinear.hpp"
#include "../GCode/ConflictChecker.hpp"
#include "../I18N.hpp"
#include "../Print.hpp"
#include "SupportCommon.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <tuple>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {
namespace {

// Arithmetic/float conversion tolerance, not a tolerance for different presets.
bool same_value(double a, double b) { return std::abs(a - b) <= 1e-6 * std::max({1., std::abs(a), std::abs(b)}); }

struct PlacedRaft
{
    PrintObject*                       object;
    Point                              shift;
    std::vector<SupportLayer*>         layers;
    std::vector<ExPolygons>            outlines;
    std::unique_ptr<SupportParameters> parameters;
    BoundingBox                        bounds;
};

bool overlap_in_z(const Layer& a, const Layer& b)
{
    return std::min(a.print_z, b.print_z) > std::max(a.bottom_z(), b.bottom_z()) + EPSILON;
}

bool overlaps(const PlacedRaft& a, const PlacedRaft& b)
{
    if (!a.bounds.overlap(b.bounds))
        return false;
    for (size_t i = 0; i < a.layers.size(); ++i)
        for (size_t j = 0; j < b.layers.size(); ++j)
            if (overlap_in_z(*a.layers[i], *b.layers[j]) && !intersection_ex(a.outlines[i], b.outlines[j]).empty())
                return true;
    return false;
}

struct RaftSlice
{
    PlacedRaft*   raft;
    size_t        index;
    size_t        geometry_index = size_t(-1);
    SupportLayer& layer() const { return *raft->layers[index]; }
    size_t        plan_index() const { return geometry_index == size_t(-1) ? index : geometry_index; }
    RaftPhase     phase() const { return raft->object->slicing_parameters().raft_phase(plan_index()); }
};

void append_source_paths(const ExtrusionEntity* entity,
                         ExtrusionPaths&        paths,
                         double                 print_z      = 0.,
                         double                 max_bottom_z = std::numeric_limits<double>::infinity(),
                         bool*                  filtered     = nullptr)
{
    const auto append = [&](const ExtrusionPath& path) {
        if (print_z - double(path.height) <= max_bottom_z + EPSILON)
            paths.push_back(path);
        else if (filtered != nullptr)
            *filtered = true;
    };
    if (const auto* collection = dynamic_cast<const ExtrusionEntityCollection*>(entity))
        for (const auto* child : collection->entities)
            append_source_paths(child, paths, print_z, max_bottom_z, filtered);
    else if (const auto* path = dynamic_cast<const ExtrusionPath*>(entity))
        append(*path);
    else if (const auto* loop = dynamic_cast<const ExtrusionLoop*>(entity))
        for (const auto& path : loop->paths)
            append(path);
    else if (const auto* multi = dynamic_cast<const ExtrusionMultiPath*>(entity))
        for (const auto& path : multi->paths)
            append(path);
    else
        throw RuntimeError("Unexpected raft source extrusion type.");
}

ExtrusionEntityCollection source_paths(const RaftSlice& slice, bool raft_only)
{
    const SupportLayer& layer = slice.layer();
    if (!layer.has_raft_provenance())
        return raft_only ? layer.support_fills : ExtrusionEntityCollection{};
    ExtrusionEntityCollection paths;
    paths.no_sort = layer.support_fills.no_sort;
    for (size_t i = 0; i < layer.support_fills.entities.size(); ++i)
        if (bool(layer.raft_entity_flags[i]) == raft_only)
            paths.append(*layer.support_fills.entities[i]);
    return paths;
}

// Some generators also put ordinary support columns on raft layers. Preserve
// their original paths instead of regenerating the whole layer with raft flow.
bool can_regenerate(const RaftSlice& slice, bool raft_only = false)
{
    if (!slice.raft->parameters || slice.layer().raft_outline.empty() ||
        slice.plan_index() >= slice.raft->object->slicing_parameters().raft_layers() ||
        !same_value(slice.layer().print_z, slice.raft->object->slicing_parameters().raft_layer_print_z(slice.plan_index())))
        return false;
    const auto&    params = *slice.raft->parameters;
    const Flow&    flow   = params.raft_flow(slice.phase());
    const auto     role   = slice.phase() == RaftPhase::Base ? erSupportMaterial : erSupportMaterialInterface;
    ExtrusionPaths paths;
    if (raft_only && slice.layer().has_raft_provenance()) {
        for (size_t i = 0; i < slice.layer().support_fills.entities.size(); ++i)
            if (slice.layer().raft_entity_flags[i])
                append_source_paths(slice.layer().support_fills.entities[i], paths);
    } else
        getExtrusionPathsFromEntity(&slice.layer().support_fills, paths);
    return !paths.empty() && std::all_of(paths.begin(), paths.end(), [&](const ExtrusionPath& path) {
        return path.role() == role && same_value(path.width, flow.width()) && same_value(path.height, flow.height()) &&
               same_value(path.mm3_per_mm, flow.mm3_per_mm() * params.raft_flow_ratio(slice.phase()));
    });
}

// Automatic support material means the active tool. With more than one possible
// tool keep each object's paths/material assignment, including purge overrides.
int raft_extruder(const RaftSlice& slice)
{
    const auto& config     = slice.raft->object->config();
    const int   configured = slice.phase() == RaftPhase::Base ? config.support_filament.value : config.support_interface_filament.value;
    if (configured > 0)
        return configured - 1;
    const auto extruders = slice.raft->object->print()->extruders();
    return extruders.size() == 1 ? int(extruders.front()) : -1;
}

bool same_recipe(const RaftSlice& a, const RaftSlice& b, bool raft_only = false)
{
    if (!can_regenerate(a, raft_only) || !can_regenerate(b, raft_only) || a.phase() != b.phase() ||
        !same_value(a.layer().print_z, b.layer().print_z) || !same_value(a.layer().height, b.layer().height))
        return false;
    const int tool = raft_extruder(a);
    if (tool < 0 || tool != raft_extruder(b))
        return false;
    const auto&  pa    = *a.raft->parameters;
    const auto&  pb    = *b.raft->parameters;
    const auto   phase = a.phase();
    const Flow&  fa    = pa.raft_flow(phase);
    const Flow&  fb    = pb.raft_flow(phase);
    const double delta = std::abs(double(pa.raft_layer_angle(a.plan_index())) - double(pb.raft_layer_angle(b.plan_index())));
    if (!same_value(fa.nozzle_diameter(), fb.nozzle_diameter()) || !same_value(fa.width(), fb.width()) ||
        !same_value(fa.height(), fb.height()) || !same_value(pa.raft_density(phase), pb.raft_density(phase)) ||
        !same_value(pa.raft_pattern_spacing(phase), pb.raft_pattern_spacing(phase)) ||
        !same_value(pa.raft_flow_ratio(phase), pb.raft_flow_ratio(phase)) || pa.raft_wall_count(phase) != pb.raft_wall_count(phase) ||
        pa.raft_fill_pattern(phase) != pb.raft_fill_pattern(phase) || std::min(delta, std::abs(PI - delta)) > 1e-6)
        return false;
    const std::string prefix = phase == RaftPhase::Base      ? "raft_base_" :
                               phase == RaftPhase::Interface ? "raft_interface_" :
                                                               "raft_surface_";
    const auto&       ca     = a.raft->object->config();
    const auto&       cb     = b.raft->object->config();
    for (const char* suffix : {"speed", "acceleration", "fan_speed"}) {
        const std::string key = prefix + suffix;
        if (ca.opt_serialize(key) != cb.opt_serialize(key))
            return false;
    }
    // Conservative grouping for motion fallbacks. Differing values partition
    // the overlap without changing either object's settings or raising a warning.
    for (const char* key : {"default_acceleration", "initial_layer_acceleration", "default_jerk", "initial_layer_jerk"})
        if (ca.opt_serialize(key) != cb.opt_serialize(key))
            return false;
    return true;
}

void translate_paths(ExtrusionEntityCollection& paths, const Point& shift)
{
    for (ExtrusionEntity* entity : paths.entities) {
        if (auto* nested = dynamic_cast<ExtrusionEntityCollection*>(entity))
            translate_paths(*nested, shift);
        else if (auto* path = dynamic_cast<ExtrusionPath*>(entity))
            path->polyline.translate(shift);
        else if (auto* loop = dynamic_cast<ExtrusionLoop*>(entity)) {
            for (auto& path : loop->paths)
                path.polyline.translate(shift);
        } else if (auto* multipath = dynamic_cast<ExtrusionMultiPath*>(entity)) {
            for (auto& path : multipath->paths)
                path.polyline.translate(shift);
        } else
            throw RuntimeError("Unexpected shared raft extrusion type.");
    }
}

struct MergedLayer
{
    std::vector<RaftSlice>    sources;
    ExtrusionEntityCollection paths;
    ExPolygons                areas;
    bool                      preserve_support = false;
};

// Keep a finished chunk atomic for G-code's recursive support-path ordering.
// Classify only paths already clipped to the disjoint solid regions, so a leaf
// cannot cross between islands. Preserve homogeneous nested collections (walls,
// loops and clipped runs), splitting only collection ownership, never geometry.
bool group_raft_paths_by_island(ExtrusionEntityCollection& paths, const ExPolygons& islands, const std::function<void()>& cancel)
{
    if (islands.size() < 2 || paths.empty())
        return true;
    std::vector<BoundingBox> bounds;
    for (const ExPolygon& island : islands)
        bounds.push_back(get_extents(island));
    const size_t                                        unassigned = islands.size();
    const std::function<size_t(const ExtrusionEntity&)> owner      = [&](const ExtrusionEntity& entity) -> size_t {
        cancel();
        if (const auto* nested = dynamic_cast<const ExtrusionEntityCollection*>(&entity)) {
            size_t common = unassigned;
            for (const ExtrusionEntity* child : nested->entities) {
                const size_t child_owner = owner(*child);
                if (child_owner == unassigned || (common != unassigned && common != child_owner))
                    return unassigned;
                common = child_owner;
            }
            return common;
        }
        const Point& point = entity.first_point();
        for (size_t i = 0; i < islands.size(); ++i)
            if (bounds[i].contains(point) && islands[i].contains(point))
                return i;
        return unassigned;
    };
    std::vector<ExtrusionEntityCollection>            groups(islands.size());
    const std::function<bool(const ExtrusionEntity&)> append = [&](const ExtrusionEntity& entity) {
        const size_t index = owner(entity);
        if (index != unassigned) {
            groups[index].append(entity);
            return true;
        }
        if (const auto* nested = dynamic_cast<const ExtrusionEntityCollection*>(&entity))
            return std::all_of(nested->entities.begin(), nested->entities.end(),
                               [&](const ExtrusionEntity* child) { return append(*child); });
        return false;
    };
    for (const ExtrusionEntity* entity : paths.entities)
        if (!append(*entity))
            return false; // Leave the complete original collection intact.
    ExtrusionEntityCollection grouped;
    for (ExtrusionEntityCollection& group : groups)
        if (!group.empty()) {
            // The exporter chooses the nearest paths inside each group. Do not
            // reverse an entire group while choosing the next chunk to visit.
            group.no_sort = true;
            grouped.append(std::move(group));
        }
    paths.swap(grouped);
    return true;
}

void generate_shared_paths(MergedLayer& result, const BoundingBox& bounds, const Polygons& bed, const ExPolygons* uncut_areas = nullptr)
{
    const auto& source  = result.sources.front();
    const auto& params  = *source.raft->parameters;
    const Flow& flow    = params.raft_flow(source.phase());
    const auto  role    = source.phase() == RaftPhase::Base ? erSupportMaterial : erSupportMaterialInterface;
    const auto  pattern = params.raft_fill_pattern(source.phase());
    // Consistent-pattern fillers (ZigZag) extend their bounding box around the
    // coordinate origin before rotation. Generate in a centered group frame;
    // plate coordinates can otherwise leave the hatch outside the entire raft.
    const Point fill_origin = bounds.center();
    BoundingBox fill_bounds = bounds;
    fill_bounds.translate(-fill_origin);
    ExPolygons local_areas = result.areas;
    translate(local_areas, -fill_origin);
    std::unique_ptr<Fill> filler(Fill::new_from_type(pattern));
    filler->set_bounding_box(fill_bounds);
    filler->angle               = params.raft_layer_angle(source.plan_index());
    filler->spacing         = params.raft_pattern_spacing(source.phase());
    filler->link_max_length = 0;
    const size_t     walls      = params.raft_wall_count(source.phase());
    ExPolygons       fill_areas;
    RaftPathRegions  path_regions;
    if (uncut_areas != nullptr) {
        ExPolygons local_uncut = *uncut_areas;
        translate(local_uncut, -fill_origin);
        path_regions = prepare_raft_path_regions(local_areas, local_uncut, flow.width(), [&source] {
            if (source.raft->object->print()->canceled())
                throw CanceledException();
        });
        fill_areas   = std::move(path_regions.fill_regions);
        if (walls == 0 && pattern == ipZigZag) {
            // There are no walls to overlap here. Both the turn contour AND
            // the outer hatch contour must fit inside the final half-width +
            // 0.001 mm clip. Otherwise a hatch chord may cross a concave notch
            // in the centerline region and get split by the safety clip.
            // Leave 0.001 mm between contours and another micron for rounding;
            // preserve the requested hatch spacing, density and phase.
            filler->overlap = std::min(0., 0.5 * filler->spacing - (0.5 * double(flow.width()) + 0.003));
            static_cast<FillZigZag*>(filler.get())->max_endpoint_overlap = 0.001;
        }
        if (path_regions.omitted_regions > 0)
            BOOST_LOG_TRIVIAL(info) << "[Raft][Geometry] omitted collapsed path regions=" << path_regions.omitted_regions
                                    << ", layer=" << source.index;
    } else {
        // Preserve the established merger's sheath preparation when the trial
        // is off. Only trial paths use explicitly prepared centerline regions.
        fill_areas = walls == 0 ? local_areas : offset_ex(local_areas, -scale_(0.5 * flow.width() + 0.001));
    }
    fill_expolygons_with_sheath_generate_paths(result.paths.entities, to_polygons(fill_areas), filler.get(),
                                               float(params.raft_density(source.phase())), role, flow, params, walls, true,
                                               params.raft_flow_ratio(source.phase()), pattern,
                                               uncut_areas != nullptr ? &path_regions.wall_centerlines : nullptr);
    const size_t generated_entities = result.paths.entities.size();
    translate_paths(result.paths, fill_origin);
    clip_raft_paths_to_area(result.paths, intersection(to_polygons(result.areas), bed), uncut_areas != nullptr);
    if (uncut_areas != nullptr && source.raft->object->config().raft_segmented.value &&
        !group_raft_paths_by_island(result.paths, result.areas, [&source] {
            if (source.raft->object->print()->canceled())
                throw CanceledException();
        }))
        BOOST_LOG_TRIVIAL(warning) << "[Raft][Geometry] path grouping skipped: unknown island, layer=" << source.plan_index();
    if (uncut_areas != nullptr && result.paths.empty())
        BOOST_LOG_TRIVIAL(info) << "[Raft][Geometry] empty candidate, layer=" << source.plan_index() << ", z=" << source.layer().print_z
                                << ", height=" << flow.height() << ", width=" << flow.width() << ", regions=" << result.areas.size()
                                << ", fill_regions=" << fill_areas.size() << ", centerline_regions=" << path_regions.wall_centerlines.size()
                                << ", generated_entities=" << generated_entities << ", clipped_entities=0";
}

bool geometry_requested(const PrintObjectConfig& config)
{
    return config.raft_mode.value == RaftMode::CuraV1 && (config.raft_segmented.value || config.raft_smoothing.value > 0.);
}

bool same_geometry_options(const PrintObjectConfig& a, const PrintObjectConfig& b)
{
    return a.raft_segmented.value == b.raft_segmented.value && same_value(a.raft_smoothing.value, b.raft_smoothing.value) &&
           (!a.raft_segmented.value ||
            (same_value(a.raft_chunk_size.value, b.raft_chunk_size.value) && same_value(a.raft_chunk_gap.value, b.raft_chunk_gap.value)));
}

// Keep the existing merger as the baseline. Only replace a complete compatible
// group's physical layers, never partial layers or unrelated ordinary supports.
bool apply_raft_geometry(Print&                          print,
                         std::vector<PlacedRaft>&        placed,
                         const std::vector<size_t>&      members,
                         std::vector<MergedLayer>&       batches,
                         const std::vector<MergedLayer>& pending,
                         const BoundingBox&              bounds,
                         const Polygons&                 bed)
{
    if (std::none_of(members.begin(), members.end(), [&](size_t i) { return geometry_requested(placed[i].object->config()); }))
        return false;
    const auto started    = std::chrono::steady_clock::now();
    const auto elapsed_ms = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); };
    const auto skip       = [&](const char* reason) {
        BOOST_LOG_TRIVIAL(info) << "[Raft][Geometry] status=unchanged, reason=" << reason << ", objects=" << members.size()
                                << ", elapsed_ms=" << elapsed_ms();
        return false;
    };
    const auto check_cancel = [&print] {
        if (print.canceled())
            throw CanceledException();
    };
    const PlacedRaft& reference = placed[members.front()];
    const auto&       config    = reference.object->config();
    const bool separate_support = std::any_of(members.begin(), members.end(), [&](size_t i) { return placed[i].object->has_support(); });
    if (print.extruders().size() != 1)
        return skip("incompatible tool plans");
    for (size_t member : members) {
        const auto& raft = placed[member];
        if (!raft.parameters || !same_geometry_options(config, raft.object->config()) ||
            raft.object->slicing_parameters().raft_layers() != reference.object->slicing_parameters().raft_layers())
            return skip("unsupported source or different geometry settings");
    }
    // Rebuild the enhancement input from explicitly identified raft entities.
    // Ordinary supports can have additional physical layers inside the raft Z
    // range; those layers stay with their object and never become raft batches.
    std::vector<MergedLayer> raft_batches;
    if (separate_support) {
        for (size_t i = 0; i < reference.object->slicing_parameters().raft_layers(); ++i) {
            check_cancel();
            raft_batches.emplace_back();
            auto& batch = raft_batches.back();
            for (size_t member : members) {
                auto*        raft = &placed[member];
                const double z    = raft->object->slicing_parameters().raft_layer_print_z(i);
                const auto   it   = std::find_if(raft->layers.begin(), raft->layers.end(),
                                                 [&](const SupportLayer* layer) { return same_value(layer->print_z, z); });
                if (it == raft->layers.end() || !(*it)->has_raft_provenance() || (*it)->raft_geometry_outline.empty())
                    return skip("missing raft path provenance; regenerate support data");
                const RaftSlice source{raft, size_t(it - raft->layers.begin()), i};
                if (!can_regenerate(source, true) || (!batch.sources.empty() && !same_recipe(batch.sources.front(), source, true)))
                    return skip("incompatible raft layer recipe");
                batch.sources.push_back(source);
                ExPolygons outline = source.layer().raft_geometry_outline;
                translate(outline, raft->shift);
                batch.areas.insert(batch.areas.end(), outline.begin(), outline.end());
            }
            batch.areas = intersection_ex(union_ex(batch.areas), bed);
        }
    }
    const auto& input_batches = separate_support ? raft_batches : batches;
    if (input_batches.size() != reference.object->slicing_parameters().raft_layers())
        return skip("incompatible layer plans");
    std::vector<ExPolygons> outlines;
    std::vector<double>     phase_insets;
    for (size_t i = 0; i < input_batches.size(); ++i) {
        const auto& batch = input_batches[i];
        if (batch.areas.empty() || batch.sources.size() != members.size())
            return skip("incomplete raft layer");
        ExPolygons outline;
        for (const RaftSlice& source : batch.sources) {
            if (source.plan_index() != i || !can_regenerate(source, separate_support) ||
                !same_recipe(batch.sources.front(), source, separate_support))
                return skip("mixed raft paths");
            ExPolygons local = separate_support ? source.layer().raft_geometry_outline : source.layer().raft_outline;
            translate(local, source.raft->shift);
            outline.insert(outline.end(), std::make_move_iterator(local.begin()), std::make_move_iterator(local.end()));
        }
        outlines.push_back(intersection_ex(union_ex(outline), bed));
        const RaftPhase phase = batch.sources.front().phase();
        const double    width = reference.parameters->raft_flow(phase).width();
        phase_insets.push_back(phase == RaftPhase::Base ? 0. : 0.5 * width);
    }
    const RaftGeometryOptions options{config.raft_segmented.value, config.raft_chunk_size.value, config.raft_chunk_gap.value,
                                      config.raft_smoothing.value};
    const double              cleanup_width = print.brim_flow().width();
    BOOST_LOG_TRIVIAL(info) << "[Raft][Geometry] options, objects=" << members.size() << ", segmented=" << options.segmented
                            << ", chunk_size=" << options.chunk_size << ", chunk_gap=" << options.chunk_gap
                            << ", smoothing=" << options.smoothing << ", support_provenance=" << separate_support;
    RaftGeometryPlan          plan          = plan_raft_geometry(outlines, phase_insets, bed, options, cleanup_width, check_cancel);
    if (!plan.changed || plan.uncut_layers.size() != input_batches.size())
        return skip("no geometry change");
    const double planning_ms = elapsed_ms();
    ExPolygons   support_demand;
    if (separate_support) {
        struct Occupancy
        {
            const PrintObject* owner;
            double             bottom_z;
            double             print_z;
            BoundingBox        bounds;
            ExPolygons         areas;
        };
        std::vector<Occupancy> supports;
        size_t                 mixed_height_layers = 0;
        const double           raft_top            = input_batches.back().sources.front().layer().print_z;
        for (size_t member : members) {
            const auto& raft = placed[member];
            for (const SupportLayer* layer : raft.object->support_layers()) {
                check_cancel();
                ExtrusionPaths paths;
                bool           filtered = false;
                if (layer->has_raft_provenance()) {
                    for (size_t e = 0; e < layer->support_fills.entities.size(); ++e)
                        if (!layer->raft_entity_flags[e])
                            append_source_paths(layer->support_fills.entities[e], paths, layer->print_z, raft_top, &filtered);
                } else
                    append_source_paths(&layer->support_fills, paths, layer->print_z, raft_top, &filtered);
                if (paths.empty())
                    continue;
                // Contact modulation may leave different extrusion heights at
                // one print Z. A thin path resting ON the raft must not inherit
                // a neighbouring column's deeper bottom Z and erase the raft.
                std::map<float, ExtrusionPaths> by_height;
                for (auto& path : paths)
                    by_height[path.height].push_back(std::move(path));
                const bool  uniform_height = by_height.size() == 1 && !filtered;
                const auto& outline        = layer->has_raft_provenance() ? layer->raft_support_outline : layer->support_islands;
                for (const auto& entry : by_height) {
                    check_cancel();
                    const double bottom_z = layer->print_z - double(entry.first);
                    ExPolygons   areas    = raft_extrusion_coverage(entry.second, check_cancel);
                    // A layer-wide envelope has no per-height provenance. Keep
                    // it as a collision mask only for a single-height layer;
                    // otherwise reserve each height group's actual beads.
                    if (uniform_height)
                        areas = union_ex(outline, areas);
                    translate(areas, raft.shift);
                    support_demand.insert(support_demand.end(), areas.begin(), areas.end());
                    const BoundingBox area_bounds = get_extents(areas);
                    supports.push_back({raft.object, bottom_z, layer->print_z, area_bounds, std::move(areas)});
                }
                if (!uniform_height) {
                    ++mixed_height_layers;
                    // The whole envelope remains conservative bearing demand,
                    // so sparse support feet never lose their base raft islands.
                    ExPolygons demand = outline;
                    translate(demand, raft.shift);
                    support_demand.insert(support_demand.end(), demand.begin(), demand.end());
                }
            }
        }
        BOOST_LOG_TRIVIAL(info) << "[Raft][Geometry] support occupancy, groups=" << supports.size()
                                << ", mixed_height_layers=" << mixed_height_layers;
        // The established merger may partition conflicting support columns.
        // Do not undo that protection by restoring both owners' original paths.
        for (size_t i = 0; i < supports.size(); ++i) {
            check_cancel();
            for (size_t j = i + 1; j < supports.size(); ++j) {
                check_cancel();
                if (supports[i].owner != supports[j].owner && supports[i].bounds.overlap(supports[j].bounds) &&
                    std::min(supports[i].print_z, supports[j].print_z) > std::max(supports[i].bottom_z, supports[j].bottom_z) + EPSILON &&
                    !intersection_ex(supports[i].areas, supports[j].areas).empty())
                    return skip("ordinary supports from different objects overlap");
            }
        }
        for (size_t i = 0; i < input_batches.size(); ++i) {
            check_cancel();
            const auto&  source = input_batches[i].sources.front();
            const double top    = source.layer().print_z;
            const double bottom = top - reference.parameters->raft_flow(source.phase()).height();
            ExPolygons   occupied;
            for (const auto& support : supports)
                if (std::min(top, support.print_z) > std::max(bottom, support.bottom_z) + EPSILON)
                    occupied.insert(occupied.end(), support.areas.begin(), support.areas.end());
            if (!occupied.empty()) {
                // Reserve same-Z columns, including their actual bead width.
                // Feet ABOVE the raft are bearing demand, not a veto on cuts.
                occupied             = offset_ex(union_ex(occupied), scale_(0.002));
                plan.layers[i]       = diff_ex(plan.layers[i], occupied);
                plan.uncut_layers[i] = diff_ex(plan.uncut_layers[i], occupied);
            }
        }
    }
    const double support_ms = elapsed_ms() - planning_ms;
    for (size_t i = 0; i < plan.layers.size(); ++i) {
        const auto& source = input_batches[i].sources.front();
        const auto& params = *source.raft->parameters;
        const Flow& flow   = params.raft_flow(source.phase());
        BOOST_LOG_TRIVIAL(info) << "[Raft][Geometry] phase planning, layer=" << i << ", cuts=" << plan.layer_cuts[i]
                                << ", islands=" << plan.layers[i].size() << ", z=" << source.layer().print_z << ", height=" << flow.height()
                                << ", width=" << flow.width() << ", spacing=" << params.raft_pattern_spacing(source.phase())
                                << ", flow_ratio=" << params.raft_flow_ratio(source.phase());
    }
    // Smoothing may add material. Do not grow into another object's model or
    // support footprint, including raft instances excluded from this merger.
    ExPolygons added;
    for (size_t i = 0; i < input_batches.size(); ++i) {
        ExPolygons growth = diff_ex(plan.layers[i], input_batches[i].areas);
        added.insert(added.end(), growth.begin(), growth.end());
    }
    if (!added.empty()) {
        added              = union_ex(added);
        const double top_z = input_batches.back().sources.front().layer().print_z;
        // Earlier groups may already have smoothing candidates. Checking only
        // their original live paths would miss two groups growing into the same gap.
        for (const MergedLayer& previous : pending) {
            check_cancel();
            if (!intersection_ex(added, previous.areas).empty())
                return skip("smoothing reaches another planned raft");
        }
        for (const PrintObject* other : print.objects()) {
            check_cancel();
            if (std::any_of(members.begin(), members.end(), [&](size_t i) { return placed[i].object == other; }))
                continue;
            Polygons obstacles;
            for (const Layer* layer : other->layers()) {
                if (layer->bottom_z() >= top_z - EPSILON)
                    break;
                polygons_append(obstacles, to_polygons(layer->lslices));
                for (const LayerRegion* region : layer->regions()) {
                    polygons_append(obstacles, region->perimeters.polygons_covered_by_width());
                    polygons_append(obstacles, region->fills.polygons_covered_by_width());
                }
            }
            for (const SupportLayer* layer : other->support_layers()) {
                if (layer->bottom_z() < top_z - EPSILON) {
                    polygons_append(obstacles, to_polygons(layer->raft_outline));
                    polygons_append(obstacles, layer->support_fills.polygons_covered_by_width());
                }
            }
            for (const PrintInstance& instance : other->instances()) {
                Polygons shifted = obstacles;
                for (Polygon& polygon : shifted)
                    polygon.translate(instance.shift);
                if (!intersection_ex(added, shifted).empty())
                    return skip("smoothing reaches another object");
            }
        }
    }

    std::vector<MergedLayer> candidates;
    std::vector<ExPolygons>  materials(input_batches.size());
    size_t                   omitted_fragments = 0;
    const auto               validate_candidate = [&](MergedLayer& candidate, ExPolygons& material) {
        ExtrusionPaths paths;
        getExtrusionPathsFromEntity(&candidate.paths, paths);
        material = raft_extrusion_coverage(paths, check_cancel);
        // Geometry fragments without extrusion are not printable islands. Leave
        // them out of output metadata instead of undoing every valid cut in the
        // group. The generator, not an unrelated global size veto, decides this.
        const size_t before = candidate.areas.size();
        candidate.areas.erase(std::remove_if(candidate.areas.begin(), candidate.areas.end(),
                                             [&](const ExPolygon& island) { return intersection_ex(ExPolygons{island}, material).empty(); }),
                              candidate.areas.end());
        omitted_fragments += before - candidate.areas.size();
        // The helper clips by half the actual line width. Check the swept bead,
        // not square-join coverage whose artificial corners can protrude even
        // when every circular bead lies inside. Keep the existing 2 micron
        // numerical allowance and the actual bed/gap boundaries unchanged.
        return !material.empty() && diff_ex(material, offset_ex(candidate.areas, scale_(0.002))).empty();
    };
    candidates.reserve(input_batches.size());
    for (size_t i = 0; i < input_batches.size(); ++i) {
        check_cancel();
        candidates.emplace_back();
        auto& candidate   = candidates.back();
        candidate.sources          = input_batches[i].sources;
        candidate.preserve_support = separate_support;
        candidate.areas   = std::move(plan.layers[i]);
        generate_shared_paths(candidate, bounds, bed, &plan.uncut_layers[i]);
        if (candidate.paths.empty())
            return skip("no printable paths after geometry change");
        if (!validate_candidate(candidate, materials[i]))
            return skip("extrusions leave the assigned region");
    }

    size_t base_layers = 0;
    while (base_layers < candidates.size() && candidates[base_layers].sources.front().phase() == RaftPhase::Base)
        ++base_layers;
    size_t omitted_base_regions = 0;
    // Only segmented base stacks with known upper raft/model data qualify.
    // Smoothing alone, a base-only raft and incomplete caches stay unchanged.
    if (options.segmented && base_layers > 0 && base_layers < candidates.size() &&
        std::any_of(plan.layer_cuts.begin(), plan.layer_cuts.begin() + base_layers, [](size_t cuts) { return cuts > 0; }) &&
        std::all_of(members.begin(), members.end(), [&](size_t member) { return !placed[member].object->layers().empty(); })) {
        ExPolygons base, uncut_base, demand = support_demand;
        const auto append = [](ExPolygons& to, const ExPolygons& from) { to.insert(to.end(), from.begin(), from.end()); };
        for (size_t i = 0; i < candidates.size(); ++i) {
            check_cancel();
            if (i < base_layers) {
                append(base, candidates[i].areas);
                append(base, materials[i]);
                append(uncut_base, plan.uncut_layers[i]);
            } else {
                // Solid envelopes protect sparse hatch gaps; actual beads also
                // protect paths which extend slightly past their nominal solid.
                append(demand, outlines[i]);
                append(demand, plan.uncut_layers[i]);
                append(demand, candidates[i].areas);
                append(demand, materials[i]);
            }
        }
        for (size_t member : members) {
            check_cancel();
            const PlacedRaft& raft  = placed[member];
            const Layer&      first = *raft.object->layers().front();
            ExPolygons        model = first.lslices;
            for (const LayerRegion* region : first.regions()) {
                append(model, union_ex(region->perimeters.polygons_covered_by_width()));
                append(model, union_ex(region->fills.polygons_covered_by_width()));
            }
            translate(model, raft.shift);
            append(demand, model);
        }
        const ExPolygons removed = find_unneeded_raft_base_regions(base, uncut_base, demand, check_cancel);
        omitted_base_regions     = removed.size();
        if (!removed.empty()) {
            for (size_t i = 0; i < base_layers; ++i) {
                check_cancel();
                auto&        candidate = candidates[i];
                const size_t before    = candidate.areas.size();
                // Remove whole solid sections, never individual short paths.
                // Regenerate both walls and infill using the same origin/recipe;
                // output metadata and emitted paths then describe the same area.
                candidate.areas.erase(std::remove_if(candidate.areas.begin(), candidate.areas.end(),
                                                     [&](const ExPolygon& area) { return diff_ex(ExPolygons{area}, removed).empty(); }),
                                      candidate.areas.end());
                if (candidate.areas.size() == before)
                    continue;
                if (candidate.areas.empty())
                    return skip("base cleanup would remove an entire layer");
                candidate.paths.clear();
                generate_shared_paths(candidate, bounds, bed, &plan.uncut_layers[i]);
                if (!validate_candidate(candidate, materials[i]))
                    return skip("invalid paths after base cleanup");
            }
        }
    }
    check_cancel();
    batches = std::move(candidates);
    BOOST_LOG_TRIVIAL(info) << "[Raft][Geometry] status=applied, objects=" << members.size() << ", omitted_fragments=" << omitted_fragments
                            << ", omitted_base_regions=" << omitted_base_regions << ", smoothing=" << options.smoothing
                            << ", preserved_support=" << separate_support << ", planning_ms=" << planning_ms
                            << ", support_mask_ms=" << support_ms << ", paths_validation_ms=" << elapsed_ms() - planning_ms - support_ms
                            << ", elapsed_ms=" << elapsed_ms();
    return true;
}

struct ClearanceBand
{
    double     bottom_z;
    double     print_z;
    ExPolygons areas;
};

// A support layer may contain paths of different heights. Using Layer::height
// (the minimum of those heights) would miss collisions underneath taller paths.
std::vector<ClearanceBand> extrusion_clearance_bands(const Print&                     print,
                                                     const ExtrusionEntityCollection& fills,
                                                     double                           print_z,
                                                     double                           fallback_height,
                                                     double                           below_z = std::numeric_limits<double>::max())
{
    ExtrusionPaths paths;
    getExtrusionPathsFromEntity(&fills, paths);
    std::map<double, Polygons> by_height;
    for (const ExtrusionPath& path : paths) {
        if (print.canceled())
            throw CanceledException();
        if (path.is_force_no_extrusion() || path.mm3_per_mm <= 0. || path.width <= 0.)
            continue;
        const double height = path.height > 0. ? path.height : fallback_height;
        if (height > 0. && print_z - height + EPSILON < below_z)
            path.polygons_covered_by_width(by_height[height], 0.f);
    }
    std::vector<ClearanceBand> bands;
    for (auto& [height, areas] : by_height)
        if (!areas.empty())
            bands.push_back({print_z - height, print_z, union_ex(areas)});
    return bands;
}

struct PlacedClearanceBand
{
    const PrintObject* owner;
    ClearanceBand      band;
    BoundingBox        bounds;
};

} // namespace

void Print::validate_raft_clearance() const
{
    if (m_config.print_sequence == PrintSequence::ByObject)
        return;
    const bool has_unrafted = std::any_of(m_objects.begin(), m_objects.end(),
                                          [](const PrintObject* object) { return !object->has_raft() && !object->instances().empty(); });
    if (!has_unrafted)
        return;

    std::vector<PlacedClearanceBand> rafts;
    double                           raft_top_z = 0.;
    for (const PrintObject* object : m_objects) {
        if (!object->has_raft() || object->instances().empty())
            continue;
        for (const SupportLayer* layer : object->support_layers()) {
            if (layer->print_z > object->slicing_parameters().raft_contact_top_z + EPSILON)
                continue;
            // Inspect the final emitted paths, not the merge backups or a solid
            // nominal outline: holes and gaps between sparse raft lines stay free.
            // This also covers ordinary supports sharing a physical raft layer.
            const auto bands = extrusion_clearance_bands(*this, layer->support_fills, layer->print_z, layer->height);
            for (const PrintInstance& instance : object->instances()) {
                for (const ClearanceBand& band : bands) {
                    ClearanceBand placed = band;
                    translate(placed.areas, instance.shift);
                    BoundingBox bounds = get_extents(placed.areas);
                    raft_top_z         = std::max(raft_top_z, placed.print_z);
                    rafts.push_back({object, std::move(placed), bounds});
                }
            }
        }
    }
    if (rafts.empty())
        return;

    const auto check = [&](const PrintObject& object, const ClearanceBand& band, const Point& shift) {
        if (band.areas.empty() || band.bottom_z + EPSILON >= raft_top_z)
            return;
        ExPolygons areas = band.areas;
        translate(areas, shift);
        const BoundingBox bounds = get_extents(areas);
        for (const PlacedClearanceBand& raft : rafts) {
            throw_if_canceled();
            if (std::min(band.print_z, raft.band.print_z) <= std::max(band.bottom_z, raft.band.bottom_z) + EPSILON ||
                !bounds.overlap(raft.bounds) || intersection_ex(areas, raft.band.areas).empty())
                continue;
            throw SlicingError((boost::format(_u8L("The raft for \"%1%\" overlaps \"%2%\", which has raft disabled. Move the objects "
                                                   "farther apart or enable a raft for \"%2%\".")) %
                                raft.owner->model_object()->name % object.model_object()->name)
                                   .str(),
                               object.model_object()->id().id);
        }
    };
    for (const PrintObject* object : m_objects) {
        if (object->has_raft() || object->instances().empty())
            continue;
        throw_if_canceled();
        const auto check_local = [&](const ClearanceBand& band) {
            for (const PrintInstance& instance : object->instances())
                check(*object, band, instance.shift);
        };
        for (const Layer* layer : object->layers()) {
            if (layer->bottom_z() + EPSILON >= raft_top_z)
                break;
            // Keep real model holes; an XY bounding box is not a model footprint.
            check_local({layer->bottom_z(), layer->print_z, layer->lslices});
            // Width/flow settings can put emitted walls outside the slice contour.
            for (const LayerRegion* region : layer->regions())
                for (const auto* fills : {&region->perimeters, &region->fills})
                    for (const ClearanceBand& band : extrusion_clearance_bands(*this, *fills, layer->print_z, layer->height, raft_top_z))
                        check_local(band);
        }
        for (const SupportLayer* layer : object->support_layers()) {
            for (const ClearanceBand& band :
                 extrusion_clearance_bands(*this, layer->support_fills, layer->print_z, layer->height, raft_top_z))
                check_local(band);
        }

        // Brim maps already contain every instance in plate/world coordinates.
        // They are emitted once at the owning model/support's first print Z.
        const auto check_brim = [&](const auto& map, const auto& layers) {
            const auto it = map.find(object->id());
            if (it == map.end() || layers.empty())
                return;
            const Layer* first = layers.front();
            for (const ClearanceBand& band : extrusion_clearance_bands(*this, it->second, first->print_z, first->height, raft_top_z))
                check(*object, band, Point(0, 0));
        };
        check_brim(m_brimMap, object->layers());
        check_brim(m_supportBrimMap, object->support_layers());

        // Per-object skirts belong to the unrafted object. Their stored paths
        // use first-layer flow, but G-code changes the height at each model layer.
        if (m_config.skirt_type == stPerObject && has_skirt() && !object->object_skirt().empty()) {
            const ExPolygons all_loops = union_ex(object->object_skirt().polygons_covered_by_width());
            const ExPolygons last_loop = m_config.single_loop_draft_shield ?
                                             union_ex(object->object_skirt().entities.back()->polygons_covered_by_width()) :
                                             all_loops;
            for (const Layer* layer : object->layers()) {
                if (layer->bottom_z() + EPSILON >= raft_top_z ||
                    (layer->id() >= size_t(m_config.skirt_height.value) && !has_infinite_skirt()))
                    break;
                const LayerTools& tools = m_tool_ordering.tools_for_layer(layer->print_z);
                if (tools.has_skirt && !tools.extruders.empty())
                    check_local({layer->bottom_z(), layer->print_z, layer->id() == 0 ? all_loops : last_loop});
            }
        }
    }
}

void restore_unmerged_rafts(Print& print)
{
    for (size_t i = 0; i < print.objects().size(); ++i) {
        PrintObject* object = print.get_object(i);
        for (SupportLayer* layer : object->support_layers())
            if (layer->raft_merge_backup) {
                layer->support_fills.swap(layer->raft_merge_backup->fills);
                layer->support_islands.swap(layer->raft_merge_backup->islands);
                layer->raft_entity_flags.swap(layer->raft_merge_backup->entity_flags);
                layer->raft_merge_backup.reset();
                layer->raft_covered_by_shared_paths = false;
            }
    }
}

void merge_overlapping_rafts(Print& print)
{
    restore_unmerged_rafts(print);
    if (print.config().print_sequence == PrintSequence::ByObject)
        return;
    std::vector<PlacedRaft> placed;
    placed.reserve(print.objects().size());
    for (size_t i = 0; i < print.objects().size(); ++i) {
        PrintObject* object = print.get_object(i);
        // Instance copies share one path collection; this pass targets independent
        // OBJ objects. Do not make their per-copy data diverge behind that API.
        if (!object->has_raft() || object->instances().size() != 1)
            continue;
        PlacedRaft item;
        item.object = object;
        item.shift  = object->instances().front().shift;
        for (SupportLayer* layer : object->support_layers()) {
            if (layer->print_z > object->slicing_parameters().raft_contact_top_z + EPSILON)
                continue;
            item.layers.push_back(layer);
            // Footprints also cover old caches without outline metadata and
            // account for the width of paths extending beyond a nominal outline.
            ExPolygons outline = union_ex(layer->raft_outline, layer->support_fills.polygons_covered_by_width());
            translate(outline, item.shift);
            item.bounds.merge(get_extents(outline));
            item.outlines.push_back(std::move(outline));
        }
        if (item.layers.empty())
            continue;
        if (object->slicing_parameters().cura_raft_mode)
            item.parameters = std::make_unique<SupportParameters>(*object);
        placed.push_back(std::move(item));
    }
    const bool requested = std::any_of(placed.begin(), placed.end(),
                                       [](const PlacedRaft& raft) { return geometry_requested(raft.object->config()); });
    if (placed.size() < 2 && !requested)
        return;

    // Stable geometric ownership, independent of incoming OBJ order.
    std::sort(placed.begin(), placed.end(), [](const PlacedRaft& a, const PlacedRaft& b) {
        return std::make_tuple(a.bounds.min.x(), a.bounds.min.y(), a.bounds.max.x(), a.bounds.max.y(), a.object->id().id) <
               std::make_tuple(b.bounds.min.x(), b.bounds.min.y(), b.bounds.max.x(), b.bounds.max.y(), b.object->id().id);
    });
    std::vector<size_t> parent(placed.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&parent](size_t i) {
        while (parent[i] != i)
            i = parent[i];
        return i;
    };
    for (size_t i = 0; i < placed.size(); ++i) {
        if (print.canceled())
            throw CanceledException();
        for (size_t j = i + 1; j < placed.size(); ++j)
            if (overlaps(placed[i], placed[j]))
                parent[root(j)] = root(i);
    }
    std::map<size_t, std::vector<size_t>> groups;
    for (size_t i = 0; i < placed.size(); ++i)
        groups[root(i)].push_back(i);
    const Vec3d origin = print.get_plate_origin();
    Polygons    bed    = diff(Polygons{Polygon(get_bed_shape(print.config()))}, get_bed_excluded_area(print.config()));
    for (auto& polygon : bed)
        polygon.translate(Point::new_scale(origin.x(), origin.y()));

    std::vector<MergedLayer> pending;
    size_t                   group_count = 0;
    for (const auto& [id, members] : groups) {
        if (members.size() < 2 && !geometry_requested(placed[members.front()].object->config()))
            continue;
        std::vector<MergedLayer> batches;
        BoundingBox              bounds;
        for (size_t i : members) {
            bounds.merge(placed[i].bounds);
            for (size_t layer_index = 0; layer_index < placed[i].layers.size(); ++layer_index) {
                const RaftSlice source{&placed[i], layer_index};
                auto            batch = std::find_if(batches.begin(), batches.end(),
                                                     [&](const MergedLayer& candidate) { return same_recipe(candidate.sources.front(), source); });
                if (batch == batches.end()) {
                    batches.emplace_back();
                    batch = std::prev(batches.end());
                }
                batch->sources.push_back(source);
                batch->areas.insert(batch->areas.end(), placed[i].outlines[layer_index].begin(), placed[i].outlines[layer_index].end());
            }
        }
        // Keep different physical layer plans intact. Lower physical layers get
        // priority if the Z intervals overlap, without inventing new layer heights.
        std::stable_sort(batches.begin(), batches.end(), [](const MergedLayer& a, const MergedLayer& b) {
            return a.sources.front().layer().print_z < b.sources.front().layer().print_z;
        });
        for (size_t i = 0; i < batches.size(); ++i) {
            if (print.canceled())
                throw CanceledException();
            auto& batch      = batches[i];
            batch.areas      = intersection_ex(union_ex(batch.areas), bed);
            Polygons allowed = bed;
            for (size_t j = 0; j < i; ++j)
                if (overlap_in_z(batch.sources.front().layer(), batches[j].sources.front().layer())) {
                    batch.areas = diff_ex(batch.areas, batches[j].areas);
                    allowed     = diff(allowed, to_polygons(batches[j].areas));
                }
            if (batch.areas.empty())
                continue;
            if (batch.sources.size() > 1)
                generate_shared_paths(batch, bounds, bed);
            else {
                // Different recipes, Legacy, old caches or mixed ordinary
                // support: preserve paths and trim only the other region. Do not
                // inset a path's own footprint, which would erase narrow ribbons.
                const auto& source = batch.sources.front();
                batch.paths        = source.layer().support_fills;
                translate_paths(batch.paths, source.raft->shift);
                clip_raft_paths_to_area(batch.paths, allowed);
            }
        }
        const bool enhanced = apply_raft_geometry(print, placed, members, batches, pending, bounds, bed);
        if (members.size() == 1 && !enhanced)
            continue;
        for (auto& batch : batches)
            pending.push_back(std::move(batch));
        ++group_count;
    }
    if (pending.empty())
        return;
    if (print.canceled())
        throw CanceledException();

    // Stage ownership and allocations before touching live layers. Cancellation
    // during geometry work leaves every object's original support paths intact.
    struct StagedLayer
    {
        SupportLayer*                                  layer;
        ExtrusionEntityCollection                      fills;
        ExPolygons                                     islands;
        std::vector<unsigned char>                     flags;
        std::unique_ptr<SupportLayer::RaftMergeBackup> backup;
        bool                                           covered = false;
    };
    std::vector<StagedLayer> staged;
    for (auto& batch : pending) {
        const bool supplied = !batch.paths.empty() || batch.areas.empty();
        for (size_t i = 0; i < batch.sources.size(); ++i) {
            if (print.canceled())
                throw CanceledException();
            const RaftSlice& source = batch.sources[i];
            staged.emplace_back();
            auto& item                = staged.back();
            item.layer                = &source.layer();
            item.backup               = std::make_unique<SupportLayer::RaftMergeBackup>();
            item.backup->islands      = item.layer->support_islands;
            item.backup->entity_flags = item.layer->raft_entity_flags;
            if (batch.preserve_support) {
                item.fills = source_paths(source, false);
                item.flags.resize(item.fills.entities.size(), 0);
                if (!item.fills.empty())
                    item.islands = union_ex(item.layer->raft_support_outline, item.fills.polygons_covered_by_width());
            }
            if (i == 0) {
                translate_paths(batch.paths, -source.raft->shift);
                translate(batch.areas, -source.raft->shift);
                if (batch.preserve_support) {
                    item.islands = union_ex(item.islands, batch.areas);
                    item.fills.append(std::move(batch.paths.entities));
                    item.flags.resize(item.fills.entities.size(), 1);
                } else {
                    item.islands = std::move(batch.areas);
                    item.fills.swap(batch.paths);
                }
            }
            item.covered = supplied && item.fills.empty() && item.islands.empty();
        }
    }
    if (print.canceled())
        throw CanceledException();
    for (auto& item : staged) {
        item.layer->support_fills.swap(item.backup->fills);
        item.layer->raft_merge_backup = std::move(item.backup);
        item.layer->support_fills.swap(item.fills);
        item.layer->support_islands.swap(item.islands);
        item.layer->raft_entity_flags.swap(item.flags);
        item.layer->raft_covered_by_shared_paths = item.covered;
    }
    BOOST_LOG_TRIVIAL(info) << "[Raft][Merge] groups=" << group_count << ", layer_regions=" << pending.size();
}

} // namespace Slic3r
