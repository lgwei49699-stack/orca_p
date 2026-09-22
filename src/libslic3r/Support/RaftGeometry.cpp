#include "RaftGeometry.hpp"

#include "../BoundingBox.hpp"
#include "../ClipperUtils.hpp"
#include "../ExtrusionEntity.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace Slic3r {
namespace {

// Clipper's round-join argument is an arc tolerance in scaled coordinates, not
// a dimensionless miter limit. Use one micron, matching the path clip's margin;
// the default value 3 would mean 0.000003 mm and produce excessive vertices.
constexpr double RAFT_ARC_TOLERANCE_MM = 0.001;

bool different(const ExPolygons& a, const ExPolygons& b) { return !diff_ex(a, b).empty() || !diff_ex(b, a).empty(); }

Polygon cut_strip(const Vec2d& from, const Vec2d& to, double gap)
{
    const Vec2d direction = (to - from).normalized();
    const Vec2d normal(-direction.y(), direction.x());
    const Vec2d a    = from - gap * direction;
    const Vec2d b    = to + gap * direction;
    const Vec2d half = 0.5 * gap * normal;
    return Polygon({Point::new_scale((a - half).x(), (a - half).y()), Point::new_scale((b - half).x(), (b - half).y()),
                    Point::new_scale((b + half).x(), (b + half).y()), Point::new_scale((a + half).x(), (a + half).y())});
}

void radial_cuts(Polygons& cuts, const Vec2d& lo, const Vec2d& hi, double gap)
{
    const Vec2d mid = 0.5 * (lo + hi);
    cuts.push_back(cut_strip(Vec2d(mid.x(), lo.y()), Vec2d(mid.x(), hi.y()), gap));
    cuts.push_back(cut_strip(Vec2d(lo.x(), mid.y()), Vec2d(hi.x(), mid.y()), gap));
    cuts.push_back(cut_strip(lo, hi, gap));
    cuts.push_back(cut_strip(Vec2d(lo.x(), hi.y()), Vec2d(hi.x(), lo.y()), gap));
}

// X-PRINT 5.9.6 classifies each connected outline, not the combined plate
// bounds. Long, wide shapes are first split into strips, then each resulting
// strip gets its own radial centre. Do not extend a strip's diagonals into its
// neighbours or veto cuts because a later wall generator omits a tiny fragment.
ExPolygons chunk_island(const ExPolygon& island, const RaftGeometryOptions& options, size_t& cuts, const std::function<void()>& cancel)
{
    const BoundingBox bounds = get_extents(island);
    const Vec2d       lo = unscale(bounds.min), hi = unscale(bounds.max), size = hi - lo;
    const bool        wide = size.x() > options.chunk_size + 1e-6;
    const bool        tall = size.y() > options.chunk_size + 1e-6;
    if (!wide && !tall)
        return {island};

    const auto radial = [&](const ExPolygon& part) {
        if (cancel)
            cancel();
        const BoundingBox part_bounds = get_extents(part);
        Polygons          masks;
        radial_cuts(masks, unscale(part_bounds.min), unscale(part_bounds.max), options.chunk_gap);
        cuts += masks.size();
        return diff_ex(ExPolygons{part}, union_(masks));
    };
    const double ratio = size.x() / size.y();
    if (wide && tall && ratio >= 0.5 && ratio < 2.)
        return radial(island);

    const bool   along_x = wide && (!tall || ratio >= 2.);
    const size_t axis    = along_x ? 0 : 1;
    // The limit only bounds work for unusually large custom beds/settings.
    const size_t count = size_t(std::min(64., std::ceil(size[axis] / options.chunk_size)));
    const double step  = size[axis] / double(count);
    Polygons     masks;
    // The reference includes the positive end strip: its outer edge is trimmed
    // by half the gap. Retain this measured behaviour for long-strip parity.
    for (size_t i = 1; i <= count; ++i) {
        if (cancel)
            cancel();
        const double position = lo[axis] + double(i) * step;
        masks.push_back(along_x ? cut_strip(Vec2d(position, lo.y()), Vec2d(position, hi.y()), options.chunk_gap) :
                                  cut_strip(Vec2d(lo.x(), position), Vec2d(hi.x(), position), options.chunk_gap));
    }
    cuts += masks.size();
    ExPolygons strips = diff_ex(ExPolygons{island}, union_(masks));
    if (!wide || !tall)
        return strips;
    ExPolygons result;
    for (const ExPolygon& strip : strips) {
        ExPolygons sections = radial(strip);
        result.insert(result.end(), std::make_move_iterator(sections.begin()), std::make_move_iterator(sections.end()));
    }
    return result;
}

} // namespace

