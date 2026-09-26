#include "Session.h"

#include <algorithm>
#include <chrono>
#include <ctime>

#include "Storage.h"

namespace RingChief {

    namespace {
        constexpr int kProtocol = 2;

        std::string NowIso() {
            std::time_t t = std::time(nullptr);
            std::tm tm{};
            gmtime_s(&tm, &t);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
            return buf;
        }

        bool Expired(const std::string& iso) {
            // ISO-8601 strings in UTC compare correctly as text.
            return !iso.empty() && iso.substr(0, 19) <= NowIso().substr(0, 19);
        }

        std::string ErrorFrom(const HttpResult& r) {
            if (r.status == 0) return r.error.empty() ? "Can't reach the server" : r.error;
            try {
                auto j = json::parse(r.body);
                if (j.contains("error") && j["error"].is_string()) return j["error"].get<std::string>();
            } catch (...) {}
            if (r.status == 429) return "Too many tries. Wait a few minutes.";
            return "Server error (HTTP " + std::to_string(r.status) + ")";
        }
    }

    Session::Session(GameAdapter& game, Options options) : game_(game), opt_(std::move(options)) {}

    Session::~Session() { Shutdown(); }

    void Session::Shutdown() {
        StopWorker();
        link_.Stop();
    }

    bool Session::WantsLoginPanel() const {
        return link_.GetRole() != LocalLink::Role::Follower && state_ == State::LoggedOut;
    }

    std::string Session::NightName() const {
        return night_.is_object() ? night_.value("name", "") : "";
    }

    // ------------------------------------------------------------------ startup / roles

    void Session::Start() {
        if (opt_.use_storage) {
            Storage::SavedLogin saved;
            if (Storage::LoadLogin(saved)) {
                server_input_ = saved.server;
                group_input_ = saved.group_id;
                if (!saved.token.empty() && !Expired(saved.expires_at)) token_ = saved.token;
            }
        }
        BecomeRole(0);
    }

    void Session::BecomeRole(double now_s) {
        auto role = link_.Start(opt_.local_port);
        follower_instances_.clear();
        last_instances_.clear();
        slots_dirty_ = true;
        if (role == LocalLink::Role::Follower) {
            StopWorker();
            state_ = State::Follower;
            SendHello();
            return;
        }
        if (role == LocalLink::Role::None) {
            next_role_try_ = now_s + 2.0;
            if (state_ == State::Follower) state_ = State::LoggedOut;
            return;
        }
        // Hub: resume with the token we have (saved, or handed over by the old hub).
        if (!token_.empty() && ParseServer(server_input_, server_, error_)) {
            error_.clear();
            if (opt_.use_storage && profiles_.empty()) {
                json cached;
                if (Storage::LoadGroupCache(cached)) ApplyWelcome(cached);
            }
            state_ = State::Working;
            StartWorker(server_, token_, "");
        } else if (state_ == State::Follower) {
            state_ = profiles_.empty() ? State::LoggedOut : State::Offline;
        }
    }

    // ------------------------------------------------------------------ UI actions

    void Session::Login(const std::string& server, const std::string& group_id, const std::string& password, bool remember) {
        if (link_.GetRole() == LocalLink::Role::Follower) return;
        server_input_ = server;
        group_input_ = group_id;
        remember_ = remember;
        std::string err;
        if (!ParseServer(server, server_, err)) { error_ = err; return; }
        if (group_id.empty() || password.empty()) { error_ = "Enter the group ID and password"; return; }
        error_.clear();
        state_ = State::Working;
        json body{{"groupId", group_id}, {"password", password}, {"hostname", game_.InstanceId()}};
        StartWorker(server_, "", body.dump());
    }

    void Session::PlayOffline() {
        StopWorker();
        json cached;
        if (opt_.use_storage && Storage::LoadGroupCache(cached)) {
            ApplyWelcome(cached);
            error_.clear();
        } else if (profiles_.empty()) {
            error_ = "No saved group on this PC yet. Profiles appear after the first online login.";
        }
        state_ = State::Offline;
        if (link_.GetRole() == LocalLink::Role::Hub && welcome_.is_object()) link_.Broadcast(welcome_.dump());
    }

