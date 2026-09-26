#include "GameBridge.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>

#include "common.h"
#include "imgui.h"
#include "global/Global.h"
#include "mcc/mcc.h"
#include "mcc/CGameManager.h"
#include "mcc/CGameGlobal.h"
#include "mcc/InstanceConfig.h"
#include "mcc/splitscreen/ProfileManager.h"
#include "mcc/CUserProfileJson.h"

#include "Session.h"

namespace RingChief::Game {
    using MCC::Splitscreen::ProfileManager;
    using MCC::Splitscreen::PersistentProfile;

    namespace {
        std::map<std::string, int> g_teams;   // tonight's teams: profile id -> team

        double Now() {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        std::string GameTitle() {
            auto* g = GameGlobal();
            if (!g) return "";
            switch (g->current_game) {
                case CGameGlobal::Halo1: return "Halo CE";
                case CGameGlobal::Halo2: return "Halo 2";
                case CGameGlobal::Halo3: return "Halo 3";
                case CGameGlobal::Halo4: return "Halo 4";
                case CGameGlobal::GroundHog: return "Halo 2 Anniversary";
                case CGameGlobal::Halo3ODST: return "Halo 3 ODST";
                case CGameGlobal::HaloReach: return "Halo Reach";
                default: return "";
            }
        }

        std::string MakeInstanceId() {
            char name[MAX_COMPUTERNAME_LENGTH + 1] = {0};
            DWORD n = sizeof(name);
            if (!GetComputerNameA(name, &n)) strcpy_s(name, "PC");
            return std::string(name) + "_" + std::to_string(GetCurrentProcessId());
        }

        PersistentProfile ToPersistent(const V5Profile& v) {
            PersistentProfile p;
            p.filename = v.id;
            p.display_name = v.gamertag;
            std::memcpy(p.service_tag, v.service_tag, sizeof(p.service_tag));
            p.service_tag[4] = L'\0';
            p.rank_xp = v.rank_xp;
            p.rank_level = ProfileManager::ComputeRankLevel(v.rank_xp);
            p.controller_preset = static_cast<MCC::Splitscreen::ControllerPreset>(v.controller_preset);
            p.team_preference = static_cast<MCC::Splitscreen::Team>(v.team_preference);
            if (v.has_custom_mapping) p.gamepad_mapping = v.custom_mapping;
            p.user_profile = BuildUserProfile(v.raw);   // what the overlay's editors show
            if (v.raw.contains("stats") && v.raw["stats"].is_object()) {
                const auto& s = v.raw["stats"];
                p.career_stats.games_played = s.value("games", 0);
                p.career_stats.wins = s.value("wins", 0);
                p.career_stats.losses = s.value("losses", 0);
                p.career_stats.total_kills = s.value("kills", 0);
                p.career_stats.total_deaths = s.value("deaths", 0);
                p.career_stats.total_assists = s.value("assists", 0);
                p.career_stats.total_score = s.value("score", 0);
            }
            p.v5 = v.raw;
            return p;
        }

        void ApplySlot(int slot) {
            auto* prof = ProfileManager::GetProfileByKey(ProfileManager::selected_profile_key[slot]);
            if (!prof) return;
            ProfileManager::ApplyToSlot(slot, *prof);
            auto t = g_teams.find(prof->filename);
            ProfileManager::SetSlotTeam(slot, t != g_teams.end() ? t->second : static_cast<int>(prof->team_preference));
        }

        class MccAdapter : public GameAdapter {
        public:
            std::string InstanceId() override { return id_; }

            int SlotCount() override {
                auto* s = AlphaRing::Global::MCC::Splitscreen();
                return s->b_override ? std::clamp(s->player_count, 1, 4) : 1;
            }

            std::string SlotProfile(int slot) override {
                const auto& key = ProfileManager::selected_profile_key[slot];
                return (key.rfind("p_", 0) == 0 && ProfileManager::GetProfileByKey(key)) ? key : "";
            }

            void SetGroupProfiles(const std::vector<V5Profile>& list) override {
                ProfileManager::profiles.clear();
                for (const auto& v : list) ProfileManager::profiles.push_back(ToPersistent(v));
                for (int i = 0; i < 4; i++) ApplySlot(i);
                LOG_INFO("[RINGCHIEF] Group profiles loaded: {}", list.size());
            }

            void UpdateProfile(const V5Profile& v) override {
                auto p = ToPersistent(v);
                auto* existing = ProfileManager::GetProfileByKey(v.id);
                if (existing) *existing = p; else ProfileManager::profiles.push_back(p);
                for (int i = 0; i < 4; i++) {
                    if (ProfileManager::selected_profile_key[i] == v.id) {
                        ApplySlot(i);
                        LOG_INFO("[RINGCHIEF] Live update applied to slot {} ({})", i, v.id);
                    }
                }
            }

            void RemoveProfile(const std::string& id) override {
                auto& list = ProfileManager::profiles;
                list.erase(std::remove_if(list.begin(), list.end(), [&](const PersistentProfile& p) { return p.filename == id; }), list.end());
            }

            void AssignSlot(int slot, const std::string& id) override {
                if (slot < 0 || slot >= 4 || !ProfileManager::GetProfileByKey(id)) return;
                ProfileManager::selected_profile_key[slot] = id;
                ApplySlot(slot);
                LOG_INFO("[RINGCHIEF] Slot {} claimed by {}", slot, id);
            }

            void ApplyTeams(const std::map<std::string, int>& teams) override {
                g_teams = teams;
                for (int i = 0; i < 4; i++) {
                    auto t = teams.find(ProfileManager::selected_profile_key[i]);
                    if (t != teams.end()) ProfileManager::SetSlotTeam(i, t->second);
                }
            }

            json LiveStats() override {
                if (!MCC::IsInGame()) return {{"inGame", false}};
                json players = json::array();
                for (int i = 0; i < SlotCount(); i++) {
                    auto* s = CGameManager::get_profile(i);
                    if (!s) continue;
                    std::string pid = SlotProfile(i);
                    players.push_back({
                        {"slot", i}, {"profileId", pid.empty() ? json(nullptr) : json(pid)},
                        {"team", ProfileManager::current_team[i]},
                        {"kills", s->stats_kills}, {"deaths", s->stats_deaths},
                        {"assists", s->stats_assists}, {"score", s->stats_score},
                    });
                }
                return {{"inGame", true}, {"game", GameTitle()}, {"mode", ""}, {"map", ""},
                        {"matchEpoch", ProfileManager::GetMatchEpoch()}, {"players", players}};
            }

        private:
            std::string id_ = MakeInstanceId();
        };

        std::unique_ptr<MccAdapter> g_adapter;
        std::unique_ptr<Session> g_session;
    }

