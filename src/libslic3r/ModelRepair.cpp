#include "ModelRepair.hpp"

#include "AABBTreeIndirect.hpp"
#include "I18N.hpp"
#include "MeshBoolean.hpp"
#include "MeshRepair.hpp"
#include "Model.hpp"
#include "ModelInspection.hpp"

#include <boost/log/trivial.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iterator>
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

bool run_validation(const std::function<bool()>& validation, const ModelRepairOptions& options)
{
    return options.run_validation ? options.run_validation(validation) : validation();
}

bool same_oriented_surface(const TriangleMesh& a, const TriangleMesh& b)
{
    if (a.its.indices.size() != b.its.indices.size())
        return false;
    using Point      = std::array<float, 3>;
    using Face       = std::array<Point, 3>;
    const auto faces = [](const TriangleMesh& mesh) {
        std::vector<Face> result;
        result.reserve(mesh.its.indices.size());
        for (const auto& indices : mesh.its.indices) {
            Face face;
            for (size_t i = 0; i < 3; ++i) {
                const auto& v = mesh.its.vertices[indices[i]];
                face[i]       = {v.x(), v.y(), v.z()};
            }
            std::rotate(face.begin(), std::min_element(face.begin(), face.end()), face.end());
            result.push_back(face);
        }
        std::sort(result.begin(), result.end());
        return result;
    };
    return faces(a) == faces(b);
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
    if (options.basic_repair) {
        ModelInspectionOptions inspection;
        inspection.analyze_components = false;
        return inspect_model_mesh(mesh, inspection).at("next_action") == "advanced_repair";
    }
    return options.mesh_requires_repair ? options.mesh_requires_repair(mesh) : MeshBoolean::cgal::requires_repair(mesh);
}

bool has_preservable_annotations(const ModelVolume& volume)
{
    return !volume.supported_facets.empty() || !volume.seam_facets.empty() || !volume.mmu_segmentation_facets.empty() ||
           !volume.fuzzy_skin_facets.empty();
}

// Real beam/bracket regressions: a closed candidate can add coincident caps.
// Compare only exact duplicate/zero-area faces, not unstable intersection-pair
// counts or a heuristic distance score. This check does not modify either mesh.
bool adds_invalid_faces(const TriangleMesh& source, const TriangleMesh& candidate)
{
    const auto before = inspect_model_mesh_topology(source);
    const auto after  = inspect_model_mesh_topology(candidate);
    return after.at("duplicate").at("count") > before.at("duplicate").at("count") ||
           after.at("degenerate").at("count") > before.at("degenerate").at("count");
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
    return run_validation(validation, options);
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

void add_unresolved(ModelRepairReport& report, ModelRepairVolumeReport& volume_report, const std::string& message)
{
    if (volume_report.unresolved_parts++ == 0)
        ++report.unresolved;
    if (!volume_report.message.empty())
        volume_report.message += "\n";
    volume_report.message += message;
    report.failures.push_back(
        {volume_report.object_index, volume_report.volume_index, volume_report.object_name, volume_report.volume_name, message, true});
}

void mark_rolled_back(ModelRepairReport& report)
{
    for (ModelRepairVolumeReport& volume_report : report.volume_reports) {
        volume_report.basic_removed_faces = 0;
        if (volume_report.result != ModelRepairVolumeStatus::Repaired && volume_report.result != ModelRepairVolumeStatus::PartiallyRepaired)
            continue;
        volume_report.result                = ModelRepairVolumeStatus::RolledBack;
        volume_report.after_facets          = volume_report.before_facets;
        volume_report.after_open_edges      = volume_report.before_open_edges;
        volume_report.message               = "Repair was rolled back because the model transaction failed.";
        volume_report.annotations_cleared   = false;
        volume_report.annotations_preserved = false;
        ++report.rolled_back;
    }

    report.repaired            = 0;
    report.mesh_repaired       = 0;
    report.split_volumes       = 0;
    report.created_parts       = 0;
    report.removed_parts       = 0;
    report.basic_removed_faces = 0;
    report.committed           = false;
    report.completed           = false;
    report.success             = false;
    report.after_facets        = report.before_facets;
    report.after_open_edges    = report.before_open_edges;
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
    case ModelRepairVolumeStatus::PartiallyRepaired: return "partially_repaired";
    case ModelRepairVolumeStatus::Unresolved: return "unresolved";
    case ModelRepairVolumeStatus::RolledBack: return "rolled_back";
    case ModelRepairVolumeStatus::Failed: return "failed";
    }
    return "failed";
}

