#ifndef slic3r_GuideMachineMetadata_hpp_
#define slic3r_GuideMachineMetadata_hpp_

#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Slic3r {

struct GuideMachineMetadata
{
    std::string model;
    std::string nozzle;
};

// A read-only, per-vendor resolver for the configuration guide. It deliberately
// does not consult the selected/edited printer or write flattened preset files.
class GuideMachineMetadataResolver
{
public:
    GuideMachineMetadataResolver(const boost::filesystem::path& vendor_dir, const nlohmann::json& machines) : m_vendor_dir(vendor_dir)
    {
        if (!machines.is_array())
            throw std::runtime_error("machine_list must be an array");
        for (const auto& entry : machines) {
            if (!entry.is_object() || !entry.contains("name") || !entry.at("name").is_string() || !entry.contains("sub_path") ||
                !entry.at("sub_path").is_string()) {
                m_errors.emplace_back("Invalid machine catalog entry: " + entry.dump());
                continue;
            }
            const std::string name = entry.at("name").get<std::string>();
            const std::string path = entry.at("sub_path").get<std::string>();
            if (name.empty() || path.empty() || !m_files.emplace(name, path).second)
                m_errors.emplace_back("Empty or duplicate machine catalog entry: " + name);
        }
    }

    const std::vector<std::string>& catalog_errors() const { return m_errors; }

    std::optional<GuideMachineMetadata> resolve(const std::string& name, std::string& error)
    {
        error.clear();
        try {
            std::set<std::string> visiting;
            const auto            effective = resolve_json(name, visiting);
            if (!effective.contains("printer_model") || !effective.at("printer_model").is_string() ||
                effective.at("printer_model").get<std::string>().find_first_not_of(" \t\r\n") == std::string::npos)
                throw std::runtime_error("printer_model must be a nonempty string");
            if (!effective.contains("nozzle_diameter") || !effective.at("nozzle_diameter").is_array() ||
                effective.at("nozzle_diameter").empty())
                throw std::runtime_error("nozzle_diameter must be a nonempty array");
            const auto& raw      = effective.at("nozzle_diameter").front();
            double      diameter = 0.;
            if (raw.is_number()) {
                diameter = raw.get<double>();
            } else if (raw.is_string()) {
                std::istringstream stream(raw.get<std::string>());
                stream.imbue(std::locale::classic());
                if (!(stream >> diameter))
                    throw std::runtime_error("invalid nozzle_diameter");
                stream >> std::ws;
                if (!stream.eof())
                    throw std::runtime_error("invalid nozzle_diameter suffix");
            } else {
                throw std::runtime_error("nozzle_diameter entry must be a number or numeric string");
            }
            if (!std::isfinite(diameter) || diameter <= 0.)
                throw std::runtime_error("nozzle_diameter must be finite and positive");
            // The guide matches nozzle identifiers as strings against the model
            // catalog. Keep valid spellings such as "1.0" instead of turning them
            // into "1" and breaking the filament compatibility mapping.
            if (raw.is_string())
                return GuideMachineMetadata{effective.at("printer_model").get<std::string>(), raw.get<std::string>()};
            std::ostringstream nozzle;
            nozzle.imbue(std::locale::classic());
            nozzle << std::setprecision(12) << diameter;
            return GuideMachineMetadata{effective.at("printer_model").get<std::string>(), nozzle.str()};
        } catch (const std::exception& e) {
            const auto file = m_files.find(name);
            error           = name + " (" + (file == m_files.end() ? m_vendor_dir.string() : (m_vendor_dir / file->second).string()) +
                    "): " + e.what();
            return std::nullopt;
        }
    }

private:
    nlohmann::json resolve_json(const std::string& name, std::set<std::string>& visiting)
    {
        const auto cached = m_cache.find(name);
        if (cached != m_cache.end())
            return cached->second;
        if (!visiting.insert(name).second)
            throw std::runtime_error("inheritance cycle at " + name);
        const auto file = m_files.find(name);
        if (file == m_files.end())
            throw std::runtime_error("missing parent/catalog entry " + name);
        const auto              path = m_vendor_dir / file->second;
        boost::nowide::ifstream input(path.string());
        if (!input.is_open())
            throw std::runtime_error("cannot read " + path.string());
        nlohmann::json own;
        try {
            input >> own;
        } catch (const std::exception& e) {
            throw std::runtime_error(path.string() + ": " + e.what());
        }
        if (!own.is_object())
            throw std::runtime_error(path.string() + ": preset must be an object");
        nlohmann::json effective = nlohmann::json::object();
        if (own.contains("inherits")) {
            if (!own.at("inherits").is_string())
                throw std::runtime_error(path.string() + ": inherits must be a string");
            const auto parent = own.at("inherits").get<std::string>();
            if (!parent.empty())
                effective = resolve_json(parent, visiting);
        }
        effective.update(own);
        visiting.erase(name);
        m_cache.emplace(name, effective);
        return effective;
    }

    boost::filesystem::path               m_vendor_dir;
    std::map<std::string, std::string>    m_files;
    std::map<std::string, nlohmann::json> m_cache;
    std::vector<std::string>              m_errors;
};

} // namespace Slic3r

#endif
