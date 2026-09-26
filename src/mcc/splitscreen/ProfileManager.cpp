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

// ==============================================================================
// SectionBar - Full-width expandable section bar UI helper
// ==============================================================================
// Draws a full-width colored bar with expand/collapse chevron, consistent with
// the main expander style but slightly smaller for subsections.
//
// Returns true if the section is expanded (caller should render contents).
// The open_state is persisted via the provided bool pointer.
// ==============================================================================
static bool SectionBar(const char* label, bool* open_state, ImU32 bar_color = IM_COL32(46, 89, 148, 255)) {
    ImGui::PushID(label);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    float avail_width = ImGui::GetContentRegionAvail().x;
    float bar_height = ImGui::GetTextLineHeight() + 8.0f;  // Slightly smaller than main expanders

    // Draw background bar
    ImVec2 bar_min = cursor_pos;
    ImVec2 bar_max = ImVec2(cursor_pos.x + avail_width, cursor_pos.y + bar_height);

    // Hover detection
    ImGui::InvisibleButton("##section_bar", ImVec2(avail_width, bar_height));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();

    // Darken/lighten on hover
    ImU32 bg_color = bar_color;
    if (hovered) {
        // Lighten slightly
        int r = (bar_color >> 0) & 0xFF;
        int g = (bar_color >> 8) & 0xFF;
        int b = (bar_color >> 16) & 0xFF;
        r = (r + 30 > 255) ? 255 : r + 30;
        g = (g + 30 > 255) ? 255 : g + 30;
        b = (b + 30 > 255) ? 255 : b + 30;
        bg_color = IM_COL32(r, g, b, 255);
    }

    draw_list->AddRectFilled(bar_min, bar_max, bg_color, 3.0f);

    // Draw chevron (triangle indicator)
    float chevron_size = 8.0f;
    float chevron_x = bar_min.x + 8.0f;
    float chevron_y = bar_min.y + (bar_height - chevron_size) / 2.0f;

    if (*open_state) {
        // Down chevron (expanded)
        draw_list->AddTriangleFilled(
            ImVec2(chevron_x, chevron_y),
            ImVec2(chevron_x + chevron_size, chevron_y),
            ImVec2(chevron_x + chevron_size / 2.0f, chevron_y + chevron_size),
            IM_COL32(255, 255, 255, 220)
        );
    } else {
        // Right chevron (collapsed)
        draw_list->AddTriangleFilled(
            ImVec2(chevron_x, chevron_y),
            ImVec2(chevron_x + chevron_size, chevron_y + chevron_size / 2.0f),
            ImVec2(chevron_x, chevron_y + chevron_size),
            IM_COL32(255, 255, 255, 220)
        );
    }

    // Draw label text
    float text_x = bar_min.x + 24.0f;  // After chevron
    float text_y = bar_min.y + 4.0f;
    draw_list->AddText(ImVec2(text_x, text_y), IM_COL32(255, 255, 255, 255), label);

    // Toggle on click
    if (clicked) {
        *open_state = !(*open_state);
    }

    ImGui::PopID();

    // Return whether content should be shown
    return *open_state;
}

// Random number generator
static std::mt19937& GetRNG() {
    static std::mt19937 rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return rng;
}

namespace MCC::Splitscreen {

    std::vector<PersistentProfile> ProfileManager::profiles;
    std::string ProfileManager::selected_profile_key[4] = {"", "", "", ""};  // Key-based selection (stable across Refresh)
    bool ProfileManager::slot_dirty[4] = {false, false, false, false};
    int ProfileManager::current_preset[4] = {0, 0, 0, 0};  // UI state: controller preset
    int ProfileManager::current_team[4] = {0, 0, 0, 0};    // UI state: team preference
    bool ProfileManager::s_auto_load_attempted = false;

    // ==============================================================================
    // Key-based profile lookup helpers
    // ==============================================================================
    int ProfileManager::FindProfileIndexByKey(const std::string& key) {
        if (key.empty()) return -1;
        for (int i = 0; i < (int)profiles.size(); i++) {
            if (profiles[i].filename == key) {
                return i;
            }
        }
        return -1;  // Not found
    }

    PersistentProfile* ProfileManager::GetProfileByKey(const std::string& key) {
        int idx = FindProfileIndexByKey(key);
        if (idx >= 0 && idx < (int)profiles.size()) {
            return &profiles[idx];
        }
        return nullptr;
    }

    void ProfileManager::SyncUIStateFromProfile(int slot_index) {
        if (slot_index < 0 || slot_index >= 4) return;

        auto* prof = GetProfileByKey(selected_profile_key[slot_index]);
        if (prof) {
            current_preset[slot_index] = static_cast<int>(prof->controller_preset);
            current_team[slot_index] = static_cast<int>(prof->team_preference);
            LOG_INFO("[UI_SYNC] slot={} after sync: preset={} team={} key={}",
                slot_index,
                GetPresetName(prof->controller_preset),
                GetTeamName(prof->team_preference),
                selected_profile_key[slot_index]);
        } else if (!selected_profile_key[slot_index].empty()) {
            // Key was set but profile not found after reload - clear it
            LOG_WARNING("[UI_SYNC] slot={} key='{}' not found after reload - clearing",
                slot_index, selected_profile_key[slot_index]);
            selected_profile_key[slot_index] = "";
            current_preset[slot_index] = 0;
            current_team[slot_index] = 0;
        }
    }

    void ProfileManager::SyncAllUIState() {
        LOG_INFO("[UI_SYNC] Syncing all slots after profile reload");
        for (int i = 0; i < 4; i++) {
            SyncUIStateFromProfile(i);
        }
    }

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
          controller_preset(ControllerPreset::UniversalDefaultRecon), team_preference(Team::Red) {
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
            case ControllerPreset::UniversalDefaultRecon: return "Universal Default (Recon)";
            case ControllerPreset::UniversalReclaimer: return "Universal Reclaimer";
            case ControllerPreset::UniversalZoomAndShoot: return "Universal Zoom & Shoot";
            case ControllerPreset::UniversalBumpAndJump: return "Universal Bump & Jump";
            case ControllerPreset::UniversalGreenThumbs: return "Universal Green Thumbs";
            case ControllerPreset::UniversalInfinite: return "Universal Infinite";
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
        LOG_INFO("[ROSTER] Changed player {} to team {}", slot_index, GetTeamName(team));
    }

    void ProfileManager::ApplyPendingTeams() {
        if (!MCC::IsInGame()) return;

        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        for (int i = 0; i < p_setting->player_count && i < 4; i++) {
            auto p_profile = CGameManager::get_profile(i);
            if (p_profile && p_profile->team_pending) {
                ChangePlayerTeam(i, static_cast<Team>(p_profile->pending_team));
                p_profile->team_pending = false;
                LOG_INFO("[ROSTER] Applied pending team {} to slot {} (reason=auto_match_start)",
                    GetTeamName(static_cast<Team>(p_profile->pending_team)), i);
            }
        }
    }

    void ProfileManager::ApplyTeamsNow(const char* reason) {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        LOG_INFO("[ROSTER] ApplyTeamsNow called (reason={})", reason);

        if (!MCC::IsInGame()) {
            // Schedule for later - set pending flags
            for (int i = 0; i < p_setting->player_count && i < 4; i++) {
                auto* prof = GetProfileByKey(selected_profile_key[i]);
                if (prof) {
                    auto p_profile = CGameManager::get_profile(i);
                    if (p_profile) {
                        p_profile->team_pending = true;
                        p_profile->pending_team = static_cast<int>(prof->team_preference);
                        LOG_INFO("[ROSTER]   Slot {}: scheduled team {} (pending)", i,
                            GetTeamName(static_cast<Team>(p_profile->pending_team)));
                    }
                }
            }
            return;
        }

        // Apply immediately
        for (int i = 0; i < p_setting->player_count && i < 4; i++) {
            auto* prof = GetProfileByKey(selected_profile_key[i]);
            if (prof) {
                Team team = prof->team_preference;
                ChangePlayerTeam(i, team);
                LOG_INFO("[ROSTER]   Slot {}: applied team {} immediately", i, GetTeamName(team));

                // Clear pending flag since we applied now
                auto p_profile = CGameManager::get_profile(i);
                if (p_profile) {
                    p_profile->team_pending = false;
                }
            }
        }
    }

    void ProfileManager::ResetTeamState() {
        LOG_INFO("[ROSTER] ResetTeamState() - clearing all pending team flags");
        for (int i = 0; i < 4; i++) {
            auto p_profile = CGameManager::get_profile(i);
            if (p_profile) {
                p_profile->team_pending = false;
                p_profile->pending_team = 0;
            }
        }
    }

    void ProfileManager::MarkDirty(int slot_index) {
        if (slot_index >= 0 && slot_index < 4) {
            slot_dirty[slot_index] = true;
        }
    }

