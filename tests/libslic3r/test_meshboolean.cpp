#include <catch2/catch.hpp>
#include <test_utils.hpp>

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/MeshBoolean.hpp>
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

TEST_CASE("CGAL repair diagnostics cover accepted rejected and exceptional exits", "[MeshBoolean][ModelRepair][Diagnostics]")
{
    RepairLogCapture logs;
    std::string      error;
    SECTION("accepted candidate")
    {
        auto mesh = make_cube(10., 10., 10.);
        mesh.its.indices.erase(mesh.its.indices.begin(), mesh.its.indices.begin() + 2);
        REQUIRE(MeshBoolean::cgal::repair(mesh, nullptr, &error));
        CHECK(logs.contains("reason=no_topology_change"));
        CHECK(logs.contains("outcome=candidate_accepted"));
        CHECK(logs.contains("distance_checked=true"));
        CHECK(its_num_open_edges(mesh.its) == 0);
    }
    SECTION("rejected candidate")
    {
        TriangleMesh empty;
        CHECK_FALSE(MeshBoolean::cgal::repair(empty, nullptr, &error));
        CHECK(logs.contains("outcome=candidate_rejected"));
        CHECK_FALSE(error.empty());
    }
    SECTION("exception does not hide timing or change partial API semantics")
    {
        TriangleMesh empty;
        CHECK_THROWS(MeshBoolean::cgal::repair_with_partial(empty, &error));
        CHECK(logs.contains("outcome=exception"));
        CHECK(empty.empty());
    }
    size_t      summaries = 0, stages = 0;
    std::string repair_id;
    for (const auto& message : logs.messages()) {
        if (message.find("event=stage_finished") == std::string::npos && message.find("event=timing_summary") == std::string::npos)
            continue;
        const auto id_begin = message.find("repair_id=");
        REQUIRE(id_begin != std::string::npos);
        const auto id = message.substr(id_begin, message.find(',', id_begin) - id_begin);
        if (repair_id.empty())
            repair_id = id;
        CHECK(id == repair_id);
        const auto elapsed = message.find("elapsed_ms=");
        REQUIRE(elapsed != std::string::npos);
        CHECK(std::stod(message.substr(elapsed + 11)) >= 0.);
        if (message.find("event=timing_summary") != std::string::npos)
            ++summaries;
        else
            ++stages;
    }
    CHECK(summaries == 1);
    CHECK(stages >= 1);
}

TEST_CASE("Soup topology diagnostics distinguish query budget from face limit", "[MeshBoolean][ModelRepair][Diagnostics]")
{
    const int face_count = GENERATE(700, 20001);
    CAPTURE(face_count);
    indexed_triangle_set its;
    for (int i = 0; i < face_count; ++i) {
        const float x = 3.f * i;
        its.vertices.emplace_back(x, 0.f, 0.f);
        its.vertices.emplace_back(x + 1.f, 0.f, 0.f);
        its.vertices.emplace_back(x, 1.f, 0.f);
        its.indices.emplace_back(3 * i, 3 * i + 1, 3 * i + 2);
    }
    TriangleMesh     mesh(its);
    RepairLogCapture logs;
    std::string      error;
    CHECK_FALSE(MeshBoolean::cgal::repair(mesh, nullptr, &error));
    CHECK(logs.contains("event=soup_topology_result"));
    CHECK(logs.contains(face_count == 700 ? "reason=boundary_query_budget" : "reason=face_limit"));
    CHECK(logs.contains("stage=soup_orientation")); // Skips this branch, not the entire repair.
    CHECK(logs.contains("outcome=candidate_rejected"));
    CHECK(mesh.its.vertices == its.vertices);
    CHECK(mesh.its.indices == its.indices);
}

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

TEST_CASE("Partial candidate validation requires located progress without geometry loss", "[MeshBoolean][ModelRepair][Partial]")
{
    TriangleMesh source = make_cube(10., 10., 10.);
    source.its.indices.erase(source.its.indices.begin(), source.its.indices.begin() + 4);
    TriangleMesh candidate = source;
    candidate.its.indices.emplace_back(0, 1, 2);
    candidate.its.indices.emplace_back(0, 2, 3);
    const auto original = source.its;
    REQUIRE(its_num_open_edges(source.its) == 8);
    REQUIRE(its_num_open_edges(candidate.its) == 4);
    REQUIRE(MeshBoolean::cgal::is_safe_partial_repair(source, candidate));
    CHECK_FALSE(MeshBoolean::cgal::is_safe_partial_repair(source, source));
    CHECK_FALSE(MeshBoolean::cgal::is_safe_partial_repair(candidate, source));

    SECTION("new boundary elsewhere") { candidate.its.indices.erase(candidate.its.indices.begin()); }
    SECTION("displacement") { candidate.translate(1.f, 0.f, 0.f); }
    SECTION("reversed winding") { candidate.flip_triangles(); }
    SECTION("non-finite output") { candidate.its.vertices.front().x() = std::numeric_limits<float>::infinity(); }
    SECTION("invalid index") { candidate.its.indices.front()[0] = int(candidate.its.vertices.size()); }
    SECTION("new degenerate face") { candidate.its.indices.emplace_back(0, 0, 1); }
    SECTION("new duplicate face") { candidate.its.indices.push_back(candidate.its.indices.front()); }
    SECTION("lost shell") { candidate = TriangleMesh{}; }
    CHECK_FALSE(MeshBoolean::cgal::is_safe_partial_repair(source, candidate));
    CHECK(source.its.vertices == original.vertices);
    CHECK(source.its.indices == original.indices);
}

