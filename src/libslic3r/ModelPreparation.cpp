#include "ModelInspection.hpp"
#include "Model.hpp"
#include "ModelRepair.hpp"

#include <string>
#include <vector>

namespace Slic3r {
using json = nlohmann::json;

nlohmann::json prepare_model_for_inspection(Model& model)
{
    // A separate transaction from advanced repair. Fatal validation/staging
    // failures cannot leave half of the input objects prepared.
    auto                     staged = make_model_repair_staging_copy(model, {});
    std::vector<std::size_t> changed;
    json                     volumes      = json::array();
    std::size_t              zero_removed = 0, duplicate_removed = 0;
    for (std::size_t oi = 0; oi < staged->objects.size(); ++oi) {
        auto* object = staged->objects[oi];
        if (object == nullptr)
            continue;
        bool object_changed = false;
        for (std::size_t vi = 0; vi < object->volumes.size(); ++vi) {
            auto* volume = object->volumes[vi];
            if (!volume->is_model_part())
                continue;
            const auto& source = volume->mesh();
            const auto  plan   = plan_model_mesh_cleanup(source);
            const auto  zeros = plan.zero_area_faces, duplicates = plan.same_winding_duplicates;
            if (zeros + duplicates == 0)
                continue;
            const bool annotated = !volume->supported_facets.empty() || !volume->seam_facets.empty() ||
                                   !volume->mmu_segmentation_facets.empty() || !volume->fuzzy_skin_facets.empty() ||
                                   !volume->exterior_facets.empty();
            const std::string deferred = plan.kept_faces.empty() ? "would_erase_volume" :
                                                                   (annotated ? "annotations_require_advanced_repair" : "");
            volumes.push_back({{"object_index", oi},
                               {"volume_index", vi},
                               {"zero_area_candidates", zeros},
                               {"same_winding_duplicate_candidates", duplicates},
                               {"applied", deferred.empty()},
                               {"deferred_reason", deferred}});
            if (!deferred.empty())
                continue;
            indexed_triangle_set cleaned;
            cleaned.vertices = source.its.vertices;
            cleaned.indices.reserve(plan.kept_faces.size());
            for (const auto id : plan.kept_faces)
                cleaned.indices.push_back(source.its.indices[id]);
            its_compactify_vertices(cleaned);
            TriangleMesh mesh(std::move(cleaned));
            mesh.set_init_shift(Vec3d::Zero());
            volume->set_mesh(std::move(mesh));
            volume->set_new_unique_id();
            volume->calculate_convex_hull();
            volume->invalidate_convex_hull_2d();
            object_changed = true;
            zero_removed += zeros;
            duplicate_removed += duplicates;
        }
        if (object_changed)
            changed.push_back(oi);
    }
    json result = {{"policy", "exact_cleanup_v1"},
                   {"changed", !changed.empty()},
                   {"zero_area_faces_removed", zero_removed},
                   {"same_winding_duplicate_faces_removed", duplicate_removed},
                   {"volumes", std::move(volumes)}};
    if (!changed.empty()) {
        auto prepared = model.prepare_move_object_volume_structures(*staged, changed);
        model.commit_move_object_volume_structures(std::move(prepared));
    }
    return result;
}
} // namespace Slic3r
