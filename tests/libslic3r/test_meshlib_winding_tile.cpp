#include "WindingTile.hpp"

#include <MRMesh/MRBitSet.h>
#include <MRMesh/MRCube.h>
#include <MRMesh/MRMakeSphereMesh.h>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using OrcaMeshLib::detail::WindingTile;

struct Coverage
{
    unsigned inside{}, outside{}, undecided{};
};

static int compare_tile(const MR::Mesh& mesh, MR::Vector3f first, float step, float threshold, Coverage& seen)
{
    const auto        center = [&](int x, int y, int z) { return first + step * MR::Vector3f(x, y, z); };
    const WindingTile tile(mesh, mesh.getAABBTree(), mesh.getDipoles(), first, center(7, 7, 7), 3.f);
    const int         sign = tile.certified_sign(threshold);
    if (!sign) {
        ++seen.undecided;
        return 0;
    }
    ++(sign > 0 ? seen.outside : seen.inside);
    for (int z = 0; z < 8; ++z)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                if (mesh.isOutside(center(x, y, z), threshold, 3.f) != (sign > 0))
                    throw std::runtime_error("Certified winding sign differs from the MeshLib SDK");
    return sign;
}

int main()
{
    auto           sphere = MR::makeUVSphere(1.f, 24, 16);
    auto           open   = sphere;
    MR::FaceBitSet hole(open.topology.faceSize());
    for (auto f : open.topology.getValidFaces())
        if (open.triCenter(f).z > 0.72f)
            hole.set(f);
    open.deleteFaces(hole);
    auto     cube = MR::makeCube();
    Coverage seen;
    for (const MR::Mesh* mesh : std::array<const MR::Mesh*, 3>{&sphere, &open, &cube}) {
        for (int z = -5; z <= 5; ++z)
            for (int y = -5; y <= 5; ++y)
                for (int x = -5; x <= 5; ++x)
                    compare_tile(*mesh, 0.27f * MR::Vector3f(x, y, z), 0.025f, 0.5f, seen);
        // At the actual winding threshold both possible roundoff directions
        // matter; include the negative atan2 branch cut and exact surface points.
        for (float z : {-0.5f, 0.f, 0.49999997f, 0.5f, 0.50000006f, 1.25f}) {
            const MR::Vector3f p(0.f, 0.f, z);
            const float        w = mesh->calcFastWindingNumber(p, 3.f);
            for (float threshold : {std::nextafter(w, -2.f), w, std::nextafter(w, 2.f)}) {
                const int sign = compare_tile(*mesh, p, 0.00001f, threshold, seen);
                if (threshold == w && sign != 0)
                    throw std::runtime_error("A threshold touching a sampled point must use the SDK");
            }
        }
    }
    // Opposite sides of one thin wall can share a tile far from the origin.
    // Here the y spacing is exactly two float ULPs at the translated position.
    const MR::Vector3f offset(16384.f, 4096.f, -8192.f);
    const auto foil = MR::makeCube({4.f, 1.f / 256.f, 2.f}, offset);
    const float step = 1.f / 1024.f;
    const auto first = offset + MR::Vector3f(1.f, step, 0.5f);
    if (foil.isOutside(first, 0.5f, 3.f) || !foil.isOutside(first + 7.f * MR::Vector3f::diagonal(step), 0.5f, 3.f))
        throw std::runtime_error("Translated thin-wall fixture must straddle the material boundary");
    if (compare_tile(foil, first, step, 0.5f, seen) != 0)
        throw std::runtime_error("One tile spanning material and air cannot have a common sign");
    std::cout << "tiles: " << seen.inside << " inside, " << seen.outside << " outside, " << seen.undecided << " using SDK\n";
    return seen.inside && seen.outside && seen.undecided ? 0 : 1;
}
