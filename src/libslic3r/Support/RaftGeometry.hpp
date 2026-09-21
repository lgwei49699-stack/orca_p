#ifndef slic3r_RaftGeometry_hpp_
#define slic3r_RaftGeometry_hpp_

#include "../ExPolygon.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace Slic3r {

class ExtrusionPath;

struct RaftGeometryOptions
{
    bool   segmented  = false;
    double chunk_size = 40.;
    double chunk_gap  = 0.8;
    double smoothing  = 0.;
};

struct RaftGeometryPlan
{
    std::vector<ExPolygons> layers;
    std::vector<size_t>     layer_cuts;
    bool                    changed = false;
    // Same phase preparation, before cuts, restricted to the printable bed.
    // Distinguishes new cut remnants from originally small, separate islands.
    std::vector<ExPolygons> uncut_layers;
};

struct RaftPathRegions
{
    ExPolygons fill_regions;
    ExPolygons wall_centerlines;
    size_t     omitted_regions = 0;
};

// Process each physical layer independently: union, round closing/opening,
// phase inset, connected-island segmentation, printable-area intersection.
// Inputs are nominal solid outlines, not extrusion coverage. Lengths are in mm.
// cleanup_width corresponds to X-PRINT's skirt/brim width. Phase insets are
// supplied by the caller; they are applied once before size classification.
// Off (no segmentation and zero smoothing) preserves input exactly.
RaftGeometryPlan plan_raft_geometry(const std::vector<ExPolygons>& layers,
                                    const std::vector<double>&     phase_insets,
                                    const Polygons&                bed,
                                    const RaftGeometryOptions&     options,
                                    double                         cleanup_width,
                                    const std::function<void()>&   cancel = {});

// Prepare paths from final solid sections. Walls start on width-safe contours,
// rather than on a sheath which must subsequently be split by the bed clip.
// Only newly cut centerline islands with both small area and span are filtered;
// original small islands, long thin regions and normal short hatch paths are
// not subject to that filter. Fill outlines without a removal stay exact.
RaftPathRegions prepare_raft_path_regions(const ExPolygons&            sections,
                                          const ExPolygons&            uncut_sections,
                                          double                       width,
                                          const std::function<void()>& cancel = {});

// Find disposable, newly separated base components, not individual hatch lines.
// The caller projects ALL base layers (solid regions and emitted beads) together
// and supplies upper raft/model demand, including nominal solids and real paths.
// A component is removable only if its uncut parent still has a bearing sibling.
// Original separate islands and uncertain provenance remain unchanged. Returned
// masks include a numerical allowance and must only be used to REMOVE material.
ExPolygons find_unneeded_raft_base_regions(const ExPolygons&            base_regions,
                                           const ExPolygons&            uncut_base_regions,
                                           const ExPolygons&            bearing_regions,
                                           const std::function<void()>& cancel = {});

// Round swept beads for containment checks, including rounded path ends.
// General-purpose square-join coverage extends beyond the bead at sharp turns.
// This does not alter the paths, their widths or the allowed raft/bed region.
ExPolygons raft_extrusion_coverage(const std::vector<ExtrusionPath>& paths, const std::function<void()>& cancel = {});

} // namespace Slic3r

#endif
