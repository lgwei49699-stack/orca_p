#include <catch_main.hpp>
#include "slic3r/Utils/GFDPrintOptions.hpp"

using namespace Slic3r::GFD;
using nlohmann::json;

static json status_response()
{
    return {{"code", 0},
            {"data",
             {{"deviceId", "24A10DB2ADDC"},
              {"deviceState", "ready"},
              {"matlStationInfo",
               {{"slotInfos",
                 json::array({{{"slotId", 1}, {"hasFilament", true}, {"materialName", "PLA"}, {"materialColor", "#F330F9"}},
                              {{"slotId", 2}, {"hasFilament", true}, {"materialName", "PLA"}, {"materialColor", "#FFFFFF"}},
                              {{"slotId", 3}, {"hasFilament", true}, {"materialName", "PLA"}, {"materialColor", "#0ACC38"}},
                              {{"slotId", 4}, {"hasFilament", true}, {"materialName", "PLA"}, {"materialColor", "#161616"}}})}}}}}};
}

TEST_CASE("Advancer3 defaults to leveling while allowing an explicit opt out", "[GFDPrintOptions]")
{
    PrintOptions options;
    options.advancer3.emplace();
    options.advancer3->mappings.push_back({0, 1, "PLA", "#FFFFFF", "#FFFFFF"});
    auto request = build_print_command("Advancer3", "id", "MAC", "url", "gcode", "a.gcode", "req", options);
    REQUIRE(request["levelingBeforePrint"] == true);
    options.advancer3->leveling_before_print = false;
    request                                  = build_print_command("Advancer3", "id", "MAC", "url", "gcode", "a.gcode", "req", options);
    REQUIRE(request["levelingBeforePrint"] == false);
}

TEST_CASE("Advancer3 reads physical loaded IFS slots without generic filament serials", "[GFDPrintOptions]")
{
    auto response = status_response();
    auto slots    = parse_advancer3_slots(response.dump(), "24A10DB2ADDC");
    REQUIRE(slots.size() == 4);
    REQUIRE(slots.front().slot == 1);
    REQUIRE(slots.back().slot == 4);
    REQUIRE(slots[2].material == "PLA");
    REQUIRE(slots[2].color == "#0ACC38");
    response["data"]["matlStationInfo"]["slotInfos"][1] = {{"slotId", 2}, {"hasFilament", false}};
    slots                                               = parse_advancer3_slots(response.dump(), "24A10DB2ADDC");
    REQUIRE(slots.size() == 3);
    REQUIRE(slots[1].slot == 3); // No renumbering after an empty physical slot.
}

TEST_CASE("Unusable PMC status cannot silently bypass Advancer3 mapping", "[GFDPrintOptions]")
{
    auto  response = status_response();
    auto& slots    = response["data"]["matlStationInfo"]["slotInfos"];
    SECTION("offline") { response["data"]["deviceState"] = "offline"; }
    SECTION("different device") { response["data"]["deviceId"] = "another-device"; }
    SECTION("request failure") { response["code"] = 500; }
    SECTION("generic API response") { response["data"] = {{"mulColorFlag", false}, {"slots", json::array()}}; }
    SECTION("no loaded materials")
    {
        for (auto& slot : slots)
            slot["hasFilament"] = false;
    }
    SECTION("no slots") { slots = json::array(); }
    SECTION("missing color") { slots[0].erase("materialColor"); }
    SECTION("invalid color") { slots[0]["materialColor"] = "white"; }
    SECTION("missing material") { slots[0]["materialName"] = ""; }
    SECTION("wrong boolean type") { slots[0]["hasFilament"] = "true"; }
    SECTION("duplicate slot") { slots[1]["slotId"] = 1; }
    SECTION("zero based slot") { slots[0]["slotId"] = 0; }
    SECTION("fractional slot") { slots[0]["slotId"] = 1.5; }
    SECTION("out of range slot") { slots[3]["slotId"] = 5; }
    SECTION("overflow slot") { slots[0]["slotId"] = 4294967297ULL; }
    REQUIRE_THROWS(parse_advancer3_slots(response.dump(), "24A10DB2ADDC"));
    REQUIRE_THROWS(parse_advancer3_slots("bad json", "24A10DB2ADDC"));
}

