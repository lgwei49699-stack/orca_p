#include "MeshLibAdapter.hpp"
#include "runtime_identity.hpp"

#include <MRMesh/MRBitSet.h>
#include <MRMesh/MRMesh.h>
#include <MRMesh/MRMeshDecimate.h>
#include <MRMesh/MRBuffer.h>
#include <MRMesh/MRMeshBuilder.h>
#include <MRMesh/MRParallelFor.h>
#include <MRMesh/MRQuadraticForm.h>
#include <MRMesh/MRRingIterator.h>
#include <MRMesh/MRSystem.h>
#include <MRMesh/MRTriMesh.h>
#include <MRMesh/MRTriangleIntersection.h>
#include <MRVoxels/MRRebuildMesh.h>
#include <MRVoxels/MRMarchingCubes.h>
#include <MRVoxels/MRCalcDims.h>
#include <MRVoxels/MRMeshToDistanceVolume.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace OrcaMeshLib {
namespace {
namespace fs                           = std::filesystem;
using Json                             = nlohmann::json;
using Clock                            = std::chrono::steady_clock;
constexpr const char* SDK_VERSION      = "3.1.3.429";
constexpr float       WINDING_NUMBER_BETA = 3.f;
constexpr size_t      MAX_INPUT_FACES  = 3000000;
constexpr size_t      MAX_OUTPUT_FACES = 1000000;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

struct Failure : std::runtime_error
{
    std::string status;
    Failure(const char* code, const std::string& message) : std::runtime_error(message), status(code) {}
};

void require(bool condition, const char* status, const char* message)
{
    if (!condition)
        throw Failure(status, message);
}

double elapsed_ms(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

// Never allow a caller's exception to unwind through the SDK's thread pool.
class Progress
{
public:
    explicit Progress(const std::function<bool(float)>& callback) : callback_(callback) {}

    bool operator()(float value) noexcept
    {
        if (stopped_.load(std::memory_order_relaxed))
            return false;
        try {
            if (!callback_ || callback_(value))
                return true;
        } catch (...) {
            callback_failed_.store(true, std::memory_order_relaxed);
        }
        stopped_.store(true, std::memory_order_relaxed);
        return false;
    }

    void checkpoint()
    {
        if (!(*this)(0.f))
            throw Failure(callback_failed_.load(std::memory_order_relaxed) ? "callback_error" : "cancelled",
                          "MeshLib operation stopped by its cooperative control callback.");
    }

    bool stopped() const noexcept { return stopped_.load(std::memory_order_relaxed); }
    bool callback_failed() const noexcept { return callback_failed_.load(std::memory_order_relaxed); }

private:
    const std::function<bool(float)>& callback_;
    std::atomic<bool>                 stopped_{false};
    std::atomic<bool>                 callback_failed_{false};
};

std::string sha256_file(const fs::path& path, Progress& progress)
{
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(context && EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) == 1, "sdk_version_mismatch",
            "Cannot initialize the SDK identity digest.");
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "sdk_version_mismatch", "Cannot read the loaded SDK library.");
    std::array<char, 65536> buffer{};
    while (input) {
        progress.checkpoint();
        input.read(buffer.data(), buffer.size());
        require(EVP_DigestUpdate(context.get(), buffer.data(), static_cast<size_t>(input.gcount())) == 1, "sdk_version_mismatch",
                "Cannot compute the SDK identity digest.");
    }
    require(input.eof(), "sdk_version_mismatch", "SDK library read failed during identity verification.");
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned                                   length = 0;
    require(EVP_DigestFinal_ex(context.get(), digest.data(), &length) == 1 && length == 32, "sdk_version_mismatch",
            "Cannot finish the SDK identity digest.");
    static constexpr char HEX[] = "0123456789abcdef";
    std::string           result;
    result.reserve(64);
    for (unsigned i = 0; i < length; ++i) {
        result += HEX[digest[i] >> 4];
        result += HEX[digest[i] & 15];
    }
    return result;
}

