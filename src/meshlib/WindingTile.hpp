#pragma once

#include <MRMesh/MRAABBTree.h>
#include <MRMesh/MRDipole.h>
#include <MRMesh/MRMesh.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace OrcaMeshLib::detail {

// Bound the SDK's winding sum for an entire voxel tile. At a node whose
// acceptance varies across the tile, include both the dipole and child sums.
// For leaves, bound the signed triangle angle over the tile. An inconclusive
// interval always uses the SDK evaluator at each voxel center.
struct WindingTile
{
    struct Range
    {
        float low = 0.f, high = 0.f;
    };
    const MR::Mesh&     mesh;
    const MR::AABBTree& tree;
    const MR::Dipoles&  dipoles;
    MR::Vector3f        first, last;
    float               beta_squared;

    MR::Vector3d tile_center;
    double       tile_radius;

    WindingTile(const MR::Mesh&     source,
                const MR::AABBTree& cached_tree,
                const MR::Dipoles&  cached_dipoles,
                const MR::Vector3f& first_center,
                const MR::Vector3f& last_center,
                float               beta)
        : mesh(source)
        , tree(cached_tree)
        , dipoles(cached_dipoles)
        , first(first_center)
        , last(last_center)
        , beta_squared(beta * beta)
        , tile_center((MR::Vector3d(first) + MR::Vector3d(last)) * 0.5)
        , tile_radius((MR::Vector3d(last) - MR::Vector3d(first)).length() * 0.5)
    {}

    // The SDK leaf angle is twice atan2(det(a,b,c), |a||b||c| + cyclic dot(a,b)|c|).
    // Moving the query by u changes dot(a,b) by dot(u,a+b) + |u|^2.
    // The determinant is affine in the query with gradient equal to the face normal (up to sign).
    bool leaf_range(MR::FaceId face, Range& sum) const
    {
        const auto   tri    = mesh.getTriPoints(face);
        const double radius = tile_radius;
        MR::Vector3d v[3];
        double       lo[3], hi[3];
        for (int i = 0; i < 3; ++i) {
            v[i]                = MR::Vector3d(tri[i]) - tile_center;
            const double length = v[i].length();
            lo[i]               = std::max(0., length - radius);
            hi[i]               = length + radius;
        }
        const double scale = hi[0] * hi[1] * hi[2];
        // When vector lengths span extreme exponents, use SDK float arithmetic directly.
        // In this range products of up to three lengths stay normal and finite.
        for (int i = 0; i < 3; ++i)
            if (!(lo[i] > 0x1p-32 && hi[i] < 0x1p32))
                return false;
        double dlow = lo[0] * lo[1] * lo[2], dhigh = scale;
        for (int i = 0; i < 3; ++i) {
            const int    j = (i + 1) % 3, k = (i + 2) % 3;
            const double d      = dot(v[i], v[j]);
            const double change = radius * (v[i] + v[j]).length();
            const double a = d - change, b = d + change + radius * radius;
            dlow += a * (a < 0. ? hi[k] : lo[k]);
            dhigh += b * (b < 0. ? lo[k] : hi[k]);
        }
        // Evaluate the determinant from the three query-relative vectors.
        // This keeps its roundoff bound proportional to |a||b||c| even on skinny triangles.
        const auto   bc     = cross(v[1], v[2]);
        const auto   normal = bc + cross(v[2], v[0]) + cross(v[0], v[1]);
        const double error  = 128. * std::numeric_limits<float>::epsilon() * scale;
        const double det = dot(v[0], bc), spread = radius * normal.length() + error;
        const double nlow = det - spread, nhigh = det + spread;
        dlow -= error;
        dhigh += error;
        // atan2 is discontinuous across the negative horizontal axis.
        if (!(dlow > 0.) && !(nlow > 0. || nhigh < 0.))
            return false;
        const double ang[]  = {std::atan2(nlow, dlow), std::atan2(nlow, dhigh), std::atan2(nhigh, dlow), std::atan2(nhigh, dhigh)};
        const auto   limits = std::minmax_element(std::begin(ang), std::end(ang));
        sum.low += float(2. * *limits.first - 1.e-5);
        sum.high += float(2. * *limits.second + 1.e-5);
        return true;
    }

