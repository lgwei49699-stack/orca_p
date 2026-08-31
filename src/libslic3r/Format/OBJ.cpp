#include "../libslic3r.h"
#include "../Model.hpp"
#include "../TexturePainting.hpp"
#include "../TriangleMesh.hpp"

#include "OBJ.hpp"
#include "objparser.hpp"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include "nlohmann/json.hpp"
#include <boost/filesystem.hpp>
#include <fstream>
#include <cmath>
#include <cstdio>  
#include <algorithm> 

using namespace nlohmann;
namespace fs   = boost::filesystem;

namespace {

struct AtomicOutputFile
{
    fs::path target;
    fs::path temporary;
    fs::path backup;
    bool     backed_up{false};
    bool     installed{false};
};

void remove_file_if_exists(const fs::path& path)
{
    boost::system::error_code ignored;
    fs::remove(path, ignored);
}

bool is_replaceable_regular_file(const fs::path& path, const char* output_description)
{
    boost::system::error_code status_error;
    const fs::file_status     status = fs::symlink_status(path, status_error);
    if (status.type() == fs::file_not_found)
        return true;
    if (status_error || fs::is_symlink(status) || !fs::is_regular_file(status)) {
        BOOST_LOG_TRIVIAL(error) << "Refusing to replace non-regular " << output_description << " output: " << path.string();
        return false;
    }
    return true;
}

bool make_atomic_output_files(const std::vector<fs::path>& targets, const char* output_description, std::vector<AtomicOutputFile>& outputs)
{
    outputs.clear();
    outputs.reserve(targets.size());
    try {
        for (const fs::path& target : targets) {
            if (!is_replaceable_regular_file(target, output_description))
                return false;

            const fs::path directory = target.parent_path();
            outputs.push_back({target, directory / fs::unique_path(target.filename().string() + ".tmp-%%%%-%%%%"),
                               directory / fs::unique_path(target.filename().string() + ".bak-%%%%-%%%%")});
        }
    } catch (const fs::filesystem_error& exception) {
        BOOST_LOG_TRIVIAL(error) << "Failed creating temporary " << output_description << " paths: " << exception.what();
        outputs.clear();
        return false;
    }
    return true;
}

bool restore_atomic_output_files(std::vector<AtomicOutputFile>& outputs, const char* output_description)
{
    bool restored = true;
    for (auto output = outputs.rbegin(); output != outputs.rend(); ++output) {
        if (output->installed) {
            boost::system::error_code remove_error;
            fs::remove(output->target, remove_error);
            if (remove_error) {
                restored = false;
                BOOST_LOG_TRIVIAL(error) << "Failed removing incomplete " << output_description << " output " << output->target.string()
                                         << " during rollback: " << remove_error.message();
            }
            output->installed = false;
        }

        if (output->backed_up) {
            boost::system::error_code restore_error;
            fs::rename(output->backup, output->target, restore_error);
            if (restore_error) {
                restored = false;
                BOOST_LOG_TRIVIAL(error) << "Failed restoring " << output_description << " output " << output->target.string()
                                         << "; the original remains at " << output->backup.string() << ": " << restore_error.message();
            } else {
                output->backed_up = false;
            }
        }
        remove_file_if_exists(output->temporary);
    }
    return restored;
}

bool commit_atomic_output_files(std::vector<AtomicOutputFile>& outputs, const char* output_description)
{
    // Validate again immediately before moving any existing output. This keeps
    // a symlink or directory substituted while the temporary files were being
    // written from being followed or overwritten.
    for (const AtomicOutputFile& output : outputs) {
        if (!is_replaceable_regular_file(output.target, output_description)) {
            for (const AtomicOutputFile& cleanup : outputs)
                remove_file_if_exists(cleanup.temporary);
            return false;
        }
    }

    for (AtomicOutputFile& output : outputs) {
        boost::system::error_code status_error;
        const fs::file_status     status = fs::symlink_status(output.target, status_error);
        if (status.type() == fs::file_not_found)
            continue;
        if (status_error) {
            BOOST_LOG_TRIVIAL(error) << "Failed inspecting " << output_description << " output " << output.target.string() << ": "
                                     << status_error.message();
            restore_atomic_output_files(outputs, output_description);
            return false;
        }
        boost::system::error_code backup_error;
        fs::rename(output.target, output.backup, backup_error);
        if (backup_error) {
            BOOST_LOG_TRIVIAL(error) << "Failed backing up " << output_description << " output " << output.target.string() << ": "
                                     << backup_error.message();
            restore_atomic_output_files(outputs, output_description);
            return false;
        }
        output.backed_up = true;
    }

    for (AtomicOutputFile& output : outputs) {
        boost::system::error_code install_error;
        fs::rename(output.temporary, output.target, install_error);
        if (install_error) {
            BOOST_LOG_TRIVIAL(error) << "Failed installing " << output_description << " output " << output.target.string() << ": "
                                     << install_error.message();
            restore_atomic_output_files(outputs, output_description);
            return false;
        }
        output.installed = true;
    }

    for (AtomicOutputFile& output : outputs) {
        if (output.backed_up) {
            boost::system::error_code remove_error;
            fs::remove(output.backup, remove_error);
            if (remove_error)
                BOOST_LOG_TRIVIAL(warning) << "Failed removing obsolete " << output_description << " backup " << output.backup.string()
                                           << ": " << remove_error.message();
            else
                output.backed_up = false;
        }
    }
    return true;
}

bool indexed_mesh_is_exportable(const indexed_triangle_set& mesh)
{
    if (mesh.vertices.empty() || mesh.indices.empty())
        return false;
    for (const stl_vertex& vertex : mesh.vertices)
        if (!vertex.allFinite())
            return false;
    for (const stl_triangle_vertex_indices& face : mesh.indices) {
        for (size_t corner = 0; corner < 3; ++corner)
            if (face[corner] < 0 || size_t(face[corner]) >= mesh.vertices.size())
                return false;
    }
    return true;
}

} // namespace

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

