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
