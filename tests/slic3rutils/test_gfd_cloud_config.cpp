#include <catch_main.hpp>
#include "slic3r/Utils/GFDCloudConfig.hpp"

using namespace Slic3r::GFD;
using nlohmann::json;

static json scheme()
{
    return {{"id", "9007199254740993"},         {"name", "EP3 scheme"},
            {"deviceType", "EP3Plus"},          {"sliceType", "orca"},
            {"info", "Keep this note"},         {"configFileUrl", "standard.3mf"},
            {"configFileName", "standard.3mf"}, {"defaultProcessConf", "standard parameters"}};
}

TEST_CASE("Standard imports reuse explicit names before legacy names without crossing grades", "[GFDCloudConfig]")
{
    std::set<std::string> presets;
    const auto            target = [&](const std::string& name, const std::string& grade) {
        return cloud_process_import_name(name, grade, [&](const std::string& candidate) { return presets.count(candidate) != 0; });
    };
    REQUIRE(target("测试", "standard") == "测试 [standard]");
    presets.insert("测试");
    REQUIRE(target("测试", "standard") == "测试");
    REQUIRE(target("测试", "") == "测试");
    REQUIRE(target("测试 [standard]", "standard") == "测试");
    REQUIRE(target("测试", "fast") == "测试 [fast]");
    REQUIRE(target("测试", "fine") == "测试 [fine]");
    presets.insert("测试 [standard]");
    REQUIRE(target("测试", "standard") == "测试 [standard]");
    REQUIRE(target("测试 [standard]", "standard") == "测试 [standard]");
    presets.insert("测试 [fast]");
    REQUIRE(target("测试", "fast") == "测试 [fast]");
    REQUIRE(target("测试", "fine") == "测试 [fine]");
    REQUIRE(target("测试 [fast]", "fast") == "测试 [fast]");
    REQUIRE(target("测试 @EP3 Plus", "standard") == "测试 @EP3 Plus [standard]");
    REQUIRE(target("", "standard").empty());
    REQUIRE_THROWS(target("测试", "unknown"));
}

TEST_CASE("Only successful Standard imports decorate legacy process labels", "[GFDCloudConfig]")
{
    const json uploaded = {{"cloud_config_id", "123"}, {"fineness", "standard"}};
    REQUIRE(cloud_process_display_name("测试", json::object()) == "测试");
    REQUIRE(cloud_process_display_name("测试", uploaded) == "测试");
    auto imported                     = uploaded;
    imported["cloud_import_fineness"] = "standard";
    REQUIRE(cloud_process_display_name("测试", imported) == "测试 [standard]");
    REQUIRE(cloud_process_display_name("测试 [standard]", imported) == "测试 [standard]");
    REQUIRE(cloud_process_display_name("测试 [fast]", imported) == "测试 [fast]");
    REQUIRE(cloud_process_display_name("测试 [fine]", imported) == "测试 [fine]");
    // Saving another cloud grade does not change the grade last imported into this local preset.
    imported["fineness"] = "fine";
    REQUIRE(cloud_process_display_name("测试", imported) == "测试 [standard]");
    REQUIRE(cloud_process_display_name("测试", json::parse(imported.dump())) == "测试 [standard]");
    REQUIRE(cloud_process_display_name("", imported).empty());
    imported["cloud_import_fineness"] = nullptr;
    REQUIRE(cloud_process_display_name("测试", imported) == "测试");
}

TEST_CASE("Legacy source records default to Standard without losing explicit qualities", "[GFDCloudConfig]")
{
    for (const auto& record : {json::object(), json{{"fineness", nullptr}}, json{{"fineness", ""}}}) {
        const auto quality = cloud_record_fineness(record);
        REQUIRE(quality == "standard");
        const auto saved = cloud_save_payload(scheme(), quality, "new-standard.3mf", "new-standard.3mf", "new parameters");
        REQUIRE(saved["configFileUrl"] == "new-standard.3mf");
    }
    for (const char* quality : CLOUD_FINENESS) {
        REQUIRE(cloud_record_fineness({{"fineness", quality}}) == quality);
        const auto saved = cloud_save_payload(scheme(), cloud_record_fineness({{"fineness", quality}}), "selected.3mf", "selected.3mf",
                                              "new");
        REQUIRE(cloud_profile(saved, quality)["configFileUrl"] == "selected.3mf");
    }
    REQUIRE_THROWS(cloud_record_fineness({{"fineness", "unknown"}}));
    REQUIRE_THROWS(cloud_record_fineness({{"fineness", 1}}));
}