const char* ModelRepairReport::status() const
{
    if (canceled)
        return "canceled";
    if (can_continue() && unresolved != 0)
        return committed ? "partially_repaired" : "unresolved";
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
        report.completed = true;
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

        bool repair_volume = false;
        try {
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

        if (!repair_volume) {
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

        bool changed                        = false;
        bool annotation_preservation_failed = false;
        try {
            const TriangleMesh& part_mesh = volume->mesh();

            std::optional<TriangleSelector::SavedPainting> saved_painting;
            if (options.preserve_annotations && had_preservable_annotations)
                saved_painting = volume->save_painting();

            TriangleMesh repaired_mesh;
            std::string  error;
            bool         repair_succeeded;
            if (options.repair_mesh_from_source) {
                repair_succeeded = options.repair_mesh_from_source(part_mesh, repaired_mesh, error);
            } else {
                repaired_mesh    = part_mesh;
                repair_succeeded = options.repair_mesh ? options.repair_mesh(repaired_mesh, error) :
                                                         Slic3r::repair_mesh(repaired_mesh, nullptr, &error);
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
            bool remaining_defects = false;
            if (repair_succeeded) {
                if (repaired_mesh.empty() || !has_valid_indices(repaired_mesh) || !has_finite_vertices(repaired_mesh))
                    throw std::runtime_error("Repair failed: CGAL returned an invalid or open mesh.");
                if (its_num_open_edges(repaired_mesh.its) != 0) {
                    repair_succeeded = false;
                    error            = "Repair failed: CGAL returned an invalid or open mesh.";
                } else if (options.basic_repair) {
                    ModelInspectionOptions inspection;
                    inspection.analyze_components = false;
                    remaining_defects             = inspect_model_mesh(repaired_mesh, inspection).at("mesh_status") != "passed";
                } else {
                    remaining_defects = mesh_requires_repair(repaired_mesh, 0, options);
                }
                if (remaining_defects) {
                    error = options.allow_partial ? L("Repair retained verified progress, but geometry problems remain.") :
                                                    "Repair failed: repaired mesh did not pass full topology validation.";
                    // 2.5 may close a volume while retaining intersecting shells.
                    // Partial mode keeps that result, never labels it fully valid.
                    if (!options.allow_partial)
                        repair_succeeded = false;
                    else if (run_validation([&]() { return adds_invalid_faces(part_mesh, repaired_mesh); }, options)) {
                        repair_succeeded = false;
                        error            = L("Repair failed: candidate introduces duplicated or degenerate faces.");
                    }
                }
            }
            if (!repair_succeeded) {
                if (!options.allow_partial)
                    throw std::runtime_error(error.empty() ? "Repair failed." : error);
                add_unresolved(report, stored_report, error.empty() ? "Repair failed." : error);
            } else if (remaining_defects)
                add_unresolved(report, stored_report, error);
            // Rejected candidates and triangle reordering do not replace the source.
            if (repair_succeeded && (repaired_mesh.its.vertices.size() < part_mesh.its.vertices.size() ||
                                     run_validation([&]() { return !same_oriented_surface(part_mesh, repaired_mesh); }, options))) {
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
                // CGAL edits vertices in the current volume-local coordinate
                // system. Keeping the import shift would apply it twice while
                // remapping SavedPainting.
                repaired_mesh.set_init_shift(Vec3d::Zero());
                volume->set_mesh(std::move(repaired_mesh));
                volume->calculate_convex_hull();
                volume->invalidate_convex_hull_2d();
                if (options.preserve_annotations) {
                    volume->restore_painting(saved_painting);
                    bool repaired_painting_restored = true;
                    if (saved_painting) {
                        repaired_painting_restored = saved_painting_was_restored(*saved_painting, std::vector<ModelVolume*>{volume},
                                                                                 options);
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
                        volume->reset_extra_facets();
                        annotation_preservation_failed = true;
                    }
                } else {
                    volume->reset_extra_facets();
                }
                volume->exterior_facets.reset();
                volume->set_new_unique_id();

                ++stored_report.repaired_parts;
                ++report.mesh_repaired;
                changed = true;
            }
            if (stored_report.unresolved_parts != 0 && options.preserve_annotations && annotation_preservation_failed)
                throw std::runtime_error("Repair failed: painted facet annotations could not be preserved in a partial result.");
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

        stored_report.after_facets     = volume->mesh().its.indices.size();
        stored_report.after_open_edges = its_num_open_edges(volume->mesh().its);

        if (changed) {
            stored_report.result = stored_report.unresolved_parts == 0 ? ModelRepairVolumeStatus::Repaired :
                                                                         ModelRepairVolumeStatus::PartiallyRepaired;
            stored_report.annotations_preserved = options.preserve_annotations && had_preservable_annotations &&
                                                  !annotation_preservation_failed;
            stored_report.annotations_cleared = (!options.preserve_annotations && had_preservable_annotations) ||
                                                had_exterior_annotations || annotation_preservation_failed;
            ++report.repaired;
            changed_objects[target.object_index] = true;
            report.after_facets -= stored_report.before_facets;
            report.after_facets += stored_report.after_facets;
            report.after_open_edges -= stored_report.before_open_edges;
            report.after_open_edges += stored_report.after_open_edges;
        } else if (stored_report.unresolved_parts != 0) {
            stored_report.result = ModelRepairVolumeStatus::Unresolved;
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
        report.completed = true;
        report.success   = report.unresolved == 0;
        return report.success;
    }

    if (cancellation_requested())
        return false;

    report.committed = true;
    report.completed = true;
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