    bool ProfileManager::IsSlotDirty(int slot_index) {
        if (slot_index >= 0 && slot_index < 4) {
            return slot_dirty[slot_index];
        }
        return false;
    }

    void ProfileManager::ClearDirty(int slot_index) {
        if (slot_index >= 0 && slot_index < 4) {
            slot_dirty[slot_index] = false;
        }
    }

    void ProfileManager::RevertSlot(int slot_index) {
        if (slot_index < 0 || slot_index >= 4) return;
        auto* prof = GetProfileByKey(selected_profile_key[slot_index]);
        if (!prof) return;

        // Re-apply the saved profile to revert changes
        ApplyToSlot(slot_index, *prof);
        slot_dirty[slot_index] = false;
        LOG_INFO("Reverted slot {} to saved profile '{}'", slot_index, selected_profile_key[slot_index]);
    }

    // ==============================================================================
    // Armor State Machine - Two-shot delayed application
    // ==============================================================================
    // MCC/Reach armor requires TWO load_setting() calls with a delay to reliably
    // apply in subsequent matches. State machine:
    //   0: Idle - no pending armor
    //   1: Pending - UI change detected, waiting for debounce delay
    //   2: FirstApply - first load_setting() called, waiting before second shot
    //   3: SecondApply - second load_setting() called, will transition to Idle
    //
    // At match start, armor is ALWAYS scheduled for two-shot apply.
    // ==============================================================================

    constexpr int ARMOR_DEBOUNCE_FRAMES = 30;   // ~0.5 sec debounce after UI change
    constexpr int ARMOR_BETWEEN_SHOTS_FRAMES = 60;  // ~1 sec between first and second apply

    void ProfileManager::MarkArmorDirty(int slot_index) {
        if (slot_index < 0 || slot_index >= 4) return;
        auto p_slot = CGameManager::get_profile(slot_index);
        if (!p_slot) return;

        // Only transition to Pending if we're Idle or already Pending
        // If we're in FirstApply/SecondApply, let that complete first
        if (p_slot->armor_state == 0 || p_slot->armor_state == 1) {
            if (p_slot->armor_state != 1) {
                LOG_INFO("[ARMOR] Slot {} state: Idle -> Pending (UI change)", slot_index);
            }
            p_slot->armor_state = 1;  // Pending
            p_slot->armor_state_frames = 0;  // Reset debounce timer
        }
    }

    void ProfileManager::ProcessPendingArmor() {
        // Called every frame from ImGuiContext
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();

        for (int i = 0; i < 4; i++) {
            auto p_slot = CGameManager::get_profile(i);
            if (!p_slot || p_slot->armor_state == 0) continue;

            p_slot->armor_state_frames++;

            switch (p_slot->armor_state) {
                case 1:  // Pending - waiting for debounce (or stagger if frames started negative)
                    // Note: armor_state_frames can be negative for staggered starts
                    if (p_slot->armor_state_frames >= ARMOR_DEBOUNCE_FRAMES) {
                        LOG_INFO("[ARMOR] Slot {} state: Pending -> FirstApply (debounce/stagger complete)", i);
                        ApplyArmorNow(i, "first_shot");
                        p_slot->armor_state = 2;  // FirstApply
                        p_slot->armor_state_frames = 0;
                    }
                    break;

                case 2:  // FirstApply - waiting before second shot
                    if (p_slot->armor_state_frames >= ARMOR_BETWEEN_SHOTS_FRAMES) {
                        LOG_INFO("[ARMOR] Slot {} state: FirstApply -> SecondApply", i);
                        ApplyArmorNow(i, "second_shot");
                        p_slot->armor_state = 3;  // SecondApply
                        p_slot->armor_state_frames = 0;
                    }
                    break;

                case 3:  // SecondApply - complete
                    LOG_INFO("[ARMOR] Slot {} state: SecondApply -> Idle (complete, epoch={})", i, s_match_start_epoch);
                    p_slot->armor_state = 0;  // Idle
                    p_slot->armor_state_frames = 0;
                    p_slot->armor_match_epoch = s_match_start_epoch;
                    break;
            }
        }
    }

    void ProfileManager::ApplyArmorNow(int slot_index, const char* reason) {
        if (slot_index < 0 || slot_index >= 4) return;
        auto p_slot = CGameManager::get_profile(slot_index);
        if (!p_slot) return;

        auto p_setting = AlphaRing::Global::MCC::Splitscreen();

        // CRITICAL: Ensure BOTH flags are set correctly for per-player armor
        if (!p_setting->b_override_profile) {
            p_setting->b_override_profile = true;
            LOG_INFO("[ARMOR] Auto-enabled 'Override profile' for custom armor");
        }
        if (p_setting->b_use_player0_profile && slot_index > 0) {
            // IMPORTANT: If we're changing armor for slot > 0, we MUST disable this
            // Otherwise all slots return Player 0's profile!
            p_setting->b_use_player0_profile = false;
            LOG_INFO("[ARMOR] Auto-disabled 'Use player1's profile' - each slot needs own armor");
        }

        LOG_INFO("[ARMOR] ApplyArmorNow slot={} reason={} epoch={}", slot_index, reason, s_match_start_epoch);
        LOG_INFO("[ARMOR]   helmet={} chest={} lshoulder={} rshoulder={}",
            p_slot->profile.HelmetIndex,
            p_slot->profile.ChestIndex,
            p_slot->profile.LeftShoulderIndex,
            p_slot->profile.RightShoulderIndex);
        LOG_INFO("[ARMOR]   arms={} legs={} visor={} primary={} secondary={}",
            p_slot->profile.ArmsIndex,
            p_slot->profile.LegsIndex,
            p_slot->profile.VisorColorIndex,
            p_slot->profile.PlayerModelPrimaryColorIndex,
            p_slot->profile.PlayerModelSecondaryColorIndex);

        auto p_engine = GameEngine();
        if (MCC::IsInGame() && p_engine) {
            p_engine->load_setting();
            LOG_INFO("[ARMOR]   load_setting() called - game should refresh appearance");
        } else {
            LOG_INFO("[ARMOR]   Not in-game - armor will apply when match starts");
        }
    }

    // Schedule two-shot armor apply for a slot (called at match start)
    // IMPORTANT: Stagger by slot_index to prevent all slots from calling load_setting()
    // simultaneously, which can cause only one slot's armor to be applied.
    void ProfileManager::ScheduleArmorTwoShot(int slot_index) {
        if (slot_index < 0 || slot_index >= 4) return;
        auto p_slot = CGameManager::get_profile(slot_index);
        if (!p_slot) return;

        // Stagger by slot index: slot 0 starts immediately, slot 1 after 90 frames, etc.
        // This ensures load_setting() calls don't overlap and override each other.
        constexpr int STAGGER_PER_SLOT = 90;  // ~1.5 sec between slots

        int stagger_frames = slot_index * STAGGER_PER_SLOT;

        LOG_INFO("[ARMOR] Slot {} scheduling two-shot apply (match start, stagger={})",
            slot_index, stagger_frames);

        // Debug: Extra logging for slot 0 since it had issues
        if (slot_index == 0) {
            auto p_setting = AlphaRing::Global::MCC::Splitscreen();
            LOG_INFO("[ARMOR] Slot 0 debug: b_override={} b_override_profile={} b_use_player0_profile={}",
                p_setting->b_override, p_setting->b_override_profile, p_setting->b_use_player0_profile);
            LOG_INFO("[ARMOR] Slot 0 debug: helmet={} chest={} primary_color={}",
                p_slot->profile.HelmetIndex, p_slot->profile.ChestIndex,
                p_slot->profile.PlayerModelPrimaryColorIndex);
        }

        // Start in Pending state with negative frame count to delay the start
        p_slot->armor_state = 1;  // Pending (will transition to FirstApply after stagger)
        p_slot->armor_state_frames = -stagger_frames;  // Negative = still waiting
    }

    // ==============================================================================
    // Stats and History System
    // ==============================================================================

    int ProfileManager::s_match_start_epoch = 0;

    void ProfileManager::OnMatchStart() {
        s_match_start_epoch++;
        LOG_INFO("[SESSION] Match started (epoch={})", s_match_start_epoch);

        // Reset live stats for all slots
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        for (int i = 0; i < p_setting->player_count && i < 4; i++) {
            auto p_slot = CGameManager::get_profile(i);
            if (p_slot) {
                p_slot->stats_kills = 0;
                p_slot->stats_deaths = 0;
                p_slot->stats_assists = 0;
                p_slot->stats_score = 0;
            }
        }
    }

