#ifndef slic3r_SupportParameters_hpp_
#define slic3r_SupportParameters_hpp_

#include <tuple>
#include <utility>

#include <boost/log/trivial.hpp>
#include "../libslic3r.h"
#include "../Flow.hpp"
#include "../Slicing.hpp"

namespace Slic3r {
struct SupportParameters {
    SupportParameters() = delete;
    SupportParameters(const PrintObject& object)
    {
        const PrintConfig& print_config = object.print()->config();
        const PrintObjectConfig& object_config = object.config();
        const SlicingParameters& slicing_params = object.slicing_parameters();

        this->cura_raft_mode = slicing_params.cura_raft_mode;

	    this->soluble_interface = slicing_params.soluble_interface;
	    this->soluble_interface_non_soluble_base =
	        // Zero z-gap between the overhangs and the support interface.
	        slicing_params.soluble_interface &&
	        // Interface extruder soluble.
	        object_config.support_interface_filament.value > 0 && print_config.filament_soluble.get_at(object_config.support_interface_filament.value - 1) &&
	        // Base extruder: Either "print with active extruder" not soluble.
	        (object_config.support_filament.value == 0 || ! print_config.filament_soluble.get_at(object_config.support_filament.value - 1));

	    {
	        this->num_top_interface_layers    = std::max(0, object_config.support_interface_top_layers.value);
	        this->num_bottom_interface_layers = object_config.support_interface_bottom_layers < 0 ? 
	            num_top_interface_layers : object_config.support_interface_bottom_layers;
	        this->has_top_contacts              = num_top_interface_layers    > 0;
	        this->has_bottom_contacts           = num_bottom_interface_layers > 0;
	        if (this->soluble_interface_non_soluble_base) {
	            // Try to support soluble dense interfaces with non-soluble dense interfaces.
	            this->num_top_base_interface_layers    = size_t(std::min(int(num_top_interface_layers) / 2, 2));
	            this->num_bottom_base_interface_layers = size_t(std::min(int(num_bottom_interface_layers) / 2, 2));
	        } else {
                // BBS: if support interface and support base do not use the same filament, add a base layer to improve their adhesion
                // Note: support materials (such as Supp.W) can't be used as support base now, so support interface and base are still using different filaments even if
                // support_filament==0
                bool differnt_support_interface_filament = object_config.support_interface_filament != 0 &&
                                                           object_config.support_interface_filament != object_config.support_filament;
                this->num_top_base_interface_layers    = differnt_support_interface_filament ? 1 : 0;
                this->num_bottom_base_interface_layers       = differnt_support_interface_filament ? 1 : 0;
	        }
	    }
        this->first_layer_flow = Slic3r::support_material_1st_layer_flow(&object, float(slicing_params.first_print_layer_height));
        this->support_material_flow = Slic3r::support_material_flow(&object, float(slicing_params.layer_height));
        this->support_material_interface_flow = Slic3r::support_material_interface_flow(&object, float(slicing_params.layer_height));
    	this->raft_interface_flow                = support_material_interface_flow;

        if (this->cura_raft_mode) {
            const coordf_t support_nozzle = print_config.nozzle_diameter.get_at(object_config.support_filament.value - 1);
            const coordf_t interface_nozzle =
                print_config.nozzle_diameter.get_at(object_config.support_interface_filament.value - 1);
            const RaftPhasePlan raft_plan = build_cura_raft_phase_plan(
                resolve_cura_raft_plan_config(
                    print_config, object_config, support_nozzle, interface_nozzle));
            assert(raft_plan.validate());

            const RaftPhaseLayer &base_layer = *raft_plan.find_layer(RaftPhase::Base, 0);
            const RaftPhaseLayer *interface_layer = raft_plan.find_layer(RaftPhase::Interface, 0);
            const RaftPhaseLayer &surface_layer = *raft_plan.find_layer(RaftPhase::Surface, 0);
            if (interface_layer == nullptr)
                interface_layer = &surface_layer;

            const auto phase_flow_density_and_spacing = [](const RaftPhaseLayer &layer, coordf_t nozzle_diameter) {
                Flow flow(float(layer.line_width), float(layer.height), float(nozzle_diameter));
                // Fill generators require density <= 1. Select a pattern base
                // spacing no larger than the configured center distance, while
                // keeping Flow's nominal spacing for wall geometry and width.
                const coordf_t pattern_spacing = std::min(coordf_t(flow.spacing()), coordf_t(layer.line_spacing));
                const coordf_t density = pattern_spacing / layer.line_spacing;
                return std::make_tuple(flow, density, pattern_spacing);
            };

            std::tie(this->raft_base_flow, this->raft_base_density, this->raft_base_pattern_spacing) =
                phase_flow_density_and_spacing(base_layer, support_nozzle);
            std::tie(this->raft_phase_interface_flow, this->raft_phase_interface_density,
                     this->raft_phase_interface_pattern_spacing) =
                phase_flow_density_and_spacing(*interface_layer, interface_nozzle);
            std::tie(this->raft_surface_flow, this->raft_surface_density, this->raft_surface_pattern_spacing) =
                phase_flow_density_and_spacing(surface_layer, interface_nozzle);
            this->raft_base_flow_ratio = base_layer.flow_ratio;
            this->raft_phase_interface_flow_ratio = interface_layer->flow_ratio;
            this->raft_surface_flow_ratio = surface_layer.flow_ratio;
            this->raft_base_wall_count = base_layer.wall_count;
            this->raft_phase_interface_wall_count = interface_layer->wall_count;
            this->raft_surface_wall_count = surface_layer.wall_count;

            this->first_layer_flow = this->raft_base_flow;
            this->raft_interface_flow = this->raft_surface_flow;
        }

        // Cura V1 RaftContact does not yet have a separate ironing path. Ignore
        // stale legacy values when switching modes instead of silently turning
        // the final Surface layer into legacy support ironing.
        this->ironing = !this->cura_raft_mode && object_config.support_ironing;
        this->ironing_flow = support_material_interface_flow.with_height(support_material_interface_flow.height() * 0.01 * object_config.support_ironing_flow.value);
        this->ironing_spacing = object_config.support_ironing_spacing;
        this->ironing_pattern = object_config.support_ironing_pattern;

        // Calculate a minimum support layer height as a minimum over all extruders, but not smaller than 10um.
        this->support_layer_height_min = scaled<coord_t>(0.01);
        for (auto lh : print_config.min_layer_height.values)
            this->support_layer_height_min = std::min(this->support_layer_height_min, std::max(0.01, lh));
        for (auto layer : object.layers())
            this->support_layer_height_min = std::min(this->support_layer_height_min, std::max(0.01, layer->height));
        
        if (object_config.support_interface_top_layers.value == 0) {
            // No interface layers allowed, print everything with the base support pattern.
            this->support_material_interface_flow = this->support_material_flow;
        }
        
        // Evaluate the XY gap between the object outer perimeters and the support structures.
        // Evaluate the XY gap between the object outer perimeters and the support structures.
        coordf_t external_perimeter_width = 0.;
        coordf_t bridge_flow_ratio = 0;
        for (size_t region_id = 0; region_id < object.num_printing_regions(); ++ region_id) {
            const PrintRegion &region = object.printing_region(region_id);
            external_perimeter_width = std::max(external_perimeter_width, coordf_t(region.flow(object, frExternalPerimeter, slicing_params.layer_height).width()));
            bridge_flow_ratio += region.config().bridge_flow;
        }
        this->gap_xy = object_config.support_object_xy_distance.value;
        this->gap_xy_first_layer = object_config.support_object_first_layer_gap.value;
        bridge_flow_ratio /= object.num_printing_regions();

        this->support_material_bottom_interface_flow = slicing_params.soluble_interface || !object_config.thick_bridges ?
            this->support_material_interface_flow.with_flow_ratio(bridge_flow_ratio) :
            Flow::bridging_flow(bridge_flow_ratio * this->support_material_interface_flow.nozzle_diameter(), this->support_material_interface_flow.nozzle_diameter());
        
        this->can_merge_support_regions = object_config.support_filament.value == object_config.support_interface_filament.value;
        if (!this->can_merge_support_regions && (object_config.support_filament.value == 0 || object_config.support_interface_filament.value == 0)) {
            // One of the support extruders is of "don't care" type.
            auto object_extruders = object.object_extruders();
            if (object_extruders.size() == 1 &&
                *object_extruders.begin() == std::max<unsigned int>(object_config.support_filament.value, object_config.support_interface_filament.value))
                // Object is printed with the same extruder as the support.
                this->can_merge_support_regions = true;
        }


        this->base_angle = Geometry::deg2rad(float(object_config.support_angle.value));
        this->interface_angle = Geometry::deg2rad(float(object_config.support_angle.value + 90.));
        // Orca: Force solid support interface when using support ironing
        this->interface_spacing = (this->ironing ? 0 : object_config.support_interface_spacing.value) + this->support_material_interface_flow.spacing();
        this->interface_density = std::min(1., this->support_material_interface_flow.spacing() / this->interface_spacing);
        // Orca: Force solid support interface when using support ironing
        double raft_interface_spacing = (this->ironing ? 0 : object_config.support_interface_spacing.value) + this->raft_interface_flow.spacing();
        this->raft_interface_density = std::min(1., this->raft_interface_flow.spacing() / raft_interface_spacing);
        if (this->cura_raft_mode)
            this->raft_interface_density = this->raft_surface_density;
        this->support_spacing = object_config.support_base_pattern_spacing.value + this->support_material_flow.spacing();
        this->support_density = std::min(1., this->support_material_flow.spacing() / this->support_spacing);
        if (object_config.support_interface_top_layers.value == 0) {
            // No interface layers allowed, print everything with the base support pattern.
            this->interface_spacing = this->support_spacing;
            this->interface_density = this->support_density;
        }

        SupportMaterialPattern  support_pattern = object_config.support_base_pattern;
        this->with_sheath = object_config.tree_support_wall_count > 0;
        this->base_fill_pattern =
            support_pattern == smpHoneycomb ? ipHoneycomb :
            this->support_density > 0.95 || this->with_sheath ? ipRectilinear : ipSupportBase;
        this->interface_fill_pattern = (this->interface_density > 0.95 ? ipRectilinear : ipSupportBase);
        this->raft_interface_fill_pattern = this->raft_interface_density > 0.95 ? ipRectilinear : ipSupportBase;
        if (object_config.support_interface_pattern == smipGrid)
            this->contact_fill_pattern = ipGrid;
        else if (object_config.support_interface_pattern == smipRectilinearInterlaced)
            this->contact_fill_pattern = ipRectilinear;
        else
            this->contact_fill_pattern =
            (object_config.support_interface_pattern == smipAuto && slicing_params.soluble_interface) ||
            object_config.support_interface_pattern == smipConcentric ?
            ipConcentric :
            (this->interface_density > 0.95 ? ipRectilinear : ipSupportBase);

        this->raft_angle_1st_layer  = 0.f;
        this->raft_angle_base       = 0.f;
        this->raft_angle_interface  = 0.f;
        this->raft_angle_increment  = 0.f;
        if (this->cura_raft_mode) {
            this->raft_angle_1st_layer = Geometry::deg2rad(float(object_config.raft_angle.value));
            this->raft_angle_base = this->raft_angle_1st_layer;
            this->raft_angle_interface = this->raft_angle_1st_layer;
            this->raft_angle_increment = Geometry::deg2rad(float(object_config.raft_angle_increment.value));
        } else if (slicing_params.base_raft_layers > 1) {
            assert(slicing_params.raft_layers() >= 4);
            // There are all raft layer types (1st layer, base, interface & contact layers) available.
            this->raft_angle_1st_layer  = this->interface_angle;
            this->raft_angle_base       = this->base_angle;
            this->raft_angle_interface  = this->interface_angle;
            if ((slicing_params.interface_raft_layers & 1) == 0)
                // Allign the 1st raft interface layer so that the object 1st layer is hatched perpendicularly to the raft contact interface.
                this->raft_angle_interface += float(0.5 * M_PI);
        } else if (slicing_params.base_raft_layers == 1 || slicing_params.interface_raft_layers > 1) {
            assert(slicing_params.raft_layers() == 2 || slicing_params.raft_layers() == 3);
            // 1st layer, interface & contact layers available.
            this->raft_angle_1st_layer  = this->base_angle;
            this->raft_angle_interface  = this->interface_angle + 0.5 * M_PI;
        } else if (slicing_params.interface_raft_layers == 1) {
            // Only the contact raft layer is non-empty, which will be printed as the 1st layer.
            assert(slicing_params.base_raft_layers == 0);
            assert(slicing_params.interface_raft_layers == 1);
            assert(slicing_params.raft_layers() == 1);
            this->raft_angle_1st_layer = float(0.5 * M_PI);
            this->raft_angle_interface = this->raft_angle_1st_layer;
        } else {
            // No raft.
            assert(slicing_params.base_raft_layers == 0);
            assert(slicing_params.interface_raft_layers == 0);
            assert(slicing_params.raft_layers() == 0);
        }

	    const auto     nozzle_diameter = print_config.nozzle_diameter.get_at(object_config.support_interface_filament - 1);
        const coordf_t extrusion_width = object_config.line_width.get_abs_value(nozzle_diameter);
        support_extrusion_width        = object_config.support_line_width.get_abs_value(nozzle_diameter);
        support_extrusion_width        = support_extrusion_width > 0 ? support_extrusion_width : extrusion_width;

        independent_layer_height = print_config.independent_support_layer_height;

        // force double walls everywhere if wall count is larger than 1        
        tree_branch_diameter_double_wall_area_scaled = object_config.tree_support_wall_count.value > 1  ? 0.1 :
                                                       object_config.tree_support_wall_count.value == 0 ? 0.25 * sqr(scaled<double>(5.0)) * M_PI :
                                                                                                          std::numeric_limits<double>::max();

        support_style = object_config.support_style;
        if (support_style != smsDefault) {
            if ((support_style == smsSnug || support_style == smsGrid) && is_tree(object_config.support_type)) support_style = smsDefault;
            if ((support_style == smsTreeSlim || support_style == smsTreeStrong || support_style == smsTreeHybrid || support_style == smsTreeOrganic) &&
                !is_tree(object_config.support_type))
                support_style = smsDefault;
        }
        if (support_style == smsDefault) {
            if (is_tree(object_config.support_type)) {
                // Orca: use organic as default
                support_style = smsTreeOrganic;
            } else {
                support_style = smsGrid;
            }
        }
    }
	// Both top / bottom contacts and interfaces are soluble.
    bool                    soluble_interface;
    // Support contact & interface are soluble, but support base is non-soluble.
    bool                    soluble_interface_non_soluble_base;

