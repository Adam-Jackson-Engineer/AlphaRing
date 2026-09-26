#include "Splitscreen.h"
#include "ProfileManager.h"

#include "common.h"

#include "global/Global.h"

#include <offset_mcc.h>
#include <vector>
#include <cstring>

#include "../CGameManager.h"
#include "../InstanceConfig.h"
#include "ringchief/GameBridge.h"

namespace MCC::Splitscreen {
    DefDetourFunction(__int64, __fastcall, get_index_by_xuid, void* a1, __int64 xuid) {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();

        if (!p_setting->b_override)
            return ppOriginal_get_index_by_xuid(a1, xuid);

        return CGameManager::get_index(xuid);
    }

    // todo:: let other players have the ability to pause the game

    bool Initialize() {
        bool result;

        // fix: changing team freeze the game
        result = AlphaRing::Hook::Detour({
            {0x38A09C/*0x2D01DC*/, 0x374164/*0x2BD620*/, get_index_by_xuid, (void**)&ppOriginal_get_index_by_xuid},
        });

        if (!result) { LOG_ERROR("MCC:Splitscreen: failed to hook"); return false; }

        // Optional settings from the Nucleus handler (player count, login pre-fill).
        AlphaRing::Config::LoadInstanceConfig();
        ProfileManager::ResetTeamState();

        const auto& inst_cfg = AlphaRing::Config::GetInstanceConfig();
        if (!inst_cfg.loaded) {
            // Single window: AlphaRing input from the start. Player N = controller N,
            // keyboard & mouse off (turn it on per screen in the Ring Chief window), and
            // Ring Chief profiles apply (plain MCC mode would use MCC's own profile).
            auto p_setting = AlphaRing::Global::MCC::Splitscreen();
            p_setting->player_count = 1;
            p_setting->b_override = true;
            p_setting->b_override_profile = true;
            p_setting->b_use_player0_profile = false;
            p_setting->b_player0_use_km = false;
        }
        if (inst_cfg.loaded) {
            // Launched by Nucleus: start with split-screen off so the instance can join LAN
            // first; the host turns it on from the overlay.
            auto p_setting = AlphaRing::Global::MCC::Splitscreen();
            p_setting->player_count = inst_cfg.player_count;
            p_setting->b_override = false;
            p_setting->b_override_profile = true;
            // Several MCC windows on one screen: hide focus loss so the others don't blur.
            AlphaRing::Global::Global()->keep_focus = true;
        }

        // Ring Chief: group login, profiles from halo.dronedude.app, live updates.
        RingChief::Game::Initialize();

        return true;
    }
}

#include "imgui.h"
#include "mcc/mcc.h"

#include <string>

namespace MCC::Splitscreen {
    void RealContext();
    void RosterPanel();  // Forward declaration

    // ==============================================================================
    // Team Apply State Machine (v2 - with epoch tracking)
    // ==============================================================================
    // This runs every frame in ImGuiContext() (even when UI window is closed) to
    // reliably detect game state transitions and apply pending teams.
    //
    // States:
    //   IDLE        - Not in game, waiting for game to start
    //   READY       - Just entered game, waiting for engine to be ready
    //   APPLIED     - Teams have been applied for this match epoch
    //
    // Transitions:
    //   IDLE -> READY   : IsInGame() becomes true
    //   READY -> APPLIED: Engine ready, teams applied successfully
    //   APPLIED -> IDLE : IsInGame() becomes false (match ended)
    //
    // Epoch tracking prevents stale state from previous sessions:
    //   - s_match_epoch increments on each game start
    //   - Teams are only auto-applied once per epoch
    //   - Manual "Set Teams Now" always works regardless of epoch
    // ==============================================================================
    enum class TeamApplyState { IDLE, READY, APPLIED };
    static TeamApplyState s_team_state = TeamApplyState::IDLE;
    static int s_ready_frames = 0;           // Frames to wait before applying teams
    static int s_match_epoch = 0;            // Increments on each game start
    static int s_applied_epoch = -1;         // Last epoch where teams were applied
    static bool s_initialized = false;       // First-run flag for reset
    constexpr int TEAM_APPLY_DELAY_FRAMES = 60;  // ~1 sec at 60fps (increased for safety)

