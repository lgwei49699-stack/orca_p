#ifndef libslic3r_MeshRepairOutcome_hpp_
#define libslic3r_MeshRepairOutcome_hpp_

namespace Slic3r {

// A retainable result is not necessarily a completely repaired mesh.
// Rejected never grants permission to use the output candidate.
enum class MeshRepairOutcome { Rejected, Repaired, PartiallyRepaired };

} // namespace Slic3r

#endif // libslic3r_MeshRepairOutcome_hpp_