    bool accumulate(MR::NodeId id, Range& sum) const
    {
        const auto& dipole = dipoles[id];
        // Same float subtractions as the SDK. Every voxel center in this
        // tile lies between the endpoints, including rounded coordinates.
        const auto low = dipole.pos - last, high = dipole.pos - first;
        double     near_sq = 0., far_sq = 0., dot_low = 0., dot_high = 0., absolute_dot = 0.;
        for (int axis = 0; axis < 3; ++axis) {
            const double a = low[axis], b = high[axis], area = dipole.dirArea[axis];
            const double nearest = std::max({0., a, -b});
            near_sq += nearest * nearest;
            far_sq += std::max(a * a, b * b);
            dot_low += std::min(a * area, b * area);
            dot_high += std::max(a * area, b * area);
            absolute_dot += std::max(std::abs(a * area), std::abs(b * area));
        }
        // Enclose float products and the two additions of a 3-D dot product,
        // with or without contraction. Use absolute terms for cancellation.
        constexpr double slack = 8. * std::numeric_limits<float>::epsilon();
        constexpr double tiny  = std::numeric_limits<float>::min();
        near_sq                = near_sq * (1. - slack) - 8. * tiny;
        far_sq                 = far_sq * (1. + slack) + 8. * tiny;
        const float cutoff     = beta_squared * dipole.rr;
        if (!std::isfinite(cutoff) || !std::isfinite(far_sq))
            return false;
        const bool may_use_dipole = far_sq > cutoff;
        Range      far_sum;
        if (may_use_dipole) {
            if (!(near_sq > 0.))
                return false;
            const double denom_low  = near_sq * std::sqrt(near_sq) * (1. - slack);
            const double denom_high = far_sq * std::sqrt(far_sq) * (1. + slack);
            const double limit      = std::numeric_limits<float>::max() * 0.5;
            if (!(denom_low >= tiny && denom_high < limit && absolute_dot < limit))
                return false;
            const double error = slack * absolute_dot + 8. * tiny;
            dot_low -= error;
            dot_high += error;
            double lower = dot_low / (dot_low < 0. ? denom_low : denom_high);
            double upper = dot_high / (dot_high > 0. ? denom_low : denom_high);
            if (!(lower > -limit && upper < limit))
                return false;
            // Enclose the SDK division, then accumulate in its left-first
            // order. Monotone float addition carries the enclosure forward.
            far_sum.low  = sum.low + float(lower - slack * std::abs(lower) - 8. * tiny);
            far_sum.high = sum.high + float(upper + slack * std::abs(upper) + 8. * tiny);
            if (near_sq > cutoff) {
                sum = far_sum;
                return true;
            }
        }
        const auto& node = tree[id];
        if (node.leaf() ? !leaf_range(node.leafId(), sum) : (!accumulate(node.l, sum) || !accumulate(node.r, sum)))
            return false;
        if (may_use_dipole) {
            sum.low  = std::min(sum.low, far_sum.low);
            sum.high = std::max(sum.high, far_sum.high);
        }
        return true;
    }

    int certified_sign(float threshold) const
    {
        Range sum;
        if (!accumulate(MR::AABBTree::rootNodeId(), sum) || !std::isfinite(sum.low) || !std::isfinite(sum.high))
            return 0;
        constexpr float scale = 1.f / (4.f * MR::PI_F);
        if (sum.high * scale < threshold)
            return 1;
        if (sum.low * scale > threshold)
            return -1;
        return 0;
    }
};

} // namespace OrcaMeshLib::detail
