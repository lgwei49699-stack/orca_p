#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/filesystem.hpp>

#include "libslic3r/Point.hpp"
#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/TexturePainting.hpp"
#include "libslic3r/TextureToColor/ColorUtils.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace {

Slic3r::TexturedMesh make_textured_tetrahedron()
{
    Slic3r::TexturedMesh mesh;
    mesh.vertices = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
    mesh.indices  = {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}};
    mesh.uvs.assign(mesh.vertices.size(), {0.f, 0.f});
    return mesh;
}

bool contains_color_near(const std::vector<std::array<std::size_t, 3>>& colors, const std::array<std::size_t, 3>& expected,
                         std::size_t tolerance = 2)
{
    return std::any_of(colors.begin(), colors.end(), [&](const auto& color) {
        for (std::size_t channel = 0; channel < color.size(); ++channel) {
            const std::size_t difference = color[channel] > expected[channel] ? color[channel] - expected[channel] :
                                                                                 expected[channel] - color[channel];
            if (difference > tolerance)
                return false;
        }
        return true;
    });
}

std::uint64_t edge_key(int first, int second)
{
    const std::uint32_t low  = static_cast<std::uint32_t>(std::min(first, second));
    const std::uint32_t high = static_cast<std::uint32_t>(std::max(first, second));
    return (static_cast<std::uint64_t>(low) << 32) | high;
}

} // namespace

TEST_CASE("Texture import shares advanced repair without changing the skip or cancel result", "[TexturePainting][MeshRepair]")
{
    using namespace Slic3r;
    using tex2color::MeshRepairDecision;
    TexturedMesh mesh = make_textured_tetrahedron();
    mesh.indices.pop_back(); // Deliberately leave a triangular opening.
    mesh.textures.push_back({2, 2, 3, std::vector<unsigned char>(12, 255)});
    mesh.material_ids              = {0, 0, 1};
    mesh.material_texture_map      = {0, 0};
    mesh.material_colors           = {{{1.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 1.f, 1.f}}};
    const auto              source = mesh;
    TexturePaintingSettings settings;
    settings.target_colors_num             = 2;
    settings.smooth_weight                 = 0;
    settings.oversampling_iters            = 1;
    bool decision_required                 = false;
    settings.mesh_repair_decision_required = &decision_required;
    PaintedMesh painted;
    painted.vertices = {{42.f, 43.f, 44.f}};

    const auto open_edges = [](const PaintedMesh& result) {
        indexed_triangle_set its;
        for (const auto& p : result.vertices)
            its.vertices.emplace_back(p[0], p[1], p[2]);
        for (const auto& f : result.indices)
            its.indices.emplace_back(f[0], f[1], f[2]);
        return TriangleMesh(std::move(its)).stats().open_edges;
    };

    SECTION("Ask publishes no candidate and leaves the input untouched")
    {
        settings.mesh_repair_decision = MeshRepairDecision::Ask;
        REQUIRE_FALSE(texture_to_painting(mesh, painted, settings));
        REQUIRE(decision_required);
        REQUIRE(painted.vertices == std::vector<std::array<float, 3>>{{42.f, 43.f, 44.f}});
        REQUIRE(painted.indices.empty());
    }
    SECTION("Skipping advanced repair keeps the opening")
    {
        REQUIRE(texture_to_painting(mesh, painted, settings));
        REQUIRE_FALSE(decision_required);
        REQUIRE(open_edges(painted) > 0);
    }
    SECTION("Explicit repair closes the opening and retains both source colors")
    {
        settings.mesh_repair_decision = MeshRepairDecision::RepairAndImport;
        REQUIRE(texture_to_painting(mesh, painted, settings));
        REQUIRE_FALSE(decision_required);
        REQUIRE(open_edges(painted) == 0);
        REQUIRE(painted.indices.size() == painted.face_colors.size());
        REQUIRE(contains_color_near(painted.cluster_colors, {255, 0, 0}));
        REQUIRE(contains_color_near(painted.cluster_colors, {0, 0, 255}));
    }
    SECTION("Cancel inside advanced repair publishes no partial result")
    {
        settings.mesh_repair_decision = MeshRepairDecision::RepairAndImport;
        bool canceled                 = false;
        REQUIRE_FALSE(texture_to_painting(
            mesh, painted, settings,
            [&](int percent, const char* message) {
                if (percent >= 42 && std::string(message) == "Repairing mesh")
                    canceled = true;
            },
            [&] { return canceled; }));
        REQUIRE(canceled);
        REQUIRE(painted.vertices == std::vector<std::array<float, 3>>{{42.f, 43.f, 44.f}});
        REQUIRE(painted.indices.empty());
    }
    SECTION("An explicit repair failure does not fall back to un-repaired import")
    {
        mesh.indices.resize(1);
        mesh.material_ids.resize(1);
        settings.mesh_repair_decision = MeshRepairDecision::RepairAndImport;
        REQUIRE_FALSE(texture_to_painting(mesh, painted, settings));
        REQUIRE(painted.vertices == std::vector<std::array<float, 3>>{{42.f, 43.f, 44.f}});
        REQUIRE(painted.indices.empty());
        REQUIRE(mesh.indices.size() == 1);
        mesh.indices      = source.indices;
        mesh.material_ids = source.material_ids;
    }
    REQUIRE(mesh.vertices == source.vertices);
    REQUIRE(mesh.indices == source.indices);
    REQUIRE(mesh.material_ids == source.material_ids);
    REQUIRE(mesh.material_colors == source.material_colors);
}

