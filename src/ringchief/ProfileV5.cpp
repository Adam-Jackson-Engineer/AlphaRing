#include "ProfileV5.h"

#include <algorithm>
#include <cstring>

#include "mcc/CUserProfileJson.h"
#include "DefaultProfile.h"

namespace RingChief {

    namespace {
        struct SlotField { const char* slot; int CUserProfile::* field; };

        // v5 armor slot -> CUserProfile field (mirrors catalog/armor.json in the web repo).
        const SlotField kArmorSlots[] = {
            {"helmet", &CUserProfile::HelmetIndex},
            {"leftShoulder", &CUserProfile::LeftShoulderIndex},
            {"rightShoulder", &CUserProfile::RightShoulderIndex},
            {"chest", &CUserProfile::ChestIndex},
            {"wrist", &CUserProfile::WristIndex},
            {"utility", &CUserProfile::UtilityIndex},
            {"arms", &CUserProfile::ArmsIndex},
            {"legs", &CUserProfile::LegsIndex},
            {"backpack", &CUserProfile::BackpackIndex},
            {"knees", &CUserProfile::KneesIndex},
            {"visorColor", &CUserProfile::VisorColorIndex},
            {"armorEffect", &CUserProfile::SpartanArmorEffectIndex},
            {"voice", &CUserProfile::VoiceIndex},
            {"spartanPose", &CUserProfile::SpartanPose},
            {"eliteHelmet", &CUserProfile::EliteHelmetIndex},
            {"eliteLeftShoulder", &CUserProfile::EliteLeftShoulderIndex},
            {"eliteRightShoulder", &CUserProfile::EliteRightShoulderIndex},
            {"eliteChest", &CUserProfile::EliteChestIndex},
            {"eliteArms", &CUserProfile::EliteArmsIndex},
            {"eliteLegs", &CUserProfile::EliteLegsIndex},
            {"eliteArmor", &CUserProfile::EliteArmorIndex},
            {"eliteArmorEffect", &CUserProfile::EliteArmorEffectIndex},
            {"elitePose", &CUserProfile::ElitePose},
        };

        int IntOr(const json& j, const char* key, int dflt) {
            auto it = j.find(key);
            return (it != j.end() && it->is_number_integer()) ? it->get<int>() : dflt;
        }

        bool IsInt(const json& j, const char* key) {
            auto it = j.find(key);
            return it != j.end() && it->is_number_integer();
        }

        template <typename T>
        void SetBool(const json& j, const char* key, T& field, bool invert = false) {
            auto it = j.find(key);
            if (it != j.end() && it->is_boolean()) field = invert ? !it->get<bool>() : it->get<bool>();
        }

        void SetU8(const json& j, const char* key, uint8_t& field, int lo, int hi) {
            if (IsInt(j, key)) field = static_cast<uint8_t>(std::clamp(j[key].get<int>(), lo, hi));
        }

        void SetFloat(const json& j, const char* key, float& field) {
            auto it = j.find(key);
            if (it != j.end() && it->is_number()) field = std::clamp(it->get<float>(), 0.0f, 1.0f);
        }
    }