//Translation
#include "I18N.hpp"
#define _L(s) Slic3r::I18N::translate(s)

namespace Slic3r {
    
bool load_general_extruder_config(const std::string& config_path, GeneralExtruderConfig& config)
{
    try {
        if (!fs::exists(config_path)) {
            BOOST_LOG_TRIVIAL(warning) << "General extruder config not found: " << config_path << ", use default config";
            config.default_extruder_id   = 0;
            config.default_filament_name = "Generic PLA @System";
            config.color_match_tolerance = 0.01f;
            return true;
        }

        std::ifstream ifs(config_path);
        if (!ifs.is_open()) {
            BOOST_LOG_TRIVIAL(error) << "Failed to open extruder config: " << config_path;
            config.default_extruder_id   = 0;
            config.default_filament_name = "Generic PLA @System";
            config.color_match_tolerance = 0.01f;
            return true;
        }

        json root;
        ifs >> root;
        ifs.close();

        config.default_extruder_id   = static_cast<unsigned char>(root["default_extruder_id"].get<int>());
        config.default_filament_name = root["default_filament_name"].get<std::string>();
        if (root.contains("color_match_tolerance")) {
            config.color_match_tolerance = root["color_match_tolerance"].get<float>();
        }

        config.extruder_mapping.clear();
        if (root.contains("extruder_mapping") && root["extruder_mapping"].is_array()) {
            for (const auto& item : root["extruder_mapping"]) {
                RGBA ref_color;
                ref_color[0] = item["reference_color"]["r"].get<float>();
                ref_color[1] = item["reference_color"]["g"].get<float>();
                ref_color[2] = item["reference_color"]["b"].get<float>();
                ref_color[3] = item["reference_color"]["a"].get<float>();

                unsigned char extruder_id = static_cast<unsigned char>(item["extruder_id"].get<int>());
                config.extruder_mapping.emplace_back(ref_color, extruder_id);
            }
        }

        BOOST_LOG_TRIVIAL(info) << "Loaded general extruder config: " << config_path;
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Parse extruder config failed: " << e.what() << ", use default config";
        config.default_extruder_id   = 0;
        config.default_filament_name = "Generic PLA @System";
        config.color_match_tolerance = 0.01f;
        return true;
    }
}

std::string rgba_to_html(const RGBA& rgba)
{
    int r = static_cast<int>(std::clamp(rgba[0], 0.0f, 1.0f) * 255);
    int g = static_cast<int>(std::clamp(rgba[1], 0.0f, 1.0f) * 255);
    int b = static_cast<int>(std::clamp(rgba[2], 0.0f, 1.0f) * 255);

    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return std::string(buf);
}

std::map<unsigned char, std::string> get_extruder_color_map(const std::vector<RGBA>&          face_colors,
                                                                   const std::vector<unsigned char>& face_filament_ids)
{
    std::map<unsigned char, std::string> color_map;

    for (size_t i = 0; i < face_filament_ids.size(); ++i) {
        unsigned char extruder_id = face_filament_ids[i];
        if (color_map.find(extruder_id) == color_map.end()) {
            color_map[extruder_id] = rgba_to_html(face_colors[i]);
        }
    }

    return color_map;
}

bool match_color(const RGBA& target, const RGBA& ref, float tolerance)
{
    return std::abs(target[0] - ref[0]) < tolerance && std::abs(target[1] - ref[1]) < tolerance &&
           std::abs(target[2] - ref[2]) < tolerance && std::abs(target[3] - ref[3]) < tolerance;
}

void match_face_filament_ids(const std::vector<RGBA>&     face_colors,
                             const GeneralExtruderConfig& config, 
                             std::vector<unsigned char>&  face_filament_ids,
                             unsigned char&               first_extruder_id)
{
    first_extruder_id = config.default_extruder_id;
    face_filament_ids.resize(face_colors.size(), first_extruder_id); 

    for (size_t i = 0; i < face_colors.size(); ++i) {
        const RGBA& target_color = face_colors[i];
        for (const auto& [ref_color, extruder_id] : config.extruder_mapping) {
            if (match_color(target_color, ref_color, config.color_match_tolerance)) {
                face_filament_ids[i] = extruder_id;
                //BOOST_LOG_TRIVIAL(debug) << "Face " << i << " color matched extruder ID: " << (int) extruder_id;
                break;
            }
        }
    }

    BOOST_LOG_TRIVIAL(info) << "Matched " << face_filament_ids.size() << " face extruder IDs (default: " << (int) first_extruder_id << ")";
}

//void match_vertex_filament_ids(const std::vector<RGBA>&     vertex_colors,
//                               const GeneralExtruderConfig& config,
//                               std::vector<unsigned char>&  vertex_filament_ids,
//                               unsigned char&               first_extruder_id)
//{
//    first_extruder_id = config.default_extruder_id;
//    vertex_filament_ids.resize(vertex_colors.size(), first_extruder_id);
//    for (size_t i = 0; i < vertex_colors.size(); ++i) {
//        const RGBA& target_color = vertex_colors[i];
//        for (const auto& [ref_color, extruder_id] : config.extruder_mapping) {
//            if (match_color(target_color, ref_color, config.color_match_tolerance)) {
//                vertex_filament_ids[i] = extruder_id;
//                break;
//            }
//        }
//    }
//
//    BOOST_LOG_TRIVIAL(info) << "Matched " << vertex_filament_ids.size() << " vertex extruder IDs (default: " << (int) first_extruder_id
//                            << ")";
//}

std::string rgba_to_string(const RGBA& color, int precision)
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(precision) << color[0] << "," << color[1] << "," << color[2] << "," << color[3];
    return ss.str();
}