fs::path loaded_library_path(const void* symbol)
{
#ifdef _WIN32
    HMODULE module = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(symbol), &module) != 0,
            "sdk_version_mismatch", "Cannot locate the loaded SDK library.");
    // Use the Unicode API and grow the buffer for extended Windows paths. The
    // handle is borrowed; the loaded SDK must not be released by this check.
    for (size_t size = 512; size <= 65536; size *= 2) {
        std::wstring buffer(size, L'\0');
        const DWORD  length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        require(length != 0, "sdk_version_mismatch", "Cannot read the loaded SDK library path.");
        if (length < buffer.size()) {
            buffer.resize(length);
            return fs::canonical(fs::path(buffer));
        }
    }
    throw Failure("sdk_version_mismatch", "The loaded SDK library path is too long.");
#else
    Dl_info info{};
    require(dladdr(symbol, &info) != 0 && info.dli_fname, "sdk_version_mismatch", "Cannot locate the loaded SDK library.");
    return fs::canonical(info.dli_fname);
#endif
}

std::string utf8_path(const fs::path& path)
{
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

Json verify_runtime_libraries(Progress& progress)
{
    Json       libraries = Json::array();
    const auto verify    = [&](const char* name, const void* symbol, const char* expected_sha256) {
        const fs::path loaded = loaded_library_path(symbol);
        const auto     digest = sha256_file(loaded, progress);
        require(digest == expected_sha256, "sdk_version_mismatch", "The loaded SDK library changed since configuration.");
        // Content identity survives relocation; a build-machine absolute path
        // must not become a deployment requirement. This is not a loader sandbox.
        libraries.push_back({{"name", name}, {"path", utf8_path(loaded)}, {"sha256", digest}});
    };
    verify("MRMesh", reinterpret_cast<const void*>(&MR::GetMRVersionString), MESH_SHA256);
    verify("MRVoxels", reinterpret_cast<const void*>(&MR::rebuildMesh), VOXELS_SHA256);
    return libraries;
}

Json parameters(const Options& options)
{
    return {{"voxel_mm", static_cast<float>(options.voxel_mm)},
            {"input_mode", "independent_triangles"},
            {"sign_mode", "HoleWindingNumber"},
            {"offset_mode", "Standard"},
            {"pre_subdivide", false},
            {"close_holes_in_hole_winding_number", options.close_holes},
            {"winding_number_threshold", 0.5},
            {"winding_number_beta", WINDING_NUMBER_BETA},
            {"decimate", true},
            {"reduce_angle_iterations", 0},
            {"tiny_edge_length_mm", -1.f},
            {"decimation_max_error_mm", 0.25f * static_cast<float>(options.voxel_mm)},
            {"decimation_optimize_vertex_position", false},
            {"prevent_tetrahedron_collapse", true},
            {"prevent_coplanar_overlap", true},
            {"fragment_removal", "none"},
            {"max_input_faces", options.max_input_faces},
            {"max_output_faces", options.max_output_faces}};
}

// Reconstruction uses triangle geometry, not input adjacency. Keep each face
// independent: welding non-manifold fans in the SDK STL loader can drop faces.
// Only this private SDK representation is disconnected; the source model stays intact.
MR::Mesh input_mesh(const Mesh& input, const Options& options, Progress& progress, int& skipped)
{
    require(options.max_input_faces > 0 && options.max_input_faces <= MAX_INPUT_FACES && options.max_output_faces > 0 &&
                options.max_output_faces <= MAX_OUTPUT_FACES,
            "invalid_options", "MeshLib face budgets exceed the supported limits.");
    require(!input.faces.empty() && !input.vertices.empty(), "invalid_input", "Input mesh is empty.");
    require(input.faces.size() <= options.max_input_faces && input.vertices.size() <= 3 * options.max_input_faces, "resource_limit",
            "MeshLib input exceeds the face or vertex budget.");
    MR::VertCoords points;
    points.resize(3 * input.faces.size());
    MR::Triangulation triangles;
    triangles.resize(input.faces.size());
    for (size_t face_id = 0; face_id < input.faces.size(); ++face_id) {
        if ((face_id & 1023u) == 0)
            progress.checkpoint();
        const auto& face = input.faces[face_id];
        for (size_t corner = 0; corner < 3; ++corner) {
            require(face[corner] >= 0 && size_t(face[corner]) < input.vertices.size(), "invalid_input",
                    "Input mesh contains an invalid vertex index.");
            const auto& vertex = input.vertices[size_t(face[corner])];
            require(std::isfinite(vertex[0]) && std::isfinite(vertex[1]) && std::isfinite(vertex[2]), "invalid_input",
                    "Input mesh contains a non-finite coordinate.");
            const MR::VertId id(int(3 * face_id + corner));
            points[id]                                  = {vertex[0], vertex[1], vertex[2]};
            triangles[MR::FaceId(int(face_id))][corner] = id;
        }
    }
    MR::MeshBuilder::BuildSettings settings;
    settings.skippedFaceCount = &skipped;
    MR::Mesh source;
    source.points = std::move(points);
    // Independent triangles have six half-edges each; avoid parallel part copies.
    source.topology.edgeReserve(6 * triangles.size());
    MR::MeshBuilder::addTriangles(source.topology, triangles, settings);
    progress.checkpoint();
    return source;
}

// Supply the same SDK distance samples in Z slabs, reusing the shared plane.
// This avoids a full XY slice cache for each meshing thread.
template<typename Emit>
MR::Expected<void> sample_distance_parts(const MR::Mesh& mesh, float voxel, const MR::Vector3i& dims,
                                        const MR::DistanceToMeshOptions& options, const Emit& emit, const MR::ProgressCallback& progress)
{
    MR::MeshToDistanceVolumeParams params;
    params.vol.origin     = MR::calcOriginAndDimensions(mesh.computeBoundingBox(), voxel).origin;
    params.vol.dimensions = dims;
    params.vol.voxelSize  = MR::Vector3f::diagonal(voxel);
    params.dist           = {options, MR::SignDetectionMode::HoleWindingRule};
    // Preserve the SDK's float evaluation order for voxel-center coordinates.
    const auto       volume       = MR::meshToDistanceFunctionVolume(mesh, params);
    constexpr size_t CHUNK_VALUES = 8 * 1024 * 1024;
    const size_t     plane_size   = size_t(dims.x) * size_t(dims.y);
    const int        depth        = std::max(2, int(std::min<size_t>(dims.z, CHUNK_VALUES / plane_size)));
    std::vector<float> overlap_values;
    for (int z = 0; z < dims.z;) {
        const int          count = std::min(depth, dims.z - z);
        std::vector<float> values(plane_size * size_t(count));
        std::copy(overlap_values.begin(), overlap_values.end(), values.begin());
        const auto part_progress = MR::subprogress(progress, float(z + int(overlap_values.size() / plane_size)) / dims.z,
                                                   float(z + count) / dims.z);
        if (!MR::ParallelFor(
                overlap_values.size(), values.size(),
                [&](size_t i) {
                    const MR::Vector3i p(int(i % size_t(dims.x)), int((i / size_t(dims.x)) % size_t(dims.y)), z + int(i / plane_size));
                    values[i] = volume.data(p);
                },
                part_progress))
            return MR::unexpectedOperationCanceled();
        if (z + count < dims.z)
            overlap_values.assign(values.end() - plane_size, values.end());
        auto result = emit(std::move(values), MR::Vector3i(dims.x, dims.y, count));
        if (!result)
            return result;
        if (progress && !progress(float(z + count) / dims.z))
            return MR::unexpectedOperationCanceled();
        if (z + count == dims.z)
            break;
        z += count - 1;
    }
    return {};
}

// Keep the SDK sampling and marching-cubes geometry, but release its temporary
// mesher before building topology. Reserving the closed surface's half-edges
// avoids repeated large reallocations and parallel topology copies.
MR::Expected<MR::Mesh> rebuild_surface(const MR::Mesh& source, float voxel, bool close_holes, Progress& progress)
{
    const auto [origin, dims] = MR::calcOriginAndDimensions(source.computeBoundingBox(), voxel);
    const auto voxel_size    = MR::Vector3f::diagonal(voxel);
    MR::DistanceToMeshOptions distance;
    distance.minDistSq              = 0.f;
    distance.maxDistSq              = MR::sqr(1.001f * voxel);
    distance.nullOutsideMinMax      = !close_holes;
    distance.windingNumberThreshold = 0.5f;
    distance.windingNumberBeta      = WINDING_NUMBER_BETA;
    auto triangles = [&]() -> MR::Expected<MR::TriMesh> {
        MR::MarchingCubesParams params;
        params.origin     = origin;
        params.lessInside = true;
        bool extracting   = false;
        // Sampling reports its own progress; keep SDK addPart cancellation active too.
        params.cb = [&](float value) { return progress(extracting ? 0.72f + 0.08f * value : 0.f); };
        MR::MarchingCubesByParts mesher(dims, params);
        const auto emit = [&](std::vector<float>&& data, const MR::Vector3i& part_dims) {
            MR::SimpleVolume part;
            part.data      = std::move(data);
            part.dims      = part_dims;
            part.voxelSize = voxel_size;
            return mesher.addPart(part);
        };
        auto sampled = sample_distance_parts(source, voxel, dims, distance, emit,
                                             [&](float value) { return progress(0.72f * value); });
        if (!sampled)
            return MR::unexpected(sampled.error());
        extracting = true;
        return mesher.finalize();
    }();
    if (!triangles)
        return MR::unexpected(triangles.error());
    progress.checkpoint();
    MR::Mesh candidate;
    candidate.points = std::move(triangles->points);
    candidate.topology.edgeReserve(3 * triangles->tris.size());
    const auto added = MR::MeshBuilder::addTriangles(candidate.topology, triangles->tris);
    require(added == triangles->tris.size(), "candidate_rejected", "MeshLib output topology counts are inconsistent.");
    progress.checkpoint();
    return candidate;
}

Mesh copy_candidate(const MR::Mesh& mesh, size_t face_limit, Progress& progress)
{
    const auto count = mesh.topology.numValidFaces();
    require(count > 0, "candidate_rejected", "MeshLib returned an empty mesh.");
    require(size_t(count) <= face_limit && size_t(mesh.topology.numValidVerts()) <= 3 * face_limit, "resource_limit",
            "MeshLib candidate exceeds the output face or vertex budget.");
    std::vector<int32_t> mapping(mesh.points.size(), -1);
    Mesh                 result;
    result.vertices.reserve(std::min<size_t>(mesh.topology.numValidVerts(), size_t(count) * 3));
    result.faces.reserve(size_t(count));
    for (const auto face : mesh.topology.getValidFaces()) {
        if ((result.faces.size() & 1023u) == 0)
            progress.checkpoint();
        const auto             ids = mesh.topology.getTriVerts(face);
        std::array<int32_t, 3> triangle{};
        for (size_t corner = 0; corner < 3; ++corner) {
            const int index = int(ids[corner]);
            require(index >= 0 && size_t(index) < mesh.points.size(), "candidate_rejected", "MeshLib returned an invalid vertex index.");
            if (mapping[size_t(index)] < 0) {
                const auto& point = mesh.points[ids[corner]];
                require(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z), "candidate_rejected",
                        "MeshLib returned a non-finite coordinate.");
                mapping[size_t(index)] = static_cast<int32_t>(result.vertices.size());
                result.vertices.push_back({point.x, point.y, point.z});
            }
            triangle[corner] = mapping[size_t(index)];
        }
        result.faces.push_back(triangle);
    }
    require(result.faces.size() == size_t(count) && !result.vertices.empty(), "candidate_rejected",
            "MeshLib output topology counts are inconsistent.");
    return result;
}
} // namespace

