#include "ModelRepair.hpp"

#include "AABBTreeIndirect.hpp"
#include "MeshBoolean.hpp"
#include "Model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Slic3r {

namespace {

bool has_finite_vertices(const TriangleMesh& mesh)
{
    for (const stl_vertex& vertex : mesh.its.vertices)
        if (!vertex.allFinite())
            return false;
    return true;
}

bool has_valid_indices(const TriangleMesh& mesh)
{
    for (const stl_triangle_vertex_indices& face : mesh.its.indices) {
        for (size_t i = 0; i < 3; ++i) {
            const int vertex_index = face[i];
            if (vertex_index < 0 || size_t(vertex_index) >= mesh.its.vertices.size())
                return false;
        }
    }
    return true;
}

// Equivalent in intent to the latest OrcaSlicer CGAL wrapper. Degenerate
// faces are ignored for diagnosis, while the original mesh is retained until
// the transaction commits.
bool is_not_3dimensional_part(const TriangleMesh& mesh)
{
    if (mesh.its.indices.empty())
        return true;

    indexed_triangle_set cleaned = mesh.its;
    its_remove_degenerate_faces(cleaned, true);
    cleaned.indices.erase(std::remove_if(cleaned.indices.begin(), cleaned.indices.end(),
                                         [&cleaned](const auto& face) {
                                             const Vec3d a = cleaned.vertices[size_t(face[0])].cast<double>();
                                             const Vec3d b = cleaned.vertices[size_t(face[1])].cast<double>();
                                             const Vec3d c = cleaned.vertices[size_t(face[2])].cast<double>();
                                             return (b - a).cross(c - a).squaredNorm() <= std::numeric_limits<double>::epsilon();
                                         }),
                          cleaned.indices.end());
    if (cleaned.indices.empty())
        return true;
    its_compactify_vertices(cleaned, true);

    const BoundingBoxf3 bbox    = Slic3r::bounding_box(cleaned);
    const Vec3d         size    = bbox.size();
    const double        min_dim = std::min(size.x(), std::min(size.y(), size.z()));
    const double        max_dim = std::max(size.x(), std::max(size.y(), size.z()));
    if (min_dim <= EPSILON)
        return true;

    // Do not use signed mesh volume here. A genuinely three-dimensional shell
    // with locally reversed faces may have canceling signed contributions and
    // is exactly the kind of input CGAL should orient and repair. Measuring
    // absolute tetrahedra from a mesh vertex is orientation-independent while
    // remaining zero for coplanar geometry.
    const Vec3d reference                   = cleaned.vertices[size_t(cleaned.indices.front()[0])].cast<double>();
    double      absolute_tetrahedral_volume = 0.0;
    for (const stl_triangle_vertex_indices& face : cleaned.indices) {
        const Vec3d a = cleaned.vertices[size_t(face[0])].cast<double>() - reference;
        const Vec3d b = cleaned.vertices[size_t(face[1])].cast<double>() - reference;
        const Vec3d c = cleaned.vertices[size_t(face[2])].cast<double>() - reference;
        absolute_tetrahedral_volume += std::abs(a.dot(b.cross(c))) / 6.0;
    }
    const double bbox_volume = size.x() * size.y() * size.z();
    if (absolute_tetrahedral_volume <= EPSILON)
        return true;

    constexpr double min_relative_thickness = 1e-6;
    constexpr double min_volume_ratio       = 1e-6;
    if (min_dim / max_dim <= min_relative_thickness)
        return true;
    if (bbox_volume > 0.0 && absolute_tetrahedral_volume / bbox_volume <= min_volume_ratio)
        return true;

    return false;
}

bool target_selected(const ModelRepairOptions& options, size_t object_index, size_t volume_index)
{
    if (options.targets.empty())
        return true;
    for (const ModelRepairTarget& target : options.targets)
        if (target.object_index == object_index && (!target.volume_index || *target.volume_index == volume_index))
            return true;
    return false;
}

bool mesh_requires_repair(const TriangleMesh& mesh, size_t open_edges, const ModelRepairOptions& options)
{
    if (open_edges != 0)
        return true;
    return options.mesh_requires_repair ? options.mesh_requires_repair(mesh) : MeshBoolean::cgal::requires_repair(mesh);
}

bool has_preservable_annotations(const ModelVolume& volume)
{
    return !volume.supported_facets.empty() || !volume.seam_facets.empty() || !volume.mmu_segmentation_facets.empty() ||
           !volume.fuzzy_skin_facets.empty();
}

// TriangleMesh::split() normalizes every disconnected component to positive
// volume. That is correct for separate solids, but it turns an inward-oriented
// nested shell (a cavity) into a positive solid. Be deliberately conservative:
// split only components whose axis-aligned bounds are separated by a positive
// gap. Nested, touching, intersecting, and geometrically interlocked shells
// stay in one volume and are inspected/repaired together, preserving their
// nesting-aware orientation.
bool has_only_safely_separable_components(const TriangleMesh& mesh, size_t& component_count)
{
    std::vector<indexed_triangle_set> components = its_split(mesh.its);
    component_count                              = components.size();
    if (components.size() <= 1)
        return false;

    std::vector<BoundingBoxf3> bounds;
    bounds.reserve(components.size());
    for (const indexed_triangle_set& component : components) {
        if (component.vertices.empty() || component.indices.empty())
            return false;
        bounds.emplace_back(Slic3r::bounding_box(component));
    }

    // Sweep along X so the common "many disconnected dust components" case
    // does not compare every pair. Only X-overlapping bounds can possibly be
    // nested, touching or intersecting and therefore need the Y/Z checks.
    std::vector<const BoundingBoxf3*> sorted_bounds;
    sorted_bounds.reserve(bounds.size());
    for (const BoundingBoxf3& bound : bounds)
        sorted_bounds.emplace_back(&bound);
    std::sort(sorted_bounds.begin(), sorted_bounds.end(),
              [](const BoundingBoxf3* lhs, const BoundingBoxf3* rhs) { return lhs->min.x() < rhs->min.x(); });

    for (size_t first = 0; first < sorted_bounds.size(); ++first) {
        const BoundingBoxf3& a = *sorted_bounds[first];
        for (size_t second = first + 1; second < sorted_bounds.size(); ++second) {
            const BoundingBoxf3& b = *sorted_bounds[second];
            if (a.max.x() < b.min.x())
                break;
            const bool separated_yz = a.max.y() < b.min.y() || b.max.y() < a.min.y() || a.max.z() < b.min.z() || b.max.z() < a.min.z();
            if (!separated_yz)
                return false;
        }
    }
    return true;
}

struct PaintedCoverage
{
    double area   = 0.0;
    size_t facets = 0;
};

PaintedCoverage painted_coverage(const TriangleSelector& selector, const TriangleSelector::TriangleSplittingData& painting)
{
    PaintedCoverage coverage;
    for (size_t state_index = 1; state_index < painting.used_states.size(); ++state_index) {
        if (!painting.used_states[state_index])
            continue;
        const indexed_triangle_set painted_facets = selector.get_facets_strict(static_cast<EnforcerBlockerType>(state_index));
        coverage.facets += painted_facets.indices.size();
        for (const stl_triangle_vertex_indices& face : painted_facets.indices) {
            const Vec3d a = painted_facets.vertices[size_t(face[0])].cast<double>();
            const Vec3d b = painted_facets.vertices[size_t(face[1])].cast<double>();
            const Vec3d c = painted_facets.vertices[size_t(face[2])].cast<double>();
            coverage.area += 0.5 * (b - a).cross(c - a).norm();
        }
    }
    return coverage;
}

template<class Annotation>
bool saved_annotation_was_restored(const std::shared_ptr<const TriangleMesh>&     source_mesh,
                                   const TriangleSelector::TriangleSplittingData& saved,
                                   const std::vector<ModelVolume*>&               volumes,
                                   Annotation&&                                   annotation)
{
    if (saved.bitstream.empty())
        return true;
    if (source_mesh == nullptr)
        return false;

    TriangleSelector source_selector(*source_mesh);
    source_selector.deserialize(saved, false);
    const PaintedCoverage source_coverage = painted_coverage(source_selector, saved);
    if (source_coverage.facets == 0)
        return false;

    double restored_area = 0.0;
    for (size_t state_index = 1; state_index < saved.used_states.size(); ++state_index) {
        if (!saved.used_states[state_index])
            continue;
        const EnforcerBlockerType  state         = static_cast<EnforcerBlockerType>(state_index);
        const indexed_triangle_set source_facets = source_selector.get_facets_strict(state);

        TriangleMesh target_facets;
        for (const ModelVolume* volume : volumes) {
            if (volume == nullptr)
                continue;
            const FacetsAnnotation& target_annotation = annotation(*volume);
            if (target_annotation.empty())
                continue;
            TriangleMesh target_piece(target_annotation.get_facets_strict(*volume, state));
            if (target_piece.empty())
                continue;
            // An unchanged staged volume still shares the saved mesh and is
            // already in the same coordinates. Split/repaired volumes use the
            // init shift to map their local coordinates back to the source.
            if (volume->mesh_ptr() != source_mesh)
                target_piece.transform(Geometry::translation_transform(volume->mesh().get_init_shift()));
            target_facets.merge(target_piece);
        }
        if (target_facets.empty())
            continue;

        const AABBTreeIndirect::Tree3f target_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(target_facets.its.vertices,
                                                                                                                 target_facets.its.indices);
        // One tenth of TriangleSelector's broad remap tolerance: enough for
        // float/retriangulation noise, but not for a nearby opposite wall.
        constexpr float point_tolerance_squared = 1e-6f;
        for (const stl_triangle_vertex_indices& face : source_facets.indices) {
            const Vec3f& a    = source_facets.vertices[size_t(face[0])];
            const Vec3f& b    = source_facets.vertices[size_t(face[1])];
            const Vec3f& c    = source_facets.vertices[size_t(face[2])];
            const double area = 0.5 * double((b - a).cross(c - a).norm());
            if (area <= std::numeric_limits<double>::epsilon())
                continue;

            const std::array<Vec3f, 7> samples         = {a, b, c, (a + b) * 0.5f, (b + c) * 0.5f, (c + a) * 0.5f, (a + b + c) / 3.f};
            size_t                     covered_samples = 0;
            for (const Vec3f& sample : samples) {
                size_t      hit_index = 0;
                Vec3f       hit_point;
                const float squared_distance = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(target_facets.its.vertices,
                                                                                                          target_facets.its.indices,
                                                                                                          target_tree, sample, hit_index,
                                                                                                          hit_point);
                if (squared_distance >= 0.f && squared_distance <= point_tolerance_squared)
                    ++covered_samples;
            }
            restored_area += area * double(covered_samples) / double(samples.size());
        }
    }

    // Retriangulation may move or split facets slightly. Spatial sampling is
    // more meaningful than serialized leaf counts and prevents excess paint on
    // one component from hiding a different painted component that vanished.
    constexpr double relative_tolerance = 1e-3;
    constexpr double absolute_tolerance = 1e-8;
    return restored_area + std::max(absolute_tolerance, source_coverage.area * relative_tolerance) >= source_coverage.area;
}

bool saved_painting_was_restored(const TriangleSelector::SavedPainting& saved, const std::vector<ModelVolume*>& volumes)
{
    if (saved.mesh == nullptr)
        return saved.supported.bitstream.empty() && saved.seam.bitstream.empty() && saved.mmu.bitstream.empty() &&
               saved.fuzzy.bitstream.empty();

    return saved_annotation_was_restored(saved.mesh, saved.supported, volumes,
                                         [](const ModelVolume& volume) -> const FacetsAnnotation& { return volume.supported_facets; }) &&
           saved_annotation_was_restored(saved.mesh, saved.seam, volumes,
                                         [](const ModelVolume& volume) -> const FacetsAnnotation& { return volume.seam_facets; }) &&
           saved_annotation_was_restored(saved.mesh, saved.mmu, volumes,
                                         [](const ModelVolume& volume) -> const FacetsAnnotation& {
                                             return volume.mmu_segmentation_facets;
                                         }) &&
           saved_annotation_was_restored(saved.mesh, saved.fuzzy, volumes,
                                         [](const ModelVolume& volume) -> const FacetsAnnotation& { return volume.fuzzy_skin_facets; });
}

bool saved_painting_was_restored(const TriangleSelector::SavedPainting& saved,
                                 const std::vector<ModelVolume*>&        volumes,
                                 const ModelRepairOptions&               options)
{
    const std::function<bool()> validation = [&saved, &volumes]() { return saved_painting_was_restored(saved, volumes); };
    return options.run_painting_validation ? options.run_painting_validation(validation) : validation();
}

void clear_preservable_annotations(const std::vector<ModelVolume*>& volumes)
{
    for (ModelVolume* volume : volumes)
        if (volume != nullptr)
            volume->reset_extra_facets();
}

struct RepairTarget
{
    size_t       object_index  = 0;
    size_t       volume_index  = 0;
    ModelVolume* staged_volume = nullptr;
    std::string  object_name;
    std::string  volume_name;
    size_t       before_facets     = 0;
    size_t       before_open_edges = 0;
};

void add_failure(ModelRepairReport& report, ModelRepairVolumeReport& volume_report, const std::string& message)
{
    volume_report.result           = ModelRepairVolumeStatus::Failed;
    volume_report.after_facets     = volume_report.before_facets;
    volume_report.after_open_edges = volume_report.before_open_edges;
    volume_report.message          = message;
    report.failures.push_back(
        {volume_report.object_index, volume_report.volume_index, volume_report.object_name, volume_report.volume_name, message});
    ++report.failed;
}

void mark_rolled_back(ModelRepairReport& report)
{
    for (ModelRepairVolumeReport& volume_report : report.volume_reports) {
        if (volume_report.result != ModelRepairVolumeStatus::Repaired)
            continue;
        volume_report.result                = ModelRepairVolumeStatus::RolledBack;
        volume_report.after_facets          = volume_report.before_facets;
        volume_report.after_open_edges      = volume_report.before_open_edges;
        volume_report.message               = "Repair was rolled back because the model transaction failed.";
        volume_report.annotations_cleared   = false;
        volume_report.annotations_preserved = false;
        ++report.rolled_back;
    }

    report.repaired         = 0;
    report.mesh_repaired    = 0;
    report.split_volumes    = 0;
    report.created_parts    = 0;
    report.removed_parts    = 0;
    report.committed        = false;
    report.after_facets     = report.before_facets;
    report.after_open_edges = report.before_open_edges;
    report.changed_object_indices.clear();
}

void add_staging_failure(ModelRepairReport& report, const std::string& message)
{
    report.failures.push_back({0, 0, {}, {}, message});
    ++report.failed;
    report.success = false;
}

} // namespace

