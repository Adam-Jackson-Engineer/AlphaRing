// Session logic without the website: the test plays the hub on the loopback port and
// scripts exactly what AlphaRing would receive.
#include "test_util.h"

#include "ringchief/Storage.h"

using namespace RingChief;
using namespace testutil;

namespace {
    Session::Options Opts(uint16_t port, const std::string& storage_dir = "") {
        Session::Options o;
        o.local_port = port;
        o.use_storage = !storage_dir.empty();
        o.storage_dir = storage_dir;
        o.client = "session-tests";
        o.stats_interval_s = 0.05;
        o.backup_interval_s = 0.05;
        return o;
    }

    // The test's side of the loopback link, acting as the hub.
    struct ScriptedHub {
        LocalLink link;
        std::vector<json> received;
        std::vector<int> peers;
        explicit ScriptedHub(uint16_t port) { REQUIRE(link.Start(port) == LocalLink::Role::Hub); }
        void Poll() {
            std::vector<LocalLink::Incoming> in;
            std::vector<int> joined, left;
            bool lost = false;
            link.Poll(in, joined, left, lost);
            for (int p : joined) peers.push_back(p);
            for (auto& i : in) received.push_back(json::parse(i.line));
        }
        void Send(const json& msg) { link.Broadcast(msg.dump()); }
        int Count(const std::string& type) const {
            int n = 0;
            for (auto& m : received) if (m.value("type", "") == type) n++;
            return n;
        }
        json Last(const std::string& type) const {
            for (auto it = received.rbegin(); it != received.rend(); ++it) if (it->value("type", "") == type) return *it;
            return nullptr;
        }
    };

    // Runs both sides until done() or timeout.
    bool Run(Session& s, ScriptedHub& hub, const std::function<bool()>& done, double seconds = 3.0) {
        return PollUntil([&] { s.Tick(Now()); hub.Poll(); }, done, seconds);
    }

    struct StorageReset { ~StorageReset() { Storage::SetDirForTests(""); } };
}

