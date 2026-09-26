#pragma once
// On-disk state in %ProgramData%\RingChief (shared by every MCC instance on the PC, even
// when Nucleus redirects each instance's user folders):
//   login.json        server, group, and the PC token (DPAPI-encrypted, never the password)
//   group-cache.json  the last group the server sent, for "Play offline"
#include <string>
#include <nlohmann/json.hpp>

namespace RingChief::Storage {
    using json = nlohmann::json;

    struct SavedLogin {
        std::string server;       // as typed / normalised, e.g. "halo.dronedude.app"
        std::string group_id;     // as typed, e.g. "WarGames2027"
        std::string token;        // decrypted PC token ("" if none)
        std::string expires_at;   // ISO time from the server
    };

    std::string Dir();  // creates it if needed; "" if unavailable

    bool LoadLogin(SavedLogin& out);
    bool SaveLogin(const SavedLogin& login);  // token is encrypted with DPAPI
    void ClearLogin();

    bool LoadGroupCache(json& welcome);
    void SaveGroupCache(const json& welcome);

    // Exposed for tests.
    std::string Protect(const std::string& plain);    // DPAPI + base64
    std::string Unprotect(const std::string& b64);    // "" on failure
}
