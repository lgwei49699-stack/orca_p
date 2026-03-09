#ifndef slic3r_Format_OBJ_hpp_
#define slic3r_Format_OBJ_hpp_
#include "libslic3r/Color.hpp"
#include <unordered_map>
namespace Slic3r {

class TriangleMesh;
class Model;
class ModelObject;
typedef std::function<void(std::vector<RGBA> &input_colors, bool is_single_color, std::vector<unsigned char> &filament_ids, unsigned char &first_extruder_id)> ObjImportColorFn;
// Load an OBJ file into a provided model.
struct ObjInfo {
    std::vector<RGBA> vertex_colors;
    std::vector<RGBA> face_colors;
    bool              is_single_mtl{false};
    std::vector<std::array<Vec2f,3>> uvs;
    std::string        obj_dircetory;
    std::map<std::string,bool>  pngs;
    std::unordered_map<int, std::string> uv_map_pngs;
    bool              has_uv_png{false};

};

std::string rgba_to_string(const RGBA& color, int precision = 2);

struct GeneralExtruderConfig
{
    unsigned char                               default_extruder_id = 0; 
    std::string                                 default_filament_name; 
    std::vector<std::pair<RGBA, unsigned char>> extruder_mapping;
    float                                       color_match_tolerance = 0.01f; 
};
extern bool load_obj(const char *path, TriangleMesh *mesh, ObjInfo &vertex_colors, std::string &message);
extern bool load_obj(const char *path, Model *model, ObjInfo &vertex_colors, std::string &message, const char *object_name = nullptr);

extern bool store_obj(const char *path, TriangleMesh *mesh);
extern bool store_obj(const char *path, ModelObject *model);
extern bool store_obj(const char *path, Model *model);


extern bool load_general_extruder_config(const std::string& config_path, GeneralExtruderConfig& config);
extern void match_face_filament_ids(const std::vector<RGBA>&     face_colors, 
                                    const GeneralExtruderConfig& config,
                                    std::vector<unsigned char>&  face_filament_ids, 
                                    unsigned char&               first_extruder_id  
);

extern std::map<unsigned char, std::string> get_extruder_color_map(const std::vector<RGBA>&          face_colors,
                                                                   const std::vector<unsigned char>& face_filament_ids);
extern std::string                          rgba_to_html(const RGBA& rgba);

//extern void match_vertex_filament_ids(const std::vector<RGBA>&     vertex_colors,
//                                      const GeneralExtruderConfig& config,
//                                      std::vector<unsigned char>&  vertex_filament_ids,
//                                      unsigned char&               first_extruder_id);

}; // namespace Slic3r

#endif /* slic3r_Format_OBJ_hpp_ */