std::unique_ptr<Model> make_model_repair_staging_copy(const Model& model, const std::vector<ModelRepairTarget>& targets)
{
    std::vector<size_t> object_indices;
    if (targets.empty()) {
        object_indices.reserve(model.objects.size());
        for (size_t object_index = 0; object_index < model.objects.size(); ++object_index)
            if (model.objects[object_index] != nullptr)
                object_indices.push_back(object_index);
    } else {
        object_indices.reserve(targets.size());
        for (const ModelRepairTarget& target : targets)
            object_indices.push_back(target.object_index);
        std::sort(object_indices.begin(), object_indices.end());
        object_indices.erase(std::unique(object_indices.begin(), object_indices.end()), object_indices.end());
    }
    return model.make_object_staging_copy(object_indices);
}

const char* model_repair_volume_status(ModelRepairVolumeStatus status)
{
    switch (status) {
    case ModelRepairVolumeStatus::Skipped: return "skipped";
    case ModelRepairVolumeStatus::Repaired: return "repaired";
    case ModelRepairVolumeStatus::RolledBack: return "rolled_back";
    case ModelRepairVolumeStatus::Failed: return "failed";
    }
    return "failed";
}

const char* ModelRepairReport::status() const
{
    if (canceled)
        return "canceled";
    if (!success)
        return "failed";
    if (committed)
        return "repaired";
    return "not_needed";
}

