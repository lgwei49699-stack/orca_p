#ifndef slic3r_ModelInspection_hpp_
#define slic3r_ModelInspection_hpp_

#include <cstddef>
#include <vector>
#include <nlohmann/json.hpp>

namespace Slic3r {
class Model;
class TriangleMesh;

struct ModelInspectionOptions
{
    std::size_t max_triangles        = 1000000;
    std::size_t max_vertices         = 1000000;
    unsigned    max_intersections    = 20000;
    std::size_t max_solid_components = 128;
    std::size_t detail_limit         = 200;
};

// Seven geometry checks and three supplemental checks, on the supplied mesh.
// Exact coordinate canonicalization is diagnostic only; it never edits the mesh.
// Throws on invalid input/resource failures. An incomplete check is never a pass.
nlohmann::json inspect_model_mesh(const TriangleMesh& mesh, const ModelInspectionOptions& options = {});

struct ModelMeshCleanupPlan
{
    std::vector<std::size_t> kept_faces;
    std::size_t              zero_area_faces         = 0;
    std::size_t              same_winding_duplicates = 0;
};

// Geometry-only read-only plan. The transaction decides whether attributes and
// remaining geometry permit applying it; a plan alone is not a safe repair.
ModelMeshCleanupPlan plan_model_mesh_cleanup(const TriangleMesh& mesh);

// Transactional whitelist: exact zero-area faces and same-winding duplicates.
// Annotated volumes and candidates that would erase a volume are retained and
// reported as deferred. No welding, filling, shell splitting or orientation edits.
nlohmann::json prepare_model_for_inspection(Model& model);
} // namespace Slic3r

#endif