    bool ParseV5(const json& j, V5Profile& out, std::string& err) {
        if (!j.is_object()) { err = "profile is not an object"; return false; }
        if (!j.contains("id") || !j["id"].is_string()) { err = "profile has no id"; return false; }
        if (!j.contains("gamertag") || !j["gamertag"].is_string()) { err = "profile has no gamertag"; return false; }

        out = V5Profile{};
        out.id = j["id"].get<std::string>();
        out.gamertag = MCC::Splitscreen::utf8_to_wstring(j["gamertag"].get<std::string>());
        if (out.gamertag.empty()) out.gamertag = L"Spartan";

        std::string tag = j.value("serviceTag", "");
        for (size_t i = 0; i < 4 && i < tag.size(); i++) out.service_tag[i] = static_cast<wchar_t>(tag[i]);

        out.team_preference = std::clamp(IntOr(j, "teamPreference", 0), 0, 7);
        out.rank_xp = std::max(0, IntOr(j, "rankXp", 0));

        if (j.contains("controls") && j["controls"].is_object()) {
            const auto& c = j["controls"];
            out.controller_preset = std::clamp(IntOr(c, "preset", 0), 0, 6);
            out.base_preset = out.controller_preset;
            auto m = c.find("customMapping");
            if (m != c.end() && m->is_array() && m->size() == 66) {
                out.has_custom_mapping = true;
                for (size_t i = 0; i < 66; i++) {
                    int b = (*m)[i].is_number_integer() ? (*m)[i].get<int>() : 0;
                    out.custom_mapping.actions[i] = static_cast<CGamepadMapping::eButton>(std::clamp(b, 0, 15));
                }
            }
        }
        // A saved mapping is the player's layout tweaked on the website ("Modded Zoom & Shoot"):
        // it wins over the named layout. Without one, the named layout applies as-is.
        if (out.has_custom_mapping) out.controller_preset = 6;
        else if (out.controller_preset == 6) out.controller_preset = 0;

        out.raw = j;
        return true;
    }

    void ApplyToUserProfile(const json& v5, CUserProfile& up) {
        // 1. Stored MCC fields (from imported v4 profiles). Only present keys change.
        if (v5.contains("mcc") && v5["mcc"].is_object()) {
            MCC::Splitscreen::merge_json(v5["mcc"], up);
        }

        // 2. Armor. null / missing = keep what the game (or step 1) has.
        if (v5.contains("armor") && v5["armor"].is_object()) {
            const auto& a = v5["armor"];
            SetBool(a, "useElite", up.UseEliteModel);
            SetBool(a, "femaleVoice", up.UseFemaleVoice);
            for (const auto& s : kArmorSlots) {
                if (IsInt(a, s.slot)) up.*(s.field) = std::clamp(a[s.slot].get<int>(), 0, 2130);
            }
            // Colours are stored twice in CUserProfile; keep both in step.
            if (IsInt(a, "primaryColor")) up.PlayerModelPrimaryColor = up.PlayerModelPrimaryColorIndex = a["primaryColor"].get<int>();
            if (IsInt(a, "secondaryColor")) up.PlayerModelSecondaryColor = up.PlayerModelSecondaryColorIndex = a["secondaryColor"].get<int>();
            if (IsInt(a, "tertiaryColor")) up.PlayerModelTertiaryColor = up.PlayerModelTertiaryColorIndex = a["tertiaryColor"].get<int>();
        }

        // 3. Controls.
        if (v5.contains("controls") && v5["controls"].is_object()) {
            const auto& c = v5["controls"];
            SetU8(c, "lookSensitivityH", up.HorizontalLookSensitivity, 1, 10);
            SetU8(c, "lookSensitivityV", up.VerticalLookSensitivity, 1, 10);
            SetU8(c, "lookAcceleration", up.LookAcceleration, 1, 5);
            SetFloat(c, "axialDeadZone", up.LookAxialDeadZone);
            SetFloat(c, "radialDeadZone", up.LookRadialDeadZone);
            SetBool(c, "invertLook", up.LookControlsInverted);
            SetBool(c, "invertAircraft", up.AircraftControlsInverted);
            SetBool(c, "vibration", up.VibrationDisabled, /*invert=*/true);
            SetBool(c, "crouchLock", up.CrouchLockEnabled);
        }

        // 4. Service tag (4 chars, not null-terminated in MCC's struct).
        std::string tag = v5.value("serviceTag", "");
        if (!tag.empty()) {
            for (int i = 0; i < 4; i++) up.ServiceTag[i] = i < (int)tag.size() ? static_cast<wchar_t>(tag[i]) : L'\0';
        }
    }

    CUserProfile BuildUserProfile(const json& v5) {
        static const json kDefault = json::parse(kDefaultUserProfileJson);
        CUserProfile up;
        MCC::Splitscreen::from_json(kDefault, up);
        ApplyToUserProfile(v5, up);
        return up;
    }
}
