#ifndef slic3r_GUI_Utils_FixModelByCgal_hpp_
#define slic3r_GUI_Utils_FixModelByCgal_hpp_

#include "libslic3r/ModelRepair.hpp"
#include "../GUI/Widgets/ProgressDialog.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Slic3r {

class Model;
class ModelObject;

struct StagedModelRepair
{
    ModelRepairReport      report;
    std::shared_ptr<Model> model;
    std::string            error;
};

// Repairs a private copy of model without modifying the source. Model staging,
// volume structure changes and ObjectBase IDs stay on the GUI thread; only a
// private TriangleMesh enters the CGAL worker. Callers may create an undo
// snapshot and commit the returned volume structure only after the whole
// repair transaction succeeds.
// Returns false only when the user canceled the operation.
bool stage_model_repair_by_cgal_gui(const Model&                          model,
                                    const std::vector<ModelRepairTarget>& targets,
                                    GUI::ProgressDialog&                  progress_dialog,
                                    const wxString&                       message_header,
                                    StagedModelRepair&                    staged_repair,
                                    bool                                  keep_painting);

// Cross-platform replacement for the former Windows SDK helper. This overload
// is used for temporary cut results that are not yet owned by the GUI model.
// It never falls back to the Windows repair service.
bool fix_model_with_cgal_gui(ModelObject&         model_object,
                             int                  volume_idx,
                             GUI::ProgressDialog& progress_dialog,
                             const wxString&      message_header,
                             std::string&         fix_result,
                             bool                 keep_painting,
                             ModelRepairReport*   repair_report = nullptr);

} // namespace Slic3r

#endif // slic3r_GUI_Utils_FixModelByCgal_hpp_
