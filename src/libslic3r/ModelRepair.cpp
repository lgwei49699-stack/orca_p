#include "ModelRepair.hpp"

// Include before Model.hpp introduces OpenCascade's Handle macro.
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>

#include "AABBTreeIndirect.hpp"
#include "I18N.hpp"
#include "MeshBoolean.hpp"
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
#include <numeric>
#include <set>
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

// A separately retainable baseline, not an intermediate CGAL candidate. Only
// remove exact zero-area faces and coincident faces with the SAME winding.
// Oppositely oriented interfaces, small faces and all vertex positions remain
// untouched. This works with both indexed OBJ and exact-welded STL geometry.
size_t remove_basic_faces(const TriangleMesh& source, TriangleMesh& result)
{
    using Corner = std::array<float, 3>;
    std::set<std::array<Corner, 3>>          seen;
    std::vector<stl_triangle_vertex_indices> faces;
    faces.reserve(source.its.indices.size());
    for (const auto& face : source.its.indices) {
        const Vec3d a = source.its.vertices[face[0]].cast<double>();
        const Vec3d b = source.its.vertices[face[1]].cast<double>();
        const Vec3d c = source.its.vertices[face[2]].cast<double>();
        if ((b - a).cross(c - a).squaredNorm() == 0.)
            continue;
        std::array<Corner, 3> key;
        for (size_t i = 0; i < 3; ++i) {
            const auto& vertex = source.its.vertices[face[i]];
            key[i]             = {vertex.x(), vertex.y(), vertex.z()};
        }
        std::rotate(key.begin(), std::min_element(key.begin(), key.end()), key.end());
        if (seen.insert(key).second)
            faces.push_back(face);
    }
    const size_t removed = source.its.indices.size() - faces.size();
    if (removed != 0) {
        if (faces.empty()) {
            result = TriangleMesh{};
            return removed;
        }
        indexed_triangle_set cleaned;
        cleaned.vertices = source.its.vertices;
        cleaned.indices  = std::move(faces);
        its_compactify_vertices(cleaned, true);
        TriangleMesh candidate(std::move(cleaned));
        const size_t before_open_edges = its_num_open_edges(source.its);
        const size_t after_open_edges  = its_num_open_edges(candidate.its);
        if (after_open_edges > before_open_edges) {
            // A collinear triangle may close an indexed edge fan. Deleting it
            // alone opens that fan. Defer this batch to the independently
            // validated topology preparation below, or the original CGAL
            // repair path for unsupported cases. Never retain deletion alone.
            BOOST_LOG_TRIVIAL(info) << "Model repair: event=basic_cleanup_deferred, reason=would_open_connections, faces="
                                    << source.facets_count() << "->" << candidate.facets_count() << ", open_edges=" << before_open_edges
                                    << "->" << after_open_edges;
            return 0;
        }
        result = std::move(candidate);
    }
    return removed;
}

struct BasicMeshPreparation
{
    size_t removed_faces      = 0;
    size_t repaired_edge_caps = 0;
    bool   changed() const { return removed_faces != 0 || repaired_edge_caps != 0; }
};

BasicMeshPreparation prepare_basic_mesh(const TriangleMesh& source, TriangleMesh& result)
{
    BasicMeshPreparation preparation;
    preparation.removed_faces    = remove_basic_faces(source, result);
    const TriangleMesh& baseline = preparation.removed_faces != 0 ? result : source;
    TriangleMesh        normalized;
    preparation.repaired_edge_caps = MeshBoolean::cgal::repair_degenerate_edge_caps(baseline, normalized);
    if (preparation.repaired_edge_caps != 0)
        result = std::move(normalized);
    return preparation;
}

