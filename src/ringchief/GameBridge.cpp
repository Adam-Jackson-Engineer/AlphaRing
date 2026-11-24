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
#include "input/Input.h"
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

        // Empties a seat: no Ring Chief profile, plain "Player N" name.
        void ClearSlot(int slot) {
            ProfileManager::selected_profile_key[slot].clear();
            if (auto* p = CGameManager::get_profile(slot)) swprintf(p->name, 16, L"Player %d", slot + 1);
        }

        // Which controllers are plugged in. Asking XInput about an empty port is slow, so one
        // port a second, in turn.
        const bool* ConnectedControllers() {
            static bool connected[4] = {false, false, false, false};
            static int next = 0;
            static double next_at = 0;
            double now = Now();
            if (now >= next_at) {
                next_at = now + 0.75;
                XINPUT_STATE state;
                connected[next] = AlphaRing::Input::GetXInputGetState(next, &state);
                next = (next + 1) % 4;
            }
            return connected;
        }

        // AlphaRing split-screen with Ring Chief profiles, on or off.
        void SetEnabled(bool on) {
            auto* ss = AlphaRing::Global::MCC::Splitscreen();
            ss->b_override = on;
            if (on) {
                ss->b_override_profile = true;
                ss->b_use_player0_profile = false;
                for (int i = 0; i < 4; i++) ApplySlot(i);
            }
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
                if (slot < 0 || slot >= 4) return;
                if (id.empty()) { ClearSlot(slot); return; }
                if (!ProfileManager::GetProfileByKey(id)) return;
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

            json ScreenConfig() override {
                auto* ss = AlphaRing::Global::MCC::Splitscreen();
                const bool* connected = ConnectedControllers();
                json controllers = json::array(), plugged = json::array();
                for (int i = 0; i < 4; i++) {
                    controllers.push_back(std::clamp(CGameManager::get_profile(i)->controller_index, 0, 4));
                    plugged.push_back(connected[i]);
                }
                return {{"enabled", ss->b_override}, {"overlay", AlphaRing::Global::Global()->show_imgui},
                        {"inGame", MCC::IsInGame()}, {"keyboardP1", ss->b_player0_use_km},
                        {"players", std::clamp(ss->player_count, 1, 4)},
                        {"controllers", controllers}, {"connected", plugged}};
            }

            bool ApplyControl(const json& set, std::string& err) override {
                auto* ss = AlphaRing::Global::MCC::Splitscreen();
                auto flag = [&](const char* k) { return set.contains(k) && set[k].is_boolean(); };

                // Check everything first, so a bad change does nothing at all.
                int players = std::clamp(ss->player_count, 1, 4);
                if (set.contains("players")) {
                    if (!set["players"].is_number_integer()) { err = "Players must be 1-4"; return false; }
                    players = set["players"].get<int>();
                    if (players < 1 || players > 4) { err = "Players must be 1-4"; return false; }
                }
                int pads[4];
                bool set_pads = set.contains("controllers");
                if (set_pads) {
                    const auto& c = set["controllers"];
                    bool ok = c.is_array() && c.size() == 4;
                    for (int i = 0; ok && i < 4; i++) {
                        ok = c[i].is_number_integer();
                        if (ok) pads[i] = c[i].get<int>();
                        ok = ok && pads[i] >= 0 && pads[i] <= 4;
                        for (int j = 0; ok && j < i; j++) ok = pads[i] == 4 || pads[j] != pads[i];
                    }
                    if (!ok) { err = "Those controllers don't work together"; return false; }
                }
                std::map<int, std::string> seats;
                if (set.contains("slots") && set["slots"].is_object()) {
                    for (const auto& [k, v] : set["slots"].items()) {
                        if (k.size() != 1 || k[0] < '0' || k[0] > '3') { err = "No such seat"; return false; }
                        std::string id = v.is_string() ? v.get<std::string>() : "";
                        if (!id.empty() && !ProfileManager::GetProfileByKey(id)) { err = "That player isn't loaded on this PC"; return false; }
                        seats[k[0] - '0'] = id;
                    }
                }

                ss->player_count = players;
                if (set_pads) for (int i = 0; i < 4; i++) CGameManager::get_profile(i)->controller_index = pads[i];
                if (flag("keyboardP1")) ss->b_player0_use_km = set["keyboardP1"].get<bool>();
                for (const auto& [slot, id] : seats) if (id.empty()) AssignSlot(slot, "");
                for (const auto& [slot, id] : seats) if (!id.empty()) AssignSlot(slot, id);
                if (flag("enabled")) SetEnabled(set["enabled"].get<bool>());
                if (flag("overlay")) AlphaRing::Global::Global()->show_imgui = set["overlay"].get<bool>();
                LOG_INFO("[RINGCHIEF] Remote change: {}", set.dump());
                return true;
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

        // Players and input for this MCC window.
        void ScreenSection() {
            auto* ss = AlphaRing::Global::MCC::Splitscreen();
            int players = ss->b_override ? std::clamp(ss->player_count, 1, 4) : 1;
            ImGui::Text("Players on this screen");
            for (int n = 1; n <= 4; n++) {
                char label[8];
                snprintf(label, sizeof(label), "%d##players", n);
                if (n > 1) ImGui::SameLine();
                if (ImGui::RadioButton(label, players == n) && players != n) {
                    // Always AlphaRing input: player N = controller N, profiles applied.
                    ss->player_count = n;
                    SetEnabled(true);
                    g_session->SlotsChanged();
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("With Nucleus, join the LAN lobby first, then add players here.");

            if (!ss->b_override) {
                ImGui::TextDisabled("AlphaRing is off on this screen: Halo uses its own profile and controls.");
                if (ImGui::SmallButton("Turn on")) { SetEnabled(true); g_session->SlotsChanged(); }
                return;
            }
            const bool* connected = ConnectedControllers();
            for (int i = 0; i < players; i++) {
                int& pad = CGameManager::get_profile(i)->controller_index;
                bool km = i == 0 && ss->b_player0_use_km;
                auto pad_name = [&](int c) {
                    return c >= 0 && c < 4 ? "Controller " + std::to_string(c + 1) + (connected[c] ? "" : " (off)") : std::string("No controller");
                };
                char label[40];
                snprintf(label, sizeof(label), "Player %d uses##pad%d", i + 1, i);
                if (ImGui::BeginCombo(label, km ? "Keyboard & mouse" : pad_name(pad).c_str())) {
                    if (i == 0 && ImGui::Selectable("Keyboard & mouse", km)) ss->b_player0_use_km = true;
                    for (int c = 0; c <= 4; c++) {
                        if (!ImGui::Selectable(pad_name(c).c_str(), !km && pad == c)) continue;
                        // Swap with whoever has it, so two players never share one.
                        for (int j = 0; c < 4 && j < 4; j++) {
                            int& other = CGameManager::get_profile(j)->controller_index;
                            if (j != i && other == c) other = pad;
                        }
                        pad = c;
                        if (i == 0) ss->b_player0_use_km = false;
                    }
                    ImGui::EndCombo();
                }
            }
            if (ImGui::SmallButton("Turn AlphaRing off on this screen")) { SetEnabled(false); g_session->SlotsChanged(); }
        }

        // Phone remote + adding people, from the server's "pc" frame.
        void FriendsSection() {
            auto* s = g_session.get();
            const json& pc = s->Pc();
            if (!pc.is_object() || s->GetState() != Session::State::Online) return;
            const std::string code = pc.value("code", ""), url = pc.value("codeUrl", "");
            if (!code.empty()) {
                ImGui::Text("Phones: %s", url.c_str());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.72f, 0.88f, 1.0f, 1), "%s", code.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("New code")) s->NewScreenCode();
                ImGui::TextDisabled("Loads your Spartan here and makes the phone a remote.");
            }
            // Group on this PC (only PCs signed in with a code can switch).
            if (!pc.value("owner", "").empty() && pc.contains("groups") && pc["groups"].is_array()) {
                const std::string current = pc.value("groupId", "");
                if (ImGui::BeginCombo("Group", s->GroupName().c_str())) {
                    if (ImGui::Selectable("No group", current.empty())) s->SwitchGroup("");
                    for (const auto& g : pc["groups"]) {
                        const std::string id = g.value("id", ""), name = g.value("name", "") + "##" + id;
                        if (ImGui::Selectable(name.c_str(), id == current)) s->SwitchGroup(id);
                    }
                    ImGui::EndCombo();
                }
            }
            if (pc.value("canManage", false) && ImGui::CollapsingHeader("Manage group")) {
                ImGui::Text("Friends join this group with code");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.72f, 0.88f, 1.0f, 1), "%s", pc.value("joinCode", "").c_str());
                static char name[24] = {0};
                ImGui::InputTextWithHint("##newplayer", "New friend's gamertag", name, sizeof(name));
                ImGui::SameLine();
                if (ImGui::Button("Add") && name[0]) { s->AddPlayer(name); name[0] = 0; }
                static std::string confirm;
                std::string owner = pc.value("owner", "");
                for (const auto& p : s->Profiles()) {
                    bool member = false;
                    if (pc.contains("memberIds") && pc["memberIds"].is_array())
                        for (const auto& m : pc["memberIds"]) if (m.is_string() && m.get<std::string>() == p.id) member = true;
                    if (!member) continue;
                    ImGui::PushID(p.id.c_str());
                    ImGui::Text("%s", MCC::Splitscreen::wstring_to_utf8(p.gamertag).c_str());
                    ImGui::SameLine(ImGui::GetFontSize() * 14.0f);
                    if (ImGui::SmallButton(confirm == p.id ? "Really remove?" : "Remove")) {
                        std::string id = p.id;
                        if (confirm == id) { confirm.clear(); s->RemovePlayer(id); } else confirm = id;
                    }
                    ImGui::PopID();
                }
            }
            if (!s->Notice().empty()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
                ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1), "%s", s->Notice().c_str());
                ImGui::PopTextWrapPos();
            }
        }

        void TroubleshootingSection() {
            if (!ImGui::CollapsingHeader("Troubleshooting")) return;
            auto* g = AlphaRing::Global::Global();
            ImGui::Text("Left clicks: %d seen, %d to the game, %d kept by this overlay",
                        g->clicks_seen, g->clicks_to_game, g->clicks_to_overlay);
            ImGui::TextDisabled("Clicks on an overlay window stay here; everywhere else goes to MCC.");
            ImGui::Checkbox("Hide focus changes from MCC (multi-window blur fix)", &g->keep_focus);
            ImGui::TextDisabled("On automatically with Nucleus. Press Insert or F4 to hide the overlay.");
        }

        // Local copies of the group's profiles on this PC (for offline play / if the site is down).
        void BackupsSection() {
            auto* s = g_session.get();
            if (!ImGui::CollapsingHeader("Local backups")) return;
            auto list = s->Backups();
            if (list.empty()) {
                ImGui::TextDisabled("No backups on this PC yet. One is made after the first sign-in.");
            } else {
                ImGui::Text("%zu backup(s), newest %s", list.size(), list.front().saved_at.substr(0, 16).c_str());
            }
            if (s->GetState() == Session::State::Online || s->GetState() == Session::State::Offline || s->GetState() == Session::State::Reconnecting) {
                if (ImGui::Button("Back up now")) s->BackupNow();
                ImGui::SameLine();
                ImGui::TextDisabled("(automatic after changes, at most once a minute)");
            }
            int shown = 0;
            for (const auto& b : list) {
                if (shown++ >= 8) break;
                ImGui::PushID(b.path.c_str());
                ImGui::Text("%s  -  %zu players", b.saved_at.substr(0, 16).c_str(), b.profile_count);
                ImGui::SameLine();
                if (ImGui::SmallButton("Play from this")) s->PlayFromBackup(b.path);
                ImGui::PopID();
            }
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
            if (s->GroupInput().rfind("pc_", 0) != 0) strncpy_s(group, s->GroupInput().c_str(), _TRUNCATE);
            filled = true;
        }

        auto* g = AlphaRing::Global::Global();
        if (g->pause_game_on_menu_shown || g->disable_input_on_menu_shown)
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.8f, 0.3f, 1));
            ImGui::TextWrapped("The game ignores input while this overlay is open. Press Insert, F4 or Back+Start to hide it.");
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }

        auto state = s->GetState();
        if (state == Session::State::Follower) {
            ImGui::TextWrapped("This screen is connected through the main game window on this PC.");
            if (!s->GroupName().empty()) ImGui::Text("Group: %s", s->GroupName().c_str());
            if (!s->NightName().empty()) ImGui::Text("Tonight: %s", s->NightName().c_str());
            ImGui::Separator();
            ScreenSection();
            ImGui::Separator();
            SlotPickers();
            TroubleshootingSection();
        } else if (state == Session::State::LoggedOut || state == Session::State::Working) {
            if (state == Session::State::Working && !s->PairCode().empty()) {
                ImGui::TextWrapped("On your phone, open your personal link, then go to");
                ImGui::Text("%s", s->PairUrl().c_str());
                ImGui::Text("and enter");
                ImGui::SetWindowFontScale(2.0f);
                ImGui::TextColored(ImVec4(0.72f, 0.88f, 1.0f, 1), "%s", s->PairCode().c_str());
                ImGui::SetWindowFontScale(1.0f);
                ImGui::TextDisabled("Waiting for your phone...");
                if (ImGui::Button("Cancel")) s->CancelLogin();
            } else if (state == Session::State::Working) {
                ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1), "Signing in...");
                if (ImGui::Button("Cancel")) s->CancelLogin();
            } else {
                ImGui::TextWrapped("Sign in to load your Spartans. No group needed.");
                ImGui::Spacing();
                if (ImGui::Button("Sign in with a code")) s->LoginWithCode(server, remember);
                ImGui::SameLine();
                if (ImGui::Button("Play offline")) s->PlayOffline();
                ImGui::Checkbox("Remember on this PC", &remember);
                if (ImGui::CollapsingHeader("Use a group password instead")) {
                    ImGui::InputText("Group ID", group, sizeof(group));
                    ImGui::InputText("Password", password, sizeof(password), ImGuiInputTextFlags_Password);
                    if (ImGui::Button("Join")) {
                        s->Login(server, group, password, remember);
                        SecureZeroMemory(password, sizeof(password));
                    }
                }
                if (ImGui::CollapsingHeader("Server")) ImGui::InputText("Server", server, sizeof(server));
            }
            if (!s->Error().empty()) ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", s->Error().c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("Players build their Spartans on the website.");
            BackupsSection();
            TroubleshootingSection();
        } else {
            const char* label = state == Session::State::Online ? "Online" : state == Session::State::Offline ? "Offline (saved group)" : "Reconnecting...";
            ImGui::Text("%s  -  %s", label, s->GroupName().c_str());
            if (!s->NightName().empty()) ImGui::Text("Tonight: %s", s->NightName().c_str());
            if (s->FollowerCount()) ImGui::Text("Other screens on this PC: %zu", s->FollowerCount());
            if (!s->Error().empty() && state != Session::State::Online) ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1), "%s", s->Error().c_str());
            FriendsSection();
            ImGui::Separator();
            ScreenSection();
            ImGui::Separator();
            SlotPickers();
            ImGui::Separator();
            BackupsSection();
            TroubleshootingSection();
            if (ImGui::Button(state == Session::State::Offline ? "Sign in" : "Log out")) { s->Logout(); filled = false; }
        }
        ImGui::End();
    }
}