RaftGeometryPlan plan_raft_geometry(const std::vector<ExPolygons>& layers,
                                    const std::vector<double>&     phase_insets,
                                    const Polygons&                bed,
                                    const RaftGeometryOptions&     options,
                                    double                         cleanup_width,
                                    const std::function<void()>&   cancel)
{
    RaftGeometryPlan result{layers, std::vector<size_t>(layers.size(), 0), false, {}};
    if (layers.empty() || (!options.segmented && options.smoothing == 0.) || layers.size() != phase_insets.size() ||
        !std::isfinite(cleanup_width) || cleanup_width <= 0. || !std::isfinite(options.smoothing) || options.smoothing < 0. ||
        options.smoothing > 20. ||
        (options.segmented && (!std::isfinite(options.chunk_size) || options.chunk_size < 5. || options.chunk_size > 500. ||
                               !std::isfinite(options.chunk_gap) || options.chunk_gap < 0.1 || options.chunk_gap > 5.)) ||
        std::any_of(phase_insets.begin(), phase_insets.end(), [](double v) { return !std::isfinite(v) || v < 0.; }))
        return result;

    for (size_t i = 0; i < layers.size(); ++i) {
        if (cancel)
            cancel();
        ExPolygons outline = union_ex(layers[i]);
        if (options.smoothing > 0.)
            outline = closing_ex(outline, scale_(options.smoothing), jtRound, scale_(RAFT_ARC_TOLERANCE_MM));
        // Like the reference, opening remains active at smoothing=0 while the
        // feature is enabled. Closing may fill small holes; opening cleans thin
        // edges. Neither operation is applied after the cuts.
        outline = opening_ex(outline, scale_(cleanup_width), jtRound, scale_(RAFT_ARC_TOLERANCE_MM));
        if (phase_insets[i] > 0.)
            outline = offset_ex(outline, -scale_(phase_insets[i]));
        result.uncut_layers.push_back(intersection_ex(outline, bed));
        ExPolygons sections;
        if (options.segmented) {
            for (const ExPolygon& island : outline) {
                if (cancel)
                    cancel();
                ExPolygons part = chunk_island(island, options, result.layer_cuts[i], cancel);
                sections.insert(sections.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
            }
        } else
            sections = std::move(outline);
        // Smoothing must never fill a printer exclusion, even if it fills a
        // small raft hole. The downstream path clip also includes bead width.
        result.layers[i] = intersection_ex(sections, bed);
        result.changed |= different(layers[i], result.layers[i]);
    }
    return result;
}

RaftPathRegions prepare_raft_path_regions(const ExPolygons&            sections,
                                          const ExPolygons&            uncut_sections,
                                          double                       width,
                                          const std::function<void()>& cancel)
{
    RaftPathRegions result;
    if (!std::isfinite(width) || width <= 0.)
        return result;

    // Keep an extra micron INSIDE the final clip's half-width + 0.001 mm
    // boundary. Repeated close/offset operations at exactly that boundary used
    // to turn continuous sheaths into numerous short clipped segments.
    const double inset        = scale_(0.5 * width + 0.002);
    const double half_width   = scale_(0.5 * width);
    const double scaled_width = scale_(width);
    const auto   is_collapsed = [&](const ExPolygon& p) {
        // Area alone also rejects long, usable narrow strips. The bounding-box
        // diagonal is a conservative upper bound on the whole region's span:
        // only a small remnant contained within one line width is disposable.
        return p.area() < half_width * half_width && get_extents(p).size().cast<double>().squaredNorm() < scaled_width * scaled_width;
    };
    for (const ExPolygon& section : sections) {
        if (cancel)
            cancel();
        ExPolygons   centers         = offset_ex(section, -inset);
        bool         original_island = false;
        const size_t before          = centers.size();
        if (std::any_of(centers.begin(), centers.end(), is_collapsed)) {
            const BoundingBox bounds = get_extents(section);
            for (const ExPolygon& uncut : uncut_sections) {
                if (cancel)
                    cancel();
                if (bounds.contains(get_extents(uncut)) && diff_ex(ExPolygons{uncut}, ExPolygons{section}).empty()) {
                    original_island = true;
                    break;
                }
            }
            if (!original_island)
                centers.erase(std::remove_if(centers.begin(), centers.end(), is_collapsed), centers.end());
        }
        const size_t omitted = before - centers.size();
        result.omitted_regions += omitted;
        if (centers.empty())
            continue;
        // Keep unaffected fill outlines exact. Reconstruct only a section which
        // actually lost a remnant, so an extra opening does not round every
        // corner or remove otherwise valid narrow infill. Intersect per section
        // to preserve cuts, holes and the bed. Walls use centers directly.
        ExPolygons fill = omitted == 0 ?
                              ExPolygons{section} :
                              intersection_ex(offset_ex(centers, inset, jtRound, scale_(RAFT_ARC_TOLERANCE_MM)), ExPolygons{section});
        result.fill_regions.insert(result.fill_regions.end(), std::make_move_iterator(fill.begin()), std::make_move_iterator(fill.end()));
        result.wall_centerlines.insert(result.wall_centerlines.end(), std::make_move_iterator(centers.begin()),
                                       std::make_move_iterator(centers.end()));
    }
    return result;
}

ExPolygons find_unneeded_raft_base_regions(const ExPolygons&            base_regions,
                                           const ExPolygons&            uncut_base_regions,
                                           const ExPolygons&            bearing_regions,
                                           const std::function<void()>& cancel)
{
    if (cancel)
        cancel();
    if (base_regions.empty() || uncut_base_regions.empty() || bearing_regions.empty())
        return {};

    // Projection across base layers deliberately overestimates connectivity:
    // a narrow lower base supporting a wider upper base must not be discarded.
    // Two microns also preserve touching/near-touching sections and bead ends.
    const ExPolygons components = offset_ex(union_ex(base_regions), scale_(0.002));
    const ExPolygons parents    = union_ex(uncut_base_regions);
    const ExPolygons demand     = offset_ex(union_ex(bearing_regions), scale_(0.002));
    ExPolygons       bearing, detached;
    for (const ExPolygon& component : components) {
        if (cancel)
            cancel();
        (intersection_ex(ExPolygons{component}, demand).empty() ? detached : bearing).push_back(component);
    }
    if (bearing.empty())
        return {}; // No proven useful sibling; do not delete an entire source island.

    ExPolygons removed;
    for (const ExPolygon& component : detached) {
        if (cancel)
            cancel();
        for (const ExPolygon& parent : parents) {
            if (cancel)
                cancel();
            if (!get_extents(component).overlap(get_extents(parent)))
                continue;
            // Only a known subset split off a larger parent is eligible. The
            // extra allowance covers the projected beads and the union above.
            if (!diff_ex(ExPolygons{component}, offset_ex(parent, scale_(0.004))).empty())
                continue;
            if (!diff_ex(ExPolygons{parent}, ExPolygons{component}).empty() && !intersection_ex(ExPolygons{parent}, bearing).empty())
                removed.push_back(component);
            break;
        }
    }
    return removed;
}

ExPolygons raft_extrusion_coverage(const std::vector<ExtrusionPath>& paths, const std::function<void()>& cancel)
{
    Polygons beads;
    for (const ExtrusionPath& path : paths) {
        if (cancel)
            cancel();
        if (path.is_force_no_extrusion() || path.mm3_per_mm <= 0. || path.width <= 0. || path.polyline.size() < 2)
            continue;
        polygons_append(beads, offset(path.polyline, float(scale_(0.5 * double(path.width))), jtRound, scale_(RAFT_ARC_TOLERANCE_MM),
                                      ClipperLib::etOpenRound));
    }
    return union_ex(beads);
}

} // namespace Slic3r