TEST_CASE("Legacy files represent only Standard", "[GFDCloudConfig]")
{
    const auto old = scheme();
    REQUIRE(cloud_profile(old, "standard")["configFileUrl"] == "standard.3mf");
    REQUIRE(cloud_profile(old, "fast").empty());
    REQUIRE(cloud_profile(old, "fine").empty());
    REQUIRE_THROWS(cloud_profile(old, "unknown"));
    auto no_file = old;
    no_file.erase("configFileUrl");
    REQUIRE(cloud_profile(no_file, "standard")["defaultProcessConf"] == "standard parameters");
    REQUIRE(cloud_profile(no_file, "fine").empty());
}

TEST_CASE("Adding Fast preserves Standard and metadata", "[GFDCloudConfig]")
{
    const auto old   = scheme();
    const auto saved = cloud_save_payload(old, "fast", "fast.3mf", "fast.3mf", "fast parameters");
    REQUIRE(saved["profiles"].size() == 2);
    REQUIRE(saved["configFileUrl"] == "standard.3mf");
    REQUIRE(saved["defaultProcessConf"] == "standard parameters");
    REQUIRE(saved["info"] == old["info"]);
    REQUIRE(saved["id"] == "9007199254740993");
    REQUIRE(cloud_profile(saved, "fast")["configFileUrl"] == "fast.3mf");
    REQUIRE(cloud_profile(saved, "fast").count("defaultProcessConf") == 0);
    REQUIRE(old == scheme());
}

TEST_CASE("Each quality update preserves all other profiles", "[GFDCloudConfig]")
{
    auto all = cloud_save_payload(scheme(), "fast", "fast.3mf", "fast.3mf", "fast parameters");
    all      = cloud_save_payload(all, "fine", "fine.3mf", "fine.3mf", "fine parameters");
    all["profiles"].push_back({{"fineness", "future"}, {"configFileUrl", "future.3mf"}, {"custom", 42}});
    for (const char* grade : CLOUD_FINENESS) {
        CAPTURE(grade);
        const auto saved = cloud_save_payload(all, grade, "new.3mf", "new.3mf", "new parameters");
        REQUIRE(saved["profiles"].size() == 4);
        for (const auto& old : all["profiles"]) {
            if (cloud_string(old, "fineness") == grade)
                continue;
            bool unchanged = false;
            for (const auto& profile : saved["profiles"])
                unchanged |= profile == old;
            REQUIRE(unchanged);
        }
        REQUIRE(cloud_profile(saved, grade)["configFileUrl"] == "new.3mf");
        REQUIRE(saved["configFileUrl"] == (std::string(grade) == "standard" ? "new.3mf" : "standard.3mf"));
        REQUIRE(saved["defaultProcessConf"] == (std::string(grade) == "standard" ? "new parameters" : "standard parameters"));
    }
}

TEST_CASE("Reject ambiguous profiles rather than overwrite another grade", "[GFDCloudConfig]")
{
    auto bad        = scheme();
    bad["profiles"] = "unexpected";
    REQUIRE_THROWS(cloud_save_payload(bad, "standard", "new.3mf", "new.3mf", "new"));
    bad["profiles"] = {{{"fineness", "fast"}}, {{"fineness", "fast"}}};
    REQUIRE_THROWS(cloud_save_payload(bad, "fast", "new.3mf", "new.3mf", "new"));
    bad["profiles"] = {nullptr};
    REQUIRE_THROWS(cloud_profiles(bad));
    REQUIRE_THROWS(cloud_save_payload(scheme(), "unknown", "new.3mf", "new.3mf", "new"));
}

