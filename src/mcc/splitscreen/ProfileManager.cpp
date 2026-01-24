#include "ProfileManager.h"
#include "InstanceConfig.h"
#include "common.h"
#include "../CGameManager.h"
#include "../mcc.h"
#include "../../global/Global.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>

#include "imgui.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// Random number generator
static std::mt19937& GetRNG() {
    static std::mt19937 rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return rng;
}

namespace MCC::Splitscreen {

    std::vector<PersistentProfile> ProfileManager::profiles;
    int ProfileManager::selected_profile_index[4] = {-1, -1, -1, -1};
    bool ProfileManager::s_auto_load_attempted = false;

    // Emblem constructor
    EmblemConfig::EmblemConfig()
        : foreground(0), background(0), flags(0),
          primary_color(0), secondary_color(0), background_color(0) {}

    void EmblemConfig::Randomize() {
        std::uniform_int_distribution<int> fg_dist(0, NUM_EMBLEM_FOREGROUNDS - 1);
        std::uniform_int_distribution<int> bg_dist(0, NUM_EMBLEM_BACKGROUNDS - 1);
        std::uniform_int_distribution<int> color_dist(0, NUM_EMBLEM_COLORS - 1);
        std::uniform_int_distribution<int> flag_dist(0, 3);

        foreground = fg_dist(GetRNG());
        background = bg_dist(GetRNG());
        flags = flag_dist(GetRNG());
        primary_color = color_dist(GetRNG());
        secondary_color = color_dist(GetRNG());
        background_color = color_dist(GetRNG());
    }

    PersistentProfile::PersistentProfile()
        : filename(""), display_name(L"New Profile"), rank_xp(0), rank_level(1),
          controller_preset(ControllerPreset::Default), team_preference(Team::Red) {
        memset(service_tag, 0, sizeof(service_tag));
        service_tag[0] = L'N';
        service_tag[1] = L'E';
        service_tag[2] = L'W';
        service_tag[3] = L'B';
        memset(&user_profile, 0, sizeof(user_profile));
        memset(&gamepad_mapping, 0, sizeof(gamepad_mapping));
    }

    // Armor piece limits for randomization (conservative estimates that work across games)
    // These are approximate counts - some games have more/fewer options
    constexpr int NUM_HELMETS = 90;
    constexpr int NUM_SHOULDERS = 20;
    constexpr int NUM_CHESTS = 20;
    constexpr int NUM_ARMS = 10;
    constexpr int NUM_LEGS = 10;
    constexpr int NUM_VISORS = 17;
    constexpr int NUM_EFFECTS = 10;

    // Individual randomizer functions
    static int RandomColor() {
        std::uniform_int_distribution<int> dist(0, NUM_ARMOR_COLORS - 1);
        return dist(GetRNG());
    }

    static int RandomArmor(int max) {
        std::uniform_int_distribution<int> dist(0, max - 1);
        return dist(GetRNG());
    }

    static wchar_t RandomTagChar() {
        const wchar_t* chars = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::uniform_int_distribution<int> dist(0, 35);
        return chars[dist(GetRNG())];
    }

    void PersistentProfile::RandomizeAppearance() {
        std::uniform_int_distribution<int> bool_dist(0, 1);

        // Randomize colors
        user_profile.PlayerModelPrimaryColorIndex = RandomColor();
        user_profile.PlayerModelSecondaryColorIndex = RandomColor();
        user_profile.PlayerModelTertiaryColorIndex = RandomColor();
        user_profile.PlayerModelPrimaryColor = user_profile.PlayerModelPrimaryColorIndex;
        user_profile.PlayerModelSecondaryColor = user_profile.PlayerModelSecondaryColorIndex;
        user_profile.PlayerModelTertiaryColor = user_profile.PlayerModelTertiaryColorIndex;

        // Random Spartan vs Elite
        user_profile.UseEliteModel = bool_dist(GetRNG()) == 1;

        // Randomize visor
        user_profile.VisorColorIndex = RandomArmor(NUM_VISORS);

        // Randomize Spartan armor pieces
        user_profile.HelmetIndex = RandomArmor(NUM_HELMETS);
        user_profile.LeftShoulderIndex = RandomArmor(NUM_SHOULDERS);
        user_profile.RightShoulderIndex = RandomArmor(NUM_SHOULDERS);
        user_profile.ChestIndex = RandomArmor(NUM_CHESTS);
        user_profile.ArmsIndex = RandomArmor(NUM_ARMS);
        user_profile.LegsIndex = RandomArmor(NUM_LEGS);
        user_profile.WristIndex = RandomArmor(10);
        user_profile.UtilityIndex = RandomArmor(10);
        user_profile.KneesIndex = RandomArmor(10);

        // Randomize Elite armor pieces
        user_profile.EliteHelmetIndex = RandomArmor(20);
        user_profile.EliteLeftShoulderIndex = RandomArmor(10);
        user_profile.EliteRightShoulderIndex = RandomArmor(10);
        user_profile.EliteChestIndex = RandomArmor(10);
        user_profile.EliteArmsIndex = RandomArmor(5);
        user_profile.EliteLegsIndex = RandomArmor(5);
        user_profile.EliteArmorIndex = RandomArmor(5);

        // Randomize emblem
        emblem.Randomize();

        // Generate a random 4-char service tag
        for (int i = 0; i < 4; i++) {
            service_tag[i] = RandomTagChar();
        }
        service_tag[4] = L'\0';

        // Copy service tag to user_profile as well
        for (int i = 0; i < 4; i++) {
            user_profile.ServiceTag[i] = service_tag[i];
        }

        char tag_buf[8];
        for (int i = 0; i < 4; i++) tag_buf[i] = static_cast<char>(service_tag[i]);
        tag_buf[4] = '\0';

        LOG_INFO("Randomized appearance: colors=({},{},{}), elite={}, helmet={}, tag={}",
            user_profile.PlayerModelPrimaryColorIndex,
            user_profile.PlayerModelSecondaryColorIndex,
            user_profile.PlayerModelTertiaryColorIndex,
            user_profile.UseEliteModel,
            user_profile.HelmetIndex,
            tag_buf);
    }

    const char* GetPresetName(ControllerPreset preset) {
        switch (preset) {
            case ControllerPreset::Default: return "Default";
            case ControllerPreset::BumperJumper: return "Bumper Jumper";
            case ControllerPreset::Fishstick: return "Fishstick";
            case ControllerPreset::Recon: return "Recon";
            case ControllerPreset::UniversalReclaimer: return "Universal Reclaimer";
            case ControllerPreset::UniversalZoomAndShoot: return "Universal Zoom & Shoot";
            case ControllerPreset::Custom: return "Custom";
            default: return "Unknown";
        }
    }

    const char* GetTeamName(Team team) {
        switch (team) {
            case Team::Red: return "Red";
            case Team::Blue: return "Blue";
            case Team::Green: return "Green";
            case Team::Orange: return "Orange";
            case Team::Purple: return "Purple";
            case Team::Gold: return "Gold";
            case Team::Brown: return "Brown";
            case Team::Pink: return "Pink";
            default: return "Unknown";
        }
    }

    void ChangePlayerTeam(int slot_index, Team team) {
        if (slot_index < 0 || slot_index >= 4) return;

        auto p_engine = GameEngine();
        if (!MCC::IsInGame() || !p_engine) {
            LOG_WARNING("Cannot change team - not in game");
            return;
        }

        auto xuid = CGameManager::get_xuid(slot_index);
        if (xuid == 0) {
            LOG_WARNING("Cannot change team - invalid player xuid for slot {}", slot_index);
            return;
        }

        p_engine->change_team(xuid, static_cast<int>(team));
        LOG_INFO("Changed player {} to team {}", slot_index, GetTeamName(team));
    }