    void Session::Logout() {
        StopWorker();
        if (!token_.empty()) {
            // Best effort, off the render thread; the token also expires on its own.
            std::thread([server = server_, tok = token_]() { PostJson(server, "/api/hub/logout", "{}", tok, 3000); }).detach();
        }
        token_.clear();
        if (opt_.use_storage) {
            Storage::SavedLogin keep{server_input_, group_input_, "", ""};
            Storage::SaveLogin(keep);
        }
        state_ = State::LoggedOut;
        error_.clear();
    }

    // ------------------------------------------------------------------ worker thread

    void Session::Push(Event e) {
        std::lock_guard<std::mutex> lock(inbox_mutex_);
        inbox_.push_back(std::move(e));
    }

    void Session::StartWorker(const ServerAddr& server, const std::string& token, const std::string& login_body) {
        StopWorker();
        stopping_ = false;
        worker_ = std::thread([this, server, token, login_body]() {
            std::string tok = token;
            if (!login_body.empty()) {
                HttpResult r = PostJson(server, "/api/hub/login", login_body);
                if (r.status != 200) { Push({Event::LoginFailed, ErrorFrom(r), {}}); return; }
                json j;
                try { j = json::parse(r.body); } catch (...) { Push({Event::LoginFailed, "Unexpected reply from the server", {}}); return; }
                tok = j.value("token", "");
                Push({Event::LoginOk, "", j});
            }
            int backoff_ms = 1000;
            while (!stopping_) {
                auto ws = std::make_shared<WebSocket>();
                int http_status = 0;
                std::string err;
                if (ws->Connect(server, "/hub", tok, http_status, err)) {
                    { std::lock_guard<std::mutex> lock(ws_mutex_); ws_ = ws; }
                    Push({Event::Connected, "", {}});
                    backoff_ms = 1000;
                    std::string text;
                    while (!stopping_ && ws->Receive(text)) Push({Event::Frame, text, {}});
                    { std::lock_guard<std::mutex> lock(ws_mutex_); ws_.reset(); }
                    ws->Close();
                    if (stopping_) break;
                    Push({Event::Disconnected, "Connection lost, reconnecting…", {}});
                } else if (http_status == 401) {
                    Push({Event::AuthFailed, err, {}});
                    return;
                } else {
                    Push({Event::Disconnected, err, {}});
                }
                for (int waited = 0; waited < backoff_ms && !stopping_; waited += 100) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                backoff_ms = (std::min)(backoff_ms * 2, 30000);
            }
        });
    }

