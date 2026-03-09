#include "../libslic3r.h"
#include "../Model.hpp"
#include "../TriangleMesh.hpp"

#include "OBJ.hpp"
#include "objparser.hpp"

#include <string>

#include <boost/log/trivial.hpp>

#include "nlohmann/json.hpp"
#include <boost/filesystem.hpp>
#include <fstream>
#include <cmath>
#include <cstdio>  
#include <algorithm> 



using namespace nlohmann;
namespace fs   = boost::filesystem;


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
    return std::abs(target[0] - ref[0]) < tolerance && 
           std::abs(target[1] - ref[1]) < tolerance &&
           std::abs(target[2] - ref[2]) < tolerance &&
           std::abs(target[3] - ref[3]) < tolerance;
}

void match_face_filament_ids(const std::vector<RGBA>&     face_colors,
                             const GeneralExtruderConfig& config, 
                             std::vector<unsigned char>&  face_filament_ids,
                             unsigned char&               first_extruder_id  
)
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


std::string rgba_to_string(const RGBA& color, int precision) { 
    std::stringstream ss;
    ss << std::fixed << std::setprecision(precision) << color[0] << "," << color[1] << "," << color[2] << "," << color[3];
    return ss.str();
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
        for (auto mtl_name : data.mtllibs) {
            if (mtl_name.size() == 0){
                continue;
            }
            exist_mtl = true;
            bool                    mtl_name_is_path = false;
            boost::filesystem::path mtl_abs_path(mtl_name);
            if (boost::filesystem::exists(mtl_abs_path)) {
                mtl_name_is_path = true;
            }
            boost::filesystem::path mtl_path;
            if (!mtl_name_is_path) {
                boost::filesystem::path full_path(path);
                std::string             dir = full_path.parent_path().string();
                auto                    mtl_file = dir + "/" + mtl_name;
                boost::filesystem::path temp_mtl_path(mtl_file);
                mtl_path = temp_mtl_path;
            }
            auto    _mtl_path = mtl_name_is_path ? mtl_abs_path.string().c_str() : mtl_path.string().c_str();
            if (boost::filesystem::exists(mtl_name_is_path ? mtl_abs_path : mtl_path)) {
                if (!ObjParser::mtlparse(_mtl_path, mtl_data)) {
                    BOOST_LOG_TRIVIAL(error) << "load_obj:load_mtl: failed to parse " << _mtl_path;
                    message = _L("load mtl in obj: failed to parse");
                    return false;
                }
            }
            else {
                BOOST_LOG_TRIVIAL(error) << "load_obj: failed to load mtl_path:" << _mtl_path;
            }
        }
    }
    // Count the faces and verify, that all faces are triangular.
    size_t num_faces = 0;
    size_t num_quads = 0;
    for (size_t i = 0; i < data.vertices.size(); ++ i) {
        // Find the end of face.
        size_t j = i;
        for (; j < data.vertices.size() && data.vertices[j].coordIdx != -1; ++ j) ;
        if (size_t num_face_vertices = j - i; num_face_vertices > 0) {
            if (num_face_vertices > 4) {
                // Non-triangular and non-quad faces are not supported as of now.
                BOOST_LOG_TRIVIAL(error) << "load_obj: failed to parse " << path << ". The file contains polygons with more than 4 vertices.";
                message = _L("The file contains polygons with more than 4 vertices.");
                return false;
            } else if (num_face_vertices < 3) {
                // Non-triangular and non-quad faces are not supported as of now.
                BOOST_LOG_TRIVIAL(error) << "load_obj: failed to parse " << path << ". The file contains polygons with less than 2 vertices.";
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
    if (exist_mtl) {
        obj_info.is_single_mtl = data.usemtls.size() == 1 && mtl_data.new_mtl_unmap.size() == 1;
        obj_info.face_colors.reserve(num_faces + num_quads);
    }
    bool has_color = data.has_vertex_color;
    for (size_t i = 0; i < num_vertices; ++ i) {
        size_t j = i * OBJ_VERTEX_LENGTH;
        its.vertices.emplace_back(data.coordinates[j], data.coordinates[j + 1], data.coordinates[j + 2]);
        if (data.has_vertex_color) {
            RGBA color{std::clamp(data.coordinates[j + 3], 0.f, 1.f), std::clamp(data.coordinates[j + 4], 0.f, 1.f), std::clamp(data.coordinates[j + 5], 0.f, 1.f),
                       std::clamp(data.coordinates[j + 6], 0.f, 1.f)};
            obj_info.vertex_colors.emplace_back(color);
        }
    }
    int indices[ONE_FACE_SIZE];
    int uvs[ONE_FACE_SIZE];
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
                                face_color[n] = std::clamp(float(mtl_data.new_mtl_unmap[mtl_name]->Ka[n] + mtl_data.new_mtl_unmap[mtl_name]->Kd[n]), 0.f, 1.f);
                            }
                            else {
                                face_color[n] = std::clamp(float(mtl_data.new_mtl_unmap[mtl_name]->Kd[n]), 0.f, 1.f);
                            }
                        }
                        face_color[3] = mtl_data.new_mtl_unmap[mtl_name]->Tr; // alpha
                        if (mtl_data.new_mtl_unmap[mtl_name]->map_Kd.size() > 0) {
                            auto png_name       = mtl_data.new_mtl_unmap[mtl_name]->map_Kd;
                            obj_info.has_uv_png = true;
                            if (obj_info.pngs.find(png_name) == obj_info.pngs.end()) { obj_info.pngs[png_name] = false; }
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
                auto set_face_color_by_mtl = [&data, &set_face_color](int face_index) {
                    if (data.usemtls.size() == 1) {
                        set_face_color(face_index, data.usemtls[0].name);
                    } else {
                        for (size_t k = 0; k < data.usemtls.size(); k++) {
                            auto mtl = data.usemtls[k];
                            if (face_index >= mtl.face_start && face_index <= mtl.face_end) {
                                set_face_color(face_index, data.usemtls[k].name);
                                break;
                            }
                        }
                    }
                };
                if (exist_mtl) {
                    set_face_color_by_mtl(face_index);
                }
                if (cnt == 4) {
                    its.indices.emplace_back(indices[0], indices[2], indices[3]);
                    int face_index = its.indices.size() - 1;
                    if (exist_mtl) {
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
    //FIXME returning false even if write failed.
    mesh->WriteOBJFile(path);
    return true;
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

}; // namespace Slic3r
