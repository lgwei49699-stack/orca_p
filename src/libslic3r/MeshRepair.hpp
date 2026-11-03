// Repair core ported from OrcaSlicer v2.5.0-beta, commit 345efec64c7b95a05ce5cf96df0dde66a158835c.
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace Slic3r {

class TriangleMesh;

// Explicit reconstruction input only; caller validates storage and works on a
// private copy. Keep one exact coincident face and orient it with its neighbors.
// No caps, proximity welding, remeshing or vertex movement. Returns removed faces.
std::size_t normalize_reconstruction_duplicates(TriangleMesh& mesh);

struct MeshRepairStats
{
    std::size_t welded_vertices    = 0;
    std::size_t removed_triangles  = 0;
    std::size_t reversed_shells    = 0;
    std::size_t filled_holes       = 0;
    std::size_t removed_shells     = 0;
    std::size_t self_intersections = 0;
    // False when the input already was closed, consistently oriented and free of self-intersections.
    bool modified = false;
};

// Returns false, leaving the mesh untouched, if no solid can be built. progress gets the percentage done and may
// throw to cancel. Closed output may still contain intersecting shells; callers
// must inspect it separately before claiming full mesh validity.
bool repair_mesh(TriangleMesh&                   mesh,
                 MeshRepairStats*                stats    = nullptr,
                 std::string*                    error    = nullptr,
                 const std::function<void(int)>& progress = {});

} // namespace Slic3r
