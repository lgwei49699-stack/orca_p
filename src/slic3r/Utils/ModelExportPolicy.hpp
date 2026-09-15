#ifndef slic3r_Utils_ModelExportPolicy_hpp_
#define slic3r_Utils_ModelExportPolicy_hpp_

#include "libslic3r/Model.hpp"

namespace Slic3r {

enum class GuiModelExportMode { SingleObject, CombinedObjects, SeparateObjects };

// Preserve the legacy GUI STL coordinate convention for both STL and OBJ.
// The offset is in world coordinates, AFTER volume and instance transforms.
// Inspect the scene object before filtering instances for an export snapshot:
// selecting one instance of a multi-instance object must not recenter it.
// CLI exporters deliberately do not use this GUI-only policy.
inline Vec3d gui_model_export_offset(const ModelObject& source, GuiModelExportMode mode)
{
    if (mode == GuiModelExportMode::SeparateObjects || (mode == GuiModelExportMode::SingleObject && source.instances.size() == 1))
        return -source.origin_translation;
    return Vec3d::Zero();
}

} // namespace Slic3r

#endif // slic3r_Utils_ModelExportPolicy_hpp_
