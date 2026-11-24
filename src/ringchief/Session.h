#pragma once
// Ring Chief session: logs a PC into a group on halo.dronedude.app, keeps the group's
// profiles, relays between the server and the other MCC instances on this PC, and pushes
// profile / team / slot changes into the game through GameAdapter.
//
// Threading: Tick() and everything that touches the game runs on the caller's (render)
// thread. The login request and the WebSocket receive loop run on one worker thread and
// hand results over through a locked queue.
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Http.h"
#include "LocalLink.h"
#include "ProfileV5.h"
#include "Storage.h"

namespace RingChief {
    using json = nlohmann::json;

    // What the session needs from the game. GameBridge.cpp implements it for MCC;
    // tests use a fake.
    class GameAdapter {
    public:
        virtual ~GameAdapter() = default;
        virtual std::string InstanceId() = 0;                         // unique per MCC process
        virtual int SlotCount() = 0;                                  // local players (1-4)
        virtual std::string SlotProfile(int slot) = 0;                // profile id or ""
        virtual void SetGroupProfiles(const std::vector<V5Profile>& profiles) = 0;
        virtual void UpdateProfile(const V5Profile& profile) = 0;     // create or edit
        virtual void RemoveProfile(const std::string& id) = 0;
        virtual void AssignSlot(int slot, const std::string& profile_id) = 0;
        virtual void ApplyTeams(const std::map<std::string, int>& teams) = 0;
        // {"inGame":bool,"game":..,"mode":..,"map":..,"matchEpoch":n,"players":[...]} or null
        virtual json LiveStats() = 0;
        // How this screen is set up, for the phone remote:
        // {"enabled","overlay","inGame","keyboardP1","players","controllers":[4],"connected":[4]}
        virtual json ScreenConfig() { return nullptr; }
        // Applies a change from the phone remote (same keys plus "slots":{"0":"p_.."|null}).
        // All or nothing; false + a message if it can't.
        virtual bool ApplyControl(const json& /*set*/, std::string& err) { err = "Not supported"; return false; }
    };

    class Session {
    public:
        enum class State { LoggedOut, Working, Online, Reconnecting, Offline, Follower };

        struct Options {
            uint16_t local_port = 42069;
            std::string client = "AlphaRing";
            bool use_storage = true;           // false: no disk reads/writes at all
            std::string storage_dir;           // tests: use this folder instead of %ProgramData%\RingChief
            double backup_interval_s = 60.0;   // at most one backup per minute for live edits
            double ping_interval_s = 20.0;
            double stats_interval_s = 0.25;
        };

        Session(GameAdapter& game, Options options);
        ~Session();

        // Default login fields (e.g. from the Nucleus instance config); a saved login wins.
        void Prefill(const std::string& server, const std::string& group_id) {
            if (!group_input_.empty()) return;
            if (!server.empty()) server_input_ = server;
            group_input_ = group_id;
        }
        void Start();                // pick hub/follower role, resume a saved login
        void Tick(double now_s);     // call every frame
        void Shutdown();

        // UI actions (hub only)
        void Login(const std::string& server, const std::string& group_id, const std::string& password, bool remember);
        // Sign in without a password: shows a code to approve from a phone (PairCode()).
        void LoginWithCode(const std::string& server, bool remember);
        void CancelLogin();
        void PlayOffline();                              // latest local copy of the group
        bool PlayFromBackup(const std::string& path);    // a specific local backup
        void Logout();

        // Group tools for a PC signed in with a code (hub only; results arrive as frames).
        void SwitchGroup(const std::string& group_id);   // "" = no group
        void NewScreenCode();
        void AddPlayer(const std::string& gamertag);
        void RemovePlayer(const std::string& profile_id);

        // Local backups of the group's profiles (hub only; followers use the hub's).
        bool BackupNow();                                // false if nothing changed / no group
        std::vector<Storage::BackupInfo> Backups() const;
        const std::string& LastBackupAt() const { return last_backup_at_; }

        // Game events
        void OnMatchEnd(const json& match);   // {"game","mode","map","matchEpoch","players":[...],"winner"}
        void SlotsChanged() { slots_dirty_ = true; }

        // UI state
        State GetState() const { return state_; }
        LocalLink::Role GetRole() const { return link_.GetRole(); }
        const std::string& Error() const { return error_; }
        const std::string& ServerInput() const { return server_input_; }
        const std::string& GroupInput() const { return group_input_; }
        const std::string& GroupName() const { return group_name_; }
        std::string NightName() const;
        const std::string& PairCode() const { return pair_code_; }   // "" unless waiting for a phone
        const std::string& PairUrl() const { return pair_url_; }
        const json& Pc() const { return pc_; }                       // last "pc" frame (screen code, groups, ...)
        const std::string& Notice() const { return notice_; }        // result of the last group action
        size_t FollowerCount() const { return link_.FollowerCount(); }
        const std::vector<V5Profile>& Profiles() const { return profiles_; }
        bool WantsLoginPanel() const;

    private:
        struct Event { enum Kind { PairCode, LoginOk, LoginFailed, Connected, Frame, Disconnected, AuthFailed } kind; std::string text; json data; };

        void StartWorker(const ServerAddr& server, const std::string& token, const std::string& login_body, bool pair = false);
        bool WaitForPairing(const ServerAddr& server, const std::string& body, std::string& token);   // worker thread
        void SaveCurrentLogin();
        void StopWorker();
        void Push(Event e);
        void Drain(double now_s);

        void PollLink(double now_s);
        void HandleFrame(const json& msg, bool from_server);
        void ApplyWelcome(const json& welcome);
        std::string GroupKeyForStorage() const;
        void SaveLocalCopy(bool force_backup);
        void ApplyTeamsFrom(const json& teams);
        void SendUp(const json& msg);           // hub: to server; follower: to hub
        void SendHello();
        json MyInstance();
        json CombinedInstances();
        void SendInstancesIfChanged(bool force);
        void BecomeRole(double now_s);

        GameAdapter& game_;
        Options opt_;
        LocalLink link_;
        State state_ = State::LoggedOut;
        std::string error_;

        // login
        ServerAddr server_;
        std::string server_input_ = "halo.dronedude.app";
        std::string group_input_;
        std::string group_name_;
        std::string token_;
        bool remember_ = true;
        std::string expires_at_;
        std::string pair_code_, pair_url_, notice_;
        json pc_;

        // group
        std::vector<V5Profile> profiles_;
        json welcome_;               // last welcome (kept current with profile updates)
        json night_;
        std::string group_id_;       // from the server's welcome ("wargames2027")
        bool backup_dirty_ = false;
        double next_backup_ = 0;
        std::string last_backup_at_;

        // local link
        std::map<int, json> follower_instances_;   // hub: peer -> instances array
        std::string last_instances_;
        bool slots_dirty_ = true;
        double next_role_try_ = 0, next_ping_ = 0, next_stats_ = 0;

        // worker
        std::thread worker_;
        std::atomic<bool> stopping_{false};
        std::shared_ptr<WebSocket> ws_;
        std::mutex ws_mutex_;
        std::mutex inbox_mutex_;
        std::deque<Event> inbox_;
    };
}
