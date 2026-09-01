#include <catch2/catch.hpp>
#include <test_utils.hpp>

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/MeshBoolean.hpp>

using namespace Slic3r;

TEST_CASE("CGAL and TriangleMesh conversions", "[MeshBoolean]") {
    TriangleMesh sphere = make_sphere(1.);
    
    auto cgalmesh_ptr = MeshBoolean::cgal::triangle_mesh_to_cgal(sphere);
    
    REQUIRE(cgalmesh_ptr);
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(*cgalmesh_ptr));
    
    TriangleMesh M = MeshBoolean::cgal::cgal_to_triangle_mesh(*cgalmesh_ptr);
    
    REQUIRE(M.its.vertices.size() == sphere.its.vertices.size());
    REQUIRE(M.its.indices.size() == sphere.its.indices.size());
    
    REQUIRE(M.volume() == Approx(sphere.volume()));
    
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(M));
}

TEST_CASE("CGAL repair closes a missing cube side", "[MeshBoolean][ModelRepair]")
{
    TriangleMesh mesh = make_cube(10., 10., 10.);
    mesh.its.indices.erase(mesh.its.indices.begin(), mesh.its.indices.begin() + 2);
    const size_t open_edges_before = its_num_open_edges(mesh.its);
    REQUIRE(open_edges_before > 0);

    std::string error;
    const bool  repaired = MeshBoolean::cgal::repair(mesh, nullptr, &error);
    INFO(error);
    REQUIRE(repaired);

    CHECK(error.empty());
    CHECK_FALSE(mesh.empty());
    CHECK(its_num_open_edges(mesh.its) == 0);
    CHECK(mesh.volume() > 0.);
}