bool load_obj_material_names(const char *path, std::vector<std::string> &material_names, std::string &message)
{
    ObjParser::ObjData data;
    material_names.clear();
    if (!ObjParser::objparse(path, data)) {
        BOOST_LOG_TRIVIAL(error) << "load_obj_material_names: failed to parse " << path;
        message = _L("load_obj: failed to parse");
        return false;
    }

    if (data.usemtls.size() == 1) {
        material_names.emplace_back(data.usemtls.front().name);
        return true;
    }

    std::unordered_map<std::string, bool> seen;
    for (const ObjParser::ObjUseMtl &usemtl : data.usemtls) {
        if (usemtl.face_end < usemtl.face_start)
            continue;
        if (seen.emplace(usemtl.name, true).second)
            material_names.emplace_back(usemtl.name);
    }
    return true;
}

bool load_obj_material_libraries(const char* path, std::vector<std::string>& library_paths, std::string& message)
{
    library_paths.clear();
    if (path == nullptr || *path == '\0') {
        message = _L("load_obj: failed to parse");
        return false;
    }

    ObjParser::ObjData data;
    if (!ObjParser::objparse(path, data)) {
        BOOST_LOG_TRIVIAL(error) << "load_obj_material_libraries: failed to parse " << path;
        message = _L("load_obj: failed to parse");
        return false;
    }

    const fs::path obj_path(path);
    for (const std::string& library_name : data.mtllibs) {
        if (library_name.empty())
            continue;

        fs::path library_path(library_name);
        // The OBJ specification resolves relative mtllib references from the
        // OBJ file, not from the process working directory. Looking in the
        // working directory first may protect or load an unrelated same-named
        // file while allowing the real sidecar next to the OBJ to be replaced.
        if (!library_path.is_absolute())
            library_path = obj_path.parent_path() / library_path;
        library_paths.push_back(library_path.lexically_normal().string());
    }
    return true;
}

