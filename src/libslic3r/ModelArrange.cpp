#include "ModelArrange.hpp"
#include "Arrange.hpp"

#include <libslic3r/Model.hpp>
#include <libslic3r/Geometry/ConvexHull.hpp>
#include <libslic3r/Print.hpp>
#include "MTUtils.hpp"

namespace Slic3r {

namespace {

constexpr double DEFAULT_ARRANGE_MARGIN_MM = 1.0;
// Match the upstream safety allowance: normal supports may grow about 5 mm
// beyond the model projection, with 1 mm retained as the arrange safety gap.
constexpr double NORMAL_SUPPORT_MARGIN_MM = 6.0;
// Upstream represents the maximum first-layer tree-support footprint as a
// 24 mm diameter and converts it to a 12 mm one-sided arrange margin. Keep the
// derivation visible while storing only the final margin in ArrangePolygon.
constexpr double TREE_SUPPORT_FIRST_LAYER_DIAMETER_MM = 24.0;
constexpr double TREE_SUPPORT_MARGIN_MM = TREE_SUPPORT_FIRST_LAYER_DIAMETER_MM / 2.0;
constexpr size_t MAX_OVERHANG_FACES_TO_INSPECT = 20000;

bool has_manual_support(const ModelObject &object)
{
    return object.is_fdm_support_painted() ||
           std::any_of(object.volumes.begin(), object.volumes.end(), [](const ModelVolume *volume) {
               return volume->is_support_enforcer();
           });
}

bool has_arrange_overhang(const ModelInstance &instance, const DynamicPrintConfig &config)
{
    ModelObject *object = instance.get_object();
    const auto *enforce_layers = object->get_config_value<ConfigOptionInt>(config, "enforce_support_layers");
    if (enforce_layers != nullptr && enforce_layers->value > 0)
        return true;

    const auto *support_type = object->get_config_value<ConfigOptionEnum<SupportType>>(config, "support_type");
    if (!is_auto(support_type->value))
        return has_manual_support(*object);

    if (has_manual_support(*object))
        return true;

    size_t face_count = 0;
    for (const ModelVolume *volume : object->volumes)
        if (volume->is_model_part())
            face_count += volume->mesh().its.indices.size();
    if (face_count == 0)
        return false;

    const double object_min_z = object->instance_bounding_box(instance, true).min.z();
    const auto *first_layer_height = object->get_config_value<ConfigOptionFloat>(config, "initial_layer_print_height");
    const double bed_tolerance = std::max(0.05, first_layer_height != nullptr ? first_layer_height->value : 0.2);
    const size_t sample_step = std::max<size_t>(1, (face_count + MAX_OVERHANG_FACES_TO_INSPECT - 1) /
                                                      MAX_OVERHANG_FACES_TO_INSPECT);

    size_t face_offset = 0;
    for (const ModelVolume *volume : object->volumes) {
        if (!volume->is_model_part())
            continue;

        const indexed_triangle_set &mesh = volume->mesh().its;
        const Transform3d transform = instance.get_matrix_no_offset() * volume->get_matrix();
        const double orientation = transform.linear().determinant() < 0.0 ? -1.0 : 1.0;
        const size_t first_face = (sample_step - face_offset % sample_step) % sample_step;
        for (size_t face_index = first_face; face_index < mesh.indices.size(); face_index += sample_step) {
            const stl_triangle_vertex_indices &face = mesh.indices[face_index];

            const Vec3d p0 = transform * mesh.vertices[face[0]].cast<double>();
            const Vec3d p1 = transform * mesh.vertices[face[1]].cast<double>();
            const Vec3d p2 = transform * mesh.vertices[face[2]].cast<double>();
            if (std::max({p0.z(), p1.z(), p2.z()}) <= object_min_z + bed_tolerance)
                continue;

            Vec3d normal = orientation * (p1 - p0).cross(p2 - p0);
            const double normal_length = normal.norm();
            // The real support generators detect overhangs from adjacent slices and may
            // additionally classify an edge-resting object's narrow initial layers as a
            // sharp tail. A face-angle threshold alone therefore misses objects such as a
            // cube tilted by about 45 degrees: its downward face is shallower than the
            // configured threshold, while tree support still creates a build-plate base.
            // Ignore downward faces that lie entirely on the bed, but conservatively keep
            // the support footprint for every downward-facing surface above the bed.
            if (normal_length > EPSILON && normal.z() < -EPSILON)
                return true;
        }
        face_offset += mesh.indices.size();
    }

    return false;
}

double support_margin(SupportType support_type)
{
    return is_tree(support_type) ? TREE_SUPPORT_MARGIN_MM : NORMAL_SUPPORT_MARGIN_MM;
}

} // namespace

double estimate_arrange_support_margin(const ModelInstance &instance, const DynamicPrintConfig &config)
{
    ModelObject *object = instance.get_object();
    const auto *support_enabled = object->get_config_value<ConfigOptionBool>(config, "enable_support");
    if (support_enabled == nullptr || !support_enabled->value || !has_arrange_overhang(instance, config))
        return DEFAULT_ARRANGE_MARGIN_MM;

    const auto *support_type = object->get_config_value<ConfigOptionEnum<SupportType>>(config, "support_type");
    return support_margin(support_type->value);
}

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

    ap.brim_width = estimate_arrange_support_margin(*instance, config);

    auto size = obj->instance_convex_hull_bounding_box(instance).size();
    ap.height = size.z();
    ap.name = obj->name;
    return ap;
}

} // namespace Slic3r