TEST_CASE("Healthy texture meshes need no repair decision and keep the same result", "[TexturePainting][MeshRepair]")
{
    using namespace Slic3r;
    TexturedMesh mesh = make_textured_tetrahedron();
    mesh.textures.push_back({2, 2, 3, std::vector<unsigned char>(12, 255)});
    TexturePaintingSettings settings;
    settings.target_colors_num  = 1;
    settings.smooth_weight      = 0;
    settings.oversampling_iters = 1;
    PaintedMesh baseline;
    REQUIRE(texture_to_painting(mesh, baseline, settings));
    for (auto decision : {tex2color::MeshRepairDecision::Ask, tex2color::MeshRepairDecision::RepairAndImport}) {
        bool required                          = true;
        settings.mesh_repair_decision          = decision;
        settings.mesh_repair_decision_required = &required;
        PaintedMesh actual;
        REQUIRE(texture_to_painting(mesh, actual, settings));
        REQUIRE_FALSE(required);
        REQUIRE(actual.vertices == baseline.vertices);
        REQUIRE(actual.indices == baseline.indices);
        REQUIRE(actual.face_colors == baseline.face_colors);
    }
}

TEST_CASE("Texture repair preserves the bore of an open colored pipe", "[TexturePainting][MeshRepair]")
{
    using namespace Slic3r;
    TexturedMesh    input;
    constexpr int   sides = 16;
    constexpr float outer = 5.f, inner = 1.5f, height = 10.f;
    for (float radius : {outer, inner})
        for (int i = 0; i < sides; ++i) {
            const double angle = 2. * PI * i / sides;
            input.vertices.push_back({float(radius * std::cos(angle)), float(radius * std::sin(angle)), 0.f});
            input.vertices.push_back({float(radius * std::cos(angle)), float(radius * std::sin(angle)), height});
        }
    for (int i = 0; i < sides; ++i) {
        const int a = 2 * i, b = a + 1, c = 2 * ((i + 1) % sides), d = c + 1;
        input.indices.push_back({a, c, d});
        input.indices.push_back({a, d, b});
        input.indices.push_back({2 * sides + a, 2 * sides + d, 2 * sides + c});
        input.indices.push_back({2 * sides + a, 2 * sides + b, 2 * sides + d});
    }
    if (GENERATE(false, true)) {
        const auto faces = input.indices;
        for (size_t i = 0; i < faces.size(); ++i)
            if (i % 4 >= 2)
                input.indices.push_back({faces[i][2], faces[i][1], faces[i][0]});
    }
    input.uvs.assign(input.vertices.size(), {0.5f, 0.5f});
    input.textures.push_back({2, 2, 3, std::vector<unsigned char>(12, 255)});
    TexturePaintingSettings settings;
    settings.mesh_repair_decision = tex2color::MeshRepairDecision::RepairAndImport;
    settings.target_colors_num    = 1;
    settings.smooth_weight        = 0;
    settings.oversampling_iters   = 1;
    PaintedMesh painted;
    REQUIRE(texture_to_painting(input, painted, settings));
    indexed_triangle_set its;
    for (const auto& v : painted.vertices)
        its.vertices.emplace_back(v[0], v[1], v[2]);
    for (const auto& f : painted.indices)
        its.indices.emplace_back(f[0], f[1], f[2]);
    TriangleMesh mesh(std::move(its));
    REQUIRE(mesh.stats().manifold());
    const double expected_volume = height * 0.5 * sides * std::sin(2. * PI / sides) * (outer * outer - inner * inner);
    REQUIRE(mesh.volume() == Approx(expected_volume).epsilon(1e-5));
    REQUIRE(painted.indices.size() == painted.face_colors.size());
}