bool load_obj(const char* path, TriangleMesh* meshptr, ObjInfo& obj_info, std::string& message)
{
    if (meshptr == nullptr)
        return false;
    // Parse the OBJ file.
    ObjParser::ObjData data;
    ObjParser::MtlData mtl_data;
    if (! ObjParser::objparse(path, data)) {
        BOOST_LOG_TRIVIAL(error) << "load_obj: failed to parse " << path;
        message = _L("load_obj: failed to parse");
        return false;
    }
    bool exist_mtl = false;
    if (data.mtllibs.size() > 0) { // read mtl
        const fs::path obj_path(path);
        for (const std::string& mtl_name : data.mtllibs) {
            if (mtl_name.empty()) {
                continue;
            }
            exist_mtl = true;
            fs::path mtl_path(mtl_name);
            if (!mtl_path.is_absolute())
                mtl_path = obj_path.parent_path() / mtl_path;
            mtl_path = mtl_path.lexically_normal();
            if (fs::exists(mtl_path)) {
                const std::string mtl_path_string = mtl_path.string();
                if (!ObjParser::mtlparse(mtl_path_string.c_str(), mtl_data)) {
                    BOOST_LOG_TRIVIAL(error) << "load_obj:load_mtl: failed to parse " << mtl_path_string;
                    message = _L("load mtl in obj: failed to parse");
                    return false;
                }
            } else {
                BOOST_LOG_TRIVIAL(error) << "load_obj: failed to load mtl_path:" << mtl_path.string();
            }
        }
    }
    // Count the faces and verify, that all faces are triangular.
    size_t num_faces = 0;
    size_t num_quads = 0;
    for (size_t i = 0; i < data.vertices.size(); ++ i) {
        // Find the end of face.
        size_t j = i;
        for (; j < data.vertices.size() && data.vertices[j].coordIdx != -1; ++j)
            ;
        if (size_t num_face_vertices = j - i; num_face_vertices > 0) {
            if (num_face_vertices > 4) {
                // Non-triangular and non-quad faces are not supported as of now.
                BOOST_LOG_TRIVIAL(error) << "load_obj: failed to parse " << path
                                         << ". The file contains polygons with more than 4 vertices.";
                message = _L("The file contains polygons with more than 4 vertices.");
                return false;
            } else if (num_face_vertices < 3) {
                // Non-triangular and non-quad faces are not supported as of now.
                BOOST_LOG_TRIVIAL(error) << "load_obj: failed to parse " << path
                                         << ". The file contains polygons with less than 2 vertices.";
                message = _L("The file contains polygons with less than 2 vertices.");
                return false;
            }
            if (num_face_vertices == 4)
                ++ num_quads;
            ++ num_faces;
            i = j;
        }
    }
    // Convert ObjData into indexed triangle set.
    indexed_triangle_set its;
    size_t               num_vertices = data.coordinates.size() / OBJ_VERTEX_LENGTH;
    its.vertices.reserve(num_vertices);
    its.indices.reserve(num_faces + num_quads);
    const bool has_material_assignments = !data.usemtls.empty();
    if (exist_mtl) {
        obj_info.is_single_mtl = data.usemtls.size() == 1 && mtl_data.new_mtl_unmap.size() == 1;
        obj_info.face_colors.reserve(num_faces + num_quads);
    }
    if (has_material_assignments)
        obj_info.face_material_ids.reserve(num_faces + num_quads);
    bool has_color = data.has_vertex_color;
    for (size_t i = 0; i < num_vertices; ++ i) {
        size_t j = i * OBJ_VERTEX_LENGTH;
        its.vertices.emplace_back(data.coordinates[j], data.coordinates[j + 1], data.coordinates[j + 2]);
        if (data.has_vertex_color) {
            RGBA color{std::clamp(data.coordinates[j + 3], 0.f, 1.f), std::clamp(data.coordinates[j + 4], 0.f, 1.f),
                       std::clamp(data.coordinates[j + 5], 0.f, 1.f), std::clamp(data.coordinates[j + 6], 0.f, 1.f)};
            obj_info.vertex_colors.emplace_back(color);
        }
    }
    int indices[ONE_FACE_SIZE];
    int uvs[ONE_FACE_SIZE];
    std::unordered_map<std::string, unsigned int> material_name_to_id;
    for (size_t i = 0; i < data.vertices.size();)
        if (data.vertices[i].coordIdx == -1)
            ++ i;
        else {
            int cnt = 0;
            while (i < data.vertices.size())
                if (const ObjParser::ObjVertex &vertex = data.vertices[i ++]; vertex.coordIdx == -1) {
                    break;
                } else {
                    assert(cnt < OBJ_VERTEX_LENGTH);
                    if (vertex.coordIdx < 0 || vertex.coordIdx >= int(its.vertices.size())) {
                        BOOST_LOG_TRIVIAL(error) << "load_obj: failed to parse " << path << ". The file contains invalid vertex index.";
                        message = _L("The file contains invalid vertex index.");
                        return false;
                    }
                    indices[cnt] = vertex.coordIdx;
                    uvs[cnt]     = vertex.textureCoordIdx;
                    cnt++;
                }
            if (cnt) {
                assert(cnt == 3 || cnt == 4);
                // Insert one or two faces (triangulate a quad).
                its.indices.emplace_back(indices[0], indices[1], indices[2]);
                int  face_index =its.indices.size() - 1;
                RGBA face_color;
                auto set_face_color = [&uvs, &data, &mtl_data, &obj_info, &face_color](int face_index, const std::string mtl_name) {
                    if (mtl_data.new_mtl_unmap.find(mtl_name) != mtl_data.new_mtl_unmap.end()) {
                        bool is_merge_ka_kd = true;
                        for (size_t n = 0; n < 3; n++) {
                            if (float(mtl_data.new_mtl_unmap[mtl_name]->Ka[n] + mtl_data.new_mtl_unmap[mtl_name]->Kd[n]) > 1.0) {
                                is_merge_ka_kd=false;
                                break;
                            }
                        }
                        for (size_t n = 0; n < 3; n++) {
                            if (is_merge_ka_kd) {
                                face_color[n] = std::clamp(float(mtl_data.new_mtl_unmap[mtl_name]->Ka[n] +
                                                                 mtl_data.new_mtl_unmap[mtl_name]->Kd[n]),
                                                           0.f, 1.f);
                            } else {
                                face_color[n] = std::clamp(float(mtl_data.new_mtl_unmap[mtl_name]->Kd[n]), 0.f, 1.f);
                            }
                        }
                        face_color[3] = mtl_data.new_mtl_unmap[mtl_name]->Tr; // alpha
                        if (mtl_data.new_mtl_unmap[mtl_name]->map_Kd.size() > 0) {
                            auto png_name       = mtl_data.new_mtl_unmap[mtl_name]->map_Kd;
                            obj_info.has_uv_png = true;
                            if (obj_info.pngs.find(png_name) == obj_info.pngs.end()) {
                                obj_info.pngs[png_name] = false;
                            }
                            obj_info.uv_map_pngs[face_index] = png_name;
                        }
                        if (data.textureCoordinates.size() > 0) {
                            Vec2f                uv0(data.textureCoordinates[uvs[0] * 2], data.textureCoordinates[uvs[0] * 2 + 1]);
                            Vec2f                uv1(data.textureCoordinates[uvs[1] * 2], data.textureCoordinates[uvs[1] * 2 + 1]);
                            Vec2f                uv2(data.textureCoordinates[uvs[2] * 2], data.textureCoordinates[uvs[2] * 2 + 1]);
                            std::array<Vec2f, 3> uv_array{uv0, uv1, uv2};
                            obj_info.uvs.emplace_back(uv_array);
                        }
                        obj_info.face_colors.emplace_back(face_color);
                    }
                };
                auto set_face_color_by_mtl = [&data, &set_face_color, &obj_info, &material_name_to_id](int face_index) {
                    const auto record_material = [&obj_info, &material_name_to_id](const std::string &name) {
                        auto [it, inserted] = material_name_to_id.emplace(name, static_cast<unsigned int>(obj_info.material_names.size()));
                        if (inserted)
                            obj_info.material_names.emplace_back(name);
                        obj_info.face_material_ids.emplace_back(it->second);
                    };
                    if (data.usemtls.size() == 1) {
                        record_material(data.usemtls[0].name);
                        set_face_color(face_index, data.usemtls[0].name);
                    } else {
                        for (size_t k = 0; k < data.usemtls.size(); k++) {
                            auto mtl = data.usemtls[k];
                            if (face_index >= mtl.face_start && face_index <= mtl.face_end) {
                                record_material(data.usemtls[k].name);
                                set_face_color(face_index, data.usemtls[k].name);
                                return;
                            }
                        }
                        obj_info.face_material_ids.emplace_back(std::numeric_limits<unsigned int>::max());
                    }
                };
                if (has_material_assignments) {
                    set_face_color_by_mtl(face_index);
                }
                if (cnt == 4) {
                    its.indices.emplace_back(indices[0], indices[2], indices[3]);
                    int face_index = its.indices.size() - 1;
                    if (has_material_assignments) {
                        set_face_color_by_mtl(face_index);
                    }
                }
            }
        }

    *meshptr = TriangleMesh(std::move(its));
    if (meshptr->empty()) {
        BOOST_LOG_TRIVIAL(error) << "load_obj: This OBJ file couldn't be read because it's empty. " << path;
        message = _L("This OBJ file couldn't be read because it's empty.");
        return false;
    }
    if (meshptr->volume() < 0)
        meshptr->flip_triangles();
    return true;
}