    // Is there at least a top contact layer extruded above support base?
    bool                    has_top_contacts;
    // Is there at least a bottom contact layer extruded below support base?
    bool                    has_bottom_contacts;
    // Number of top interface layers without counting the contact layer.
    size_t                  num_top_interface_layers;
    // Number of bottom interface layers without counting the contact layer.
    size_t                  num_bottom_interface_layers;
    // Number of top base interface layers. Zero if not soluble_interface_non_soluble_base.
    size_t                  num_top_base_interface_layers;
    // Number of bottom base interface layers. Zero if not soluble_interface_non_soluble_base.
    size_t                  num_bottom_base_interface_layers;

    bool                    has_contacts() const { return this->has_top_contacts || this->has_bottom_contacts; }
    bool                    has_interfaces() const { return this->num_top_interface_layers + this->num_bottom_interface_layers > 0; }
    bool                    has_base_interfaces() const { return this->num_top_base_interface_layers + this->num_bottom_base_interface_layers > 0; }
    size_t                  num_top_interface_layers_only() const { return this->num_top_interface_layers - this->num_top_base_interface_layers; }
    size_t                  num_bottom_interface_layers_only() const { return this->num_bottom_interface_layers - this->num_bottom_base_interface_layers; }

	// Flow at the 1st print layer.
	Flow 					first_layer_flow;
	// Flow at the support base (neither top, nor bottom interface).
	// Also flow at the raft base with the exception of raft interface and contact layers.
	Flow 					support_material_flow;
	// Flow at the top interface and contact layers.
	Flow 					support_material_interface_flow;
	// Flow at the bottom interfaces and contacts.
	Flow 					support_material_bottom_interface_flow;
	// Flow at raft inteface & contact layers.
	Flow    				raft_interface_flow;
    // Resolved Cura V1 phase geometry. Flow percentages are kept separately
    // and applied only to extrusion volume, so configured width and spacing do
    // not change when material flow is adjusted.
    Flow                    raft_base_flow;
    Flow                    raft_phase_interface_flow;
    Flow                    raft_surface_flow;
    coordf_t                raft_base_flow_ratio { 1. };
    coordf_t                raft_phase_interface_flow_ratio { 1. };
    coordf_t                raft_surface_flow_ratio { 1. };
    coordf_t support_extrusion_width;
	// Is merging of regions allowed? Could the interface & base support regions be printed with the same extruder?
	bool 					can_merge_support_regions;

