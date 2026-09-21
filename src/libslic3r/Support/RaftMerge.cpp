#include "RaftMerge.hpp"
#include "RaftGeometry.hpp"

#include "../ClipperUtils.hpp"
#include "../Fill/FillBase.hpp"
#include "../GCode/ConflictChecker.hpp"
#include "../I18N.hpp"
#include "../Print.hpp"
#include "SupportCommon.hpp"

#include <algorithm>
#include <cmath>
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
    filler->angle           = params.raft_layer_angle(source.index);
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
    translate_paths(result.paths, fill_origin);
    clip_raft_paths_to_area(result.paths, intersection(to_polygons(result.areas), bed), uncut_areas != nullptr);
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
                         const std::vector<PlacedRaft>&  placed,
                         const std::vector<size_t>&      members,
                         std::vector<MergedLayer>&       batches,
                         const std::vector<MergedLayer>& pending,
                         const BoundingBox&              bounds,
                         const Polygons&                 bed)
{
    if (std::none_of(members.begin(), members.end(), [&](size_t i) { return geometry_requested(placed[i].object->config()); }))
        return false;
    const auto skip = [](const char* reason) {
        BOOST_LOG_TRIVIAL(info) << "[Raft][Geometry] status=unchanged, reason=" << reason;
        return false;
    };
    const auto check_cancel = [&print] {
        if (print.canceled())
            throw CanceledException();
    };
    const PlacedRaft& reference = placed[members.front()];
    const auto&       config    = reference.object->config();
    if (print.extruders().size() != 1 || batches.size() != reference.layers.size())
        return skip("incompatible layer or tool plans");
    for (size_t member : members) {
        const auto& raft    = placed[member];
        const auto& current = raft.object->config();
        // Disabled ordinary supports plus exact raft layer/path checks avoid
        // inferring path provenance from a matching extrusion width alone.
        if (!raft.parameters || raft.object->has_support() || !same_geometry_options(config, current) ||
            raft.layers.size() != reference.layers.size() || raft.layers.size() != raft.object->slicing_parameters().raft_layers())
            return skip("unsupported source or different geometry settings");
    }
    std::vector<ExPolygons> outlines;
    std::vector<double>     phase_insets;
    for (size_t i = 0; i < batches.size(); ++i) {
        const auto& batch = batches[i];
        if (batch.areas.empty() || batch.paths.empty() || batch.sources.size() != members.size())
            return skip("incomplete raft layer");
        ExPolygons outline;
        for (const RaftSlice& source : batch.sources) {
            if (source.index != i || !can_regenerate(source) || !same_recipe(batch.sources.front(), source))
                return skip("mixed raft paths");
            // Use nominal source geometry. Adding old extrusion-width coverage
            // would feed wall fringes back into smoothing and size thresholds.
            ExPolygons local = source.layer().raft_outline;
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
    RaftGeometryPlan          plan          = plan_raft_geometry(outlines, phase_insets, bed, options, cleanup_width, check_cancel);
    for (size_t i = 0; i < plan.layers.size(); ++i)
        BOOST_LOG_TRIVIAL(info) << "[Raft][Geometry] phase planning, layer=" << i << ", cuts=" << plan.layer_cuts[i]
                                << ", islands=" << plan.layers[i].size();
    if (!plan.changed)
        return skip("no geometry change");

    // Smoothing may add material. Do not grow into another object's model or
    // support footprint, including raft instances excluded from this merger.
    ExPolygons added;
    for (size_t i = 0; i < batches.size(); ++i) {
        ExPolygons growth = diff_ex(plan.layers[i], batches[i].areas);
        added.insert(added.end(), growth.begin(), growth.end());
    }
    if (!added.empty()) {
        added              = union_ex(added);
        const double top_z = batches.back().sources.front().layer().print_z;
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
    std::vector<ExPolygons>  materials(batches.size());
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
    candidates.reserve(batches.size());
    for (size_t i = 0; i < batches.size(); ++i) {
        check_cancel();
        candidates.emplace_back();
        auto& candidate   = candidates.back();
        candidate.sources = batches[i].sources;
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
        ExPolygons base, uncut_base, demand;
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
                            << ", omitted_base_regions=" << omitted_base_regions << ", smoothing=" << options.smoothing;
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