static PrintOptions print_options()
{
    PrintOptions options;
    options.advancer3 = Advancer3PrintSettings{false, {{1, 4, "PLA", "#161616", "#FFFFFF"}, {3, 4, "PLA", "#161616", "#F330F9"}}};
    return options;
}

TEST_CASE("Flashforge ms preserves sparse T ids and shared physical slots", "[GFDPrintOptions]")
{
    auto options = print_options();
    for (bool leveling : {false, true}) {
        options.advancer3->leveling_before_print = leveling;
        const auto request = build_print_command("Advancer3", "registry-id", "24A10DB2ADDC", "https://cdn/file.gcode", "gcode",
                                                 "my.model.gcode", "stable-request", options);
        REQUIRE(request["deviceId"] == "24A10DB2ADDC");
        REQUIRE(request["levelingBeforePrint"] == leveling);
        REQUIRE(request["fileName"] == "my.model");
        REQUIRE(request["source"] == "slicer");
        REQUIRE(request["fileType"] == "gcode");
        REQUIRE(request["printSn"] == "stable-request");
        REQUIRE(request["requestId"] == "stable-request");
        REQUIRE_FALSE(request.contains("consumable"));
        REQUIRE(request["ms"] == json::array({{{"slot", 4}, {"tNum", 1}, {"mt", "PLA"}, {"sRgb", "#161616"}, {"tRgb", "#FFFFFF"}},
                                              {{"slot", 4}, {"tNum", 3}, {"mt", "PLA"}, {"sRgb", "#161616"}, {"tRgb", "#F330F9"}}}));
    }
}

TEST_CASE("Advancer3 never falls back to unconfirmed print settings", "[GFDPrintOptions]")
{
    auto options = print_options();
    SECTION("no options") { options.advancer3.reset(); }
    SECTION("no mappings") { options.advancer3->mappings.clear(); }
    SECTION("invalid slot") { options.advancer3->mappings[0].slot = 0; }
    SECTION("bad tool") { options.advancer3->mappings[0].tool = 256; }
    SECTION("duplicate tool") { options.advancer3->mappings[1].tool = 1; }
    SECTION("missing model color") { options.advancer3->mappings[0].target_color = ""; }
    SECTION("missing material") { options.advancer3->mappings[0].material = ""; }
    REQUIRE_THROWS(build_print_command("Advancer3", "id", "MAC", "url", "gcode", "model.gcode", "stable", options));
}

TEST_CASE("Advancer3 requires a MAC and G-code filename", "[GFDPrintOptions]")
{
    REQUIRE_THROWS(build_print_command("Advancer3", "id", "", "url", "gcode", "a.gcode", "req", print_options()));
    REQUIRE_THROWS(build_print_command("Advancer3", "id", "MAC", "url", "3mf", "a.3mf", "req", print_options()));
    REQUIRE_THROWS(build_print_command("Advancer3", "id", "MAC", "url", "gcode", "", "req", print_options()));
}

TEST_CASE("Other printers keep the original request and consumable contract", "[GFDPrintOptions]")
{
    for (const auto& type : {"AD5X", "EP7", "EP3Plus", ""}) {
        REQUIRE_FALSE(is_advancer3(type));
        for (const auto& file_type : {"3mf", "gcode"}) {
            auto options = print_options(); // Advancer3 state must never leak into other device types.
            json expected{{"url", "url"},
                          {"deviceId", "registry-id"},
                          {"printSn", "stable"},
                          {"requestId", "stable"},
                          {"fileType", file_type}};
            REQUIRE(build_print_command(type, "registry-id", "MAC", "url", file_type, "name", "stable", options) == expected);
            options.consumable     = {-1, 0, -1, 0};
            expected["consumable"] = {-1, 0, -1, 0};
            REQUIRE(build_print_command(type, "registry-id", "MAC", "url", file_type, "name", "stable", options) == expected);
        }
    }
}
