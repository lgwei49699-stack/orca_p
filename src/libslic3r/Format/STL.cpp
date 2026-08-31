#include "../libslic3r.h"
#include "../Model.hpp"
#include "../TriangleMesh.hpp"

#include "STL.hpp"

#include <string>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

namespace fs = boost::filesystem;

namespace {

bool is_replaceable_stl_output(const fs::path& output)
{
    boost::system::error_code status_error;
    const fs::file_status     status = fs::symlink_status(output, status_error);
    if (status.type() == fs::file_not_found)
        return true;
    if (status_error || fs::is_symlink(status) || !fs::is_regular_file(status)) {
        BOOST_LOG_TRIVIAL(error) << "Refusing to replace non-regular STL output: " << output.string();
        return false;
    }
    return true;
}

void remove_stl_temporary(const fs::path& path)
{
    boost::system::error_code ignored;
    fs::remove(path, ignored);
}

bool install_stl_output(const fs::path& output, const fs::path& temporary, const fs::path& backup)
{
    if (!is_replaceable_stl_output(output)) {
        remove_stl_temporary(temporary);
        return false;
    }

    boost::system::error_code status_error;
    const fs::file_status     status = fs::symlink_status(output, status_error);
    if (status.type() == fs::file_not_found)
        status_error.clear();
    if (status_error) {
        remove_stl_temporary(temporary);
        BOOST_LOG_TRIVIAL(error) << "Failed inspecting STL output " << output.string() << ": " << status_error.message();
        return false;
    }

    bool backed_up = false;
    if (status.type() != fs::file_not_found) {
        boost::system::error_code backup_error;
        fs::rename(output, backup, backup_error);
        if (backup_error) {
            remove_stl_temporary(temporary);
            BOOST_LOG_TRIVIAL(error) << "Failed backing up STL output " << output.string() << ": " << backup_error.message();
            return false;
        }
        backed_up = true;
    }

    boost::system::error_code install_error;
    fs::rename(temporary, output, install_error);
    if (install_error) {
        remove_stl_temporary(temporary);
        if (backed_up) {
            boost::system::error_code restore_error;
            fs::rename(backup, output, restore_error);
            if (restore_error)
                BOOST_LOG_TRIVIAL(error) << "Failed restoring STL output " << output.string() << "; the original remains at "
                                         << backup.string() << ": " << restore_error.message();
        }
        BOOST_LOG_TRIVIAL(error) << "Failed installing STL output " << output.string() << ": " << install_error.message();
        return false;
    }

    if (backed_up) {
        boost::system::error_code remove_error;
        fs::remove(backup, remove_error);
        if (remove_error)
            BOOST_LOG_TRIVIAL(warning) << "Failed removing obsolete STL backup " << backup.string() << ": " << remove_error.message();
    }
    return true;
}

} // namespace

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

namespace Slic3r {

bool load_stl(const char *path, Model *model, const char *object_name_in, ImportstlProgressFn stlFn, int custom_header_length, bool repair,
              MeshRepairReport *repair_report)
{
    TriangleMesh mesh;
    std::string design_id;

    if (!mesh.ReadSTLFile(path, repair, stlFn, custom_header_length, repair_report)) {
        //    die "Failed to open $file\n" if !-e $path;
        return false;
    }
    if (mesh.empty()) {
        // die "This STL file couldn't be read because it's empty.\n"
        return false;
    }

    std::string object_name;
    if (object_name_in == nullptr) {
        const char *last_slash = strrchr(path, DIR_SEPARATOR);
        object_name.assign((last_slash == nullptr) ? path : last_slash + 1);
    } else
       object_name.assign(object_name_in);

    model->add_object(object_name.c_str(), path, std::move(mesh));
    return true;
}

bool store_stl(const char *path, TriangleMesh *mesh, bool binary)
{
    if (path == nullptr || *path == '\0' || mesh == nullptr || mesh->empty())
        return false;

    const fs::path output(path);
    if (!is_replaceable_stl_output(output))
        return false;

    fs::path temporary;
    fs::path backup;
    try {
        const fs::path directory = output.parent_path();
        temporary                = directory / fs::unique_path(output.filename().string() + ".tmp-%%%%-%%%%");
        backup                   = directory / fs::unique_path(output.filename().string() + ".bak-%%%%-%%%%");
    } catch (const fs::filesystem_error& exception) {
        BOOST_LOG_TRIVIAL(error) << "Failed creating temporary STL paths: " << exception.what();
        return false;
    }

    const std::string temporary_path = temporary.string();
    if (!(binary ? mesh->write_binary(temporary_path.c_str()) : mesh->write_ascii(temporary_path.c_str()))) {
        remove_stl_temporary(temporary);
        return false;
    }
    return install_stl_output(output, temporary, backup);
}

bool store_stl(const char *path, ModelObject *model_object, bool binary)
{
    TriangleMesh mesh = model_object->mesh();
    return store_stl(path, &mesh, binary);
}

bool store_stl(const char *path, Model *model, bool binary)
{
    TriangleMesh mesh = model->mesh();
    return store_stl(path, &mesh, binary);
}

}; // namespace Slic3r