    Session* Get() { return g_session.get(); }

    void Initialize() {
        if (g_session) return;
        g_adapter = std::make_unique<MccAdapter>();
        Session::Options opt;
        opt.client = std::string("AlphaRing ") + GAME_VERSION + " (Ring Chief)";
        g_session = std::make_unique<Session>(*g_adapter, opt);
        const auto& cfg = AlphaRing::Config::GetInstanceConfig();
        if (cfg.loaded) g_session->Prefill(cfg.ringchief_server, cfg.ringchief_group);
        g_session->Start();
        LOG_INFO("[RINGCHIEF] Session started as {}", g_session->GetRole() == LocalLink::Role::Hub ? "hub" :
                 g_session->GetRole() == LocalLink::Role::Follower ? "follower" : "standalone (link unavailable)");
    }

    void Tick() {
        if (g_session) g_session->Tick(Now());
    }

    void Shutdown() {
        if (g_session) g_session->Shutdown();
    }

    bool WantsAttention() { return g_session && g_session->WantsLoginPanel(); }

    void ReloadProfiles() {
        if (g_session && g_adapter) g_adapter->SetGroupProfiles(g_session->Profiles());
    }

    void SlotsChanged() {
        if (g_session) g_session->SlotsChanged();
    }

    void ReportMatchEnd(int match_epoch) {
        if (!g_session || !g_adapter) return;
        json players = json::array();
        bool any = false;
        for (int i = 0; i < g_adapter->SlotCount(); i++) {
            auto* s = CGameManager::get_profile(i);
            std::string pid = g_adapter->SlotProfile(i);
            if (!s || pid.empty()) continue;
            if (s->stats_kills || s->stats_deaths || s->stats_assists || s->stats_score) any = true;
            players.push_back({{"slot", i}, {"profileId", pid}, {"team", ProfileManager::current_team[i]},
                               {"kills", s->stats_kills}, {"deaths", s->stats_deaths},
                               {"assists", s->stats_assists}, {"score", s->stats_score}});
        }
        // Until live stats are read from the game, an all-zero match would only add noise;
        // the host records those on the website instead.
        if (!any) { LOG_INFO("[RINGCHIEF] Match {} ended with no stats; not reported", match_epoch); return; }
        g_session->OnMatchEnd({{"game", GameTitle()}, {"mode", ""}, {"map", ""}, {"matchEpoch", match_epoch},
                               {"players", players}, {"winner", nullptr}});
    }

    // ------------------------------------------------------------------ UI