    void ProfileManager::OnMatchEnd() {
        LOG_INFO("[SESSION] Match ended (epoch={})", s_match_start_epoch);

        // Snapshot final stats and update career
        WriteMatchHistory();

        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        for (int i = 0; i < p_setting->player_count && i < 4; i++) {
            auto* profile = GetProfileByKey(selected_profile_key[i]);
            if (!profile) continue;

            auto p_slot = CGameManager::get_profile(i);
            if (p_slot) {
                // Update career stats
                profile->career_stats.games_played++;
                profile->career_stats.total_kills += p_slot->stats_kills;
                profile->career_stats.total_deaths += p_slot->stats_deaths;
                profile->career_stats.total_assists += p_slot->stats_assists;
                profile->career_stats.total_score += p_slot->stats_score;

                // Fast leveling: +10 per kill, +5 per assist, -2 per death
                int xp_gain = (p_slot->stats_kills * 10) + (p_slot->stats_assists * 5) - (p_slot->stats_deaths * 2);
                if (xp_gain < 5) xp_gain = 5;  // Minimum XP gain per match
                profile->rank_xp += xp_gain;
                profile->rank_level = ComputeRankLevel(profile->rank_xp);

                LOG_INFO("[STATS] Slot {} match end: K={} D={} A={} XP+={} (total XP={})",
                    i, p_slot->stats_kills, p_slot->stats_deaths, p_slot->stats_assists,
                    xp_gain, profile->rank_xp);

                // Auto-save profile with updated stats
                SaveProfile(*profile, profile->filename);
            }
        }
    }

    void ProfileManager::UpdateLiveStats(int slot_index, int kills, int deaths, int assists, int score) {
        if (slot_index < 0 || slot_index >= 4) return;
        auto p_slot = CGameManager::get_profile(slot_index);
        if (!p_slot) return;

        // Only log if changed
        if (p_slot->stats_kills != kills || p_slot->stats_deaths != deaths ||
            p_slot->stats_assists != assists || p_slot->stats_score != score) {
            LOG_INFO("[STATS] Slot {} updated: K={} D={} A={} S={}",
                slot_index, kills, deaths, assists, score);
        }

        p_slot->stats_kills = kills;
        p_slot->stats_deaths = deaths;
        p_slot->stats_assists = assists;
        p_slot->stats_score = score;
    }

    std::string ProfileManager::GetHistoryPath() {
        return GetProfilesPath() + "/../history";
    }

