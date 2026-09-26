// JSON (de)serialization for MCC's CUserProfile and CGamepadMapping.
// Split out of ProfileManager.cpp so it can be unit-tested without the game.
#include "CUserProfileJson.h"

#include <cstring>
#include <algorithm>
#include <windows.h>

namespace MCC::Splitscreen {
    std::string wstring_to_utf8(const std::wstring& wstr) {
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

    std::wstring utf8_to_wstring(const std::string& str) {
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

    void to_json(json& j, const CUserProfile& p) {
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

    void from_json(const json& j, CUserProfile& p) {
        memset(&p, 0, sizeof(CUserProfile));
        merge_json(j, p);
    }

    // Like from_json, but only overwrites fields present in j (keeps the rest of p).
    void merge_json(const json& j, CUserProfile& p) {

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
        // Sensitivity fields: backward-compatible (bool -> default 3, int -> use value)
        if (j.contains("VerticalLookSensitivity")) {
            auto& v = j["VerticalLookSensitivity"];
            p.VerticalLookSensitivity = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("HorizontalLookSensitivity")) {
            auto& v = j["HorizontalLookSensitivity"];
            p.HorizontalLookSensitivity = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("LookAcceleration")) {
            auto& v = j["LookAcceleration"];
            p.LookAcceleration = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("LookAxialDeadZone")) p.LookAxialDeadZone = j["LookAxialDeadZone"].get<float>();
        if (j.contains("LookRadialDeadZone")) p.LookRadialDeadZone = j["LookRadialDeadZone"].get<float>();
        if (j.contains("ZoomLookSensitivityMultiplier")) p.ZoomLookSensitivityMultiplier = j["ZoomLookSensitivityMultiplier"].get<float>();
        if (j.contains("VehicleLookSensitivityMultiplier")) p.VehicleLookSensitivityMultiplier = j["VehicleLookSensitivityMultiplier"].get<float>();
        if (j.contains("ButtonPreset")) p.ButtonPreset = j["ButtonPreset"].get<bool>();
        if (j.contains("StickPreset")) p.StickPreset = j["StickPreset"].get<bool>();
        if (j.contains("LeftyToggle")) p.LeftyToggle = j["LeftyToggle"].get<bool>();
        // Theater/camera sensitivity fields: backward-compatible (bool -> default 3, int -> use value)
        if (j.contains("FlyingCameraTurnSensitivity")) {
            auto& v = j["FlyingCameraTurnSensitivity"];
            p.FlyingCameraTurnSensitivity = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("FlyingCameraPanning")) {
            auto& v = j["FlyingCameraPanning"];
            p.FlyingCameraPanning = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("FlyingCameraSpeed")) {
            auto& v = j["FlyingCameraSpeed"];
            p.FlyingCameraSpeed = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("FlyingCameraThrust")) {
            auto& v = j["FlyingCameraThrust"];
            p.FlyingCameraThrust = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("TheaterTurnSensitivity")) {
            auto& v = j["TheaterTurnSensitivity"];
            p.TheaterTurnSensitivity = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("TheaterPanning")) {
            auto& v = j["TheaterPanning"];
            p.TheaterPanning = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("TheaterSpeed")) {
            auto& v = j["TheaterSpeed"];
            p.TheaterSpeed = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("TheaterThrust")) {
            auto& v = j["TheaterThrust"];
            p.TheaterThrust = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("MKTheaterTurnSensitivity")) {
            auto& v = j["MKTheaterTurnSensitivity"];
            p.MKTheaterTurnSensitivity = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("MKTheaterPanning")) {
            auto& v = j["MKTheaterPanning"];
            p.MKTheaterPanning = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("MKTheaterSpeed")) {
            auto& v = j["MKTheaterSpeed"];
            p.MKTheaterSpeed = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
        if (j.contains("MKTheaterThrust")) {
            auto& v = j["MKTheaterThrust"];
            p.MKTheaterThrust = v.is_boolean() ? 3 : v.get<uint8_t>();
        }
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

        // Clamp sensitivity fields to valid ranges
        auto clamp8 = [](uint8_t& val, uint8_t lo, uint8_t hi) {
            if (val < lo) val = lo;
            if (val > hi) val = hi;
        };
        clamp8(p.VerticalLookSensitivity, 1, 10);
        clamp8(p.HorizontalLookSensitivity, 1, 10);
        clamp8(p.LookAcceleration, 1, 5);
        clamp8(p.FlyingCameraTurnSensitivity, 1, 10);
        clamp8(p.FlyingCameraPanning, 1, 10);
        clamp8(p.FlyingCameraSpeed, 1, 10);
        clamp8(p.FlyingCameraThrust, 1, 10);
        clamp8(p.TheaterTurnSensitivity, 1, 10);
        clamp8(p.TheaterPanning, 1, 10);
        clamp8(p.TheaterSpeed, 1, 10);
        clamp8(p.TheaterThrust, 1, 10);
        clamp8(p.MKTheaterTurnSensitivity, 1, 10);
        clamp8(p.MKTheaterPanning, 1, 10);
        clamp8(p.MKTheaterSpeed, 1, 10);
        clamp8(p.MKTheaterThrust, 1, 10);
    }

    void to_json(json& j, const CGamepadMapping& m) {
        json actions_array = json::array();
        for (int i = 0; i < 66; i++) {
            actions_array.push_back(static_cast<int>(m.actions[i]));
        }
        j = json{{"actions", actions_array}};
    }

    void from_json(const json& j, CGamepadMapping& m) {
        memset(&m, 0, sizeof(CGamepadMapping));
        if (j.contains("actions") && j["actions"].is_array()) {
            auto& actions = j["actions"];
            for (size_t i = 0; i < actions.size() && i < 66; i++) {
                m.actions[i] = static_cast<CGamepadMapping::eButton>(actions[i].get<int>());
            }
        }
    }
}