bool load_obj(const char *path, Model *model, ObjInfo& obj_info, std::string &message, const char *object_name_in)
{
    TriangleMesh mesh;

    bool ret = load_obj(path, &mesh, obj_info, message);

    if (ret) {
        std::string  object_name;
        if (object_name_in == nullptr) {
            const char *last_slash = strrchr(path, DIR_SEPARATOR);
            object_name.assign((last_slash == nullptr) ? path : last_slash + 1);
        } else
           object_name.assign(object_name_in);
        model->add_object(object_name.c_str(), path, std::move(mesh));
    }

    return ret;
}

bool store_obj(const char *path, TriangleMesh *mesh)
{
    if (path == nullptr || *path == '\0' || mesh == nullptr || !indexed_mesh_is_exportable(mesh->its))
        return false;

    std::vector<AtomicOutputFile> outputs;
    if (!make_atomic_output_files({fs::path(path)}, "OBJ", outputs))
        return false;

    if (!mesh->write_obj_file(outputs.front().temporary.string().c_str())) {
        remove_file_if_exists(outputs.front().temporary);
        return false;
    }
    return commit_atomic_output_files(outputs, "OBJ");
}

bool store_obj(const char *path, ModelObject *model_object)
{
    TriangleMesh mesh = model_object->mesh();
    return store_obj(path, &mesh);
}

bool store_obj(const char *path, Model *model)
{
    TriangleMesh mesh = model->mesh();
    return store_obj(path, &mesh);
}

