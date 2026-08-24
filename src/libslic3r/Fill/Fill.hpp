#ifndef slic3r_Fill_hpp_
#define slic3r_Fill_hpp_

#include <memory.h>
#include <float.h>
#include <stdint.h>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"

#include "FillBase.hpp"

namespace Slic3r {

class ExtrusionEntityCollection;
class LayerRegion;
class PrintObject;

// Resolve the configured fixed/template infill direction for one model layer.
// The returned angle is expressed in radians; fill generators may apply their
// own pattern rotation afterwards.
double calculate_infill_rotation_angle(const PrintObject *object, size_t layer_id, const double &fixed_infill_angle,
                                       const std::string &template_string);

// An interface class to Perl, aggregating an instance of a Fill and a FillData.
class Filler
{
public:
    Filler() : fill(nullptr) {}
    ~Filler() { 
        delete fill; 
        fill = nullptr;
    }
    Fill        *fill;
    FillParams   params;
};

} // namespace Slic3r

#endif // slic3r_Fill_hpp_
