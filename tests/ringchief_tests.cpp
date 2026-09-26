// Unit tests for the parts of Ring Chief that don't need MCC running.
#include "doctest.h"

#include <chrono>
#include <fstream>
#include <random>
#include <thread>

#include "ringchief/Http.h"
#include "ringchief/LocalLink.h"
#include "ringchief/ProfileV5.h"
#include "ringchief/Storage.h"
#include "mcc/CUserProfileJson.h"

using namespace RingChief;
using json = nlohmann::json;

static json Fixture(const char* name) {
    std::ifstream f(std::string(RINGCHIEF_TEST_FIXTURES) + "/" + name);
    REQUIRE(f.good());
    return json::parse(f);
}

static uint16_t RandomPort() {
    std::random_device rd;
    return static_cast<uint16_t>(43000 + rd() % 2000);
}

TEST_CASE("ParseV5 reads identity and controls") {
    json j = Fixture("profile-v5.json");
    V5Profile p;
    std::string err;
    REQUIRE(ParseV5(j, p, err));
    CHECK(p.id == "p_fixture00001");
    CHECK(p.gamertag == L"Fixture Spartan");
    CHECK(std::wstring(p.service_tag) == L"FX01");
    CHECK(p.team_preference == 6);
    CHECK(p.controller_preset == 6);
    CHECK(p.has_custom_mapping);
    CHECK(static_cast<int>(p.custom_mapping.actions[0]) == j["controls"]["customMapping"][0].get<int>());
}

TEST_CASE("ParseV5 rejects unusable profiles and repairs bad presets") {
    V5Profile p;
    std::string err;
    CHECK_FALSE(ParseV5(json::array(), p, err));
    CHECK_FALSE(ParseV5(json{{"gamertag", "x"}}, p, err));
    CHECK(err.find("id") != std::string::npos);
    json j{{"id", "p_1"}, {"gamertag", "Solo"}, {"controls", {{"preset", 6}}}};
    REQUIRE(ParseV5(j, p, err));
    CHECK(p.controller_preset == 0);   // Custom without a mapping falls back to Recon
}

TEST_CASE("BuildUserProfile: baseline + stored MCC fields + v5 armor/controls") {
    json j = Fixture("profile-v5.json");
    CUserProfile up = BuildUserProfile(j);
    // baseline
    CHECK(up.MasterVolume == doctest::Approx(1.0f));
    // v5 armor wins
    CHECK(up.HelmetIndex == 27);
    CHECK(up.LeftShoulderIndex == j["armor"]["leftShoulder"].get<int>());
    // raw MCC value carried from the v4 import (chest 8 wasn't a confirmed chest item)
    CHECK(up.ChestIndex == 8);
    // controls
    CHECK(up.VibrationDisabled == true);           // fixture has vibration off
    CHECK(up.HorizontalLookSensitivity == 10);
    CHECK(up.ServiceTag[0] == L'F');
    CHECK(up.ServiceTag[3] == L'1');
}

TEST_CASE("BuildUserProfile ignores the slot's history") {
    json a{{"id", "p_a"}, {"gamertag", "A"}, {"armor", {{"helmet", 5}}}};
    json b{{"id", "p_b"}, {"gamertag", "B"}, {"armor", json::object()}};
    CUserProfile ua = BuildUserProfile(a);
    CUserProfile ub = BuildUserProfile(b);
    CHECK(ua.HelmetIndex == 5);
    CHECK(ub.HelmetIndex == 0);   // not A's helmet
    CHECK(ub.MasterVolume == doctest::Approx(1.0f));
}

TEST_CASE("CUserProfile JSON round-trip and merge") {
    json j = Fixture("profile-v5.json");
    CUserProfile up = BuildUserProfile(j);
    json out;
    MCC::Splitscreen::to_json(out, up);
    CUserProfile back;
    MCC::Splitscreen::from_json(out, back);
    CHECK(back.HelmetIndex == up.HelmetIndex);
    CHECK(back.LookAcceleration == up.LookAcceleration);
    MCC::Splitscreen::merge_json(json{{"HelmetIndex", 42}}, back);
    CHECK(back.HelmetIndex == 42);
    CHECK(back.LookAcceleration == up.LookAcceleration);   // untouched by merge
}

