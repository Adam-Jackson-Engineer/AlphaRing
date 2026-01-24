#pragma once

#include <string>
#include <vector>
#include "../CUserProfile.h"
#include "../CGamepadMapping.h"

namespace MCC::Splitscreen {

    // Controller preset types
    enum class ControllerPreset {
        Default = 0,
        BumperJumper,
        Fishstick,
        Recon,
        UniversalReclaimer,
        UniversalZoomAndShoot,
        Custom,
        COUNT
    };

    // Team colors
    enum class Team {
        Red = 0,
        Blue,
        Green,
        Orange,
        Purple,
        Gold,
        Brown,
        Pink,
        COUNT
    };

    const char* GetPresetName(ControllerPreset preset);
    const char* GetTeamName(Team team);
    void ApplyControllerPreset(CGamepadMapping& mapping, ControllerPreset preset);
    void ChangePlayerTeam(int slot_index, Team team);

    // Emblem configuration (for in-game display)
    struct EmblemConfig {
        int foreground;          // Foreground emblem shape (0-127)
        int background;          // Background emblem shape (0-31)
        int flags;               // Emblem flags (flip, etc.)
        int primary_color;       // Primary color index (0-31)
        int secondary_color;     // Secondary color index (0-31)
        int background_color;    // Background color index (0-31)

        EmblemConfig();
        void Randomize();
    };

    struct PersistentProfile {
        std::string filename;           // e.g., "player1.json"
        std::wstring display_name;      // Gamer tag (max 1024 chars)
        wchar_t service_tag[5];         // 4-char service tag + null terminator
        int rank_xp;                    // Simplified XP (0-10000)
        int rank_level;                 // Computed rank (1-50)
        ControllerPreset controller_preset; // Controller scheme preset
        Team team_preference;           // Preferred team for team games
        EmblemConfig emblem;            // Player emblem/insignia
        CUserProfile user_profile;      // Full game profile (armor, settings)
        CGamepadMapping gamepad_mapping; // Controller bindings

        PersistentProfile();
        void RandomizeAppearance();     // Randomize colors, emblem, armor
    };

    class ProfileManager {
    public:
        static std::vector<PersistentProfile> profiles;
        static int selected_profile_index[4]; // Selected profile index for each player slot

        static bool LoadAllProfiles();
        static bool SaveProfile(const PersistentProfile& profile, const std::string& filename);
        static bool DeleteProfile(const std::string& filename);
        static void ApplyToSlot(int slot_index, const PersistentProfile& profile);
        static std::string GetProfilesPath();
        static int ComputeRankLevel(int xp);
        static const char* GetRankName(int level);

        static void ImGuiProfileSelector(int slot_index);

        // Auto-load profile from instance config (called after LoadAllProfiles)
        // Only loads once per session, subsequent calls are no-ops
        static void TryAutoLoadFromConfig();

    private:
        static bool EnsureProfilesDirectory();
        static bool s_auto_load_attempted;
    };

    // XP thresholds for ranks
    constexpr int RANK_XP_THRESHOLDS[] = {
        0,      // Level 1: Recruit
        100,    // Level 2-9
        200,
        300,
        400,
        500,    // Level 10: Private
        700,
        900,
        1100,
        1300,
        1500,
        1700,
        1900,
        2000,   // Level 20: Corporal
        2200,
        2400,
        2600,
        2800,
        3000,
        3500,
        4000,
        4500,
        5000,   // Level 30: Sergeant
        5300,
        5600,
        5900,
        6200,
        6500,
        6800,
        7100,
        7400,
        7700,
        8000,   // Level 40: Lieutenant
        8200,
        8400,
        8600,
        8800,
        9000,
        9200,
        9400,
        9600,
        9800,
        10000   // Level 50: Captain
    };

    constexpr int PROFILE_VERSION = 2;

    // Number of available colors in MCC palette
    constexpr int NUM_ARMOR_COLORS = 32;
    constexpr int NUM_EMBLEM_FOREGROUNDS = 128;
    constexpr int NUM_EMBLEM_BACKGROUNDS = 32;
    constexpr int NUM_EMBLEM_COLORS = 32;
}