    coordf_t 				support_layer_height_min;
//	coordf_t				support_layer_height_max;

    coordf_t	gap_xy;
    coordf_t	gap_xy_first_layer;

    float    				base_angle;
    float    				interface_angle;
    coordf_t 				interface_spacing;
    coordf_t				support_expansion=0;
    // Density of the top / bottom interface and contact layers.
    coordf_t 				interface_density;
    // Density of the raft interface and contact layers.
    coordf_t 				raft_interface_density;
    coordf_t               raft_base_density { 0. };
    coordf_t               raft_phase_interface_density { 0. };
    coordf_t               raft_surface_density { 0. };
    coordf_t               raft_base_pattern_spacing { 0. };
    coordf_t               raft_phase_interface_pattern_spacing { 0. };
    coordf_t               raft_surface_pattern_spacing { 0. };
    size_t                 raft_base_wall_count { 0 };
    size_t                 raft_phase_interface_wall_count { 0 };
    size_t                 raft_surface_wall_count { 0 };
    coordf_t 				support_spacing;
    // Density of the base support layers.
    coordf_t 				support_density;
    SupportMaterialStyle    support_style = smsDefault;

    // Pattern of the sparse infill including sparse raft layers.
    InfillPattern           base_fill_pattern;
    // Pattern of the top / bottom interface and contact layers.
    InfillPattern           interface_fill_pattern;
    // Pattern of the raft interface and contact layers.
    InfillPattern           raft_interface_fill_pattern;
    // Pattern of the contact layers.
    InfillPattern 			contact_fill_pattern;
    // Shall the sparse (base) layers be printed with a single perimeter line (sheath) for robustness?
    bool                    with_sheath;
    // Branches of organic supports with area larger than this threshold will be extruded with double lines.
    double                  tree_branch_diameter_double_wall_area_scaled = 0.25 * sqr(scaled<double>(5.0)) * M_PI;;

