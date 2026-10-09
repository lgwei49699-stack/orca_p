#ifndef slic3r_GFDCloudConfig_hpp_
#define slic3r_GFDCloudConfig_hpp_

#include <array>
#include <set>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

namespace Slic3r { namespace GFD {

inline constexpr std::array<const char*, 3> CLOUD_FINENESS = {"standard", "fast", "fine"};

inline bool valid_cloud_fineness(const std::string& value)
{
    for (const char* candidate : CLOUD_FINENESS)
        if (value == candidate)
            return true;
    return false;
}

inline std::string cloud_string(const nlohmann::json& object, const char* key)
{
    const auto it = object.find(key);
    if (it == object.end() || it->is_null())
        return {};
    if (it->is_string())
        return it->get<std::string>();
    if (it->is_number_integer() || it->is_number_unsigned())
        return it->dump();
    return {};
}

// A cloud scheme ID belongs to the account/environment where it was selected.
// Only a new scheme with no selected account may acquire its first account at login.
inline bool cloud_upload_session_matches(const std::string& expected_environment,
                                         const std::string& expected_account,
                                         const std::string& current_environment,
                                         const std::string& current_account,
                                         bool               allow_initial_account = false)
{
    return expected_environment == current_environment && !current_account.empty() &&
           (expected_account == current_account || (allow_initial_account && expected_account.empty()));
}

// Records created before quality selection have no grade and belong to Standard.
inline std::string cloud_record_fineness(const nlohmann::json& record)
{
    const auto it = record.find("fineness");
    if (it == record.end() || it->is_null() || (it->is_string() && it->get<std::string>().empty()))
        return "standard";
    if (!it->is_string() || !valid_cloud_fineness(it->get<std::string>()))
        throw std::runtime_error("Unknown cloud configuration quality.");
    return it->get<std::string>();
}

// Preserve unknown grades and profile metadata when editing one supported grade.
inline nlohmann::json cloud_profiles(const nlohmann::json& scheme)
{
    nlohmann::json        profiles = nlohmann::json::array();
    std::set<std::string> grades;
    const auto            it = scheme.find("profiles");
    if (it != scheme.end() && !it->is_null()) {
        if (!it->is_array())
            throw std::runtime_error("Invalid cloud configuration profiles.");
        for (const auto& profile : *it) {
            const std::string grade = cloud_string(profile, "fineness");
            if (!profile.is_object() || grade.empty() || !grades.insert(grade).second)
                throw std::runtime_error("Invalid or duplicate cloud configuration quality.");
            profiles.push_back(profile);
        }
    }
    if (!grades.count("standard") &&
        (!cloud_string(scheme, "configFileUrl").empty() || !cloud_string(scheme, "defaultProcessConf").empty())) {
        nlohmann::json standard = {{"fineness", "standard"},
                                   {"configFileUrl", cloud_string(scheme, "configFileUrl")},
                                   {"configFileName", cloud_string(scheme, "configFileName")}};
        if (!cloud_string(scheme, "defaultProcessConf").empty())
            standard["defaultProcessConf"] = cloud_string(scheme, "defaultProcessConf");
        profiles.push_back(std::move(standard));
    }
    return profiles;
}

inline nlohmann::json cloud_profile(const nlohmann::json& scheme, const std::string& grade)
{
    if (!valid_cloud_fineness(grade))
        throw std::runtime_error("Unknown cloud configuration quality.");
    for (const auto& profile : cloud_profiles(scheme))
        if (cloud_string(profile, "fineness") == grade)
            return profile;
    return nlohmann::json::object();
}

inline nlohmann::json cloud_save_payload(const nlohmann::json& scheme,
                                         const std::string&    grade,
                                         const std::string&    file_url,
                                         const std::string&    file_name,
                                         const std::string&    process_config,
                                         const nlohmann::json& metadata_changes = nlohmann::json::object())
{
    if (!valid_cloud_fineness(grade))
        throw std::runtime_error("Unknown cloud configuration quality.");
    if (file_url.empty())
        throw std::runtime_error("The cloud configuration file is missing.");
    if (!metadata_changes.is_object())
        throw std::runtime_error("Invalid cloud scheme details.");
    for (auto field = metadata_changes.begin(); field != metadata_changes.end(); ++field) {
        if ((field.key() != "name" && field.key() != "info") || !field->is_string() ||
            (field.key() == "name" && field->get<std::string>().find_first_not_of(" \t\r\n") == std::string::npos))
            throw std::runtime_error("Invalid cloud scheme details.");
    }

    auto profiles = cloud_profiles(scheme);
    bool replaced = false;
    for (auto& profile : profiles) {
        if (cloud_string(profile, "fineness") != grade)
            continue;
        profile["configFileUrl"]  = file_url;
        profile["configFileName"] = file_name;
        // Only retain this extension when it is part of the server's profile schema.
        if (profile.contains("defaultProcessConf"))
            profile["defaultProcessConf"] = process_config;
        replaced = true;
    }
    if (!replaced)
        profiles.push_back({{"fineness", grade}, {"configFileUrl", file_url}, {"configFileName", file_name}});

    nlohmann::json payload = nlohmann::json::object();
    for (const char* key : {"id", "name", "info", "deviceType", "sliceType", "defaultProcessConf"})
        if (scheme.contains(key))
            payload[key] = scheme[key];
    bool has_standard = false;
    for (const auto& profile : profiles) {
        if (cloud_string(profile, "fineness") != "standard")
            continue;
        payload["configFileUrl"]  = cloud_string(profile, "configFileUrl");
        payload["configFileName"] = cloud_string(profile, "configFileName");
        has_standard              = !cloud_string(profile, "configFileUrl").empty();
    }
    if (!has_standard)
        throw std::runtime_error("Upload a Standard profile before adding other qualities.");
    if (grade == "standard")
        payload["defaultProcessConf"] = process_config;
    // Apply only explicitly edited metadata, preserving untouched fields from the latest detail.
    for (auto field = metadata_changes.begin(); field != metadata_changes.end(); ++field)
        payload[field.key()] = field.value();
    payload["profiles"] = std::move(profiles);
    return payload;
}

inline bool cloud_profiles_confirmed(const nlohmann::json& expected, const nlohmann::json& actual)
{
    const auto actual_profiles = cloud_profiles(actual);
    for (const auto& profile : cloud_profiles(expected)) {
        bool matched = false;
        for (const auto& saved : actual_profiles) {
            if (cloud_string(saved, "fineness") == cloud_string(profile, "fineness") &&
                cloud_string(saved, "configFileUrl") == cloud_string(profile, "configFileUrl")) {
                matched = true;
                break;
            }
        }
        if (!matched)
            return false;
    }
    return true;
}

inline bool cloud_metadata_confirmed(const nlohmann::json& changes, const nlohmann::json& actual)
{
    if (!changes.is_object())
        return false;
    for (auto field = changes.begin(); field != changes.end(); ++field) {
        if (!field->is_string() || cloud_string(actual, field.key().c_str()) != field->get<std::string>())
            return false;
    }
    return true;
}

}} // namespace Slic3r::GFD

#endif