    void DrawMenuStatus() {
        if (!g_session) return;
        ImVec4 green(0.35f, 0.85f, 0.45f, 1), yellow(0.95f, 0.8f, 0.3f, 1), red(0.95f, 0.4f, 0.4f, 1), grey(0.65f, 0.65f, 0.7f, 1);
        switch (g_session->GetState()) {
            case Session::State::Online: ImGui::TextColored(green, "[Ring Chief: %s]", g_session->GroupName().c_str()); break;
            case Session::State::Working: ImGui::TextColored(yellow, "[Ring Chief: signing in]"); break;
            case Session::State::Reconnecting: ImGui::TextColored(yellow, "[Ring Chief: reconnecting]"); break;
            case Session::State::Offline: ImGui::TextColored(grey, "[Ring Chief: offline]"); break;
            case Session::State::Follower: ImGui::TextColored(green, "[Ring Chief: via main window]"); break;
            default: ImGui::TextColored(red, "[Ring Chief: not signed in]"); break;
        }
    }

    namespace {
        void SlotPickers() {
            auto* s = g_session.get();
            int n = g_adapter->SlotCount();
            const auto& profiles = s->Profiles();
            if (profiles.empty()) { ImGui::TextDisabled("No profiles in this group yet."); return; }
            for (int i = 0; i < n; i++) {
                const std::string& key = ProfileManager::selected_profile_key[i];
                std::string current = "(nobody)";
                for (const auto& p : profiles) {
                    if (p.id == key) current = MCC::Splitscreen::wstring_to_utf8(p.gamertag);
                }
                char label[32];
                snprintf(label, sizeof(label), "Player %d", i + 1);
                if (ImGui::BeginCombo(label, current.c_str())) {
                    for (const auto& p : profiles) {
                        std::string name = MCC::Splitscreen::wstring_to_utf8(p.gamertag) + "  [" +
                                           MCC::Splitscreen::wstring_to_utf8(std::wstring(p.service_tag)) + "]";
                        if (ImGui::Selectable(name.c_str(), p.id == key)) {
                            ProfileManager::selected_profile_key[i] = p.id;
                            ApplySlot(i);
                            s->SlotsChanged();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::TextDisabled("Players can also tap \"That's me\" on the group page.");
        }
    }

    void DrawWindow(bool* open) {
        if (!g_session) return;
        auto* s = g_session.get();
        ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(480, 60), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Ring Chief", open, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }

        static char server[160] = {0}, group[64] = {0}, password[128] = {0};
        static bool remember = true, filled = false;
        if (!filled) {
            strncpy_s(server, s->ServerInput().c_str(), _TRUNCATE);
            strncpy_s(group, s->GroupInput().c_str(), _TRUNCATE);
            filled = true;
        }

        auto state = s->GetState();
        if (state == Session::State::Follower) {
            ImGui::TextWrapped("This screen is connected through the main game window on this PC.");
            if (!s->GroupName().empty()) ImGui::Text("Group: %s", s->GroupName().c_str());
            if (!s->NightName().empty()) ImGui::Text("Tonight: %s", s->NightName().c_str());
            ImGui::Separator();
            SlotPickers();
        } else if (state == Session::State::LoggedOut || state == Session::State::Working) {
            ImGui::TextWrapped("Join your group to load everyone's Spartans.");
            ImGui::Spacing();
            ImGui::InputText("Server", server, sizeof(server));
            ImGui::InputText("Group ID", group, sizeof(group));
            ImGui::InputText("Password", password, sizeof(password), ImGuiInputTextFlags_Password);
            ImGui::Checkbox("Remember on this PC", &remember);
            if (state == Session::State::Working) {
                ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1), "Signing in...");
            } else {
                if (ImGui::Button("Join")) {
                    s->Login(server, group, password, remember);
                    SecureZeroMemory(password, sizeof(password));
                }
                ImGui::SameLine();
                if (ImGui::Button("Play offline")) s->PlayOffline();
            }
            if (!s->Error().empty()) ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", s->Error().c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("Players build their Spartans on the website.");
        } else {
            const char* label = state == Session::State::Online ? "Online" : state == Session::State::Offline ? "Offline (saved group)" : "Reconnecting...";
            ImGui::Text("%s  -  %s", label, s->GroupName().c_str());
            if (!s->NightName().empty()) ImGui::Text("Tonight: %s", s->NightName().c_str());
            if (s->FollowerCount()) ImGui::Text("Other screens on this PC: %zu", s->FollowerCount());
            if (!s->Error().empty() && state != Session::State::Online) ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1), "%s", s->Error().c_str());
            ImGui::Separator();
            SlotPickers();
            ImGui::Separator();
            if (ImGui::Button(state == Session::State::Offline ? "Sign in" : "Log out")) { s->Logout(); filled = false; }
        }
        ImGui::End();
    }
}