TEST_CASE("Partial self-intersection progress retains only unchanged residual pairs", "[MeshBoolean][ModelRepair][Partial]")
{
    // Two independent intersecting pairs, all in one mesh. Separating the first
    // pair by 0.002 model units is inside the unchanged fidelity limits. The
    // remaining intersection region must be identical, not merely fewer pairs.
    const auto mesh_at = [](float first_shift, float second_shift) {
        TriangleMesh mesh = make_cube(10., 10., 10.);
        TriangleMesh a    = make_cube(10., 10., 10.);
        a.translate(first_shift, 0.f, 0.f);
        mesh.merge(a);
        TriangleMesh b = make_cube(10., 10., 10.);
        b.translate(0.f, 30.f, 0.f);
        mesh.merge(b);
        TriangleMesh c = make_cube(10., 10., 10.);
        c.translate(second_shift, 35.f, 5.f);
        mesh.merge(c);
        return mesh;
    };
    const TriangleMesh source   = mesh_at(9.999f, 5.f);
    const TriangleMesh improved = mesh_at(10.001f, 5.f);
    REQUIRE(MeshBoolean::cgal::does_self_intersect(source));
    REQUIRE(MeshBoolean::cgal::does_self_intersect(improved));
    CHECK(MeshBoolean::cgal::is_safe_partial_repair(source, improved));
    CHECK_FALSE(MeshBoolean::cgal::is_safe_partial_repair(source, mesh_at(10.001f, 5.001f)));
    CHECK_FALSE(MeshBoolean::cgal::is_safe_partial_repair(source, mesh_at(11.f, 5.f)));
}

TEST_CASE("CGAL partial checkpoints retain hole filling in one self-intersecting shell", "[MeshBoolean][ModelRepair][Partial][CGAL]")
{
    // One connected rectangular tube. A folded corner pierces its bottom near
    // x=10; the missing end at x=30 can be filled without changing that region.
    indexed_triangle_set tube;
    for (int ring = 0; ring < 4; ++ring) {
        const float x = float(ring * 10);
        tube.vertices.emplace_back(x, 0.f, 0.f);
        tube.vertices.emplace_back(x, 10.f, 0.f);
        tube.vertices.emplace_back(x, 10.f, 10.f);
        tube.vertices.emplace_back(x, 0.f, 10.f);
    }
    tube.vertices[6] = Vec3f(5.f, 5.f, -2.f);
    tube.indices     = {Vec3i32(0, 2, 1), Vec3i32(0, 3, 2)};
    for (int ring = 0; ring < 3; ++ring)
        for (int corner = 0; corner < 4; ++corner) {
            const int a = ring * 4 + corner;
            const int b = ring * 4 + (corner + 1) % 4;
            tube.indices.emplace_back(a, b, b + 4);
            tube.indices.emplace_back(a, b + 4, a + 4);
        }
    TriangleMesh source(std::move(tube));
    const auto   original = source.its;
    REQUIRE(its_split(source.its).size() == 1);
    REQUIRE(its_num_open_edges(source.its) == 4);
    REQUIRE(MeshBoolean::cgal::does_self_intersect(source));

    TriangleMesh strict = source;
    std::string  error;
    CHECK_FALSE(MeshBoolean::cgal::repair(strict, nullptr, &error));
    CHECK(strict.its.vertices == original.vertices);
    CHECK(strict.its.indices == original.indices);

    TriangleMesh partial = source;
    const auto   outcome = MeshBoolean::cgal::repair_with_partial(partial, &error);
    INFO(error);
    REQUIRE(outcome == MeshRepairOutcome::PartiallyRepaired);
    CHECK(its_num_open_edges(partial.its) == 0);
    CHECK(MeshBoolean::cgal::does_self_intersect(partial));
    CHECK(MeshBoolean::cgal::is_safe_partial_repair(source, partial));
    CHECK(source.its.vertices == original.vertices);
    CHECK(source.its.indices == original.indices);
}