TEST_CASE("A new scheme requires a Standard file", "[GFDCloudConfig]")
{
    json empty = {{"name", "New"}, {"sliceType", "orca"}, {"deviceType", "EP3Plus"}};
    REQUIRE_THROWS(cloud_save_payload(empty, "fast", "fast.3mf", "fast.3mf", "fast"));
    REQUIRE_THROWS(cloud_save_payload(empty, "fine", "fine.3mf", "fine.3mf", "fine"));
    const auto saved = cloud_save_payload(empty, "standard", "standard.3mf", "standard.3mf", "standard");
    REQUIRE(saved["profiles"].size() == 1);
    REQUIRE(saved.count("id") == 0);
    REQUIRE_THROWS(cloud_save_payload(saved, "standard", "", "", ""));
}

TEST_CASE("An explicit missing Standard profile is not replaced with a stale top-level file", "[GFDCloudConfig]")
{
    auto input        = scheme();
    input["profiles"] = {{{"fineness", "standard"}, {"configFileUrl", ""}}};
    REQUIRE(cloud_profile(input, "standard")["configFileUrl"] == "");
    REQUIRE_THROWS(cloud_save_payload(input, "fast", "fast.3mf", "fast.3mf", "fast"));
}

TEST_CASE("Readback detects a server ignoring profiles or dropping another quality", "[GFDCloudConfig]")
{
    const auto requested = cloud_save_payload(scheme(), "fine", "fine.3mf", "fine.3mf", "fine");
    REQUIRE(cloud_profiles_confirmed(requested, requested));
    REQUIRE_FALSE(cloud_profiles_confirmed(requested, scheme()));
    auto partial                            = requested;
    partial["profiles"][0]["configFileUrl"] = "overwritten.3mf";
    REQUIRE_FALSE(cloud_profiles_confirmed(requested, partial));
    REQUIRE(cloud_profiles_confirmed(scheme(), scheme()));
}

TEST_CASE("Only explicitly edited scheme details replace the latest server values", "[GFDCloudConfig]")
{
    auto latest            = cloud_save_payload(scheme(), "fine", "fine.3mf", "fine.3mf", "fine parameters");
    latest["info"]         = "A newer server note";
    const json name_change = {{"name", "Renamed scheme"}};
    const auto renamed     = cloud_save_payload(latest, "standard", "new.3mf", "new.3mf", "new", name_change);
    REQUIRE(renamed["name"] == "Renamed scheme");
    REQUIRE(renamed["info"] == "A newer server note");
    REQUIRE(renamed["id"] == latest["id"]);
    REQUIRE(cloud_profile(renamed, "fine") == cloud_profile(latest, "fine"));
    REQUIRE(cloud_metadata_confirmed(name_change, renamed));
    REQUIRE_FALSE(cloud_metadata_confirmed(name_change, latest));

    const json clear_note = {{"info", ""}};
    const auto cleared    = cloud_save_payload(latest, "fast", "fast.3mf", "fast.3mf", "fast", clear_note);
    REQUIRE(cleared["name"] == latest["name"]);
    REQUIRE(cleared["info"] == "");
    REQUIRE(cloud_profile(cleared, "standard") == cloud_profile(latest, "standard"));
    REQUIRE(cloud_metadata_confirmed(clear_note, cleared));
    REQUIRE_FALSE(cloud_metadata_confirmed(clear_note, latest));
    REQUIRE(cloud_metadata_confirmed(clear_note, {{"info", nullptr}}));
    REQUIRE(cloud_metadata_confirmed(json::object(), latest));
    REQUIRE_THROWS(cloud_save_payload(latest, "standard", "x.3mf", "x.3mf", "x", {{"id", "another-id"}}));
    REQUIRE_THROWS(cloud_save_payload(latest, "standard", "x.3mf", "x.3mf", "x", {{"name", " "}}));
    REQUIRE_THROWS(cloud_save_payload(latest, "standard", "x.3mf", "x.3mf", "x", {{"info", nullptr}}));
}