    float 					raft_angle_1st_layer;
    float 					raft_angle_base;
    float 					raft_angle_interface;
    float                       raft_angle_increment;
    bool                        cura_raft_mode { false };

    float raft_layer_angle(size_t layer_id) const
    {
        if (!cura_raft_mode)
            return layer_id == 0 ? raft_angle_1st_layer : raft_angle_interface;
        float angle = std::fmod(raft_angle_1st_layer + raft_angle_increment * float(layer_id), float(M_PI));
        if (angle < 0.f)
            angle += float(M_PI);
        return angle;
    }

    const Flow &raft_flow(RaftPhase phase) const
    {
        switch (phase) {
        case RaftPhase::Base:      return raft_base_flow;
        case RaftPhase::Interface: return raft_phase_interface_flow;
        case RaftPhase::Surface:   return raft_surface_flow;
        }
        return raft_surface_flow;
    }

    coordf_t raft_density(RaftPhase phase) const
    {
        switch (phase) {
        case RaftPhase::Base:      return raft_base_density;
        case RaftPhase::Interface: return raft_phase_interface_density;
        case RaftPhase::Surface:   return raft_surface_density;
        }
        return raft_surface_density;
    }

    coordf_t raft_flow_ratio(RaftPhase phase) const
    {
        switch (phase) {
        case RaftPhase::Base:      return raft_base_flow_ratio;
        case RaftPhase::Interface: return raft_phase_interface_flow_ratio;
        case RaftPhase::Surface:   return raft_surface_flow_ratio;
        }
        return raft_surface_flow_ratio;
    }

