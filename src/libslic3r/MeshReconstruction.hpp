#ifndef slic3r_MeshReconstruction_hpp_
#define slic3r_MeshReconstruction_hpp_

#include <cstddef>
#include <nlohmann/json.hpp>

namespace Slic3r {
class TriangleMesh;

// Shared hard bounds for GUI, standalone STL input, worker protocol and engine.
inline constexpr std::size_t RECONSTRUCTION_MAX_INPUT_FACES  = 3000000;
inline constexpr std::size_t RECONSTRUCTION_MAX_OUTPUT_FACES = 1000000;

struct MeshReconstructionOptions
{
    double alpha_mm  = 0.5;
    double offset_mm = 0.05;
    // Compatibility field name: now a reported warning threshold, not a veto.
    double max_deviation_mm = 0.25;
    // Only within explicit reconstruction: try a validated closed input before
    // wrapping open surfaces. May fill intended openings; callers must disclose it.
    bool        close_open_boundaries = true;
    std::size_t max_input_faces       = RECONSTRUCTION_MAX_INPUT_FACES;
    std::size_t max_output_faces      = RECONSTRUCTION_MAX_OUTPUT_FACES;
};

// Read-only validation of the actual float mesh, including degenerate faces,
// closure, manifoldness, solid orientation, self-intersections and complete
// public model checks. Valid handles and separate solids are allowed.
// This does not compare design intent or run reconstruction.
// The worker owns cancellation, time/memory limits and the supplied face limit.
bool validate_reconstruction_mesh(const TriangleMesh& mesh, nlohmann::json& report, std::size_t face_limit);

// Explicit, approximate reconstruction in physical millimetres. A true result
// is a validated candidate requiring human review, not restored design intent
// or a printability certificate. The caller must isolate this operation in a
// resource-limited, cancelable process. On failure neither mesh is modified.
// Source and candidate must be distinct objects.
bool reconstruct_mesh(const TriangleMesh&              source_mm,
                      TriangleMesh&                    candidate_mm,
                      nlohmann::json&                  report,
                      const MeshReconstructionOptions& options = {});
} // namespace Slic3r

#endif