    // Reset all team state - call on init or when state is corrupted
    static void ResetTeamStateMachine() {
        LOG_INFO("TeamStateMachine: ResetTeamStateMachine() called");
        s_team_state = TeamApplyState::IDLE;
        s_ready_frames = 0;
        s_match_epoch = 0;
        s_applied_epoch = -1;
        ProfileManager::ResetTeamState();  // Clear pending flags on all slots
    }

    static void UpdateTeamStateMachine() {
        // First-run initialization
        if (!s_initialized) {
            LOG_INFO("TeamStateMachine: First run initialization");
            ResetTeamStateMachine();
            s_initialized = true;
        }

        bool is_in_game = MCC::IsInGame();

        switch (s_team_state) {
            case TeamApplyState::IDLE:
                if (is_in_game) {
                    s_match_epoch++;
                    LOG_INFO("TeamStateMachine: IDLE -> READY (entered game, epoch={})", s_match_epoch);
                    s_team_state = TeamApplyState::READY;
                    s_ready_frames = 0;

                    // Notify stats system of match start
                    ProfileManager::OnMatchStart();
                }
                break;

            case TeamApplyState::READY:
                if (!is_in_game) {
                    // Game ended before we could apply
                    LOG_INFO("TeamStateMachine: READY -> IDLE (game ended early, epoch={})", s_match_epoch);
                    s_team_state = TeamApplyState::IDLE;

                    // Notify stats system of match end
                    ProfileManager::OnMatchEnd();
                } else {
                    s_ready_frames++;
                    if (s_ready_frames >= TEAM_APPLY_DELAY_FRAMES) {
                        // Only apply if we haven't applied for this epoch yet
                        if (s_applied_epoch != s_match_epoch) {
                            LOG_INFO("[MATCH] start epoch={} - applying teams and scheduling armor two-shot", s_match_epoch);
                            ProfileManager::ApplyPendingTeams();

                            // Schedule two-shot armor apply for all players
                            // This uses a state machine: first apply -> wait ~1 sec -> second apply
                            // Two shots required for armor to reliably apply in subsequent matches
                            auto p_setting = AlphaRing::Global::MCC::Splitscreen();
                            for (int i = 0; i < p_setting->player_count && i < 4; i++) {
                                ProfileManager::ScheduleArmorTwoShot(i);
                            }

                            s_applied_epoch = s_match_epoch;
                            LOG_INFO("TeamStateMachine: READY -> APPLIED (epoch={})", s_match_epoch);
                        } else {
                            LOG_INFO("TeamStateMachine: Skipping apply - already applied for epoch {}", s_match_epoch);
                        }
                        s_team_state = TeamApplyState::APPLIED;
                    }
                }
                break;

            case TeamApplyState::APPLIED:
                if (!is_in_game) {
                    LOG_INFO("TeamStateMachine: APPLIED -> IDLE (match ended, epoch={})", s_match_epoch);

                    // Notify stats system of match end
                    ProfileManager::OnMatchEnd();

                    s_team_state = TeamApplyState::IDLE;
                }
                break;
        }
    }

    // Forward declarations for Session Details window
    void SessionDetailsWindow();
    void SessionRosterTab();
    void GameStatsTab();

    // Runs every frame from the render loop, whether or not the overlay is showing.
    void Tick() {
        UpdateTeamStateMachine();
        ProfileManager::ProcessPendingArmor();
        RingChief::Game::Tick();
    }

