#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// This C++17-compatible boundary deliberately exposes no MeshLib, Eigen, TBB,
// OpenVDB, or JSON types. The implementation is an internal C++20 build target.
namespace OrcaMeshLib {

struct Mesh
{
    std::vector<std::array<float, 3>>   vertices;
    std::vector<std::array<int32_t, 3>> faces;
};

struct Options
{
    double voxel_mm         = 0.;
    size_t max_input_faces  = 3000000;
    size_t max_output_faces = 1000000;
    // May be called by SDK threads. Must be thread-safe. False requests
    // cooperative cancellation; this cannot interrupt arbitrary SDK code.
    std::function<bool(float)> progress;
};

struct Result
{
    bool        success = false;
    Mesh        mesh;
    std::string status;
    std::string message;
    std::string report_json;
};

bool available() noexcept;
// The caller owns admission estimates, serialization of SDK jobs, and the
// cooperative budget callback. Face caps are enforced here, but they are not a
// hard ceiling on SDK intermediate allocations or a crash-containment boundary.
Result rebuild(const Mesh& input, const Options& options);

} // namespace OrcaMeshLib
