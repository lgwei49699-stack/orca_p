#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Surface.hpp"
#include "../VariableWidth.hpp"
#include "Arachne/WallToolPaths.hpp"

#include "FillConcentric.hpp"
#include <libslic3r/ShortestPath.hpp>

namespace Slic3r {

void FillConcentric::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    // no rotation is supported for this infill pattern
    BoundingBox bounding_box = expolygon.contour.bounding_box();
    
    coord_t min_spacing = scale_(this->spacing);
    coord_t distance = coord_t(min_spacing / params.density);
    
    if (params.density > 0.9999f && !params.dont_adjust) {
        distance = this->_adjust_solid_spacing(bounding_box.size()(0), distance);
        this->spacing = unscale<double>(distance);
    }

    Polygons   loops = to_polygons(expolygon);
    ExPolygons last { std::move(expolygon) };
    while (! last.empty()) {
        last = offset2_ex(last, -(distance + min_spacing/2), +min_spacing/2);
        append(loops, to_polygons(last));
    }

    // generate paths from the outermost to the innermost, to avoid
    // adhesion problems of the first central tiny loops
    loops = union_pt_chained_outside_in(loops);
    
    // split paths using a nearest neighbor search
    size_t iPathFirst = polylines_out.size();
    Point last_pos(0, 0);
    for (const Polygon &loop : loops) {
        polylines_out.emplace_back(loop.split_at_index(last_pos.nearest_point_index(loop.points)));
        last_pos = polylines_out.back().last_point();
    }

    // clip the paths to prevent the extruder from getting exactly on the first point of the loop
    // Keep valid paths only.
    size_t j = iPathFirst;
    for (size_t i = iPathFirst; i < polylines_out.size(); ++ i) {
        polylines_out[i].clip_end(this->loop_clipping);
        if (polylines_out[i].is_valid()) {
            if (j < i)
                polylines_out[j] = std::move(polylines_out[i]);
            ++ j;
        }
    }
    if (j < polylines_out.size())
        polylines_out.erase(polylines_out.begin() + j, polylines_out.end());
    //TODO: return ExtrusionLoop objects to get better chained paths,
    // otherwise the outermost loop starts at the closest point to (0, 0).
    // We want the loops to be split inside the G-code generator to get optimum path planning.
}

void FillConcentric::_fill_surface_single(const FillParams& params,
    unsigned int                   thickness_layers,
    const std::pair<float, Point>& direction,
    ExPolygon                      expolygon,
    ThickPolylines& thick_polylines_out)
{
    assert(params.use_arachne);
    assert(this->print_config != nullptr && this->print_object_config != nullptr);

    // no rotation is supported for this infill pattern
    Point   bbox_size = expolygon.contour.bounding_box().size();
    coord_t min_spacing = scaled<coord_t>(this->spacing);

    if (params.density > 0.9999f && !params.dont_adjust) {
        coord_t                loops_count = std::max(bbox_size.x(), bbox_size.y()) / min_spacing + 1;
        Polygons               polygons = offset(expolygon, float(min_spacing) / 2.f);

        double min_nozzle_diameter = *std::min_element(print_config->nozzle_diameter.values.begin(), print_config->nozzle_diameter.values.end());
        Arachne::WallToolPathsParams input_params;
        input_params.min_bead_width = 0.85 * min_nozzle_diameter;
        input_params.min_feature_size = 0.25 * min_nozzle_diameter;
        input_params.wall_transition_length = 1.0 * min_nozzle_diameter;
        input_params.wall_transition_angle = 10;
        input_params.wall_transition_filter_deviation = 0.25 * min_nozzle_diameter;
        input_params.wall_distribution_count = 1;

        Arachne::WallToolPaths wallToolPaths(polygons, min_spacing, min_spacing, loops_count, 0, params.layer_height, input_params);

        std::vector<Arachne::VariableWidthLines>    loops = wallToolPaths.getToolPaths();
        std::vector<const Arachne::ExtrusionLine*> all_extrusions;
        for (Arachne::VariableWidthLines& loop : loops) {
            if (loop.empty())
                continue;
            for (const Arachne::ExtrusionLine& wall : loop)
                all_extrusions.emplace_back(&wall);
        }

        // Split paths using a nearest neighbor search.
        size_t firts_poly_idx = thick_polylines_out.size();
        Point  last_pos(0, 0);
        for (const Arachne::ExtrusionLine* extrusion : all_extrusions) {
            if (extrusion->empty())
                continue;

            ThickPolyline thick_polyline = Arachne::to_thick_polyline(*extrusion);
            if (extrusion->is_closed)
                thick_polyline.start_at_index(last_pos.nearest_point_index(thick_polyline.points));
            thick_polylines_out.emplace_back(std::move(thick_polyline));
            last_pos = thick_polylines_out.back().last_point();
        }

        // clip the paths to prevent the extruder from getting exactly on the first point of the loop
        // Keep valid paths only.
        size_t j = firts_poly_idx;
        for (size_t i = firts_poly_idx; i < thick_polylines_out.size(); ++i) {
            thick_polylines_out[i].clip_end(this->loop_clipping);
            if (thick_polylines_out[i].is_valid()) {
                if (j < i)
                    thick_polylines_out[j] = std::move(thick_polylines_out[i]);
                ++j;
            }
        }
        if (j < thick_polylines_out.size())
            thick_polylines_out.erase(thick_polylines_out.begin() + int(j), thick_polylines_out.end());

        reorder_by_shortest_traverse(thick_polylines_out);
    }
    else {
        Polylines polylines;
        this->_fill_surface_single(params, thickness_layers, direction, expolygon, polylines);
        append(thick_polylines_out, to_thick_polylines(std::move(polylines), min_spacing));
    }
}

