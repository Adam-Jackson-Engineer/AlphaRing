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
#include "ringchief/Storage.h"
#include "test_util.h"

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
        int welcomes = 0;
        void SetGroupProfiles(const std::vector<V5Profile>& list) override { profiles = list; welcomes++; }
        void UpdateProfile(const V5Profile& p) override {
            for (auto& x : profiles) if (x.id == p.id) x = p;
            std::string name(p.gamertag.begin(), p.gamertag.end());
            last_update_name = name;
        }
        std::vector<std::string> removed;
        void RemoveProfile(const std::string& id) override { removed.push_back(id); }
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
    testutil::TempDir storage;
    struct Reset { ~Reset() { Storage::SetDirForTests(""); } } reset;
    Session::Options opt;
    opt.local_port = port;
    opt.use_storage = false;
    opt.client = "ringchief-integration-test";
    opt.stats_interval_s = 0.1;
    opt.backup_interval_s = 0.1;
    Session::Options hub_opt = opt;
    hub_opt.use_storage = true;
    hub_opt.storage_dir = storage.str();
    Session hub(hub_game, hub_opt), follower(fol_game, opt);
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
    const std::string group_key = Storage::GroupKey(group);
    REQUIRE(RunUntil(hub, follower, [&] { return Storage::ListBackups(group_key).size() == 1; }, 5));
    json cached;
    REQUIRE(Storage::LoadGroupCache(group_key, cached));
    CHECK(cached["profiles"].size() == expect_profiles);
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
    REQUIRE(RunUntil(hub, follower, [&] {
        auto list = Storage::ListBackups(group_key);
        json newest;
        return list.size() >= 2 && Storage::LoadBackup(list.front().path, newest) && newest.dump().find("Updated Name") != std::string::npos;
    }, 5));

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

    // Host removes a player and ends the night: both windows hear about it.
    std::puts("RC_IT_WAIT_REMOVE");
    std::fflush(stdout);
    REQUIRE(RunUntil(hub, follower, [&] {
        auto has = [&](const std::vector<std::string>& v) { return std::find(v.begin(), v.end(), profile_y) != v.end(); };
        return has(hub_game.removed) && has(fol_game.removed) && follower.NightName().empty() && hub.NightName().empty();
    }, 15));
    CHECK(hub.Profiles().size() == expect_profiles - 1);

    // The server drops the connection: the hub reconnects by itself and gets the group again.
    int welcomes_before = hub_game.welcomes;
    std::puts("RC_IT_WAIT_DROP");
    std::fflush(stdout);
    REQUIRE(RunUntil(hub, follower, [&] { return hub.GetState() == Session::State::Reconnecting; }, 10));
    REQUIRE(RunUntil(hub, follower, [&] { return hub.GetState() == Session::State::Online && hub_game.welcomes > welcomes_before; }, 15));
    std::puts("RC_IT_RECONNECT_OK");
    std::fflush(stdout);

    // The hub window closes: the follower takes over with the handed-over token.
    hub.Shutdown();
    REQUIRE(RunUntil(follower, follower, [&] { return follower.GetRole() == LocalLink::Role::Hub && follower.GetState() == Session::State::Online; }, 15));
    CHECK(follower.Profiles().size() == expect_profiles - 1);
    std::puts("RC_IT_FAILOVER_OK");
    std::fflush(stdout);

    // The host changes the group password: this PC's login stops working and it says so.
    std::puts("RC_IT_WAIT_REVOKE");
    std::fflush(stdout);
    REQUIRE(RunUntil(follower, follower, [&] { return follower.GetState() == Session::State::LoggedOut; }, 20));
    CHECK(follower.Error().find("signed out") != std::string::npos);
    CHECK(follower.WantsLoginPanel());
    std::puts("RC_IT_REVOKED_OK");
    std::fflush(stdout);
    follower.Shutdown();
}

// Sign in with a code, no group; then the phone remote and the in-game group tools.
TEST_CASE("pairing: code sign-in, remote and group tools against the halo server" * doctest::skip(Env("RC_PAIR_SERVER").empty())) {
    const std::string server = Env("RC_PAIR_SERVER");
    const uint16_t port = static_cast<uint16_t>(std::stoi(Env("RC_IT_PORT")));
    testutil::TempDir storage;
    struct Reset { ~Reset() { Storage::SetDirForTests(""); } } reset;
    Session::Options opt;
    opt.local_port = port;
    opt.use_storage = true;
    opt.storage_dir = storage.str();
    testutil::FakeGame game("PAIR-PC_1");
    game.slots = 2;
    game.config = {{"enabled", true}, {"overlay", true}, {"inGame", false}, {"keyboardP1", false}, {"players", 2},
                   {"controllers", {0, 1, 2, 3}}, {"connected", {true, false, false, false}}};
    auto say = [](const std::string& text) { std::puts(text.c_str()); std::fflush(stdout); };
    {
        Session s(game, opt);
        s.Start();
        auto run = [&](const std::function<bool()>& done, double sec = 15) {
            return testutil::PollUntil([&] { s.Tick(Now()); }, done, sec);
        };
        // Cancel goes back to the start at once; a second try works.
        s.LoginWithCode(server, true);
        REQUIRE(run([&] { return !s.PairCode().empty(); }));
        s.CancelLogin();
        CHECK(s.GetState() == Session::State::LoggedOut);
        s.LoginWithCode(server, true);
        REQUIRE(run([&] { return !s.PairCode().empty(); }));
        say("RC_PAIR_CODE " + s.PairCode());
        REQUIRE(run([&] { return s.GetState() == Session::State::Online && s.Pc().is_object(); }));
        CHECK(s.PairCode().empty());
        CHECK(s.GroupName() == "No group");
        REQUIRE(s.Profiles().size() == 1);
        say("RC_PAIR_SCREEN " + s.Pc().value("code", ""));
        REQUIRE(run([&] { return game.controls.size() == 1; }));       // phone: players, seats
        CHECK(game.controls[0]["players"] == 3);
        say("RC_PAIR_CONTROLLED");
        REQUIRE(run([&] { return s.Profiles().size() == 2; }));        // a friend typed the screen code
        REQUIRE(s.Pc()["groups"].size() == 1);
        s.SwitchGroup(s.Pc()["groups"][0]["id"].get<std::string>());
        REQUIRE(run([&] { return s.Pc().value("canManage", false); }));
        CHECK(s.GroupName() == "PairGroup");
        s.AddPlayer("From The Couch");
        REQUIRE(run([&] { return s.Notice().find("code ") != std::string::npos; }));
        s.AddPlayer("bad<name>");
        REQUIRE(run([&] { return s.Notice().find("code ") == std::string::npos && !s.Notice().empty(); }));
        say("RC_PAIR_MANAGED");
    }
    {   // next launch: signed straight back in, same group
        Session s(game, opt);
        s.Start();
        REQUIRE(testutil::PollUntil([&] { s.Tick(Now()); }, [&] { return s.GetState() == Session::State::Online && s.Pc().is_object(); }, 15));
        CHECK(s.GroupName() == "PairGroup");
        say("RC_PAIR_RESUMED");
        // the owner signs this PC out from the phone
        REQUIRE(testutil::PollUntil([&] { s.Tick(Now()); }, [&] { return s.GetState() == Session::State::LoggedOut; }, 20));
        CHECK(s.Error().find("signed out") != std::string::npos);
    }
}
