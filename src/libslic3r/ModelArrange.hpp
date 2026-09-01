#ifndef MODELARRANGE_HPP
#define MODELARRANGE_HPP

#include <map>
#include <set>
#include <string>

#include <libslic3r/Arrange.hpp>
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {
using ModelInstancePtrs = std::vector<ModelInstance*>;

using arrangement::ArrangePolygon;
using arrangement::ArrangePolygons;
using arrangement::ArrangeParams;
using arrangement::InfiniteBed;
using arrangement::CircleBed;

// Do something with ArrangePolygons in virtual beds
using VirtualBedFn = std::function<void(arrangement::ArrangePolygon&)>;

[[noreturn]] inline void throw_if_out_of_bed(arrangement::ArrangePolygon&) 
{
    throw Slic3r::RuntimeError("Objects could not fit on the bed");
}

ArrangePolygons get_arrange_polys(const Model &model, ModelInstancePtrs &instances);
ArrangePolygon  get_arrange_poly(const Model &model);
bool apply_arrange_polys(ArrangePolygons &polys, ModelInstancePtrs &instances, VirtualBedFn);

void duplicate(Model &model, ArrangePolygons &copies, VirtualBedFn);
void duplicate_objects(Model &model, size_t copies_num);

template<class TBed>
bool arrange_objects(Model &              model,
                     const TBed &         bed,
                     const ArrangeParams &params,
                     VirtualBedFn         vfn = throw_if_out_of_bed)
{
    ModelInstancePtrs instances;
    auto&& input = get_arrange_polys(model, instances);
    arrangement::arrange(input, bed, params);
    
    return apply_arrange_polys(input, instances, vfn);
}

template<class TBed>
void duplicate(Model &              model,
               size_t               copies_num,
               const TBed &         bed,
               const ArrangeParams &params,
               VirtualBedFn         vfn = throw_if_out_of_bed)
{
    ArrangePolygons copies(copies_num, get_arrange_poly(model));
    arrangement::arrange(copies, bed, params);
    duplicate(model, copies, vfn);
}

template<class TBed>
void duplicate_objects(Model &              model,
                       size_t               copies_num,
                       const TBed &         bed,
                       const ArrangeParams &params,
                       VirtualBedFn         vfn = throw_if_out_of_bed)
{
    duplicate_objects(model, copies_num);
    arrange_objects(model, bed, params, vfn);
}

template<class T> struct PtrWrapper
{
    T* ptr;

    explicit PtrWrapper(T* p) : ptr{ p } {}

    arrangement::ArrangePolygon get_arrange_polygon(const Slic3r::DynamicPrintConfig &config = Slic3r::DynamicPrintConfig()) const
    {
        arrangement::ArrangePolygon ap;
        ptr->get_arrange_polygon(&ap, config);
        return ap;
    }

    void apply_arrange_result(const Vec2d& t, double rot, int item_id)
    {
        ptr->apply_arrange_result(t, rot);
        ptr->arrange_order = item_id;
    }
};

template<class T>
arrangement::ArrangePolygon get_arrange_poly(T obj, const DynamicPrintConfig &config = DynamicPrintConfig());

template<>
arrangement::ArrangePolygon get_arrange_poly(ModelInstance* inst, const DynamicPrintConfig& config);

ArrangePolygon get_instance_arrange_poly(ModelInstance* instance, const DynamicPrintConfig& config);

// Lightweight support/Cura V1 raft-footprint estimate used by both GUI and CLI arranging.
double estimate_arrange_support_margin(const ModelInstance &instance, const DynamicPrintConfig &config);

// Keep the CLI's arrange and transform-export paths on the same auto-plate
// semantics. An omitted option retains the historical multi-plate behavior.
int resolve_arrange_auto_plate_value(bool option_was_specified, int configured_value);

// --split-by-color is a stronger constraint than the general CLI preference:
// its color groups must stay on separate plates during the subsequent arrange.
bool resolve_arrange_allow_multicolor_oneplate(bool configured_value, bool split_by_color);
std::string resolve_model_arrange_group(const std::string& explicit_group,
                                        bool               auto_color_enabled,
                                        bool               grouping_enabled,
                                        const std::string& detected_color_group);

// Keep GUI and CLI consistent when deciding whether a wipe tower must reserve
// space during arranging. Merely enabling the option is not sufficient: a
// traditional single-filament print has no tool change and needs no tower.
// selected_are_fixed_to_one_plate is true for current-plate and assembled-plate
// arranging, where distinct single-extruder objects cannot be split apart.
bool arrange_wipe_tower_needed(const DynamicPrintConfig &config,
                               const ArrangePolygons &selected,
                               const ArrangeParams &params,
                               bool selected_are_fixed_to_one_plate,
                               std::string *reason = nullptr);

using ArrangeWipeTowerPlan = std::map<int, std::set<int>>;

// Build a per-bed tower plan from an already arranged result. This avoids
// reserving a tower on every possible logical bed before the objects have a
// real bed assignment. The value contains the extruders used by that bed and
// is also used to estimate the required tower depth.
ArrangeWipeTowerPlan arrange_wipe_tower_plan(const DynamicPrintConfig &config,
                                             const ArrangePolygons &arranged,
                                             const ArrangePolygons &fixed,
                                             const ArrangeParams &params);

// Current-plate arranging must never silently move overflow to another logical
// bed, because that bed is not created by the current-plate GUI operation.
bool arrange_result_fits_single_plate(const ArrangePolygons &arranged);
}

#endif // MODELARRANGE_HPP
