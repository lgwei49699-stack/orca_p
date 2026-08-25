#include <catch2/catch.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/miniz_extension.hpp"

#include <boost/filesystem/operations.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <stdexcept>

using namespace Slic3r;

namespace {

class Temporary3mfPath
{
public:
    explicit Temporary3mfPath(const char *prefix) :
        m_path((boost::filesystem::temp_directory_path() /
                boost::filesystem::unique_path(std::string(prefix) + "-%%%%-%%%%.3mf")).string())
    {}

    ~Temporary3mfPath()
    {
        boost::system::error_code error;
        boost::filesystem::remove(m_path, error);
        boost::filesystem::remove(m_path + ".tmp", error);
    }

    const std::string &path() const { return m_path; }

private:
    std::string m_path;
};

class TemporaryFilePath
{
public:
    TemporaryFilePath(const char *prefix, const char *extension) :
        m_path((boost::filesystem::temp_directory_path() /
                boost::filesystem::unique_path(std::string(prefix) + "-%%%%-%%%%" + extension)).string())
    {}

    ~TemporaryFilePath()
    {
        boost::system::error_code error;
        boost::filesystem::remove(m_path, error);
    }

    const std::string &path() const { return m_path; }

private:
    std::string m_path;
};

TEST_CASE("CuraV1 optional raft settings are forward compatible in JSON", "[3mf][CuraV1][Config]")
{
    const auto load_json = [](const nlohmann::json &document, DynamicPrintConfig &config,
                              ConfigSubstitutionContext &substitutions) {
        TemporaryFilePath path("orca-cura-raft-forward-config", ".json");
        std::ofstream output(path.path());
        output << document.dump(2);
        output.close();

        std::map<std::string, std::string> key_values;
        std::string reason;
        const int result = config.load_from_json(path.path(), substitutions, true, key_values, reason);
        return std::make_pair(result, reason);
    };

    SECTION("future optional raft fields are ignored") {
        DynamicPrintConfig config;
        ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
        const auto [result, reason] = load_json(
            {{"raft_mode", "cura_v1"}, {"raft_airgap", "0.31"}, {"raft_future_surface_bias", "17"}},
            config, substitutions);

        REQUIRE(result == 0);
        REQUIRE(reason.empty());
        REQUIRE(config.opt_enum<RaftMode>("raft_mode") == RaftMode::CuraV1);
        REQUIRE(config.opt_float("raft_airgap") == Approx(0.31));
        REQUIRE(std::find(substitutions.unrecogized_keys.begin(), substitutions.unrecogized_keys.end(),
                          "raft_future_surface_bias") != substitutions.unrecogized_keys.end());
    }

    SECTION("invalid known mode values still fail") {
        DynamicPrintConfig invalid_mode_config;
        ConfigSubstitutionContext invalid_mode_substitutions(ForwardCompatibilitySubstitutionRule::Disable);
        const auto [invalid_mode_result, invalid_mode_reason] =
            load_json({{"raft_mode", "cura_v2"}}, invalid_mode_config, invalid_mode_substitutions);
        REQUIRE(invalid_mode_result != 0);
        REQUIRE_FALSE(invalid_mode_reason.empty());
    }
}

class ModelBackupPathGuard
{
public:
    explicit ModelBackupPathGuard(Model &model) :
        m_model(model),
        m_path((boost::filesystem::temp_directory_path() /
                boost::filesystem::unique_path("orca-bbs-model-backup-%%%%-%%%%")).string())
    {
        boost::system::error_code error;
        boost::filesystem::create_directories(m_path, error);
        if (error)
            throw std::runtime_error("Unable to create BBS test model backup path: " + error.message());
        m_model.set_backup_path(m_path);
    }

    // BBS config import/export uses Model::get_backup_path() as scratch storage.
    // Detach before Model destruction and remove synchronously so this fixture
    // neither depends on global temporary_dir() initialization nor the backup worker.
    ~ModelBackupPathGuard()
    {
        m_model.set_backup_path("detach");
        boost::system::error_code error;
        boost::filesystem::remove_all(m_path, error);
    }

private:
    Model      &m_model;
    std::string m_path;
};

class ZipReaderGuard
{
public:
    explicit ZipReaderGuard(mz_zip_archive &archive) : m_archive(&archive) {}
    ~ZipReaderGuard() { close(); }

    void close()
    {
        if (m_archive != nullptr) {
            close_zip_reader(m_archive);
            m_archive = nullptr;
        }
    }

private:
    mz_zip_archive *m_archive;
};

class ZipWriterGuard
{
public:
    explicit ZipWriterGuard(mz_zip_archive &archive) : m_archive(&archive) {}
    ~ZipWriterGuard() { close(); }

    void close()
    {
        if (m_archive != nullptr) {
            close_zip_writer(m_archive);
            m_archive = nullptr;
        }
    }

private:
    mz_zip_archive *m_archive;
};

std::string read_zip_entry_text(const std::string &archive_path, const char *entry_name)
{
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    if (!open_zip_reader(&archive, archive_path))
        throw std::runtime_error("Unable to open test 3MF archive");
    ZipReaderGuard guard(archive);

    const int entry_index = mz_zip_reader_locate_file(&archive, entry_name, nullptr, 0);
    if (entry_index < 0)
        throw std::runtime_error("Required test 3MF entry is missing");

    size_t extracted_size = 0;
    void  *extracted      = mz_zip_reader_extract_to_heap(&archive, static_cast<mz_uint>(entry_index), &extracted_size, 0);
    if (extracted == nullptr && extracted_size != 0)
        throw std::runtime_error("Unable to extract test 3MF entry");

    std::string text;
    if (extracted_size != 0)
        text.assign(static_cast<const char *>(extracted), extracted_size);
    mz_free(extracted);
    return text;
}

void copy_zip_replacing_entry(const std::string &source_path,
                              const std::string &destination_path,
                              const char        *entry_name,
                              const std::string &replacement)
{
    mz_zip_archive source;
    mz_zip_zero_struct(&source);
    if (!open_zip_reader(&source, source_path))
        throw std::runtime_error("Unable to open source test 3MF archive");
    ZipReaderGuard source_guard(source);

    mz_zip_archive destination;
    mz_zip_zero_struct(&destination);
    if (!open_zip_writer(&destination, destination_path))
        throw std::runtime_error("Unable to create rewritten test 3MF archive");
    ZipWriterGuard destination_guard(destination);

    bool replaced = false;
    const mz_uint entry_count = mz_zip_reader_get_num_files(&source);
    for (mz_uint entry_index = 0; entry_index < entry_count; ++entry_index) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&source, entry_index, &stat))
            throw std::runtime_error("Unable to inspect source test 3MF entry");

        if (std::string(stat.m_filename) == entry_name) {
            if (replaced)
                throw std::runtime_error("Source test 3MF contains duplicate model entries");
            if (!mz_zip_writer_add_mem(&destination, entry_name, replacement.data(), replacement.size(), MZ_DEFAULT_COMPRESSION))
                throw std::runtime_error("Unable to replace test 3MF model entry");
            replaced = true;
        } else if (!mz_zip_writer_add_from_zip_reader(&destination, &source, entry_index)) {
            throw std::runtime_error("Unable to copy test 3MF entry");
        }
    }

    if (!replaced)
        throw std::runtime_error("Source test 3MF model entry was not found");
    if (!mz_zip_writer_finalize_archive(&destination))
        throw std::runtime_error("Unable to finalize rewritten test 3MF archive");

    destination_guard.close();
    source_guard.close();
}

void replace_first_or_throw(std::string &text, const std::string &old_value, const std::string &new_value)
{
    const size_t position = text.find(old_value);
    if (position == std::string::npos)
        throw std::runtime_error("Required test fixture value was not found");
    text.replace(position, old_value.size(), new_value);
}

void append_json_member(std::string &text, const std::string &key, const std::string &serialized_value)
{
    const size_t object_end = text.find_last_of('}');
    if (object_end == std::string::npos)
        throw std::runtime_error("Required test JSON object was not found");
    text.insert(object_end, ",\n    \"" + key + "\": " + serialized_value + "\n");
}

bool erase_json_member(std::string &text, const std::string &key)
{
    nlohmann::json document = nlohmann::json::parse(text);
    if (!document.is_object())
        throw std::runtime_error("Required test JSON root is not an object");

    const bool erased = document.erase(key) != 0;
    text              = document.dump(4);
    return erased;
}