TEST_CASE("Disabled and weak texture smoothing retain isolated source color regions", "[TexturePainting]")
{
    using namespace Slic3r::tex2color;
    const auto source = make_textured_tetrahedron();
    TriMesh    mesh;
    for (const auto& v : source.vertices)
        mesh.vertices.emplace_back(v[0], v[1], v[2]);
    for (const auto& f : source.indices)
        mesh.indices.emplace_back(f[0], f[1], f[2]);
    color_utils::SmoothParameters settings;
    settings.smooth_weight = GENERATE(0., 0.2, 0.3, 0.5);
    SECTION("Label-based conversion")
    {
        std::vector<std::size_t> labels = {1, 0, 0, 0};
        REQUIRE(color_utils::smooth_region(mesh, labels, settings));
        REQUIRE(labels == (settings.smooth_weight < 0.3 ? std::vector<std::size_t>{1, 0, 0, 0} : std::vector<std::size_t>{0, 0, 0, 0}));
    }
    SECTION("RGB-based conversion")
    {
        const std::array<std::size_t, 3>        blue = {0, 0, 255}, red = {255, 0, 0};
        std::vector<std::array<std::size_t, 3>> colors = {blue, red, red, red};
        REQUIRE(color_utils::smooth_region(mesh, colors, settings));
        REQUIRE(colors.front() == (settings.smooth_weight < 0.3 ? blue : red));
        REQUIRE(std::all_of(colors.begin() + 1, colors.end(), [&](const auto& color) { return color == red; }));
    }
}

TEST_CASE("Texture painting falls back to baseColorFactor when a referenced texture cannot be decoded", "[TexturePainting]")
{
    Slic3r::TexturedMesh mesh = make_textured_tetrahedron();
    mesh.textures.push_back({-1, -1, 0, {0x00, 0x01, 0x02, 0x03}});
    mesh.material_ids.assign(mesh.indices.size(), 0);
    mesh.material_texture_map = {0};
    mesh.material_colors      = {{{1.f, 0.f, 0.f, 1.f}}};

    Slic3r::TexturePaintingSettings settings;
    settings.target_colors_num  = 1;
    settings.oversampling_iters = 1;

    Slic3r::PaintedMesh painted;
    REQUIRE(Slic3r::texture_to_painting(mesh, painted, settings));
    REQUIRE_FALSE(painted.face_colors.empty());
    REQUIRE(contains_color_near(painted.cluster_colors, {255, 0, 0}));
}

