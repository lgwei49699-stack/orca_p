#ifndef slic3r_RaftAngleResolver_hpp_
#define slic3r_RaftAngleResolver_hpp_

namespace Slic3r {

class PrintObject;

// Resolve the CuraV1 final Surface input angle in degrees. Directional model
// L1 bottom patterns are kept perpendicular to the final Raft Surface. If L1
// has no unique directional angle, the stable Cura-compatible fallback is
// returned.
double resolve_cura_raft_surface_angle(const PrintObject &object, double fallback_angle = 135.0);

} // namespace Slic3r

#endif /* slic3r_RaftAngleResolver_hpp_ */