    coordf_t raft_pattern_spacing(RaftPhase phase) const
    {
        switch (phase) {
        case RaftPhase::Base:      return raft_base_pattern_spacing;
        case RaftPhase::Interface: return raft_phase_interface_pattern_spacing;
        case RaftPhase::Surface:   return raft_surface_pattern_spacing;
        }
        return raft_surface_pattern_spacing;
    }

    size_t raft_wall_count(RaftPhase phase) const
    {
        switch (phase) {
        case RaftPhase::Base:      return raft_base_wall_count;
        case RaftPhase::Interface: return raft_phase_interface_wall_count;
        case RaftPhase::Surface:   return raft_surface_wall_count;
        }
        return raft_surface_wall_count;
    }

    InfillPattern raft_fill_pattern(RaftPhase phase) const
    {
        return raft_density(phase) > 0.95 ? ipRectilinear : ipSupportBase;
    }

    // Produce a raft interface angle for a given SupportLayer::interface_id()
    float 					raft_interface_angle(size_t interface_id) const 
    	{ return this->raft_angle_interface + ((interface_id & 1) ? float(- M_PI / 4.) : float(+ M_PI / 4.)); }
		
    bool independent_layer_height = false;
    const double thresh_big_overhang = Slic3r::sqr(scale_(10));

	bool          ironing;
    Flow          ironing_flow; // Flow at the interface ironing.
    InfillPattern ironing_pattern;
    float         ironing_spacing;
};

} // namespace Slic3r

#endif /* slic3r_SupportParameters_hpp_ */
