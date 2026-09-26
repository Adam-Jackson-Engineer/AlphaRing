#pragma once
// Ring Chief profile v5 (from halo.dronedude.app) -> what AlphaRing applies to a player slot.
// Schema: web/schema/profile.schema.json in the halo repo.
#include <string>
#include <nlohmann/json.hpp>

#include "mcc/CUserProfile.h"
#include "mcc/CGamepadMapping.h"

namespace RingChief {
    using json = nlohmann::json;

    struct V5Profile {
        std::string id;
        std::wstring gamertag;
        wchar_t service_tag[5] = {0};
        int team_preference = 0;       // 0-7
        int controller_preset = 0;     // 0-5 named layout; 6 when a saved mapping applies (modded/custom)
        int base_preset = 0;           // the named layout as chosen on the website (0-5, or 6 for legacy custom)
        bool has_custom_mapping = false;
        CGamepadMapping custom_mapping{};
        int rank_xp = 0;
        json raw;                      // the full profile, used by ApplyToUserProfile
    };

    // Reads the identity/controls part of a v5 profile. Returns false (with err) if it
    // isn't usable (missing id or gamertag); tolerant of everything else.
    bool ParseV5(const json& j, V5Profile& out, std::string& err);

    // Builds the in-game MCC profile for a player: starts from `base` (the slot's
    // current profile), layers the stored MCC fields (`mcc`), then the v5 armor and
    // controls on top. Only fields the profile actually sets are changed.
    void ApplyToUserProfile(const json& v5, CUserProfile& base);

    // The full in-game profile for a v5 profile: the built-in baseline (DefaultProfile.h)
    // with the profile applied on top. Same input -> same result, whatever slot it's in.
    CUserProfile BuildUserProfile(const json& v5);
}
