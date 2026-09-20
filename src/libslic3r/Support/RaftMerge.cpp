#include "RaftMerge.hpp"

#include "../ClipperUtils.hpp"
#include "../Fill/FillBase.hpp"
#include "../GCode/ConflictChecker.hpp"
#include "../Print.hpp"
#include "SupportCommon.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <numeric>
#include <tuple>
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
    SupportLayer& layer() const { return *raft->layers[index]; }
    RaftPhase     phase() const { return raft->object->slicing_parameters().raft_phase(index); }
};

// Some generators also put ordinary support columns on raft layers. Preserve
// their original paths instead of regenerating the whole layer with raft flow.
bool can_regenerate(const RaftSlice& slice)
{
    if (!slice.raft->parameters || slice.layer().raft_outline.empty() ||
        !same_value(slice.layer().print_z, slice.raft->object->slicing_parameters().raft_layer_print_z(slice.index)))
        return false;
    const auto&    params = *slice.raft->parameters;
    const Flow&    flow   = params.raft_flow(slice.phase());
    const auto     role   = slice.phase() == RaftPhase::Base ? erSupportMaterial : erSupportMaterialInterface;
    ExtrusionPaths paths;
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

bool same_recipe(const RaftSlice& a, const RaftSlice& b)
{
    if (!can_regenerate(a) || !can_regenerate(b) || a.phase() != b.phase() || !same_value(a.layer().print_z, b.layer().print_z) ||
        !same_value(a.layer().height, b.layer().height))
        return false;
    const int tool = raft_extruder(a);
    if (tool < 0 || tool != raft_extruder(b))
        return false;
    const auto&  pa    = *a.raft->parameters;
    const auto&  pb    = *b.raft->parameters;
    const auto   phase = a.phase();
    const Flow&  fa    = pa.raft_flow(phase);
    const Flow&  fb    = pb.raft_flow(phase);
    const double delta = std::abs(double(pa.raft_layer_angle(a.index)) - double(pb.raft_layer_angle(b.index)));
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
};

void generate_shared_paths(MergedLayer& result, const BoundingBox& bounds, const Polygons& bed)
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
    filler->angle           = params.raft_layer_angle(source.index);
    filler->spacing         = params.raft_pattern_spacing(source.phase());
    filler->link_max_length = 0;
    // The shared sheath helper expects wall centerline regions; inset half a
    // bead so walls and fill remain within the assigned region, including holes.
    const size_t     walls      = params.raft_wall_count(source.phase());
    const ExPolygons fill_areas = walls == 0 ? local_areas : offset_ex(local_areas, -scale_(0.5 * flow.width() + 0.001));
    fill_expolygons_with_sheath_generate_paths(result.paths.entities, to_polygons(fill_areas), filler.get(),
                                               float(params.raft_density(source.phase())), role, flow, params, walls, true,
                                               params.raft_flow_ratio(source.phase()), pattern);
    translate_paths(result.paths, fill_origin);
    clip_raft_paths_to_area(result.paths, intersection(to_polygons(result.areas), bed));
}

} // namespace

void restore_unmerged_rafts(Print& print)
{
    for (size_t i = 0; i < print.objects().size(); ++i) {
        PrintObject* object = print.get_object(i);
        for (SupportLayer* layer : object->support_layers())
            if (layer->raft_merge_backup) {
                layer->support_fills.swap(layer->raft_merge_backup->fills);
                layer->support_islands.swap(layer->raft_merge_backup->islands);
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
    if (placed.size() < 2)
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
        if (members.size() < 2)
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
        for (auto& batch : batches)
            pending.push_back(std::move(batch));
        ++group_count;
    }
    if (pending.empty())
        return;
    if (print.canceled())
        throw CanceledException();

    // All geometry is ready before changing any live paths. Backups allow moves,
    // deletion, cached re-slicing and repeated process() calls to start cleanly.
    for (auto& batch : pending) {
        const bool supplied = !batch.paths.empty() || batch.areas.empty();
        for (const RaftSlice& source : batch.sources) {
            SupportLayer& layer     = source.layer();
            layer.raft_merge_backup = std::make_unique<SupportLayer::RaftMergeBackup>();
            layer.support_fills.swap(layer.raft_merge_backup->fills);
            layer.raft_merge_backup->islands   = layer.support_islands;
            layer.raft_covered_by_shared_paths = supplied;
        }
        const auto& owner = batch.sources.front();
        translate_paths(batch.paths, -owner.raft->shift);
        translate(batch.areas, -owner.raft->shift);
        owner.layer().support_fills.swap(batch.paths);
        owner.layer().support_islands.swap(batch.areas);
        owner.layer().raft_covered_by_shared_paths = owner.layer().support_fills.empty() && owner.layer().support_islands.empty();
    }
    BOOST_LOG_TRIVIAL(info) << "[Raft][Merge] groups=" << group_count << ", layer_regions=" << pending.size();
}

} // namespace Slic3r