void append_object_metadata(std::string &text, const std::string &key, const std::string &value)
{
    const size_t object_end = text.find("  </object>");
    if (object_end == std::string::npos)
        throw std::runtime_error("Required test model object was not found");
    text.insert(object_end, "    <metadata key=\"" + key + "\" value=\"" + value + "\"/>\n");
}

void append_volume_metadata(std::string &text, const std::string &key, const std::string &value)
{
    const size_t volume_end = text.find("    </part>");
    if (volume_end == std::string::npos)
        throw std::runtime_error("Required test model volume was not found");
    text.insert(volume_end, "      <metadata key=\"" + key + "\" value=\"" + value + "\"/>\n");
}

void append_layer_range_option(std::string &text, const std::string &key, const std::string &value)
{
    const size_t range_end = text.find("</range>");
    if (range_end == std::string::npos)
        throw std::runtime_error("Required test layer range was not found");
    text.insert(range_end, "   <option opt_key=\"" + key + "\">" + value + "</option>\n  ");
}

void configure_raft(DynamicPrintConfig &config, RaftMode mode, int base_layers, int interface_layers, int surface_layers)
{
    config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(mode));
    config.set("raft_base_layers", base_layers);
    config.set("raft_interface_layers", interface_layers);
    config.set("raft_surface_layers", surface_layers);
}

void attach_enum_vector_serialization_maps(DynamicPrintConfig &config)
{
    // full_print_config() clones a few generic enum-vector defaults without
    // their definition maps. Real presets provide these maps, while a synthetic
    // test config would otherwise dereference nullptr in save_to_json().
    for (const std::string &key : config.keys()) {
        const auto            *enum_option = dynamic_cast<const ConfigOptionEnumsGeneric *>(config.option(key));
        const ConfigOptionDef *option_def  = print_config_def.get(key);
        if (enum_option == nullptr || enum_option->keys_map != nullptr || option_def == nullptr || option_def->enum_keys_map == nullptr)
            continue;

        auto *mapped_option   = new ConfigOptionEnumsGeneric(option_def->enum_keys_map);
        mapped_option->values = enum_option->values;
        config.set_key_value(key, mapped_option);
    }
}

bool store_bbs_project(const std::string          &path,
                       DynamicPrintConfig         &config,
                       const RaftMode             *object_raft_mode = nullptr,
                       const std::vector<Preset *> &project_presets = {},
                       const std::function<void(ModelObject &)> &configure_object = {})
{
    attach_enum_vector_serialization_maps(config);

    Model                model;
    ModelBackupPathGuard backup_path_guard(model);
    ModelObject         *object = model.add_object();
    object->name                = "required feature test cube";
    object->add_volume(make_cube(10., 10., 10.));
    object->add_instance();
    object->ensure_on_bed();
    if (object_raft_mode != nullptr)
        object->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(*object_raft_mode));
    if (configure_object)
        configure_object(*object);

    StoreParams params;
    params.path            = path.c_str();
    params.model           = &model;
    params.config          = &config;
    params.project_presets = project_presets;
    // Silence prevents the exporter from persisting origin.txt; all remaining
    // StoreParams collections are safely empty for a model+config-only project.
    params.strategy = SaveStrategy::Zip64 | SaveStrategy::Silence;
    return store_bbs_3mf(params);
}

bool store_bbs_gcode_project(const std::string &path, DynamicPrintConfig &config, const std::string &gcode_path)
{
    attach_enum_vector_serialization_maps(config);

    Model                model;
    ModelBackupPathGuard backup_path_guard(model);
    ModelObject         *object = model.add_object();
    object->name                = "required feature G-code test cube";
    object->add_volume(make_cube(10., 10., 10.));
    object->add_instance();
    object->ensure_on_bed();

    PlateData plate;
    plate.plate_index             = 0;
    plate.plate_name              = "required feature G-code test plate";
    plate.is_sliced_valid         = true;
    plate.gcode_file              = gcode_path;
    plate.gcode_prediction        = "321.50";
    plate.gcode_weight            = "12.75";
    plate.printer_model_id        = "required-feature-test-printer";
    plate.nozzle_diameters        = "0.4";
    plate.is_support_used         = true;
    plate.is_label_object_enabled = true;
    plate.objects_and_instances.emplace_back(0, 0);
    plate.plate_thumbnail.set(2, 2);
    plate.plate_thumbnail.pixels = {
        0xff, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
        0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };

    StoreParams params;
    params.path            = path.c_str();
    params.model           = &model;
    params.plate_data_list = {&plate};
    params.config          = &config;
    params.thumbnail_data  = {&plate.plate_thumbnail};
    params.strategy = SaveStrategy::Zip64 | SaveStrategy::Silence | SaveStrategy::WithGcode | SaveStrategy::WithSliceInfo;
    return store_bbs_3mf(params);
}

struct BbsLoadArtifacts
{
    PlateDataPtrs        plates;
    std::vector<Preset*> presets;

    ~BbsLoadArtifacts()
    {
        release_PlateData_list(plates);
        for (Preset *preset : presets)
            delete preset;
    }
};

bool load_bbs_project(const std::string &path, DynamicPrintConfig &config)
{
    // load_from_json() deserializes into the same synthetic full config and
    // therefore needs the maps just as much as save_to_json().
    attach_enum_vector_serialization_maps(config);

    Model                     model;
    ModelBackupPathGuard      backup_path_guard(model);
    BbsLoadArtifacts          artifacts;
    ConfigSubstitutionContext substitutions {ForwardCompatibilitySubstitutionRule::Disable};
    bool                      is_bbl_3mf = false;
    Semver                    file_version;
    return load_bbs_3mf(path.c_str(), &config, &substitutions, &model, &artifacts.plates, &artifacts.presets, &is_bbl_3mf,
                        &file_version, nullptr, LoadStrategy::LoadModel | LoadStrategy::LoadConfig);
}

std::string required_feature_metadata()
{
    return std::string("<metadata name=\"") + BBS_3MF_REQUIRED_FEATURES_TAG + "\">" + BBS_3MF_FEATURE_CURA_RAFT_V1 + "</metadata>";
}

std::string required_feature_namespace_declaration()
{
    return std::string("xmlns:OrcaSlicer=\"") + BBS_3MF_REQUIRED_FEATURES_NAMESPACE + "\"";
}

} // namespace

SCENARIO("Reading 3mf file", "[3mf]") {
    GIVEN("umlauts in the path of the file") {
        Model model;
        WHEN("3mf model is read") {
        	std::string path = std::string(TEST_DATA_DIR) + "/test_3mf/Geräte/Büchse.3mf";
        	DynamicPrintConfig config;
            ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
            bool ret = load_3mf(path.c_str(), config, ctxt, &model, false);
            THEN("load should succeed") {
                REQUIRE(ret);
            }
        }
    }
}

SCENARIO("Export+Import geometry to/from 3mf file cycle", "[3mf]") {
    GIVEN("world vertices coordinates before save") {
        // load a model from stl file
        Model src_model;
        std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        load_stl(src_file.c_str(), &src_model);
        src_model.add_default_instances();

        ModelObject* src_object = src_model.objects.front();

        // apply generic transformation to the 1st volume
        Geometry::Transformation src_volume_transform;
        src_volume_transform.set_offset({ 10.0, 20.0, 0.0 });
        src_volume_transform.set_rotation({ Geometry::deg2rad(25.0), Geometry::deg2rad(35.0), Geometry::deg2rad(45.0) });
        src_volume_transform.set_scaling_factor({ 1.1, 1.2, 1.3 });
        src_volume_transform.set_mirror({ -1.0, 1.0, -1.0 });
        src_object->volumes.front()->set_transformation(src_volume_transform);

        // apply generic transformation to the 1st instance
        Geometry::Transformation src_instance_transform;
        src_instance_transform.set_offset({ 5.0, 10.0, 0.0 });
        src_instance_transform.set_rotation({ Geometry::deg2rad(12.0), Geometry::deg2rad(13.0), Geometry::deg2rad(14.0) });
        src_instance_transform.set_scaling_factor({ 0.9, 0.8, 0.7 });
        src_instance_transform.set_mirror({ 1.0, -1.0, -1.0 });
        src_object->instances.front()->set_transformation(src_instance_transform);

        WHEN("model is saved+loaded to/from 3mf file") {
            // save the model to 3mf file
            std::string test_file = std::string(TEST_DATA_DIR) + "/test_3mf/prusa.3mf";
            store_3mf(test_file.c_str(), &src_model, nullptr, false);

            // load back the model from the 3mf file
            Model dst_model;
            DynamicPrintConfig dst_config;
            {
                ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
                load_3mf(test_file.c_str(), dst_config, ctxt, &dst_model, false);
            }
            boost::filesystem::remove(test_file);

            // compare meshes
            TriangleMesh src_mesh = src_model.mesh();
            TriangleMesh dst_mesh = dst_model.mesh();

            bool res = src_mesh.its.vertices.size() == dst_mesh.its.vertices.size();
            if (res) {
                for (size_t i = 0; i < dst_mesh.its.vertices.size(); ++i) {
                    res &= dst_mesh.its.vertices[i].isApprox(src_mesh.its.vertices[i]);
                }
            }
            THEN("world vertices coordinates after load match") {
                REQUIRE(res);
            }
        }
    }
}

