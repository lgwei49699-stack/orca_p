#ifndef slic3r_Utils_ModelRepairPolicy_hpp_
#define slic3r_Utils_ModelRepairPolicy_hpp_

#include "libslic3r/ModelRepair.hpp"

namespace Slic3r {

// Explicit GUI repair is a color-preserving operation, independent of the
// experimental keep_painting preference used by other mesh-editing tools.
// Keep this policy free of wx dependencies so headless tests exercise the
// same options as the GUI, including its partial-result safety gates.
inline ModelRepairOptions gui_model_repair_options()
{
    ModelRepairOptions options;
    options.allow_partial             = true;
    options.preserve_annotations      = true;
    options.allow_annotation_clearing = false;
    return options;
}

} // namespace Slic3r

#endif // slic3r_Utils_ModelRepairPolicy_hpp_