    void ImGuiContext() {
        static bool show_splitscreen = false;      // "Advanced settings"
        static bool show_session_details = true;
        static bool show_ringchief = true;
        if (RingChief::Game::WantsAttention()) show_ringchief = true;

        if (ImGui::BeginMainMenuBar()) {
            ImGui::MenuItem("Advanced settings", nullptr, &show_splitscreen);
            ImGui::MenuItem("Session Details", nullptr, &show_session_details);

            // LAN Reminder indicator (always visible in menu bar)
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "[LAN: Manual]");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("LAN Mode Reminder:");
                ImGui::BulletText("Go to MCC main menu");
                ImGui::BulletText("Select 'Custom Games'");
                ImGui::BulletText("Choose 'System Link / LAN'");
                ImGui::BulletText("Host or join a LAN session");
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Auto-LAN not available (requires engine hooks)");
                ImGui::EndTooltip();
            }

            // Ring Chief status (click to open the Ring Chief window)
            ImGui::Separator();
            RingChief::Game::DrawMenuStatus();
            if (ImGui::IsItemClicked()) show_ringchief = true;
            ImGui::MenuItem("Ring Chief", nullptr, &show_ringchief);

            ImGui::EndMainMenuBar();
        }

        if (show_splitscreen) {
            ImGui::SetNextWindowPos(ImVec2(0, 25), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Advanced settings", &show_splitscreen, ImGuiWindowFlags_MenuBar))
                RealContext();
            ImGui::End();
        }

        if (show_session_details) {
            SessionDetailsWindow();
        }

        if (show_ringchief) {
            RingChief::Game::DrawWindow(&show_ringchief);
        }
    }

    void ProfileContext(int index) {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        auto p_profile = CGameManager::get_profile(index);

        // P0-3: Track index changes to force collapse on switch
        static int s_last_index = -1;
        static bool s_force_collapse = true;
        if (s_last_index != index) {
            s_force_collapse = true;
            s_last_index = index;
        }

        // Main profile UI (Profile Settings, Appearance, Button Mapping, Advanced Settings)
        ProfileManager::ImGuiProfileSelector(index);

        ImGui::Separator();

        // Copy from Player 1 button (useful for copying live game profile)
        if (ImGui::Button("Copy from Player 1")) {
            __int64 xuid;
            auto p_mng = GameManager();
            auto p_engine = GameEngine();
            if (MCC::IsInGame() && p_mng && (xuid = CGameManager::get_xuid(0))) {
                memcpy(&p_profile->profile, p_mng->ppOriginal.get_player_profile(p_mng, xuid), sizeof(CUserProfile));
                memcpy(&p_profile->mapping, p_mng->ppOriginal.retrive_gamepad_mapping(p_mng, xuid), sizeof(CGamepadMapping));
                if (p_engine)
                    p_engine->load_setting();
                ProfileManager::MarkDirty(index);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copy profile settings from logged-in Player 1 (must be in-game)");

        // Raw profile view for advanced debugging
        bool is_disabled = (!index && !p_setting->b_override_profile) || (index && p_setting->b_use_player0_profile);
        if (s_force_collapse) {
            ImGui::SetNextItemOpen(false, ImGuiCond_Always);
        }
        if (ImGui::CollapsingHeader("Raw Profile Data (Debug)")) {
            ImGui::Indent();
            ImGui::BeginDisabled(is_disabled);
            p_profile->profile.ImGuiContext();
            ImGui::EndDisabled();
            ImGui::Unindent();
        }

        // P0-3: Clear force collapse flag after processing
        s_force_collapse = false;
    }

    void RealContext() {
        // Team apply is now handled by UpdateTeamStateMachine() in ImGuiContext()
        // which runs every frame even when this window is closed.

        char buffer[10];
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();

        // Flag to defer profile reload until after menu processing is complete
        static bool s_pending_profile_reload = false;

        if (ImGui::BeginMenuBar()) {
            ImGui::MenuItem(p_setting->b_override ? "Disable" : "Enable", nullptr, &p_setting->b_override);
            if (ImGui::BeginMenu("Profiles")) {
                if (ImGui::MenuItem("Refresh All")) {
                    // Defer reload until menu is closed to avoid use-after-free
                    s_pending_profile_reload = true;
                }
                ImGui::Separator();
                // Quick load for each player slot
                // Copy profile filenames to local vector to avoid iterator invalidation
                std::vector<std::string> profile_keys;
                std::vector<std::string> profile_labels;
                profile_keys.reserve(ProfileManager::profiles.size());
                for (const auto& p : ProfileManager::profiles) {
                    profile_keys.push_back(p.filename);
                    profile_labels.push_back(ProfileManager::DisplayLabel(p.filename) + "##" + p.filename);
                }

                for (int i = 0; i < p_setting->player_count; i++) {
                    char label[32];
                    sprintf(label, "Player %d", i + 1);
                    if (ImGui::BeginMenu(label)) {
                        for (int j = 0; j < (int)profile_keys.size(); j++) {
                            bool is_selected = (ProfileManager::selected_profile_key[i] == profile_keys[j]);
                            if (ImGui::MenuItem(profile_labels[j].c_str(),
                                                nullptr,
                                                is_selected)) {
                                // Safely get profile by key (returns nullptr if not found)
                                auto* prof = ProfileManager::GetProfileByKey(profile_keys[j]);
                                if (prof) {
                                    ProfileManager::selected_profile_key[i] = prof->filename;
                                    ProfileManager::ApplyToSlot(i, *prof);
                                    RingChief::Game::SlotsChanged();
                                    ProfileManager::current_preset[i] = static_cast<int>(prof->controller_preset);
                                    ProfileManager::current_team[i] = static_cast<int>(prof->team_preference);
                                }
                            }
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Options")) {
                ImGui::MenuItem("Use player1's profile", nullptr, &p_setting->b_use_player0_profile);
                ImGui::MenuItem("Enable K/M for player1", nullptr, &p_setting->b_player0_use_km);
                ImGui::MenuItem("Override profile", nullptr, &p_setting->b_override_profile);
                ImGui::EndMenu();
            }
#pragma region player count
            ImGui::PushItemWidth(200);
            int count = p_setting->player_count;
            if (ImGui::InputInt("Players", &count) && count >= 1 && count <=4) {
                p_setting->player_count = count;
            }
            ImGui::PopItemWidth();
            ImGui::EndMenuBar();
#pragma endregion
        }

        // Process deferred profile reload (after menu is closed to avoid use-after-free)
        if (s_pending_profile_reload) {
            ProfileManager::LoadAllProfiles();
            s_pending_profile_reload = false;
            LOG_INFO("[UI] Deferred profile reload completed");
        }

        // Show instance config info if loaded (from Nucleus)
        const auto& inst_cfg = AlphaRing::Config::GetInstanceConfig();
        if (inst_cfg.loaded) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Nucleus instance: %d player(s)", inst_cfg.player_count);
            ImGui::Separator();
        }

        // Quick status line and Session Details button (Roster moved to Session Details window)
        {
            auto p_setting_local = AlphaRing::Global::MCC::Splitscreen();
            bool is_enabled = p_setting_local->b_override;
            bool is_in_game = MCC::IsInGame();

            ImGui::Text("Status: ");
            ImGui::SameLine();
            if (is_enabled) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "ENABLED");
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.2f, 1.0f), "DISABLED");
            }
            ImGui::SameLine();
            ImGui::Text(" | ");
            ImGui::SameLine();
            if (is_in_game) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "IN GAME");
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "IN MENU");
            }
            ImGui::SameLine();
            ImGui::Text(" | Players: %d", p_setting_local->player_count);

            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine();
            static bool* p_show_details = nullptr;
            if (ImGui::Button("Session Details...")) {
                // Toggle is handled by the window - just ensure it's visible
                // (The window has its own open state via ImGui::Begin)
            }
        }

        ImGui::Separator();

        if (ImGui::BeginTabBar("Players")) {
            for (int i = 0; i < p_setting->player_count; ++i) {
                sprintf(buffer, "Player %d", i + 1);
                if (ImGui::BeginTabItem(buffer)) {
                    ProfileContext(i);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

    // ==============================================================================
    // Roster Panel - Shows all configured players in a compact table format
    // ==============================================================================
    // This provides visibility into the full session setup BEFORE starting a match.
    // Displays player names, teams, profile status, and controller assignments.
    // ==============================================================================
    void RosterPanel() {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();

        // P0-3: Always start collapsed
        static bool s_first_frame = true;
        if (s_first_frame) {
            ImGui::SetNextItemOpen(false, ImGuiCond_Always);
            s_first_frame = false;
        }
        if (ImGui::CollapsingHeader("Session Roster")) {

            // Status indicators
            bool is_enabled = p_setting->b_override;
            bool is_in_game = MCC::IsInGame();

            ImGui::Text("Status: ");
            ImGui::SameLine();
            if (is_enabled) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "ENABLED");
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.2f, 1.0f), "DISABLED");
            }
            ImGui::SameLine();
            ImGui::Text(" | ");
            ImGui::SameLine();
            if (is_in_game) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "IN GAME");
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "IN MENU");
            }

            ImGui::Spacing();

            // Player table
            if (ImGui::BeginTable("RosterTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Player Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                // Team colors for visual indicators
                const ImVec4 team_colors[] = {
                    ImVec4(0.9f, 0.2f, 0.2f, 1.0f),  // Red
                    ImVec4(0.2f, 0.4f, 0.9f, 1.0f),  // Blue
                    ImVec4(0.2f, 0.8f, 0.2f, 1.0f),  // Green
                    ImVec4(0.9f, 0.6f, 0.1f, 1.0f),  // Orange
                    ImVec4(0.6f, 0.2f, 0.8f, 1.0f),  // Purple
                    ImVec4(0.9f, 0.8f, 0.2f, 1.0f),  // Gold
                    ImVec4(0.5f, 0.3f, 0.2f, 1.0f),  // Brown
                    ImVec4(0.9f, 0.4f, 0.6f, 1.0f),  // Pink
                };

                for (int i = 0; i < p_setting->player_count; ++i) {
                    auto p_profile = CGameManager::get_profile(i);

                    ImGui::TableNextRow();

                    // Slot number
                    ImGui::TableNextColumn();
                    ImGui::Text("P%d", i + 1);

                    // Player name (from profile)
                    ImGui::TableNextColumn();
                    if (p_profile && p_profile->name[0] != L'\0') {
                        // Convert wide string to narrow for display
                        char name_buf[256];
                        wcstombs(name_buf, p_profile->name, sizeof(name_buf));
                        ImGui::Text("%s", name_buf);
                    } else {
                        ImGui::TextDisabled("(not set)");
                    }

                    // Service tag
                    ImGui::TableNextColumn();
                    auto* sel_prof = ProfileManager::GetProfileByKey(ProfileManager::selected_profile_key[i]);
                    if (sel_prof) {
                        char tag_buf[8];
                        wcstombs(tag_buf, sel_prof->service_tag, sizeof(tag_buf));
                        ImGui::Text("%s", tag_buf);
                    } else {
                        ImGui::TextDisabled("----");
                    }

                    // Team preference with color
                    ImGui::TableNextColumn();
                    if (sel_prof) {
                        int team_idx = static_cast<int>(sel_prof->team_preference);
                        if (team_idx >= 0 && team_idx < 8) {
                            ImGui::TextColored(team_colors[team_idx], "%s", GetTeamName(sel_prof->team_preference));
                        } else {
                            ImGui::Text("%s", GetTeamName(sel_prof->team_preference));
                        }
                    } else {
                        ImGui::TextDisabled("-");
                    }

                    // Profile filename
                    ImGui::TableNextColumn();
                    if (sel_prof) {
                        // Show dirty indicator if modified
                        if (ProfileManager::IsSlotDirty(i)) {
                            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s *",
                                ProfileManager::DisplayLabel(sel_prof->filename).c_str());
                        } else {
                            ImGui::Text("%s", ProfileManager::DisplayLabel(sel_prof->filename).c_str());
                        }
                    } else {
                        ImGui::TextDisabled("(no profile)");
                    }
                }

                ImGui::EndTable();
            }

            // Quick actions
            ImGui::Spacing();
            if (ImGui::Button("Set Teams Now")) {
                ProfileManager::ApplyTeamsNow("Roster panel button");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Force apply team preferences for all players immediately (requires in-game)");
            }

            ImGui::SameLine();
            if (ImGui::Button("Refresh Profiles")) {
                ProfileManager::LoadAllProfiles();
                LOG_INFO("[ROSTER] Profiles refreshed from disk");
            }

            // Log roster state periodically for debugging (only when roster changes)
            static int s_last_player_count = -1;
            if (s_last_player_count != p_setting->player_count) {
                LOG_INFO("[ROSTER] Player count changed: {} -> {}", s_last_player_count, p_setting->player_count);
                for (int i = 0; i < p_setting->player_count; ++i) {
                    auto p_profile = CGameManager::get_profile(i);
                    char name_buf[256] = "(null)";
                    if (p_profile && p_profile->name[0] != L'\0') {
                        wcstombs(name_buf, p_profile->name, sizeof(name_buf));
                    }
                    const std::string& prof_key = ProfileManager::selected_profile_key[i];
                    LOG_INFO("[ROSTER] Slot {} - Name: '{}', Profile: '{}'", i, name_buf,
                        prof_key.empty() ? "(none)" : prof_key.c_str());
                }
                s_last_player_count = p_setting->player_count;
            }
        }
        // P0-3: else block removed - no need to track show_roster anymore
    }

    // ==============================================================================
    // Session Details Window - Sortable roster table and live game stats
    // ==============================================================================

    // Sorting state for roster table
    struct RosterRow {
        int slot;
        char name[256];
        char tag[8];
        int team;
        const char* team_name;
        int rank_level;
        int rank_xp;
        const char* console;
        int profile_idx;
    };
    static std::vector<RosterRow> s_roster_rows;
    static ImGuiTableSortSpecs* s_current_sort_specs = nullptr;

    static int CompareRosterRows(const void* a, const void* b) {
        const RosterRow* ra = (const RosterRow*)a;
        const RosterRow* rb = (const RosterRow*)b;

        for (int n = 0; n < s_current_sort_specs->SpecsCount; n++) {
            const ImGuiTableColumnSortSpecs* spec = &s_current_sort_specs->Specs[n];
            int delta = 0;

            switch (spec->ColumnIndex) {
                case 0: delta = ra->slot - rb->slot; break;  // Slot
                case 1: delta = strcmp(ra->name, rb->name); break;  // Name
                case 2: delta = strcmp(ra->tag, rb->tag); break;  // Tag
                case 3: delta = ra->team - rb->team; break;  // Team
                case 4: delta = strcmp(ra->console, rb->console); break;  // Console
                case 5: delta = ra->rank_level - rb->rank_level; break;  // Rank
            }

            if (delta != 0) {
                return (spec->SortDirection == ImGuiSortDirection_Ascending) ? delta : -delta;
            }
        }
        return 0;
    }

    void SessionRosterTab() {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();

        // Team colors
        const ImVec4 team_colors[] = {
            ImVec4(0.9f, 0.2f, 0.2f, 1.0f),  // Red
            ImVec4(0.2f, 0.4f, 0.9f, 1.0f),  // Blue
            ImVec4(0.2f, 0.8f, 0.2f, 1.0f),  // Green
            ImVec4(0.9f, 0.6f, 0.1f, 1.0f),  // Orange
            ImVec4(0.6f, 0.2f, 0.8f, 1.0f),  // Purple
            ImVec4(0.9f, 0.8f, 0.2f, 1.0f),  // Gold
            ImVec4(0.5f, 0.3f, 0.2f, 1.0f),  // Brown
            ImVec4(0.9f, 0.4f, 0.6f, 1.0f),  // Pink
        };

        // Build roster data
        s_roster_rows.clear();
        for (int i = 0; i < p_setting->player_count && i < 4; i++) {
            RosterRow row = {};
            row.slot = i + 1;
            row.console = "Local-PC";  // Default; could come from instance config later

            auto p_profile = CGameManager::get_profile(i);
            if (p_profile && p_profile->name[0] != L'\0') {
                wcstombs(row.name, p_profile->name, sizeof(row.name));
            } else {
                strcpy(row.name, "(not set)");
            }

            auto* prof = ProfileManager::GetProfileByKey(ProfileManager::selected_profile_key[i]);
            row.profile_idx = ProfileManager::FindProfileIndexByKey(ProfileManager::selected_profile_key[i]);
            if (prof) {
                wcstombs(row.tag, prof->service_tag, sizeof(row.tag));
                row.team = static_cast<int>(prof->team_preference);
                row.team_name = GetTeamName(prof->team_preference);
                row.rank_level = prof->rank_level;
                row.rank_xp = prof->rank_xp;
            } else {
                strcpy(row.tag, "----");
                row.team = 0;
                row.team_name = "-";
                row.rank_level = 1;
                row.rank_xp = 0;
            }

            s_roster_rows.push_back(row);
        }

        // Sortable table
        ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti |
                                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTable("RosterDetailTable", 6, flags)) {
            ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 45.0f);
            ImGui::TableSetupColumn("Player Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Console", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            // Sort if specs changed
            if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
                if (specs->SpecsDirty && !s_roster_rows.empty()) {
                    s_current_sort_specs = specs;
                    qsort(s_roster_rows.data(), s_roster_rows.size(), sizeof(RosterRow), CompareRosterRows);
                    specs->SpecsDirty = false;
                }
            }

            // Render rows
            for (auto& row : s_roster_rows) {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("P%d", row.slot);

                ImGui::TableNextColumn();
                ImGui::Text("%s", row.name);

                ImGui::TableNextColumn();
                ImGui::Text("%s", row.tag);

                ImGui::TableNextColumn();
                if (row.team >= 0 && row.team < 8) {
                    ImGui::TextColored(team_colors[row.team], "%s", row.team_name);
                } else {
                    ImGui::Text("%s", row.team_name);
                }

                ImGui::TableNextColumn();
                ImGui::Text("%s", row.console);

                ImGui::TableNextColumn();
                // Show rank with XP progress
                const char* rank_name = ProfileManager::GetRankName(row.rank_level);
                ImGui::Text("Lv%d %s", row.rank_level, rank_name);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("XP: %d", row.rank_xp);
                }
            }

            ImGui::EndTable();
        }

        // Note: Quick action buttons are now at the Session Details window level
    }

    void GameStatsTab() {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        bool is_in_game = MCC::IsInGame();

        // Status
        if (is_in_game) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "LIVE - In Match");
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Waiting for match to start...");
        }

        ImGui::Separator();

        // Stats table
        ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchProp;

        if (ImGui::BeginTable("GameStatsTable", 6, flags)) {
            ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 45.0f);
            ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Kills", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Deaths", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Assists", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < p_setting->player_count && i < 4; i++) {
                auto p_profile = CGameManager::get_profile(i);
                if (!p_profile) continue;

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("P%d", i + 1);

                ImGui::TableNextColumn();
                char name_buf[256];
                if (p_profile->name[0] != L'\0') {
                    wcstombs(name_buf, p_profile->name, sizeof(name_buf));
                    ImGui::Text("%s", name_buf);
                } else {
                    ImGui::TextDisabled("(not set)");
                }

                ImGui::TableNextColumn();
                if (is_in_game) {
                    ImGui::Text("%d", p_profile->stats_kills);
                } else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableNextColumn();
                if (is_in_game) {
                    ImGui::Text("%d", p_profile->stats_deaths);
                } else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableNextColumn();
                if (is_in_game) {
                    ImGui::Text("%d", p_profile->stats_assists);
                } else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableNextColumn();
                if (is_in_game) {
                    ImGui::Text("%d", p_profile->stats_score);
                } else {
                    ImGui::TextDisabled("-");
                }
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();

        // Note about stats
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Note: Live stats require MCC memory hooks (not yet implemented).");
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Stats will be captured when match ends and saved to history.");

        // Manual stat entry for testing (debug)
        static bool show_debug_stats = false;
        if (ImGui::Checkbox("Debug: Manual Stats Entry", &show_debug_stats)) {}

        if (show_debug_stats) {
            ImGui::Indent();
            for (int i = 0; i < p_setting->player_count && i < 4; i++) {
                auto p_profile = CGameManager::get_profile(i);
                if (!p_profile) continue;

                ImGui::PushID(i);
                char label[32];
                sprintf(label, "P%d Stats", i + 1);
                if (ImGui::CollapsingHeader(label)) {
                    ImGui::InputInt("Kills", &p_profile->stats_kills);
                    ImGui::InputInt("Deaths", &p_profile->stats_deaths);
                    ImGui::InputInt("Assists", &p_profile->stats_assists);
                    ImGui::InputInt("Score", &p_profile->stats_score);
                }
                ImGui::PopID();
            }

            if (ImGui::Button("Simulate Match End")) {
                ProfileManager::OnMatchEnd();
            }
            ImGui::Unindent();
        }
    }

    void SessionDetailsWindow() {
        static bool show = true;
        ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Session Details", &show)) {
            auto p_setting = AlphaRing::Global::MCC::Splitscreen();

            // ========== STATUS INDICATORS (moved from Session Roster) ==========
            {
                bool is_enabled = p_setting->b_override;
                bool is_in_game = MCC::IsInGame();

                ImGui::Text("Status: ");
                ImGui::SameLine();
                if (is_enabled) {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "ENABLED");
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.2f, 1.0f), "DISABLED");
                }
                ImGui::SameLine();
                ImGui::Text(" | ");
                ImGui::SameLine();
                if (is_in_game) {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "IN GAME");
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "IN MENU");
                }
                ImGui::SameLine();
                ImGui::Text(" | Players: %d", p_setting->player_count);
            }

            // Quick actions row
            if (ImGui::Button("Set Teams Now")) {
                ProfileManager::ApplyTeamsNow("Session Details button");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Force apply team preferences for all players immediately");
            }

            ImGui::SameLine();
            if (ImGui::Button("Refresh Profiles")) {
                ProfileManager::LoadAllProfiles();
                LOG_INFO("[UI] Profiles refreshed from disk via Session Details");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reload all profiles from disk");
            }

            ImGui::SameLine();
            if (ImGui::Button("Apply All Armor")) {
                for (int i = 0; i < p_setting->player_count && i < 4; i++) {
                    ProfileManager::ScheduleArmorTwoShot(i);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Force apply armor settings for all players (two-shot apply)");
            }

            ImGui::Separator();

            // Tab bar for Roster and Game Stats
            if (ImGui::BeginTabBar("SessionTabs")) {
                if (ImGui::BeginTabItem("Roster")) {
                    SessionRosterTab();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Game Stats")) {
                    GameStatsTab();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }
}