SCENARIO("2D convex hull of sinking object", "[3mf]") {
    GIVEN("model") {
        // load a model
        Model model;
        std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        load_stl(src_file.c_str(), &model);
        model.add_default_instances();

        WHEN("model is rotated, scaled and set as sinking") {
            ModelObject* object = model.objects.front();
            object->center_around_origin(false);

            // set instance's attitude so that it is rotated, scaled and sinking
            ModelInstance* instance = object->instances.front();
            instance->set_rotation(X, -M_PI / 4.0);
            instance->set_offset(Vec3d::Zero());
            instance->set_scaling_factor({ 2.0, 2.0, 2.0 });

            // calculate 2D convex hull
            Polygon hull_2d = object->convex_hull_2d(instance->get_transformation().get_matrix());

            // verify result
            Points result = {
                { -91501496, -15914144 },
                { 91501496, -15914144 },
                { 91501496, 4243 },
                { 78229680, 4246883 },
                { 56898100, 4246883 },
                { -85501496, 4242641 },
                { -91501496, 4243 }
            };

            // Allow 1um error due to floating point rounding.
            bool res = hull_2d.points.size() == result.size();
            if (res)
                for (size_t i = 0; i < result.size(); ++ i) {
                    const Point &p1 = result[i];
                    const Point &p2 = hull_2d.points[i];
                    if (std::abs(p1.x() - p2.x()) > 1 || std::abs(p1.y() - p2.y()) > 1) {
                        res = false;
                        break;
                    }
                }

            THEN("2D convex hull should match with reference") {
                REQUIRE(res);
            }
        }
    }
}

