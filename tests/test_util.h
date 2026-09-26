#pragma once
// Shared helpers for the Ring Chief tests.
#include "doctest.h"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "ringchief/Session.h"

namespace testutil {
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    inline json Fixture(const std::string& name) {
        std::ifstream f(std::string(RINGCHIEF_TEST_FIXTURES) + "/" + name);
        REQUIRE_MESSAGE(f.good(), "missing fixture " << name);
        return json::parse(f);
    }

    inline uint16_t RandomPort() {
        static std::mt19937 rng(std::random_device{}());
        return static_cast<uint16_t>(43000 + rng() % 12000);
    }

    inline double Now() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    inline void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

    // Calls step() every 10 ms until done() or the timeout.
    inline bool PollUntil(const std::function<void()>& step, const std::function<bool()>& done, double seconds = 3.0) {
        double end = Now() + seconds;
        while (Now() < end) {
            step();
            if (done()) return true;
            Sleep(10);
        }
        step();
        return done();
    }

    // A fresh folder under %TEMP%, deleted afterwards.
    struct TempDir {
        fs::path path;
        TempDir() {
            static std::mt19937 rng(std::random_device{}());
            path = fs::temp_directory_path() / ("ringchief-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(rng()));
            fs::create_directories(path);
        }
        ~TempDir() {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
        std::string str() const { return path.string(); }
    };

    // Records everything the session asks the game to do.
    struct FakeGame : RingChief::GameAdapter {
        std::string id;
        int slots = 1;
        std::vector<RingChief::V5Profile> profiles;
        std::string slot[4];
        std::map<std::string, int> teams;
        std::vector<std::string> updated, removed;
        int set_group_calls = 0, apply_teams_calls = 0;
        json live;

        explicit FakeGame(std::string instance = "TEST-PC_1") : id(std::move(instance)) {}
        std::string InstanceId() override { return id; }
        int SlotCount() override { return slots; }
        std::string SlotProfile(int s) override { return slot[s]; }
        void SetGroupProfiles(const std::vector<RingChief::V5Profile>& list) override { profiles = list; set_group_calls++; }
        void UpdateProfile(const RingChief::V5Profile& p) override {
            bool found = false;
            for (auto& x : profiles) if (x.id == p.id) { x = p; found = true; }
            if (!found) profiles.push_back(p);
            updated.push_back(p.id);
        }
        void RemoveProfile(const std::string& id) override {
            removed.push_back(id);
            profiles.erase(std::remove_if(profiles.begin(), profiles.end(), [&](const RingChief::V5Profile& p) { return p.id == id; }), profiles.end());
        }
        void AssignSlot(int s, const std::string& pid) override { slot[s] = pid; }
        void ApplyTeams(const std::map<std::string, int>& t) override { teams = t; apply_teams_calls++; }
        json LiveStats() override { return live.is_null() ? json{{"inGame", false}} : live; }
    };

    inline json Profile(const std::string& id, const std::string& gamertag, json extra = json::object()) {
        json p{{"id", id}, {"gamertag", gamertag}, {"serviceTag", "TEST"}, {"teamPreference", 0},
               {"armor", json::object()}, {"controls", {{"preset", 0}}}};
        for (auto& [k, v] : extra.items()) p[k] = v;
        return p;
    }

    inline json Welcome(const std::vector<json>& profiles, json night = nullptr, const std::string& group = "testgroup") {
        return {{"type", "welcome"}, {"protocol", 2}, {"group", {{"id", group}, {"name", "TestGroup"}}},
                {"night", night}, {"profiles", profiles}, {"serverTime", "2026-09-26T00:00:00Z"}};
    }
}