    void Session::StopWorker() {
        stopping_ = true;
        {
            std::lock_guard<std::mutex> lock(ws_mutex_);
            if (ws_) ws_->Close();
        }
        if (worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> lock(ws_mutex_);
        ws_.reset();
    }

    // ------------------------------------------------------------------ main-thread tick

    void Session::Tick(double now_s) {
        if (link_.GetRole() == LocalLink::Role::None && now_s >= next_role_try_) BecomeRole(now_s);
        PollLink(now_s);
        Drain(now_s);

        bool connected = link_.GetRole() == LocalLink::Role::Follower ||
                         (link_.GetRole() == LocalLink::Role::Hub && state_ == State::Online);
        if (!connected) return;

        SendInstancesIfChanged(false);

        if (link_.GetRole() == LocalLink::Role::Hub && now_s >= next_ping_) {
            next_ping_ = now_s + opt_.ping_interval_s;
            SendUp({{"type", "ping"}});
        }
        if (now_s >= next_stats_) {
            next_stats_ = now_s + opt_.stats_interval_s;
            json live = game_.LiveStats();
            if (live.is_object() && live.value("inGame", false)) {
                json msg = live;
                msg["type"] = "stats";
                msg["instanceId"] = game_.InstanceId();
                SendUp(msg);
            }
        }
    }

    void Session::Drain(double now_s) {
        std::deque<Event> events;
        {
            std::lock_guard<std::mutex> lock(inbox_mutex_);
            events.swap(inbox_);
        }
        for (auto& e : events) {
            switch (e.kind) {
                case Event::LoginOk: {
                    token_ = e.data.value("token", "");
                    if (e.data.contains("group")) group_name_ = e.data["group"].value("name", group_input_);
                    if (opt_.use_storage) {
                        Storage::SavedLogin s{server_.display, group_input_, remember_ ? token_ : "", remember_ ? e.data.value("expiresAt", "") : ""};
                        Storage::SaveLogin(s);
                    }
                    server_input_ = server_.display;
                    link_.Broadcast(json{{"type", "session"}, {"server", server_input_}, {"groupId", group_input_}, {"token", token_}}.dump());
                    break;
                }
                case Event::LoginFailed:
                    error_ = e.text;
                    state_ = State::LoggedOut;
                    break;
                case Event::Connected:
                    state_ = State::Online;
                    error_.clear();
                    next_ping_ = now_s + opt_.ping_interval_s;
                    SendHello();
                    break;
                case Event::Frame: {
                    json msg;
                    try { msg = json::parse(e.text); } catch (...) { break; }
                    HandleFrame(msg, true);
                    if (link_.GetRole() == LocalLink::Role::Hub) link_.Broadcast(e.text);
                    break;
                }
                case Event::Disconnected:
                    if (state_ != State::LoggedOut) {
                        state_ = State::Reconnecting;
                        error_ = e.text;
                    }
                    break;
                case Event::AuthFailed:
                    token_.clear();
                    if (opt_.use_storage) Storage::SaveLogin({server_input_, group_input_, "", ""});
                    state_ = State::LoggedOut;
                    error_ = "Your login for this PC expired. Enter the group password again.";
                    break;
            }
        }
    }

    // ------------------------------------------------------------------ local link

    void Session::PollLink(double now_s) {
        std::vector<LocalLink::Incoming> in;
        std::vector<int> joined, left;
        bool lost = false;
        link_.Poll(in, joined, left, lost);

        if (link_.GetRole() == LocalLink::Role::Hub) {
            for (int peer : joined) {
                if (!token_.empty())
                    link_.Send(peer, json{{"type", "session"}, {"server", server_input_}, {"groupId", group_input_}, {"token", token_}}.dump());
                if (welcome_.is_object()) link_.Send(peer, welcome_.dump());
            }
            for (int peer : left) follower_instances_.erase(peer);
            if (!left.empty()) slots_dirty_ = true;
        }

        for (auto& msg_in : in) {
            json msg;
            try { msg = json::parse(msg_in.line); } catch (...) { continue; }
            std::string type = msg.value("type", "");
            if (link_.GetRole() == LocalLink::Role::Hub) {
                if (type == "hello" || type == "instances") {
                    follower_instances_[msg_in.peer] = msg.contains("instances") ? msg["instances"] : json::array();
                    slots_dirty_ = true;
                } else if (type == "stats" || type == "matchEnd") {
                    if (state_ == State::Online) SendUp(msg);
                }
            } else {
                HandleFrame(msg, false);
            }
        }

        if (lost) BecomeRole(now_s);  // the hub went away: take over or follow the new one
    }

    void Session::SendUp(const json& msg) {
        std::string text = msg.dump();
        if (link_.GetRole() == LocalLink::Role::Follower) { link_.SendToHub(text); return; }
        std::shared_ptr<WebSocket> ws;
        { std::lock_guard<std::mutex> lock(ws_mutex_); ws = ws_; }
        if (ws) ws->Send(text);
    }

    json Session::MyInstance() {
        json slots = json::array();
        int n = std::clamp(game_.SlotCount(), 1, 4);
        for (int i = 0; i < n; i++) {
            std::string pid = game_.SlotProfile(i);
            slots.push_back({{"slot", i}, {"profileId", pid.empty() ? json(nullptr) : json(pid)}});
        }
        return {{"instanceId", game_.InstanceId()}, {"slots", slots}};
    }

    json Session::CombinedInstances() {
        json all = json::array({MyInstance()});
        for (auto& [peer, list] : follower_instances_) {
            if (list.is_array()) for (auto& inst : list) all.push_back(inst);
        }
        return all;
    }

    void Session::SendHello() {
        json hello{{"type", "hello"}, {"protocol", kProtocol}, {"hostname", game_.InstanceId()}, {"client", opt_.client}};
        hello["instances"] = link_.GetRole() == LocalLink::Role::Follower ? json::array({MyInstance()}) : CombinedInstances();
        last_instances_ = hello["instances"].dump();
        slots_dirty_ = false;
        SendUp(hello);
    }

    void Session::SendInstancesIfChanged(bool force) {
        if (!slots_dirty_ && !force) {
            // Cheap check each tick: slot selections can change from the overlay.
            json mine = link_.GetRole() == LocalLink::Role::Follower ? json::array({MyInstance()}) : CombinedInstances();
            if (mine.dump() == last_instances_) return;
        }
        json list = link_.GetRole() == LocalLink::Role::Follower ? json::array({MyInstance()}) : CombinedInstances();
        std::string text = list.dump();
        slots_dirty_ = false;
        if (text == last_instances_ && !force) return;
        last_instances_ = text;
        SendUp({{"type", "instances"}, {"instances", list}});
    }

    // ------------------------------------------------------------------ frames from the server

    void Session::HandleFrame(const json& msg, bool from_server) {
        std::string type = msg.value("type", "");
        if (type == "welcome") {
            ApplyWelcome(msg);
            if (opt_.use_storage && from_server) Storage::SaveGroupCache(msg);
        } else if (type == "profile" && msg.contains("profile")) {
            V5Profile p;
            std::string err;
            if (!ParseV5(msg["profile"], p, err)) return;
            auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const V5Profile& x) { return x.id == p.id; });
            if (it != profiles_.end()) *it = p; else profiles_.push_back(p);
            if (welcome_.is_object() && welcome_.contains("profiles")) {
                auto& arr = welcome_["profiles"];
                bool replaced = false;
                for (auto& x : arr) if (x.value("id", "") == p.id) { x = msg["profile"]; replaced = true; }
                if (!replaced) arr.push_back(msg["profile"]);
            }
            game_.UpdateProfile(p);
        } else if (type == "profileRemoved") {
            std::string id = msg.value("profileId", "");
            profiles_.erase(std::remove_if(profiles_.begin(), profiles_.end(), [&](const V5Profile& x) { return x.id == id; }), profiles_.end());
            game_.RemoveProfile(id);
        } else if (type == "night") {
            night_ = msg.contains("night") ? msg["night"] : json();
            if (welcome_.is_object()) welcome_["night"] = night_;
            if (night_.is_object() && night_.contains("teams")) ApplyTeamsFrom(night_["teams"]);
        } else if (type == "teams") {
            if (night_.is_object()) night_["teams"] = msg.value("teams", json::object());
            ApplyTeamsFrom(msg.value("teams", json::object()));
        } else if (type == "assign") {
            if (msg.value("instanceId", "") == game_.InstanceId()) {
                int slot = msg.value("slot", -1);
                if (slot >= 0 && slot < 4) {
                    game_.AssignSlot(slot, msg.value("profileId", ""));
                    slots_dirty_ = true;
                }
            }
        } else if (type == "session" && !from_server) {
            // From our hub: lets this instance take over if the hub closes.
            server_input_ = msg.value("server", server_input_);
            group_input_ = msg.value("groupId", group_input_);
            token_ = msg.value("token", "");
            std::string err;
            ParseServer(server_input_, server_, err);
        }
    }

    void Session::ApplyWelcome(const json& welcome) {
        welcome_ = welcome;
        welcome_["type"] = "welcome";
        if (welcome.contains("group") && welcome["group"].is_object()) group_name_ = welcome["group"].value("name", group_name_);
        night_ = welcome.contains("night") ? welcome["night"] : json();
        profiles_.clear();
        if (welcome.contains("profiles") && welcome["profiles"].is_array()) {
            for (const auto& j : welcome["profiles"]) {
                V5Profile p;
                std::string err;
                if (ParseV5(j, p, err)) profiles_.push_back(std::move(p));
            }
        }
        game_.SetGroupProfiles(profiles_);
        if (night_.is_object() && night_.contains("teams")) ApplyTeamsFrom(night_["teams"]);
    }

    void Session::ApplyTeamsFrom(const json& teams) {
        std::map<std::string, int> t;
        if (teams.is_object()) {
            for (auto& [pid, v] : teams.items()) if (v.is_number_integer()) t[pid] = std::clamp(v.get<int>(), 0, 7);
        }
        game_.ApplyTeams(t);
    }

    void Session::OnMatchEnd(const json& match) {
        bool connected = link_.GetRole() == LocalLink::Role::Follower ||
                         (link_.GetRole() == LocalLink::Role::Hub && state_ == State::Online);
        if (!connected) return;
        json msg = match;
        msg["type"] = "matchEnd";
        msg["instanceId"] = game_.InstanceId();
        SendUp(msg);
    }
}