ExPolygons FillConcentric::fill_surface_single_wall_extrusion(
    const Surface *surface, const FillParams &params, ExtrusionEntitiesPtr &out)
{
    assert(this->print_config != nullptr && this->print_object_config != nullptr);
    assert(params.use_arachne);

    const Polygons polygons = to_polygons(surface->expolygon);
    if (polygons.empty())
        return {};

    const coord_t nominal_width = scaled<coord_t>(this->spacing);
    if (nominal_width <= 0)
        return {};
    Arachne::WallToolPathsParams input_params =
        Arachne::make_paths_params(int(this->layer_id), *this->print_object_config, *this->print_config);
    input_params.is_top_or_bottom_layer = true;

    Arachne::WallToolPaths wall_tool_paths(
        polygons, nominal_width, nominal_width, 1, 0, params.layer_height, input_params);
    const std::vector<Arachne::VariableWidthLines> &tool_paths = wall_tool_paths.getToolPaths();

    ThickPolylines thick_polylines;
    Point last_pos(0, 0);
    for (const Arachne::VariableWidthLines &walls : tool_paths) {
        for (const Arachne::ExtrusionLine &wall : walls) {
            if (wall.empty())
                continue;

            ThickPolyline thick_polyline = Arachne::to_thick_polyline(wall);
            if (wall.is_closed && thick_polyline.points.front() == thick_polyline.points.back() &&
                thick_polyline.width.front() == thick_polyline.width.back()) {
                thick_polyline.points.pop_back();
                assert(thick_polyline.points.size() * 2 == thick_polyline.width.size());
                const int nearest_idx = last_pos.nearest_point_index(thick_polyline.points);
                std::rotate(
                    thick_polyline.points.begin(), thick_polyline.points.begin() + nearest_idx, thick_polyline.points.end());
                std::rotate(
                    thick_polyline.width.begin(), thick_polyline.width.begin() + 2 * nearest_idx, thick_polyline.width.end());
                thick_polyline.points.emplace_back(thick_polyline.points.front());
            }
            last_pos = thick_polyline.last_point();
            thick_polylines.emplace_back(std::move(thick_polyline));
        }
    }

    if (!thick_polylines.empty()) {
        auto *collection = new ExtrusionEntityCollection();
        collection->no_sort = true;
        const Flow flow = params.flow.with_spacing(this->spacing);
        ExtrusionEntitiesPtr variable_width_entities;
        variable_width(thick_polylines, params.extrusion_role, flow, variable_width_entities);
        for (ExtrusionEntity *entity : variable_width_entities) {
            if (auto *loop = dynamic_cast<ExtrusionLoop *>(entity)) {
                // This is a solid-fill contour, not a perimeter seam. Keep it
                // geometrically closed while routing it through G-code's
                // multipath exporter, which does not clip the end by seam_gap.
                auto *closed_fill = new ExtrusionMultiPath();
                closed_fill->paths = std::move(loop->paths);
                closed_fill->set_reverse();
                collection->entities.emplace_back(closed_fill);
                delete loop;
            } else {
                entity->set_reverse();
                collection->entities.emplace_back(entity);
            }
        }
        variable_width_entities.clear();
        if (!collection->entities.empty())
            out.emplace_back(collection);
        else
            delete collection;
    }

    return union_ex(wall_tool_paths.getInnerContour());
}

} // namespace Slic3r
