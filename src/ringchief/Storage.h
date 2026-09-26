#pragma once
// On-disk state in %ProgramData%\RingChief (shared by every MCC instance on the PC, even
// when Nucleus redirects each instance's user folders):
//   login.json                          server, group, and the PC token (DPAPI-encrypted, never the password)
//   groups\<group>\cache.json           the latest copy of the group from the server
//   groups\<group>\backups\profiles-<utc time>.json   rolling local backups (newest kept)
// A backup is the same shape as the server's "welcome" frame plus "savedAt", so any backup
// can be played offline directly.
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace RingChief::Storage {
    using json = nlohmann::json;

    constexpr size_t kKeepBackups = 20;

    struct SavedLogin {
        std::string server;       // as typed / normalised, e.g. "halo.dronedude.app"
        std::string group_id;     // as typed, e.g. "WarGames2027"
        std::string token;        // decrypted PC token ("" if none)
        std::string expires_at;   // ISO time from the server
    };

    struct BackupInfo {
        std::string path;
        std::string saved_at;     // ISO UTC
        size_t profile_count = 0;
        uintmax_t bytes = 0;
    };

    std::string Dir();                           // creates it if needed; "" if unavailable
    void SetDirForTests(const std::string& dir); // "" = back to %ProgramData%\RingChief

    bool LoadLogin(SavedLogin& out);
    bool SaveLogin(const SavedLogin& login);     // token is encrypted with DPAPI
    void ClearLogin();

    // Folder-safe key for a group ID ("WarGames2027" -> "wargames2027").
    std::string GroupKey(const std::string& group_id);

    // A usable group snapshot: an object with a "profiles" array. Bad entries in the
    // array are tolerated (AlphaRing skips them); a missing/garbled file is not.
    bool IsValidSnapshot(const json& j);

    bool LoadGroupCache(const std::string& group_id, json& welcome);   // falls back to the newest good backup
    bool SaveGroupCache(const std::string& group_id, const json& welcome);

    // Writes a backup unless the profiles are identical to the newest one. Returns the new
    // file's path, or "" if nothing was written. Keeps the newest `keep` backups.
    std::string SaveBackup(const std::string& group_id, const json& welcome, size_t keep = kKeepBackups);
    std::vector<BackupInfo> ListBackups(const std::string& group_id);  // newest first, unreadable ones skipped
    bool LoadBackup(const std::string& path, json& welcome);

    // Exposed for tests.
    std::string Protect(const std::string& plain);    // DPAPI + base64
    std::string Unprotect(const std::string& b64);    // "" on failure
    std::string ContentHash(const json& welcome);     // hash of the profiles (ignores savedAt etc.)
}