static bool repair_staged_model_impl(Model& model, ModelRepairReport& report, const ModelRepairOptions& options)
{
    report = ModelRepairReport{};

    const auto cancellation_requested = [&]() {
        if (!options.is_canceled || !options.is_canceled())
            return false;
        mark_rolled_back(report);
        report.canceled = true;
        report.success  = false;
        return true;
    };
    // Avoid target enumeration and open-edge scans when cancellation was
    // already requested before the transaction started.
    if (cancellation_requested())
        return false;

    std::vector<RepairTarget> targets;
    std::vector<bool>         selected_objects(model.objects.size(), false);
    for (size_t object_index = 0; object_index < model.objects.size(); ++object_index) {
        const ModelObject* object = model.objects[object_index];
        if (object == nullptr)
            continue;
        for (size_t volume_index = 0; volume_index < object->volumes.size(); ++volume_index) {
            const ModelVolume* volume = object->volumes[volume_index];
            if (volume == nullptr || !volume->is_model_part() || !target_selected(options, object_index, volume_index))
                continue;

            RepairTarget target;
            target.object_index  = object_index;
            target.volume_index  = volume_index;
            target.object_name   = object->name;
            target.volume_name   = volume->name;
            target.before_facets = volume->mesh().its.indices.size();
            if (has_valid_indices(volume->mesh()))
                target.before_open_edges = its_num_open_edges(volume->mesh().its);
            targets.emplace_back(std::move(target));
            selected_objects[object_index] = true;
        }
    }

    report.volumes = targets.size();
    report.objects = std::count(selected_objects.begin(), selected_objects.end(), true);
    for (const RepairTarget& target : targets) {
        report.before_facets += target.before_facets;
        report.before_open_edges += target.before_open_edges;
    }
    report.after_facets     = report.before_facets;
    report.after_open_edges = report.before_open_edges;

    if (targets.empty()) {
        report.success = true;
        return true;
    }

    for (RepairTarget& target : targets) {
        if (target.object_index >= model.objects.size() || model.objects[target.object_index] == nullptr ||
            target.volume_index >= model.objects[target.object_index]->volumes.size()) {
            add_staging_failure(report, "Could not map selected model volumes into the staged repair model.");
            return false;
        }
        target.staged_volume = model.objects[target.object_index]->volumes[target.volume_index];
    }

    std::vector<bool> changed_objects(model.objects.size(), false);
    size_t            completed_volumes = 0;

    const auto volume_finished = [&](const ModelRepairVolumeReport& volume_report) {
        ++completed_volumes;
        if (options.on_progress)
            options.on_progress(completed_volumes, targets.size(), volume_report);
    };

    for (const RepairTarget& target : targets) {
        if (cancellation_requested())
            return false;

        ModelRepairVolumeReport volume_report;
        volume_report.object_index      = target.object_index;
        volume_report.volume_index      = target.volume_index;
        volume_report.object_name       = target.object_name;
        volume_report.volume_name       = target.volume_name;
        volume_report.before_facets     = target.before_facets;
        volume_report.after_facets      = target.before_facets;
        volume_report.before_open_edges = target.before_open_edges;
        volume_report.after_open_edges  = target.before_open_edges;
        report.volume_reports.emplace_back(std::move(volume_report));
        ModelRepairVolumeReport& stored_report = report.volume_reports.back();

        if (target.object_index >= model.objects.size() || model.objects[target.object_index] == nullptr) {
            add_failure(report, stored_report, "Repair failed: selected model object disappeared during staging.");
            volume_finished(stored_report);
            mark_rolled_back(report);
            return false;
        }

        ModelObject* object    = model.objects[target.object_index];
        const auto   volume_it = std::find(object->volumes.begin(), object->volumes.end(), target.staged_volume);
        if (volume_it == object->volumes.end()) {
            add_failure(report, stored_report, "Repair failed: selected model volume disappeared during staging.");
            volume_finished(stored_report);
            mark_rolled_back(report);
            return false;
        }

        const size_t        volume_index = size_t(volume_it - object->volumes.begin());
        ModelVolume*        volume       = *volume_it;
        const TriangleMesh& mesh         = volume->mesh();
        if (!has_valid_indices(mesh)) {
            stored_report.attempted = true;
            report.attempted        = true;
            add_failure(report, stored_report, "Repair failed: model-part volume contains an invalid vertex index.");
            volume_finished(stored_report);
            mark_rolled_back(report);
            return false;
        }
        if (!has_finite_vertices(mesh)) {
            stored_report.attempted = true;
            report.attempted        = true;
            add_failure(report, stored_report, "Repair failed: model-part volume contains non-finite vertices.");
            volume_finished(stored_report);
            mark_rolled_back(report);
            return false;
        }
        if (mesh.its.indices.empty()) {
            stored_report.attempted = true;
            report.attempted        = true;
            add_failure(report, stored_report, "Repair failed: model-part volume is empty.");
            volume_finished(stored_report);
            mark_rolled_back(report);
            return false;
        }

        bool   split_volume            = false;
        bool   remove_volume           = false;
        bool   repair_volume           = false;
        size_t disconnected_part_count = 1;
        try {
            if (options.split_components && !mesh.its.indices.empty() && volume->is_splittable())
                split_volume = has_only_safely_separable_components(mesh, disconnected_part_count);
            remove_volume = options.remove_non_3d_parts && !split_volume && is_not_3dimensional_part(mesh);
            if (!split_volume && !remove_volume && !mesh.its.indices.empty())
                repair_volume = mesh_requires_repair(mesh, stored_report.before_open_edges, options);
        } catch (const std::exception& exception) {
            stored_report.attempted = true;
            report.attempted        = true;
            add_failure(report, stored_report, std::string("Could not inspect model-part volume: ") + exception.what());
            volume_finished(stored_report);
            mark_rolled_back(report);
            return false;
        } catch (...) {
            stored_report.attempted = true;
            report.attempted        = true;
            add_failure(report, stored_report, "Could not inspect model-part volume.");
            volume_finished(stored_report);
            mark_rolled_back(report);
            return false;
        }

        if (cancellation_requested()) {
            volume_finished(stored_report);
            return false;
        }

        if (!split_volume && !remove_volume && !repair_volume) {
            ++report.skipped;
            volume_finished(stored_report);
            if (cancellation_requested())
                return false;
            continue;
        }

        stored_report.attempted = true;
        report.attempted        = true;

        const bool                                           had_preservable_annotations = has_preservable_annotations(*volume);
        const bool                                           had_exterior_annotations    = !volume->exterior_facets.empty();
        const bool                                           had_annotations = had_preservable_annotations || had_exterior_annotations;
        const std::optional<TriangleSelector::SavedPainting> source_painting = options.preserve_annotations && had_preservable_annotations ?
                                                                                   volume->save_painting() :
                                                                                   std::nullopt;
        if (had_annotations && !options.preserve_annotations && !options.allow_annotation_clearing) {
            add_failure(report, stored_report,
                        "Repair would invalidate painted facet annotations; enable painting preservation or allow annotation clearing.");
            volume_finished(stored_report);
            mark_rolled_back(report);
            return false;
        }
        if (had_exterior_annotations && options.preserve_annotations && !options.allow_annotation_clearing) {
            add_failure(report, stored_report,
                        "Repair cannot preserve exterior-facet annotations; allow annotation clearing or remove those annotations first.");
            volume_finished(stored_report);
            mark_rolled_back(report);
            return false;
        }

        bool                      changed                        = false;
        bool                      annotation_preservation_failed = false;
        std::vector<ModelVolume*> parts;
        try {
            size_t parts_count = 1;
            if (split_volume) {
                parts_count = volume->split(1, options.preserve_annotations);
                ++report.split_volumes;
                report.created_parts += parts_count > 1 ? parts_count - 1 : 0;
                const size_t removed_during_split = disconnected_part_count > parts_count ? disconnected_part_count - parts_count : 0;
                stored_report.removed_parts += removed_during_split;
                report.removed_parts += removed_during_split;
                changed = true;
            }
            stored_report.split_parts = split_volume ? disconnected_part_count : parts_count;

            const size_t available_parts = volume_index < object->volumes.size() ?
                                               std::min(parts_count, object->volumes.size() - volume_index) :
                                               0;
            parts.reserve(available_parts);
            for (size_t part_index = 0; part_index < available_parts; ++part_index)
                parts.push_back(object->volumes[volume_index + part_index]);

            if (options.remove_non_3d_parts) {
                for (size_t part_offset = parts.size(); part_offset > 0; --part_offset) {
                    ModelVolume* part = parts[part_offset - 1];
                    if (!is_not_3dimensional_part(part->mesh()))
                        continue;
                    const auto part_it = std::find(object->volumes.begin(), object->volumes.end(), part);
                    if (part_it == object->volumes.end())
                        throw std::runtime_error("A split model part disappeared during cleanup.");
                    object->delete_volume(size_t(part_it - object->volumes.begin()));
                    parts.erase(parts.begin() + (part_offset - 1));
                    ++stored_report.removed_parts;
                    ++report.removed_parts;
                    changed = true;
                }
            }

            bool source_painting_restored = true;
            if (source_painting) {
                source_painting_restored = saved_painting_was_restored(*source_painting, parts, options);
                if (cancellation_requested()) {
                    stored_report.result  = ModelRepairVolumeStatus::RolledBack;
                    stored_report.message = "Repair was canceled during painted-facet validation.";
                    ++report.rolled_back;
                    mark_rolled_back(report);
                    volume_finished(stored_report);
                    return false;
                }
            }
            if (!source_painting_restored) {
                if (!options.allow_annotation_clearing)
                    throw std::runtime_error(
                        "Repair failed: painted facet annotations could not be mapped onto the remaining model parts.");
                clear_preservable_annotations(parts);
                annotation_preservation_failed = true;
            }

            for (ModelVolume* part : parts) {
                const TriangleMesh& part_mesh = part->mesh();
                if (part_mesh.its.vertices.empty() || part_mesh.its.indices.empty() || !has_valid_indices(part_mesh) ||
                    !has_finite_vertices(part_mesh))
                    throw std::runtime_error("Repair failed: split produced an invalid model part.");

                const size_t part_open_edges      = its_num_open_edges(part_mesh.its);
                const bool   part_requires_repair = mesh_requires_repair(part_mesh, part_open_edges, options);
                if (cancellation_requested()) {
                    stored_report.result  = ModelRepairVolumeStatus::RolledBack;
                    stored_report.message = "Repair was canceled during model inspection.";
                    ++report.rolled_back;
                    mark_rolled_back(report);
                    volume_finished(stored_report);
                    return false;
                }
                if (!part_requires_repair)
                    continue;

                const bool part_has_preservable_annotations = has_preservable_annotations(*part);
                const bool part_has_exterior_annotations    = !part->exterior_facets.empty();
                if (part_has_exterior_annotations && options.preserve_annotations && !options.allow_annotation_clearing)
                    throw std::runtime_error("Repair cannot preserve exterior-facet annotations.");

                std::optional<TriangleSelector::SavedPainting> saved_painting;
                if (options.preserve_annotations && part_has_preservable_annotations)
                    saved_painting = part->save_painting();

                TriangleMesh repaired_mesh;
                std::string  error;
                bool         repair_succeeded = false;
                if (options.repair_mesh_from_source) {
                    repair_succeeded = options.repair_mesh_from_source(part_mesh, repaired_mesh, error);
                } else {
                    // Headless and legacy hooks retain their original
                    // synchronous semantics. GUI callers use the source-to-
                    // result hook above so this O(mesh) copy runs on a worker.
                    repaired_mesh    = part_mesh;
                    repair_succeeded = options.repair_mesh ? options.repair_mesh(repaired_mesh, error) :
                                                             MeshBoolean::cgal::repair(repaired_mesh, nullptr, &error);
                }
                if (cancellation_requested()) {
                    stored_report.result                = ModelRepairVolumeStatus::RolledBack;
                    stored_report.after_facets          = stored_report.before_facets;
                    stored_report.after_open_edges      = stored_report.before_open_edges;
                    stored_report.message               = "Repair was canceled before the repaired mesh could be committed.";
                    stored_report.annotations_cleared   = false;
                    stored_report.annotations_preserved = false;
                    ++report.rolled_back;
                    mark_rolled_back(report);
                    volume_finished(stored_report);
                    return false;
                }
                if (!repair_succeeded)
                    throw std::runtime_error(error.empty() ? "Repair failed." : error);

                const size_t repaired_open_edges = its_num_open_edges(repaired_mesh.its);
                if (repaired_mesh.its.vertices.empty() || repaired_mesh.its.indices.empty() || !has_valid_indices(repaired_mesh) ||
                    !has_finite_vertices(repaired_mesh) || repaired_open_edges != 0)
                    throw std::runtime_error("Repair failed: CGAL returned an invalid or open mesh.");
                // Reuse the inspection hook for the post-repair gate. GUI
                // callers run this full CGAL validation on their worker,
                // while headless callers retain the direct implementation.
                const bool repaired_mesh_requires_repair = mesh_requires_repair(repaired_mesh, repaired_open_edges, options);
                if (cancellation_requested()) {
                    stored_report.result                = ModelRepairVolumeStatus::RolledBack;
                    stored_report.after_facets          = stored_report.before_facets;
                    stored_report.after_open_edges      = stored_report.before_open_edges;
                    stored_report.message               = "Repair was canceled during repaired mesh validation.";
                    stored_report.annotations_cleared   = false;
                    stored_report.annotations_preserved = false;
                    ++report.rolled_back;
                    mark_rolled_back(report);
                    volume_finished(stored_report);
                    return false;
                }
                if (repaired_mesh_requires_repair)
                    throw std::runtime_error(
                        "Repair failed: repaired mesh did not pass full topology, self-intersection, and orientation validation.");

                // CGAL edits vertices in the current volume-local coordinate
                // system. Keeping the import shift would apply it twice while
                // remapping SavedPainting.
                repaired_mesh.set_init_shift(Vec3d::Zero());
                part->set_mesh(std::move(repaired_mesh));
                part->calculate_convex_hull();
                part->invalidate_convex_hull_2d();
                if (options.preserve_annotations) {
                    part->restore_painting(saved_painting);
                    bool repaired_painting_restored = true;
                    if (saved_painting) {
                        repaired_painting_restored =
                            saved_painting_was_restored(*saved_painting, std::vector<ModelVolume*>{part}, options);
                        if (cancellation_requested()) {
                            stored_report.result                = ModelRepairVolumeStatus::RolledBack;
                            stored_report.after_facets          = stored_report.before_facets;
                            stored_report.after_open_edges      = stored_report.before_open_edges;
                            stored_report.message               = "Repair was canceled during painted-facet validation.";
                            stored_report.annotations_cleared   = false;
                            stored_report.annotations_preserved = false;
                            ++report.rolled_back;
                            mark_rolled_back(report);
                            volume_finished(stored_report);
                            return false;
                        }
                    }
                    if (!repaired_painting_restored) {
                        if (!options.allow_annotation_clearing)
                            throw std::runtime_error(
                                "Repair failed: painted facet annotations could not be mapped onto the repaired mesh.");
                        part->reset_extra_facets();
                        annotation_preservation_failed = true;
                    }
                } else {
                    part->reset_extra_facets();
                }
                part->exterior_facets.reset();
                part->set_new_unique_id();

                ++stored_report.repaired_parts;
                ++report.mesh_repaired;
                changed = true;
            }
        } catch (const std::exception& exception) {
            add_failure(report, stored_report, exception.what());
            volume_finished(stored_report);
            mark_rolled_back(report);
            return false;
        } catch (...) {
            add_failure(report, stored_report, "Repair failed with an unknown error.");
            volume_finished(stored_report);
            mark_rolled_back(report);
            return false;
        }

        stored_report.after_facets     = 0;
        stored_report.after_open_edges = 0;
        for (const ModelVolume* part : parts) {
            stored_report.after_facets += part->mesh().its.indices.size();
            stored_report.after_open_edges += its_num_open_edges(part->mesh().its);
        }

        if (changed) {
            stored_report.result = ModelRepairVolumeStatus::Repaired;
            if (parts.empty()) {
                stored_report.annotations_cleared   = had_annotations;
                stored_report.annotations_preserved = false;
            } else {
                stored_report.annotations_preserved = options.preserve_annotations && had_preservable_annotations &&
                                                      !annotation_preservation_failed;
                stored_report.annotations_cleared = (!options.preserve_annotations && had_preservable_annotations) ||
                                                    had_exterior_annotations || annotation_preservation_failed;
            }
            ++report.repaired;
            changed_objects[target.object_index] = true;
            report.after_facets -= stored_report.before_facets;
            report.after_facets += stored_report.after_facets;
            report.after_open_edges -= stored_report.before_open_edges;
            report.after_open_edges += stored_report.after_open_edges;
        } else {
            ++report.skipped;
        }

        volume_finished(stored_report);
        if (cancellation_requested())
            return false;
    }

    for (size_t object_index = 0; object_index < changed_objects.size(); ++object_index)
        if (changed_objects[object_index])
            report.changed_object_indices.push_back(object_index);

    if (report.changed_object_indices.empty()) {
        report.success = true;
        return true;
    }

    if (cancellation_requested())
        return false;

    report.committed = true;
    report.success   = true;
    return true;
}