TEST_CASE("Texture painting separates material factors which share one texture", "[TexturePainting]")
{
    Slic3r::TexturedMesh mesh = make_textured_tetrahedron();
    mesh.textures.push_back({2, 2, 3, std::vector<unsigned char>(2 * 2 * 3, 255)});
    mesh.material_ids         = {0, 0, 1, 1};
    mesh.material_texture_map = {0, 0};
    mesh.material_colors      = {{{1.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 1.f, 1.f}}};

    Slic3r::TexturePaintingSettings settings;
    settings.target_colors_num  = 2;
    settings.oversampling_iters = 1;

    Slic3r::PaintedMesh painted;
    REQUIRE(Slic3r::texture_to_painting(mesh, painted, settings));
    REQUIRE(painted.cluster_colors.size() == 2);
    REQUIRE(contains_color_near(painted.cluster_colors, {255, 0, 0}));
    REQUIRE(contains_color_near(painted.cluster_colors, {0, 0, 255}));
}

TEST_CASE("Texture color clustering honors physical sample weights", "[TexturePainting]")
{
    using namespace Slic3r::tex2color::color_utils;

    ClusterParameters parameters;
    parameters.cluster_k              = 1;
    parameters.color_difference_method = ColorDifferenceMethod::RGB;
    parameters.sample_weights          = {9.0, 1.0};

    const std::vector<Color> centers = cluster_k_means({{0, 0, 0}, {255, 255, 255}}, parameters);
    REQUIRE(centers == std::vector<Color>{{26, 26, 26}});
}

TEST_CASE("Painted OBJ export remains successful when seam stitching leaves a nonmanifold edge", "[TexturePainting][OBJ]")
{
    Slic3r::PaintedMesh painted;
    painted.vertices = {
        {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
        {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f},
        {0.f, -1.f, 0.f},
    };
    painted.indices        = {{0, 1, 2}, {3, 4, 5}, {0, 1, 6}};
    painted.face_colors    = {{255, 0, 0}, {255, 0, 0}, {255, 0, 0}};
    painted.cluster_colors = {{255, 0, 0}};

    const boost::filesystem::path output_dir = boost::filesystem::temp_directory_path() /
                                                boost::filesystem::unique_path("orca-painted-obj-%%%%-%%%%");
    const boost::filesystem::path obj_path = output_dir / "nonmanifold.obj";
    boost::filesystem::create_directories(output_dir);

    REQUIRE(Slic3r::store_painted_mesh_obj(obj_path.string().c_str(), painted, {{1.f, 0.f, 0.f, 1.f}}));

    std::ifstream input(obj_path.string());
    REQUIRE(input.good());

    std::unordered_map<std::uint64_t, std::size_t> edge_counts;
    std::size_t vertex_count = 0;
    bool has_relative_mtl_reference = false;
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("v ", 0) == 0) {
            ++vertex_count;
            continue;
        }
        if (line == "mtllib nonmanifold.mtl") {
            has_relative_mtl_reference = true;
            continue;
        }
        if (line.rfind("f ", 0) != 0)
            continue;
        std::istringstream stream(line.substr(2));
        std::array<int, 3> face{};
        REQUIRE(stream >> face[0] >> face[1] >> face[2]);
        for (int& vertex : face)
            --vertex;
        ++edge_counts[edge_key(face[0], face[1])];
        ++edge_counts[edge_key(face[1], face[2])];
        ++edge_counts[edge_key(face[2], face[0])];
    }

    const auto max_edge_use = std::max_element(edge_counts.begin(), edge_counts.end(), [](const auto& left, const auto& right) {
        return left.second < right.second;
    });
    REQUIRE(max_edge_use != edge_counts.end());
    REQUIRE(max_edge_use->second == 3);
    REQUIRE(vertex_count == 4);
    REQUIRE(has_relative_mtl_reference);

    input.close();
    boost::filesystem::remove_all(output_dir);
}
