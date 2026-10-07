#ifndef slic3r_GUI_ModelReconstructionDialog_hpp_
#define slic3r_GUI_ModelReconstructionDialog_hpp_

class wxWindow;

namespace Slic3r {
class ModelObject;
struct ModelReconstructionOptions;
struct ModelReconstructionResult;

namespace GUI {

bool show_model_reconstruction_settings(wxWindow* parent, ModelReconstructionOptions& options);
bool show_model_reconstruction_preview(wxWindow*                        parent,
                                       const ModelObject&               original,
                                       const ModelObject&               candidate,
                                       const ModelReconstructionResult& result,
                                       double                           deviation_limit_mm,
                                       bool&                            place_on_bed);

} // namespace GUI
} // namespace Slic3r

#endif