TEST_CASE("ParseServer accepts the forms people type") {
    ServerAddr a;
    std::string err;
    REQUIRE(ParseServer("halo.dronedude.app", a, err));
    CHECK(a.secure);
    CHECK(a.port == 443);
    CHECK(a.host == L"halo.dronedude.app");
    CHECK(a.display == "halo.dronedude.app");

    REQUIRE(ParseServer("  HTTPS://Halo.DroneDude.app/ ", a, err));
    CHECK(a.host == L"halo.dronedude.app");
    CHECK(a.display == "halo.dronedude.app");

    REQUIRE(ParseServer("http://192.168.4.98:3004", a, err));
    CHECK_FALSE(a.secure);
    CHECK(a.port == 3004);
    CHECK(a.display == "http://192.168.4.98:3004");

    REQUIRE(ParseServer("example.com:8443/ringchief", a, err));
    CHECK(a.secure);
    CHECK(a.port == 8443);
    CHECK(a.base_path == L"/ringchief");

    CHECK_FALSE(ParseServer("", a, err));
    CHECK_FALSE(ParseServer("host:abc", a, err));
    CHECK_FALSE(ParseServer("user@host", a, err));
}

TEST_CASE("DPAPI protect/unprotect round-trips and rejects junk") {
    std::string secret = "token-abc123_XYZ";
    std::string sealed = Storage::Protect(secret);
    CHECK_FALSE(sealed.empty());
    CHECK(sealed.find(secret) == std::string::npos);
    CHECK(Storage::Unprotect(sealed) == secret);
    CHECK(Storage::Unprotect("not-base64!!") == "");
    CHECK(Storage::Unprotect("") == "");
}

TEST_CASE("LocalLink: first instance is the hub, the next follows, frames flow both ways") {
    uint16_t port = RandomPort();
    LocalLink hub, follower, third;
    REQUIRE(hub.Start(port) == LocalLink::Role::Hub);
    REQUIRE(follower.Start(port) == LocalLink::Role::Follower);

    std::vector<LocalLink::Incoming> in;
    std::vector<int> joined, left;
    bool lost = false;
    for (int i = 0; i < 50 && joined.empty(); i++) { hub.Poll(in, joined, left, lost); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    REQUIRE(joined.size() == 1);

    follower.SendToHub(R"({"type":"hello"})");
    hub.Broadcast(R"({"type":"welcome"})");
    std::vector<LocalLink::Incoming> hub_in, fol_in;
    for (int i = 0; i < 100 && (hub_in.empty() || fol_in.empty()); i++) {
        std::vector<int> j2, l2;
        bool lost2 = false;
        hub.Poll(hub_in, j2, l2, lost2);
        follower.Poll(fol_in, j2, l2, lost2);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(hub_in.size() == 1);
    CHECK(hub_in[0].line == R"({"type":"hello"})");
    CHECK(hub_in[0].peer == joined[0]);
    REQUIRE(fol_in.size() == 1);
    CHECK(fol_in[0].line == R"({"type":"welcome"})");

    // The hub closes: the follower notices, and can take over the port.
    hub.Stop();
    bool follower_lost = false;
    for (int i = 0; i < 100 && !follower_lost; i++) {
        std::vector<LocalLink::Incoming> x; std::vector<int> j3, l3;
        follower.Poll(x, j3, l3, follower_lost);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(follower_lost);
    CHECK(follower.Start(port) == LocalLink::Role::Hub);
    CHECK(third.Start(port) == LocalLink::Role::Follower);
}

TEST_CASE("LocalLink splits frames that arrive together or in pieces") {
    uint16_t port = RandomPort();
    LocalLink hub, follower;
    REQUIRE(hub.Start(port) == LocalLink::Role::Hub);
    REQUIRE(follower.Start(port) == LocalLink::Role::Follower);
    std::string big(200000, 'x');
    follower.SendToHub("{\"a\":1}");
    follower.SendToHub("{\"big\":\"" + big + "\"}");
    follower.SendToHub("{\"b\":2}");
    std::vector<LocalLink::Incoming> in;
    for (int i = 0; i < 200 && in.size() < 3; i++) {
        std::vector<int> j, l;
        bool lost = false;
        hub.Poll(in, j, l, lost);
        follower.Poll(in, j, l, lost);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(in.size() == 3);
    CHECK(in[0].line == "{\"a\":1}");
    CHECK(in[1].line.size() == big.size() + 10);
    CHECK(in[2].line == "{\"b\":2}");
}