bool repair_staged_model(Model& model, ModelRepairReport& report, const ModelRepairOptions& options)
{
    try {
        return repair_staged_model_impl(model, report, options);
    } catch (const std::exception& exception) {
        mark_rolled_back(report);
        add_staging_failure(report, std::string("Repair transaction failed: ") + exception.what());
    } catch (...) {
        mark_rolled_back(report);
        add_staging_failure(report, "Repair transaction failed with an unknown error.");
    }
    return false;
}

bool repair_model(Model& model, ModelRepairReport& report, const ModelRepairOptions& options)
{
    std::unique_ptr<Model> staged_model;
    try {
        if (options.is_canceled && options.is_canceled()) {
            report          = ModelRepairReport{};
            report.canceled = true;
            return false;
        }
        staged_model = make_model_repair_staging_copy(model, options.targets);
    } catch (const std::exception& exception) {
        report = ModelRepairReport{};
        add_staging_failure(report, std::string("Could not stage model repair: ") + exception.what());
        return false;
    } catch (...) {
        report = ModelRepairReport{};
        add_staging_failure(report, "Could not stage model repair.");
        return false;
    }

    try {
        if (!repair_staged_model_impl(*staged_model, report, options))
            return false;
    } catch (const std::exception& exception) {
        mark_rolled_back(report);
        add_staging_failure(report, std::string("Repair transaction failed: ") + exception.what());
        return false;
    } catch (...) {
        mark_rolled_back(report);
        add_staging_failure(report, "Repair transaction failed with an unknown error.");
        return false;
    }
    if (!report.committed)
        return true;

    try {
        auto prepared = model.prepare_move_object_volume_structures(*staged_model, report.changed_object_indices);
        model.commit_move_object_volume_structures(std::move(prepared));
    } catch (const std::exception& exception) {
        mark_rolled_back(report);
        add_staging_failure(report, std::string("Repair commit failed: ") + exception.what());
        return false;
    } catch (...) {
        mark_rolled_back(report);
        add_staging_failure(report, "Repair commit failed with an unknown error.");
        return false;
    }

    return true;
}

} // namespace Slic3r
