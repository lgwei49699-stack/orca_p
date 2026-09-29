#include "Exception.hpp"
#include "MeshBoolean.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TryCatchSignal.hpp"
#include "libslic3r/format.hpp"
#undef PI

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <map>
#include <set>
#include <stdexcept>

#include <boost/next_prior.hpp>
#include <boost/variant/get.hpp>
#include "boost/log/trivial.hpp"
// Include igl first. It defines "L" macro which then clashes with our localization
#include <igl/copyleft/cgal/mesh_boolean.h>
#undef L
#include "libslic3r/I18N.hpp"

// CGAL headers
#include <CGAL/Polygon_mesh_processing/corefinement.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Polygon_mesh_processing/distance.h>
#include <CGAL/Exact_integer.h>
#include <CGAL/Intersections_3/Triangle_3_Triangle_3.h>
#include <CGAL/Side_of_triangle_mesh.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Cartesian_converter.h>
#include <CGAL/Kernel/global_functions_3.h>
#include <CGAL/Polygon_mesh_processing/border.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/repair_degeneracies.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/repair_self_intersections.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/shape_predicates.h>
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/boost/graph/Euler_operations.h>
#include <CGAL/version.h>
// BBS: for segment
#include <CGAL/mesh_segmentation.h>
#include <CGAL/property_map.h>
#include <CGAL/boost/graph/copy_face_graph.h>
#include <CGAL/boost/graph/Face_filtered_graph.h>
// BBS: for boolean using mcut
#include "mcut/include/mcut/mcut.h"