bool available() noexcept { return true; }

Result rebuild(const Mesh& input, const Options& options)
{
    const auto started = Clock::now();
    Result     result;
    Progress   progress(options.progress);
    Json       report = {{"schema", "meshlib_adapter_v1"}, {"engine", "meshlib"},          {"execution", "in_process"},
                         {"sdk_version", SDK_VERSION},     {"status", "failed"},           {"parameters", Json::object()},
                         {"input", Json::object()},        {"output", Json::object()},     {"elapsed_ms", Json::object()},
                         {"skipped_face_count", 0},        {"duplicated_vertex_count", 0}, {"candidate_validated", false},
                         {"print_ready", false},           {"fragment_removal", "none"},   {"runtime_identity_verified", false}};
    try {
        progress.checkpoint();
        require(std::isfinite(options.voxel_mm) && options.voxel_mm >= static_cast<double>(std::numeric_limits<float>::denorm_min()) &&
                    options.voxel_mm <= static_cast<double>(std::numeric_limits<float>::max()),
                "invalid_options", "Voxel size must be finite, positive and representable as a float.");
        const float voxel                        = static_cast<float>(options.voxel_mm);
        report["parameters"]                     = parameters(options);
        const auto identity_started              = Clock::now();
        report["runtime_libraries"]              = verify_runtime_libraries(progress);
        report["runtime_identity_verified"]      = true;
        report["elapsed_ms"]["runtime_identity"] = elapsed_ms(identity_started);
        // Diagnostic only: this API may return a build date, not an SDK version.
        report["sdk_runtime_version"]    = MR::GetMRVersionString();
        report["input"]                  = {{"faces", input.faces.size()}, {"vertices", input.vertices.size()}, {"units", "mm"}};
        int        skipped               = 0;
        const auto load_started          = Clock::now();
        auto       source                = input_mesh(input, options, progress, skipped);
        report["elapsed_ms"]["sdk_load"] = elapsed_ms(load_started);
        report["skipped_face_count"]     = skipped;
        progress.checkpoint();
        report["input"]["loaded_faces"]    = source.topology.numValidFaces();
        report["input"]["loaded_vertices"] = source.topology.numValidVerts();
        require(skipped == 0 && size_t(source.topology.numValidFaces()) == input.faces.size(), "input_faces_skipped",
                "MeshLib omitted input faces while constructing topology; refusing to rebuild a truncated model.");

        const auto rebuild_started       = Clock::now();
        auto       candidate             = rebuild_surface(source, voxel, options.close_holes, progress);
        report["elapsed_ms"]["rebuild"]    = elapsed_ms(rebuild_started);
        report["selected_sign_mode"]      = int(MR::SignDetectionMode::HoleWindingRule);
        report["selected_sign_mode_name"] = MR::asString(MR::SignDetectionMode::HoleWindingRule);
        progress.checkpoint();
        if (!candidate)
            throw Failure("engine_error", candidate.error());
        // Sampling no longer holds winding references; simplification only uses the candidate.
        source = MR::Mesh{};
        const auto           simplify_started = Clock::now();
        MR::DecimateSettings decimation;
        decimation.maxError          = 0.25f * voxel; // Same voxel-relative error metric as SDK rebuildMesh.
        decimation.stabilizer        = 1e-5f;
        decimation.optimizeVertexPos = false;
        // copy_candidate already compacts valid faces and vertices into the output mesh.
        decimation.packMesh          = false;
        struct LocalFace
        {
            MR::VertId   a, b;
            MR::Vector3d normal;
        };
        std::vector<LocalFace> local_faces;
        decimation.preCollapse = [&](MR::EdgeId edge, const MR::Vector3f& position) {
            const auto& topology = candidate->topology;
            // Preserve the smallest closed shell. A tetrahedron with coincident
            // vertices is already collapsed, so allow the SDK to eliminate it.
            if (topology.getOrgDegree(edge) == 3 && topology.getOrgDegree(edge.sym()) == 3) {
                const auto& a = candidate->points[topology.org(edge)];
                const auto& b = candidate->points[topology.dest(edge)];
                const auto& c = candidate->points[topology.dest(topology.next(edge))];
                const auto& d = candidate->points[topology.dest(topology.next(edge.sym()))];
                if (a != b && a != c && a != d && b != c && b != d && c != d)
                    return false;
            }
            // A five-vertex closed shell becomes a tetrahedron after collapse.
            // Keep its signed volume: near-coplanar faces can otherwise invert a cavity.
            const auto degree_a = topology.getOrgDegree(edge), degree_b = topology.getOrgDegree(edge.sym());
            if ((degree_a == 3 && degree_b == 4) || (degree_a == 4 && degree_b == 3)) {
                std::vector<MR::VertId> vertices{topology.org(edge), topology.dest(edge)};
                for (const auto end : {edge, edge.sym()})
                    for (const auto ring : MR::orgRing(topology, end)) {
                        const auto v = topology.dest(ring);
                        if (std::find(vertices.begin(), vertices.end(), v) == vertices.end())
                            vertices.push_back(v);
                    }
                if (vertices.size() == 5) {
                    std::vector<MR::FaceId> faces;
                    bool entire_shell = true;
                    for (const auto v : vertices)
                        for (const auto ring : MR::orgRing(topology, v)) {
                            if (std::find(vertices.begin(), vertices.end(), topology.dest(ring)) == vertices.end() ||
                                !topology.left(ring)) {
                                entire_shell = false;
                                break;
                            }
                            const auto f = topology.left(ring);
                            if (std::find(faces.begin(), faces.end(), f) == faces.end())
                                faces.push_back(f);
                        }
                    if (entire_shell && faces.size() == 6) {
                        double before = 0., after = 0.;
                        const MR::Vector3d p(position);
                        for (const auto f : faces) {
                            const auto ids = topology.getTriVerts(f);
                            const double volume = MR::dot(MR::Vector3d(candidate->points[ids[0]]) - p,
                                MR::cross(MR::Vector3d(candidate->points[ids[1]]) - p,
                                          MR::Vector3d(candidate->points[ids[2]]) - p));
                            before += volume;
                            bool moved = false;
                            for (const auto id : ids)
                                if (id == topology.org(edge) || id == topology.dest(edge))
                                    moved = true;
                            // All moved faces contain p after collapse and contribute zero.
                            if (!moved)
                                after += volume;
                        }
                        if (before != 0. && (after == 0. || (before < 0.) != (after < 0.)))
                            return false;
                    }
                }
            }
            local_faces.clear();
            for (const auto end : {edge, edge.sym()}) {
                for (const auto adjacent : MR::orgRing(topology, end)) {
                    const auto face = topology.left(adjacent);
                    if (!face || face == topology.left(edge) || face == topology.right(edge))
                        continue; // These two faces disappear during collapse.
                    const auto a = topology.dest(adjacent), b = topology.dest(topology.next(adjacent));
                    const auto normal = MR::cross(MR::Vector3d(candidate->points[a]) - MR::Vector3d(position),
                                                  MR::Vector3d(candidate->points[b]) - MR::Vector3d(position));
                    // Every surviving face shares the collapsed vertex. Adjacent faces
                    // with opposite coplanar normals overlap across their shared edge.
                    for (const auto& previous : local_faces) {
                        const bool shared_edge = a == previous.a || a == previous.b || b == previous.a || b == previous.b;
                        if (shared_edge && MR::dot(normal, previous.normal) < 0 && MR::cross(normal, previous.normal).lengthSq() == 0)
                            return false;
                    }
                    const MR::Vector3d p(position), pa(candidate->points[a]), pb(candidate->points[b]);
                    for (const auto& previous : local_faces)
                        if (a != previous.a && a != previous.b && b != previous.a && b != previous.b &&
                            MR::doTrianglesIntersect(p, pa, pb, p, MR::Vector3d(candidate->points[previous.a]),
                                                     MR::Vector3d(candidate->points[previous.b])))
                            return false;
                    // Check unchanged faces incident to either fixed corner. A valid shared
                    // edge is not a crossing; coplanar edges entering a face need a separate test.
                    for (const auto shared : {a, b}) {
                        const auto other = shared == a ? b : a;
                        const MR::Vector3d origin(candidate->points[shared]);
                        const auto u = p - origin, v = MR::Vector3d(candidate->points[other]) - origin;
                        const auto n = MR::cross(u, v);
                        for (const auto neighbor : MR::orgRing(topology, shared)) {
                            const auto id = topology.dest(neighbor);
                            if (id != other && id != topology.org(edge) && id != topology.dest(edge)) {
                                const auto w = MR::Vector3d(candidate->points[id]) - origin;
                                if (n.lengthSq() != 0. && MR::dot(n, w) == 0.) {
                                    const auto left = MR::cross(u, w), right = MR::cross(w, v);
                                    if ((MR::dot(left, n) > 0. && MR::dot(right, n) > 0.) ||
                                        (left.lengthSq() == 0. && MR::dot(u, w) > 0.) ||
                                        (right.lengthSq() == 0. && MR::dot(v, w) > 0.))
                                        return false;
                                }
                            }
                            if (!topology.left(neighbor))
                                continue;
                            const auto third = topology.dest(topology.next(neighbor));
                            if (id == other || third == other || id == topology.org(edge) || id == topology.dest(edge) ||
                                third == topology.org(edge) || third == topology.dest(edge))
                                continue;
                            if (MR::doTrianglesIntersect(origin, p, MR::Vector3d(candidate->points[other]), origin,
                                                         MR::Vector3d(candidate->points[id]), MR::Vector3d(candidate->points[third])))
                                return false;
                        }
                    }
                    local_faces.push_back({a, b, normal});
                }
            }
            return true;
        };
        const auto simplified = [&] {
            // Limit the collapse queue without splitting the mesh. All passes share
            // the initial quadrics and accumulated error; do not reset it per region.
            constexpr int regions = 4;
            auto forms = MR::computeFormsAtVertices(MR::MeshPart(*candidate), decimation.stabilizer,
                                                   decimation.angleWeightedDistToPlane, decimation.notFlippable);
            progress.checkpoint();
            decimation.vertForms = &forms;
            const auto initial_faces = candidate->topology.getValidFaces();
            for (int i = 0; i < regions; ++i) {
                auto region = MR::getSubdividePart(initial_faces, regions, i);
                auto partial = decimation;
                partial.region = &region;
                partial.touchNearBdEdges = false;
                partial.progressCallback = [&](float value) { return progress(0.8f + 0.2f * (i + value) / (regions + 1)); };
                const auto step = MR::decimateMesh(*candidate, partial);
                progress.checkpoint();
                if (step.cancelled)
                    return step;
            }
            // Simplify the retained region boundaries on the same complete mesh.
            decimation.progressCallback = [&](float value) { return progress(0.8f + 0.2f * (regions + value) / (regions + 1)); };
            return MR::decimateMesh(*candidate, decimation);
        }();
        progress.checkpoint();
        if (simplified.cancelled)
            throw Failure("cancelled", "MeshLib simplification was canceled.");
        report["elapsed_ms"]["simplify"]       = elapsed_ms(simplify_started);
        const auto copy_started                = Clock::now();
        result.mesh                            = copy_candidate(*candidate, options.max_output_faces, progress);
        report["elapsed_ms"]["candidate_copy"] = elapsed_ms(copy_started);
        report["output"]                       = {{"faces", result.mesh.faces.size()}, {"vertices", result.mesh.vertices.size()}};
        progress.checkpoint();
        result.status  = "candidate_generated";
        result.message = "MeshLib generated a candidate; independent Orca geometry validation is still required.";
        // Publish success only after all potentially allocating result fields
        // have been assigned; an allocation failure must not retain success.
        result.success = true;
    } catch (const Failure& error) {
        result.status  = error.status;
        result.message = std::string(error.what()).substr(0, 4096);
    } catch (const std::bad_alloc&) {
        result.status  = "resource_limit";
        result.message = "MeshLib could not allocate memory.";
    } catch (const std::exception& error) {
        result.status  = "engine_error";
        result.message = std::string(error.what()).substr(0, 4096);
    } catch (...) {
        result.status  = "engine_error";
        result.message = "MeshLib failed with an unknown exception.";
    }
    if (!result.success)
        result.mesh = {};
    report["status"]              = result.status;
    report["message"]             = result.message;
    report["elapsed_ms"]["total"] = elapsed_ms(started);
    result.report_json            = report.dump();
    return result;
}
} // namespace OrcaMeshLib
