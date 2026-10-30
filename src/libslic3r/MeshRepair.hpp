// Repair core ported from OrcaSlicer v2.5.0-beta, commit 345efec64c7b95a05ce5cf96df0dde66a158835c.
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace Slic3r {

class TriangleMesh;

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

// Boundary preparation for an already validated oriented manifold. Reuse the
// annular patcher without welding, removing faces/shells or remeshing. Orient
// the closed result to bound a volume; the caller still validates float output.
// A refused patch/budget leaves mesh unchanged; engine exceptions propagate.
bool close_mesh_holes(TriangleMesh& mesh, std::size_t max_faces);

} // namespace Slic3r
