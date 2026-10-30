#include <catch2/catch.hpp>
#include <test_utils.hpp>

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/MeshBoolean.hpp>
#include <libslic3r/MeshRepair.hpp>
#include <libslic3r/ModelInspection.hpp>
#include <libslic3r/Utils.hpp>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/core.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/make_shared.hpp>

namespace {
class RepairLogCapture
{
    class Backend : public boost::log::sinks::basic_sink_backend<boost::log::sinks::synchronized_feeding>
    {
    public:
        std::vector<std::string> messages;
        void                     consume(const boost::log::record_view& record)
        {
            const auto message = record.attribute_values()["Message"].extract<std::string>();
            if (message)
                messages.push_back(message.get());
        }
    };
    using Sink                                = boost::log::sinks::synchronous_sink<Backend>;
    const unsigned             previous_level = Slic3r::get_logging_level();
    boost::shared_ptr<Backend> backend        = boost::make_shared<Backend>();
    boost::shared_ptr<Sink>    sink           = boost::make_shared<Sink>(backend);

public:
    RepairLogCapture()
    {
        boost::log::core::get()->add_sink(sink);
        Slic3r::set_logging_level(3);
    }
    ~RepairLogCapture()
    {
        boost::log::core::get()->remove_sink(sink);
        Slic3r::set_logging_level(previous_level);
    }
    const std::vector<std::string>& messages() const { return backend->messages; }
    bool                            contains(const std::string& text) const
    {
        return std::any_of(messages().begin(), messages().end(),
                           [&](const std::string& message) { return message.find(text) != std::string::npos; });
    }
};
} // namespace

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

TEST_CASE("App basic repair handles measured local defects in one pass", "[MeshBoolean][ModelRepair][AppBasic]")
{
    const char*      fixture = GENERATE("01-missing-top", "03-wrong-winding", "04-duplicate-face", "05-degenerate-face", "07-thin-solid");
    auto             mesh    = load_model(std::string("model_repair_acceptance/") + fixture + ".obj");
    std::string      error;
    RepairLogCapture logs;
    REQUIRE(Slic3r::repair_mesh(mesh, nullptr, &error));
    INFO(fixture);
    INFO(error);
    CHECK(inspect_model_mesh(mesh)["mesh_status"] == "passed");
    CHECK_FALSE(logs.contains("surface_distance"));
    CHECK_FALSE(logs.contains("checkpoint"));
    CHECK_FALSE(logs.contains("fidelity_checked"));
}

TEST_CASE("App basic rejected candidates leave original geometry intact", "[MeshBoolean][ModelRepair][AppBasic]")
{
    auto mesh = make_cube(10., 10., 10.);
    SECTION("only a zero area face") { mesh.its.indices = {{0, 0, 1}}; }
    SECTION("invalid index") { mesh.its.indices[0][0] = -1; }
    const auto  original = mesh.its;
    std::string error;
    CHECK_FALSE(Slic3r::repair_mesh(mesh, nullptr, &error));
    CHECK_FALSE(error.empty());
    CHECK(mesh.its.vertices == original.vertices);
    CHECK(mesh.its.indices == original.indices);
}

TEST_CASE("2.5 preserves separately indexed coincident closed shells as residual defects", "[MeshBoolean][ModelRepair][AppBasic]")
{
    auto mesh = make_cube(10., 10., 10.);
    auto copy = mesh;
    if (GENERATE(false, true))
        copy.flip_triangles();
    mesh.merge(copy);
    std::string error;
    REQUIRE(Slic3r::repair_mesh(mesh, nullptr, &error));
    CHECK(mesh.its.indices.size() == 24);
    CHECK(mesh.volume() == Approx(2000.));
    CHECK(inspect_model_mesh(mesh)["mesh_status"] == "needs_repair");
}
