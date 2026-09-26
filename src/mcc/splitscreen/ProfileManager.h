#pragma once

#include <string>
#include <vector>
#include "../CUserProfile.h"
#include "../CGamepadMapping.h"

namespace MCC::Splitscreen {

    // Controller preset types - Universal presets only (present in all MCC games)
    // Mappings based on Halo Reach as primary, with game-specific actions (Sprint, etc.) from H4/H3/H2
    enum class ControllerPreset {
        UniversalDefaultRecon = 0,
        UniversalReclaimer = 1,
        UniversalZoomAndShoot = 2,
        UniversalBumpAndJump = 3,
        UniversalGreenThumbs = 4,
        UniversalInfinite = 5,
        Custom = 6,
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

    // Career stats (persisted across sessions)
    struct CareerStats {
        int games_played = 0;
        int wins = 0;
        int losses = 0;
        int total_kills = 0;
        int total_deaths = 0;
        int total_assists = 0;
        int total_score = 0;

        float GetKDRatio() const {
            return total_deaths > 0 ? (float)total_kills / total_deaths : (float)total_kills;
        }
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
        CareerStats career_stats;       // Persistent career statistics

        PersistentProfile();
        void RandomizeAppearance();     // Randomize colors, emblem, armor
    };

    class ProfileManager {
    public:
        static std::vector<PersistentProfile> profiles;
        static std::string selected_profile_key[4]; // Selected profile KEY (filename) per slot - stable across Refresh
        static bool slot_dirty[4];            // Has unsaved changes
        static int current_preset[4];         // UI state: controller preset per slot
        static int current_team[4];           // UI state: team preference per slot

        static bool LoadAllProfiles();
        static bool SaveProfile(const PersistentProfile& profile, const std::string& filename);
        static bool DeleteProfile(const std::string& filename);
        static void ApplyToSlot(int slot_index, const PersistentProfile& profile);
        static std::string GetProfilesPath();
        static int ComputeRankLevel(int xp);
        static const char* GetRankName(int level);

        // Key-based profile lookup (returns -1 if not found)
        static int FindProfileIndexByKey(const std::string& key);
        // Get profile by key (returns nullptr if not found)
        static PersistentProfile* GetProfileByKey(const std::string& key);

        // Sync UI state arrays from loaded profile (call after Refresh/Load)
        static void SyncUIStateFromProfile(int slot_index);
        // Sync all slots after a bulk reload
        static void SyncAllUIState();

        static void ImGuiProfileSelector(int slot_index);

        // Auto-load profile from instance config (called after LoadAllProfiles)
        // Only loads once per session, subsequent calls are no-ops
        static void TryAutoLoadFromConfig();

        // Apply pending team preferences when game starts
        static void ApplyPendingTeams();

        // Manual team apply - force team application for all active slots
        static void ApplyTeamsNow(const char* reason);

        // Reset team state (call on init/relaunch to prevent stale state)
        static void ResetTeamState();

        // Dirty tracking for unsaved changes
        static void MarkDirty(int slot_index);
        static bool IsSlotDirty(int slot_index);
        static void RevertSlot(int slot_index);
        static void ClearDirty(int slot_index);

        // Armor state machine (two-shot delayed application)
        static void MarkArmorDirty(int slot_index);
        static void ProcessPendingArmor();  // Call every frame
        static void ApplyArmorNow(int slot_index, const char* reason);
        static void ScheduleArmorTwoShot(int slot_index);  // Start two-shot apply for match start

        // Stats and history system
        static void OnMatchStart();
        static void OnMatchEnd();
        static void UpdateLiveStats(int slot_index, int kills, int deaths, int assists, int score);
        static void WriteMatchHistory();
        static std::string GetHistoryPath();
        static int GetMatchEpoch() { return s_match_start_epoch; }

    private:
        static bool EnsureProfilesDirectory();
        static bool s_auto_load_attempted;
        static int s_match_start_epoch;
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

    constexpr int PROFILE_VERSION = 4;

    // Number of available colors in MCC palette
    constexpr int NUM_ARMOR_COLORS = 32;
    constexpr int NUM_EMBLEM_FOREGROUNDS = 128;
    constexpr int NUM_EMBLEM_BACKGROUNDS = 32;
    constexpr int NUM_EMBLEM_COLORS = 32;
}