bool store_multicolor_obj(const char *path, const Model &model, const std::vector<RGBA> &filament_colors)
{
    if (path == nullptr || *path == '\0' || filament_colors.empty())
        return false;

    fs::path obj_path(path);
    if (!boost::algorithm::iends_with(obj_path.extension().string(), ".obj"))
        obj_path.replace_extension(".obj");
    fs::path mtl_path = obj_path;
    mtl_path.replace_extension(".mtl");

    struct ExportGeometry
    {
        indexed_triangle_set mesh;
        int                  filament_id;
    };
    struct ExportGroup
    {
        size_t      geometry_index;
        Transform3d transform;
    };

    std::vector<ExportGeometry> geometries;
    std::vector<ExportGroup>    groups;
    std::set<int>               used_filaments;
    size_t                      total_faces         = 0;
    bool                        invalid_export_data = false;

    const auto append_geometry = [&](indexed_triangle_set mesh, int filament_id) -> std::optional<size_t> {
        if (mesh.indices.empty())
            return std::nullopt;
        if (!indexed_mesh_is_exportable(mesh)) {
            BOOST_LOG_TRIVIAL(error) << "OBJ export encountered invalid mesh geometry";
            invalid_export_data = true;
            return std::nullopt;
        }
        if (filament_id < 1 || filament_id > static_cast<int>(filament_colors.size())) {
            BOOST_LOG_TRIVIAL(error) << "OBJ face references filament " << filament_id << ", but only " << filament_colors.size()
                                     << " colors are available";
            invalid_export_data = true;
            return std::nullopt;
        }
        const size_t index = geometries.size();
        geometries.push_back({std::move(mesh), filament_id});
        used_filaments.insert(filament_id);
        return index;
    };

    // Resolve and validate the complete export before creating either output
    // file. Every ModelInstance is materialized into world coordinates.
    for (const ModelObject* object : model.objects) {
        if (object == nullptr)
            continue;
        for (const ModelVolume* volume : object->volumes) {
            if (volume == nullptr || !volume->is_model_part())
                continue;

            const int                                         base_filament = std::max(volume->extruder_id(), 1);
            std::vector<std::pair<int, indexed_triangle_set>> volume_groups;
            if (volume->mmu_segmentation_facets.empty()) {
                volume_groups.emplace_back(base_filament, volume->mesh().its);
            } else {
                std::vector<indexed_triangle_set> facets_per_filament;
                volume->mmu_segmentation_facets.get_facets(*volume, facets_per_filament);
                for (size_t state = 0; state < facets_per_filament.size(); ++state) {
                    const int filament_id = state == 0 ? base_filament : static_cast<int>(state);
                    volume_groups.emplace_back(filament_id, std::move(facets_per_filament[state]));
                }
            }

            for (auto& volume_group : volume_groups) {
                const std::optional<size_t> geometry_index = append_geometry(std::move(volume_group.second), volume_group.first);
                if (invalid_export_data)
                    return false;
                if (!geometry_index)
                    continue;

                const size_t face_count = geometries[*geometry_index].mesh.indices.size();
                if (object->instances.empty()) {
                    groups.push_back({*geometry_index, volume->get_matrix()});
                    total_faces += face_count;
                } else {
                    for (const ModelInstance* instance : object->instances) {
                        if (instance == nullptr)
                            continue;
                        groups.push_back({*geometry_index, instance->get_matrix() * volume->get_matrix()});
                        total_faces += face_count;
                    }
                }
            }
        }
    }

    if (groups.empty() || total_faces == 0) {
        BOOST_LOG_TRIVIAL(error) << "No printable faces were found for multicolor OBJ export";
        return false;
    }

    // TriangleMesh::transform materializes the double-precision model transform
    // into float geometry. A finite source mesh can therefore still overflow.
    // Validate every instance before creating either sidecar output.
    for (const ExportGroup& group : groups) {
        TriangleMesh transformed_mesh(geometries[group.geometry_index].mesh);
        transformed_mesh.transform(group.transform, true);
        if (!indexed_mesh_is_exportable(transformed_mesh.its)) {
            BOOST_LOG_TRIVIAL(error) << "OBJ export transform produced invalid mesh geometry";
            return false;
        }
    }

    boost::system::error_code error_code;
    if (!obj_path.parent_path().empty()) {
        fs::create_directories(obj_path.parent_path(), error_code);
        if (error_code) {
            BOOST_LOG_TRIVIAL(error) << "Failed creating multicolor OBJ output directory: " << error_code.message();
            return false;
        }
    }

    std::vector<AtomicOutputFile> outputs;
    if (!make_atomic_output_files({obj_path, mtl_path}, "multicolor OBJ/MTL", outputs))
        return false;

    boost::nowide::ofstream obj_stream(outputs[0].temporary.string(), std::ios::out | std::ios::trunc);
    boost::nowide::ofstream mtl_stream(outputs[1].temporary.string(), std::ios::out | std::ios::trunc);
    if (!obj_stream.is_open() || !mtl_stream.is_open()) {
        obj_stream.close();
        mtl_stream.close();
        remove_file_if_exists(outputs[0].temporary);
        remove_file_if_exists(outputs[1].temporary);
        BOOST_LOG_TRIVIAL(error) << "Failed opening temporary multicolor OBJ/MTL output";
        return false;
    }

    obj_stream << "# OrcaSlicer multicolor OBJ\n";
    obj_stream << "# semantic_source: filament_and_mmu_assignments; original_mtl_preserved: false\n";
    obj_stream << "mtllib " << mtl_path.filename().generic_string() << "\n\n";
    obj_stream << std::setprecision(std::numeric_limits<float>::max_digits10);
    mtl_stream << std::setprecision(6);

    size_t        vertex_offset = 1;
    size_t        group_index   = 0;
    for (const ExportGroup& group : groups) {
        const ExportGeometry& geometry = geometries[group.geometry_index];
        TriangleMesh          mesh(geometry.mesh);
        mesh.transform(group.transform, true);

        obj_stream << "g color_group_" << group_index++ << "\n";
        for (const Vec3f &vertex : mesh.its.vertices)
            obj_stream << "v " << vertex.x() << " " << vertex.y() << " " << vertex.z() << "\n";
        obj_stream << "usemtl filament_" << geometry.filament_id << "\n";
        for (const Vec3i32& face : mesh.its.indices)
            obj_stream << "f " << vertex_offset + static_cast<size_t>(face[0]) << " " << vertex_offset + static_cast<size_t>(face[1]) << " "
                       << vertex_offset + static_cast<size_t>(face[2]) << "\n";
        obj_stream << "\n";
        vertex_offset += mesh.its.vertices.size();
    }

    for (int filament_id : used_filaments) {
        const RGBA &color = filament_colors[static_cast<size_t>(filament_id - 1)];
        mtl_stream << "newmtl filament_" << filament_id << "\n";
        mtl_stream << "Ka 0 0 0\n";
        mtl_stream << "Kd " << std::clamp(color[0], 0.f, 1.f) << " " << std::clamp(color[1], 0.f, 1.f) << " "
                   << std::clamp(color[2], 0.f, 1.f) << "\n";
        mtl_stream << "d 1\nillum 1\n\n";
    }

    obj_stream.flush();
    mtl_stream.flush();
    bool streams_ok = obj_stream.good() && mtl_stream.good();
    obj_stream.close();
    mtl_stream.close();
    streams_ok = streams_ok && !obj_stream.fail() && !mtl_stream.fail();
    if (!streams_ok) {
        remove_file_if_exists(outputs[0].temporary);
        remove_file_if_exists(outputs[1].temporary);
        BOOST_LOG_TRIVIAL(error) << "Failed writing temporary multicolor OBJ/MTL files";
        return false;
    }

    if (!commit_atomic_output_files(outputs, "multicolor OBJ/MTL")) {
        BOOST_LOG_TRIVIAL(error) << "Failed committing multicolor OBJ/MTL output pair";
        return false;
    }

    BOOST_LOG_TRIVIAL(info) << "Exported multicolor OBJ " << obj_path.string() << ", MTL " << mtl_path.string() << ", faces " << total_faces
                            << ", materials " << used_filaments.size();
    return true;
}

