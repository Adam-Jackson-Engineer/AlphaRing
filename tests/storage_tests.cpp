// Local storage: saved login (DPAPI), per-group cache, rolling profile backups.
#include "test_util.h"

#include "ringchief/Storage.h"

using namespace RingChief;
using namespace testutil;

namespace {
    // Points Storage at a temp folder for the duration of a test.
    struct StorageSandbox {
        TempDir dir;
        StorageSandbox() { Storage::SetDirForTests(dir.str()); }
        ~StorageSandbox() { Storage::SetDirForTests(""); }
    };

    std::string ReadFile(const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
}

TEST_SUITE("storage") {

TEST_CASE("GroupKey normalises group IDs to safe folder names") {
    CHECK(Storage::GroupKey("WarGames2027") == "wargames2027");
    CHECK(Storage::GroupKey("War Games 2027!") == "wargames2027");
    CHECK(Storage::GroupKey("lan_night-1") == "lan_night-1");
    CHECK(Storage::GroupKey("..\\..\\Windows") == "windows");     // no path traversal
    CHECK(Storage::GroupKey("") == "");
    CHECK(Storage::GroupKey(std::string(200, 'a')).size() == 64);
}

TEST_CASE("IsValidSnapshot needs an object with a profiles array") {
    CHECK(Storage::IsValidSnapshot(json{{"profiles", json::array()}}));
    CHECK(Storage::IsValidSnapshot(Welcome({Profile("p_1", "A")})));
    CHECK_FALSE(Storage::IsValidSnapshot(json::array()));
    CHECK_FALSE(Storage::IsValidSnapshot(json{{"profiles", "nope"}}));
    CHECK_FALSE(Storage::IsValidSnapshot(json{{"group", "x"}}));
}

TEST_CASE("login round-trips and the token is never stored in plain text") {
    StorageSandbox sb;
    Storage::SavedLogin in{"halo.dronedude.app", "WarGames2027", "secret-token-123", "2026-10-10T00:00:00Z"};
    REQUIRE(Storage::SaveLogin(in));
    std::string raw = ReadFile(sb.dir.path / "login.json");
    CHECK(raw.find("secret-token-123") == std::string::npos);
    CHECK(raw.find("WarGames2027") != std::string::npos);

    Storage::SavedLogin out;
    REQUIRE(Storage::LoadLogin(out));
    CHECK(out.server == in.server);
    CHECK(out.group_id == in.group_id);
    CHECK(out.token == in.token);
    CHECK(out.expires_at == in.expires_at);

    Storage::ClearLogin();
    CHECK_FALSE(Storage::LoadLogin(out));
}

TEST_CASE("login without a token (Remember unticked) keeps just the server and group") {
    StorageSandbox sb;
    REQUIRE(Storage::SaveLogin({"halo.dronedude.app", "WarGames2027", "", ""}));
    Storage::SavedLogin out;
    REQUIRE(Storage::LoadLogin(out));
    CHECK(out.token.empty());
    CHECK(out.group_id == "WarGames2027");
}

TEST_CASE("a corrupt or tampered login file is rejected, not crashed on") {
    StorageSandbox sb;
    { std::ofstream(sb.dir.path / "login.json") << "{ not json"; }
    Storage::SavedLogin out;
    CHECK_FALSE(Storage::LoadLogin(out));
    { std::ofstream(sb.dir.path / "login.json") << R"({"server":"s","groupId":"g","token":"AAAAtampered"})"; }
    REQUIRE(Storage::LoadLogin(out));
    CHECK(out.token.empty());   // DPAPI refuses tampered data
}

TEST_CASE("group cache is per group") {
    StorageSandbox sb;
    REQUIRE(Storage::SaveGroupCache("WarGames2027", Welcome({Profile("p_a", "Alpha")})));
    REQUIRE(Storage::SaveGroupCache("OtherGroup", Welcome({Profile("p_b", "Bravo"), Profile("p_c", "Charlie")})));
    json a, b, none;
    REQUIRE(Storage::LoadGroupCache("wargames2027", a));   // case-insensitive
    REQUIRE(Storage::LoadGroupCache("OtherGroup", b));
    CHECK(a["profiles"].size() == 1);
    CHECK(b["profiles"].size() == 2);
    CHECK(a.contains("savedAt"));
    CHECK_FALSE(Storage::LoadGroupCache("NoSuchGroup", none));
    CHECK_FALSE(Storage::SaveGroupCache("", Welcome({})));
    CHECK_FALSE(Storage::SaveGroupCache("g", json{{"not", "a snapshot"}}));
}

TEST_CASE("backups: written once per distinct content, newest first") {
    StorageSandbox sb;
    json w1 = Welcome({Profile("p_a", "Alpha")});
    std::string f1 = Storage::SaveBackup("G", w1);
    REQUIRE_FALSE(f1.empty());
    CHECK(Storage::SaveBackup("G", w1).empty());          // unchanged: no new file
    json w2 = Welcome({Profile("p_a", "Alpha Renamed")});
    std::string f2 = Storage::SaveBackup("G", w2);
    REQUIRE_FALSE(f2.empty());
    json w3 = w2;
    w3["night"] = {{"id", "n_1"}, {"name", "Friday"}, {"teams", {{"p_a", 1}}}};
    CHECK_FALSE(Storage::SaveBackup("G", w3).empty());    // night/teams count as a change

    auto list = Storage::ListBackups("G");
    REQUIRE(list.size() == 3);
    CHECK(list[0].path > list[1].path);                   // newest first
    CHECK(list[1].path == f2);
    CHECK(list[2].path == f1);
    CHECK(list[0].profile_count == 1);
    CHECK_FALSE(list[0].saved_at.empty());
    CHECK(list[0].bytes > 0);

    json loaded;
    REQUIRE(Storage::LoadBackup(f2, loaded));
    CHECK(loaded["profiles"][0]["gamertag"] == "Alpha Renamed");
    CHECK(loaded["type"] == "welcome");                   // playable offline as-is
}

TEST_CASE("backups: only the newest N are kept") {
    StorageSandbox sb;
    for (int i = 0; i < 25; i++) {
        REQUIRE_FALSE(Storage::SaveBackup("G", Welcome({Profile("p_a", "Alpha " + std::to_string(i))}), 20).empty());
    }
    auto list = Storage::ListBackups("G");
    REQUIRE(list.size() == 20);
    json newest;
    REQUIRE(Storage::LoadBackup(list.front().path, newest));
    CHECK(newest["profiles"][0]["gamertag"] == "Alpha 24");
    json oldest;
    REQUIRE(Storage::LoadBackup(list.back().path, oldest));
    CHECK(oldest["profiles"][0]["gamertag"] == "Alpha 5");
}

TEST_CASE("backups: corrupt files are skipped, and a broken cache falls back to the newest good backup") {
    StorageSandbox sb;
    std::string good = Storage::SaveBackup("G", Welcome({Profile("p_a", "Good")}));
    REQUIRE_FALSE(good.empty());
    fs::path backups = fs::path(good).parent_path();
    { std::ofstream(backups / "profiles-99999999-999999-999.json") << "{ truncated"; }
    { std::ofstream(backups / "profiles-99999999-999999-998.json") << R"({"profiles": "wrong"})"; }
    { std::ofstream(backups / "notes.txt") << "ignore me"; }
    auto list = Storage::ListBackups("G");
    REQUIRE(list.size() == 1);
    CHECK(list[0].path == good);

    fs::path cache = fs::path(sb.dir.path) / "groups" / "g" / "cache.json";
    fs::create_directories(cache.parent_path());
    { std::ofstream(cache) << "garbage"; }
    json w;
    REQUIRE(Storage::LoadGroupCache("G", w));
    CHECK(w["profiles"][0]["gamertag"] == "Good");
    CHECK_FALSE(Storage::LoadBackup((backups / "profiles-99999999-999999-999.json").string(), w));
    CHECK_FALSE(Storage::LoadBackup((backups / "missing.json").string(), w));
}

TEST_CASE("ContentHash ignores savedAt but sees profile and night changes") {
    json a = Welcome({Profile("p_a", "A")});
    json b = a;
    b["savedAt"] = "2030-01-01T00:00:00Z";
    b["serverTime"] = "different";
    CHECK(Storage::ContentHash(a) == Storage::ContentHash(b));
    json c = a;
    c["profiles"][0]["armor"]["helmet"] = 3;
    CHECK(Storage::ContentHash(a) != Storage::ContentHash(c));
    json d = a;
    d["night"] = {{"teams", {{"p_a", 1}}}};
    CHECK(Storage::ContentHash(a) != Storage::ContentHash(d));
}

TEST_CASE("writes are atomic: no .tmp files left behind") {
    StorageSandbox sb;
    Storage::SaveGroupCache("G", Welcome({Profile("p_a", "A")}));
    Storage::SaveBackup("G", Welcome({Profile("p_a", "A")}));
    Storage::SaveLogin({"s", "G", "t", ""});
    int tmp = 0;
    for (auto& e : fs::recursive_directory_iterator(sb.dir.path)) if (e.path().extension() == ".tmp") tmp++;
    CHECK(tmp == 0);
}

}  // TEST_SUITE
