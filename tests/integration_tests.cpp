// End-to-end: two Ring Chief sessions (hub + follower, as two MCC instances on one PC)
// against a real halo web server. Driven by ringchief/tests/alpharing-integration.js,
// which starts the server, seeds a group and plays the phone/host side. Skipped unless
// RC_IT_SERVER is set.
#include "doctest.h"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>

#include "ringchief/Session.h"

using namespace RingChief;
using json = nlohmann::json;

namespace {
    std::string Env(const char* name) {
        char* v = nullptr;
        size_t n = 0;
        if (_dupenv_s(&v, &n, name) != 0 || !v) return "";
        std::string s(v);
        free(v);
        return s;
    }

    double Now() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    struct FakeGame : GameAdapter {
        std::string id;
        std::vector<V5Profile> profiles;
        std::string slot[4];
        std::map<std::string, int> teams;
        std::string last_update_name;
        json live;

        explicit FakeGame(std::string instance) : id(std::move(instance)) {}
        std::string InstanceId() override { return id; }
        int SlotCount() override { return 1; }
        std::string SlotProfile(int s) override { return slot[s]; }
        void SetGroupProfiles(const std::vector<V5Profile>& list) override { profiles = list; }
        void UpdateProfile(const V5Profile& p) override {
            for (auto& x : profiles) if (x.id == p.id) x = p;
            std::string name(p.gamertag.begin(), p.gamertag.end());
            last_update_name = name;
        }
        void RemoveProfile(const std::string&) override {}
        void AssignSlot(int s, const std::string& pid) override { slot[s] = pid; }
        void ApplyTeams(const std::map<std::string, int>& t) override { teams = t; }
        json LiveStats() override { return live.is_null() ? json{{"inGame", false}} : live; }
    };

    bool RunUntil(Session& a, Session& b, const std::function<bool()>& done, double seconds) {
        double end = Now() + seconds;
        while (Now() < end) {
            a.Tick(Now());
            b.Tick(Now());
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }
}

TEST_CASE("integration: hub + follower against the halo server" * doctest::skip(Env("RC_IT_SERVER").empty())) {
    const std::string server = Env("RC_IT_SERVER");
    const std::string group = Env("RC_IT_GROUP");
    const std::string password = Env("RC_IT_PASSWORD");
    const std::string profile_x = Env("RC_IT_PROFILE_X");   // claimed + edited from "the phone"
    const std::string profile_y = Env("RC_IT_PROFILE_Y");   // picked in the hub's overlay
    const size_t expect_profiles = std::stoul(Env("RC_IT_EXPECT_PROFILES"));
    const uint16_t port = static_cast<uint16_t>(std::stoi(Env("RC_IT_PORT")));

    FakeGame hub_game("PC-A_hub"), fol_game("PC-A_follower");
    Session::Options opt;
    opt.local_port = port;
    opt.use_storage = false;
    opt.client = "ringchief-integration-test";
    opt.stats_interval_s = 0.1;
    Session hub(hub_game, opt), follower(fol_game, opt);
    hub.Start();
    follower.Start();
    REQUIRE(hub.GetRole() == LocalLink::Role::Hub);
    REQUIRE(follower.GetRole() == LocalLink::Role::Follower);
    CHECK(hub.WantsLoginPanel());
    CHECK_FALSE(follower.WantsLoginPanel());

    // Wrong password first: stays logged out with a message.
    hub.Login(server, group, "definitely-wrong", false);
    REQUIRE(RunUntil(hub, follower, [&] { return hub.GetState() == Session::State::LoggedOut && !hub.Error().empty(); }, 10));
    MESSAGE("wrong password -> " << hub.Error());

    hub.Login(server, group, password, false);
    REQUIRE(RunUntil(hub, follower, [&] {
        return hub.GetState() == Session::State::Online && hub_game.profiles.size() == expect_profiles &&
               fol_game.profiles.size() == expect_profiles;
    }, 15));
    CHECK(hub.GroupName() == group);
    hub_game.slot[0] = profile_y;
    hub.SlotsChanged();
    std::puts("RC_IT_READY");   // the driver now plays the phone and host parts
    std::fflush(stdout);

    // Phone: claims the follower's screen, edits the profile; host pushes teams.
    REQUIRE(RunUntil(hub, follower, [&] {
        return fol_game.slot[0] == profile_x && fol_game.last_update_name == "Updated Name" &&
               fol_game.teams.count(profile_x) && hub_game.teams.count(profile_x);
    }, 20));
    CHECK(fol_game.teams[profile_x] == 1);
    CHECK(hub_game.teams[profile_y] == 0);

    // A match: live stats, then both instances report the end.
    json players_hub = json::array({{{"slot", 0}, {"profileId", profile_y}, {"team", 0}, {"kills", 12}, {"deaths", 4}, {"assists", 2}, {"score", 12}}});
    json players_fol = json::array({{{"slot", 0}, {"profileId", profile_x}, {"team", 1}, {"kills", 4}, {"deaths", 12}, {"assists", 1}, {"score", 4}}});
    hub_game.live = {{"inGame", true}, {"game", "Halo 3"}, {"mode", "Team Slayer"}, {"map", "Guardian"}, {"matchEpoch", 1}, {"players", players_hub}};
    fol_game.live = {{"inGame", true}, {"game", "Halo 3"}, {"mode", "Team Slayer"}, {"map", "Guardian"}, {"matchEpoch", 1}, {"players", players_fol}};
    RunUntil(hub, follower, [] { return false; }, 1.0);
    json end{{"game", "Halo 3"}, {"mode", "Team Slayer"}, {"map", "Guardian"}, {"matchEpoch", 1}, {"winner", {{"team", 0}}}};
    json e1 = end; e1["players"] = players_hub;
    json e2 = end; e2["players"] = players_fol;
    hub_game.live = nullptr; fol_game.live = nullptr;
    hub.OnMatchEnd(e1);
    follower.OnMatchEnd(e2);
    RunUntil(hub, follower, [] { return false; }, 1.5);
    std::puts("RC_IT_MATCH_SENT");
    std::fflush(stdout);

    // The hub closes: the follower takes over with the handed-over token.
    hub.Shutdown();
    REQUIRE(RunUntil(follower, follower, [&] { return follower.GetRole() == LocalLink::Role::Hub && follower.GetState() == Session::State::Online; }, 15));
    CHECK(follower.Profiles().size() == expect_profiles);
    std::puts("RC_IT_FAILOVER_OK");
    std::fflush(stdout);
    follower.Logout();
    follower.Shutdown();
}