TEST_SUITE("session") {

TEST_CASE("follower: says hello with its instance and slots") {
    uint16_t port = RandomPort();
    ScriptedHub hub(port);
    FakeGame game("PC_follower");
    game.slots = 2;
    game.slot[1] = "p_b";
    Session s(game, Opts(port));
    s.Start();
    REQUIRE(s.GetRole() == LocalLink::Role::Follower);
    CHECK(s.GetState() == Session::State::Follower);
    CHECK_FALSE(s.WantsLoginPanel());
    REQUIRE(Run(s, hub, [&] { return hub.Count("hello") == 1; }));
    json hello = hub.Last("hello");
    CHECK(hello["protocol"] == 2);
    CHECK(hello["client"] == "session-tests");
    REQUIRE(hello["instances"].size() == 1);
    CHECK(hello["instances"][0]["instanceId"] == "PC_follower");
    REQUIRE(hello["instances"][0]["slots"].size() == 2);
    CHECK(hello["instances"][0]["slots"][0]["profileId"].is_null());
    CHECK(hello["instances"][0]["slots"][1]["profileId"] == "p_b");
}

TEST_CASE("follower: welcome, live profile edits, removal, night and teams reach the game") {
    uint16_t port = RandomPort();
    ScriptedHub hub(port);
    FakeGame game("PC_f");
    Session s(game, Opts(port));
    s.Start();
    Run(s, hub, [&] { return !hub.peers.empty(); });

    hub.Send(Welcome({Profile("p_a", "Alpha"), Profile("p_b", "Bravo"), json{{"gamertag", "no id"}}},
                     {{"id", "n_1"}, {"name", "Friday"}, {"teams", {{"p_a", 0}, {"p_b", 1}}}}));
    REQUIRE(Run(s, hub, [&] { return game.profiles.size() == 2; }));   // the bad entry is skipped
    CHECK(s.GroupName() == "TestGroup");
    CHECK(s.NightName() == "Friday");
    CHECK(game.teams.at("p_b") == 1);

    hub.Send({{"type", "profile"}, {"profile", Profile("p_a", "Alpha Renamed")}});
    REQUIRE(Run(s, hub, [&] { return !game.updated.empty(); }));
    CHECK(game.profiles[0].gamertag == L"Alpha Renamed");
    CHECK(s.Profiles()[0].gamertag == L"Alpha Renamed");

    hub.Send({{"type", "profile"}, {"profile", Profile("p_new", "Newcomer")}});
    REQUIRE(Run(s, hub, [&] { return s.Profiles().size() == 3; }));

    hub.Send({{"type", "profileRemoved"}, {"profileId", "p_b"}});
    REQUIRE(Run(s, hub, [&] { return !game.removed.empty(); }));
    CHECK(s.Profiles().size() == 2);

    hub.Send({{"type", "teams"}, {"teams", {{"p_a", 3}, {"p_new", 9}}}});
    REQUIRE(Run(s, hub, [&] { return game.teams.count("p_new") == 1; }));
    CHECK(game.teams.at("p_a") == 3);
    CHECK(game.teams.at("p_new") == 7);   // clamped to the 8 teams

    hub.Send({{"type", "night"}, {"night", nullptr}});
    REQUIRE(Run(s, hub, [&] { return s.NightName().empty(); }));
}

TEST_CASE("follower: slot claims apply only to its own instance, then it reports the new slot") {
    uint16_t port = RandomPort();
    ScriptedHub hub(port);
    FakeGame game("PC_mine");
    game.slots = 2;
    Session s(game, Opts(port));
    s.Start();
    Run(s, hub, [&] { return hub.Count("hello") == 1; });
    hub.Send(Welcome({Profile("p_a", "Alpha")}));
    Run(s, hub, [&] { return game.profiles.size() == 1; });

    hub.Send({{"type", "assign"}, {"commandId", "c1"}, {"instanceId", "PC_other"}, {"slot", 0}, {"profileId", "p_a"}});
    hub.Send({{"type", "assign"}, {"commandId", "c2"}, {"instanceId", "PC_mine"}, {"slot", 7}, {"profileId", "p_a"}});
    Run(s, hub, [] { return false; }, 0.3);
    CHECK(game.slot[0].empty());

    hub.Send({{"type", "assign"}, {"commandId", "c3"}, {"instanceId", "PC_mine"}, {"slot", 1}, {"profileId", "p_a"}});
    REQUIRE(Run(s, hub, [&] { return game.slot[1] == "p_a"; }));
    REQUIRE(Run(s, hub, [&] { return hub.Count("instances") >= 1; }));
    CHECK(hub.Last("instances")["instances"][0]["slots"][1]["profileId"] == "p_a");
}

TEST_CASE("follower: slot changes from the overlay are reported once, not every frame") {
    uint16_t port = RandomPort();
    ScriptedHub hub(port);
    FakeGame game("PC_f");
    Session s(game, Opts(port));
    s.Start();
    Run(s, hub, [&] { return hub.Count("hello") == 1; });
    game.slot[0] = "p_x";
    REQUIRE(Run(s, hub, [&] { return hub.Count("instances") == 1; }));
    Run(s, hub, [] { return false; }, 0.3);
    CHECK(hub.Count("instances") == 1);
}

TEST_CASE("follower: live stats are sent while in a match, match results when it ends") {
    uint16_t port = RandomPort();
    ScriptedHub hub(port);
    FakeGame game("PC_f");
    Session s(game, Opts(port));
    s.Start();
    Run(s, hub, [&] { return hub.Count("hello") == 1; });
    Run(s, hub, [] { return false; }, 0.2);
    CHECK(hub.Count("stats") == 0);   // not in a match

    game.live = {{"inGame", true}, {"game", "Halo 3"}, {"players", json::array({{{"slot", 0}, {"kills", 2}}})}};
    REQUIRE(Run(s, hub, [&] { return hub.Count("stats") >= 2; }));
    json st = hub.Last("stats");
    CHECK(st["instanceId"] == "PC_f");
    CHECK(st["game"] == "Halo 3");

    s.OnMatchEnd({{"game", "Halo 3"}, {"players", json::array()}, {"winner", nullptr}});
    REQUIRE(Run(s, hub, [&] { return hub.Count("matchEnd") == 1; }));
    CHECK(hub.Last("matchEnd")["instanceId"] == "PC_f");
}

TEST_CASE("follower: session hand-over lets it take over as hub when the hub closes") {
    uint16_t port = RandomPort();
    FakeGame game("PC_f");
    Session::Options o = Opts(port);
    auto hub = std::make_unique<ScriptedHub>(port);
    Session s(game, o);
    s.Start();
    Run(s, *hub, [&] { return !hub->peers.empty(); });
    // A token for a server that isn't there: after take-over it keeps trying (Working/Reconnecting).
    hub->Send({{"type", "session"}, {"server", "http://127.0.0.1:1"}, {"groupId", "G"}, {"token", "tok"}});
    Run(s, *hub, [] { return false; }, 0.2);
    hub.reset();
    REQUIRE(PollUntil([&] { s.Tick(Now()); }, [&] { return s.GetRole() == LocalLink::Role::Hub; }));
    CHECK((s.GetState() == Session::State::Working || s.GetState() == Session::State::Reconnecting));
    CHECK_FALSE(s.WantsLoginPanel());
    s.Shutdown();
}

TEST_CASE("follower: without a hand-over token it asks for the password after taking over") {
    uint16_t port = RandomPort();
    FakeGame game("PC_f");
    auto hub = std::make_unique<ScriptedHub>(port);
    Session s(game, Opts(port));
    s.Start();
    Run(s, *hub, [&] { return !hub->peers.empty(); });
    hub.reset();
    REQUIRE(PollUntil([&] { s.Tick(Now()); }, [&] { return s.GetRole() == LocalLink::Role::Hub; }));
    CHECK(s.GetState() == Session::State::LoggedOut);
    CHECK(s.WantsLoginPanel());
}

TEST_CASE("login form: bad input is caught before any network call") {
    uint16_t port = RandomPort();
    FakeGame game;
    Session s(game, Opts(port));
    s.Start();
    REQUIRE(s.GetRole() == LocalLink::Role::Hub);
    CHECK(s.WantsLoginPanel());
    CHECK(s.ServerInput() == "halo.dronedude.app");

    s.Login("", "G", "pw", false);
    CHECK(s.GetState() == Session::State::LoggedOut);
    CHECK_FALSE(s.Error().empty());
    s.Login("halo.dronedude.app", "", "pw", false);
    CHECK(s.Error().find("group") != std::string::npos);
    s.Login("halo.dronedude.app", "G", "", false);
    CHECK(s.GetState() == Session::State::LoggedOut);
    s.Login("host:notaport", "G", "pw", false);
    CHECK(s.GetState() == Session::State::LoggedOut);
}

TEST_CASE("login to an unreachable server reports an error and returns to the form") {
    uint16_t port = RandomPort();
    FakeGame game;
    Session s(game, Opts(port));
    s.Start();
    s.Login("http://127.0.0.1:1", "G", "pw", false);
    CHECK(s.GetState() == Session::State::Working);
    REQUIRE(PollUntil([&] { s.Tick(Now()); }, [&] { return s.GetState() == Session::State::LoggedOut; }, 15));
    CHECK_FALSE(s.Error().empty());
    MESSAGE("unreachable -> " << s.Error());
}

TEST_CASE("Prefill fills the form only when nothing is saved") {
    uint16_t port = RandomPort();
    FakeGame game;
    Session s(game, Opts(port));
    s.Prefill("http://lan-box:3004", "WarGames2027");
    CHECK(s.ServerInput() == "http://lan-box:3004");
    CHECK(s.GroupInput() == "WarGames2027");
    s.Prefill("other", "Other");   // already filled: ignored
    CHECK(s.GroupInput() == "WarGames2027");
}

TEST_CASE("offline: plays from the latest local copy and serves it to other windows") {
    StorageReset reset;
    TempDir dir;
    uint16_t port = RandomPort();
    Storage::SetDirForTests(dir.str());
    REQUIRE(Storage::SaveGroupCache("WarGames2027", Welcome({Profile("p_a", "Alpha"), Profile("p_b", "Bravo")}, nullptr, "wargames2027")));

    FakeGame hub_game("PC_hub");
    Session hub(hub_game, Opts(port, dir.str()));
    hub.Prefill("halo.dronedude.app", "WarGames2027");
    hub.Start();
    REQUIRE(hub.GetRole() == LocalLink::Role::Hub);
    hub.PlayOffline();
    CHECK(hub.GetState() == Session::State::Offline);
    CHECK(hub_game.profiles.size() == 2);
    CHECK(hub.Error().empty());

    FakeGame fol_game("PC_fol");
    Session fol(fol_game, Opts(port));
    fol.Start();
    REQUIRE(fol.GetRole() == LocalLink::Role::Follower);
    REQUIRE(PollUntil([&] { hub.Tick(Now()); fol.Tick(Now()); }, [&] { return fol_game.profiles.size() == 2; }));
}

TEST_CASE("offline with nothing saved says so") {
    StorageReset reset;
    TempDir dir;
    uint16_t port = RandomPort();
    FakeGame game;
    Session s(game, Opts(port, dir.str()));
    s.Prefill("", "NeverSeen");
    s.Start();
    s.PlayOffline();
    CHECK(s.GetState() == Session::State::Offline);
    CHECK_FALSE(s.Error().empty());
    CHECK(game.profiles.empty());
}

TEST_CASE("backups: play from a chosen backup; Back up now only writes when something changed") {
    StorageReset reset;
    TempDir dir;
    uint16_t port = RandomPort();
    Storage::SetDirForTests(dir.str());
    std::string old_path = Storage::SaveBackup("testgroup", Welcome({Profile("p_a", "Old Name")}));
    std::string new_path = Storage::SaveBackup("testgroup", Welcome({Profile("p_a", "New Name"), Profile("p_b", "B")}));
    REQUIRE_FALSE(old_path.empty());
    REQUIRE_FALSE(new_path.empty());

    FakeGame game;
    Session s(game, Opts(port, dir.str()));
    s.Prefill("", "testgroup");
    s.Start();
    REQUIRE(s.Backups().size() == 2);
    REQUIRE(s.PlayFromBackup(old_path));
    CHECK(s.GetState() == Session::State::Offline);
    REQUIRE(game.profiles.size() == 1);
    CHECK(game.profiles[0].gamertag == L"Old Name");

    CHECK(s.BackupNow());            // "Old Name" differs from the newest backup -> written
    CHECK_FALSE(s.BackupNow());      // nothing changed since
    CHECK(s.Backups().size() == 3);
    CHECK_FALSE(s.LastBackupAt().empty());

    CHECK_FALSE(s.PlayFromBackup((dir.path / "nope.json").string()));
    CHECK_FALSE(s.Error().empty());
    CHECK(game.profiles.size() == 1);   // still on the last good group
}

TEST_CASE("OnMatchEnd while not connected is silently dropped") {
    uint16_t port = RandomPort();
    FakeGame game;
    Session s(game, Opts(port));
    s.Start();
    s.OnMatchEnd({{"players", json::array()}});   // LoggedOut hub: nothing to send, no crash
    s.Tick(Now());
    CHECK(s.GetState() == Session::State::LoggedOut);
}

}  // TEST_SUITE