TEST_CASE("BBS 3MF advertises the Cura V1 raft reader requirement", "[3mf][bbs][Raft][RequiredFeature]")
{
    Temporary3mfPath archive("orca-required-feature-write");
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();

    SECTION("a Cura V1 project with physical raft phases writes the marker")
    {
        configure_raft(config, RaftMode::CuraV1, 1, 2, 2);
        REQUIRE(store_bbs_project(archive.path(), config));

        const std::string model_xml = read_zip_entry_text(archive.path(), BBS_3MF_MODEL_FILE);
        REQUIRE(model_xml.find(required_feature_namespace_declaration()) != std::string::npos);
        REQUIRE(model_xml.find(required_feature_metadata()) != std::string::npos);
    }

    SECTION("a Cura V1 project with zero phases still writes the marker for safe editing and round-trip")
    {
        configure_raft(config, RaftMode::CuraV1, 0, 0, 0);
        REQUIRE(store_bbs_project(archive.path(), config));

        const std::string model_xml = read_zip_entry_text(archive.path(), BBS_3MF_MODEL_FILE);
        REQUIRE(model_xml.find(required_feature_metadata()) != std::string::npos);
    }

    SECTION("an object-level Cura V1 override marks a globally legacy project")
    {
        configure_raft(config, RaftMode::Legacy, 1, 2, 2);
        const RaftMode object_raft_mode = RaftMode::CuraV1;
        REQUIRE(store_bbs_project(archive.path(), config, &object_raft_mode));

        const std::string model_xml = read_zip_entry_text(archive.path(), BBS_3MF_MODEL_FILE);
        REQUIRE(model_xml.find(required_feature_metadata()) != std::string::npos);
    }

    SECTION("a global Cura V1 mode remains required despite a legacy object override")
    {
        configure_raft(config, RaftMode::CuraV1, 1, 2, 2);
        const RaftMode object_raft_mode = RaftMode::Legacy;
        REQUIRE(store_bbs_project(archive.path(), config, &object_raft_mode));

        const std::string model_xml = read_zip_entry_text(archive.path(), BBS_3MF_MODEL_FILE);
        REQUIRE(model_xml.find(required_feature_metadata()) != std::string::npos);
    }

    SECTION("an embedded Cura V1 print preset marks a globally legacy project")
    {
        configure_raft(config, RaftMode::Legacy, 1, 2, 2);
        Preset embedded_print(Preset::TYPE_PRINT, "embedded Cura V1 print", false);
        embedded_print.is_project_embedded = true;
        embedded_print.config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
        REQUIRE(store_bbs_project(archive.path(), config, nullptr, {&embedded_print}));

        const std::string model_xml = read_zip_entry_text(archive.path(), BBS_3MF_MODEL_FILE);
        REQUIRE(model_xml.find(required_feature_metadata()) != std::string::npos);
    }

    SECTION("a volume-level Cura V1 override marks a globally legacy project")
    {
        configure_raft(config, RaftMode::Legacy, 1, 2, 2);
        REQUIRE(store_bbs_project(archive.path(), config, nullptr, {}, [](ModelObject &object) {
            REQUIRE(object.volumes.size() == 1);
            object.volumes.front()->config.set_key_value(
                "raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
        }));

        const std::string model_xml = read_zip_entry_text(archive.path(), BBS_3MF_MODEL_FILE);
        REQUIRE(model_xml.find(required_feature_metadata()) != std::string::npos);
    }

    SECTION("a layer-range Cura V1 override marks a globally legacy project")
    {
        configure_raft(config, RaftMode::Legacy, 1, 2, 2);
        REQUIRE(store_bbs_project(archive.path(), config, nullptr, {}, [](ModelObject &object) {
            DynamicPrintConfig range_config;
            range_config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
            object.layer_config_ranges[{1., 2.}].assign_config(std::move(range_config));
        }));

        const std::string model_xml = read_zip_entry_text(archive.path(), BBS_3MF_MODEL_FILE);
        REQUIRE(model_xml.find(required_feature_metadata()) != std::string::npos);
    }

    SECTION("a legacy raft project does not advertise the Cura V1 reader requirement")
    {
        configure_raft(config, RaftMode::Legacy, 1, 2, 2);
        config.set("raft_layers", 5);
        REQUIRE(store_bbs_project(archive.path(), config));

        const std::string model_xml = read_zip_entry_text(archive.path(), BBS_3MF_MODEL_FILE);
        REQUIRE(model_xml.find(required_feature_namespace_declaration()) == std::string::npos);
        REQUIRE(model_xml.find(BBS_3MF_REQUIRED_FEATURES_TAG) == std::string::npos);
        REQUIRE(model_xml.find(BBS_3MF_FEATURE_CURA_RAFT_V1) == std::string::npos);
    }
}

TEST_CASE("BBS 3MF reader accepts its known Cura V1 raft requirement", "[3mf][bbs][Raft][RequiredFeature]")
{
    Temporary3mfPath archive("orca-required-feature-known");
    DynamicPrintConfig stored_config = DynamicPrintConfig::full_print_config();
    configure_raft(stored_config, RaftMode::CuraV1, 2, 3, 4);
    stored_config.set("layer_height", 0.23);
    stored_config.set("raft_airgap", 0.31);
    stored_config.set("raft_surface_line_width", 0.47);
    stored_config.set("raft_surface_line_spacing", 0.33);
    stored_config.set("raft_surface_speed", 37.5);
    stored_config.set("raft_base_acceleration", 600.);
    stored_config.set("raft_interface_acceleration", 2000.);
    stored_config.set("raft_surface_acceleration", 800.);
    REQUIRE(store_bbs_project(archive.path(), stored_config));

    DynamicPrintConfig loaded_config = DynamicPrintConfig::full_print_config();
    configure_raft(loaded_config, RaftMode::Legacy, 1, 0, 0);
    loaded_config.set("layer_height", 0.37);

    REQUIRE(load_bbs_project(archive.path(), loaded_config));
    REQUIRE(loaded_config.opt_enum<RaftMode>("raft_mode") == RaftMode::CuraV1);
    REQUIRE(loaded_config.opt_float("layer_height") == Approx(0.23));
    REQUIRE(loaded_config.opt_float("raft_airgap") == Approx(0.31));
    REQUIRE(loaded_config.opt_int("raft_base_layers") == 2);
    REQUIRE(loaded_config.opt_int("raft_interface_layers") == 3);
    REQUIRE(loaded_config.opt_int("raft_surface_layers") == 4);
    REQUIRE(loaded_config.opt_float("raft_surface_line_width") == Approx(0.47));
    REQUIRE(loaded_config.opt_float("raft_surface_line_spacing") == Approx(0.33));
    REQUIRE(loaded_config.opt_float("raft_surface_speed") == Approx(37.5));
    REQUIRE(loaded_config.opt_float("raft_base_acceleration") == Approx(600.));
    REQUIRE(loaded_config.opt_float("raft_interface_acceleration") == Approx(2000.));
    REQUIRE(loaded_config.opt_float("raft_surface_acceleration") == Approx(800.));
}

TEST_CASE("BBS 3MF reader keeps Cura V1 while ignoring additive unknown raft options",
          "[3mf][bbs][Raft][RequiredFeature]")
{
    Temporary3mfPath source_archive("orca-required-feature-additive-raft-source");
    DynamicPrintConfig stored_config = DynamicPrintConfig::full_print_config();
    configure_raft(stored_config, RaftMode::CuraV1, 2, 3, 4);
    stored_config.set("layer_height", 0.23);
    stored_config.set("raft_airgap", 0.31);

    Preset embedded_print(Preset::TYPE_PRINT, "embedded additive Cura raft print", false);
    embedded_print.is_project_embedded = true;
    embedded_print.config.set_key_value("print_settings_id", new ConfigOptionString(embedded_print.name));
    embedded_print.config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    embedded_print.config.set_key_value("raft_airgap", new ConfigOptionFloat(0.35));

    const RaftMode object_raft_mode = RaftMode::CuraV1;
    const auto configure_nested_raft_overrides = [](ModelObject &object) {
        REQUIRE(object.volumes.size() == 1);
        object.config.set("raft_surface_speed", 39.);
        object.volumes.front()->config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
        object.volumes.front()->config.set("raft_surface_speed", 43.);

        DynamicPrintConfig layer_range_config;
        layer_range_config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
        layer_range_config.set_key_value("raft_surface_speed", new ConfigOptionFloat(41.));
        object.layer_config_ranges[{1., 2.}].assign_config(std::move(layer_range_config));
    };
    REQUIRE(store_bbs_project(source_archive.path(), stored_config, &object_raft_mode, {&embedded_print},
                              configure_nested_raft_overrides));

    const std::string project_unknown_key = "raft_future_project_bias";
    std::string project_config = read_zip_entry_text(source_archive.path(), "Metadata/project_settings.config");
    append_json_member(project_config, project_unknown_key, "\"17\"");
    Temporary3mfPath project_archive("orca-required-feature-additive-raft-project");
    copy_zip_replacing_entry(source_archive.path(), project_archive.path(), "Metadata/project_settings.config", project_config);

    const std::string object_unknown_key = "raft_future_object_bias";
    const std::string volume_unknown_key = "raft_future_volume_bias";
    std::string object_config = read_zip_entry_text(project_archive.path(), "Metadata/model_settings.config");
    append_object_metadata(object_config, object_unknown_key, "19");
    append_volume_metadata(object_config, volume_unknown_key, "21");
    Temporary3mfPath object_archive("orca-required-feature-additive-raft-object");
    copy_zip_replacing_entry(project_archive.path(), object_archive.path(), "Metadata/model_settings.config", object_config);

    const std::string layer_range_unknown_key = "raft_future_layer_range_bias";
    std::string layer_range_config = read_zip_entry_text(object_archive.path(), "Metadata/layer_config_ranges.xml");
    append_layer_range_option(layer_range_config, layer_range_unknown_key, "22");
    Temporary3mfPath layer_range_archive("orca-required-feature-additive-raft-layer-range");
    copy_zip_replacing_entry(object_archive.path(), layer_range_archive.path(), "Metadata/layer_config_ranges.xml",
                             layer_range_config);

    const std::string embedded_unknown_key = "raft_future_embedded_bias";
    std::string embedded_config = read_zip_entry_text(layer_range_archive.path(), "Metadata/process_settings_1.config");
    append_json_member(embedded_config, embedded_unknown_key, "[\"future\", \"values\"]");
    Temporary3mfPath future_archive("orca-required-feature-additive-raft");
    copy_zip_replacing_entry(layer_range_archive.path(), future_archive.path(), "Metadata/process_settings_1.config", embedded_config);

    DynamicPrintConfig loaded_config = DynamicPrintConfig::full_print_config();
    configure_raft(loaded_config, RaftMode::Legacy, 7, 0, 0);
    loaded_config.set("layer_height", 0.37);
    attach_enum_vector_serialization_maps(loaded_config);

    Model                loaded_model;
    ModelBackupPathGuard loaded_backup_path_guard(loaded_model);
    BbsLoadArtifacts     loaded_artifacts;
    ConfigSubstitutionContext substitutions {ForwardCompatibilitySubstitutionRule::Disable};
    substitutions.ignore_unknown_option = [](const t_config_option_key &key) { return key == "caller_owned_optional"; };
    bool                      is_bbl_3mf = false;
    Semver                    file_version;
    REQUIRE(load_bbs_3mf(future_archive.path().c_str(), &loaded_config, &substitutions, &loaded_model,
                         &loaded_artifacts.plates, &loaded_artifacts.presets, &is_bbl_3mf, &file_version, nullptr,
                         LoadStrategy::LoadModel | LoadStrategy::LoadConfig));

    REQUIRE(loaded_config.opt_enum<RaftMode>("raft_mode") == RaftMode::CuraV1);
    REQUIRE(loaded_config.opt_float("layer_height") == Approx(0.23));
    REQUIRE(loaded_config.opt_float("raft_airgap") == Approx(0.31));
    REQUIRE(loaded_config.opt_int("raft_base_layers") == 2);
    REQUIRE(loaded_config.opt_int("raft_interface_layers") == 3);
    REQUIRE(loaded_config.opt_int("raft_surface_layers") == 4);
    REQUIRE(std::find(substitutions.unrecogized_keys.begin(), substitutions.unrecogized_keys.end(), project_unknown_key) !=
            substitutions.unrecogized_keys.end());
    REQUIRE(std::find(substitutions.unrecogized_keys.begin(), substitutions.unrecogized_keys.end(), object_unknown_key) !=
            substitutions.unrecogized_keys.end());
    REQUIRE(std::find(substitutions.unrecogized_keys.begin(), substitutions.unrecogized_keys.end(), volume_unknown_key) !=
            substitutions.unrecogized_keys.end());
    REQUIRE(std::find(substitutions.unrecogized_keys.begin(), substitutions.unrecogized_keys.end(), layer_range_unknown_key) !=
            substitutions.unrecogized_keys.end());
    REQUIRE(static_cast<bool>(substitutions.ignore_unknown_option));
    REQUIRE(substitutions.ignore_unknown_option("caller_owned_optional"));
    REQUIRE_FALSE(substitutions.ignore_unknown_option(project_unknown_key));

    REQUIRE(loaded_model.objects.size() == 1);
    const ModelObject &loaded_object = *loaded_model.objects.front();
    REQUIRE(loaded_object.config.get().opt_enum<RaftMode>("raft_mode") == RaftMode::CuraV1);
    REQUIRE(loaded_object.config.get().opt_float("raft_surface_speed") == Approx(39.));
    REQUIRE_FALSE(loaded_object.config.has(object_unknown_key));
    REQUIRE(loaded_object.volumes.size() == 1);
    REQUIRE(loaded_object.volumes.front()->config.get().opt_enum<RaftMode>("raft_mode") == RaftMode::CuraV1);
    REQUIRE(loaded_object.volumes.front()->config.get().opt_float("raft_surface_speed") == Approx(43.));
    REQUIRE_FALSE(loaded_object.volumes.front()->config.has(volume_unknown_key));

    REQUIRE(loaded_object.layer_config_ranges.size() == 1);
    const ModelConfig &loaded_layer_range = loaded_object.layer_config_ranges.begin()->second;
    REQUIRE(loaded_layer_range.get().opt_enum<RaftMode>("raft_mode") == RaftMode::CuraV1);
    REQUIRE(loaded_layer_range.get().opt_float("raft_surface_speed") == Approx(41.));
    REQUIRE_FALSE(loaded_layer_range.has(layer_range_unknown_key));

    const auto embedded_print_it = std::find_if(
        loaded_artifacts.presets.begin(), loaded_artifacts.presets.end(), [](const Preset *preset) {
            return preset != nullptr && preset->type == Preset::TYPE_PRINT;
        });
    REQUIRE(embedded_print_it != loaded_artifacts.presets.end());
    REQUIRE((*embedded_print_it)->config.opt_enum<RaftMode>("raft_mode") == RaftMode::CuraV1);
    REQUIRE((*embedded_print_it)->config.opt_float("raft_airgap") == Approx(0.35));
    REQUIRE_FALSE((*embedded_print_it)->config.has(embedded_unknown_key));
}

TEST_CASE("BBS 3MF reader falls back unknown Cura raft algorithm versions to Legacy",
          "[3mf][bbs][Raft][RequiredFeature]")
{
    Temporary3mfPath source_archive("orca-required-feature-future-raft-source");
    DynamicPrintConfig stored_config = DynamicPrintConfig::full_print_config();
    configure_raft(stored_config, RaftMode::CuraV1, 2, 3, 4);
    stored_config.set("layer_height", 0.23);

    Preset embedded_print(Preset::TYPE_PRINT, "embedded future Cura raft print", false);
    embedded_print.is_project_embedded = true;
    embedded_print.config.set_key_value("print_settings_id", new ConfigOptionString(embedded_print.name));
    embedded_print.config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
    embedded_print.config.set_key_value("raft_airgap", new ConfigOptionFloat(0.35));

    const RaftMode object_raft_mode = RaftMode::CuraV1;
    REQUIRE(store_bbs_project(source_archive.path(), stored_config, &object_raft_mode, {&embedded_print},
                              [](ModelObject &object) {
                                  REQUIRE(object.volumes.size() == 1);
                                  object.volumes.front()->config.set_key_value(
                                      "raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
                                  object.volumes.front()->config.set("raft_surface_speed", 43.);

                                  DynamicPrintConfig range_config;
                                  range_config.set_key_value(
                                      "raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::CuraV1));
                                  range_config.set_key_value("raft_surface_speed", new ConfigOptionFloat(41.));
                                  object.layer_config_ranges[{1., 2.}].assign_config(std::move(range_config));
                              }));

    const std::string future_raft_feature = "orca_cura_raft_v999";
    std::string       model_xml           = read_zip_entry_text(source_archive.path(), BBS_3MF_MODEL_FILE);
    replace_first_or_throw(model_xml, BBS_3MF_FEATURE_CURA_RAFT_V1, future_raft_feature);

    Temporary3mfPath future_marker_archive("orca-required-feature-future-raft-marker");
    copy_zip_replacing_entry(source_archive.path(), future_marker_archive.path(), BBS_3MF_MODEL_FILE, model_xml);

    // Simulate a future raft enum in every persisted print scope. The reader
    // must normalize this one option without enabling broad substitutions for
    // unrelated future settings.
    std::string project_config = read_zip_entry_text(future_marker_archive.path(), "Metadata/project_settings.config");
    replace_first_or_throw(project_config, "cura_v1", "cura_v999");
    Temporary3mfPath future_project_archive("orca-required-feature-future-raft-project");
    copy_zip_replacing_entry(future_marker_archive.path(), future_project_archive.path(), "Metadata/project_settings.config",
                             project_config);

    std::string object_config = read_zip_entry_text(future_project_archive.path(), "Metadata/model_settings.config");
    replace_first_or_throw(object_config, "cura_v1", "cura_v999");
    replace_first_or_throw(object_config, "cura_v1", "cura_v999");
    Temporary3mfPath future_object_archive("orca-required-feature-future-raft-object");
    copy_zip_replacing_entry(future_project_archive.path(), future_object_archive.path(), "Metadata/model_settings.config",
                             object_config);

    std::string layer_range_config = read_zip_entry_text(future_object_archive.path(), "Metadata/layer_config_ranges.xml");
    replace_first_or_throw(layer_range_config, "cura_v1", "cura_v999");
    Temporary3mfPath future_layer_range_archive("orca-required-feature-future-raft-layer-range");
    copy_zip_replacing_entry(future_object_archive.path(), future_layer_range_archive.path(),
                             "Metadata/layer_config_ranges.xml", layer_range_config);

    std::string embedded_config = read_zip_entry_text(future_layer_range_archive.path(), "Metadata/process_settings_1.config");
    replace_first_or_throw(embedded_config, "cura_v1", "cura_v999");
    Temporary3mfPath future_archive("orca-required-feature-future-raft");
    copy_zip_replacing_entry(future_layer_range_archive.path(), future_archive.path(),
                             "Metadata/process_settings_1.config", embedded_config);

    DynamicPrintConfig loaded_config = DynamicPrintConfig::full_print_config();
    configure_raft(loaded_config, RaftMode::CuraV1, 7, 0, 0);
    loaded_config.set("layer_height", 0.37);
    attach_enum_vector_serialization_maps(loaded_config);

    Model                loaded_model;
    ModelBackupPathGuard loaded_backup_path_guard(loaded_model);
    BbsLoadArtifacts     loaded_artifacts;
    ConfigSubstitutionContext substitutions {ForwardCompatibilitySubstitutionRule::Disable};
    bool                      is_bbl_3mf = false;
    Semver                    file_version;
    REQUIRE(load_bbs_3mf(future_archive.path().c_str(), &loaded_config, &substitutions, &loaded_model,
                         &loaded_artifacts.plates, &loaded_artifacts.presets, &is_bbl_3mf, &file_version, nullptr,
                         LoadStrategy::LoadModel | LoadStrategy::LoadConfig));

    REQUIRE(loaded_config.opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(loaded_config.opt_float("layer_height") == Approx(0.23));
    REQUIRE(loaded_config.opt_int("raft_base_layers") == 2);
    REQUIRE(loaded_config.opt_int("raft_interface_layers") == 3);
    REQUIRE(loaded_config.opt_int("raft_surface_layers") == 4);
    REQUIRE(loaded_model.objects.size() == 1);
    const ModelObject &loaded_object = *loaded_model.objects.front();
    REQUIRE(loaded_object.config.get().opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(loaded_object.volumes.size() == 1);
    REQUIRE(loaded_object.volumes.front()->config.get().opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(loaded_object.volumes.front()->config.get().opt_float("raft_surface_speed") == Approx(43.));
    REQUIRE(loaded_object.layer_config_ranges.size() == 1);
    const ModelConfig &loaded_layer_range = loaded_object.layer_config_ranges.begin()->second;
    REQUIRE(loaded_layer_range.get().opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(loaded_layer_range.get().opt_float("raft_surface_speed") == Approx(41.));

    const auto embedded_print_it = std::find_if(
        loaded_artifacts.presets.begin(), loaded_artifacts.presets.end(), [](const Preset *preset) {
            return preset != nullptr && preset->type == Preset::TYPE_PRINT;
        });
    REQUIRE(embedded_print_it != loaded_artifacts.presets.end());
    REQUIRE((*embedded_print_it)->config.opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE((*embedded_print_it)->config.opt_float("raft_airgap") == Approx(0.35));

    // Once an unsupported future Cura raft mode has been normalized to
    // Legacy, saving the loaded project must not carry the stale feature
    // marker or enum token into the new archive.
    Temporary3mfPath resaved_archive("orca-required-feature-future-raft-resaved");
    const std::string resaved_path = resaved_archive.path();
    StoreParams       resave_params;
    resave_params.path            = resaved_path.c_str();
    resave_params.model           = &loaded_model;
    resave_params.config          = &loaded_config;
    resave_params.project_presets = loaded_artifacts.presets;
    resave_params.strategy        = SaveStrategy::Zip64 | SaveStrategy::Silence;
    REQUIRE(store_bbs_3mf(resave_params));

    const std::string resaved_model_xml = read_zip_entry_text(resaved_path, BBS_3MF_MODEL_FILE);
    REQUIRE(resaved_model_xml.find(future_raft_feature) == std::string::npos);
    REQUIRE(resaved_model_xml.find(required_feature_namespace_declaration()) == std::string::npos);
    REQUIRE(resaved_model_xml.find(BBS_3MF_REQUIRED_FEATURES_TAG) == std::string::npos);

    const std::array<const char *, 4> resaved_config_entries = {
        "Metadata/project_settings.config",
        "Metadata/model_settings.config",
        "Metadata/layer_config_ranges.xml",
        "Metadata/process_settings_1.config",
    };
    for (const char *entry : resaved_config_entries) {
        CAPTURE(entry);
        REQUIRE(read_zip_entry_text(resaved_path, entry).find("cura_v999") == std::string::npos);
    }

    DynamicPrintConfig reloaded_config = DynamicPrintConfig::full_print_config();
    configure_raft(reloaded_config, RaftMode::CuraV1, 7, 0, 1);
    reloaded_config.set("layer_height", 0.41);
    attach_enum_vector_serialization_maps(reloaded_config);

    Model                reloaded_model;
    ModelBackupPathGuard reloaded_backup_path_guard(reloaded_model);
    BbsLoadArtifacts     reloaded_artifacts;
    ConfigSubstitutionContext reloaded_substitutions {ForwardCompatibilitySubstitutionRule::Disable};
    REQUIRE(load_bbs_3mf(resaved_path.c_str(), &reloaded_config, &reloaded_substitutions, &reloaded_model,
                         &reloaded_artifacts.plates, &reloaded_artifacts.presets, &is_bbl_3mf, &file_version, nullptr,
                         LoadStrategy::LoadModel | LoadStrategy::LoadConfig));

    REQUIRE(reloaded_config.opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(reloaded_config.opt_float("layer_height") == Approx(0.23));
    REQUIRE(reloaded_config.opt_int("raft_base_layers") == 2);
    REQUIRE(reloaded_config.opt_int("raft_interface_layers") == 3);
    REQUIRE(reloaded_config.opt_int("raft_surface_layers") == 4);
    REQUIRE(reloaded_model.objects.size() == 1);
    const ModelObject &reloaded_object = *reloaded_model.objects.front();
    REQUIRE(reloaded_object.config.get().opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(reloaded_object.volumes.size() == 1);
    REQUIRE(reloaded_object.volumes.front()->config.get().opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(reloaded_object.volumes.front()->config.get().opt_float("raft_surface_speed") == Approx(43.));
    REQUIRE(reloaded_object.layer_config_ranges.size() == 1);
    const ModelConfig &reloaded_layer_range = reloaded_object.layer_config_ranges.begin()->second;
    REQUIRE(reloaded_layer_range.get().opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(reloaded_layer_range.get().opt_float("raft_surface_speed") == Approx(41.));

    const auto reloaded_embedded_print_it = std::find_if(
        reloaded_artifacts.presets.begin(), reloaded_artifacts.presets.end(), [](const Preset *preset) {
            return preset != nullptr && preset->type == Preset::TYPE_PRINT;
        });
    REQUIRE(reloaded_embedded_print_it != reloaded_artifacts.presets.end());
    REQUIRE((*reloaded_embedded_print_it)->config.opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE((*reloaded_embedded_print_it)->config.opt_float("raft_airgap") == Approx(0.35));

    // A recoverable Cura-raft token must not mask an unrelated unknown feature.
    std::string mixed_model_xml = read_zip_entry_text(source_archive.path(), BBS_3MF_MODEL_FILE);
    const std::string unknown_non_raft_feature = "orca_unknown_required_feature_v999";
    replace_first_or_throw(mixed_model_xml, BBS_3MF_FEATURE_CURA_RAFT_V1,
                           future_raft_feature + "," + unknown_non_raft_feature);
    Temporary3mfPath mixed_archive("orca-required-feature-mixed");
    copy_zip_replacing_entry(source_archive.path(), mixed_archive.path(), BBS_3MF_MODEL_FILE, mixed_model_xml);

    DynamicPrintConfig rejected_config = DynamicPrintConfig::full_print_config();
    configure_raft(rejected_config, RaftMode::Legacy, 6, 0, 0);
    rejected_config.set("layer_height", 0.41);
    attach_enum_vector_serialization_maps(rejected_config);
    const DynamicPrintConfig rejected_config_before_load = rejected_config;
    const std::string expected_error = "This 3MF project requires the unsupported OrcaSlicer feature '" +
                                       unknown_non_raft_feature + "'. Please update OrcaSlicer before opening it.";
    REQUIRE_THROWS_MATCHES(load_bbs_project(mixed_archive.path(), rejected_config), UnsupportedRequired3mfFeatureError,
                           Catch::Matchers::Message(expected_error));
    REQUIRE(rejected_config.equals(rejected_config_before_load));
}

TEST_CASE("BBS 3MF preserves dormant Cura V1 raft tuning", "[3mf][bbs][Raft][ConfigRoundTrip]")
{
    Temporary3mfPath legacy_archive("orca-raft-dormant-legacy");
    DynamicPrintConfig stored_config = DynamicPrintConfig::full_print_config();

    // Legacy mode disables the Cura-only controls in the GUI, but switching
    // modes must not erase values a user may want to restore later.
    configure_raft(stored_config, RaftMode::Legacy, 2, 3, 4);
    stored_config.set("raft_airgap", 0.29);
    stored_config.set("raft_surface_line_width", 0.46);
    stored_config.set("raft_surface_line_spacing", 0.32);
    stored_config.set("raft_surface_speed", 36.5);
    stored_config.set("raft_base_acceleration", 610.);
    stored_config.set("raft_interface_acceleration", 2010.);
    stored_config.set("raft_surface_acceleration", 810.);
    stored_config.set("support_top_z_distance", 0.0);
    REQUIRE(store_bbs_project(legacy_archive.path(), stored_config));

    DynamicPrintConfig loaded_config = DynamicPrintConfig::full_print_config();
    REQUIRE(load_bbs_project(legacy_archive.path(), loaded_config));
    REQUIRE(loaded_config.opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(loaded_config.opt_float("raft_airgap") == Approx(0.29));
    REQUIRE(loaded_config.opt_int("raft_base_layers") == 2);
    REQUIRE(loaded_config.opt_int("raft_interface_layers") == 3);
    REQUIRE(loaded_config.opt_int("raft_surface_layers") == 4);
    REQUIRE(loaded_config.opt_float("raft_surface_line_width") == Approx(0.46));
    REQUIRE(loaded_config.opt_float("raft_surface_line_spacing") == Approx(0.32));
    REQUIRE(loaded_config.opt_float("raft_surface_speed") == Approx(36.5));
    REQUIRE(loaded_config.opt_float("raft_base_acceleration") == Approx(610.));
    REQUIRE(loaded_config.opt_float("raft_interface_acceleration") == Approx(2010.));
    REQUIRE(loaded_config.opt_float("raft_surface_acceleration") == Approx(810.));
    REQUIRE(loaded_config.opt_float("support_top_z_distance") == Approx(0.0));

    // Cura mode must preserve the dormant Legacy layer count as well. The GUI
    // hides it, but switching back to Legacy must restore the exact value.
    Temporary3mfPath cura_archive("orca-raft-dormant-cura");
    configure_raft(stored_config, RaftMode::CuraV1, 2, 3, 4);
    stored_config.set("raft_layers", 7);
    stored_config.set("raft_airgap", 0.28);
    REQUIRE(store_bbs_project(cura_archive.path(), stored_config));

    DynamicPrintConfig loaded_cura_config = DynamicPrintConfig::full_print_config();
    REQUIRE(load_bbs_project(cura_archive.path(), loaded_cura_config));
    REQUIRE(loaded_cura_config.opt_enum<RaftMode>("raft_mode") == RaftMode::CuraV1);
    REQUIRE(loaded_cura_config.opt_int("raft_layers") == 7);
    REQUIRE(loaded_cura_config.opt_float("support_top_z_distance") == Approx(0.0));
    REQUIRE(loaded_cura_config.opt_float("raft_airgap") == Approx(0.28));

    loaded_cura_config.set_key_value("raft_mode", new ConfigOptionEnum<RaftMode>(RaftMode::Legacy));
    REQUIRE(loaded_cura_config.opt_int("raft_layers") == 7);
}

TEST_CASE("BBS 3MF treats a project predating raft_mode as Legacy", "[3mf][bbs][Raft][LegacyCompatibility]")
{
    Temporary3mfPath source_archive("orca-raft-legacy-with-mode");
    DynamicPrintConfig stored_config = DynamicPrintConfig::full_print_config();
    configure_raft(stored_config, RaftMode::Legacy, 1, 0, 0);
    stored_config.set("raft_layers", 6);
    stored_config.set("raft_contact_distance", 0.24);
    stored_config.set("raft_expansion", 1.75);
    stored_config.set_key_value("raft_first_layer_density", new ConfigOptionPercent(86.));
    REQUIRE(store_bbs_project(source_archive.path(), stored_config));

    // The current Legacy writer already omits the Cura reader marker. Removing
    // raft_mode from its project config then reproduces the relevant shape of
    // an archive saved before raft implementations became selectable.
    const std::string model_xml = read_zip_entry_text(source_archive.path(), BBS_3MF_MODEL_FILE);
    REQUIRE(model_xml.find(required_feature_namespace_declaration()) == std::string::npos);
    REQUIRE(model_xml.find(BBS_3MF_REQUIRED_FEATURES_TAG) == std::string::npos);
    REQUIRE(model_xml.find(BBS_3MF_FEATURE_CURA_RAFT_V1) == std::string::npos);

    std::string project_config = read_zip_entry_text(source_archive.path(), "Metadata/project_settings.config");
    REQUIRE(erase_json_member(project_config, "raft_mode"));
    REQUIRE_FALSE(nlohmann::json::parse(project_config).contains("raft_mode"));

    Temporary3mfPath legacy_archive("orca-raft-legacy-without-mode");
    copy_zip_replacing_entry(source_archive.path(), legacy_archive.path(), "Metadata/project_settings.config", project_config);

    DynamicPrintConfig loaded_config = DynamicPrintConfig::full_print_config();
    REQUIRE(loaded_config.opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(load_bbs_project(legacy_archive.path(), loaded_config));
    REQUIRE(loaded_config.opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(loaded_config.opt_int("raft_layers") == 6);
    REQUIRE(loaded_config.opt_float("raft_contact_distance") == Approx(0.24));
    REQUIRE(loaded_config.opt_float("raft_expansion") == Approx(1.75));
    REQUIRE(loaded_config.option<ConfigOptionPercent>("raft_first_layer_density")->value == Approx(86.));
}

TEST_CASE("BBS 3MF preserves Cura V1 Auto sentinels and percentage default line width",
          "[3mf][bbs][Raft][ConfigRoundTrip]")
{
    Temporary3mfPath archive("orca-raft-auto-percent-roundtrip");
    DynamicPrintConfig stored_config = DynamicPrintConfig::full_print_config();
    configure_raft(stored_config, RaftMode::CuraV1, 1, 2, 2);
    stored_config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    stored_config.set_key_value("line_width", new ConfigOptionFloatOrPercent(120., true));

    const std::array<const char *, 9> auto_keys = {
        "raft_base_layer_height",       "raft_base_line_width",       "raft_base_line_spacing",
        "raft_interface_layer_height",  "raft_interface_line_width",  "raft_interface_line_spacing",
        "raft_surface_layer_height",    "raft_surface_line_width",    "raft_surface_line_spacing",
    };
    for (const char *key : auto_keys)
        stored_config.set(key, 0.);

    REQUIRE(store_bbs_project(archive.path(), stored_config));

    DynamicPrintConfig loaded_config = DynamicPrintConfig::full_print_config();
    REQUIRE(load_bbs_project(archive.path(), loaded_config));
    REQUIRE(loaded_config.opt_enum<RaftMode>("raft_mode") == RaftMode::CuraV1);
    REQUIRE(loaded_config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(0) == Approx(0.4));

    const ConfigOptionFloatOrPercent *loaded_line_width =
        loaded_config.option<ConfigOptionFloatOrPercent>("line_width");
    REQUIRE(loaded_line_width != nullptr);
    REQUIRE(loaded_line_width->percent);
    REQUIRE(loaded_line_width->value == Approx(120.));
    for (const char *key : auto_keys) {
        CAPTURE(key);
        REQUIRE(loaded_config.opt_float(key) == Approx(0.));
    }

    PrintConfig print_config;
    print_config.apply(loaded_config, true);
    PrintObjectConfig object_config;
    object_config.apply(loaded_config, true);
    const coordf_t nozzle = loaded_config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(0);
    const RaftPlanConfig resolved = resolve_cura_raft_plan_config(print_config, object_config, nozzle, nozzle);
    REQUIRE(resolved.surface_config.line_width == Approx(0.48));
    REQUIRE(resolved.surface_config.line_spacing == Approx(0.48));
}

TEST_CASE("BBS 3MF readers gate unknown required features by artifact type",
          "[3mf][bbs][Raft][RequiredFeature]")
{
    const std::string gcode_payload =
        "; generated by the required-feature G-code fixture\n"
        "G90\n"
        "M83\n"
        "G1 X1.0 Y1.0 E0.25 F1200\n";
    TemporaryFilePath source_gcode("orca-required-feature-toolpath", ".gcode");
    {
        std::ofstream output(source_gcode.path(), std::ios::binary);
        REQUIRE(output.good());
        output.write(gcode_payload.data(), std::streamsize(gcode_payload.size()));
        REQUIRE(output.good());
    }

    Temporary3mfPath known_archive("orca-required-feature-source");
    DynamicPrintConfig stored_config = DynamicPrintConfig::full_print_config();
    configure_raft(stored_config, RaftMode::CuraV1, 1, 2, 2);
    stored_config.set("layer_height", 0.23);
    REQUIRE(store_bbs_gcode_project(known_archive.path(), stored_config, source_gcode.path()));

    REQUIRE(read_zip_entry_text(known_archive.path(), "Metadata/plate_1.gcode") == gcode_payload);
    REQUIRE_FALSE(read_zip_entry_text(known_archive.path(), "Metadata/plate_1.png").empty());
    REQUIRE_FALSE(read_zip_entry_text(known_archive.path(), "Metadata/model_settings.config").empty());
    REQUIRE_FALSE(read_zip_entry_text(known_archive.path(), "Metadata/slice_info.config").empty());

    // Additive V1 raft options are editor metadata only. A G-code 3MF keeps
    // the finalized artifact and imports every V1 option this reader knows.
    std::string additive_project_config = read_zip_entry_text(known_archive.path(), "Metadata/project_settings.config");
    append_json_member(additive_project_config, "raft_future_gcode_bias", "\"23\"");
    Temporary3mfPath additive_archive("orca-required-feature-gcode-additive-raft");
    copy_zip_replacing_entry(known_archive.path(), additive_archive.path(), "Metadata/project_settings.config",
                             additive_project_config);

    std::ifstream additive_stream(additive_archive.path(), std::ios::binary);
    REQUIRE(additive_stream.good());
    DynamicPrintConfig additive_config = DynamicPrintConfig::full_print_config();
    configure_raft(additive_config, RaftMode::Legacy, 7, 0, 0);
    additive_config.set("layer_height", 0.37);
    attach_enum_vector_serialization_maps(additive_config);
    Model                additive_model;
    ModelBackupPathGuard additive_backup_path_guard(additive_model);
    BbsLoadArtifacts     additive_artifacts;
    Semver               additive_file_version;
    REQUIRE(load_gcode_3mf_from_stream(additive_stream, &additive_config, &additive_model, &additive_artifacts.plates,
                                       &additive_file_version));
    REQUIRE(additive_config.opt_enum<RaftMode>("raft_mode") == RaftMode::CuraV1);
    REQUIRE(additive_config.opt_float("layer_height") == Approx(0.23));
    REQUIRE(additive_config.opt_int("raft_base_layers") == 1);
    REQUIRE(additive_config.opt_int("raft_interface_layers") == 2);
    REQUIRE(additive_config.opt_int("raft_surface_layers") == 2);
    REQUIRE(additive_artifacts.plates.size() == 1);
    REQUIRE(additive_artifacts.plates.front()->gcode_file == "Metadata/plate_1.gcode");
    REQUIRE_FALSE(additive_artifacts.plates.front()->plate_thumbnail.pixels.empty());

    std::string model_xml = read_zip_entry_text(known_archive.path(), BBS_3MF_MODEL_FILE);
    const size_t feature_pos = model_xml.find(BBS_3MF_FEATURE_CURA_RAFT_V1);
    REQUIRE(feature_pos != std::string::npos);
    const std::string unknown_feature = "orca_unknown_required_feature_v999";
    model_xml.replace(feature_pos, std::char_traits<char>::length(BBS_3MF_FEATURE_CURA_RAFT_V1), unknown_feature);

    Temporary3mfPath unknown_archive("orca-required-feature-unknown");
    copy_zip_replacing_entry(known_archive.path(), unknown_archive.path(), BBS_3MF_MODEL_FILE, model_xml);

    const std::string archived_thumbnail = read_zip_entry_text(unknown_archive.path(), "Metadata/plate_1.png");
    REQUIRE(read_zip_entry_text(unknown_archive.path(), "Metadata/plate_1.gcode") == gcode_payload);
    REQUIRE_FALSE(archived_thumbnail.empty());

    DynamicPrintConfig destination_config = DynamicPrintConfig::full_print_config();
    configure_raft(destination_config, RaftMode::Legacy, 7, 0, 0);
    destination_config.set("layer_height", 0.37);
    attach_enum_vector_serialization_maps(destination_config);
    const DynamicPrintConfig config_before_load = destination_config;

    const std::string expected_error = "This 3MF project requires the unsupported OrcaSlicer feature '" + unknown_feature +
                                       "'. Please update OrcaSlicer before opening it.";
    REQUIRE_THROWS_MATCHES(load_bbs_project(unknown_archive.path(), destination_config), UnsupportedRequired3mfFeatureError,
                           Catch::Matchers::Message(expected_error));
    REQUIRE(destination_config.equals(config_before_load));

    // A G-code 3MF is a finalized artifact: an unknown editor feature must not
    // hide its fixed model payload, but its incompatible project config is skipped.
    std::ifstream archive_stream(unknown_archive.path(), std::ios::binary);
    REQUIRE(archive_stream.good());

    DynamicPrintConfig stream_config = DynamicPrintConfig::full_print_config();
    configure_raft(stream_config, RaftMode::Legacy, 7, 0, 0);
    stream_config.set("layer_height", 0.37);
    attach_enum_vector_serialization_maps(stream_config);
    const DynamicPrintConfig stream_config_before_load = stream_config;

    Model                stream_model;
    ModelBackupPathGuard stream_backup_path_guard(stream_model);
    BbsLoadArtifacts     stream_artifacts;
    Semver               stream_file_version;
    REQUIRE(load_gcode_3mf_from_stream(archive_stream, &stream_config, &stream_model, &stream_artifacts.plates,
                                       &stream_file_version));
    REQUIRE(stream_config.equals(stream_config_before_load));
    REQUIRE(stream_model.model_info != nullptr);
    REQUIRE(stream_model.model_info->metadata_items.at("Thumbnail") == "Metadata/plate_1.png");
    REQUIRE(stream_artifacts.plates.size() == 1);

    const PlateData &loaded_plate = *stream_artifacts.plates.front();
    REQUIRE(loaded_plate.plate_index == 0);
    REQUIRE(loaded_plate.gcode_file == "Metadata/plate_1.gcode");
    REQUIRE(loaded_plate.gcode_prediction == "321.50");
    REQUIRE(loaded_plate.gcode_weight == "12.75");
    REQUIRE(loaded_plate.printer_model_id == "required-feature-test-printer");
    REQUIRE(loaded_plate.nozzle_diameters == "0.4");
    REQUIRE(loaded_plate.is_support_used);
    REQUIRE(loaded_plate.is_label_object_enabled);
    REQUIRE(loaded_plate.thumbnail_file == "Metadata/plate_1.png");
    REQUIRE_FALSE(loaded_plate.plate_thumbnail.pixels.empty());
    REQUIRE(std::string(loaded_plate.plate_thumbnail.pixels.begin(), loaded_plate.plate_thumbnail.pixels.end()) == archived_thumbnail);

    // An unknown feature from the narrowly recoverable Cura-raft family still
    // imports the project config, normalizes a future raft_mode enum to Legacy,
    // and preserves the same finalized G-code artifacts.
    std::string future_raft_model_xml = read_zip_entry_text(known_archive.path(), BBS_3MF_MODEL_FILE);
    replace_first_or_throw(future_raft_model_xml, BBS_3MF_FEATURE_CURA_RAFT_V1, "orca_cura_raft_v999");
    Temporary3mfPath future_raft_marker_archive("orca-required-feature-gcode-future-raft-marker");
    copy_zip_replacing_entry(known_archive.path(), future_raft_marker_archive.path(), BBS_3MF_MODEL_FILE,
                             future_raft_model_xml);

    std::string future_raft_project_config =
        read_zip_entry_text(future_raft_marker_archive.path(), "Metadata/project_settings.config");
    replace_first_or_throw(future_raft_project_config, "cura_v1", "cura_v999");
    Temporary3mfPath future_raft_archive("orca-required-feature-gcode-future-raft");
    copy_zip_replacing_entry(future_raft_marker_archive.path(), future_raft_archive.path(),
                             "Metadata/project_settings.config", future_raft_project_config);

    std::ifstream future_raft_stream(future_raft_archive.path(), std::ios::binary);
    REQUIRE(future_raft_stream.good());

    DynamicPrintConfig future_raft_stream_config = DynamicPrintConfig::full_print_config();
    configure_raft(future_raft_stream_config, RaftMode::CuraV1, 7, 0, 0);
    future_raft_stream_config.set("layer_height", 0.37);
    attach_enum_vector_serialization_maps(future_raft_stream_config);

    Model                future_raft_stream_model;
    ModelBackupPathGuard future_raft_stream_backup_path_guard(future_raft_stream_model);
    BbsLoadArtifacts     future_raft_stream_artifacts;
    Semver               future_raft_stream_file_version;
    REQUIRE(load_gcode_3mf_from_stream(future_raft_stream, &future_raft_stream_config, &future_raft_stream_model,
                                       &future_raft_stream_artifacts.plates, &future_raft_stream_file_version));
    REQUIRE(future_raft_stream_config.opt_enum<RaftMode>("raft_mode") == RaftMode::Legacy);
    REQUIRE(future_raft_stream_config.opt_float("layer_height") == Approx(0.23));
    REQUIRE(future_raft_stream_config.opt_int("raft_base_layers") == 1);
    REQUIRE(future_raft_stream_config.opt_int("raft_interface_layers") == 2);
    REQUIRE(future_raft_stream_config.opt_int("raft_surface_layers") == 2);
    REQUIRE(future_raft_stream_model.model_info != nullptr);
    REQUIRE(future_raft_stream_model.model_info->metadata_items.at("Thumbnail") == "Metadata/plate_1.png");
    REQUIRE(future_raft_stream_artifacts.plates.size() == 1);

    const PlateData &future_raft_plate = *future_raft_stream_artifacts.plates.front();
    REQUIRE(future_raft_plate.gcode_file == "Metadata/plate_1.gcode");
    REQUIRE(future_raft_plate.gcode_prediction == "321.50");
    REQUIRE(future_raft_plate.gcode_weight == "12.75");
    REQUIRE(future_raft_plate.thumbnail_file == "Metadata/plate_1.png");
    REQUIRE_FALSE(future_raft_plate.plate_thumbnail.pixels.empty());
    REQUIRE(std::string(future_raft_plate.plate_thumbnail.pixels.begin(), future_raft_plate.plate_thumbnail.pixels.end()) ==
            archived_thumbnail);
}