bool store_painted_mesh_obj(const char* path, const PaintedMesh& painted_mesh, const std::vector<RGBA>& filament_colors)
{
    if (path == nullptr || *path == '\0' || painted_mesh.vertices.empty() || painted_mesh.indices.empty() ||
        painted_mesh.face_colors.size() != painted_mesh.indices.size() || painted_mesh.cluster_colors.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Cannot export an empty or inconsistent painted mesh";
        return false;
    }
    for (const std::array<float, 3>& vertex : painted_mesh.vertices)
        if (!std::isfinite(vertex[0]) || !std::isfinite(vertex[1]) || !std::isfinite(vertex[2])) {
            BOOST_LOG_TRIVIAL(error) << "Painted mesh contains a non-finite vertex";
            return false;
        }
    for (const std::array<int, 3>& face : painted_mesh.indices)
        for (size_t corner = 0; corner < 3; ++corner)
            if (face[corner] < 0 || size_t(face[corner]) >= painted_mesh.vertices.size()) {
                BOOST_LOG_TRIVIAL(error) << "Painted mesh contains an invalid vertex index";
        return false;
    }
    if (filament_colors.size() < painted_mesh.cluster_colors.size()) {
        BOOST_LOG_TRIVIAL(error) << "Painted mesh has " << painted_mesh.cluster_colors.size() << " colors, but only "
                                 << filament_colors.size() << " filament colors are available";
        return false;
    }

    fs::path obj_path(path);
    if (!boost::algorithm::iends_with(obj_path.extension().string(), ".obj"))
        obj_path.replace_extension(".obj");
    fs::path mtl_path = obj_path;
    mtl_path.replace_extension(".mtl");

    try {
        if (!obj_path.parent_path().empty())
            fs::create_directories(obj_path.parent_path());
    } catch (const fs::filesystem_error& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed creating painted OBJ output directory: " << e.what();
        return false;
    }

    std::vector<AtomicOutputFile> outputs;
    if (!make_atomic_output_files({obj_path, mtl_path}, "painted OBJ/MTL", outputs))
        return false;

    std::map<std::array<std::size_t, 3>, std::size_t> material_by_color;
    for (std::size_t index = 0; index < painted_mesh.cluster_colors.size(); ++index)
        material_by_color.emplace(painted_mesh.cluster_colors[index], index);

    std::vector<std::vector<std::size_t>> faces_by_material(painted_mesh.cluster_colors.size());

    std::vector<std::array<float, 3>> export_vertices = painted_mesh.vertices;
    std::vector<std::array<int, 3>>   export_indices  = painted_mesh.indices;

    std::unordered_map<std::uint64_t, std::size_t> source_edge_use_counts;
    source_edge_use_counts.reserve(export_indices.size() * 3);

    auto edge_key = [](int first, int second) {
        const std::uint32_t low  = static_cast<std::uint32_t>(std::min(first, second));
        const std::uint32_t high = static_cast<std::uint32_t>(std::max(first, second));
        return (static_cast<std::uint64_t>(low) << 32) | high;
    };
    auto add_source_edge = [&source_edge_use_counts, &edge_key](int first, int second) {
        ++source_edge_use_counts[edge_key(first, second)];
    };

    for (std::size_t face_index = 0; face_index < painted_mesh.indices.size(); ++face_index) {
        const std::array<int, 3>& face = painted_mesh.indices[face_index];
        if (face[0] < 0 || face[1] < 0 || face[2] < 0 || static_cast<std::size_t>(face[0]) >= painted_mesh.vertices.size() ||
            static_cast<std::size_t>(face[1]) >= painted_mesh.vertices.size() ||
            static_cast<std::size_t>(face[2]) >= painted_mesh.vertices.size()) {
            BOOST_LOG_TRIVIAL(error) << "Painted mesh face " << face_index << " contains an invalid vertex index";
            return false;
        }

        auto material = material_by_color.find(painted_mesh.face_colors[face_index]);
        if (material == material_by_color.end()) {
            BOOST_LOG_TRIVIAL(error) << "Painted mesh face " << face_index << " references an unknown cluster color";
            return false;
        }
        faces_by_material[material->second].push_back(face_index);

        add_source_edge(face[0], face[1]);
        add_source_edge(face[1], face[2]);
        add_source_edge(face[2], face[0]);
    }

    // Textured meshes commonly duplicate vertices along UV or material seams. Stitch only an
    // unambiguous pair of boundary edges whose endpoints match exactly in reverse direction.
    // This preserves intentionally separate or merely nearby geometry and does not add faces.
    using Position        = std::array<float, 3>;
    using PositionEdgeKey = std::pair<Position, Position>;
    struct DirectedBoundaryEdge
    {
        int from;
        int to;
    };

    std::map<PositionEdgeKey, std::vector<DirectedBoundaryEdge>> boundary_edges_by_position;
    auto collect_boundary_edge = [&](int from, int to) {
        auto edge = source_edge_use_counts.find(edge_key(from, to));
        if (edge == source_edge_use_counts.end() || edge->second != 1)
            return;

        const Position& from_position = export_vertices[from];
        const Position& to_position   = export_vertices[to];
        const PositionEdgeKey position_key = from_position < to_position ? PositionEdgeKey{from_position, to_position} :
                                                                            PositionEdgeKey{to_position, from_position};
        boundary_edges_by_position[position_key].push_back({from, to});
    };
    for (const std::array<int, 3>& face : export_indices) {
        collect_boundary_edge(face[0], face[1]);
        collect_boundary_edge(face[1], face[2]);
        collect_boundary_edge(face[2], face[0]);
    }

    std::vector<int> vertex_parent(export_vertices.size());
    std::iota(vertex_parent.begin(), vertex_parent.end(), 0);
    auto find_root = [&vertex_parent](int vertex) {
        int root = vertex;
        while (vertex_parent[root] != root)
            root = vertex_parent[root];
        while (vertex_parent[vertex] != vertex) {
            const int next = vertex_parent[vertex];
            vertex_parent[vertex] = root;
            vertex = next;
        }
        return root;
    };
    auto unite_vertices = [&vertex_parent, &find_root](int first, int second) {
        const int first_root  = find_root(first);
        const int second_root = find_root(second);
        if (first_root != second_root)
            vertex_parent[second_root] = first_root;
    };

    std::size_t stitched_edge_pairs = 0;
    std::size_t ambiguous_edge_sets = 0;
    for (const auto& item : boundary_edges_by_position) {
        const std::vector<DirectedBoundaryEdge>& edges = item.second;
        if (edges.size() != 2) {
            if (edges.size() > 1)
                ++ambiguous_edge_sets;
            continue;
        }

        const DirectedBoundaryEdge& first  = edges[0];
        const DirectedBoundaryEdge& second = edges[1];
        if (export_vertices[first.from] != export_vertices[second.to] || export_vertices[first.to] != export_vertices[second.from]) {
            ++ambiguous_edge_sets;
            continue;
        }

        unite_vertices(first.from, second.to);
        unite_vertices(first.to, second.from);
        ++stitched_edge_pairs;
    }

    if (stitched_edge_pairs > 0) {
        std::vector<int> root_to_compact(export_vertices.size(), -1);
        std::vector<Position> compact_vertices;
        compact_vertices.reserve(export_vertices.size());
        for (std::size_t vertex = 0; vertex < export_vertices.size(); ++vertex) {
            const int root = find_root(static_cast<int>(vertex));
            if (root_to_compact[root] < 0) {
                root_to_compact[root] = static_cast<int>(compact_vertices.size());
                compact_vertices.push_back(export_vertices[root]);
            }
        }

        bool collapsed_face = false;
        for (std::array<int, 3>& face : export_indices) {
            for (int& vertex : face)
                vertex = root_to_compact[find_root(vertex)];
            if (face[0] == face[1] || face[1] == face[2] || face[2] == face[0])
                collapsed_face = true;
        }

        if (collapsed_face) {
            BOOST_LOG_TRIVIAL(warning) << "Exact seam stitching would collapse a triangle; exporting the original painted mesh";
            export_vertices = painted_mesh.vertices;
            export_indices  = painted_mesh.indices;
            stitched_edge_pairs = 0;
        } else {
            export_vertices = std::move(compact_vertices);
        }
    }

    std::unordered_map<std::uint64_t, std::size_t> edge_use_counts;
    edge_use_counts.reserve(export_indices.size() * 3);
    for (const std::array<int, 3>& face : export_indices) {
        ++edge_use_counts[edge_key(face[0], face[1])];
        ++edge_use_counts[edge_key(face[1], face[2])];
        ++edge_use_counts[edge_key(face[2], face[0])];
    }

    std::size_t boundary_edges    = 0;
    std::size_t nonmanifold_edges = 0;
    for (const auto& edge : edge_use_counts) {
        if (edge.second == 1)
            ++boundary_edges;
        else if (edge.second > 2)
            ++nonmanifold_edges;
    }
    BOOST_LOG_TRIVIAL(info) << "Painted mesh topology after exact seam stitching: vertices " << export_vertices.size() << ", faces "
                            << export_indices.size() << ", stitched edge pairs " << stitched_edge_pairs << ", ambiguous edge sets "
                            << ambiguous_edge_sets << ", boundary edges " << boundary_edges << ", nonmanifold edges " << nonmanifold_edges;
    if (nonmanifold_edges > 0) {
        BOOST_LOG_TRIVIAL(warning) << "Painted mesh still contains " << nonmanifold_edges
                                   << " nonmanifold edge(s) after best-effort exact seam stitching; continuing OBJ export";
    }

    boost::nowide::ofstream obj_stream(outputs[0].temporary.string(), std::ios::out | std::ios::trunc);
    boost::nowide::ofstream mtl_stream(outputs[1].temporary.string(), std::ios::out | std::ios::trunc);
    if (!obj_stream.is_open() || !mtl_stream.is_open()) {
        obj_stream.close();
        mtl_stream.close();
        remove_file_if_exists(outputs[0].temporary);
        remove_file_if_exists(outputs[1].temporary);
        BOOST_LOG_TRIVIAL(error) << "Failed opening temporary painted OBJ/MTL output: " << obj_path.string();
        return false;
    }

    obj_stream << "# OrcaSlicer painted mesh OBJ\n";
    obj_stream << "mtllib " << mtl_path.filename().generic_string() << "\n\n";
    obj_stream << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (const std::array<float, 3>& vertex : export_vertices)
        obj_stream << "v " << vertex[0] << " " << vertex[1] << " " << vertex[2] << "\n";
    obj_stream << "\n";

    std::size_t           written_faces = 0;
    std::set<std::size_t> used_materials;
    for (std::size_t material_index = 0; material_index < faces_by_material.size(); ++material_index) {
        const std::vector<std::size_t>& face_indices = faces_by_material[material_index];
        if (face_indices.empty())
            continue;

        const std::size_t filament_id = material_index + 1;
        obj_stream << "g color_group_" << material_index << "\n";
        obj_stream << "usemtl filament_" << filament_id << "\n";
        for (std::size_t face_index : face_indices) {
            const std::array<int, 3>& face = export_indices[face_index];
            obj_stream << "f " << face[0] + 1 << " " << face[1] + 1 << " " << face[2] + 1 << "\n";
        }
        obj_stream << "\n";
        written_faces += face_indices.size();
        used_materials.insert(material_index);
    }

    mtl_stream << std::setprecision(6);
    for (std::size_t material_index : used_materials) {
        const std::size_t filament_id = material_index + 1;
        const RGBA&       color       = filament_colors[material_index];
        mtl_stream << "newmtl filament_" << filament_id << "\n";
        mtl_stream << "Ka 0 0 0\n";
        mtl_stream << "Kd " << std::clamp(color[0], 0.f, 1.f) << " " << std::clamp(color[1], 0.f, 1.f) << " "
                   << std::clamp(color[2], 0.f, 1.f) << "\n";
        mtl_stream << "d 1\nillum 1\n\n";
    }

    obj_stream.flush();
    mtl_stream.flush();
    bool success = obj_stream.good() && mtl_stream.good() && written_faces == painted_mesh.indices.size();
    obj_stream.close();
    mtl_stream.close();
    success = success && !obj_stream.fail() && !mtl_stream.fail();
    if (!success) {
        remove_file_if_exists(outputs[0].temporary);
        remove_file_if_exists(outputs[1].temporary);
        BOOST_LOG_TRIVIAL(error) << "Failed writing painted OBJ/MTL or not all faces were exported";
        return false;
    }

    if (!commit_atomic_output_files(outputs, "painted OBJ/MTL")) {
        BOOST_LOG_TRIVIAL(error) << "Failed committing painted OBJ/MTL output pair";
        return false;
    }

    BOOST_LOG_TRIVIAL(info) << "Exported painted OBJ " << obj_path.string() << ", MTL " << mtl_path.string() << ", faces " << written_faces
                            << ", materials " << used_materials.size();
    return true;
}

}; // namespace Slic3r
