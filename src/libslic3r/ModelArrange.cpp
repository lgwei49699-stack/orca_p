#include "ModelArrange.hpp"
#include "Arrange.hpp"

#include <libslic3r/Model.hpp>
#include <libslic3r/Geometry/ConvexHull.hpp>
#include <libslic3r/Print.hpp>
#include "MTUtils.hpp"

namespace Slic3r {

arrangement::ArrangePolygons get_arrange_polys(const Model &model, ModelInstancePtrs &instances)
{
    size_t count = 0;
    for (auto obj : model.objects) count += obj->instances.size();

    ArrangePolygons input;
    input.reserve(count);
    instances.clear(); instances.reserve(count);
    ArrangePolygon ap;
    for (ModelObject *mo : model.objects)
        for (ModelInstance *minst : mo->instances) {
            minst->get_arrange_polygon(&ap);
            input.emplace_back(ap);
            instances.emplace_back(minst);
        }

    return input;
}

bool apply_arrange_polys(ArrangePolygons &input, ModelInstancePtrs &instances, VirtualBedFn vfn)
{
    bool ret = true;

    for(size_t i = 0; i < input.size(); ++i) {
        if (input[i].bed_idx != 0) { ret = false; if (vfn) vfn(input[i]); }
        if (input[i].bed_idx >= 0)
            instances[i]->apply_arrange_result(input[i].translation.cast<double>(),
                                               input[i].rotation);
    }

    return ret;
}

Slic3r::arrangement::ArrangePolygon get_arrange_poly(const Model &model)
{
    ArrangePolygon ap;
    Points &apts = ap.poly.contour.points;
    for (const ModelObject *mo : model.objects)
        for (const ModelInstance *minst : mo->instances) {
            ArrangePolygon obj_ap;
            minst->get_arrange_polygon(&obj_ap);
            ap.poly.contour.rotate(obj_ap.rotation);
            ap.poly.contour.translate(obj_ap.translation.x(), obj_ap.translation.y());
            const Points &pts = obj_ap.poly.contour.points;
            std::copy(pts.begin(), pts.end(), std::back_inserter(apts));
        }

    apts = std::move(Geometry::convex_hull(apts).points);
    return ap;
}

void duplicate(Model &model, Slic3r::arrangement::ArrangePolygons &copies, VirtualBedFn vfn)
{
    for (ModelObject *o : model.objects) {
        // make a copy of the pointers in order to avoid recursion when appending their copies
        ModelInstancePtrs instances = o->instances;
        o->instances.clear();
        for (const ModelInstance *i : instances) {
            for (arrangement::ArrangePolygon &ap : copies) {
                if (ap.bed_idx != 0) vfn(ap);
                ModelInstance *instance = o->add_instance(*i);
                Vec2d pos = unscale(ap.translation);
                instance->set_offset(instance->get_offset() + to_3d(pos, 0.));
            }
        }
        o->invalidate_bounding_box();
    }
}

void duplicate_objects(Model &model, size_t copies_num)
{
    for (ModelObject *o : model.objects) {
        // make a copy of the pointers in order to avoid recursion when appending their copies
        ModelInstancePtrs instances = o->instances;
        for (const ModelInstance *i : instances)
            for (size_t k = 2; k <= copies_num; ++ k)
                o->add_instance(*i);
    }
}

// Set up arrange polygon for a ModelInstance and Wipe tower
template<class T>
arrangement::ArrangePolygon get_arrange_poly(T obj, const Slic3r::DynamicPrintConfig& config)
{
    ArrangePolygon ap = obj.get_arrange_polygon(config);
    //BBS: always set bed_idx to 0 to use original transforms with no bed_idx
    //if this object is not arranged, it can keep the original transforms
    //ap.bed_idx        = ap.translation.x() / bed_stride_x(plater);
    ap.bed_idx = 0;
    ap.setter = [obj](const ArrangePolygon& p) {
        if (p.is_arranged()) {
            Vec2d t = p.translation.cast<double>();
            //BBS: change to sudoku-style computation, do it in partplate list
            //t.x() += p.bed_idx * bed_stride(plater);
            //t.x() += col * bed_stride_x(plater);
            //t.y() -= row * bed_stride_y(plater);
            T{ obj }.apply_arrange_result(t, p.rotation, p.itemid);
        }
    };

    return ap;
}

template<>
arrangement::ArrangePolygon get_arrange_poly(ModelInstance* inst, const Slic3r::DynamicPrintConfig& config)
{
    return get_arrange_poly(PtrWrapper{ inst },config);
}

ArrangePolygon get_instance_arrange_poly(ModelInstance* instance, const Slic3r::DynamicPrintConfig& config)
{
    ArrangePolygon ap = get_arrange_poly(PtrWrapper{ instance }, config);

    //BBS: add temperature information
    if (config.has("curr_bed_type")) {
        ap.bed_temp = 0;
        ap.first_bed_temp = 0;
        BedType curr_bed_type = config.opt_enum<BedType>("curr_bed_type");

        const ConfigOptionInts* bed_opt = config.option<ConfigOptionInts>(get_bed_temp_key(curr_bed_type));
        if (bed_opt != nullptr)
            ap.bed_temp = bed_opt->get_at(ap.extrude_ids.front()-1);

        const ConfigOptionInts* bed_opt_1st_layer = config.option<ConfigOptionInts>(get_bed_temp_1st_layer_key(curr_bed_type));
        if (bed_opt_1st_layer != nullptr)
            ap.first_bed_temp = bed_opt_1st_layer->get_at(ap.extrude_ids.front()-1);
    }

    if (config.has("nozzle_temperature")) //get the print temperature
        ap.print_temp = config.opt_int("nozzle_temperature", ap.extrude_ids.front() - 1);
    if (config.has("nozzle_temperature_initial_layer")) //get the nozzle_temperature_initial_layer
        ap.first_print_temp = config.opt_int("nozzle_temperature_initial_layer", ap.extrude_ids.front() - 1);

    if (config.has("temperature_vitrification")) {
        ap.vitrify_temp = config.opt_int("temperature_vitrification", ap.extrude_ids.front() - 1);
    }

    // get filament temp types
    auto* filament_types_opt = dynamic_cast<const ConfigOptionStrings*>(config.option("filament_type"));
    if (filament_types_opt) {
        std::set<int> filament_temp_types;
        for (auto i : ap.extrude_ids) {
            std::string type_str = filament_types_opt->get_at(i-1);
            int temp_type = Print::get_filament_temp_type(type_str);
            filament_temp_types.insert(temp_type);
        }
        ap.filament_temp_type = Print::get_compatible_filament_type(filament_temp_types);
    }

    // get brim width
    auto obj = instance->get_object();

    ap.brim_width = 1.0;
    
    // Smart support detection: analyze actual mesh geometry to detect overhangs
    auto supp_type_ptr = obj->get_config_value<ConfigOptionBool>(config, "enable_support");
    auto support_type_ptr = obj->get_config_value<ConfigOptionEnum<SupportType>>(config, "support_type");
    auto support_threshold_ptr = obj->get_config_value<ConfigOptionFloat>(config, "support_threshold_angle");
    
    bool enable_support = supp_type_ptr && supp_type_ptr->getBool();
    auto support_type = support_type_ptr ? support_type_ptr->value : stNormalAuto;
    double support_threshold = support_threshold_ptr ? support_threshold_ptr->value : 30.0;
    
    double support_spacing = 0;
    if (enable_support) {
        // Fast overhang detection using mesh geometry
        bool likely_needs_support = false;
        double estimated_support_extent = 0;
        
        // Get model geometry
        auto bbox = obj->raw_bounding_box();
        double width = std::max(bbox.size().x(), bbox.size().y());
        double height = bbox.size().z();
        
        // Quick check: very low models (<5mm) almost never need support
        if (height < 5.0) {
            likely_needs_support = false;
            estimated_support_extent = 0;
        } else {
            // Analyze mesh for overhangs (fast approximation)
            TriangleMesh mesh = obj->raw_mesh();
            auto& facets = mesh.its.indices;
            auto& vertices = mesh.its.vertices;
            
            if (!facets.empty() && !vertices.empty()) {
                // Count downward-facing triangles (quick overhang indicator)
                int downward_faces = 0;
                double max_overhang_extent = 0;
                
                // Adaptive sampling: adjust rate based on model complexity
                size_t sample_stride;
                if (facets.size() < 1000) {
                    // Small models: check all faces for accuracy
                    sample_stride = 1;
                } else if (facets.size() < 10000) {
                    // Medium models: 0.2% sampling (1/500)
                    sample_stride = facets.size() / 500;
                } else {
                    // Large models: 1% sampling (1/100)
                    sample_stride = facets.size() / 100;
                }
                sample_stride = std::max<size_t>(1, sample_stride);
                
                for (size_t i = 0; i < facets.size(); i += sample_stride) {
                    auto& f = facets[i];
                    Vec3f v0 = vertices[f[0]];
                    Vec3f v1 = vertices[f[1]];
                    Vec3f v2 = vertices[f[2]];
                    
                    // Calculate face normal
                    Vec3f normal = (v1 - v0).cross(v2 - v0);
                    if (normal.norm() > 0.001) {
                        normal.normalize();
                        
                        // Check if face is downward-facing (normal.z < 0)
                        // and steep enough to need support (angle > threshold)
                        double angle_deg = std::asin(std::abs(normal.z())) * 180.0 / M_PI;
                        
                        if (normal.z() < 0 && angle_deg > support_threshold) {
                            downward_faces++;
                            
                            // Estimate overhang extent in XY plane
                            double extent = std::max({
                                std::abs(v0.x() - v1.x()), std::abs(v0.y() - v1.y()),
                                std::abs(v1.x() - v2.x()), std::abs(v1.y() - v2.y()),
                                std::abs(v2.x() - v0.x()), std::abs(v2.y() - v0.y())
                            });
                            max_overhang_extent = std::max(max_overhang_extent, (double)extent);
                        }
                    }
                }
                
                // Decision: if >5% of sampled faces are overhangs, model needs support
                double overhang_ratio = (double)downward_faces / (facets.size() / sample_stride);
                if (overhang_ratio > 0.05) {
                    likely_needs_support = true;
                    estimated_support_extent = max_overhang_extent;
                }
            }
        }
        
        // Calculate support spacing based on actual detection
        if (likely_needs_support) {
            // Model needs support - use spacing based on estimated extent
            if (support_type == stNormalAuto || support_type == stNormal) {
                // Normal support: 6mm default, or estimated extent + safety margin
                support_spacing = std::max(6.0, estimated_support_extent + 2.0);
            } else {
                // Tree support: use estimated extent * 2 (tree branches spread)
                // Cap at 24mm (MAX_BRANCH_RADIUS)
                support_spacing = std::min(24.0, std::max(12.0, estimated_support_extent * 2.0));
                ap.has_tree_support = true;
            }
        } else {
            // Model doesn't need support - use minimal spacing regardless of support type
            support_spacing = 1.0;  // Minimal safety margin when no actual support needed
        }
        
        // Apply the calculated support spacing
        ap.brim_width = std::max(ap.brim_width, support_spacing);
        
        BOOST_LOG_TRIVIAL(info) << "ModelArrange: '" << obj->name << "' enable_support=" << enable_support 
                                << ", likely_needs=" << likely_needs_support << ", spacing=" << support_spacing 
                                << "mm, final_brim_width=" << ap.brim_width << "mm";
    }

    auto size = obj->instance_convex_hull_bounding_box(instance).size();
    ap.height = size.z();
    ap.name = obj->name;
    return ap;
}

} // namespace Slic3r