    void ApplyControllerPreset(CGamepadMapping& mapping, ControllerPreset preset) {
        // Button values (matching CGamepadMapping::eButton)
        const auto LT = CGamepadMapping::LeftTrigger;
        const auto RT = CGamepadMapping::RightTrigger;
        const auto DUp = CGamepadMapping::DpadUp;
        const auto DDown = CGamepadMapping::DpadDown;
        const auto DLeft = CGamepadMapping::DpadLeft;
        const auto DRight = CGamepadMapping::DpadRight;
        const auto Start = CGamepadMapping::Start;
        const auto Back = CGamepadMapping::Back;
        const auto LS = CGamepadMapping::LeftThumb;
        const auto RS = CGamepadMapping::RightThumb;
        const auto LB = CGamepadMapping::LeftShoulder;
        const auto RB = CGamepadMapping::RightShoulder;
        const auto A = CGamepadMapping::A;
        const auto B = CGamepadMapping::B;
        const auto X = CGamepadMapping::X;
        const auto Y = CGamepadMapping::Y;

        // Action indices (from CGamepadMapping action_names)
        enum Action {
            Jump = 0, SwitchGrenades = 1, Action_ = 2, Reload = 3, ChangeWeapon = 4,
            Melee = 5, Flashlight = 6, ThrowGrenade = 7, Fire = 8, Crouch = 9,
            Zoom = 10, SwapReloadLeft = 13, Sprint = 14, BansheeBomb = 15,
            Scoreboard = 20, VehicleFunc2 = 21, VehicleFunc3 = 22, Equipment = 23,
            VehicleFunc1 = 24, UseLeftWeapon = 63
        };

        // Clear all mappings first
        memset(&mapping, 0, sizeof(mapping));

        switch (preset) {
            case ControllerPreset::Default:
                // Standard MCC Default layout
                mapping.actions[Jump] = A;
                mapping.actions[SwitchGrenades] = B;
                mapping.actions[Action_] = X;
                mapping.actions[Reload] = X;
                mapping.actions[ChangeWeapon] = Y;
                mapping.actions[Melee] = B;
                mapping.actions[Flashlight] = DUp;
                mapping.actions[ThrowGrenade] = LT;
                mapping.actions[Fire] = RT;
                mapping.actions[Crouch] = LS;
                mapping.actions[Zoom] = RS;
                mapping.actions[Sprint] = LB;
                mapping.actions[Equipment] = RB;
                mapping.actions[Scoreboard] = Back;
                break;

            case ControllerPreset::BumperJumper:
                // Bumper Jumper - Jump on LB
                mapping.actions[Jump] = LB;
                mapping.actions[SwitchGrenades] = B;
                mapping.actions[Action_] = X;
                mapping.actions[Reload] = X;
                mapping.actions[ChangeWeapon] = Y;
                mapping.actions[Melee] = RB;
                mapping.actions[Flashlight] = DUp;
                mapping.actions[ThrowGrenade] = LT;
                mapping.actions[Fire] = RT;
                mapping.actions[Crouch] = LS;
                mapping.actions[Zoom] = RS;
                mapping.actions[Sprint] = A;
                mapping.actions[Equipment] = DLeft;
                mapping.actions[Scoreboard] = Back;
                break;

            case ControllerPreset::Recon:
                // Recon - Melee on RB, Zoom on LS
                mapping.actions[Jump] = A;
                mapping.actions[SwitchGrenades] = X;
                mapping.actions[Action_] = RB;
                mapping.actions[Reload] = RB;
                mapping.actions[ChangeWeapon] = Y;
                mapping.actions[Melee] = B;
                mapping.actions[Flashlight] = DUp;
                mapping.actions[ThrowGrenade] = LT;
                mapping.actions[Fire] = RT;
                mapping.actions[Crouch] = RS;
                mapping.actions[Zoom] = LS;
                mapping.actions[Sprint] = LB;
                mapping.actions[Equipment] = DLeft;
                mapping.actions[Scoreboard] = Back;
                break;

            case ControllerPreset::UniversalZoomAndShoot:
                // Universal Zoom and Shoot - Zoom on LT, Grenade on LB
                mapping.actions[Jump] = A;
                mapping.actions[SwitchGrenades] = B;
                mapping.actions[Action_] = X;
                mapping.actions[Reload] = X;
                mapping.actions[ChangeWeapon] = Y;
                mapping.actions[Melee] = RB;
                mapping.actions[Flashlight] = DUp;
                mapping.actions[ThrowGrenade] = LB;
                mapping.actions[Fire] = RT;
                mapping.actions[Crouch] = LS;
                mapping.actions[Zoom] = LT;
                mapping.actions[Sprint] = RS;
                mapping.actions[Equipment] = DLeft;
                mapping.actions[Scoreboard] = Back;
                break;

            case ControllerPreset::Fishstick:
                // Fishstick - CoD-style layout
                mapping.actions[Jump] = A;
                mapping.actions[SwitchGrenades] = DRight;
                mapping.actions[Action_] = X;
                mapping.actions[Reload] = X;
                mapping.actions[ChangeWeapon] = Y;
                mapping.actions[Melee] = RS;
                mapping.actions[Flashlight] = DUp;
                mapping.actions[ThrowGrenade] = LB;
                mapping.actions[Fire] = RT;
                mapping.actions[Crouch] = B;
                mapping.actions[Zoom] = LT;
                mapping.actions[Sprint] = LS;
                mapping.actions[Equipment] = RB;
                mapping.actions[Scoreboard] = Back;
                break;

            case ControllerPreset::UniversalReclaimer:
                // Universal Reclaimer
                mapping.actions[Jump] = A;
                mapping.actions[SwitchGrenades] = DRight;
                mapping.actions[Action_] = X;
                mapping.actions[Reload] = X;
                mapping.actions[ChangeWeapon] = Y;
                mapping.actions[Melee] = RB;
                mapping.actions[Flashlight] = DUp;
                mapping.actions[ThrowGrenade] = LB;
                mapping.actions[Fire] = RT;
                mapping.actions[Crouch] = RS;
                mapping.actions[Zoom] = LT;
                mapping.actions[Sprint] = LS;
                mapping.actions[Equipment] = B;
                mapping.actions[Scoreboard] = Back;
                break;

            case ControllerPreset::Custom:
            default:
                // Custom - don't change anything, keep current mapping
                break;
        }
    }