// Structural guard for basic candidates and partial-failure handling. A small
// solid is still 3D: do not reuse the optional size-based removal policy below
// to reject repair candidates or change the transaction's failure semantics.
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
                                             return (b - a).cross(c - a).squaredNorm() == 0.0;
                                         }),
                          cleaned.indices.end());
    if (cleaned.indices.empty())
        return true;
    its_compactify_vertices(cleaned, true);

    const BoundingBoxf3 bbox    = Slic3r::bounding_box(cleaned);
    const Vec3d         size    = bbox.size();
    const double        min_dim = std::min(size.x(), std::min(size.y(), size.z()));
    if (min_dim == 0.0)
        return true;

    // Test affine dimension, not volume: an open cone with every face incident
    // to the chosen reference vertex has zero tetrahedral sum but is still 3D.
    // Reversed faces also must not cancel away an otherwise valid solid.
    const auto& face   = cleaned.indices.front();
    const Vec3d a      = cleaned.vertices[size_t(face[0])].cast<double>();
    const Vec3d b      = cleaned.vertices[size_t(face[1])].cast<double>();
    const Vec3d c      = cleaned.vertices[size_t(face[2])].cast<double>();
    const Vec3d normal = (b - a).cross(c - a);
    for (const stl_vertex& vertex : cleaned.vertices)
        if (normal.dot(vertex.cast<double>() - a) != 0.0)
            return false;
    // A numerically ambiguous near-plane is retained for CGAL validation.
    // Safe failure is preferable to silently deleting a thin but valid solid.
    return true;
}

bool has_only_zero_area_faces(const TriangleMesh& mesh)
{
    using Point      = CGAL::Exact_predicates_inexact_constructions_kernel::Point_3;
    const auto point = [&mesh](int index) {
        const auto& v = mesh.its.vertices[size_t(index)];
        return Point(v.x(), v.y(), v.z());
    };
    return std::all_of(mesh.its.indices.begin(), mesh.its.indices.end(),
                       [&point](const auto& face) { return CGAL::collinear(point(face[0]), point(face[1]), point(face[2])); });
}

