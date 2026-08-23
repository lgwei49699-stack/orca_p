#ifndef slic3r_FillConcentric_hpp_
#define slic3r_FillConcentric_hpp_

#include "FillBase.hpp"

namespace Slic3r {

class FillConcentric : public Fill
{
public:
    ~FillConcentric() override = default;
    bool is_self_crossing() override { return false; }

    // Generate a single closed variable-width fill contour for a solid-fill
    // surface and return the exact contour left for its inner fill pattern.
    ExPolygons fill_surface_single_wall_extrusion(
        const Surface *surface, const FillParams &params, ExtrusionEntitiesPtr &out);

protected:
    Fill* clone() const override { return new FillConcentric(*this); };
	void _fill_surface_single(
	    const FillParams                &params, 
	    unsigned int                     thickness_layers,
	    const std::pair<float, Point>   &direction, 
	    ExPolygon     		             expolygon,
	    Polylines                       &polylines_out) override;

	void _fill_surface_single(const FillParams& params,
		unsigned int                   thickness_layers,
		const std::pair<float, Point>& direction,
		ExPolygon                      expolygon,
		ThickPolylines& thick_polylines_out) override;

    bool no_sort() const override { return true; }
};

} // namespace Slic3r

#endif // slic3r_FillConcentric_hpp_