namespace Slic3r { namespace MeshBoolean {

using MapMatrixXfUnaligned = Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>>;
using MapMatrixXiUnaligned = Eigen::Map<const Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>>;

TriangleMesh eigen_to_triangle_mesh(const EigenMesh& emesh)
{
    auto& VC = emesh.first;
    auto& FC = emesh.second;

    indexed_triangle_set its;
    its.vertices.reserve(size_t(VC.rows()));
    its.indices.reserve(size_t(FC.rows()));

    for (Eigen::Index i = 0; i < VC.rows(); ++i)
        its.vertices.emplace_back(VC.row(i).cast<float>());

    for (Eigen::Index i = 0; i < FC.rows(); ++i)
        its.indices.emplace_back(FC.row(i));

    return TriangleMesh{std::move(its)};
}

EigenMesh triangle_mesh_to_eigen(const TriangleMesh& mesh)
{
    EigenMesh emesh;
    emesh.first = MapMatrixXfUnaligned(mesh.its.vertices.front().data(), Eigen::Index(mesh.its.vertices.size()), 3).cast<double>();

    emesh.second = MapMatrixXiUnaligned(mesh.its.indices.front().data(), Eigen::Index(mesh.its.indices.size()), 3);
    return emesh;
}

void minus(EigenMesh& A, const EigenMesh& B)
{
    auto& [VA, FA] = A;
    auto& [VB, FB] = B;

    Eigen::MatrixXd      VC;
    Eigen::MatrixXi      FC;
    igl::MeshBooleanType boolean_type(igl::MESH_BOOLEAN_TYPE_MINUS);
    igl::copyleft::cgal::mesh_boolean(VA, FA, VB, FB, boolean_type, VC, FC);

    VA = std::move(VC);
    FA = std::move(FC);
}

void minus(TriangleMesh& A, const TriangleMesh& B)
{
    EigenMesh eA = triangle_mesh_to_eigen(A);
    minus(eA, triangle_mesh_to_eigen(B));
    A = eigen_to_triangle_mesh(eA);
}

void self_union(EigenMesh& A)
{
    EigenMesh result;
    auto& [V, F]   = A;
    auto& [VC, FC] = result;

    igl::MeshBooleanType boolean_type(igl::MESH_BOOLEAN_TYPE_UNION);
    igl::copyleft::cgal::mesh_boolean(V, F, Eigen::MatrixXd(), Eigen::MatrixXi(), boolean_type, VC, FC);

    A = std::move(result);
}

void self_union(TriangleMesh& mesh)
{
    auto eM = triangle_mesh_to_eigen(mesh);
    self_union(eM);
    mesh = eigen_to_triangle_mesh(eM);
}

namespace cgal {

namespace CGALProc   = CGAL::Polygon_mesh_processing;
namespace CGALParams = CGAL::Polygon_mesh_processing::parameters;

using EpecKernel = CGAL::Exact_predicates_exact_constructions_kernel;
using EpicKernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using _EpicMesh  = CGAL::Surface_mesh<EpicKernel::Point_3>;
using _EpecMesh  = CGAL::Surface_mesh<EpecKernel::Point_3>;

struct CGALMesh
{
    _EpicMesh m;
    CGALMesh() = default;
    CGALMesh(const _EpicMesh& _m) : m(_m) {}
};

void save_CGALMesh(const std::string& fname, const CGALMesh& cgal_mesh)
{
    std::ofstream os(fname);
    os << cgal_mesh.m;
    os.close();
}
// /////////////////////////////////////////////////////////////////////////////
// Converions from and to CGAL mesh
// /////////////////////////////////////////////////////////////////////////////

template<class _Mesh> void triangle_mesh_to_cgal(const TriangleMesh& M, _Mesh& out)
{
    using Index3 = std::array<size_t, 3>;

    if (M.empty())
        return;

    std::vector<typename _Mesh::Point> points;
    std::vector<Index3>                indices;
    points.reserve(M.its.vertices.size());
    indices.reserve(M.its.indices.size());
    for (auto& v : M.its.vertices)
        points.emplace_back(v.x(), v.y(), v.z());
    for (auto& _f : M.its.indices) {
        auto f = _f.cast<size_t>();
        indices.emplace_back(Index3{f(0), f(1), f(2)});
    }

    CGALProc::orient_polygon_soup(points, indices);
    CGALProc::polygon_soup_to_polygon_mesh(points, indices, out);

    // Number the faces because 'orient_to_bound_a_volume' needs a face <--> index map
    unsigned index = 0;
    for (auto face : out.faces())
        face = CGAL::SM_Face_index(index++);

    if (CGAL::is_closed(out))
        CGALProc::orient_to_bound_a_volume(out);
    else
        throw Slic3r::RuntimeError("Mesh not watertight");
}

template<class _Mesh>
void triangle_mesh_to_cgal(const std::vector<stl_vertex>& V, const std::vector<stl_triangle_vertex_indices>& F, _Mesh& out)
{
    if (F.empty())
        return;

    size_t vertices_count = V.size();
    size_t edges_count    = (F.size() * 3) / 2;
    size_t faces_count    = F.size();
    out.reserve(vertices_count, edges_count, faces_count);

    for (auto& v : V)
        out.add_vertex(typename _Mesh::Point{v.x(), v.y(), v.z()});

    using VI = typename _Mesh::Vertex_index;
    for (auto& f : F)
        out.add_face(VI(f(0)), VI(f(1)), VI(f(2)));
}

inline Vec3f to_vec3f(const _EpicMesh::Point& v) { return {float(v.x()), float(v.y()), float(v.z())}; }

inline Vec3f to_vec3f(const _EpecMesh::Point& v)
{
    CGAL::Cartesian_converter<EpecKernel, EpicKernel> cvt;
    auto                                              iv = cvt(v);
    return {float(iv.x()), float(iv.y()), float(iv.z())};
}

template<class _Mesh> indexed_triangle_set cgal_to_indexed_triangle_set(const _Mesh& cgalmesh)
{
    indexed_triangle_set its;
    its.vertices.reserve(cgalmesh.num_vertices());
    its.indices.reserve(cgalmesh.num_faces());

    const auto& faces    = cgalmesh.faces();
    const auto& vertices = cgalmesh.vertices();
    int         vsize    = int(vertices.size());

    for (const auto& vi : vertices) {
        auto& v = cgalmesh.point(vi); // Don't ask...
        its.vertices.emplace_back(to_vec3f(v));
    }

    for (const auto& face : faces) {
        auto vtc = cgalmesh.vertices_around_face(cgalmesh.halfedge(face));

        int     i = 0;
        Vec3i32 facet;
        for (auto v : vtc) {
            int iv = v;
            if (i > 2 || iv < 0 || iv >= vsize) {
                i = 0;
                break;
            }
            facet(i++) = iv;
        }

        if (i == 3)
            its.indices.emplace_back(facet);
    }

    return its;
}

template<class _Mesh> TriangleMesh cgal_to_triangle_mesh(const _Mesh& cgalmesh)
{
    indexed_triangle_set its = cgal_to_indexed_triangle_set(cgalmesh);
    return TriangleMesh(std::move(its));
}

std::unique_ptr<CGALMesh, CGALMeshDeleter> triangle_mesh_to_cgal(const std::vector<stl_vertex>&                  V,
                                                                 const std::vector<stl_triangle_vertex_indices>& F)
{
    std::unique_ptr<CGALMesh, CGALMeshDeleter> out(new CGALMesh{});
    triangle_mesh_to_cgal(V, F, out->m);
    return out;
}

TriangleMesh cgal_to_triangle_mesh(const CGALMesh& cgalmesh) { return TriangleMesh{cgal_to_indexed_triangle_set(cgalmesh.m)}; }

indexed_triangle_set cgal_to_indexed_triangle_set(const CGALMesh& cgalmesh) { return cgal_to_indexed_triangle_set(cgalmesh.m); }

// /////////////////////////////////////////////////////////////////////////////
// Boolean operations for CGAL meshes
// /////////////////////////////////////////////////////////////////////////////

static bool _cgal_diff(CGALMesh& A, CGALMesh& B, CGALMesh& R)
{
    const auto& p = CGALParams::throw_on_self_intersection(true);
    return CGALProc::corefine_and_compute_difference(A.m, B.m, R.m, p, p);
}

static bool _cgal_union(CGALMesh& A, CGALMesh& B, CGALMesh& R)
{
    const auto& p = CGALParams::throw_on_self_intersection(true);
    return CGALProc::corefine_and_compute_union(A.m, B.m, R.m, p, p);
}

static bool _cgal_intersection(CGALMesh& A, CGALMesh& B, CGALMesh& R)
{
    const auto& p = CGALParams::throw_on_self_intersection(true);
    return CGALProc::corefine_and_compute_intersection(A.m, B.m, R.m, p, p);
}

template<class Op> void _cgal_do(Op&& op, CGALMesh& A, CGALMesh& B)
{
    bool success = false;
    bool hw_fail = false;
    try {
        CGALMesh result;
        try_catch_signal({SIGSEGV, SIGFPE}, [&success, &A, &B, &result, &op] { success = op(A, B, result); }, [&] { hw_fail = true; });
        A = std::move(result); // In-place operation does not work
    } catch (...) {
        success = false;
    }

    if (hw_fail)
        throw Slic3r::HardCrash("CGAL mesh boolean operation crashed.");

    if (!success)
        throw Slic3r::RuntimeError("CGAL mesh boolean operation failed.");
}

void minus(CGALMesh& A, CGALMesh& B) { _cgal_do(_cgal_diff, A, B); }
void plus(CGALMesh& A, CGALMesh& B) { _cgal_do(_cgal_union, A, B); }
void intersect(CGALMesh& A, CGALMesh& B) { _cgal_do(_cgal_intersection, A, B); }
bool does_self_intersect(const CGALMesh& mesh) { return CGALProc::does_self_intersect(mesh.m); }
// BBS
void segment(CGALMesh& src, std::vector<CGALMesh>& dst, double smoothing_alpha = 0.5, int segment_number = 5)
{
    typedef boost::graph_traits<_EpicMesh>::face_descriptor  face_descriptor;
    typedef _EpicMesh::Property_map<face_descriptor, double> Facet_double_map;
    typedef CGAL::Face_filtered_graph<_EpicMesh>             Filtered_graph;

    _EpicMesh        mesh = src.m;
    Facet_double_map sdf_property_map;

    sdf_property_map = mesh.add_property_map<face_descriptor, double>("f:sdf").first;

    CGAL::sdf_values(mesh, sdf_property_map);

    // create a property-map for segment-ids
    typedef _EpicMesh::Property_map<face_descriptor, std::size_t> Facet_int_map;
    Facet_int_map segment_property_map = mesh.add_property_map<face_descriptor, std::size_t>("f:sid").first;
    ;
    // segment the mesh using default parameters for number of levels, and smoothing lambda
    // Any other scalar values can be used instead of using SDF values computed using the CGAL function
    std::size_t number_of_segments = CGAL::segmentation_from_sdf_values(mesh, sdf_property_map, segment_property_map, segment_number,
                                                                        smoothing_alpha);
    // print area of each segment and then put it in a Mesh and print it in an OFF file
    Filtered_graph segment_mesh(mesh);
    _EpicMesh      mesh_merged;
    for (std::size_t id = 0; id < number_of_segments; ++id) {
        segment_mesh.set_selected_faces(id, segment_property_map);
        // std::cout << "Segment " << id << "'s area is : " << CGAL::Polygon_mesh_processing::area(segment_mesh) << std::endl;
        _EpicMesh out;
        CGAL::copy_face_graph(segment_mesh, out);

        // save_CGALMesh("out.off", out);

        // fill holes
        typedef boost::graph_traits<_EpicMesh>::halfedge_descriptor halfedge_descriptor;
        std::vector<halfedge_descriptor>                            border_cycles;
        CGAL::Polygon_mesh_processing::extract_boundary_cycles(out, std::back_inserter(border_cycles));
        for (halfedge_descriptor h : border_cycles) {
            std::vector<face_descriptor> patch_facets;
#if defined(_WIN32) && CGAL_VERSION_NR < 1060000000
            CGAL::Polygon_mesh_processing::triangulate_hole(out, h, std::back_inserter(patch_facets));
#else
            CGAL::Polygon_mesh_processing::triangulate_hole(out, h,
                                                            CGAL::parameters::face_output_iterator(std::back_inserter(patch_facets)));
#endif
        }

        // if (id > 2) {
        //     mesh_merged.join(out);
        // }
        // else
        {
            dst.emplace_back(CGALMesh(out));
        }
    }
    // if (mesh_merged.is_empty() == false) {
    //     CGAL::Polygon_mesh_processing::stitch_borders(mesh_merged);
    //     dst.emplace_back(std::move(CGALMesh(mesh_merged)));
    // }
}

std::vector<TriangleMesh> segment(const TriangleMesh& src, double smoothing_alpha, int segment_number)
{
    CGALMesh in_cgal_mesh;
    MeshBoolean::cgal::triangle_mesh_to_cgal(src, in_cgal_mesh.m);
    std::vector<CGALMesh> out_cgal_meshes;
    segment(in_cgal_mesh, out_cgal_meshes, smoothing_alpha, segment_number);

    std::vector<TriangleMesh> out_meshes;
    for (auto& outf_cgal_mesh : out_cgal_meshes) {
        out_meshes.emplace_back(cgal_to_triangle_mesh(outf_cgal_mesh.m));
    }

    return out_meshes;
}

void merge(std::vector<_EpicMesh>& srcs, _EpicMesh& dst)
{
    _EpicMesh mesh_merged;
    for (size_t i = 0; i < srcs.size(); i++) {
        mesh_merged.join(srcs[i]);
    }
    if (mesh_merged.is_empty() == false) {
        CGAL::Polygon_mesh_processing::stitch_borders(mesh_merged);
        dst = std::move(mesh_merged);
    }
}

TriangleMesh merge(std::vector<TriangleMesh> meshes)
{
    std::vector<_EpicMesh> srcs(meshes.size());
    for (size_t i = 0; i < meshes.size(); i++) {
        MeshBoolean::cgal::triangle_mesh_to_cgal(meshes[i], srcs[i]);
    }
    _EpicMesh dst;
    merge(srcs, dst);
    return cgal_to_triangle_mesh(dst);
}

template<class Op> void _mesh_boolean_do(Op&& op, indexed_triangle_set& A, const indexed_triangle_set& B)
{
    CGALMesh meshA;
    CGALMesh meshB;
    triangle_mesh_to_cgal(A.vertices, A.indices, meshA.m);
    triangle_mesh_to_cgal(B.vertices, B.indices, meshB.m);

    _cgal_do(op, meshA, meshB);

    A = cgal_to_indexed_triangle_set(meshA.m);
}

template<class Op> void _mesh_boolean_do(Op&& op, TriangleMesh& A, const TriangleMesh& B)
{
    CGALMesh meshA;
    CGALMesh meshB;
    triangle_mesh_to_cgal(A.its.vertices, A.its.indices, meshA.m);
    triangle_mesh_to_cgal(B.its.vertices, B.its.indices, meshB.m);

    _cgal_do(op, meshA, meshB);

    A = cgal_to_triangle_mesh(meshA);
}

void minus(TriangleMesh& A, const TriangleMesh& B) { _mesh_boolean_do(_cgal_diff, A, B); }

void plus(TriangleMesh& A, const TriangleMesh& B) { _mesh_boolean_do(_cgal_union, A, B); }

void intersect(TriangleMesh& A, const TriangleMesh& B) { _mesh_boolean_do(_cgal_intersection, A, B); }

void minus(indexed_triangle_set& A, const indexed_triangle_set& B) { _mesh_boolean_do(_cgal_diff, A, B); }

void plus(indexed_triangle_set& A, const indexed_triangle_set& B) { _mesh_boolean_do(_cgal_union, A, B); }

void intersect(indexed_triangle_set& A, const indexed_triangle_set& B) { _mesh_boolean_do(_cgal_intersection, A, B); }

namespace {

struct ConnectedComponentVolumeMeasure
{
    size_t component_count = 0;
    double absolute_volume = 0.;
};

// Measure every face-connected shell independently, then discard only the
// orientation sign of each shell. A nesting-aware orientation repair changes
// the sign of an inward cavity without changing its geometry, so comparing the
// absolute volume of the complete mesh would incorrectly compare outer+inner
// against outer-inner. Summing per-shell absolute volumes remains sensitive to
// geometric volume loss while being invariant to whole-shell winding changes.
ConnectedComponentVolumeMeasure measure_connected_component_volumes(_EpicMesh& surface_mesh, const char* property_name)
{
    using face_descriptor = boost::graph_traits<_EpicMesh>::face_descriptor;

    auto component_map = surface_mesh.add_property_map<face_descriptor, size_t>(property_name, 0).first;
    const size_t component_count = CGALProc::connected_components(surface_mesh, component_map);
    std::vector<double> signed_volumes(component_count, 0.);

    const EpicKernel::Point_3 origin(0., 0., 0.);
    const EpicKernel::Compute_volume_3 compute_volume;
    for (face_descriptor face : surface_mesh.faces()) {
        const auto halfedge = surface_mesh.halfedge(face);
        signed_volumes[component_map[face]] +=
            compute_volume(origin, surface_mesh.point(target(halfedge, surface_mesh)),
                           surface_mesh.point(target(next(halfedge, surface_mesh), surface_mesh)),
                           surface_mesh.point(target(prev(halfedge, surface_mesh), surface_mesh)));
    }

    surface_mesh.remove_property_map(component_map);

    double absolute_volume = 0.;
    for (double signed_volume : signed_volumes)
        absolute_volume += std::abs(signed_volume);
    return {component_count, absolute_volume};
}

// Reuse Orca's nearby-edge matcher, but only on an already oriented manifold
// component. Do not use the legacy shortest-edge tolerance unboundedly: it can
// close intentional gaps. Independent components are never welded together.
bool close_small_cracks(indexed_triangle_set& component)
{
    namespace PMP = CGAL::Polygon_mesh_processing;
    if (its_num_open_edges(component) == 0 || component.indices.size() < 4 ||
        component.indices.size() > size_t(std::numeric_limits<int>::max()))
        return false;

    stl_file stl;
    stl.stats.type = inmemory;
    stl.stats.number_of_facets = uint32_t(component.indices.size());
    stl.facet_start.resize(component.indices.size());
    stl.neighbors_start.resize(component.indices.size());
    double shortest_edge = std::numeric_limits<double>::max();
    for (size_t i = 0; i < component.indices.size(); ++i) {
        stl_facet& facet = stl.facet_start[i];
        facet.normal.setZero();
        for (int corner = 0; corner < 3; ++corner) {
            facet.vertex[corner] = component.vertices[component.indices[i][corner]];
            shortest_edge = std::min(shortest_edge,
                (component.vertices[component.indices[i][corner]].cast<double>() -
                 component.vertices[component.indices[i][(corner + 1) % 3]].cast<double>()).norm());
        }
    }
    stl_get_size(&stl);
    const double diagonal = stl.stats.size.cast<double>().norm();
    const float tolerance = float(std::min({diagonal * 1.e-5, shortest_edge * 1.e-3, 0.005}));
    // ADMesh hashes coordinates into int32 grid cells, at half this tolerance
    // on the first pass. Avoid overflow for extreme scale/aspect ratios.
    if (!std::isfinite(diagonal) || !std::isfinite(tolerance) || tolerance <= 0.f ||
        diagonal / (0.5 * tolerance) >= double(std::numeric_limits<int32_t>::max()) - 2.)
        return false;

    stl_check_facets_exact(&stl);
    if (stl.stats.number_of_facets != component.indices.size())
        return false;
    stl_check_facets_nearby(&stl, tolerance * 0.5f);
    stl_check_facets_nearby(&stl, tolerance);
    if (stl.stats.edges_fixed == 0)
        return false;

    // Keep face provenance and cap the total displacement, not just each
    // individual match. A chain of tiny matches must not move a vertex far.
    for (size_t i = 0; i < component.indices.size(); ++i)
        for (int corner = 0; corner < 3; ++corner)
            if (!stl.facet_start[i].vertex[corner].allFinite() ||
                (stl.facet_start[i].vertex[corner].cast<double>() -
                 component.vertices[component.indices[i][corner]].cast<double>()).norm() > tolerance)
                return false;

    indexed_triangle_set candidate;
    stl_generate_shared_vertices(&stl, candidate);
    for (size_t i = 0; i < candidate.indices.size(); ++i)
        for (int corner = 0; corner < 3; ++corner)
            if ((candidate.vertices[candidate.indices[i][corner]].cast<double>() -
                 component.vertices[component.indices[i][corner]].cast<double>()).norm() > tolerance)
                return false;
    _EpicMesh surface;
    triangle_mesh_to_cgal(candidate.vertices, candidate.indices, surface);
    // Only accept a completely closed candidate. Otherwise leave the original
    // component for normal hole filling; do not conceal a bad snap with patches.
    if (surface.num_faces() != component.indices.size() || !CGAL::is_valid_polygon_mesh(surface) ||
        !CGAL::is_triangle_mesh(surface) || !CGAL::is_closed(surface))
        return false;
    std::vector<_EpicMesh::Face_index> degenerate;
    std::vector<_EpicMesh::Halfedge_index> non_manifold;
    PMP::degenerate_faces(surface, std::back_inserter(degenerate));
    PMP::non_manifold_vertices(surface, std::back_inserter(non_manifold));
    if (!degenerate.empty() || !non_manifold.empty() || PMP::does_self_intersect(surface))
        return false;

    BOOST_LOG_TRIVIAL(info) << "CGAL repair: event=small_crack_closed, tolerance=" << tolerance
                            << ", matched_edges=" << stl.stats.edges_fixed;
    component = std::move(candidate);
    return true;
}

void prepare_open_surface(_EpicMesh& surface)
{
    surface.collect_garbage();
    std::vector<indexed_triangle_set> components = its_split(cgal_to_indexed_triangle_set(surface));
    indexed_triangle_set prepared;
    size_t removed_faces = 0;
    bool changed = false;
    for (indexed_triangle_set& component : components) {
        // The legacy automatic cleanup discards facets with no edge neighbor.
        // A single triangle cannot enclose printable volume. Never remove a
        // component just because its area, thickness or volume is small.
        if (component.indices.size() == 1) {
            ++removed_faces;
            changed = true;
            continue;
        }
        changed = close_small_cracks(component) || changed;
        its_merge(prepared, component);
    }
    if (changed) {
        _EpicMesh candidate;
        triangle_mesh_to_cgal(prepared.vertices, prepared.indices, candidate);
        if (candidate.num_faces() != prepared.indices.size() || !CGAL::is_valid_polygon_mesh(candidate))
            throw std::runtime_error(L("Repair failed: basic boundary cleanup produced invalid topology."));
        surface = std::move(candidate);
        BOOST_LOG_TRIVIAL(info) << "CGAL repair: event=basic_boundary_cleanup, isolated_faces_removed=" << removed_faces;
    }
}

using RepairSoupFace  = std::vector<size_t>;
using RepairSoupEdge  = std::array<size_t, 2>;
using RepairSoupEdges = std::map<RepairSoupEdge, std::vector<size_t>>;

RepairSoupEdge soup_edge(size_t a, size_t b) { return {std::min(a, b), std::max(a, b)}; }

RepairSoupEdges soup_edges(const std::vector<RepairSoupFace>& faces)
{
    RepairSoupEdges edges;
    for (size_t i = 0; i < faces.size(); ++i)
        for (size_t j = 0; j < 3; ++j)
            edges[soup_edge(faces[i][j], faces[i][(j + 1) % 3])].push_back(i);
    return edges;
}

// Only subdivide an existing boundary triangle along EXACTLY collinear source
// vertices. No snapping, new coordinates, area removal or hole patches. Work
// on a private soup: incomplete/budget-limited preparation is never published.
bool conform_soup_boundary(const std::vector<_EpicMesh::Point>& points, std::vector<RepairSoupFace>& faces, size_t& splits)
{
    size_t budget = 2000000;
    for (unsigned pass = 0; pass < 8; ++pass) {
        const auto       edges = soup_edges(faces);
        std::set<size_t> boundary_vertices;
        for (const auto& entry : edges)
            if (entry.second.size() == 1)
                boundary_vertices.insert(entry.first.begin(), entry.first.end());
        std::vector<RepairSoupFace> next_faces;
        bool                        changed = false;
        for (const auto& face : faces) {
            bool divided = false;
            for (size_t j = 0; j < 3 && !divided; ++j) {
                const size_t a = face[j], b = face[(j + 1) % 3], c = face[(j + 2) % 3];
                if (edges.at(soup_edge(a, b)).size() != 1)
                    continue;
                std::vector<size_t> middle;
                for (size_t v : boundary_vertices) {
                    if (budget == 0)
                        return false;
                    --budget;
                    if (v != a && v != b && CGAL::collinear(points[a], points[v], points[b]) &&
                        CGAL::collinear_are_strictly_ordered_along_line(points[a], points[v], points[b]))
                        middle.push_back(v);
                }
                if (middle.empty())
                    continue;
                // Lexicographic order is monotone along an exact straight edge.
                std::sort(middle.begin(), middle.end(), [&](size_t x, size_t y) { return points[x] < points[y]; });
                if (points[b] < points[a])
                    std::reverse(middle.begin(), middle.end());
                size_t previous = a;
                for (size_t v : middle) {
                    next_faces.push_back({previous, v, c});
                    previous = v;
                }
                next_faces.push_back({previous, b, c});
                splits += middle.size();
                if (splits > 1024)
                    return false;
                changed = divided = true;
            }
            if (!divided)
                next_faces.push_back(face);
        }
        faces = std::move(next_faces);
        if (!changed)
            return true;
    }
    return false;
}

// Certify that a deleted triangle's OPEN interior is strictly inside the
// retained solid. A centroid test alone is insufficient: a large triangle can
// cross an exterior wall or bridge a through-hole. Exact intersections may
// touch its edges, but never its interior or overlap it in area.
bool internal_soup_faces(const std::vector<_EpicMesh::Point>& points, const std::vector<RepairSoupFace>& removed, const _EpicMesh& retained)
{
    _EpecMesh exact;
    CGAL::copy_face_graph(retained, exact);
    using Side = CGAL::Side_of_triangle_mesh<_EpecMesh, EpecKernel>;
    Side::AABB_tree tree(exact.faces().begin(), exact.faces().end(), exact);
    tree.build();
    Side   side(tree);
    size_t intersections = 0;
    for (const auto& face : removed) {
        const auto point = [&](size_t i) {
            const auto& p = points[face[i]];
            return EpecKernel::Point_3(p.x(), p.y(), p.z());
        };
        const EpecKernel::Triangle_3 triangle(point(0), point(1), point(2));
        if (side(CGAL::centroid(triangle)) != CGAL::ON_BOUNDED_SIDE)
            return false;
        std::vector<_EpecMesh::Face_index> contacts;
        tree.all_intersected_primitives(triangle, std::back_inserter(contacts));
        intersections += contacts.size();
        if (intersections > 20000)
            return false;
        for (const auto f : contacts) {
            const auto                   h = exact.halfedge(f);
            const EpecKernel::Triangle_3 other(exact.point(target(h, exact)), exact.point(target(next(h, exact), exact)),
                                               exact.point(target(prev(h, exact), exact)));
            const auto                   contact = CGAL::intersection(triangle, other);
            if (!contact)
                return false;
            bool boundary_only = false;
            for (int j = 0; j < 3; ++j) {
                const EpecKernel::Segment_3 edge(triangle.vertex(j), triangle.vertex((j + 1) % 3));
                if (const auto* p = boost::get<EpecKernel::Point_3>(&*contact))
                    boundary_only = boundary_only || edge.has_on(*p);
                else if (const auto* s = boost::get<EpecKernel::Segment_3>(&*contact))
                    boundary_only = boundary_only || (edge.has_on(s->source()) && edge.has_on(s->target()));
            }
            if (!boundary_only)
                return false;
        }
    }
    return true;
}

// CAD/STL exports can retain planar partitions at rounded/joined solid seams.
// Orienting those three-face edges first splits the soup; filling the resulting
// artificial holes may then cover real openings. Try a bounded, certified path
// BEFORE splitting. No general self-union, largest-shell selection or tolerance
// based face deletion: every positive-area face is either retained exactly (up
// to subdivision/winding) or certified to lie inside the final closed solid.
bool repair_soup_topology(const std::vector<_EpicMesh::Point>& source_points,
                          const std::vector<RepairSoupFace>&   source_faces,
                          _EpicMesh&                           result)
{
    if (source_faces.empty() || source_faces.size() > 20000)
        return false;
    for (const auto& f : source_faces)
        if (f.size() != 3 || CGAL::collinear(source_points[f[0]], source_points[f[1]], source_points[f[2]]))
            return false;

    auto   faces  = source_faces;
    size_t splits = 0;
    if (!conform_soup_boundary(source_points, faces, splits))
        return false;
    const auto                       edges = soup_edges(faces);
    std::vector<std::vector<size_t>> neighbors(faces.size());
    for (const auto& entry : edges)
        if (entry.second.size() == 2) {
            neighbors[entry.second[0]].push_back(entry.second[1]);
            neighbors[entry.second[1]].push_back(entry.second[0]);
        }
    std::vector<bool> visited(faces.size(), false), remove(faces.size(), false);
    for (size_t seed = 0; seed < faces.size(); ++seed) {
        if (visited[seed])
            continue;
        std::vector<size_t> patch{seed};
        visited[seed] = true;
        for (size_t i = 0; i < patch.size(); ++i)
            for (size_t n : neighbors[patch[i]])
                if (!visited[n]) {
                    visited[n] = true;
                    patch.push_back(n);
                }
        bool        planar = true, singular_boundary = false, supported_boundary = true;
        const auto& base = faces[seed];
        for (size_t f : patch)
            for (size_t j = 0; j < 3; ++j) {
                planar = planar &&
                         CGAL::coplanar(source_points[base[0]], source_points[base[1]], source_points[base[2]], source_points[faces[f][j]]);
                const size_t count = edges.at(soup_edge(faces[f][j], faces[f][(j + 1) % 3])).size();
                singular_boundary  = singular_boundary || count == 3;
                supported_boundary = supported_boundary && (count == 2 || count == 3);
            }
        if (planar && singular_boundary && supported_boundary)
            for (size_t f : patch)
                remove[f] = true;
    }
    std::vector<RepairSoupFace> kept, removed;
    for (size_t f = 0; f < faces.size(); ++f)
        (remove[f] ? removed : kept).push_back(faces[f]);
    if ((splits == 0 && removed.empty()) || kept.empty() || removed.size() > 2048)
        return false;

    auto points = source_points;
    if (!CGALProc::orient_polygon_soup(points, kept) || points.size() != source_points.size() ||
        !CGALProc::is_polygon_soup_a_polygon_mesh(kept))
        return false;
    _EpicMesh candidate;
    CGALProc::polygon_soup_to_polygon_mesh(points, kept, candidate);
    if (candidate.num_faces() != kept.size() || !CGAL::is_valid_polygon_mesh(candidate) || !CGAL::is_triangle_mesh(candidate) ||
        !CGAL::is_closed(candidate))
        return false;
    std::vector<_EpicMesh::Halfedge_index> singular;
    CGALProc::non_manifold_vertices(candidate, std::back_inserter(singular));
    if (!singular.empty() || CGALProc::does_self_intersect(candidate))
        return false;
    if (!removed.empty() && !internal_soup_faces(source_points, removed, candidate))
        return false;

    CGALProc::orient_to_bound_a_volume(candidate);
    CGALProc::remove_isolated_vertices(candidate);
    candidate.collect_garbage();
    // Recheck the actual float/welded representation, not merely the temporary
    // Surface_mesh connectivity. Never retain an unvalidated partial candidate.
    if (requires_repair(TriangleMesh(cgal_to_indexed_triangle_set(candidate))))
        return false;
    BOOST_LOG_TRIVIAL(info) << "CGAL repair: event=soup_topology_certified, boundary_splits=" << splits
                            << ", internal_faces_removed=" << removed.size() << ", faces=" << source_faces.size() << "->"
                            << candidate.num_faces() << ", moved_vertices=0, hole_patches=0";
    result = std::move(candidate);
    return true;
}

} // namespace

bool does_self_intersect(const TriangleMesh& mesh)
{
    CGALMesh cgalm;
    triangle_mesh_to_cgal(mesh.its.vertices, mesh.its.indices, cgalm.m);
    return CGALProc::does_self_intersect(cgalm.m);
}

bool requires_repair(const TriangleMesh& mesh)
{
    if (mesh.its.vertices.empty() || mesh.its.indices.empty())
        return true;
    for (const stl_vertex& vertex : mesh.its.vertices)
        if (!vertex.allFinite())
            return true;
    for (const stl_triangle_vertex_indices& face : mesh.its.indices)
        for (size_t i = 0; i < 3; ++i)
            if (face[i] < 0 || size_t(face[i]) >= mesh.its.vertices.size())
                return true;

    indexed_triangle_set compacted = mesh.its;
    if (its_compactify_vertices(compacted, true) > 0)
        return true;

    CGALMesh cgal_mesh;
    triangle_mesh_to_cgal(compacted.vertices, compacted.indices, cgal_mesh.m);

    // Surface_mesh::add_face rejects degenerate, duplicated, or locally
    // inconsistent faces. A count mismatch therefore means the original soup
    // still needs the cleanup/orientation pipeline even if its undirected edge
    // count happened to look closed.
    if (cgal_mesh.m.is_empty() || cgal_mesh.m.num_faces() != compacted.indices.size() || !CGAL::is_valid_polygon_mesh(cgal_mesh.m) ||
        !CGAL::is_triangle_mesh(cgal_mesh.m) || !CGAL::is_closed(cgal_mesh.m))
        return true;

    using face_descriptor     = boost::graph_traits<_EpicMesh>::face_descriptor;
    using halfedge_descriptor = boost::graph_traits<_EpicMesh>::halfedge_descriptor;

    std::vector<face_descriptor> degenerate_faces;
    CGALProc::degenerate_faces(cgal_mesh.m, std::back_inserter(degenerate_faces));
    if (!degenerate_faces.empty())
        return true;

    std::vector<halfedge_descriptor> non_manifold_vertices;
    CGALProc::non_manifold_vertices(cgal_mesh.m, std::back_inserter(non_manifold_vertices));
    if (!non_manifold_vertices.empty())
        return true;

    // All volume-orientation predicates below assume a self-intersection-free
    // triangle mesh.
    if (CGALProc::does_self_intersect(cgal_mesh.m))
        return true;

    CGAL::Constant_property_map<face_descriptor, std::size_t> volume_id_map(0);
    std::vector<std::size_t>                                  nesting_levels;
    std::vector<bool>                                         outward_orientations;

    CGALProc::volume_connected_components(cgal_mesh.m, volume_id_map,
                                          CGALParams::nesting_levels(boost::ref(nesting_levels))
                                              .is_cc_outward_oriented(boost::ref(outward_orientations))
                                              .do_orientation_tests(true)
                                              .i_used_for_volume_orientation(true));

    if (outward_orientations.empty())
        return true;
    if (nesting_levels.empty())
        return outward_orientations.size() != 1 || !outward_orientations.front();
    if (nesting_levels.size() != outward_orientations.size())
        return true;

    for (size_t component_index = 0; component_index < nesting_levels.size(); ++component_index) {
        const bool should_be_outward = nesting_levels[component_index] % 2 == 0;
        if (outward_orientations[component_index] != should_be_outward)
            return true;
    }
    return false;
}

void CGALMeshDeleter::operator()(CGALMesh* ptr) { delete ptr; }

bool does_bound_a_volume(const CGALMesh& mesh) { return CGAL::is_closed(mesh.m) && CGALProc::does_bound_a_volume(mesh.m); }

bool empty(const CGALMesh& mesh) { return mesh.m.is_empty(); }

namespace {

using RepairPointKey        = std::array<double, 3>;
using RepairFaceKey         = std::array<RepairPointKey, 3>;
using RepairEdgeKey         = std::array<RepairPointKey, 2>;
using RepairIntersectionKey = std::array<RepairFaceKey, 2>;

RepairPointKey repair_point_key(const _EpicMesh::Point& p) { return {double(p.x()), double(p.y()), double(p.z())}; }

RepairFaceKey repair_face_key(const _EpicMesh& surface, _EpicMesh::Face_index face)
{
    const auto    h   = surface.halfedge(face);
    RepairFaceKey key = {repair_point_key(surface.point(target(h, surface))),
                         repair_point_key(surface.point(target(next(h, surface), surface))),
                         repair_point_key(surface.point(target(prev(h, surface), surface)))};
    // Ignore vertex/face numbering, but retain winding.
    std::rotate(key.begin(), std::min_element(key.begin(), key.end()), key.end());
    return key;
}

bool partial_repair_surface(const TriangleMesh& mesh, _EpicMesh& surface)
{
    if (mesh.empty() || mesh.its.vertices.empty())
        return false;
    for (const auto& point : mesh.its.vertices)
        if (!point.allFinite())
            return false;
    for (const auto& face : mesh.its.indices)
        for (int i = 0; i < 3; ++i)
            if (face[i] < 0 || size_t(face[i]) >= mesh.its.vertices.size())
                return false;
    auto compacted = mesh.its;
    its_compactify_vertices(compacted, true);
    triangle_mesh_to_cgal(compacted.vertices, compacted.indices, surface);
    if (surface.is_empty() || surface.num_faces() != compacted.indices.size() || !CGAL::is_valid_polygon_mesh(surface) ||
        !CGAL::is_triangle_mesh(surface))
        return false;
    std::vector<_EpicMesh::Face_index>     degenerate;
    std::vector<_EpicMesh::Halfedge_index> non_manifold;
    CGALProc::degenerate_faces(surface, std::back_inserter(degenerate));
    CGALProc::non_manifold_vertices(surface, std::back_inserter(non_manifold));
    // Partial mode is not permission to run predicates on an invalid graph or
    // to accept singular/split soups. Those still use the strict repair path.
    return degenerate.empty() && non_manifold.empty();
}

std::set<RepairEdgeKey> repair_boundary_keys(const _EpicMesh& surface)
{
    std::set<RepairEdgeKey> keys;
    for (const auto h : surface.halfedges())
        if (CGAL::is_border(h, surface))
            keys.insert({repair_point_key(surface.point(source(h, surface))), repair_point_key(surface.point(target(h, surface)))});
    return keys;
}

bool repair_intersection_keys(const _EpicMesh& surface, std::set<RepairIntersectionKey>& keys)
{
    constexpr unsigned                                                   limit = 20000;
    std::vector<std::pair<_EpicMesh::Face_index, _EpicMesh::Face_index>> pairs;
    CGALProc::self_intersections<CGAL::Sequential_tag>(surface, std::back_inserter(pairs), CGALParams::maximum_number(limit + 1));
    // Incomplete evidence cannot authorize a partial result.
    if (pairs.size() > limit)
        return false;
    for (const auto& pair : pairs) {
        RepairIntersectionKey key = {repair_face_key(surface, pair.first), repair_face_key(surface, pair.second)};
        std::sort(key.begin(), key.end());
        keys.insert(key);
    }
    return true;
}

} // namespace

bool is_safe_partial_repair(const TriangleMesh& source, const TriangleMesh& candidate)
{
    namespace PMP = CGAL::Polygon_mesh_processing;
    _EpicMesh before, after;
    if (!partial_repair_surface(source, before) || !partial_repair_surface(candidate, after))
        return false;

    const auto before_boundaries = repair_boundary_keys(before);
    const auto after_boundaries  = repair_boundary_keys(after);
    if (!std::includes(before_boundaries.begin(), before_boundaries.end(), after_boundaries.begin(), after_boundaries.end()))
        return false;
    std::set<RepairIntersectionKey> before_intersections, after_intersections;
    if (!repair_intersection_keys(before, before_intersections) || !repair_intersection_keys(after, after_intersections) ||
        !std::includes(before_intersections.begin(), before_intersections.end(), after_intersections.begin(), after_intersections.end()))
        return false;
    if (after_boundaries.size() == before_boundaries.size() && after_intersections.size() == before_intersections.size())
        return false;

    // A lower pair count alone is insufficient. All remaining intersections
    // and boundary segments must be unchanged source geometry. Also prohibit
    // flipping an otherwise unchanged face while retaining residual defects.
    std::map<RepairFaceKey, RepairFaceKey> source_winding;
    for (const auto face : before.faces()) {
        const auto oriented = repair_face_key(before, face);
        auto       key      = oriented;
        std::sort(key.begin(), key.end());
        if (!source_winding.emplace(key, oriented).second)
            return false;
    }
    for (const auto face : after.faces()) {
        const auto oriented = repair_face_key(after, face);
        auto       key      = oriented;
        std::sort(key.begin(), key.end());
        const auto it = source_winding.find(key);
        if (it != source_winding.end() && it->second != oriented)
            return false;
    }

    const auto before_measure = measure_connected_component_volumes(before, "f:partial_before");
    const auto after_measure  = measure_connected_component_volumes(after, "f:partial_after");
    if (before_measure.component_count != after_measure.component_count)
        return false;
    const auto   a        = PMP::bbox(before);
    const auto   b        = PMP::bbox(after);
    const double diagonal = std::hypot(a.xmax() - a.xmin(), a.ymax() - a.ymin(), a.zmax() - a.zmin());
    const double limit    = std::clamp(diagonal * 1.e-4, 0.005, 0.05);
    if (std::abs(a.xmin() - b.xmin()) > limit || std::abs(a.xmax() - b.xmax()) > limit || std::abs(a.ymin() - b.ymin()) > limit ||
        std::abs(a.ymax() - b.ymax()) > limit || std::abs(a.zmin() - b.zmin()) > limit || std::abs(a.zmax() - b.zmax()) > limit)
        return false;
    const double before_area = double(PMP::area(before));
    const double after_area  = double(PMP::area(after));
    if (!std::isfinite(before_area) || !std::isfinite(after_area) || before_area <= 0. || after_area <= 0.)
        return false;
    if (before_boundaries.empty()) {
        // Match the existing closed-surface fidelity limits. These signed-sum
        // volume measures are only change guards, not physical volume claims
        // for a self-intersecting source.
        if (std::abs(after_area - before_area) / before_area > 2.e-3 || !std::isfinite(before_measure.absolute_volume) ||
            !std::isfinite(after_measure.absolute_volume) ||
            std::abs(after_measure.absolute_volume - before_measure.absolute_volume) / std::max(before_measure.absolute_volume, 1.e-9) >
                1.e-3)
            return false;
    } else if (after_area < before_area * (1. - 2.e-3)) {
        return false;
    }
    // An open input may gain hole patches; its existing surface must survive.
    const double error_bound = std::max(1.e-5, limit * 0.01);
    const double distance    = before_boundaries.empty() ?
                                   PMP::bounded_error_symmetric_Hausdorff_distance<CGAL::Sequential_tag>(before, after, error_bound) :
                                   PMP::bounded_error_Hausdorff_distance<CGAL::Sequential_tag>(before, after, error_bound);
    const bool   accepted    = std::isfinite(distance) && distance + error_bound <= limit;
    BOOST_LOG_TRIVIAL(debug) << "CGAL partial repair: event=progress_checked, accepted=" << accepted
                             << ", boundaries=" << before_boundaries.size() << "->" << after_boundaries.size()
                             << ", intersections=" << before_intersections.size() << "->" << after_intersections.size()
                             << ", hausdorff=" << distance << "/" << limit;
    return accepted;
}

size_t repair_degenerate_edge_caps(const TriangleMesh& source, TriangleMesh& result)
{
    namespace PMP  = CGAL::Polygon_mesh_processing;
    using Face     = _EpicMesh::Face_index;
    using Halfedge = _EpicMesh::Halfedge_index;
    if (source.empty() || source.its.vertices.empty())
        return 0;
    for (const auto& vertex : source.its.vertices)
        if (!vertex.allFinite())
            return 0;
    bool has_degenerate_face = false;
    for (const auto& face : source.its.indices) {
        for (int corner = 0; corner < 3; ++corner)
            if (face[corner] < 0 || size_t(face[corner]) >= source.its.vertices.size())
                return 0;
        if (!has_degenerate_face) {
            const auto point = [&](int corner) {
                const auto& v = source.its.vertices[face[corner]];
                return EpicKernel::Point_3(v.x(), v.y(), v.z());
            };
            has_degenerate_face = CGAL::collinear(point(0), point(1), point(2));
        }
    }
    if (!has_degenerate_face)
        return 0;

    // This is a new private baseline, never the mutated output of a failed
    // repair hook. Use the same exact coordinate welding as the existing CGAL
    // path, and reject conversions that silently dropped or split any faces.
    auto compacted = source.its;
    its_compactify_vertices(compacted, true);
    _EpicMesh surface;
    triangle_mesh_to_cgal(compacted.vertices, compacted.indices, surface);
    if (surface.is_empty() || surface.num_faces() != compacted.indices.size() || !CGAL::is_valid_polygon_mesh(surface) ||
        !CGAL::is_triangle_mesh(surface) || !CGAL::is_closed(surface))
        return 0;
    std::vector<Halfedge> non_manifold;
    PMP::non_manifold_vertices(surface, std::back_inserter(non_manifold));
    if (!non_manifold.empty())
        return 0;
    std::vector<Face> degenerate;
    PMP::degenerate_faces(surface, std::back_inserter(degenerate));
    if (degenerate.empty())
        return 0;

    const auto intersections_complete = [](const _EpicMesh& mesh) {
        // CGAL also emits (f,f) records for degenerate faces. Counting those
        // toward this bound is conservative; a truncated query never approves
        // a new retained baseline. Do not require old intersections to be zero.
        constexpr unsigned                 limit = 20000;
        std::vector<std::pair<Face, Face>> pairs;
        PMP::self_intersections<CGAL::Sequential_tag>(mesh, std::back_inserter(pairs), CGALParams::maximum_number(limit + 1));
        return pairs.size() <= limit;
    };
    size_t repaired = 0;
    for (const Face face : degenerate) {
        if (!PMP::is_degenerate_triangle_face(face, surface))
            continue;
        bool isolated = true;
        for (const Halfedge h : halfedges_around_face(halfedge(face, surface), surface))
            if (PMP::is_degenerate_triangle_face(CGAL::face(opposite(h, surface), surface), surface)) {
                isolated = false;
                break;
            }
        if (!isolated)
            continue; // No normalization of connected degenerate clusters.

        for (const Halfedge h : halfedges_around_face(halfedge(face, surface), surface)) {
            const auto  a  = CGAL::source(h, surface);
            const auto  c  = target(h, surface);
            const auto  b  = target(next(h, surface), surface);
            const auto& pa = surface.point(a);
            const auto& pb = surface.point(b);
            const auto& pc = surface.point(c);
            if (pa == pb || pb == pc || pa == pc || !CGAL::collinear(pa, pb, pc) ||
                !CGAL::collinear_are_strictly_ordered_along_line(pa, pb, pc))
                continue;
            const auto d = target(next(opposite(h, surface), surface), surface);
            if (halfedge(b, d, surface).second)
                continue; // Euler flip would introduce an existing diagonal.

            if (repaired == 0 && !intersections_complete(surface)) {
                BOOST_LOG_TRIVIAL(info) << "Model repair: event=topology_preparation_deferred, reason=intersection_limit";
                return 0;
            }
            // Exact certificate for this operation: b lies strictly on a-c;
            // the opposite face a-c-d is nondegenerate. Flipping a-c to b-d
            // replaces the zero-area cap and a-c-d by its two oriented pieces.
            // No points move, no positive-area surface disappears or appears,
            // and the manifold graph/component structure is unchanged. Hence
            // existing self-intersections may remain but cannot spread to a
            // new geometric region. This is NOT permission for arbitrary CGAL
            // cleanup, vertex collapse, remeshing or a lower pair count alone.
            CGAL::Euler::flip_edge(h, surface);
            ++repaired;
            break; // The face cycle changed; do not advance its old iterator.
        }
    }
    if (repaired == 0)
        return 0;
    if (!CGAL::is_valid_polygon_mesh(surface) || !CGAL::is_triangle_mesh(surface) || !CGAL::is_closed(surface))
        throw std::runtime_error(L("Repair failed: basic boundary cleanup produced invalid topology."));
    std::vector<Face> remaining;
    PMP::degenerate_faces(surface, std::back_inserter(remaining));
    non_manifold.clear();
    PMP::non_manifold_vertices(surface, std::back_inserter(non_manifold));
    if (!non_manifold.empty() || remaining.size() + repaired != degenerate.size())
        throw std::runtime_error(L("Repair failed: basic boundary cleanup produced invalid topology."));
    if (!intersections_complete(surface)) {
        BOOST_LOG_TRIVIAL(info) << "Model repair: event=topology_preparation_deferred, reason=intersection_limit_after";
        return 0;
    }

    // Flips allocate/remove no vertices or faces. Round-tripping therefore
    // reuses precisely the source float coordinates, not constructed doubles.
    TriangleMesh candidate(cgal_to_indexed_triangle_set(surface));
    if (candidate.facets_count() != source.facets_count() || its_num_open_edges(candidate.its) != 0)
        throw std::runtime_error(L("Repair failed: basic boundary cleanup produced invalid topology."));
    result = std::move(candidate);
    BOOST_LOG_TRIVIAL(info) << "Model repair: event=topology_preparation_validated, degenerate_caps=" << repaired
                            << ", remaining_degenerate=" << remaining.size() << ", faces=" << source.facets_count() << "->"
                            << result.facets_count() << ", moved_vertices=0";
    return repaired;
}

static bool repair_impl(TriangleMesh& mesh, RepairedMeshErrors* repaired_errors, std::string* error, TriangleMesh* partial_result)
{
    namespace PMP = CGAL::Polygon_mesh_processing;

    if (error != nullptr)
        error->clear();
    if (repaired_errors != nullptr)
        repaired_errors->clear();
    if (partial_result != nullptr)
        *partial_result = TriangleMesh{};

    const char* stage = "input_validation";
    const auto  fail  = [error, &stage, partial_result](const std::string& message, bool invalid_data = false) {
        BOOST_LOG_TRIVIAL(warning) << "CGAL repair: event=failed, stage=" << stage << ", reason=" << message;
        if (error != nullptr)
            *error = message;
        if (invalid_data && partial_result != nullptr)
            throw std::runtime_error(message);
        return false;
    };

    if (mesh.its.vertices.empty() || mesh.its.indices.empty())
        return fail("Repair failed: mesh is empty.", true);

    for (const stl_vertex& vertex : mesh.its.vertices)
        if (!vertex.allFinite())
            return fail("Repair failed: mesh contains non-finite vertices.", true);

    for (const stl_triangle_vertex_indices& face : mesh.its.indices) {
        for (size_t i = 0; i < 3; ++i) {
            const int vertex_index = face[i];
            if (vertex_index < 0 || size_t(vertex_index) >= mesh.its.vertices.size())
                return fail("Repair failed: mesh contains an invalid vertex index.", true);
        }
    }

    try {
        bool       partial_checkpoints_allowed = true;
        const auto checkpoint                  = [&](const _EpicMesh& surface, const char* checkpoint_stage) {
            if (partial_result == nullptr || !partial_checkpoints_allowed || surface.is_empty() || !CGAL::is_valid_polygon_mesh(surface) ||
                !CGAL::is_triangle_mesh(surface))
                return;
            // Validate the float representation that ModelVolume/STL/OBJ will
            // actually use, not just CGAL's double-coordinate intermediate.
            _EpicMesh compact = surface;
            compact.collect_garbage();
            for (const auto vertex : compact.vertices()) {
                const auto p = repair_point_key(compact.point(vertex));
                for (const auto coordinate : p)
                    if (!std::isfinite(coordinate) || std::abs(coordinate) > double(std::numeric_limits<float>::max()))
                        throw std::runtime_error("Repair failed: converted mesh contains non-finite vertices.");
            }
            TriangleMesh candidate(cgal_to_indexed_triangle_set(compact));
            if (!is_safe_partial_repair(mesh, candidate) ||
                (!partial_result->empty() && !is_safe_partial_repair(*partial_result, candidate)))
                return;
            *partial_result = std::move(candidate);
            BOOST_LOG_TRIVIAL(info) << "CGAL repair: event=partial_checkpoint, stage=" << checkpoint_stage
                                    << ", faces=" << mesh.facets_count() << "->" << partial_result->facets_count()
                                    << ", open_edges=" << its_num_open_edges(mesh.its) << "->" << its_num_open_edges(partial_result->its);
        };
        BOOST_LOG_TRIVIAL(info) << "CGAL repair: event=started, vertices=" << mesh.its.vertices.size()
                                << ", faces=" << mesh.its.indices.size();
        stage                          = "closed_candidate";
        indexed_triangle_set compacted = mesh.its;
        its_compactify_vertices(compacted, true);
        if (compacted.vertices.empty() || compacted.indices.empty())
            return fail("Repair failed: mesh cleanup removed all geometry.", true);

        const bool               input_had_no_open_edges = its_num_open_edges(compacted) == 0;
        _EpicMesh                cgal_mesh;
        std::optional<_EpicMesh> fallback_fidelity_reference;
        bool                     soup_vertices_split = false;
        bool                     direct_closed_candidate = false;
        std::string              direct_candidate_error;
        try {
            // A closed triangle soup may have zero-area faces and genuine
            // self-intersections even when every directed edge is paired. Keep
            // its topology intact while CGAL repairs those local defects, then
            // accept the experimental result only under strict topology and
            // geometry-fidelity gates. The original TriangleMesh remains
            // untouched until the final conversion below.
            _EpicMesh closed_candidate;
            triangle_mesh_to_cgal(compacted.vertices, compacted.indices, closed_candidate);
            direct_closed_candidate = !closed_candidate.is_empty() && closed_candidate.num_faces() == compacted.indices.size() &&
                                      CGAL::is_valid_polygon_mesh(closed_candidate) && CGAL::is_triangle_mesh(closed_candidate) &&
                                      CGAL::is_closed(closed_candidate);

            const bool closed_cleanup_complete = direct_closed_candidate && PMP::remove_degenerate_faces(closed_candidate);
            if (direct_closed_candidate)
                BOOST_LOG_TRIVIAL(info) << "CGAL repair: event=closed_degenerate_cleanup, completed=" << closed_cleanup_complete;
            if (closed_cleanup_complete) {
                PMP::remove_isolated_vertices(closed_candidate);
                closed_candidate.collect_garbage();

                using face_descriptor     = boost::graph_traits<_EpicMesh>::face_descriptor;
                using halfedge_descriptor = boost::graph_traits<_EpicMesh>::halfedge_descriptor;

                std::vector<face_descriptor> remaining_degenerate_faces;
                PMP::degenerate_faces(closed_candidate, std::back_inserter(remaining_degenerate_faces));

                if (remaining_degenerate_faces.empty() && CGAL::is_valid_polygon_mesh(closed_candidate) &&
                    CGAL::is_triangle_mesh(closed_candidate) && CGAL::is_closed(closed_candidate)) {
                    // Exact zero-area faces and any vertices referenced only by
                    // them are intentionally excluded from the fidelity
                    // baseline. They have no printable surface or volume and
                    // may be arbitrarily far from the real model. All
                    // self-intersection edits after this canonical cleanup are
                    // constrained by the gates below.
                    _EpicMesh          fidelity_reference = closed_candidate;
                    const CGAL::Bbox_3 reference_bbox     = PMP::bbox(fidelity_reference);
                    const double       bbox_dx            = reference_bbox.xmax() - reference_bbox.xmin();
                    const double       bbox_dy            = reference_bbox.ymax() - reference_bbox.ymin();
                    const double       bbox_dz            = reference_bbox.zmax() - reference_bbox.zmin();
                    const double       bbox_diagonal      = std::sqrt(bbox_dx * bbox_dx + bbox_dy * bbox_dy + bbox_dz * bbox_dz);
                    const double       hausdorff_limit    = std::clamp(bbox_diagonal * 1.e-4, 0.005, 0.05);

                    bool self_intersections_repaired = true;
                    if (PMP::does_self_intersect(closed_candidate)) {
#if defined(_WIN32) && CGAL_VERSION_NR < 1060000000
                        self_intersections_repaired = PMP::experimental::remove_self_intersections(closed_candidate,
                                                                                                   PMP::parameters::preserve_genus(true)
                                                                                                       .apply_per_connected_component(true)
                                                                                                       .number_of_iterations(7)
                                                                                                       .polyhedral_envelope_epsilon(
                                                                                                           hausdorff_limit));
#else
                        self_intersections_repaired = PMP::experimental::remove_self_intersections(closed_candidate,
                                                                                                   PMP::parameters::preserve_genus(true)
                                                                                                       .apply_per_connected_component(true)
                                                                                                       .number_of_iterations(7)
                                                                                                       .use_smoothing(false)
                                                                                                       .polyhedral_envelope_epsilon(
                                                                                                           hausdorff_limit));
#endif
                    }
                    closed_candidate.collect_garbage();

                    checkpoint(closed_candidate, "closed_defect_repair");

                    remaining_degenerate_faces.clear();
                    PMP::degenerate_faces(closed_candidate, std::back_inserter(remaining_degenerate_faces));
                    std::vector<halfedge_descriptor> non_manifold_vertices;
                    PMP::non_manifold_vertices(closed_candidate, std::back_inserter(non_manifold_vertices));

                    bool topology_valid = self_intersections_repaired && remaining_degenerate_faces.empty() &&
                                          non_manifold_vertices.empty() && CGAL::is_valid_polygon_mesh(closed_candidate) &&
                                          CGAL::is_triangle_mesh(closed_candidate) && CGAL::is_closed(closed_candidate) &&
                                          !PMP::does_self_intersect(closed_candidate);
                    BOOST_LOG_TRIVIAL(info) << "CGAL repair: event=closed_topology_checked, accepted=" << topology_valid
                                            << ", self_repair_completed=" << self_intersections_repaired
                                            << ", remaining_degenerate=" << remaining_degenerate_faces.size()
                                            << ", non_manifold_vertices=" << non_manifold_vertices.size();
                    if (topology_valid) {
                        PMP::orient_to_bound_a_volume(closed_candidate);
                        topology_valid = PMP::does_bound_a_volume(closed_candidate);
                    }

                    if (topology_valid) {
                        const ConnectedComponentVolumeMeasure reference_measure = measure_connected_component_volumes(
                            fidelity_reference, "f:model_repair_reference_component");
                        const ConnectedComponentVolumeMeasure repaired_measure = measure_connected_component_volumes(
                            closed_candidate, "f:model_repair_repaired_component");
                        const size_t       reference_components = reference_measure.component_count;
                        const size_t       repaired_components  = repaired_measure.component_count;
                        const CGAL::Bbox_3 repaired_bbox        = PMP::bbox(closed_candidate);
                        const double       reference_volume     = reference_measure.absolute_volume;
                        const double       repaired_volume      = repaired_measure.absolute_volume;
                        const double       reference_area       = double(PMP::area(fidelity_reference));
                        const double       repaired_area        = double(PMP::area(closed_candidate));
                        const double       volume_delta = std::abs(repaired_volume - reference_volume) / std::max(reference_volume, 1.e-9);
                        const double       area_delta   = std::abs(repaired_area - reference_area) / std::max(reference_area, 1.e-9);
                        const bool         bbox_preserved = std::abs(repaired_bbox.xmin() - reference_bbox.xmin()) <= hausdorff_limit &&
                                                    std::abs(repaired_bbox.ymin() - reference_bbox.ymin()) <= hausdorff_limit &&
                                                    std::abs(repaired_bbox.zmin() - reference_bbox.zmin()) <= hausdorff_limit &&
                                                    std::abs(repaired_bbox.xmax() - reference_bbox.xmax()) <= hausdorff_limit &&
                                                    std::abs(repaired_bbox.ymax() - reference_bbox.ymax()) <= hausdorff_limit &&
                                                    std::abs(repaired_bbox.zmax() - reference_bbox.zmax()) <= hausdorff_limit;
                        const double hausdorff_error_bound = std::max(1.e-5, hausdorff_limit * 0.01);
                        const double hausdorff_distance =
                            PMP::bounded_error_symmetric_Hausdorff_distance<CGAL::Sequential_tag>(fidelity_reference, closed_candidate,
                                                                                                  hausdorff_error_bound);
                        const bool fidelity_valid = reference_components == repaired_components && bbox_preserved &&
                                                    volume_delta <= 1.e-3 && area_delta <= 2.e-3 &&
                                                    hausdorff_distance + hausdorff_error_bound <= hausdorff_limit;

                        BOOST_LOG_TRIVIAL(info)
                            << "CGAL closed-defect repair: accepted=" << fidelity_valid << ", components=" << reference_components << "->"
                            << repaired_components << ", volume_delta=" << volume_delta << ", area_delta=" << area_delta
                            << ", hausdorff=" << hausdorff_distance << "/" << hausdorff_limit << ", faces=" << compacted.indices.size()
                            << "->" << closed_candidate.num_faces();
                        if (fidelity_valid)
                            cgal_mesh = std::move(closed_candidate);
                        else
                            direct_candidate_error = "closed-mesh repair exceeded the geometry-preservation limits";
                    }
                }
            }
        } catch (const std::bad_alloc&) {
            throw; // Resource exhaustion is not a recoverable geometric rejection.
        } catch (const std::exception& exception) {
            if (partial_result != nullptr)
                throw; // Partial checkpoints never survive an engine exception.
            direct_candidate_error = exception.what();
            BOOST_LOG_TRIVIAL(debug) << "CGAL closed-defect repair candidate rejected: " << exception.what();
        } catch (...) {
            if (partial_result != nullptr)
                throw;
            direct_candidate_error = "unknown CGAL error";
            BOOST_LOG_TRIVIAL(debug) << "CGAL closed-defect repair candidate rejected by an unknown error.";
        }

        // A closed Surface_mesh candidate is the least destructive repair
        // path. If its topology or fidelity gates reject the result, do not
        // bypass those gates by retrying through the more aggressive polygon-
        // soup cleanup pipeline.
        if (direct_closed_candidate && cgal_mesh.is_empty()) {
            std::string message =
                "Repair failed: closed mesh still self-intersects, failed topology validation, or exceeded geometry-preservation limits.";
            if (!direct_candidate_error.empty())
                message += " " + direct_candidate_error;
            return fail(message);
        }

        if (cgal_mesh.is_empty()) {
            stage = "soup_cleanup";
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
            its_compactify_vertices(cleaned, true);
            if (cleaned.vertices.empty() || cleaned.indices.empty())
                return fail("Repair failed: mesh cleanup removed all geometry.", true);

            std::vector<_EpicMesh::Point>         points;
            std::vector<std::vector<std::size_t>> polygons;

            points.reserve(cleaned.vertices.size());
            polygons.reserve(cleaned.indices.size());

            for (const stl_vertex& vertex : cleaned.vertices)
                points.emplace_back(vertex.x(), vertex.y(), vertex.z());

            for (const stl_triangle_vertex_indices& face : cleaned.indices)
                polygons.push_back({size_t(face[0]), size_t(face[1]), size_t(face[2])});

            PMP::repair_polygon_soup(points, polygons);
            if (points.empty() || polygons.empty())
                return fail("Repair failed: polygon soup cleanup removed all geometry.", true);

            stage = "soup_topology_preparation";
            if (repair_soup_topology(points, polygons, cgal_mesh)) {
                // The certificate preserves all exterior surface and removes
                // only strictly interior partitions. Later cleanup must keep
                // this complete solid, not compare it to redundant partitions.
                fallback_fidelity_reference = cgal_mesh;
            } else {
                // false means that CGAL duplicated singular vertices, not that the
                // modified soup cannot be converted. Such a candidate still needs
                // all topology, self-intersection and fidelity checks below.
                const size_t points_before_orientation = points.size();
                stage                                  = "soup_orientation";
                soup_vertices_split                    = !PMP::orient_polygon_soup(points, polygons);
                partial_checkpoints_allowed            = !soup_vertices_split;
                BOOST_LOG_TRIVIAL(info) << "CGAL repair: event=soup_oriented, split_vertices=" << points.size() - points_before_orientation
                                        << ", faces=" << polygons.size();
                if (!PMP::is_polygon_soup_a_polygon_mesh(polygons))
                    return fail(L("Repair failed: polygon soup is still invalid after orientation and vertex splitting."));

                PMP::polygon_soup_to_polygon_mesh(points, polygons, cgal_mesh);
                if (cgal_mesh.is_empty() || cgal_mesh.num_faces() != polygons.size() || !CGAL::is_valid_polygon_mesh(cgal_mesh) ||
                    !CGAL::is_triangle_mesh(cgal_mesh))
                    return fail(L("Repair failed: polygon soup is still invalid after orientation and vertex splitting."));
                if (input_had_no_open_edges) {
                    // For an input whose quick topology was already closed, soup
                    // cleanup may canonicalize duplicated/inconsistent faces but
                    // must not silently turn that surface into an open mesh and
                    // then replace a large region through hole filling. Save the
                    // canonical surface as a fidelity baseline for the final
                    // candidate.
                    if (cgal_mesh.is_empty() || !CGAL::is_valid_polygon_mesh(cgal_mesh) || !CGAL::is_triangle_mesh(cgal_mesh) ||
                        !CGAL::is_closed(cgal_mesh))
                        return fail("Repair failed: closed mesh became open or invalid during polygon-soup cleanup.");
                    fallback_fidelity_reference = cgal_mesh;
                }
                // A split candidate also needs a baseline when the original was
                // open. Its existing surface must survive; new hole patches need
                // not have the same area or volume as an incomplete input.
                if (soup_vertices_split)
                    fallback_fidelity_reference = cgal_mesh;
            }
        }

        stage = "mesh_cleanup";
        if (!PMP::remove_degenerate_faces(cgal_mesh))
            return fail("Repair failed: mesh still contains degenerate faces after cleanup.");
        PMP::remove_isolated_vertices(cgal_mesh);
        const size_t duplicated_non_manifold_vertices = PMP::duplicate_non_manifold_vertices(cgal_mesh);
        partial_checkpoints_allowed                   = partial_checkpoints_allowed && duplicated_non_manifold_vertices == 0;
        cgal_mesh.collect_garbage();
        BOOST_LOG_TRIVIAL(debug) << "CGAL repair cleanup: duplicated_non_manifold_vertices=" << duplicated_non_manifold_vertices
                                 << ", faces=" << cgal_mesh.num_faces() << ", closed=" << CGAL::is_closed(cgal_mesh);

        if (cgal_mesh.is_empty() || cgal_mesh.num_faces() == 0)
            return fail("Repair failed: mesh cleanup removed all geometry.", true);

        if (!CGAL::is_closed(cgal_mesh)) {
            stage = "border_stitching";
            const size_t stitched_borders = PMP::stitch_borders(cgal_mesh);
            cgal_mesh.collect_garbage();
            BOOST_LOG_TRIVIAL(debug) << "CGAL repair border stitching: stitched=" << stitched_borders << ", faces=" << cgal_mesh.num_faces()
                                     << ", closed=" << CGAL::is_closed(cgal_mesh);
            checkpoint(cgal_mesh, "border_stitching");
        }

        if (!CGAL::is_closed(cgal_mesh)) {
            // Run basic boundary cleanup only on ordinary manifold candidates.
            // Singular/split soups retain the existing conservative failure
            // path; do not send them through another heuristic repair engine.
            if (!soup_vertices_split && duplicated_non_manifold_vertices == 0) {
                stage = "basic_boundary_cleanup";
                prepare_open_surface(cgal_mesh);
                if (cgal_mesh.is_empty() || cgal_mesh.num_faces() == 0)
                    return fail(L("Repair failed: basic cleanup removed all geometry."), true);
                checkpoint(cgal_mesh, "basic_boundary_cleanup");
            }
        }

        if (!CGAL::is_closed(cgal_mesh)) {
            // Also protect the existing surface of ordinary open inputs, not
            // only vertex-split soups. Filling a hole may add area, not erase
            // unrelated geometry or change the number of connected shells.
            if (!fallback_fidelity_reference)
                fallback_fidelity_reference = cgal_mesh;
            stage = "hole_filling";
            using halfedge_descriptor = boost::graph_traits<_EpicMesh>::halfedge_descriptor;

            std::vector<halfedge_descriptor> borders;
            PMP::extract_boundary_cycles(cgal_mesh, std::back_inserter(borders));

            for (size_t i = 0; i < borders.size(); ++i) {
#if defined(_WIN32) && CGAL_VERSION_NR < 1060000000
                using face_descriptor   = boost::graph_traits<_EpicMesh>::face_descriptor;
                using vertex_descriptor = boost::graph_traits<_EpicMesh>::vertex_descriptor;
                std::vector<face_descriptor>   patch_faces;
                std::vector<vertex_descriptor> patch_vertices;
                PMP::triangulate_and_refine_hole(cgal_mesh, borders[i], std::back_inserter(patch_faces), std::back_inserter(patch_vertices));
#else
                PMP::triangulate_and_refine_hole(cgal_mesh, borders[i]);
#endif
                // Bound whole-surface inspections on meshes with many holes:
                // early checkpoints, then exponentially spaced and final ones.
                const size_t completed = i + 1;
                if (completed == borders.size() || (completed & (completed - 1)) == 0)
                    checkpoint(cgal_mesh, "hole_filling");
            }
        }

        stage = "topology_validation";
        if (cgal_mesh.is_empty() || cgal_mesh.num_faces() == 0)
            return fail("Repair failed: repaired mesh is empty.", true);
        if (!CGAL::is_valid_polygon_mesh(cgal_mesh))
            return fail("Repair failed: repaired mesh is not a valid polygon mesh.");
        if (!CGAL::is_triangle_mesh(cgal_mesh))
            return fail("Repair failed: repaired mesh contains non-triangular faces.");
        if (!CGAL::is_closed(cgal_mesh))
            return fail("Repair failed: mesh is still open after hole filling.");

        // Volume orientation assumes a self-intersection-free mesh.
        if (PMP::does_self_intersect(cgal_mesh)) {
            // Do not retry intersecting vertex-split soups with experimental
            // patch repair: this path has triggered native crashes, which are
            // not caught by C++ exception handlers. Reject before calling it.
            if (soup_vertices_split) {
                stage = "split_soup_self_intersections_unsupported";
                return fail(L("Repair failed: this model has complex intersecting surfaces that cannot be safely repaired automatically."));
            }
            return fail("Repair failed: repaired mesh still self-intersects.");
        }

        {
            using face_descriptor     = boost::graph_traits<_EpicMesh>::face_descriptor;
            using halfedge_descriptor = boost::graph_traits<_EpicMesh>::halfedge_descriptor;
            std::vector<face_descriptor>     degenerate_faces;
            std::vector<halfedge_descriptor> non_manifold_vertices;
            PMP::degenerate_faces(cgal_mesh, std::back_inserter(degenerate_faces));
            PMP::non_manifold_vertices(cgal_mesh, std::back_inserter(non_manifold_vertices));
            if (!degenerate_faces.empty() || !non_manifold_vertices.empty())
                return fail(L("Repair failed: repaired mesh still contains degenerate faces or non-manifold vertices."));
        }

        // does_bound_a_volume() accepts inward level-0 shells as the boundary
        // of an unbounded volume. Normalize nesting-aware component orientation
        // before validating the final result.
        PMP::orient_to_bound_a_volume(cgal_mesh);
        if (!PMP::does_bound_a_volume(cgal_mesh))
            return fail("Repair failed: mesh orientation does not bound a volume.");

        if (fallback_fidelity_reference) {
            stage                               = "fidelity_validation";
            _EpicMesh&         reference        = *fallback_fidelity_reference;
            const bool         reference_closed = CGAL::is_closed(reference);
            const CGAL::Bbox_3 reference_bbox   = PMP::bbox(reference);
            const CGAL::Bbox_3 repaired_bbox    = PMP::bbox(cgal_mesh);
            const double       bbox_dx          = reference_bbox.xmax() - reference_bbox.xmin();
            const double       bbox_dy          = reference_bbox.ymax() - reference_bbox.ymin();
            const double       bbox_dz          = reference_bbox.zmax() - reference_bbox.zmin();
            const double       bbox_diagonal    = std::sqrt(bbox_dx * bbox_dx + bbox_dy * bbox_dy + bbox_dz * bbox_dz);
            const double       hausdorff_limit  = std::clamp(bbox_diagonal * 1.e-4, 0.005, 0.05);

            const ConnectedComponentVolumeMeasure reference_measure =
                measure_connected_component_volumes(reference, "f:model_repair_fallback_reference_component");
            const ConnectedComponentVolumeMeasure repaired_measure =
                measure_connected_component_volumes(cgal_mesh, "f:model_repair_fallback_repaired_component");
            const size_t reference_components = reference_measure.component_count;
            const size_t repaired_components  = repaired_measure.component_count;
            const double reference_volume     = reference_measure.absolute_volume;
            const double repaired_volume      = repaired_measure.absolute_volume;
            const double reference_area       = double(PMP::area(reference));
            const double repaired_area        = double(PMP::area(cgal_mesh));
            const double volume_delta         = std::abs(repaired_volume - reference_volume) / std::max(reference_volume, 1.e-9);
            const double area_delta           = std::abs(repaired_area - reference_area) / std::max(reference_area, 1.e-9);
            const bool   bbox_preserved       = std::abs(repaired_bbox.xmin() - reference_bbox.xmin()) <= hausdorff_limit &&
                                        std::abs(repaired_bbox.ymin() - reference_bbox.ymin()) <= hausdorff_limit &&
                                        std::abs(repaired_bbox.zmin() - reference_bbox.zmin()) <= hausdorff_limit &&
                                        std::abs(repaired_bbox.xmax() - reference_bbox.xmax()) <= hausdorff_limit &&
                                        std::abs(repaired_bbox.ymax() - reference_bbox.ymax()) <= hausdorff_limit &&
                                        std::abs(repaired_bbox.zmax() - reference_bbox.zmax()) <= hausdorff_limit;
            const double hausdorff_error_bound = std::max(1.e-5, hausdorff_limit * 0.01);
            const double hausdorff_distance =
                reference_closed ?
                    PMP::bounded_error_symmetric_Hausdorff_distance<CGAL::Sequential_tag>(reference, cgal_mesh, hausdorff_error_bound) :
                    PMP::bounded_error_Hausdorff_distance<CGAL::Sequential_tag>(reference, cgal_mesh, hausdorff_error_bound);
            const bool size_preserved = reference_closed ? volume_delta <= 1.e-3 && area_delta <= 2.e-3 :
                                                           repaired_area >= reference_area * (1. - 2.e-3);
            const bool fidelity_valid = reference_components == repaired_components && bbox_preserved && size_preserved &&
                                        hausdorff_distance + hausdorff_error_bound <= hausdorff_limit;

            BOOST_LOG_TRIVIAL(info) << "CGAL polygon-soup repair: event=fidelity_checked, accepted=" << fidelity_valid
                                    << ", split_soup=" << soup_vertices_split << ", reference_closed=" << reference_closed
                                    << ", components=" << reference_components << "->" << repaired_components
                                    << ", volume_delta=" << volume_delta << ", area_delta=" << area_delta
                                    << ", hausdorff=" << hausdorff_distance << "/" << hausdorff_limit;
            if (!fidelity_valid) {
                if (soup_vertices_split)
                    return fail(L("Repair failed: vertex-split repair exceeded the geometry-preservation limits."));
                return fail(L("Repair failed: polygon-soup cleanup exceeded the geometry-preservation limits."));
            }
        }

        // Surface_mesh descriptors may contain gaps after removals. Compact them
        // before converting because cgal_to_indexed_triangle_set uses descriptor
        // indices directly.
        stage = "output_validation";
        cgal_mesh.collect_garbage();

        if (cgal_mesh.is_empty() || cgal_mesh.num_faces() == 0 || !CGAL::is_valid_polygon_mesh(cgal_mesh) ||
            !CGAL::is_triangle_mesh(cgal_mesh) || !CGAL::is_closed(cgal_mesh))
            return fail("Repair failed: compacted mesh is invalid, non-triangular, empty, or open.");

        for (const _EpicMesh::Vertex_index vertex : cgal_mesh.vertices()) {
            const _EpicMesh::Point& point = cgal_mesh.point(vertex);
            if (!std::isfinite(double(point.x())) || !std::isfinite(double(point.y())) || !std::isfinite(double(point.z())))
                return fail("Repair failed: repaired mesh contains non-finite vertices.", true);
        }

        indexed_triangle_set repaired = cgal_to_indexed_triangle_set(cgal_mesh);
        if (repaired.vertices.empty() || repaired.indices.empty())
            return fail("Repair failed: repaired mesh is empty after conversion.", true);
        for (const stl_vertex& vertex : repaired.vertices)
            if (!vertex.allFinite())
                return fail("Repair failed: converted mesh contains non-finite vertices.", true);
        if (its_num_open_edges(repaired) != 0)
            return fail("Repair failed: converted mesh is not closed.");

        BOOST_LOG_TRIVIAL(info) << "CGAL repair: event=candidate_validated, split_soup=" << soup_vertices_split
                                << ", faces=" << mesh.its.indices.size() << "->" << repaired.indices.size();

        // CGAL does not expose action counters for this pipeline. Keep the
        // legacy counters at zero; callers should compare geometry before/after.
        RepairedMeshErrors repairs;
        mesh = TriangleMesh(std::move(repaired), repairs);
        if (repaired_errors != nullptr)
            *repaired_errors = repairs;
        return true;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& exception) {
        if (partial_result != nullptr)
            throw;
        return fail(exception.what());
    } catch (...) {
        if (partial_result != nullptr)
            throw;
        return fail("Repair failed with an unknown CGAL error.");
    }
}

bool repair(TriangleMesh& mesh, RepairedMeshErrors* repaired_errors, std::string* error)
{
    return repair_impl(mesh, repaired_errors, error, nullptr);
}

MeshRepairOutcome repair_with_partial(TriangleMesh& mesh, std::string* error)
{
    TriangleMesh checkpoint;
    if (repair_impl(mesh, nullptr, error, &checkpoint))
        return MeshRepairOutcome::Repaired;
    if (checkpoint.empty())
        return MeshRepairOutcome::Rejected;
    mesh = std::move(checkpoint);
    // Keep the failed final-stage diagnostic; callers mark this as unresolved,
    // not as a completely repaired mesh. No additional CLI switch is needed.
    return MeshRepairOutcome::PartiallyRepaired;
}

CGALMeshPtr clone(const CGALMesh& m) { return CGALMeshPtr{new CGALMesh{m}}; }

} // namespace cgal

namespace mcut {
/* BBS: MusangKing
 * mcut mesh array format for Boolean Opts calculation
 */
struct McutMesh
{
    // variables for mesh data in a format suited for mcut
    std::vector<uint32_t> faceSizesArray;
    std::vector<uint32_t> faceIndicesArray;
    std::vector<double>   vertexCoordsArray;
};
void McutMeshDeleter::operator()(McutMesh* ptr) { delete ptr; }

bool empty(const McutMesh& mesh) { return mesh.vertexCoordsArray.empty() || mesh.faceIndicesArray.empty(); }
void triangle_mesh_to_mcut(const TriangleMesh& src_mesh, McutMesh& srcMesh, const Transform3d& src_nm = Transform3d::Identity())
{
    // vertices precision convention and copy
    srcMesh.vertexCoordsArray.reserve(src_mesh.its.vertices.size() * 3);
    for (int i = 0; i < src_mesh.its.vertices.size(); ++i) {
        const Vec3d v = src_nm * src_mesh.its.vertices[i].cast<double>();
        srcMesh.vertexCoordsArray.push_back(v[0]);
        srcMesh.vertexCoordsArray.push_back(v[1]);
        srcMesh.vertexCoordsArray.push_back(v[2]);
    }

    // faces copy
    srcMesh.faceIndicesArray.reserve(src_mesh.its.indices.size() * 3);
    srcMesh.faceSizesArray.reserve(src_mesh.its.indices.size());
    for (int i = 0; i < src_mesh.its.indices.size(); ++i) {
        const int& f0 = src_mesh.its.indices[i][0];
        const int& f1 = src_mesh.its.indices[i][1];
        const int& f2 = src_mesh.its.indices[i][2];
        srcMesh.faceIndicesArray.push_back(f0);
        srcMesh.faceIndicesArray.push_back(f1);
        srcMesh.faceIndicesArray.push_back(f2);

        srcMesh.faceSizesArray.push_back((uint32_t) 3);
    }
}

McutMeshPtr triangle_mesh_to_mcut(const indexed_triangle_set& M)
{
    std::unique_ptr<McutMesh, McutMeshDeleter> out(new McutMesh{});
    TriangleMesh                               trimesh(M);
    triangle_mesh_to_mcut(trimesh, *out.get());
    return out;
}

TriangleMesh mcut_to_triangle_mesh(const McutMesh& mcutmesh)
{
    uint32_t ccVertexCount = mcutmesh.vertexCoordsArray.size() / 3;
    auto&    ccVertices    = mcutmesh.vertexCoordsArray;
    auto&    ccFaceIndices = mcutmesh.faceIndicesArray;
    auto&    faceSizes     = mcutmesh.faceSizesArray;
    uint32_t ccFaceCount   = faceSizes.size();
    // rearrange vertices/faces and save into result mesh
    std::vector<Vec3f> vertices(ccVertexCount);
    for (uint32_t i = 0; i < ccVertexCount; i++) {
        vertices[i][0] = (float) ccVertices[(uint64_t) i * 3 + 0];
        vertices[i][1] = (float) ccVertices[(uint64_t) i * 3 + 1];
        vertices[i][2] = (float) ccVertices[(uint64_t) i * 3 + 2];
    }

    // output faces
    int faceVertexOffsetBase = 0;

    // for each face in CC
    std::vector<Vec3i32> faces(ccFaceCount);
    for (uint32_t f = 0; f < ccFaceCount; ++f) {
        int faceSize = faceSizes.at(f);

        // for each vertex in face
        for (int v = 0; v < faceSize; v++) {
            faces[f][v] = ccFaceIndices[(uint64_t) faceVertexOffsetBase + v];
        }
        faceVertexOffsetBase += faceSize;
    }

    TriangleMesh out(vertices, faces);
    return out;
}

void merge_mcut_meshes(McutMesh& src, const McutMesh& cut)
{
    indexed_triangle_set all_its;
    TriangleMesh         tri_src = mcut_to_triangle_mesh(src);
    TriangleMesh         tri_cut = mcut_to_triangle_mesh(cut);
    its_merge(all_its, tri_src.its);
    its_merge(all_its, tri_cut.its);
    src = *triangle_mesh_to_mcut(all_its);
}

MCAPI_ATTR void MCAPI_CALL mcDebugOutput(McDebugSource   source,
                                         McDebugType     type,
                                         unsigned int    id,
                                         McDebugSeverity severity,
                                         size_t          length,
                                         const char*     message,
                                         const void*     userParam)
{
    BOOST_LOG_TRIVIAL(debug) << Slic3r::format("mcut mcDebugOutput message ( %d ): %s ", id, message);

    switch (source) {
    case MC_DEBUG_SOURCE_API: BOOST_LOG_TRIVIAL(debug) << ("Source: API"); break;
    case MC_DEBUG_SOURCE_KERNEL: BOOST_LOG_TRIVIAL(debug) << ("Source: Kernel"); break;
    }

    switch (type) {
    case MC_DEBUG_TYPE_ERROR: BOOST_LOG_TRIVIAL(debug) << ("Type: Error"); break;
    case MC_DEBUG_TYPE_DEPRECATED_BEHAVIOR: BOOST_LOG_TRIVIAL(debug) << ("Type: Deprecated Behaviour"); break;
    case MC_DEBUG_TYPE_OTHER: BOOST_LOG_TRIVIAL(debug) << ("Type: Other"); break;
    }

    switch (severity) {
    case MC_DEBUG_SEVERITY_HIGH: BOOST_LOG_TRIVIAL(debug) << ("Severity: high"); break;
    case MC_DEBUG_SEVERITY_MEDIUM: BOOST_LOG_TRIVIAL(debug) << ("Severity: medium"); break;
    case MC_DEBUG_SEVERITY_LOW: BOOST_LOG_TRIVIAL(debug) << ("Severity: low"); break;
    case MC_DEBUG_SEVERITY_NOTIFICATION: BOOST_LOG_TRIVIAL(debug) << ("Severity: notification"); break;
    }
}

bool do_boolean_single(McutMesh& srcMesh, const McutMesh& cutMesh, const std::string& boolean_opts)
{
    // create context
    McContext context = MC_NULL_HANDLE;
    McResult  err     = mcCreateContext(&context, 0);
    // add debug callback according to https://cutdigital.github.io/mcut.site/tutorials/debugging/
    mcDebugMessageCallback(context, mcDebugOutput, nullptr);
    mcDebugMessageControl(context, MC_DEBUG_SOURCE_ALL, MC_DEBUG_TYPE_ERROR, MC_DEBUG_SEVERITY_MEDIUM, true);
    // We can either let MCUT compute all possible meshes (including patches etc.), or we can
    // constrain the library to compute exactly the boolean op mesh we want. This 'constrained' case
    // is done with the following flags.
    // NOTE#1: you can extend these flags by bitwise ORing with additional flags (see `McDispatchFlags' in mcut.h)
    // NOTE#2: below order of columns MATTERS
    const std::map<std::string, McFlags> booleanOpts = {
        {"A_NOT_B", MC_DISPATCH_FILTER_FRAGMENT_SEALING_INSIDE | MC_DISPATCH_FILTER_FRAGMENT_LOCATION_ABOVE},
        {"B_NOT_A", MC_DISPATCH_FILTER_FRAGMENT_SEALING_OUTSIDE | MC_DISPATCH_FILTER_FRAGMENT_LOCATION_BELOW},
        {"UNION", MC_DISPATCH_FILTER_FRAGMENT_SEALING_OUTSIDE | MC_DISPATCH_FILTER_FRAGMENT_LOCATION_ABOVE},
        {"INTERSECTION", MC_DISPATCH_FILTER_FRAGMENT_SEALING_INSIDE | MC_DISPATCH_FILTER_FRAGMENT_LOCATION_BELOW},
    };

    std::map<std::string, McFlags>::const_iterator it          = booleanOpts.find(boolean_opts);
    McFlags                                        boolOpFlags = it->second;

    if (srcMesh.vertexCoordsArray.empty() && (boolean_opts == "UNION" || boolean_opts == "B_NOT_A")) {
        srcMesh = cutMesh;
        mcReleaseContext(context);
        return true;
    }

    err = mcDispatch(context,
                     MC_DISPATCH_VERTEX_ARRAY_DOUBLE |          // vertices are in array of doubles
                         MC_DISPATCH_ENFORCE_GENERAL_POSITION | // perturb if necessary
                         boolOpFlags,                           // filter flags which specify the type of output we want
                     // source mesh
                     reinterpret_cast<const void*>(srcMesh.vertexCoordsArray.data()),
                     reinterpret_cast<const uint32_t*>(srcMesh.faceIndicesArray.data()), srcMesh.faceSizesArray.data(),
                     static_cast<uint32_t>(srcMesh.vertexCoordsArray.size() / 3), static_cast<uint32_t>(srcMesh.faceSizesArray.size()),
                     // cut mesh
                     reinterpret_cast<const void*>(cutMesh.vertexCoordsArray.data()), cutMesh.faceIndicesArray.data(),
                     cutMesh.faceSizesArray.data(), static_cast<uint32_t>(cutMesh.vertexCoordsArray.size() / 3),
                     static_cast<uint32_t>(cutMesh.faceSizesArray.size()));
    if (err != MC_NO_ERROR) {
        BOOST_LOG_TRIVIAL(debug) << "MCUT mcDispatch fails! err=" << err;
        mcReleaseContext(context);
        if (boolean_opts == "UNION") {
            merge_mcut_meshes(srcMesh, cutMesh);
            return true;
        }
        return false;
    }

    // query the number of available connected component
    uint32_t numConnComps;
    err = mcGetConnectedComponents(context, MC_CONNECTED_COMPONENT_TYPE_FRAGMENT, 0, NULL, &numConnComps);
    if (err != MC_NO_ERROR || numConnComps == 0) {
        BOOST_LOG_TRIVIAL(debug) << "MCUT mcGetConnectedComponents fails! err=" << err << ", numConnComps" << numConnComps;
        mcReleaseContext(context);
        if (numConnComps == 0 && boolean_opts == "UNION") {
            merge_mcut_meshes(srcMesh, cutMesh);
            return true;
        }
        return false;
    }

    std::vector<McConnectedComponent> connectedComponents(numConnComps, MC_NULL_HANDLE);
    err = mcGetConnectedComponents(context, MC_CONNECTED_COMPONENT_TYPE_FRAGMENT, (uint32_t) connectedComponents.size(),
                                   connectedComponents.data(), NULL);

    McutMesh outMesh;
    int      N_vertices = 0;
    // traversal of all connected components
    for (int n = 0; n < numConnComps; ++n) {
        // query the data of each connected component from MCUT
        McConnectedComponent connComp = connectedComponents[n];

        // query the vertices
        McSize numBytes = 0;
        err             = mcGetConnectedComponentData(context, connComp, MC_CONNECTED_COMPONENT_DATA_VERTEX_DOUBLE, 0, NULL, &numBytes);
        uint32_t            ccVertexCount = (uint32_t) (numBytes / (sizeof(double) * 3));
        std::vector<double> ccVertices((uint64_t) ccVertexCount * 3u, 0);
        err = mcGetConnectedComponentData(context, connComp, MC_CONNECTED_COMPONENT_DATA_VERTEX_DOUBLE, numBytes, (void*) ccVertices.data(),
                                          NULL);

        // query the faces
        numBytes = 0;
        err      = mcGetConnectedComponentData(context, connComp, MC_CONNECTED_COMPONENT_DATA_FACE_TRIANGULATION, 0, NULL, &numBytes);
        std::vector<uint32_t> ccFaceIndices(numBytes / sizeof(uint32_t), 0);
        err = mcGetConnectedComponentData(context, connComp, MC_CONNECTED_COMPONENT_DATA_FACE_TRIANGULATION, numBytes, ccFaceIndices.data(),
                                          NULL);
        std::vector<uint32_t> faceSizes(ccFaceIndices.size() / 3, 3);

        const uint32_t ccFaceCount = static_cast<uint32_t>(faceSizes.size());

        // Here we show, how to know when connected components, pertain particular boolean operations.
        McPatchLocation patchLocation = (McPatchLocation) 0;
        err = mcGetConnectedComponentData(context, connComp, MC_CONNECTED_COMPONENT_DATA_PATCH_LOCATION, sizeof(McPatchLocation),
                                          &patchLocation, NULL);

        McFragmentLocation fragmentLocation = (McFragmentLocation) 0;
        err = mcGetConnectedComponentData(context, connComp, MC_CONNECTED_COMPONENT_DATA_FRAGMENT_LOCATION, sizeof(McFragmentLocation),
                                          &fragmentLocation, NULL);

        outMesh.vertexCoordsArray.insert(outMesh.vertexCoordsArray.end(), ccVertices.begin(), ccVertices.end());

        // add offset to face index
        for (size_t i = 0; i < ccFaceIndices.size(); i++) {
            ccFaceIndices[i] += N_vertices;
        }

        int faceVertexOffsetBase = 0;

        // for each face in CC
        std::vector<Vec3i32> faces(ccFaceCount);
        for (uint32_t f = 0; f < ccFaceCount; ++f) {
            bool reverseWindingOrder = (fragmentLocation == MC_FRAGMENT_LOCATION_BELOW) && (patchLocation == MC_PATCH_LOCATION_OUTSIDE);
            int  faceSize            = faceSizes.at(f);
            if (reverseWindingOrder) {
                std::vector<uint32_t> faceIndex(faceSize);
                // for each vertex in face
                for (int v = faceSize - 1; v >= 0; v--) {
                    faceIndex[v] = ccFaceIndices[(uint64_t) faceVertexOffsetBase + v];
                }
                std::copy(faceIndex.begin(), faceIndex.end(), ccFaceIndices.begin() + faceVertexOffsetBase);
            }
            faceVertexOffsetBase += faceSize;
        }

        outMesh.faceIndicesArray.insert(outMesh.faceIndicesArray.end(), ccFaceIndices.begin(), ccFaceIndices.end());
        outMesh.faceSizesArray.insert(outMesh.faceSizesArray.end(), faceSizes.begin(), faceSizes.end());

        N_vertices += ccVertexCount;
    }

    // free connected component data
    err = mcReleaseConnectedComponents(context, 0, NULL);
    // destroy context
    err = mcReleaseContext(context);

    srcMesh = outMesh;

    return true;
}

void do_boolean(McutMesh& srcMesh, const McutMesh& cutMesh, const std::string& boolean_opts)
{
    TriangleMesh                      tri_src   = mcut_to_triangle_mesh(srcMesh);
    std::vector<indexed_triangle_set> src_parts = its_split(tri_src.its);

    TriangleMesh                      tri_cut   = mcut_to_triangle_mesh(cutMesh);
    std::vector<indexed_triangle_set> cut_parts = its_split(tri_cut.its);

    if (src_parts.empty() && boolean_opts == "UNION") {
        srcMesh = cutMesh;
        return;
    }
    if (cut_parts.empty())
        return;

    // when src mesh has multiple connected components, mcut refuses to work.
    // But we can force it to work by spliting the src mesh into disconnected components,
    // and do booleans seperately, then merge all the results.
    indexed_triangle_set all_its;
    if (boolean_opts == "UNION" || boolean_opts == "A_NOT_B") {
        for (size_t i = 0; i < src_parts.size(); i++) {
            auto src_part = triangle_mesh_to_mcut(src_parts[i]);
            for (size_t j = 0; j < cut_parts.size(); j++) {
                auto cut_part = triangle_mesh_to_mcut(cut_parts[j]);
                do_boolean_single(*src_part, *cut_part, boolean_opts);
            }
            TriangleMesh tri_part = mcut_to_triangle_mesh(*src_part);
            its_merge(all_its, tri_part.its);
        }
    } else if (boolean_opts == "INTERSECTION") {
        for (size_t i = 0; i < src_parts.size(); i++) {
            for (size_t j = 0; j < cut_parts.size(); j++) {
                auto src_part = triangle_mesh_to_mcut(src_parts[i]);
                auto cut_part = triangle_mesh_to_mcut(cut_parts[j]);
                bool success  = do_boolean_single(*src_part, *cut_part, boolean_opts);
                if (success) {
                    TriangleMesh tri_part = mcut_to_triangle_mesh(*src_part);
                    its_merge(all_its, tri_part.its);
                }
            }
        }
    }
    srcMesh = *triangle_mesh_to_mcut(all_its);
}

void make_boolean(const TriangleMesh&        src_mesh,
                  const TriangleMesh&        cut_mesh,
                  std::vector<TriangleMesh>& dst_mesh,
                  const std::string&         boolean_opts)
{
    McutMesh srcMesh, cutMesh;
    triangle_mesh_to_mcut(src_mesh, srcMesh);
    triangle_mesh_to_mcut(cut_mesh, cutMesh);
    // dst_mesh = make_boolean(srcMesh, cutMesh, boolean_opts);
    do_boolean(srcMesh, cutMesh, boolean_opts);
    TriangleMesh tri_src = mcut_to_triangle_mesh(srcMesh);
    if (!tri_src.empty())
        dst_mesh.push_back(std::move(tri_src));
}

} // namespace mcut

}} // namespace Slic3r::MeshBoolean