// Use OrcaSlicer 2.4.2's source-local cleanup thresholds, not nozzle/layer limits
// or instance-scaled sizes. Volume cutoffs require a proven bounded solid:
// signed sums for open, inconsistently wound or intersecting surfaces may cancel.
// Kept separate from structural validation: callers may disable removal for
// strict color preservation, and object-emptying protection remains in place.
bool is_discardable_repair_part(const TriangleMesh& mesh, const ModelRepairOptions& options)
{
    if (mesh.its.indices.empty())
        return true;
    {
        indexed_triangle_set tmp = mesh.its;
        its_remove_degenerate_faces(tmp, true);
        if (tmp.indices.empty())
            return true;
    }

    const Vec3d      size                   = mesh.bounding_box().size();
    const double     min_dim                = std::min(size.x(), std::min(size.y(), size.z()));
    const double     max_dim                = std::max(size.x(), std::max(size.y(), size.z()));
    constexpr double min_relative_thickness = 1e-6;
    constexpr double min_volume_ratio       = 1e-6;
    if (min_dim <= EPSILON || min_dim / max_dim <= min_relative_thickness)
        return true;
    const double volume      = std::abs(its_volume(mesh.its));
    const double bbox_volume = size.x() * size.y() * size.z();
    if (!std::isfinite(volume) || !(volume <= EPSILON || (bbox_volume > 0.0 && volume / bbox_volume <= min_volume_ratio)))
        return false;

    // Diagnose only volume-based deletion candidates. Ignore unused vertices
    // in this read-only surface check, but retain the original bbox policy.
    // Do not remove faces here: that could hide invalid topology from CGAL.
    bool       bounded_volume = false;
    const auto validate       = [&]() {
        indexed_triangle_set surface = mesh.its;
        its_compactify_vertices(surface, true);
        bounded_volume = !MeshBoolean::cgal::requires_repair(TriangleMesh(std::move(surface)));
    };
    if (options.run_mesh_preparation)
        options.run_mesh_preparation(validate);
    else
        validate();
    if (!bounded_volume) {
        BOOST_LOG_TRIVIAL(info) << "Model repair: event=volume_cleanup_deferred, reason=unproven_bounded_volume";
        return false;
    }
    return true;
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

struct RepairGroup
{
    TriangleMesh        mesh;
    std::vector<size_t> source_faces;
};

// Fallback for partial repair when not EVERY patch can be split individually.
// Keep touching/overlapping bounds in transitive groups, including cavities.
// Extract faces from the ORIGINAL indexed mesh: merging its_split() outputs
// would duplicate shared vertices at nonmanifold edges inside a retained group.
std::vector<RepairGroup> separated_repair_groups(const TriangleMesh& mesh)
{
    const auto                 neighbors = its_face_neighbors(mesh.its);
    const size_t               unset     = std::numeric_limits<size_t>::max();
    std::vector<size_t>        patch(mesh.its.indices.size(), unset), stack;
    std::vector<BoundingBoxf3> bounds;
    for (size_t seed = 0; seed < patch.size(); ++seed) {
        if (patch[seed] != unset)
            continue;
        const size_t id = bounds.size();
        bounds.emplace_back();
        patch[seed] = id;
        stack.push_back(seed);
        while (!stack.empty()) {
            const size_t face = stack.back();
            stack.pop_back();
            for (int vertex : mesh.its.indices[face])
                bounds.back().merge(mesh.its.vertices[size_t(vertex)].cast<double>());
            for (int adjacent : neighbors[face]) {
                if (adjacent >= 0 && patch[size_t(adjacent)] == unset) {
                    patch[size_t(adjacent)] = id;
                    stack.push_back(size_t(adjacent));
                }
            }
        }
    }
    if (bounds.size() <= 1)
        return {};
    std::vector<size_t> parent(bounds.size()), order(bounds.size());
    std::iota(parent.begin(), parent.end(), 0);
    std::iota(order.begin(), order.end(), 0);
    const auto root = [&parent](size_t id) {
        while (parent[id] != id) {
            parent[id] = parent[parent[id]];
            id         = parent[id];
        }
        return id;
    };
    std::sort(order.begin(), order.end(), [&bounds](size_t a, size_t b) { return bounds[a].min.x() < bounds[b].min.x(); });
    size_t comparisons = 0;
    for (size_t first = 0; first < order.size(); ++first) {
        const auto& a = bounds[order[first]];
        for (size_t second = first + 1; second < order.size(); ++second) {
            const auto& b = bounds[order[second]];
            if (a.max.x() < b.min.x())
                break;
            // Conservative fallback for pathological overlapping patch counts.
            // This bound affects grouping only, never a mesh validity verdict.
            if (++comparisons > 1000000)
                return {};
            if (!(a.max.y() < b.min.y() || b.max.y() < a.min.y() || a.max.z() < b.min.z() || b.max.z() < a.min.z()))
                parent[root(order[second])] = root(order[first]);
        }
    }
    std::vector<size_t> group(bounds.size(), unset);
    size_t              group_count = 0;
    for (size_t id = 0; id < bounds.size(); ++id) {
        const size_t r = root(id);
        if (group[r] == unset)
            group[r] = group_count++;
    }
    if (group_count <= 1)
        return {};
    // Compact indices without copying the full source vertex array per group.
    std::vector<indexed_triangle_set> extracted(group_count);
    std::vector<std::vector<size_t>>  source_faces(group_count);
    std::vector<size_t>               vertex_group(mesh.its.vertices.size(), unset);
    std::vector<int>                  vertex_index(mesh.its.vertices.size(), -1);
    for (size_t face = 0; face < patch.size(); ++face) {
        const size_t id     = group[root(patch[face])];
        auto&        target = extracted[id];
        Vec3i32      indices;
        for (size_t corner = 0; corner < 3; ++corner) {
            const size_t vertex = size_t(mesh.its.indices[face][corner]);
            if (vertex_group[vertex] == unset) {
                vertex_group[vertex] = id;
                vertex_index[vertex] = int(target.vertices.size());
                target.vertices.push_back(mesh.its.vertices[vertex]);
            }
            // Shared vertices must have overlapping patch bounds, hence one group.
            if (vertex_group[vertex] != id)
                throw std::runtime_error("Repair grouping separated a shared vertex.");
            indices[corner] = vertex_index[vertex];
        }
        target.indices.push_back(indices);
        source_faces[id].push_back(face);
    }
    std::vector<RepairGroup> result;
    result.reserve(group_count);
    for (size_t id = 0; id < group_count; ++id) {
        result.push_back({TriangleMesh(std::move(extracted[id])), std::move(source_faces[id])});
        result.back().mesh.set_init_shift(Vec3d::Zero()); // No per-shell winding normalization.
    }
    return result;
}

// Group extraction only renumbers vertices/faces; corner order and coordinates
// are unchanged. Copy each complete subdivision tree by provenance instead of
// rasterizing painting geometrically twice (extraction, then CGAL repair).
void copy_group_annotations(const ModelVolume& source, ModelVolume& target, const std::vector<size_t>& source_faces)
{
    const auto copy = [&source_faces](const FacetsAnnotation& from, FacetsAnnotation& to) {
        const auto&                             data = from.get_data();
        TriangleSelector::TriangleSplittingData copied;
        for (size_t face = 0; face < source_faces.size(); ++face) {
            const auto it = std::lower_bound(data.triangles_to_split.begin(), data.triangles_to_split.end(), int(source_faces[face]),
                                             [](const auto& item, int index) { return item.triangle_idx < index; });
            if (it == data.triangles_to_split.end() || it->triangle_idx != int(source_faces[face]))
                continue;
            const auto   next  = std::next(it);
            const size_t begin = size_t(it->bitstream_start_idx);
            const size_t end   = next == data.triangles_to_split.end() ? data.bitstream.size() : size_t(next->bitstream_start_idx);
            if (begin >= end || end > data.bitstream.size() || (end - begin) % 4 != 0)
                throw std::runtime_error("Repair failed: painted facet annotations could not be mapped onto the remaining model parts.");
            const size_t start = copied.bitstream.size();
            copied.triangles_to_split.emplace_back(int(face), int(start));
            copied.bitstream.insert(copied.bitstream.end(), data.bitstream.begin() + begin, data.bitstream.begin() + end);
            copied.update_used_states(start);
        }
        to.set_data(std::move(copied));
    };
    copy(source.supported_facets, target.supported_facets);
    copy(source.seam_facets, target.seam_facets);
    copy(source.mmu_segmentation_facets, target.mmu_segmentation_facets);
    copy(source.fuzzy_skin_facets, target.fuzzy_skin_facets);
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

        bool                     split_volume            = false;
        bool                     remove_volume           = false;
        bool                     repair_volume           = false;
        size_t                   disconnected_part_count = 1;
        std::vector<RepairGroup> repair_groups;
        try {
            if (options.split_components && options.basic_repair) {
                // Preserve original faces/winding, including thin parts and
                // cavities. Unlike volume->split(), grouping does not discard
                // small parts or count separation alone as repair progress.
                repair_groups = separated_repair_groups(mesh);
            } else if (options.split_components && !mesh.its.indices.empty() && volume->is_splittable()) {
                split_volume = has_only_safely_separable_components(mesh, disconnected_part_count);
                if (!split_volume && options.allow_partial)
                    repair_groups = separated_repair_groups(mesh);
            }
            remove_volume = options.remove_non_3d_parts && !split_volume && repair_groups.empty() &&
                            is_discardable_repair_part(mesh, options);
            if (cancellation_requested()) {
                volume_finished(stored_report);
                return false;
            }
            if (!split_volume && repair_groups.empty() && !remove_volume && !mesh.its.indices.empty())
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

        if (!split_volume && repair_groups.empty() && !remove_volume && !repair_volume) {
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
        const bool                grouped_repair = !repair_groups.empty();
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

            if (grouped_repair) {
                // Leave the original staging volume intact until a group makes
                // real progress. No re-centering: retain its full transform and
                // source-local geometry, including nested shell orientation.
                parts.reserve(repair_groups.size());
                for (auto& group : repair_groups) {
                    auto* part   = object->add_volume(*volume, std::move(group.mesh), false);
                    part->source = ModelVolume::Source();
                    part->text_configuration.reset();
                    if (source_painting)
                        copy_group_annotations(*volume, *part, group.source_faces);
                    parts.push_back(part);
                }
                stored_report.split_parts = parts.size();
            } else {
                const size_t available_parts = volume_index < object->volumes.size() ?
                                                   std::min(parts_count, object->volumes.size() - volume_index) :
                                                   0;
                parts.reserve(available_parts);
                for (size_t part_index = 0; part_index < available_parts; ++part_index)
                    parts.push_back(object->volumes[volume_index + part_index]);
            }

            if (options.remove_non_3d_parts || options.basic_repair) {
                for (size_t part_offset = parts.size(); part_offset > 0; --part_offset) {
                    ModelVolume* part        = parts[part_offset - 1];
                    // App removes only proven zero-area groups. A planar surface
                    // or a very thin solid still has area and must be retained.
                    const bool discardable = options.basic_repair ? has_only_zero_area_faces(part->mesh()) :
                                                                    is_discardable_repair_part(part->mesh(), options);
                    if (cancellation_requested()) {
                        volume_finished(stored_report);
                        return false;
                    }
                    if (!discardable)
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

            // Removing dust is useful only while this object still has a model
            // part. An empty repaired object is not a successful repair, even
            // if another object (or a modifier) remains elsewhere in the scene.
            if (parts.empty() &&
                std::none_of(object->volumes.begin(), object->volumes.end(), [volume, grouped_repair](const ModelVolume* part) {
                    return part != nullptr && part->is_model_part() && (!grouped_repair || part != volume);
                }))
                throw std::runtime_error("Repair failed: basic cleanup removed all geometry.");

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

                // The original, unsplit volume has already been inspected above.
                // New split/grouped parts still require their own inspection.
                const bool   unchanged_part       = part == volume && !split_volume && !grouped_repair;
                const size_t part_open_edges      = unchanged_part ? stored_report.before_open_edges : its_num_open_edges(part_mesh.its);
                const bool   part_requires_repair = unchanged_part ? repair_volume :
                                                                     mesh_requires_repair(part_mesh, part_open_edges, options);
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
                TriangleMesh basic_mesh;
                BasicMeshPreparation preparation;
                if (options.allow_partial && !options.basic_repair) {
                    const auto prepare = [&]() { preparation = prepare_basic_mesh(part_mesh, basic_mesh); };
                    if (options.run_mesh_preparation)
                        options.run_mesh_preparation(prepare);
                    else
                        prepare();
                    if (cancellation_requested())
                        return false;
                    if (preparation.changed() && (basic_mesh.empty() || is_not_3dimensional_part(basic_mesh)))
                        throw std::runtime_error("Repair failed: basic cleanup removed all geometry.");
                }
                const TriangleMesh& repair_source = preparation.changed() ? basic_mesh : part_mesh;
                std::string         error;
                bool                repair_succeeded = false;
                bool                basic_fallback   = false;
                bool                partial_candidate = false;
                const bool          basic_is_valid    = preparation.changed() &&
                                            !mesh_requires_repair(basic_mesh, its_num_open_edges(basic_mesh.its), options);
                if (cancellation_requested())
                    return false;
                if (basic_is_valid) {
                    repaired_mesh    = std::move(basic_mesh);
                    repair_succeeded = true;
                } else if (options.allow_partial && options.repair_mesh_with_partial) {
                    const auto outcome = options.repair_mesh_with_partial(repair_source, repaired_mesh, error);
                    repair_succeeded   = outcome != MeshRepairOutcome::Rejected;
                    partial_candidate  = outcome == MeshRepairOutcome::PartiallyRepaired;
                } else if (options.repair_mesh_from_source) {
                    repair_succeeded = options.repair_mesh_from_source(repair_source, repaired_mesh, error);
                } else {
                    // Headless and legacy hooks retain their original
                    // synchronous semantics. GUI callers use the source-to-
                    // result hook above so this O(mesh) copy runs on a worker.
                    repaired_mesh    = repair_source;
                    if (options.repair_mesh) {
                        repair_succeeded = options.repair_mesh(repaired_mesh, error);
                    } else if (options.basic_repair) {
                        repair_succeeded = MeshBoolean::cgal::repair_basic(repaired_mesh, &error);
                    } else if (options.allow_partial) {
                        const auto outcome = MeshBoolean::cgal::repair_with_partial(repaired_mesh, &error);
                        repair_succeeded   = outcome != MeshRepairOutcome::Rejected;
                        partial_candidate  = outcome == MeshRepairOutcome::PartiallyRepaired;
                    } else {
                        repair_succeeded = MeshBoolean::cgal::repair(repaired_mesh, nullptr, &error);
                    }
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
                if (repair_succeeded) {
                    // Illegal data remains a transaction failure, not a
                    // recoverable geometric rejection, even in partial mode.
                    if (repaired_mesh.its.vertices.empty() || repaired_mesh.its.indices.empty() || !has_valid_indices(repaired_mesh) ||
                        !has_finite_vertices(repaired_mesh))
                        throw std::runtime_error("Repair failed: CGAL returned an invalid or open mesh.");
                    const size_t open_edges = its_num_open_edges(repaired_mesh.its);
                    // App mode relies on CGAL's own validation of a successful
                    // repair. Keep storage/closure checks and partial validation;
                    // other callers retain the independent full inspection.
                    const bool needs_repair = !options.basic_repair && mesh_requires_repair(repaired_mesh, open_edges, options);
                    if (cancellation_requested())
                        return false;
                    if (partial_candidate || open_edges != 0 || needs_repair) {
                        bool retainable = false;
                        if (options.allow_partial && !options.basic_repair) {
                            const auto validate = [&]() {
                                retainable = MeshBoolean::cgal::is_safe_partial_repair(repair_source, repaired_mesh);
                            };
                            if (options.run_mesh_preparation)
                                options.run_mesh_preparation(validate);
                            else
                                validate();
                        }
                        if (cancellation_requested())
                            return false;
                        if (!retainable) {
                            repair_succeeded = false;
                            error            = open_edges != 0 ? "Repair failed: CGAL returned an invalid or open mesh." :
                                                                 "Repair failed: repaired mesh did not pass full topology, self-intersection, and "
                                                                 "orientation validation.";
                            BOOST_LOG_TRIVIAL(warning) << "Model repair: event=candidate_rejected, reason=" << error;
                        } else {
                            partial_candidate = true;
                            if (error.empty())
                                error = L("Repair retained verified progress, but geometry problems remain.");
                        }
                    }
                }
                if (!repair_succeeded) {
                    if (!options.allow_partial || (!options.basic_repair && is_not_3dimensional_part(repair_source)))
                        throw std::runtime_error(error.empty() ? "Repair failed." : error);
                    add_unresolved(report, stored_report, error.empty() ? "Repair failed." : error);
                    // Never retain the rejected candidate, even if a delegated
                    // hook modified it before returning false.
                    if (!preparation.changed())
                        continue;
                    repaired_mesh  = std::move(basic_mesh);
                    basic_fallback = true;
                    BOOST_LOG_TRIVIAL(info) << "Model repair: event=preparation_retained, removed_faces=" << preparation.removed_faces
                                            << ", repaired_edge_caps=" << preparation.repaired_edge_caps << ", reason=" << error;
                } else if (partial_candidate) {
                    add_unresolved(report, stored_report, error);
                }

                if (repaired_mesh.its.vertices.empty() || repaired_mesh.its.indices.empty() || !has_valid_indices(repaired_mesh) ||
                    !has_finite_vertices(repaired_mesh))
                    throw std::runtime_error("Repair failed: CGAL returned an invalid or open mesh.");
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
                part->set_mesh(std::move(repaired_mesh));
                part->calculate_convex_hull();
                part->invalidate_convex_hull_2d();
                if (options.preserve_annotations) {
                    part->restore_painting(saved_painting);
                    bool repaired_painting_restored = true;
                    if (saved_painting) {
                        repaired_painting_restored = saved_painting_was_restored(*saved_painting, std::vector<ModelVolume*>{part}, options);
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
                        if (!options.allow_annotation_clearing || basic_fallback)
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

                // A diagonal flip repairs a zero-area record without reducing
                // the face count. Track that progress separately from deletions
                // internally; the public report already carries before/after
                // inspection and partial status, so no new JSON field is needed.
                stored_report.basic_removed_faces += preparation.removed_faces;
                report.basic_removed_faces += preparation.removed_faces;
                if (!basic_fallback) {
                    ++stored_report.repaired_parts;
                    ++report.mesh_repaired;
                }
                changed = true;
            }
            if (stored_report.unresolved_parts != 0 && options.preserve_annotations && annotation_preservation_failed)
                throw std::runtime_error("Repair failed: painted facet annotations could not be preserved in a partial result.");
            if (grouped_repair) {
                if (changed) {
                    // Groups occupy the original slot in stable order. Other
                    // selected/unselected staging volumes keep their pointers and order.
                    object->delete_volume(volume_index);
                    for (size_t i = 0; i < parts.size(); ++i) {
                        const auto it = std::find(object->volumes.begin(), object->volumes.end(), parts[i]);
                        std::rotate(object->volumes.begin() + volume_index + i, it, it + 1);
                    }
                    ++report.split_volumes;
                    report.created_parts += parts.size() > 1 ? parts.size() - 1 : 0;
                } else {
                    // Splitting alone must not turn repeated unresolved input
                    // into an endless sequence of "partial repair" successes.
                    for (auto* part : parts) {
                        const auto it = std::find(object->volumes.begin(), object->volumes.end(), part);
                        object->delete_volume(size_t(it - object->volumes.begin()));
                    }
                    parts = {volume};
                }
                object->invalidate_bounding_box();
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
            stored_report.result = stored_report.unresolved_parts == 0 ? ModelRepairVolumeStatus::Repaired :
                                                                         ModelRepairVolumeStatus::PartiallyRepaired;
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
