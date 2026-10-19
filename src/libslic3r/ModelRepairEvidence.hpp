#ifndef slic3r_ModelRepairEvidence_hpp_
#define slic3r_ModelRepairEvidence_hpp_

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "TriangleMeshSlicer.hpp"

namespace Slic3r {
class TriangleMesh;
struct ModelRepairReport;

struct ModelSectionEvidenceOptions
{
    double                spacing_mm = 0.2;
    std::size_t           max_planes = 2000;
    unsigned              timeout_ms = 30000;
    std::function<bool()> is_canceled;
    // A complete, strictly increasing plan in the coordinate system after
    // slicing_params.trafo. Absent means reference midplanes, not print layers.
    std::optional<std::vector<float>> slice_zs;
    MeshSlicingParamsEx               slicing_params;
    // The caller owns the effective configuration/layer-plan identity. Merely
    // supplying sample heights does not establish a printer layer context.
    bool printer_layer_plan = false;
};

struct ModelRepairEvidencePolicy
{
    // Explicit caller contract: this input is intended to be a closed solid
    // and closing residual mesh boundaries is permitted. Never infer this from
    // STL format, object count, or an open-edge count. Default preserves holes.
    bool allow_boundary_closure = false;
};

// Read-only projections. Neither a defect nor section evidence is a decision
// to repair, reconstruct, adopt a candidate, or print. Empty input means not run.
nlohmann::json model_inspection_boundary(const nlohmann::json& inspection);
nlohmann::json model_repair_boundary(const ModelRepairReport& report);

// Coordinates after slicing_params.trafo must be physical millimeters. Makes
// one Z cut and uses ordinary polygon-to-region construction on those contours.
// It does not generate walls, fill, support, or G-code, or change the mesh.
// Only nonsingular, orientation-preserving affine transforms are accepted.
// Mirroring must already be baked with the ordinary slicer's winding handling.
// Limits are cooperative; individual existing slicer calls are not preemptible.
nlohmann::json inspect_model_section_evidence(const TriangleMesh& mesh, const ModelSectionEvidenceOptions& options = {});

// inspection is the actual current mesh inspection. advanced and sections are
// the projections above (or empty when not run). No pipeline is executed here.
nlohmann::json model_check_boundary(const nlohmann::json&            inspection,
                                    const nlohmann::json&            advanced,
                                    const nlohmann::json&            sections,
                                    const std::string&               failure_kind               = "",
                                    const nlohmann::json&            reconstruction_eligibility = {},
                                    const ModelRepairEvidencePolicy& policy                     = {});
nlohmann::json model_reconstruction_boundary(const nlohmann::json& report);
} // namespace Slic3r

#endif
