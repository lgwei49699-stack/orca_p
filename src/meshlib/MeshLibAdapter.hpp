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
    // False only when the caller has verified a closed, consistently oriented
    // triangle surface. Open or unverified input still needs hole filling.
    bool close_holes = true;
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
// Called synchronously after rebuilding, before any extra local repair. The
// caller's independent check decides whether one intersection repair is needed.
// Takes ownership so the source arrays can be freed after SDK import.
Result rebuild(Mesh input, const Options& options, const std::function<bool(const Mesh&)>& needs_local_repair = {});

} // namespace OrcaMeshLib