    // JSON serialization helpers
    static std::string wstring_to_utf8(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        std::string result;
        result.reserve(wstr.size() * 4);
        for (wchar_t wc : wstr) {
            if (wc < 0x80) {
                result += static_cast<char>(wc);
            } else if (wc < 0x800) {
                result += static_cast<char>(0xC0 | (wc >> 6));
                result += static_cast<char>(0x80 | (wc & 0x3F));
            } else {
                result += static_cast<char>(0xE0 | (wc >> 12));
                result += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (wc & 0x3F));
            }
        }
        return result;
    }

    static std::wstring utf8_to_wstring(const std::string& str) {
        if (str.empty()) return L"";
        std::wstring result;
        result.reserve(str.size());
        size_t i = 0;
        while (i < str.size()) {
            unsigned char c = str[i];
            if (c < 0x80) {
                result += static_cast<wchar_t>(c);
                i++;
            } else if ((c & 0xE0) == 0xC0) {
                if (i + 1 < str.size()) {
                    wchar_t wc = ((c & 0x1F) << 6) | (str[i + 1] & 0x3F);
                    result += wc;
                }
                i += 2;
            } else if ((c & 0xF0) == 0xE0) {
                if (i + 2 < str.size()) {
                    wchar_t wc = ((c & 0x0F) << 12) | ((str[i + 1] & 0x3F) << 6) | (str[i + 2] & 0x3F);
                    result += wc;
                }
                i += 3;
            } else {
                i++;
            }
        }
        return result;
    }

    static void to_json(json& j, const CUserProfile& p) {
        j = json{
            {"SubtitleSetting", p.SubtitleSetting},
            {"SubtitleSizeSetting", p.SubtitleSizeSetting},
            {"SubtitleBackgroundSetting", p.SubtitleBackgroundSetting},
            {"SubtitleShadowColorSetting", p.SubtitleShadowColorSetting},
            {"DialogueColorStyleSetting", p.DialogueColorStyleSetting},
            {"DialogueColorSetting", p.DialogueColorSetting},
            {"DialoguePaletteSetting", p.DialoguePaletteSetting},
            {"SpeakerSetting", p.SpeakerSetting},
            {"SpeakerColorStyleSetting", p.SpeakerColorStyleSetting},
            {"SpeakerColorSetting", p.SpeakerColorSetting},
            {"SpeakerPaletteSetting", p.SpeakerPaletteSetting},
            {"SubtitleFontSetting", p.SubtitleFontSetting},
            {"SubtitleBackgroundOpacitySetting", p.SubtitleBackgroundOpacitySetting},
            {"SubtitleShadowOpacitySetting", p.SubtitleShadowOpacitySetting},
            {"FOVSetting", p.FOVSetting},
            {"VehicleFOVSetting", p.VehicleFOVSetting},
            {"CrosshairLocation", p.CrosshairLocation},
            {"LookControlsInverted", p.LookControlsInverted},
            {"MouseLookControlsInverted", p.MouseLookControlsInverted},
            {"VibrationDisabled", p.VibrationDisabled},
            {"ImpulseTriggersDisabled", p.ImpulseTriggersDisabled},
            {"AircraftControlsInverted", p.AircraftControlsInverted},
            {"MouseAircraftControlsInverted", p.MouseAircraftControlsInverted},
            {"AutoCenterEnabled", p.AutoCenterEnabled},
            {"CrouchLockEnabled", p.CrouchLockEnabled},
            {"MKCrouchLockEnabled", p.MKCrouchLockEnabled},
            {"ClenchProtectionEnabled", p.ClenchProtectionEnabled},
            {"UseFemaleVoice", p.UseFemaleVoice},
            {"HoldToZoom", p.HoldToZoom},
            {"PlayerModelPrimaryColorIndex", p.PlayerModelPrimaryColorIndex},
            {"PlayerModelSecondaryColorIndex", p.PlayerModelSecondaryColorIndex},
            {"PlayerModelTertiaryColorIndex", p.PlayerModelTertiaryColorIndex},
            {"UseEliteModel", p.UseEliteModel},
            {"LockMaxAspectRatio", p.LockMaxAspectRatio},
            {"UsersSkinsEnabled", p.UsersSkinsEnabled},
            {"PlayerModelPermutation", p.PlayerModelPermutation},
            {"HelmetIndex", p.HelmetIndex},
            {"LeftShoulderIndex", p.LeftShoulderIndex},
            {"RightShoulderIndex", p.RightShoulderIndex},
            {"ChestIndex", p.ChestIndex},
            {"WristIndex", p.WristIndex},
            {"UtilityIndex", p.UtilityIndex},
            {"ArmsIndex", p.ArmsIndex},
            {"LegsIndex", p.LegsIndex},
            {"BackpackIndex", p.BackpackIndex},
            {"SpartanBodyIndex", p.SpartanBodyIndex},
            {"SpartanArmorEffectIndex", p.SpartanArmorEffectIndex},
            {"KneesIndex", p.KneesIndex},
            {"VisorColorIndex", p.VisorColorIndex},
            {"EliteHelmetIndex", p.EliteHelmetIndex},
            {"EliteLeftShoulderIndex", p.EliteLeftShoulderIndex},
            {"EliteRightShoulderIndex", p.EliteRightShoulderIndex},
            {"EliteChestIndex", p.EliteChestIndex},
            {"EliteArmsIndex", p.EliteArmsIndex},
            {"EliteLegsIndex", p.EliteLegsIndex},
            {"EliteArmorIndex", p.EliteArmorIndex},
            {"EliteArmorEffectIndex", p.EliteArmorEffectIndex},
            {"VoiceIndex", p.VoiceIndex},
            {"PlayerModelPrimaryColor", p.PlayerModelPrimaryColor},
            {"PlayerModelSecondaryColor", p.PlayerModelSecondaryColor},
            {"PlayerModelTertiaryColor", p.PlayerModelTertiaryColor},
            {"SpartanPose", p.SpartanPose},
            {"ElitePose", p.ElitePose},
            {"OnlineMedalFlasher", p.OnlineMedalFlasher},
            {"VerticalLookSensitivity", p.VerticalLookSensitivity},
            {"HorizontalLookSensitivity", p.HorizontalLookSensitivity},
            {"LookAcceleration", p.LookAcceleration},
            {"LookAxialDeadZone", p.LookAxialDeadZone},
            {"LookRadialDeadZone", p.LookRadialDeadZone},
            {"ZoomLookSensitivityMultiplier", p.ZoomLookSensitivityMultiplier},
            {"VehicleLookSensitivityMultiplier", p.VehicleLookSensitivityMultiplier},
            {"ButtonPreset", p.ButtonPreset},
            {"StickPreset", p.StickPreset},
            {"LeftyToggle", p.LeftyToggle},
            {"FlyingCameraTurnSensitivity", p.FlyingCameraTurnSensitivity},
            {"FlyingCameraPanning", p.FlyingCameraPanning},
            {"FlyingCameraSpeed", p.FlyingCameraSpeed},
            {"FlyingCameraThrust", p.FlyingCameraThrust},
            {"TheaterTurnSensitivity", p.TheaterTurnSensitivity},
            {"TheaterPanning", p.TheaterPanning},
            {"TheaterSpeed", p.TheaterSpeed},
            {"TheaterThrust", p.TheaterThrust},
            {"MKTheaterTurnSensitivity", p.MKTheaterTurnSensitivity},
            {"MKTheaterPanning", p.MKTheaterPanning},
            {"MKTheaterSpeed", p.MKTheaterSpeed},
            {"MKTheaterThrust", p.MKTheaterThrust},
            {"SwapTriggersAndBumpers", p.SwapTriggersAndBumpers},
            {"UseModernAimControl", p.UseModernAimControl},
            {"UseDoublePressJumpToJetpack", p.UseDoublePressJumpToJetpack},
            {"DualWieldInverted", p.DualWieldInverted},
            {"ControllerDualWieldInverted", p.ControllerDualWieldInverted},
            {"ControllerHornetControlJoystick", p.ControllerHornetControlJoystick},
            {"ControllerBansheeTrickButtonsSwapped", p.ControllerBansheeTrickButtonsSwapped},
            {"ColorCorrection", p.ColorCorrection},
            {"EnemyPlayerNameColor", p.EnemyPlayerNameColor},
            {"GameEngineTimer", p.GameEngineTimer},
            {"MouseSensitivity", p.MouseSensitivity},
            {"MouseSmoothing", p.MouseSmoothing},
            {"MouseAcceleration", p.MouseAcceleration},
            {"PixelPerfectHudScale", p.PixelPerfectHudScale},
            {"MouseAccelerationMinRate", p.MouseAccelerationMinRate},
            {"MouseAccelerationMaxAccel", p.MouseAccelerationMaxAccel},
            {"MouseAccelerationScale", p.MouseAccelerationScale},
            {"MouseAccelerationExp", p.MouseAccelerationExp},
            {"KeyboardMouseButtonPreset", p.KeyboardMouseButtonPreset},
            {"MasterVolume", p.MasterVolume},
            {"MusicVolume", p.MusicVolume},
            {"SfxVolume", p.SfxVolume},
            {"Brightness", p.Brightness},
            {"ColorBlindMode", p.ColorBlindMode},
            {"ColorBlindStrength", p.ColorBlindStrength},
            {"ColorBlindBrightness", p.ColorBlindBrightness},
            {"ColorBlindContrast", p.ColorBlindContrast},
            {"RemasteredHUDSetting", p.RemasteredHUDSetting},
            {"HUDScale", p.HUDScale}
        };

        // Serialize service tag
        std::wstring st(p.ServiceTag, 4);
        j["ServiceTag"] = wstring_to_utf8(st);

        // Serialize skins array
        json skins_array = json::array();
        for (int i = 0; i < 32; i++) {
            skins_array.push_back({{"object", p.Skins[i].object}, {"skin", p.Skins[i].skin}});
        }
        j["Skins"] = skins_array;

        // Serialize loadout slots
        json loadouts_array = json::array();
        for (int i = 0; i < 5; i++) {
            const auto& slot = p.LoadoutSlots[i];
            std::wstring name(slot.Name, 14);
            loadouts_array.push_back({
                {"TacticalPackageIndex", slot.TacticalPackageIndex},
                {"SupportUpgradeIndex", slot.SupportUpgradeIndex},
                {"PrimaryWeaponIndex", slot.PrimaryWeaponIndex},
                {"SecondaryWeaponIndex", slot.SecondaryWeaponIndex},
                {"PrimaryWeaponVariantIndex", slot.PrimaryWeaponVariantIndex},
                {"SecondaryWeaponVariantIndex", slot.SecondaryWeaponVariantIndex},
                {"EquipmentIndex", slot.EquipmentIndex},
                {"GrenadeIndex", slot.GrenadeIndex},
                {"Name", wstring_to_utf8(name)}
            });
        }
        j["LoadoutSlots"] = loadouts_array;

        // Serialize weapon display offsets
        json offsets_array = json::array();
        for (int i = 0; i < 5; i++) {
            offsets_array.push_back({
                {"x", p.WeaponDisplayOffset[i].x},
                {"y", p.WeaponDisplayOffset[i].y},
                {"z", p.WeaponDisplayOffset[i].z}
            });
        }
        j["WeaponDisplayOffset"] = offsets_array;
    }

    static void from_json(const json& j, CUserProfile& p) {
        memset(&p, 0, sizeof(CUserProfile));

        if (j.contains("SubtitleSetting")) p.SubtitleSetting = j["SubtitleSetting"].get<bool>();
        if (j.contains("SubtitleSizeSetting")) p.SubtitleSizeSetting = j["SubtitleSizeSetting"].get<bool>();
        if (j.contains("SubtitleBackgroundSetting")) p.SubtitleBackgroundSetting = j["SubtitleBackgroundSetting"].get<bool>();
        if (j.contains("SubtitleShadowColorSetting")) p.SubtitleShadowColorSetting = j["SubtitleShadowColorSetting"].get<bool>();
        if (j.contains("DialogueColorStyleSetting")) p.DialogueColorStyleSetting = j["DialogueColorStyleSetting"].get<bool>();
        if (j.contains("DialogueColorSetting")) p.DialogueColorSetting = j["DialogueColorSetting"].get<bool>();
        if (j.contains("DialoguePaletteSetting")) p.DialoguePaletteSetting = j["DialoguePaletteSetting"].get<bool>();
        if (j.contains("SpeakerSetting")) p.SpeakerSetting = j["SpeakerSetting"].get<bool>();
        if (j.contains("SpeakerColorStyleSetting")) p.SpeakerColorStyleSetting = j["SpeakerColorStyleSetting"].get<bool>();
        if (j.contains("SpeakerColorSetting")) p.SpeakerColorSetting = j["SpeakerColorSetting"].get<bool>();
        if (j.contains("SpeakerPaletteSetting")) p.SpeakerPaletteSetting = j["SpeakerPaletteSetting"].get<bool>();
        if (j.contains("SubtitleFontSetting")) p.SubtitleFontSetting = j["SubtitleFontSetting"].get<bool>();
        if (j.contains("SubtitleBackgroundOpacitySetting")) p.SubtitleBackgroundOpacitySetting = j["SubtitleBackgroundOpacitySetting"].get<float>();
        if (j.contains("SubtitleShadowOpacitySetting")) p.SubtitleShadowOpacitySetting = j["SubtitleShadowOpacitySetting"].get<float>();
        if (j.contains("FOVSetting")) p.FOVSetting = j["FOVSetting"].get<int>();
        if (j.contains("VehicleFOVSetting")) p.VehicleFOVSetting = j["VehicleFOVSetting"].get<int>();
        if (j.contains("CrosshairLocation")) p.CrosshairLocation = j["CrosshairLocation"].get<bool>();
        if (j.contains("LookControlsInverted")) p.LookControlsInverted = j["LookControlsInverted"].get<bool>();
        if (j.contains("MouseLookControlsInverted")) p.MouseLookControlsInverted = j["MouseLookControlsInverted"].get<bool>();
        if (j.contains("VibrationDisabled")) p.VibrationDisabled = j["VibrationDisabled"].get<bool>();
        if (j.contains("ImpulseTriggersDisabled")) p.ImpulseTriggersDisabled = j["ImpulseTriggersDisabled"].get<bool>();
        if (j.contains("AircraftControlsInverted")) p.AircraftControlsInverted = j["AircraftControlsInverted"].get<bool>();
        if (j.contains("MouseAircraftControlsInverted")) p.MouseAircraftControlsInverted = j["MouseAircraftControlsInverted"].get<bool>();
        if (j.contains("AutoCenterEnabled")) p.AutoCenterEnabled = j["AutoCenterEnabled"].get<bool>();
        if (j.contains("CrouchLockEnabled")) p.CrouchLockEnabled = j["CrouchLockEnabled"].get<bool>();
        if (j.contains("MKCrouchLockEnabled")) p.MKCrouchLockEnabled = j["MKCrouchLockEnabled"].get<bool>();
        if (j.contains("ClenchProtectionEnabled")) p.ClenchProtectionEnabled = j["ClenchProtectionEnabled"].get<bool>();
        if (j.contains("UseFemaleVoice")) p.UseFemaleVoice = j["UseFemaleVoice"].get<bool>();
        if (j.contains("HoldToZoom")) p.HoldToZoom = j["HoldToZoom"].get<int>();
        if (j.contains("PlayerModelPrimaryColorIndex")) p.PlayerModelPrimaryColorIndex = j["PlayerModelPrimaryColorIndex"].get<int>();
        if (j.contains("PlayerModelSecondaryColorIndex")) p.PlayerModelSecondaryColorIndex = j["PlayerModelSecondaryColorIndex"].get<int>();
        if (j.contains("PlayerModelTertiaryColorIndex")) p.PlayerModelTertiaryColorIndex = j["PlayerModelTertiaryColorIndex"].get<int>();
        if (j.contains("UseEliteModel")) p.UseEliteModel = j["UseEliteModel"].get<bool>();
        if (j.contains("LockMaxAspectRatio")) p.LockMaxAspectRatio = j["LockMaxAspectRatio"].get<bool>();
        if (j.contains("UsersSkinsEnabled")) p.UsersSkinsEnabled = j["UsersSkinsEnabled"].get<bool>();
        if (j.contains("PlayerModelPermutation")) p.PlayerModelPermutation = j["PlayerModelPermutation"].get<int>();
        if (j.contains("HelmetIndex")) p.HelmetIndex = j["HelmetIndex"].get<int>();
        if (j.contains("LeftShoulderIndex")) p.LeftShoulderIndex = j["LeftShoulderIndex"].get<int>();
        if (j.contains("RightShoulderIndex")) p.RightShoulderIndex = j["RightShoulderIndex"].get<int>();
        if (j.contains("ChestIndex")) p.ChestIndex = j["ChestIndex"].get<int>();
        if (j.contains("WristIndex")) p.WristIndex = j["WristIndex"].get<int>();
        if (j.contains("UtilityIndex")) p.UtilityIndex = j["UtilityIndex"].get<int>();
        if (j.contains("ArmsIndex")) p.ArmsIndex = j["ArmsIndex"].get<int>();
        if (j.contains("LegsIndex")) p.LegsIndex = j["LegsIndex"].get<int>();
        if (j.contains("BackpackIndex")) p.BackpackIndex = j["BackpackIndex"].get<int>();
        if (j.contains("SpartanBodyIndex")) p.SpartanBodyIndex = j["SpartanBodyIndex"].get<int>();
        if (j.contains("SpartanArmorEffectIndex")) p.SpartanArmorEffectIndex = j["SpartanArmorEffectIndex"].get<int>();
        if (j.contains("KneesIndex")) p.KneesIndex = j["KneesIndex"].get<int>();
        if (j.contains("VisorColorIndex")) p.VisorColorIndex = j["VisorColorIndex"].get<int>();
        if (j.contains("EliteHelmetIndex")) p.EliteHelmetIndex = j["EliteHelmetIndex"].get<int>();
        if (j.contains("EliteLeftShoulderIndex")) p.EliteLeftShoulderIndex = j["EliteLeftShoulderIndex"].get<int>();
        if (j.contains("EliteRightShoulderIndex")) p.EliteRightShoulderIndex = j["EliteRightShoulderIndex"].get<int>();
        if (j.contains("EliteChestIndex")) p.EliteChestIndex = j["EliteChestIndex"].get<int>();
        if (j.contains("EliteArmsIndex")) p.EliteArmsIndex = j["EliteArmsIndex"].get<int>();
        if (j.contains("EliteLegsIndex")) p.EliteLegsIndex = j["EliteLegsIndex"].get<int>();
        if (j.contains("EliteArmorIndex")) p.EliteArmorIndex = j["EliteArmorIndex"].get<int>();
        if (j.contains("EliteArmorEffectIndex")) p.EliteArmorEffectIndex = j["EliteArmorEffectIndex"].get<int>();
        if (j.contains("VoiceIndex")) p.VoiceIndex = j["VoiceIndex"].get<int>();
        if (j.contains("PlayerModelPrimaryColor")) p.PlayerModelPrimaryColor = j["PlayerModelPrimaryColor"].get<int>();
        if (j.contains("PlayerModelSecondaryColor")) p.PlayerModelSecondaryColor = j["PlayerModelSecondaryColor"].get<int>();
        if (j.contains("PlayerModelTertiaryColor")) p.PlayerModelTertiaryColor = j["PlayerModelTertiaryColor"].get<int>();
        if (j.contains("SpartanPose")) p.SpartanPose = j["SpartanPose"].get<int>();
        if (j.contains("ElitePose")) p.ElitePose = j["ElitePose"].get<int>();
        if (j.contains("OnlineMedalFlasher")) p.OnlineMedalFlasher = j["OnlineMedalFlasher"].get<bool>();
        if (j.contains("VerticalLookSensitivity")) p.VerticalLookSensitivity = j["VerticalLookSensitivity"].get<bool>();
        if (j.contains("HorizontalLookSensitivity")) p.HorizontalLookSensitivity = j["HorizontalLookSensitivity"].get<bool>();
        if (j.contains("LookAcceleration")) p.LookAcceleration = j["LookAcceleration"].get<bool>();
        if (j.contains("LookAxialDeadZone")) p.LookAxialDeadZone = j["LookAxialDeadZone"].get<float>();
        if (j.contains("LookRadialDeadZone")) p.LookRadialDeadZone = j["LookRadialDeadZone"].get<float>();
        if (j.contains("ZoomLookSensitivityMultiplier")) p.ZoomLookSensitivityMultiplier = j["ZoomLookSensitivityMultiplier"].get<float>();
        if (j.contains("VehicleLookSensitivityMultiplier")) p.VehicleLookSensitivityMultiplier = j["VehicleLookSensitivityMultiplier"].get<float>();
        if (j.contains("ButtonPreset")) p.ButtonPreset = j["ButtonPreset"].get<bool>();
        if (j.contains("StickPreset")) p.StickPreset = j["StickPreset"].get<bool>();
        if (j.contains("LeftyToggle")) p.LeftyToggle = j["LeftyToggle"].get<bool>();
        if (j.contains("FlyingCameraTurnSensitivity")) p.FlyingCameraTurnSensitivity = j["FlyingCameraTurnSensitivity"].get<bool>();
        if (j.contains("FlyingCameraPanning")) p.FlyingCameraPanning = j["FlyingCameraPanning"].get<bool>();
        if (j.contains("FlyingCameraSpeed")) p.FlyingCameraSpeed = j["FlyingCameraSpeed"].get<bool>();
        if (j.contains("FlyingCameraThrust")) p.FlyingCameraThrust = j["FlyingCameraThrust"].get<bool>();
        if (j.contains("TheaterTurnSensitivity")) p.TheaterTurnSensitivity = j["TheaterTurnSensitivity"].get<bool>();
        if (j.contains("TheaterPanning")) p.TheaterPanning = j["TheaterPanning"].get<bool>();
        if (j.contains("TheaterSpeed")) p.TheaterSpeed = j["TheaterSpeed"].get<bool>();
        if (j.contains("TheaterThrust")) p.TheaterThrust = j["TheaterThrust"].get<bool>();
        if (j.contains("MKTheaterTurnSensitivity")) p.MKTheaterTurnSensitivity = j["MKTheaterTurnSensitivity"].get<bool>();
        if (j.contains("MKTheaterPanning")) p.MKTheaterPanning = j["MKTheaterPanning"].get<bool>();
        if (j.contains("MKTheaterSpeed")) p.MKTheaterSpeed = j["MKTheaterSpeed"].get<bool>();
        if (j.contains("MKTheaterThrust")) p.MKTheaterThrust = j["MKTheaterThrust"].get<bool>();
        if (j.contains("SwapTriggersAndBumpers")) p.SwapTriggersAndBumpers = j["SwapTriggersAndBumpers"].get<bool>();
        if (j.contains("UseModernAimControl")) p.UseModernAimControl = j["UseModernAimControl"].get<bool>();
        if (j.contains("UseDoublePressJumpToJetpack")) p.UseDoublePressJumpToJetpack = j["UseDoublePressJumpToJetpack"].get<bool>();
        if (j.contains("DualWieldInverted")) p.DualWieldInverted = j["DualWieldInverted"].get<bool>();
        if (j.contains("ControllerDualWieldInverted")) p.ControllerDualWieldInverted = j["ControllerDualWieldInverted"].get<bool>();
        if (j.contains("ControllerHornetControlJoystick")) p.ControllerHornetControlJoystick = j["ControllerHornetControlJoystick"].get<bool>();
        if (j.contains("ControllerBansheeTrickButtonsSwapped")) p.ControllerBansheeTrickButtonsSwapped = j["ControllerBansheeTrickButtonsSwapped"].get<bool>();
        if (j.contains("ColorCorrection")) p.ColorCorrection = j["ColorCorrection"].get<bool>();
        if (j.contains("EnemyPlayerNameColor")) p.EnemyPlayerNameColor = j["EnemyPlayerNameColor"].get<bool>();
        if (j.contains("GameEngineTimer")) p.GameEngineTimer = j["GameEngineTimer"].get<int>();
        if (j.contains("MouseSensitivity")) p.MouseSensitivity = j["MouseSensitivity"].get<float>();
        if (j.contains("MouseSmoothing")) p.MouseSmoothing = j["MouseSmoothing"].get<bool>();
        if (j.contains("MouseAcceleration")) p.MouseAcceleration = j["MouseAcceleration"].get<bool>();
        if (j.contains("PixelPerfectHudScale")) p.PixelPerfectHudScale = j["PixelPerfectHudScale"].get<__int16>();
        if (j.contains("MouseAccelerationMinRate")) p.MouseAccelerationMinRate = j["MouseAccelerationMinRate"].get<float>();
        if (j.contains("MouseAccelerationMaxAccel")) p.MouseAccelerationMaxAccel = j["MouseAccelerationMaxAccel"].get<float>();
        if (j.contains("MouseAccelerationScale")) p.MouseAccelerationScale = j["MouseAccelerationScale"].get<float>();
        if (j.contains("MouseAccelerationExp")) p.MouseAccelerationExp = j["MouseAccelerationExp"].get<float>();
        if (j.contains("KeyboardMouseButtonPreset")) p.KeyboardMouseButtonPreset = j["KeyboardMouseButtonPreset"].get<int>();
        if (j.contains("MasterVolume")) p.MasterVolume = j["MasterVolume"].get<float>();
        if (j.contains("MusicVolume")) p.MusicVolume = j["MusicVolume"].get<float>();
        if (j.contains("SfxVolume")) p.SfxVolume = j["SfxVolume"].get<float>();
        if (j.contains("Brightness")) p.Brightness = j["Brightness"].get<float>();
        if (j.contains("ColorBlindMode")) p.ColorBlindMode = j["ColorBlindMode"].get<int>();
        if (j.contains("ColorBlindStrength")) p.ColorBlindStrength = j["ColorBlindStrength"].get<int>();
        if (j.contains("ColorBlindBrightness")) p.ColorBlindBrightness = j["ColorBlindBrightness"].get<int>();
        if (j.contains("ColorBlindContrast")) p.ColorBlindContrast = j["ColorBlindContrast"].get<int>();
        if (j.contains("RemasteredHUDSetting")) p.RemasteredHUDSetting = j["RemasteredHUDSetting"].get<int>();
        if (j.contains("HUDScale")) p.HUDScale = j["HUDScale"].get<float>();

        // Deserialize service tag
        if (j.contains("ServiceTag")) {
            std::wstring st = utf8_to_wstring(j["ServiceTag"].get<std::string>());
            for (int i = 0; i < 4 && i < (int)st.size(); i++) {
                p.ServiceTag[i] = st[i];
            }
        }

        // Deserialize skins array
        if (j.contains("Skins") && j["Skins"].is_array()) {
            auto& skins = j["Skins"];
            for (size_t i = 0; i < skins.size() && i < 32; i++) {
                if (skins[i].contains("object")) p.Skins[i].object = skins[i]["object"].get<int>();
                if (skins[i].contains("skin")) p.Skins[i].skin = skins[i]["skin"].get<int>();
            }
        }

        // Deserialize loadout slots
        if (j.contains("LoadoutSlots") && j["LoadoutSlots"].is_array()) {
            auto& loadouts = j["LoadoutSlots"];
            for (size_t i = 0; i < loadouts.size() && i < 5; i++) {
                auto& slot = p.LoadoutSlots[i];
                auto& jslot = loadouts[i];
                if (jslot.contains("TacticalPackageIndex")) slot.TacticalPackageIndex = jslot["TacticalPackageIndex"].get<int>();
                if (jslot.contains("SupportUpgradeIndex")) slot.SupportUpgradeIndex = jslot["SupportUpgradeIndex"].get<int>();
                if (jslot.contains("PrimaryWeaponIndex")) slot.PrimaryWeaponIndex = jslot["PrimaryWeaponIndex"].get<int>();
                if (jslot.contains("SecondaryWeaponIndex")) slot.SecondaryWeaponIndex = jslot["SecondaryWeaponIndex"].get<int>();
                if (jslot.contains("PrimaryWeaponVariantIndex")) slot.PrimaryWeaponVariantIndex = jslot["PrimaryWeaponVariantIndex"].get<int>();
                if (jslot.contains("SecondaryWeaponVariantIndex")) slot.SecondaryWeaponVariantIndex = jslot["SecondaryWeaponVariantIndex"].get<int>();
                if (jslot.contains("EquipmentIndex")) slot.EquipmentIndex = jslot["EquipmentIndex"].get<int>();
                if (jslot.contains("GrenadeIndex")) slot.GrenadeIndex = jslot["GrenadeIndex"].get<int>();
                if (jslot.contains("Name")) {
                    std::wstring name = utf8_to_wstring(jslot["Name"].get<std::string>());
                    for (int k = 0; k < 14 && k < (int)name.size(); k++) {
                        slot.Name[k] = name[k];
                    }
                }
            }
        }

        // Deserialize weapon display offsets
        if (j.contains("WeaponDisplayOffset") && j["WeaponDisplayOffset"].is_array()) {
            auto& offsets = j["WeaponDisplayOffset"];
            for (size_t i = 0; i < offsets.size() && i < 5; i++) {
                if (offsets[i].contains("x")) p.WeaponDisplayOffset[i].x = offsets[i]["x"].get<float>();
                if (offsets[i].contains("y")) p.WeaponDisplayOffset[i].y = offsets[i]["y"].get<float>();
                if (offsets[i].contains("z")) p.WeaponDisplayOffset[i].z = offsets[i]["z"].get<float>();
            }
        }
    }

    static void to_json(json& j, const CGamepadMapping& m) {
        json actions_array = json::array();
        for (int i = 0; i < 66; i++) {
            actions_array.push_back(static_cast<int>(m.actions[i]));
        }
        j = json{{"actions", actions_array}};
    }

    static void from_json(const json& j, CGamepadMapping& m) {
        memset(&m, 0, sizeof(CGamepadMapping));
        if (j.contains("actions") && j["actions"].is_array()) {
            auto& actions = j["actions"];
            for (size_t i = 0; i < actions.size() && i < 66; i++) {
                m.actions[i] = static_cast<CGamepadMapping::eButton>(actions[i].get<int>());
            }
        }
    }

    std::string ProfileManager::GetProfilesPath() {
        return "./alpha_ring/profiles";
    }

    bool ProfileManager::EnsureProfilesDirectory() {
        std::string path = GetProfilesPath();
        try {
            if (!fs::exists(path)) {
                fs::create_directories(path);
            }
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to create profiles directory: {}", e.what());
            return false;
        }
    }

    bool ProfileManager::LoadAllProfiles() {
        profiles.clear();

        if (!EnsureProfilesDirectory()) {
            return false;
        }

        std::string path = GetProfilesPath();

        try {
            for (const auto& entry : fs::directory_iterator(path)) {
                if (entry.path().extension() == ".json") {
                    std::ifstream file(entry.path());
                    if (!file.is_open()) {
                        LOG_WARNING("Failed to open profile: {}", entry.path().string());
                        continue;
                    }

                    try {
                        json j = json::parse(file);

                        // Version check
                        int version = j.value("version", 1);
                        if (version > PROFILE_VERSION) {
                            LOG_WARNING("Profile {} has newer version {}, skipping", entry.path().string(), version);
                            continue;
                        }

                        PersistentProfile profile;
                        profile.filename = entry.path().filename().string();

                        if (j.contains("display_name")) {
                            profile.display_name = utf8_to_wstring(j["display_name"].get<std::string>());
                        }

                        if (j.contains("service_tag")) {
                            std::string st_utf8 = j["service_tag"].get<std::string>();
                            std::wstring st = utf8_to_wstring(st_utf8);
                            for (int i = 0; i < 4 && i < (int)st.size(); i++) {
                                profile.service_tag[i] = st[i];
                            }
                            profile.service_tag[4] = L'\0';
                            LOG_INFO("  Service tag from JSON: '{}' -> wide: '{}'", st_utf8, wstring_to_utf8(std::wstring(profile.service_tag, 4)));
                        } else {
                            LOG_WARNING("  No service_tag field in profile!");
                        }

                        profile.rank_xp = j.value("rank_xp", 0);
                        profile.rank_level = ComputeRankLevel(profile.rank_xp);

                        // Load controller preset
                        if (j.contains("controller_preset")) {
                            int preset_val = j["controller_preset"].get<int>();
                            if (preset_val >= 0 && preset_val < static_cast<int>(ControllerPreset::COUNT)) {
                                profile.controller_preset = static_cast<ControllerPreset>(preset_val);
                            }
                            LOG_INFO("  Controller preset: {}", GetPresetName(profile.controller_preset));
                        }

                        // Load team preference
                        if (j.contains("team_preference")) {
                            int team_val = j["team_preference"].get<int>();
                            if (team_val >= 0 && team_val < static_cast<int>(Team::COUNT)) {
                                profile.team_preference = static_cast<Team>(team_val);
                            }
                            LOG_INFO("  Team preference: {}", GetTeamName(profile.team_preference));
                        }

                        // Load emblem configuration
                        if (j.contains("emblem") && j["emblem"].is_object()) {
                            auto& emb = j["emblem"];
                            if (emb.contains("foreground")) profile.emblem.foreground = emb["foreground"].get<int>();
                            if (emb.contains("background")) profile.emblem.background = emb["background"].get<int>();
                            if (emb.contains("flags")) profile.emblem.flags = emb["flags"].get<int>();
                            if (emb.contains("primary_color")) profile.emblem.primary_color = emb["primary_color"].get<int>();
                            if (emb.contains("secondary_color")) profile.emblem.secondary_color = emb["secondary_color"].get<int>();
                            if (emb.contains("background_color")) profile.emblem.background_color = emb["background_color"].get<int>();
                            LOG_INFO("  Emblem: fg={}, bg={}, colors=({},{},{})",
                                profile.emblem.foreground, profile.emblem.background,
                                profile.emblem.primary_color, profile.emblem.secondary_color,
                                profile.emblem.background_color);
                        }

                        if (j.contains("user_profile")) {
                            from_json(j["user_profile"], profile.user_profile);
                            LOG_INFO("  User profile loaded: FOV={}, Helmet={}, PrimaryColor={}",
                                profile.user_profile.FOVSetting,
                                profile.user_profile.HelmetIndex,
                                profile.user_profile.PlayerModelPrimaryColor);
                        } else {
                            LOG_WARNING("  No user_profile field in profile!");
                        }

                        if (j.contains("gamepad_mapping")) {
                            from_json(j["gamepad_mapping"], profile.gamepad_mapping);
                        }

                        profiles.push_back(profile);
                        LOG_INFO("Loaded profile: {} (display_name='{}', rank={})",
                            profile.filename,
                            wstring_to_utf8(profile.display_name),
                            profile.rank_level);
                    } catch (const json::exception& e) {
                        LOG_ERROR("Failed to parse profile {}: {}", entry.path().string(), e.what());
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to enumerate profiles: {}", e.what());
            return false;
        }

        LOG_INFO("Loaded {} profiles", profiles.size());
        return true;
    }

    bool ProfileManager::SaveProfile(const PersistentProfile& profile, const std::string& filename) {
        if (!EnsureProfilesDirectory()) {
            return false;
        }

        std::string filepath = GetProfilesPath() + "/" + filename;

        try {
            json j;
            j["version"] = PROFILE_VERSION;
            j["display_name"] = wstring_to_utf8(profile.display_name);
            j["service_tag"] = wstring_to_utf8(std::wstring(profile.service_tag, 4));
            j["rank_xp"] = profile.rank_xp;
            j["controller_preset"] = static_cast<int>(profile.controller_preset);
            j["team_preference"] = static_cast<int>(profile.team_preference);

            // Emblem configuration
            j["emblem"] = json{
                {"foreground", profile.emblem.foreground},
                {"background", profile.emblem.background},
                {"flags", profile.emblem.flags},
                {"primary_color", profile.emblem.primary_color},
                {"secondary_color", profile.emblem.secondary_color},
                {"background_color", profile.emblem.background_color}
            };

            json user_profile_json;
            to_json(user_profile_json, profile.user_profile);
            j["user_profile"] = user_profile_json;

            json gamepad_json;
            to_json(gamepad_json, profile.gamepad_mapping);
            j["gamepad_mapping"] = gamepad_json;

            std::ofstream file(filepath);
            if (!file.is_open()) {
                LOG_ERROR("Failed to open file for writing: {}", filepath);
                return false;
            }

            file << j.dump(2);
            file.close();

            LOG_INFO("Saved profile: {}", filepath);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to save profile: {}", e.what());
            return false;
        }
    }

    bool ProfileManager::DeleteProfile(const std::string& filename) {
        std::string filepath = GetProfilesPath() + "/" + filename;

        try {
            if (fs::exists(filepath)) {
                fs::remove(filepath);
                LOG_INFO("Deleted profile: {}", filepath);

                // Remove from loaded profiles
                auto it = std::remove_if(profiles.begin(), profiles.end(),
                    [&filename](const PersistentProfile& p) { return p.filename == filename; });
                profiles.erase(it, profiles.end());

                // Reset selected indices if they were pointing to this profile
                for (int i = 0; i < 4; i++) {
                    if (selected_profile_index[i] >= (int)profiles.size()) {
                        selected_profile_index[i] = -1;
                    }
                }

                return true;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to delete profile: {}", e.what());
        }
        return false;
    }

    void ProfileManager::ApplyToSlot(int slot_index, const PersistentProfile& profile) {
        if (slot_index < 0 || slot_index >= 4) return;

        auto p_slot = CGameManager::get_profile(slot_index);
        if (!p_slot) return;

        // Auto-enable the settings needed for custom profiles to work
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        if (!p_setting->b_override_profile) {
            p_setting->b_override_profile = true;
            LOG_INFO("Auto-enabled 'Override profile' setting");
        }
        if (p_setting->b_use_player0_profile) {
            p_setting->b_use_player0_profile = false;
            LOG_INFO("Auto-disabled 'Use player1's profile' setting (each player needs own profile)");
        }

        LOG_INFO("Applying profile '{}' to slot {}", wstring_to_utf8(profile.display_name), slot_index);

        // Copy display name
        String::wstrcpy(p_slot->name, profile.display_name.c_str(), 1024);
        LOG_INFO("  Name: {}", wstring_to_utf8(profile.display_name));

        // Copy the ENTIRE user profile (armor, settings, everything)
        memcpy(&p_slot->profile, &profile.user_profile, sizeof(CUserProfile));

        // Also copy service tag from the top-level field (in case it differs)
        for (int i = 0; i < 4; i++) {
            p_slot->profile.ServiceTag[i] = profile.service_tag[i];
        }
        LOG_INFO("  ServiceTag: {}", wstring_to_utf8(std::wstring(profile.service_tag, 4)));

        LOG_INFO("  Armor: Helmet={}, Chest={}, Colors=({},{},{})",
            p_slot->profile.HelmetIndex, p_slot->profile.ChestIndex,
            p_slot->profile.PlayerModelPrimaryColor,
            p_slot->profile.PlayerModelSecondaryColor,
            p_slot->profile.PlayerModelTertiaryColor);

        // Apply controller preset
        if (profile.controller_preset != ControllerPreset::Custom) {
            ApplyControllerPreset(p_slot->mapping, profile.controller_preset);
            LOG_INFO("  Controller preset: {}", GetPresetName(profile.controller_preset));
        } else {
            // Custom - copy the saved mapping
            memcpy(&p_slot->mapping, &profile.gamepad_mapping, sizeof(CGamepadMapping));
            LOG_INFO("  Controller: Custom mapping loaded");
        }

        // Trigger settings reload if in-game
        auto p_engine = GameEngine();
        if (MCC::IsInGame() && p_engine) {
            LOG_INFO("  Triggering load_setting()");
            p_engine->load_setting();

            // Apply team preference (for team games)
            ChangePlayerTeam(slot_index, profile.team_preference);
            LOG_INFO("  Team preference: {}", GetTeamName(profile.team_preference));
        }

        LOG_INFO("Profile applied successfully to slot {}", slot_index);
    }

    void ProfileManager::TryAutoLoadFromConfig() {
        // Only attempt auto-load once per session
        if (s_auto_load_attempted) {
            return;
        }
        s_auto_load_attempted = true;

        auto p_cfg = AlphaRing::Global::InstanceConfig();
        if (!p_cfg->loaded) {
            LOG_INFO("No instance config loaded - skipping auto-profile selection");
            return;
        }

        if (p_cfg->profile_name.empty()) {
            LOG_INFO("Instance config has no profile_name - skipping auto-profile selection");
            return;
        }

        // Make sure profiles are loaded
        if (profiles.empty()) {
            LOG_WARNING("No profiles loaded - cannot auto-select profile");
            return;
        }

        // Find profile by filename
        for (int i = 0; i < (int)profiles.size(); i++) {
            if (profiles[i].filename == p_cfg->profile_name) {
                selected_profile_index[0] = i;
                ApplyToSlot(0, profiles[i]);
                LOG_INFO("=== Auto-loaded profile from instance config ===");
                LOG_INFO("  Profile: {}", p_cfg->profile_name);
                LOG_INFO("  Display name: {}", p_cfg->display_name);
                LOG_INFO("  Player: {}/{}", p_cfg->player_index + 1, p_cfg->total_players);
                LOG_INFO("  Screen position: {}", p_cfg->screen_position);
                LOG_INFO("================================================");
                return;
            }
        }

        LOG_WARNING("Instance config requested profile '{}' but it was not found in profiles/",
            p_cfg->profile_name);
        LOG_WARNING("Available profiles:");
        for (const auto& p : profiles) {
            LOG_WARNING("  - {}", p.filename);
        }
    }

    int ProfileManager::ComputeRankLevel(int xp) {
        int level = 1;
        for (int i = 0; i < 43; i++) {
            if (xp >= RANK_XP_THRESHOLDS[i]) {
                level = i + 1;
            } else {
                break;
            }
        }
        return std::min(level, 50);
    }

    const char* ProfileManager::GetRankName(int level) {
        if (level < 10) return "Recruit";
        if (level < 20) return "Private";
        if (level < 30) return "Corporal";
        if (level < 40) return "Sergeant";
        if (level < 50) return "Lieutenant";
        return "Captain";
    }

    void ProfileManager::ImGuiProfileSelector(int slot_index) {
        static char new_profile_name[256] = "";
        static bool show_new_profile_popup = false;
        static int popup_slot = -1;
        static int current_preset[4] = {0, 0, 0, 0}; // Controller preset for each slot
        static int current_team[4] = {0, 0, 0, 0};   // Team preference for each slot

        ImGui::PushID(slot_index);

        // Profile dropdown - auto-applies on selection
        const char* current_profile = (selected_profile_index[slot_index] >= 0 &&
                                       selected_profile_index[slot_index] < (int)profiles.size())
            ? profiles[selected_profile_index[slot_index]].filename.c_str()
            : "-- None --";

        ImGui::PushItemWidth(200);
        if (ImGui::BeginCombo("Profile", current_profile)) {
            if (ImGui::Selectable("-- None --", selected_profile_index[slot_index] < 0)) {
                selected_profile_index[slot_index] = -1;
            }
            for (int i = 0; i < (int)profiles.size(); i++) {
                bool is_selected = (selected_profile_index[slot_index] == i);
                if (ImGui::Selectable(profiles[i].filename.c_str(), is_selected)) {
                    if (selected_profile_index[slot_index] != i) {
                        selected_profile_index[slot_index] = i;
                        // Auto-apply the profile immediately
                        ApplyToSlot(slot_index, profiles[i]);
                        current_preset[slot_index] = static_cast<int>(profiles[i].controller_preset);
                        current_team[slot_index] = static_cast<int>(profiles[i].team_preference);
                        LOG_INFO("Auto-applied profile '{}' to slot {}", profiles[i].filename, slot_index);
                    }
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();

        // Re-apply button (in case you want to reload current profile)
        ImGui::BeginDisabled(selected_profile_index[slot_index] < 0);
        if (ImGui::Button("Re-Apply")) {
            if (selected_profile_index[slot_index] >= 0 &&
                selected_profile_index[slot_index] < (int)profiles.size()) {
                ApplyToSlot(slot_index, profiles[selected_profile_index[slot_index]]);
                current_preset[slot_index] = static_cast<int>(profiles[selected_profile_index[slot_index]].controller_preset);
                current_team[slot_index] = static_cast<int>(profiles[selected_profile_index[slot_index]].team_preference);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Re-apply the current profile to the game");
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        // Save button - saves current slot settings to selected profile
        if (ImGui::Button("Save")) {
            auto p_profile = CGameManager::get_profile(slot_index);
            if (p_profile) {
                PersistentProfile new_prof;

                if (selected_profile_index[slot_index] >= 0 &&
                    selected_profile_index[slot_index] < (int)profiles.size()) {
                    // Update existing profile
                    new_prof = profiles[selected_profile_index[slot_index]];
                } else {
                    // Create new filename based on slot name
                    char name_buffer[256];
                    String::convert(name_buffer, p_profile->name, 256);
                    new_prof.filename = std::string(name_buffer) + ".json";
                }

                new_prof.display_name = p_profile->name;
                memcpy(new_prof.service_tag, p_profile->profile.ServiceTag, 4 * sizeof(wchar_t));
                new_prof.service_tag[4] = L'\0';
                new_prof.controller_preset = static_cast<ControllerPreset>(current_preset[slot_index]);
                new_prof.team_preference = static_cast<Team>(current_team[slot_index]);
                memcpy(&new_prof.user_profile, &p_profile->profile, sizeof(CUserProfile));
                memcpy(&new_prof.gamepad_mapping, &p_profile->mapping, sizeof(CGamepadMapping));

                if (SaveProfile(new_prof, new_prof.filename)) {
                    // Reload profiles to update the list
                    LoadAllProfiles();

                    // Find and select the saved profile
                    for (int i = 0; i < (int)profiles.size(); i++) {
                        if (profiles[i].filename == new_prof.filename) {
                            selected_profile_index[slot_index] = i;
                            break;
                        }
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Save current slot settings (armor, controls, etc.) to profile");
        }

        ImGui::SameLine();

        // New button
        if (ImGui::Button("New")) {
            show_new_profile_popup = true;
            popup_slot = slot_index;
            memset(new_profile_name, 0, sizeof(new_profile_name));
        }

        ImGui::SameLine();

        // Delete button
        ImGui::BeginDisabled(selected_profile_index[slot_index] < 0);
        if (ImGui::Button("Delete")) {
            if (selected_profile_index[slot_index] >= 0 &&
                selected_profile_index[slot_index] < (int)profiles.size()) {
                DeleteProfile(profiles[selected_profile_index[slot_index]].filename);
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        // Refresh button
        if (ImGui::Button("Refresh")) {
            LoadAllProfiles();
        }

        // Show rank info if profile is selected
        if (selected_profile_index[slot_index] >= 0 &&
            selected_profile_index[slot_index] < (int)profiles.size()) {
            auto& prof = profiles[selected_profile_index[slot_index]];
            ImGui::Text("Rank: %s (Level %d) - %d XP", GetRankName(prof.rank_level), prof.rank_level, prof.rank_xp);
        }

        // Controller preset selector (works on current slot, independent of profiles)
        ImGui::Separator();
        ImGui::Text("Controller Preset:");
        ImGui::SameLine();

        const char* preset_names[] = {
            "Default", "Bumper Jumper", "Fishstick", "Recon",
            "Universal Reclaimer", "Universal Zoom & Shoot", "Custom"
        };

        ImGui::PushItemWidth(180);
        if (ImGui::Combo("##ControllerPreset", &current_preset[slot_index], preset_names, IM_ARRAYSIZE(preset_names))) {
            auto p_slot = CGameManager::get_profile(slot_index);
            if (p_slot) {
                ControllerPreset preset = static_cast<ControllerPreset>(current_preset[slot_index]);
                if (preset != ControllerPreset::Custom) {
                    ApplyControllerPreset(p_slot->mapping, preset);
                    auto p_engine = GameEngine();
                    if (MCC::IsInGame() && p_engine) {
                        p_engine->load_setting();
                    }
                    LOG_INFO("Applied controller preset '{}' to slot {}", GetPresetName(preset), slot_index);
                }
            }
        }
        ImGui::PopItemWidth();

        if (current_preset[slot_index] == static_cast<int>(ControllerPreset::Custom)) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Edit in Gamepad Mapping below)");
        }

        // Team selector
        ImGui::Text("Team Preference:");
        ImGui::SameLine();

        const char* team_names[] = {
            "Red", "Blue", "Green", "Orange", "Purple", "Gold", "Brown", "Pink"
        };

        // Team colors for visual feedback
        ImVec4 team_colors[] = {
            ImVec4(1.0f, 0.2f, 0.2f, 1.0f),  // Red
            ImVec4(0.2f, 0.4f, 1.0f, 1.0f),  // Blue
            ImVec4(0.2f, 0.8f, 0.2f, 1.0f),  // Green
            ImVec4(1.0f, 0.6f, 0.0f, 1.0f),  // Orange
            ImVec4(0.6f, 0.2f, 0.8f, 1.0f),  // Purple
            ImVec4(1.0f, 0.84f, 0.0f, 1.0f), // Gold
            ImVec4(0.6f, 0.4f, 0.2f, 1.0f),  // Brown
            ImVec4(1.0f, 0.4f, 0.7f, 1.0f)   // Pink
        };

        ImGui::PushItemWidth(120);
        // Show colored preview of current team
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(team_colors[current_team[slot_index]].x * 0.3f,
                                                        team_colors[current_team[slot_index]].y * 0.3f,
                                                        team_colors[current_team[slot_index]].z * 0.3f, 1.0f));
        if (ImGui::Combo("##TeamPreference", &current_team[slot_index], team_names, IM_ARRAYSIZE(team_names))) {
            Team team = static_cast<Team>(current_team[slot_index]);
            // If in-game, immediately change team
            if (MCC::IsInGame()) {
                ChangePlayerTeam(slot_index, team);
            }
            LOG_INFO("Set team preference for slot {} to {}", slot_index, GetTeamName(team));
        }
        ImGui::PopStyleColor();
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (MCC::IsInGame()) {
            if (ImGui::Button("Change Now")) {
                ChangePlayerTeam(slot_index, static_cast<Team>(current_team[slot_index]));
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Change team immediately in the current game");
            }
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Applied when loading profile in-game)");
        }

        // Appearance section (emblem and colors)
        if (selected_profile_index[slot_index] >= 0 &&
            selected_profile_index[slot_index] < (int)profiles.size()) {
            auto& prof = profiles[selected_profile_index[slot_index]];
            bool changed = false;

            // Helper macro for randomize buttons
            #define RAND_BTN(label, field, max_val) \
                ImGui::SameLine(); \
                if (ImGui::SmallButton("R##" label)) { \
                    field = RandomArmor(max_val); \
                    changed = true; \
                } \
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Randomize " label);

            #define RAND_COLOR_BTN(label, field) \
                ImGui::SameLine(); \
                if (ImGui::SmallButton("R##" label)) { \
                    field = RandomColor(); \
                    changed = true; \
                } \
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Randomize " label);

            ImGui::Separator();
            if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
                // Randomize All button at top
                if (ImGui::Button("Randomize Everything")) {
                    prof.RandomizeAppearance();
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Randomize all colors, armor, emblem, and service tag");
                }

                ImGui::SameLine();
                if (ImGui::Button("Randomize Colors Only")) {
                    prof.user_profile.PlayerModelPrimaryColorIndex = RandomColor();
                    prof.user_profile.PlayerModelSecondaryColorIndex = RandomColor();
                    prof.user_profile.PlayerModelTertiaryColorIndex = RandomColor();
                    prof.user_profile.PlayerModelPrimaryColor = prof.user_profile.PlayerModelPrimaryColorIndex;
                    prof.user_profile.PlayerModelSecondaryColor = prof.user_profile.PlayerModelSecondaryColorIndex;
                    prof.user_profile.PlayerModelTertiaryColor = prof.user_profile.PlayerModelTertiaryColorIndex;
                    changed = true;
                }

                ImGui::SameLine();
                if (ImGui::Button("Randomize Armor Only")) {
                    prof.user_profile.HelmetIndex = RandomArmor(NUM_HELMETS);
                    prof.user_profile.LeftShoulderIndex = RandomArmor(NUM_SHOULDERS);
                    prof.user_profile.RightShoulderIndex = RandomArmor(NUM_SHOULDERS);
                    prof.user_profile.ChestIndex = RandomArmor(NUM_CHESTS);
                    prof.user_profile.ArmsIndex = RandomArmor(NUM_ARMS);
                    prof.user_profile.LegsIndex = RandomArmor(NUM_LEGS);
                    prof.user_profile.VisorColorIndex = RandomArmor(NUM_VISORS);
                    changed = true;
                }

                ImGui::Separator();

                // Model type
                if (ImGui::Checkbox("Elite Model", &prof.user_profile.UseEliteModel)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("R##Model")) {
                    std::uniform_int_distribution<int> dist(0, 1);
                    prof.user_profile.UseEliteModel = dist(GetRNG()) == 1;
                    changed = true;
                }

                ImGui::Separator();
                ImGui::Text("Colors:");

                // Primary Color
                ImGui::Text("  Primary: %d", prof.user_profile.PlayerModelPrimaryColorIndex);
                RAND_COLOR_BTN("PrimaryColor", prof.user_profile.PlayerModelPrimaryColorIndex);
                prof.user_profile.PlayerModelPrimaryColor = prof.user_profile.PlayerModelPrimaryColorIndex;

                // Secondary Color
                ImGui::Text("  Secondary: %d", prof.user_profile.PlayerModelSecondaryColorIndex);
                RAND_COLOR_BTN("SecondaryColor", prof.user_profile.PlayerModelSecondaryColorIndex);
                prof.user_profile.PlayerModelSecondaryColor = prof.user_profile.PlayerModelSecondaryColorIndex;

                // Tertiary Color
                ImGui::Text("  Tertiary: %d", prof.user_profile.PlayerModelTertiaryColorIndex);
                RAND_COLOR_BTN("TertiaryColor", prof.user_profile.PlayerModelTertiaryColorIndex);
                prof.user_profile.PlayerModelTertiaryColor = prof.user_profile.PlayerModelTertiaryColorIndex;

                ImGui::Separator();
                ImGui::Text("Spartan Armor:");

                ImGui::Text("  Helmet: %d", prof.user_profile.HelmetIndex);
                RAND_BTN("Helmet", prof.user_profile.HelmetIndex, NUM_HELMETS);

                ImGui::Text("  Left Shoulder: %d", prof.user_profile.LeftShoulderIndex);
                RAND_BTN("LeftShoulder", prof.user_profile.LeftShoulderIndex, NUM_SHOULDERS);

                ImGui::Text("  Right Shoulder: %d", prof.user_profile.RightShoulderIndex);
                RAND_BTN("RightShoulder", prof.user_profile.RightShoulderIndex, NUM_SHOULDERS);

                ImGui::Text("  Chest: %d", prof.user_profile.ChestIndex);
                RAND_BTN("Chest", prof.user_profile.ChestIndex, NUM_CHESTS);

                ImGui::Text("  Arms: %d", prof.user_profile.ArmsIndex);
                RAND_BTN("Arms", prof.user_profile.ArmsIndex, NUM_ARMS);

                ImGui::Text("  Legs: %d", prof.user_profile.LegsIndex);
                RAND_BTN("Legs", prof.user_profile.LegsIndex, NUM_LEGS);

                ImGui::Text("  Visor: %d", prof.user_profile.VisorColorIndex);
                RAND_BTN("Visor", prof.user_profile.VisorColorIndex, NUM_VISORS);

                if (prof.user_profile.UseEliteModel) {
                    ImGui::Separator();
                    ImGui::Text("Elite Armor:");

                    ImGui::Text("  Helmet: %d", prof.user_profile.EliteHelmetIndex);
                    RAND_BTN("EliteHelmet", prof.user_profile.EliteHelmetIndex, 20);

                    ImGui::Text("  Left Shoulder: %d", prof.user_profile.EliteLeftShoulderIndex);
                    RAND_BTN("EliteLeftShoulder", prof.user_profile.EliteLeftShoulderIndex, 10);

                    ImGui::Text("  Right Shoulder: %d", prof.user_profile.EliteRightShoulderIndex);
                    RAND_BTN("EliteRightShoulder", prof.user_profile.EliteRightShoulderIndex, 10);

                    ImGui::Text("  Chest: %d", prof.user_profile.EliteChestIndex);
                    RAND_BTN("EliteChest", prof.user_profile.EliteChestIndex, 10);
                }

                ImGui::Separator();
                ImGui::Text("Emblem:");

                ImGui::Text("  Foreground: %d", prof.emblem.foreground);
                RAND_BTN("EmblemFG", prof.emblem.foreground, NUM_EMBLEM_FOREGROUNDS);

                ImGui::Text("  Background: %d", prof.emblem.background);
                RAND_BTN("EmblemBG", prof.emblem.background, NUM_EMBLEM_BACKGROUNDS);

                ImGui::Text("  Primary Color: %d", prof.emblem.primary_color);
                RAND_BTN("EmblemPrimary", prof.emblem.primary_color, NUM_EMBLEM_COLORS);

                ImGui::Text("  Secondary Color: %d", prof.emblem.secondary_color);
                RAND_BTN("EmblemSecondary", prof.emblem.secondary_color, NUM_EMBLEM_COLORS);

                ImGui::Text("  Background Color: %d", prof.emblem.background_color);
                RAND_BTN("EmblemBGColor", prof.emblem.background_color, NUM_EMBLEM_COLORS);

                ImGui::SameLine();
                if (ImGui::SmallButton("R All##Emblem")) {
                    prof.emblem.Randomize();
                    changed = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Randomize entire emblem");

                ImGui::Separator();

                // Service tag with individual char randomizers
                char tag_buf[8];
                for (int i = 0; i < 4; i++) tag_buf[i] = static_cast<char>(prof.service_tag[i]);
                tag_buf[4] = '\0';
                ImGui::Text("Service Tag: %s", tag_buf);
                ImGui::SameLine();
                if (ImGui::SmallButton("R##Tag")) {
                    for (int i = 0; i < 4; i++) {
                        prof.service_tag[i] = RandomTagChar();
                        prof.user_profile.ServiceTag[i] = prof.service_tag[i];
                    }
                    changed = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Randomize service tag");

                // Save and auto-apply if anything changed
                if (changed) {
                    SaveProfile(prof, prof.filename);
                    // Auto-apply the changes to the game
                    ApplyToSlot(slot_index, prof);
                    LOG_INFO("Auto-applied changes to slot {}", slot_index);
                }
            }

            #undef RAND_BTN
            #undef RAND_COLOR_BTN
        }

        // Show settings status
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        if (!p_setting->b_override_profile || p_setting->b_use_player0_profile) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                "Note: Load will auto-enable required settings");
        }

        ImGui::PopID();

        // New profile popup (handled outside PushID to avoid ID conflicts)
        if (show_new_profile_popup && popup_slot == slot_index) {
            ImGui::OpenPopup("New Profile");
            show_new_profile_popup = false;
        }

        if (ImGui::BeginPopupModal("New Profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static bool randomize_appearance = true;

            ImGui::Text("Enter profile name:");
            ImGui::InputText("##new_profile_name", new_profile_name, sizeof(new_profile_name));

            ImGui::Checkbox("Randomize colors, emblem & service tag", &randomize_appearance);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Generate random armor colors, emblem design, and service tag");
            }

            if (ImGui::Button("Create", ImVec2(120, 0))) {
                if (strlen(new_profile_name) > 0) {
                    auto p_profile = CGameManager::get_profile(popup_slot);
                    if (p_profile) {
                        PersistentProfile new_prof;
                        new_prof.filename = std::string(new_profile_name) + ".json";
                        new_prof.display_name = utf8_to_wstring(new_profile_name);
                        new_prof.controller_preset = static_cast<ControllerPreset>(current_preset[popup_slot]);
                        new_prof.team_preference = static_cast<Team>(current_team[popup_slot]);
                        memcpy(&new_prof.user_profile, &p_profile->profile, sizeof(CUserProfile));
                        memcpy(&new_prof.gamepad_mapping, &p_profile->mapping, sizeof(CGamepadMapping));

                        if (randomize_appearance) {
                            // Randomize colors, emblem, and service tag
                            new_prof.RandomizeAppearance();
                        } else {
                            // Copy existing service tag
                            memcpy(new_prof.service_tag, p_profile->profile.ServiceTag, 4 * sizeof(wchar_t));
                            new_prof.service_tag[4] = L'\0';
                        }

                        if (SaveProfile(new_prof, new_prof.filename)) {
                            LoadAllProfiles();

                            // Find and select the new profile
                            for (int i = 0; i < (int)profiles.size(); i++) {
                                if (profiles[i].filename == new_prof.filename) {
                                    selected_profile_index[popup_slot] = i;
                                    break;
                                }
                            }
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

}
