// Profile conversion: the contract with the website (armor slots, corpus of real-shaped
// profiles) and every rule AlphaRing applies on top.
#include "test_util.h"

#include "ringchief/ProfileV5.h"
#include "mcc/CUserProfileJson.h"

using namespace RingChief;
using namespace testutil;

namespace {
    json ToJson(const CUserProfile& up) {
        json j;
        MCC::Splitscreen::to_json(j, up);
        return j;
    }
}

TEST_SUITE("profile") {

TEST_CASE("every armor slot on the website sets exactly the MCC field it names") {
    json slots = Fixture("armor-slots.json");   // exported from the website's catalog
    REQUIRE(slots.size() >= 20);
    json base = ToJson(BuildUserProfile(Profile("p_x", "Base")));
    for (auto& [slot, field_json] : slots.items()) {
        std::string field = field_json.get<std::string>();
        CAPTURE(slot);
        CAPTURE(field);
        REQUIRE_MESSAGE(base.contains(field), "CUserProfile JSON has no field " << field);
        int value = 7;
        json p = Profile("p_x", "X", {{"armor", {{slot, value}}}});
        json after = ToJson(BuildUserProfile(p));
        CHECK(after[field].get<int>() == value);
        // ...and nothing else in the armor block changed.
        for (auto& [other_slot, other_field] : slots.items()) {
            std::string of = other_field.get<std::string>();
            if (of == field) continue;
            CHECK_MESSAGE(after[of] == base[of], "setting " << slot << " also changed " << of);
        }
    }
}

TEST_CASE("colours are written to both MCC colour fields") {
    json p = Profile("p_x", "X", {{"armor", {{"primaryColor", 4}, {"secondaryColor", 5}, {"tertiaryColor", 6}}}});
    CUserProfile up = BuildUserProfile(p);
    CHECK(up.PlayerModelPrimaryColor == 4);
    CHECK(up.PlayerModelPrimaryColorIndex == 4);
    CHECK(up.PlayerModelSecondaryColor == 5);
    CHECK(up.PlayerModelSecondaryColorIndex == 5);
    CHECK(up.PlayerModelTertiaryColor == 6);
    CHECK(up.PlayerModelTertiaryColorIndex == 6);
}

TEST_CASE("null or missing armor keeps the baseline; wrong types are ignored") {
    CUserProfile base = BuildUserProfile(Profile("p_x", "X"));
    json p = Profile("p_x", "X", {{"armor", {{"helmet", nullptr}, {"chest", "7"}, {"legs", 3.5}, {"arms", true}}}});
    CUserProfile up = BuildUserProfile(p);
    CHECK(up.HelmetIndex == base.HelmetIndex);
    CHECK(up.ChestIndex == base.ChestIndex);
    CHECK(up.LegsIndex == base.LegsIndex);
    CHECK(up.ArmsIndex == base.ArmsIndex);
}

TEST_CASE("out-of-range values are clamped to what MCC accepts") {
    json p = Profile("p_x", "X", {
        {"armor", {{"helmet", 99999}, {"chest", -5}}},
        {"controls", {{"preset", 0}, {"lookSensitivityH", 99}, {"lookSensitivityV", 0}, {"lookAcceleration", 9},
                      {"axialDeadZone", -1.0}, {"radialDeadZone", 7.0}}},
    });
    CUserProfile up = BuildUserProfile(p);
    CHECK(up.HelmetIndex == 2130);
    CHECK(up.ChestIndex == 0);
    CHECK(up.HorizontalLookSensitivity == 10);
    CHECK(up.VerticalLookSensitivity == 1);
    CHECK(up.LookAcceleration == 5);
    CHECK(up.LookAxialDeadZone == doctest::Approx(0.0f));
    CHECK(up.LookRadialDeadZone == doctest::Approx(1.0f));
}

TEST_CASE("controls: every toggle maps to the right MCC flag") {
    json on = Profile("p_x", "X", {{"controls", {{"preset", 0}, {"invertLook", true}, {"invertAircraft", true}, {"vibration", false}, {"crouchLock", true}}}});
    json off = Profile("p_x", "X", {{"controls", {{"preset", 0}, {"invertLook", false}, {"invertAircraft", false}, {"vibration", true}, {"crouchLock", false}}}});
    CUserProfile a = BuildUserProfile(on), b = BuildUserProfile(off);
    CHECK(a.LookControlsInverted);   CHECK_FALSE(b.LookControlsInverted);
    CHECK(a.AircraftControlsInverted); CHECK_FALSE(b.AircraftControlsInverted);
    CHECK(a.VibrationDisabled);      CHECK_FALSE(b.VibrationDisabled);   // vibration is inverted in MCC
    CHECK(a.CrouchLockEnabled);      CHECK_FALSE(b.CrouchLockEnabled);
}

TEST_CASE("layering order: baseline < stored MCC fields < website armor") {
    json p = Profile("p_x", "X", {{"mcc", {{"HelmetIndex", 11}, {"ChestIndex", 12}, {"MasterVolume", 0.5}}}, {"armor", {{"helmet", 27}}}});
    CUserProfile up = BuildUserProfile(p);
    CHECK(up.HelmetIndex == 27);                        // website wins
    CHECK(up.ChestIndex == 12);                         // stored MCC value kept
    CHECK(up.MasterVolume == doctest::Approx(0.5f));    // stored MCC setting kept
    CUserProfile plain = BuildUserProfile(Profile("p_y", "Y"));
    CHECK(plain.MasterVolume == doctest::Approx(1.0f)); // baseline for web-only profiles
}

TEST_CASE("elite model and female voice") {
    CUserProfile up = BuildUserProfile(Profile("p_x", "X", {{"armor", {{"useElite", true}, {"femaleVoice", true}}}}));
    CHECK(up.UseEliteModel);
    CHECK(up.UseFemaleVoice);
    CUserProfile no = BuildUserProfile(Profile("p_x", "X", {{"armor", {{"useElite", false}}}}));
    CHECK_FALSE(no.UseEliteModel);
}

TEST_CASE("service tags: short tags are padded, the 4-char MCC field is filled") {
    CUserProfile a = BuildUserProfile(Profile("p_x", "X", {{"serviceTag", "AB"}}));
    CHECK(a.ServiceTag[0] == L'A');
    CHECK(a.ServiceTag[1] == L'B');
    CHECK(a.ServiceTag[2] == L'\0');
    V5Profile p;
    std::string err;
    REQUIRE(ParseV5(Profile("p_x", "X", {{"serviceTag", "ABCDEFG"}}), p, err));
    CHECK(std::wstring(p.service_tag) == L"ABCD");     // never overflows
}

TEST_CASE("ParseV5: gamertags keep non-ASCII characters") {
    V5Profile p;
    std::string err;
    REQUIRE(ParseV5(Profile("p_x", "J\xC3\xB6rg \xE2\x98\x85"), p, err));   // "Jörg ★"
    CHECK(p.gamertag == L"Jörg ★");
    REQUIRE(ParseV5(Profile("p_y", ""), p, err));
    CHECK(p.gamertag == L"Spartan");                   // never blank in game
}

TEST_CASE("ParseV5: team and preset are clamped, custom mapping values too") {
    json m = json::array();
    for (int i = 0; i < 66; i++) m.push_back(i == 0 ? 99 : (i == 1 ? -3 : i % 16));
    V5Profile p;
    std::string err;
    REQUIRE(ParseV5(Profile("p_x", "X", {{"teamPreference", 42}, {"controls", {{"preset", 6}, {"customMapping", m}}}}), p, err));
    CHECK(p.team_preference == 7);
    CHECK(p.controller_preset == 6);
    CHECK(static_cast<int>(p.custom_mapping.actions[0]) == 15);
    CHECK(static_cast<int>(p.custom_mapping.actions[1]) == 0);
    REQUIRE(ParseV5(Profile("p_y", "Y", {{"controls", {{"preset", 6}, {"customMapping", json::array({1, 2, 3})}}}}), p, err));
    CHECK_FALSE(p.has_custom_mapping);                 // wrong length ignored
    CHECK(p.controller_preset == 0);
}

TEST_CASE("every profile the website can produce parses and builds a sane MCC profile") {
    json corpus = Fixture("profiles-corpus.json");
    REQUIRE(corpus.size() == 150);
    int customs = 0;
    for (const auto& j : corpus) {
        CAPTURE(j["id"].get<std::string>());
        V5Profile p;
        std::string err;
        REQUIRE(ParseV5(j, p, err));
        CHECK(p.id == j["id"]);
        CHECK(p.team_preference == j["teamPreference"]);
        CHECK(p.controller_preset == j["controls"]["preset"]);
        if (p.controller_preset == 6) { CHECK(p.has_custom_mapping); customs++; }

        CUserProfile up = BuildUserProfile(j);
        CHECK(up.HorizontalLookSensitivity == j["controls"]["lookSensitivityH"].get<int>());
        CHECK(up.VerticalLookSensitivity == j["controls"]["lookSensitivityV"].get<int>());
        CHECK(up.LookAcceleration == j["controls"]["lookAcceleration"].get<int>());
        CHECK(up.LookAxialDeadZone == doctest::Approx(j["controls"]["axialDeadZone"].get<float>()));
        CHECK(up.VibrationDisabled == !j["controls"]["vibration"].get<bool>());
        CHECK(up.UseEliteModel == j["armor"]["useElite"].get<bool>());
        if (j["armor"].contains("helmet") && j["armor"]["helmet"].is_number()) CHECK(up.HelmetIndex == j["armor"]["helmet"].get<int>());
        if (j["armor"].contains("visorColor") && j["armor"]["visorColor"].is_number()) CHECK(up.VisorColorIndex == j["armor"]["visorColor"].get<int>());
        CHECK(up.MasterVolume == doctest::Approx(1.0f));
    }
    CHECK(customs > 0);
}

TEST_CASE("the built-in baseline is a sane MCC profile") {
    CUserProfile up = BuildUserProfile(json{{"id", "p_x"}, {"gamertag", "X"}});
    CHECK(up.MasterVolume == doctest::Approx(1.0f));
    CHECK(up.MusicVolume > 0.0f);
    CHECK(up.SfxVolume > 0.0f);
    CHECK(up.HorizontalLookSensitivity == 3);
    CHECK(up.VerticalLookSensitivity == 3);
    CHECK(up.LookAcceleration == 3);
    CHECK(up.HelmetIndex == 0);
    CHECK_FALSE(up.VibrationDisabled);
}

}  // TEST_SUITE