    void ProfileManager::WriteMatchHistory() {
        try {
            // Ensure history directory exists
            std::string history_dir = GetHistoryPath();
            if (!fs::exists(history_dir)) {
                fs::create_directories(history_dir);
            }

            // Get current date for filename
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm_buf;
            localtime_s(&tm_buf, &time);

            char date_buf[32];
            strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_buf);

            std::string filename = history_dir + "/" + date_buf + ".jsonl";

            // Build match record
            json match_record;
            match_record["timestamp"] = time;
            match_record["epoch"] = s_match_start_epoch;

            json players = json::array();
            auto p_setting = AlphaRing::Global::MCC::Splitscreen();
            for (int i = 0; i < p_setting->player_count && i < 4; i++) {
                auto p_slot = CGameManager::get_profile(i);
                if (!p_slot) continue;

                json player;
                char name_buf[256];
                wcstombs(name_buf, p_slot->name, sizeof(name_buf));
                player["slot"] = i;
                player["name"] = name_buf;
                player["kills"] = p_slot->stats_kills;
                player["deaths"] = p_slot->stats_deaths;
                player["assists"] = p_slot->stats_assists;
                player["score"] = p_slot->stats_score;
                players.push_back(player);
            }
            match_record["players"] = players;

            // Append to JSONL file
            std::ofstream file(filename, std::ios::app);
            if (file.is_open()) {
                file << match_record.dump() << "\n";
                LOG_INFO("[STATS] Match history written to {}", filename);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("[STATS] Failed to write match history: {}", e.what());
        }
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

        // Universal presets - present in all MCC games (Reach, CE, H2, H3, ODST, H4).
        // Base mappings from Halo Reach. Game-specific actions layered in:
        //   Sprint from H4, SwapReloadLeft/UseLeftWeapon from H2/H3.
        switch (preset) {
            case ControllerPreset::UniversalDefaultRecon:
                // Universal Default (Recon): The standard universal layout.
                // Reach: LB=SwitchGrenade, B=Melee, X=ArmorAbility, RB=Action/Reload
                // H4 adds: LB=Sprint | H3 adds: LB=SwapReloadLeft | H2 adds: LT=UseLeftWeapon
                mapping.actions[Jump] = A;
                mapping.actions[SwitchGrenades] = LB;
                mapping.actions[Action_] = RB;
                mapping.actions[Reload] = RB;
                mapping.actions[ChangeWeapon] = Y;
                mapping.actions[Melee] = B;
                mapping.actions[Flashlight] = DUp;
                mapping.actions[ThrowGrenade] = LT;
                mapping.actions[Fire] = RT;
                mapping.actions[Crouch] = LS;
                mapping.actions[Zoom] = RS;
                mapping.actions[Equipment] = X;
                mapping.actions[Scoreboard] = Back;
                mapping.actions[Sprint] = LB;
                mapping.actions[SwapReloadLeft] = LB;
                mapping.actions[UseLeftWeapon] = LT;
                break;

            case ControllerPreset::UniversalReclaimer:
                // Universal Reclaimer: RB=Melee, LB=ArmorAbility, B=Crouch, X=Action/Reload
                // H4 adds: LS=Sprint | H3 adds: LS=SwapReloadLeft | H2 adds: LT=UseLeftWeapon
                mapping.actions[Jump] = A;
                mapping.actions[SwitchGrenades] = DRight;
                mapping.actions[Action_] = X;
                mapping.actions[Reload] = X;
                mapping.actions[ChangeWeapon] = Y;
                mapping.actions[Melee] = RB;
                mapping.actions[Flashlight] = DUp;
                mapping.actions[ThrowGrenade] = LT;
                mapping.actions[Fire] = RT;
                mapping.actions[Crouch] = B;
                mapping.actions[Zoom] = RS;
                mapping.actions[Equipment] = LB;
                mapping.actions[Scoreboard] = Back;
                mapping.actions[Sprint] = LS;
                mapping.actions[SwapReloadLeft] = LS;
                mapping.actions[UseLeftWeapon] = LT;
                break;

            case ControllerPreset::UniversalZoomAndShoot:
                // Universal Zoom & Shoot: LT=Zoom, RB=ThrowGrenade, RS=Melee
                // H4 adds: LS=Sprint | H3 adds: LS=SwapReloadLeft | H2 adds: LT=UseLeftWeapon
                mapping.actions[Jump] = A;
                mapping.actions[SwitchGrenades] = DRight;
                mapping.actions[Action_] = X;
                mapping.actions[Reload] = X;
                mapping.actions[ChangeWeapon] = Y;
                mapping.actions[Melee] = RS;
                mapping.actions[Flashlight] = DUp;
                mapping.actions[ThrowGrenade] = RB;
                mapping.actions[Fire] = RT;
                mapping.actions[Crouch] = B;
                mapping.actions[Zoom] = LT;
                mapping.actions[Equipment] = LB;
                mapping.actions[Scoreboard] = Back;
                mapping.actions[Sprint] = LS;
                mapping.actions[SwapReloadLeft] = LS;
                mapping.actions[UseLeftWeapon] = LT;
                break;

            case ControllerPreset::UniversalBumpAndJump:
                // Universal Bump & Jump: LB=Jump, RB=Melee, B=Action/Reload
                // H4 adds: A=Sprint | H3 adds: A=SwapReloadLeft | H2 adds: LT=UseLeftWeapon
                mapping.actions[Jump] = LB;
                mapping.actions[SwitchGrenades] = DRight;
                mapping.actions[Action_] = B;
                mapping.actions[Reload] = B;
                mapping.actions[ChangeWeapon] = Y;
                mapping.actions[Melee] = RB;
                mapping.actions[Flashlight] = DUp;
                mapping.actions[ThrowGrenade] = LT;
                mapping.actions[Fire] = RT;
                mapping.actions[Crouch] = LS;
                mapping.actions[Zoom] = RS;
                mapping.actions[Equipment] = X;
                mapping.actions[Scoreboard] = Back;
                mapping.actions[Sprint] = A;
                mapping.actions[SwapReloadLeft] = A;
                mapping.actions[UseLeftWeapon] = LT;
                break;

            case ControllerPreset::UniversalGreenThumbs:
                // Universal Green Thumbs: RS=Melee, RB=Zoom, LB=ArmorAbility
                // H4 adds: LS=Sprint | H3 adds: LS=SwapReloadLeft | H2 adds: LT=UseLeftWeapon
                mapping.actions[Jump] = A;
                mapping.actions[SwitchGrenades] = DRight;
                mapping.actions[Action_] = X;
                mapping.actions[Reload] = X;
                mapping.actions[ChangeWeapon] = Y;
                mapping.actions[Melee] = RS;
                mapping.actions[Flashlight] = DUp;
                mapping.actions[ThrowGrenade] = LT;
                mapping.actions[Fire] = RT;
                mapping.actions[Crouch] = B;
                mapping.actions[Zoom] = RB;
                mapping.actions[Equipment] = LB;
                mapping.actions[Scoreboard] = Back;
                mapping.actions[Sprint] = LS;
                mapping.actions[SwapReloadLeft] = LS;
                mapping.actions[UseLeftWeapon] = LT;
                break;

            case ControllerPreset::UniversalInfinite:
                // Universal Infinite: LB=ThrowGrenade, LT=Zoom, RS=Melee, RB=ArmorAbility
                // H4 adds: LS=Sprint | H3 adds: LS=SwapReloadLeft | H2 adds: LT=UseLeftWeapon
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
                mapping.actions[Equipment] = RB;
                mapping.actions[Scoreboard] = Back;
                mapping.actions[Sprint] = LS;
                mapping.actions[SwapReloadLeft] = LS;
                mapping.actions[UseLeftWeapon] = LT;
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
        // Centralized profile storage location
        // All profiles are stored in RingChief directory for easy machine migration
        // The entire C:\RingChief folder can be zipped and moved to another machine
        static std::string s_cached_path = "C:\\RingChief\\profiles";
        LOG_INFO("[PATH] ProfilesDir={}", s_cached_path);
        return s_cached_path;
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
        LOG_INFO("[LOAD] ScanningDir={}", path);

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

                        if (j.contains("gamer_tag")) {
                            profile.display_name = utf8_to_wstring(j["gamer_tag"].get<std::string>());
                        } else if (j.contains("display_name")) {
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
                            // Migration from old enum to new Universal-only enum:
                            // Old: 0=Default, 1=BumperJumper, 2=Fishstick, 3=Recon,
                            //      4=Southpaw, 5=Boxer, 6=GreenThumb, 7=Custom
                            // New: 0=UniversalDefaultRecon, 1=UniversalReclaimer, 2=UniversalZoomAndShoot,
                            //      3=UniversalBumpAndJump, 4=UniversalGreenThumbs, 5=UniversalInfinite, 6=Custom
                            if (preset_val >= static_cast<int>(ControllerPreset::COUNT)) {
                                // Old values 7+ (Custom was 7) -> new Custom (6)
                                preset_val = static_cast<int>(ControllerPreset::Custom);
                                LOG_INFO("  Migrated old preset({}) -> Custom({})", j["controller_preset"].get<int>(), preset_val);
                            }
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

                        // Load career stats (persistent across sessions)
                        if (j.contains("career_stats") && j["career_stats"].is_object()) {
                            auto& cs = j["career_stats"];
                            if (cs.contains("games_played")) profile.career_stats.games_played = cs["games_played"].get<int>();
                            if (cs.contains("wins")) profile.career_stats.wins = cs["wins"].get<int>();
                            if (cs.contains("losses")) profile.career_stats.losses = cs["losses"].get<int>();
                            if (cs.contains("total_kills")) profile.career_stats.total_kills = cs["total_kills"].get<int>();
                            if (cs.contains("total_deaths")) profile.career_stats.total_deaths = cs["total_deaths"].get<int>();
                            if (cs.contains("total_assists")) profile.career_stats.total_assists = cs["total_assists"].get<int>();
                            if (cs.contains("total_score")) profile.career_stats.total_score = cs["total_score"].get<int>();
                            LOG_INFO("  Career stats: games={}, K/D={}/{}, K/D ratio={:.2f}",
                                profile.career_stats.games_played,
                                profile.career_stats.total_kills,
                                profile.career_stats.total_deaths,
                                profile.career_stats.GetKDRatio());
                        }

                        profiles.push_back(profile);
                        LOG_INFO("[LOAD] ProfileName={} version={} Path={}",
                            profile.filename, version, entry.path().string());
                        LOG_INFO("[LOAD] Fields: preset={}, team={}, helmet={}, tag={}",
                            GetPresetName(profile.controller_preset),
                            GetTeamName(profile.team_preference),
                            profile.user_profile.HelmetIndex,
                            wstring_to_utf8(std::wstring(profile.service_tag, 4)));
                    } catch (const json::exception& e) {
                        LOG_ERROR("Failed to parse profile {}: {}", entry.path().string(), e.what());
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to enumerate profiles: {}", e.what());
            return false;
        }

        LOG_INFO("[LOAD] Loaded {} profiles from {}", profiles.size(), path);

        // CRITICAL: Sync UI state for all slots after reload
        // This fixes the "Refresh changes team but UI doesn't update" bug
        SyncAllUIState();

        return true;
    }

    bool ProfileManager::SaveProfile(const PersistentProfile& profile, const std::string& filename) {
        if (!EnsureProfilesDirectory()) {
            return false;
        }

        std::string filepath = GetProfilesPath() + "/" + filename;

        try {
            // Load existing JSON first to preserve fields we don't manage
            // (e.g., ProfileEditor's name-based emblem fields, nameplate data)
            json j;
            {
                std::ifstream existing(filepath);
                if (existing.is_open()) {
                    try {
                        j = json::parse(existing);
                    } catch (...) {
                        j = json::object();
                    }
                }
            }

            j["version"] = PROFILE_VERSION;
            j["gamer_tag"] = wstring_to_utf8(profile.display_name);
            j.erase("display_name");
            j["service_tag"] = wstring_to_utf8(std::wstring(profile.service_tag, 4));
            j["rank_xp"] = profile.rank_xp;
            j["controller_preset"] = static_cast<int>(profile.controller_preset);
            j["team_preference"] = static_cast<int>(profile.team_preference);

            // Emblem configuration
            if (!j.contains("emblem") || !j["emblem"].is_object()) {
                j["emblem"] = json::object();
            }
            j["emblem"]["foreground"] = profile.emblem.foreground;
            j["emblem"]["background"] = profile.emblem.background;
            j["emblem"]["flags"] = profile.emblem.flags;
            j["emblem"]["primary_color"] = profile.emblem.primary_color;
            j["emblem"]["secondary_color"] = profile.emblem.secondary_color;
            j["emblem"]["background_color"] = profile.emblem.background_color;

            // Career stats - merge into existing to preserve any extra fields
            if (!j.contains("career_stats") || !j["career_stats"].is_object()) {
                j["career_stats"] = json::object();
            }
            j["career_stats"]["games_played"] = profile.career_stats.games_played;
            j["career_stats"]["wins"] = profile.career_stats.wins;
            j["career_stats"]["losses"] = profile.career_stats.losses;
            j["career_stats"]["total_kills"] = profile.career_stats.total_kills;
            j["career_stats"]["total_deaths"] = profile.career_stats.total_deaths;
            j["career_stats"]["total_assists"] = profile.career_stats.total_assists;
            j["career_stats"]["total_score"] = profile.career_stats.total_score;

            json user_profile_json;
            to_json(user_profile_json, profile.user_profile);
            j["user_profile"] = user_profile_json;

            json gamepad_json;
            to_json(gamepad_json, profile.gamepad_mapping);
            j["gamepad_mapping"] = gamepad_json;

            std::string json_str = j.dump(2);

            std::ofstream file(filepath);
            if (!file.is_open()) {
                LOG_ERROR("[SAVE] Failed to open for writing: {}", filepath);
                return false;
            }

            file << json_str;
            file.close();

            LOG_INFO("[SAVE] AbsolutePath={} bytes={}", filepath, json_str.size());
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

                // Clear selection for any slots that had this profile selected
                for (int i = 0; i < 4; i++) {
                    if (selected_profile_key[i] == filename) {
                        selected_profile_key[i] = "";
                        LOG_INFO("[DELETE] Cleared selection for slot {} (deleted profile)", i);
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

        LOG_INFO("[ROSTER] Applying profile '{}' to slot {}", wstring_to_utf8(profile.display_name), slot_index);

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

        LOG_INFO("[ARMOR] ApplyToSlot slot={} helmet={} chest={} lshoulder={} rshoulder={} arms={} legs={} visor={}",
            slot_index,
            p_slot->profile.HelmetIndex,
            p_slot->profile.ChestIndex,
            p_slot->profile.LeftShoulderIndex,
            p_slot->profile.RightShoulderIndex,
            p_slot->profile.ArmsIndex,
            p_slot->profile.LegsIndex,
            p_slot->profile.VisorColorIndex);
        LOG_INFO("[ARMOR] Colors: primary={} secondary={} tertiary={}",
            p_slot->profile.PlayerModelPrimaryColorIndex,
            p_slot->profile.PlayerModelSecondaryColorIndex,
            p_slot->profile.PlayerModelTertiaryColorIndex);

        // Apply controller preset
        if (profile.controller_preset != ControllerPreset::Custom) {
            ApplyControllerPreset(p_slot->mapping, profile.controller_preset);
            LOG_INFO("  Controller preset: {}", GetPresetName(profile.controller_preset));
        } else {
            // Custom - copy the saved mapping
            memcpy(&p_slot->mapping, &profile.gamepad_mapping, sizeof(CGamepadMapping));
            LOG_INFO("  Controller: Custom mapping loaded");
        }

        // Log control settings
        LOG_INFO("  LookControlsInverted={}", p_slot->profile.LookControlsInverted);

        // CRITICAL: Sync UI state arrays with profile values
        // This ensures the Save button writes correct values
        current_preset[slot_index] = static_cast<int>(profile.controller_preset);
        current_team[slot_index] = static_cast<int>(profile.team_preference);
        LOG_INFO("[UI] Synced UI state for slot {}: preset={} team={}",
            slot_index, GetPresetName(profile.controller_preset), GetTeamName(profile.team_preference));

        // Trigger settings reload if in-game
        auto p_engine = GameEngine();
        if (MCC::IsInGame() && p_engine) {
            LOG_INFO("[ARMOR] load_setting() triggered from ApplyToSlot - game should refresh appearance");
            p_engine->load_setting();

            // Apply team preference immediately since we're in-game
            ChangePlayerTeam(slot_index, profile.team_preference);
            p_slot->team_pending = false;
            LOG_INFO("  Team applied immediately: {}", GetTeamName(profile.team_preference));
        } else {
            // Mark team preference as pending for when game starts
            p_slot->team_pending = true;
            p_slot->pending_team = static_cast<int>(profile.team_preference);
            LOG_INFO("  Team preference {} pending (not in-game yet)", GetTeamName(profile.team_preference));
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

        // Find profile by filename (key-based)
        auto* prof = GetProfileByKey(p_cfg->profile_name);
        if (prof) {
            selected_profile_key[0] = p_cfg->profile_name;
            ApplyToSlot(0, *prof);
            current_preset[0] = static_cast<int>(prof->controller_preset);
            current_team[0] = static_cast<int>(prof->team_preference);
            LOG_INFO("=== Auto-loaded profile from instance config ===");
            LOG_INFO("  Profile: {}", p_cfg->profile_name);
            LOG_INFO("  Display name: {}", p_cfg->display_name);
            LOG_INFO("  Player: {}/{}", p_cfg->player_index + 1, p_cfg->total_players);
            LOG_INFO("  Screen position: {}", p_cfg->screen_position);
            LOG_INFO("================================================");
            return;
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

    // Helper to draw team color square
    static void DrawTeamColorSquare(Team team) {
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
        int idx = static_cast<int>(team);
        if (idx >= 0 && idx < 8) {
            ImGui::ColorButton("##teamcolor", team_colors[idx], ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(16, 16));
        }
    }

    void ProfileManager::ImGuiProfileSelector(int slot_index) {
        static char new_profile_name[4][256] = {{0}, {0}, {0}, {0}}; // Per-slot to prevent state bleed
        static bool show_new_profile_popup = false;
        static int popup_slot = -1;
        // NOTE: current_preset and current_team are now class static members,
        // synced in ApplyToSlot() to ensure save/load consistency

        // P0-3: Track slot changes to force all sections collapsed on switch
        static int s_last_slot = -1;
        static bool s_force_collapse = true;  // Start collapsed on first open
        if (s_last_slot != slot_index) {
            s_force_collapse = true;
            s_last_slot = slot_index;
        }

        auto p_slot = CGameManager::get_profile(slot_index);
        if (!p_slot) return;

        ImGui::PushID(slot_index);

        // ========== TOP SECTION: Profile dropdown (left) + Input dropdown (right) ==========
        // Profile dropdown - uses key-based selection (stable across Refresh)
        const char* current_profile = !selected_profile_key[slot_index].empty()
            ? selected_profile_key[slot_index].c_str()
            : "-- None --";

        ImGui::PushItemWidth(180);
        if (ImGui::BeginCombo("Profile", current_profile)) {
            if (ImGui::Selectable("-- None --", selected_profile_key[slot_index].empty())) {
                selected_profile_key[slot_index] = "";
                ClearDirty(slot_index);
            }
            // Copy profile keys to local vector to avoid iterator invalidation
            // if profiles vector is modified during combo iteration
            std::vector<std::string> profile_keys;
            profile_keys.reserve(profiles.size());
            for (const auto& p : profiles) {
                profile_keys.push_back(p.filename);
            }
            for (int i = 0; i < (int)profile_keys.size(); i++) {
                bool is_selected = (selected_profile_key[slot_index] == profile_keys[i]);
                if (ImGui::Selectable(profile_keys[i].c_str(), is_selected)) {
                    if (selected_profile_key[slot_index] != profile_keys[i]) {
                        // Use safe lookup by key instead of direct vector access
                        auto* prof = GetProfileByKey(profile_keys[i]);
                        if (prof) {
                            selected_profile_key[slot_index] = prof->filename;
                            ApplyToSlot(slot_index, *prof);
                            current_preset[slot_index] = static_cast<int>(prof->controller_preset);
                            current_team[slot_index] = static_cast<int>(prof->team_preference);
                            ClearDirty(slot_index);
                            LOG_INFO("[UI] Selected profile '{}' for slot {} (key-based)", prof->filename, slot_index);
                        }
                    }
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        // Dirty indicator
        if (IsSlotDirty(slot_index)) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "*unsaved");
        }

        // Input dropdown (stacked below Profile)
        // NOTE: Controller assignment is per-slot RUNTIME state, NOT saved to profile
        const char* input_items[] = {"Controller 1", "Controller 2", "Controller 3", "Controller 4", "NONE"};
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        ImGui::BeginDisabled(!slot_index && p_setting->b_player0_use_km);
        ImGui::PushItemWidth(180);
        ImGui::Combo("Input", &p_slot->controller_index, input_items, IM_ARRAYSIZE(input_items));
        // Do NOT mark dirty - controller is not part of profile
        ImGui::PopItemWidth();
        ImGui::EndDisabled();

        // ========== SAVE / REVERT / NEW PROFILE BUTTONS (always visible, outside expanders) ==========
        ImGui::Spacing();
        {
            bool has_profile_selected = !selected_profile_key[slot_index].empty();

            // Save button
            ImGui::BeginDisabled(!IsSlotDirty(slot_index) && has_profile_selected);
            if (ImGui::Button("Save", ImVec2(80, 0))) {
                PersistentProfile new_prof;
                std::string old_key = selected_profile_key[slot_index];

                auto* existing = GetProfileByKey(selected_profile_key[slot_index]);
                if (existing) {
                    new_prof = *existing;
                } else {
                    char fname_buf[256];
                    String::convert(fname_buf, p_slot->name, 256);
                    new_prof.filename = std::string(fname_buf) + ".json";
                }

                // Capture ALL slot state for saving
                new_prof.display_name = p_slot->name;
                memcpy(new_prof.service_tag, p_slot->profile.ServiceTag, 4 * sizeof(wchar_t));
                new_prof.service_tag[4] = L'\0';
                new_prof.controller_preset = static_cast<ControllerPreset>(current_preset[slot_index]);
                new_prof.team_preference = static_cast<Team>(current_team[slot_index]);
                memcpy(&new_prof.user_profile, &p_slot->profile, sizeof(CUserProfile));
                memcpy(&new_prof.gamepad_mapping, &p_slot->mapping, sizeof(CGamepadMapping));

                LOG_INFO("[SAVE] Slot={} ProfileKey={} Begin", slot_index, new_prof.filename);
                LOG_INFO("[SAVE] Fields: name={}, tag={}, preset={}, team={}, helmet={}",
                    wstring_to_utf8(new_prof.display_name),
                    wstring_to_utf8(std::wstring(new_prof.service_tag, 4)),
                    GetPresetName(new_prof.controller_preset),
                    GetTeamName(new_prof.team_preference),
                    new_prof.user_profile.HelmetIndex);

                if (SaveProfile(new_prof, new_prof.filename)) {
                    LoadAllProfiles();
                    // Re-sync UI state after reload
                    SyncUIStateFromProfile(slot_index);
                    ClearDirty(slot_index);
                    LOG_INFO("[SAVE] Success - slot {} using key '{}'", slot_index, new_prof.filename);
                } else {
                    LOG_ERROR("[SAVE] FAILED for slot {} profile {}", slot_index, new_prof.filename);
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Save current settings to profile file");
            }

            ImGui::SameLine();

            // Revert button
            ImGui::BeginDisabled(!IsSlotDirty(slot_index) || selected_profile_key[slot_index].empty());
            if (ImGui::Button("Revert", ImVec2(80, 0))) {
                RevertSlot(slot_index);
                SyncUIStateFromProfile(slot_index);
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Revert to last saved profile");
            }

            ImGui::SameLine();

            // New profile button
            if (ImGui::Button("New Profile", ImVec2(100, 0))) {
                show_new_profile_popup = true;
                popup_slot = slot_index;
                memset(new_profile_name[slot_index], 0, sizeof(new_profile_name[slot_index]));
            }

            // Show rank info on same line if profile is selected
            auto* selected_prof = GetProfileByKey(selected_profile_key[slot_index]);
            if (selected_prof) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "  Rank: %s (Lv%d)",
                    GetRankName(selected_prof->rank_level), selected_prof->rank_level);
            }
        }

        ImGui::Separator();

        // ========== EXPANDER 1: Profile Settings ==========
        // P0-3: Force collapsed on slot change or window open
        if (s_force_collapse) {
            ImGui::SetNextItemOpen(false, ImGuiCond_Always);
        }
        if (ImGui::CollapsingHeader("Profile Settings")) {
            ImGui::Indent();

            // Display Name
            char name_buffer[256];
            String::convert(name_buffer, p_slot->name, 256);
            ImGui::PushItemWidth(250);
            if (ImGui::InputText("Display Name", name_buffer, sizeof(name_buffer))) {
                String::convert(p_slot->name, name_buffer, 1024);
                MarkDirty(slot_index);
            }
            ImGui::PopItemWidth();

            // Service Tag (3-char + 4-char fields)
            char tag3[4] = {0};
            char tag4[5] = {0};
            for (int i = 0; i < 3 && i < 4; i++) {
                tag3[i] = static_cast<char>(p_slot->profile.ServiceTag[i]);
            }
            for (int i = 0; i < 4; i++) {
                tag4[i] = static_cast<char>(p_slot->profile.ServiceTag[i]);
            }

            ImGui::Text("Service Tag:");
            ImGui::SameLine();
            ImGui::PushItemWidth(50);
            if (ImGui::InputText("##tag3", tag3, 4, ImGuiInputTextFlags_CharsUppercase)) {
                for (int i = 0; i < 3; i++) {
                    p_slot->profile.ServiceTag[i] = static_cast<wchar_t>(tag3[i] ? tag3[i] : L' ');
                }
                MarkDirty(slot_index);
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::Text("/");
            ImGui::SameLine();
            ImGui::PushItemWidth(60);
            if (ImGui::InputText("##tag4", tag4, 5, ImGuiInputTextFlags_CharsUppercase)) {
                for (int i = 0; i < 4; i++) {
                    p_slot->profile.ServiceTag[i] = static_cast<wchar_t>(tag4[i] ? tag4[i] : L' ');
                }
                MarkDirty(slot_index);
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(3-char or 4-char tag)");

            // Button Layout
            const char* preset_names[] = {
                "Universal Default (Recon)", "Universal Reclaimer", "Universal Zoom & Shoot",
                "Universal Bump & Jump", "Universal Green Thumbs", "Universal Infinite", "Custom"
            };
            ImGui::PushItemWidth(180);
            if (ImGui::Combo("Button Layout", &current_preset[slot_index], preset_names, IM_ARRAYSIZE(preset_names))) {
                ControllerPreset preset = static_cast<ControllerPreset>(current_preset[slot_index]);
                if (preset != ControllerPreset::Custom) {
                    ApplyControllerPreset(p_slot->mapping, preset);
                    auto p_engine = GameEngine();
                    if (MCC::IsInGame() && p_engine) {
                        p_engine->load_setting();
                    }
                    LOG_INFO("Applied controller preset '{}' to slot {}", GetPresetName(preset), slot_index);
                }
                MarkDirty(slot_index);
            }
            ImGui::PopItemWidth();
            if (current_preset[slot_index] == static_cast<int>(ControllerPreset::Custom)) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Edit in Button Mapping)");
            }

            // Team Preference with color indicator
            const char* team_names[] = {
                "Red", "Blue", "Green", "Orange", "Purple", "Gold", "Brown", "Pink"
            };
            ImVec4 team_colors[] = {
                ImVec4(1.0f, 0.2f, 0.2f, 1.0f), ImVec4(0.2f, 0.4f, 1.0f, 1.0f),
                ImVec4(0.2f, 0.8f, 0.2f, 1.0f), ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                ImVec4(0.6f, 0.2f, 0.8f, 1.0f), ImVec4(1.0f, 0.84f, 0.0f, 1.0f),
                ImVec4(0.6f, 0.4f, 0.2f, 1.0f), ImVec4(1.0f, 0.4f, 0.7f, 1.0f)
            };

            ImGui::PushItemWidth(120);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(team_colors[current_team[slot_index]].x * 0.3f,
                                                            team_colors[current_team[slot_index]].y * 0.3f,
                                                            team_colors[current_team[slot_index]].z * 0.3f, 1.0f));
            if (ImGui::Combo("Team Preference", &current_team[slot_index], team_names, IM_ARRAYSIZE(team_names))) {
                Team team = static_cast<Team>(current_team[slot_index]);
                if (MCC::IsInGame()) {
                    ChangePlayerTeam(slot_index, team);
                }
                MarkDirty(slot_index);
                LOG_INFO("Set team preference for slot {} to {}", slot_index, GetTeamName(team));
            }
            ImGui::PopStyleColor();
            ImGui::PopItemWidth();
            ImGui::SameLine();
            DrawTeamColorSquare(static_cast<Team>(current_team[slot_index]));

            // Set Teams Now button - applies teams for all active slots
            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine();
            if (ImGui::Button("Set Teams Now")) {
                ApplyTeamsNow("manual_button");
            }
            if (ImGui::IsItemHovered()) {
                if (MCC::IsInGame()) {
                    ImGui::SetTooltip("Apply team preferences for all players immediately");
                } else {
                    ImGui::SetTooltip("Not in-game: will apply when match starts");
                }
            }

            // Show pending status
            auto p_profile_check = CGameManager::get_profile(slot_index);
            if (p_profile_check && p_profile_check->team_pending) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "(pending)");
            }

            ImGui::Unindent();
        }

        // ========== EXPANDER 2: Spartan Appearance ==========
        if (s_force_collapse) {
            ImGui::SetNextItemOpen(false, ImGuiCond_Always);
        }
        if (ImGui::CollapsingHeader("Spartan Appearance")) {
            ImGui::Indent();

            bool changed = false;

            // Helper macro for randomize buttons
            #define RAND_BTN(label, field, max_val) \
                ImGui::SameLine(); \
                if (ImGui::SmallButton("R##" label)) { \
                    field = RandomArmor(max_val); \
                    changed = true; \
                    MarkDirty(slot_index); \
                } \
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Randomize " label);

            #define RAND_COLOR_BTN(label, field) \
                ImGui::SameLine(); \
                if (ImGui::SmallButton("R##" label)) { \
                    field = RandomColor(); \
                    changed = true; \
                    MarkDirty(slot_index); \
                } \
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Randomize " label);

            // Model type toggle at top
            if (ImGui::Checkbox("Elite Model", &p_slot->profile.UseEliteModel)) {
                changed = true;
                MarkDirty(slot_index);
            }

            // SectionBar open states (static per slot to persist across frames)
            static bool s_armor_colors_open[4] = {false, false, false, false};
            static bool s_emblem_open[4] = {false, false, false, false};
            static bool s_reach_armor_open[4] = {true, true, true, true};  // Default open
            static bool s_h1_armor_open[4] = {false, false, false, false};
            static bool s_h2_armor_open[4] = {false, false, false, false};
            static bool s_h2a_armor_open[4] = {false, false, false, false};
            static bool s_h3_armor_open[4] = {false, false, false, false};
            static bool s_h4_armor_open[4] = {false, false, false, false};

            // ===== SUBSECTION A: Armor Colors =====
            ImGui::Spacing();
            if (SectionBar("Armor Colors", &s_armor_colors_open[slot_index])) {
                ImGui::Indent();
                ImGui::Spacing();
                if (ImGui::Button("Randomize Colors")) {
                    p_slot->profile.PlayerModelPrimaryColorIndex = RandomColor();
                    p_slot->profile.PlayerModelSecondaryColorIndex = RandomColor();
                    p_slot->profile.PlayerModelTertiaryColorIndex = RandomColor();
                    p_slot->profile.PlayerModelPrimaryColor = p_slot->profile.PlayerModelPrimaryColorIndex;
                    p_slot->profile.PlayerModelSecondaryColor = p_slot->profile.PlayerModelSecondaryColorIndex;
                    p_slot->profile.PlayerModelTertiaryColor = p_slot->profile.PlayerModelTertiaryColorIndex;
                    changed = true;
                    MarkDirty(slot_index);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Colors apply to all games");
                }

                ImGui::Text("Primary: %d", p_slot->profile.PlayerModelPrimaryColorIndex);
                RAND_COLOR_BTN("PrimaryColor", p_slot->profile.PlayerModelPrimaryColorIndex);
                p_slot->profile.PlayerModelPrimaryColor = p_slot->profile.PlayerModelPrimaryColorIndex;

                ImGui::Text("Secondary: %d", p_slot->profile.PlayerModelSecondaryColorIndex);
                RAND_COLOR_BTN("SecondaryColor", p_slot->profile.PlayerModelSecondaryColorIndex);
                p_slot->profile.PlayerModelSecondaryColor = p_slot->profile.PlayerModelSecondaryColorIndex;

                ImGui::Text("Tertiary: %d", p_slot->profile.PlayerModelTertiaryColorIndex);
                RAND_COLOR_BTN("TertiaryColor", p_slot->profile.PlayerModelTertiaryColorIndex);
                p_slot->profile.PlayerModelTertiaryColor = p_slot->profile.PlayerModelTertiaryColorIndex;

                ImGui::Spacing();
                ImGui::Unindent();
            }

            // ===== SUBSECTION B: Emblem =====
            ImGui::Spacing();
            if (SectionBar("Emblem", &s_emblem_open[slot_index])) {
                ImGui::Indent();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Emblem editing: use in-game customization");
                ImGui::Spacing();
                ImGui::Unindent();
            }

            // ===== SUBSECTION C: Per-Game Armor =====
            // Note: CUserProfile fields (HelmetIndex, ChestIndex, etc.) are shared across all games.
            // The same indices map to different items per game. Changing armor here works for ALL games
            // that support the indices - not just Reach. This is verified by the customization_item array
            // in CUserProfile.cpp which contains HR_ (Reach), H2A_, H3_, H4_ prefixed items.

            // Halo Reach Armor (verified working)
            ImGui::Spacing();
            if (SectionBar("Halo Reach Armor", &s_reach_armor_open[slot_index])) {
                ImGui::Indent();
                ImGui::Spacing();
                if (ImGui::Button("Randomize Reach Armor")) {
                    p_slot->profile.HelmetIndex = RandomArmor(NUM_HELMETS);
                    p_slot->profile.LeftShoulderIndex = RandomArmor(NUM_SHOULDERS);
                    p_slot->profile.RightShoulderIndex = RandomArmor(NUM_SHOULDERS);
                    p_slot->profile.ChestIndex = RandomArmor(NUM_CHESTS);
                    p_slot->profile.ArmsIndex = RandomArmor(NUM_ARMS);
                    p_slot->profile.LegsIndex = RandomArmor(NUM_LEGS);
                    p_slot->profile.VisorColorIndex = RandomArmor(NUM_VISORS);
                    changed = true;
                    MarkDirty(slot_index);
                }

                ImGui::Text("Helmet: %d", p_slot->profile.HelmetIndex);
                RAND_BTN("Helmet", p_slot->profile.HelmetIndex, NUM_HELMETS);

                ImGui::Text("Left Shoulder: %d", p_slot->profile.LeftShoulderIndex);
                RAND_BTN("LeftShoulder", p_slot->profile.LeftShoulderIndex, NUM_SHOULDERS);

                ImGui::Text("Right Shoulder: %d", p_slot->profile.RightShoulderIndex);
                RAND_BTN("RightShoulder", p_slot->profile.RightShoulderIndex, NUM_SHOULDERS);

                ImGui::Text("Chest: %d", p_slot->profile.ChestIndex);
                RAND_BTN("Chest", p_slot->profile.ChestIndex, NUM_CHESTS);

                ImGui::Text("Arms: %d", p_slot->profile.ArmsIndex);
                RAND_BTN("Arms", p_slot->profile.ArmsIndex, NUM_ARMS);

                ImGui::Text("Legs: %d", p_slot->profile.LegsIndex);
                RAND_BTN("Legs", p_slot->profile.LegsIndex, NUM_LEGS);

                ImGui::Text("Visor: %d", p_slot->profile.VisorColorIndex);
                RAND_BTN("Visor", p_slot->profile.VisorColorIndex, NUM_VISORS);

                if (p_slot->profile.UseEliteModel) {
                    ImGui::Separator();
                    ImGui::Text("Elite Armor:");

                    ImGui::Text("  Helmet: %d", p_slot->profile.EliteHelmetIndex);
                    RAND_BTN("EliteHelmet", p_slot->profile.EliteHelmetIndex, 20);

                    ImGui::Text("  Left Shoulder: %d", p_slot->profile.EliteLeftShoulderIndex);
                    RAND_BTN("EliteLeftShoulder", p_slot->profile.EliteLeftShoulderIndex, 10);

                    ImGui::Text("  Right Shoulder: %d", p_slot->profile.EliteRightShoulderIndex);
                    RAND_BTN("EliteRightShoulder", p_slot->profile.EliteRightShoulderIndex, 10);

                    ImGui::Text("  Chest: %d", p_slot->profile.EliteChestIndex);
                    RAND_BTN("EliteChest", p_slot->profile.EliteChestIndex, 10);
                }

                ImGui::Spacing();
                ImGui::Unindent();
            }

            // Halo CE/1 Armor - No armor customization in Halo 1
            ImGui::Spacing();
            if (SectionBar("Halo CE Armor", &s_h1_armor_open[slot_index], IM_COL32(80, 80, 80, 255))) {
                ImGui::Indent();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Halo CE does not support armor customization.");
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Only colors are available.");
                ImGui::Spacing();
                ImGui::Unindent();
            }

            // Halo 2 Classic - Limited armor options
            ImGui::Spacing();
            if (SectionBar("Halo 2 Armor", &s_h2_armor_open[slot_index], IM_COL32(80, 80, 80, 255))) {
                ImGui::Indent();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Halo 2 Classic has limited armor options.");
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Colors work; armor pieces are shared with Reach indices.");
                ImGui::Spacing();
                ImGui::Unindent();
            }

            // Halo 2 Anniversary - Has armor via H2A_Spartan_* items
            ImGui::Spacing();
            if (SectionBar("Halo 2A Armor", &s_h2a_armor_open[slot_index])) {
                ImGui::Indent();
                ImGui::Spacing();
                // H2A uses the same CUserProfile indices, mapped to H2A_Spartan_* items
                // Based on CUserProfile.cpp customization_item array: H2A has ~3 variants per piece
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Armor applies via shared indices.");
                ImGui::Text("Helmet: %d", p_slot->profile.HelmetIndex);
                RAND_BTN("H2AHelmet", p_slot->profile.HelmetIndex, 30);  // H2A has fewer options
                ImGui::Text("Chest: %d", p_slot->profile.ChestIndex);
                RAND_BTN("H2AChest", p_slot->profile.ChestIndex, 20);
                ImGui::Text("Shoulders: %d/%d", p_slot->profile.LeftShoulderIndex, p_slot->profile.RightShoulderIndex);
                ImGui::SameLine();
                if (ImGui::SmallButton("R##H2AShoulders")) {
                    p_slot->profile.LeftShoulderIndex = RandomArmor(15);
                    p_slot->profile.RightShoulderIndex = RandomArmor(15);
                    changed = true;
                    MarkDirty(slot_index);
                }
                ImGui::Text("Arms: %d", p_slot->profile.ArmsIndex);
                RAND_BTN("H2AArms", p_slot->profile.ArmsIndex, 10);
                ImGui::Text("Legs: %d", p_slot->profile.LegsIndex);
                RAND_BTN("H2ALegs", p_slot->profile.LegsIndex, 10);
                ImGui::Spacing();
                ImGui::Unindent();
            }

            // Halo 3 - Has armor customization
            ImGui::Spacing();
            if (SectionBar("Halo 3 Armor", &s_h3_armor_open[slot_index])) {
                ImGui::Indent();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Armor applies via shared indices.");
                ImGui::Text("Helmet: %d", p_slot->profile.HelmetIndex);
                RAND_BTN("H3Helmet", p_slot->profile.HelmetIndex, 20);
                ImGui::Text("Chest: %d", p_slot->profile.ChestIndex);
                RAND_BTN("H3Chest", p_slot->profile.ChestIndex, 15);
                ImGui::Text("Shoulders: %d/%d", p_slot->profile.LeftShoulderIndex, p_slot->profile.RightShoulderIndex);
                ImGui::SameLine();
                if (ImGui::SmallButton("R##H3Shoulders")) {
                    p_slot->profile.LeftShoulderIndex = RandomArmor(15);
                    p_slot->profile.RightShoulderIndex = RandomArmor(15);
                    changed = true;
                    MarkDirty(slot_index);
                }
                ImGui::Spacing();
                ImGui::Unindent();
            }

            // Halo 4 Armor
            ImGui::Spacing();
            if (SectionBar("Halo 4 Armor", &s_h4_armor_open[slot_index])) {
                ImGui::Indent();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Armor applies via shared indices.");
                ImGui::Text("Helmet: %d", p_slot->profile.HelmetIndex);
                RAND_BTN("H4Helmet", p_slot->profile.HelmetIndex, 50);
                ImGui::Text("Chest: %d", p_slot->profile.ChestIndex);
                RAND_BTN("H4Chest", p_slot->profile.ChestIndex, 30);
                ImGui::Text("Shoulders: %d/%d", p_slot->profile.LeftShoulderIndex, p_slot->profile.RightShoulderIndex);
                ImGui::SameLine();
                if (ImGui::SmallButton("R##H4Shoulders")) {
                    p_slot->profile.LeftShoulderIndex = RandomArmor(30);
                    p_slot->profile.RightShoulderIndex = RandomArmor(30);
                    changed = true;
                    MarkDirty(slot_index);
                }
                ImGui::Text("Arms: %d", p_slot->profile.ArmsIndex);
                RAND_BTN("H4Arms", p_slot->profile.ArmsIndex, 20);
                ImGui::Text("Legs: %d", p_slot->profile.LegsIndex);
                RAND_BTN("H4Legs", p_slot->profile.LegsIndex, 20);
                ImGui::Text("Visor: %d", p_slot->profile.VisorColorIndex);
                RAND_BTN("H4Visor", p_slot->profile.VisorColorIndex, 20);
                ImGui::Spacing();
                ImGui::Unindent();
            }

            // Apply changes to game if anything changed (debounced)
            if (changed) {
                // Use debounced armor application instead of immediate load_setting()
                // This prevents spamming the game engine with dozens of calls
                MarkArmorDirty(slot_index);
            }

            #undef RAND_BTN
            #undef RAND_COLOR_BTN

            ImGui::Unindent();
        }

        // ========== EXPANDER 3: Customize Button Mapping ==========
        if (s_force_collapse) {
            ImGui::SetNextItemOpen(false, ImGuiCond_Always);
        }
        if (ImGui::CollapsingHeader("Customize Button Mapping")) {
            ImGui::Indent();

            // Gamepad mapping UI
            p_slot->mapping.ImGuiContext();

            // Axis Options
            ImGui::Separator();
            ImGui::Text("Axis Options:");
            if (ImGui::Checkbox("Invert Y Look", &p_slot->profile.LookControlsInverted)) {
                auto p_engine = GameEngine();
                if (MCC::IsInGame() && p_engine) {
                    p_engine->load_setting();
                }
                MarkDirty(slot_index);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Invert vertical look axis for controller (default: OFF)");
            }

            if (ImGui::Checkbox("Invert Aircraft Controls", &p_slot->profile.AircraftControlsInverted)) {
                auto p_engine = GameEngine();
                if (MCC::IsInGame() && p_engine) {
                    p_engine->load_setting();
                }
                MarkDirty(slot_index);
            }

            ImGui::Unindent();
        }

        // ========== EXPANDER 4: Advanced Settings ==========
        if (s_force_collapse) {
            ImGui::SetNextItemOpen(false, ImGuiCond_Always);
        }
        if (ImGui::CollapsingHeader("Advanced Settings")) {
            ImGui::Indent();

            // Controller sensitivity sliders
            ImGui::Text("Controller Sensitivity:");
            {
                int v = p_slot->profile.VerticalLookSensitivity;
                if (ImGui::SliderInt("Vertical Sensitivity", &v, 1, 10)) {
                    p_slot->profile.VerticalLookSensitivity = (uint8_t)v;
                    auto p_engine = GameEngine();
                    if (MCC::IsInGame() && p_engine) {
                        p_engine->load_setting();
                    }
                    MarkDirty(slot_index);
                }
            }
            {
                int v = p_slot->profile.HorizontalLookSensitivity;
                if (ImGui::SliderInt("Horizontal Sensitivity", &v, 1, 10)) {
                    p_slot->profile.HorizontalLookSensitivity = (uint8_t)v;
                    auto p_engine = GameEngine();
                    if (MCC::IsInGame() && p_engine) {
                        p_engine->load_setting();
                    }
                    MarkDirty(slot_index);
                }
            }
            {
                int v = p_slot->profile.LookAcceleration;
                if (ImGui::SliderInt("Look Acceleration", &v, 1, 5)) {
                    p_slot->profile.LookAcceleration = (uint8_t)v;
                    auto p_engine = GameEngine();
                    if (MCC::IsInGame() && p_engine) {
                        p_engine->load_setting();
                    }
                    MarkDirty(slot_index);
                }
            }

            ImGui::Separator();
            ImGui::Text("Deadzones & Multipliers:");
            if (ImGui::SliderFloat("Axial Deadzone", &p_slot->profile.LookAxialDeadZone, 0.0f, 1.0f)) {
                auto p_engine = GameEngine();
                if (MCC::IsInGame() && p_engine) {
                    p_engine->load_setting();
                }
                MarkDirty(slot_index);
            }
            if (ImGui::SliderFloat("Radial Deadzone", &p_slot->profile.LookRadialDeadZone, 0.0f, 1.0f)) {
                auto p_engine = GameEngine();
                if (MCC::IsInGame() && p_engine) {
                    p_engine->load_setting();
                }
                MarkDirty(slot_index);
            }
            if (ImGui::SliderFloat("Zoom Sensitivity", &p_slot->profile.ZoomLookSensitivityMultiplier, 0.0f, 2.0f)) {
                auto p_engine = GameEngine();
                if (MCC::IsInGame() && p_engine) {
                    p_engine->load_setting();
                }
                MarkDirty(slot_index);
            }
            if (ImGui::SliderFloat("Vehicle Sensitivity", &p_slot->profile.VehicleLookSensitivityMultiplier, 0.0f, 2.0f)) {
                auto p_engine = GameEngine();
                if (MCC::IsInGame() && p_engine) {
                    p_engine->load_setting();
                }
                MarkDirty(slot_index);
            }

            ImGui::Separator();
            ImGui::Text("Audio:");
            if (ImGui::SliderFloat("Master Volume", &p_slot->profile.MasterVolume, 0.0f, 1.0f)) {
                MarkDirty(slot_index);
            }
            if (ImGui::SliderFloat("Music Volume", &p_slot->profile.MusicVolume, 0.0f, 1.0f)) {
                MarkDirty(slot_index);
            }
            if (ImGui::SliderFloat("SFX Volume", &p_slot->profile.SfxVolume, 0.0f, 1.0f)) {
                MarkDirty(slot_index);
            }

            ImGui::Separator();
            ImGui::Text("Display:");
            if (ImGui::SliderInt("FOV", &p_slot->profile.FOVSetting, 70, 120)) {
                MarkDirty(slot_index);
            }
            if (ImGui::SliderInt("Vehicle FOV", &p_slot->profile.VehicleFOVSetting, 70, 120)) {
                MarkDirty(slot_index);
            }
            if (ImGui::SliderFloat("HUD Scale", &p_slot->profile.HUDScale, 0.5f, 2.0f)) {
                MarkDirty(slot_index);
            }
            if (ImGui::SliderFloat("Brightness", &p_slot->profile.Brightness, 0.0f, 1.0f)) {
                MarkDirty(slot_index);
            }

            ImGui::Separator();
            ImGui::Text("Accessibility:");
            if (ImGui::Checkbox("Subtitles", &p_slot->profile.SubtitleSetting)) {
                MarkDirty(slot_index);
            }
            if (ImGui::Checkbox("Vibration Disabled", &p_slot->profile.VibrationDisabled)) {
                MarkDirty(slot_index);
            }

            const char* colorblind_modes[] = {"Off", "Protanopia", "Deuteranopia", "Tritanopia"};
            if (ImGui::Combo("Colorblind Mode", &p_slot->profile.ColorBlindMode, colorblind_modes, IM_ARRAYSIZE(colorblind_modes))) {
                MarkDirty(slot_index);
            }

            ImGui::Unindent();
        }

        // Show settings status (p_setting already declared above)
        if (!p_setting->b_override_profile || p_setting->b_use_player0_profile) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                "Note: Load will auto-enable required settings");
        }

        // P0-3: Clear force collapse flag after processing this frame
        // (we only want to collapse once on open/switch, then let user toggle freely)
        s_force_collapse = false;

        ImGui::PopID();

        // New profile popup (handled outside PushID to avoid ID conflicts)
        if (show_new_profile_popup && popup_slot == slot_index) {
            ImGui::OpenPopup("New Profile");
            show_new_profile_popup = false;
        }

        if (ImGui::BeginPopupModal("New Profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static bool randomize_appearance = true;

            // Validate popup_slot is in valid range
            int safe_slot = (popup_slot >= 0 && popup_slot < 4) ? popup_slot : 0;

            ImGui::Text("Enter profile name:");
            ImGui::InputText("##new_profile_name", new_profile_name[safe_slot], sizeof(new_profile_name[safe_slot]));

            ImGui::Checkbox("Randomize colors, emblem & service tag", &randomize_appearance);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Generate random armor colors, emblem design, and service tag");
            }

            if (ImGui::Button("Create", ImVec2(120, 0))) {
                if (strlen(new_profile_name[safe_slot]) > 0) {
                    auto p_profile = CGameManager::get_profile(popup_slot);
                    if (p_profile) {
                        PersistentProfile new_prof;
                        new_prof.filename = std::string(new_profile_name[safe_slot]) + ".json";
                        new_prof.display_name = utf8_to_wstring(new_profile_name[safe_slot]);
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

                            // Key-based: directly set the key
                            selected_profile_key[popup_slot] = new_prof.filename;
                            LOG_INFO("[NEW] Created and selected profile '{}' for slot {}", new_prof.filename, popup_slot);
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
