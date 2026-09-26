#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace MCC {
namespace Server {

// Protocol version
constexpr int PROTOCOL_VERSION = 1;

// Default server configuration
constexpr int DEFAULT_PORT = 42069;
constexpr int DEFAULT_HEARTBEAT_INTERVAL_MS = 5000;
constexpr int DEFAULT_STATS_INTERVAL_MS = 250;
constexpr int CONNECTION_TIMEOUT_MS = 15000;

// Message types for the TCP protocol
namespace MessageType {
    // Client -> Server
    constexpr const char* REGISTER = "REGISTER";
    constexpr const char* STATS_UPDATE = "STATS_UPDATE";
    constexpr const char* HEARTBEAT = "HEARTBEAT";
    constexpr const char* ACK = "ACK";

    // Server -> Client
    constexpr const char* SET_TEAMS = "SET_TEAMS";
    constexpr const char* SET_PLAYER_TEAM = "SET_PLAYER_TEAM";
    constexpr const char* SET_ENABLED = "SET_ENABLED";  // Enable/disable AlphaRing
    constexpr const char* SET_PLAYER_SETTINGS = "SET_PLAYER_SETTINGS";
    constexpr const char* SERVER_CONFIG = "SERVER_CONFIG";
    constexpr const char* PING = "PING";
    constexpr const char* WELCOME = "WELCOME";
}

// Team enum (matches ProfileManager::Team)
enum class Team {
    Red = 0,
    Blue = 1,
    Green = 2,
    Orange = 3,
    Purple = 4,
    Gold = 5,
    Brown = 6,
    Pink = 7
};

inline const char* TeamToString(Team team) {
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

inline Team StringToTeam(const std::string& str) {
    if (str == "Red") return Team::Red;
    if (str == "Blue") return Team::Blue;
    if (str == "Green") return Team::Green;
    if (str == "Orange") return Team::Orange;
    if (str == "Purple") return Team::Purple;
    if (str == "Gold") return Team::Gold;
    if (str == "Brown") return Team::Brown;
    if (str == "Pink") return Team::Pink;
    return Team::Red;
}

// Player info for registration and updates
struct PlayerInfo {
    int slot = 0;
    std::string name;
    std::string service_tag;
    std::string team;
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int score = 0;

    // Controller settings (included in STATS_UPDATE for STATE_BROADCAST visibility)
    int controller_preset = -1;
    std::vector<int> button_mappings;  // 66 ints (empty unless Custom preset)
    int vibration_disabled = -1;
    int crouch_lock_enabled = -1;
    int horizontal_look_sensitivity = -1;
    int vertical_look_sensitivity = -1;
    int look_acceleration = -1;
    float look_axial_dead_zone = -1.0f;
    float look_radial_dead_zone = -1.0f;
    int look_controls_inverted = -1;
    int aircraft_controls_inverted = -1;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlayerInfo,
        slot, name, service_tag, team, kills, deaths, assists, score,
        controller_preset, button_mappings, vibration_disabled, crouch_lock_enabled,
        horizontal_look_sensitivity, vertical_look_sensitivity, look_acceleration,
        look_axial_dead_zone, look_radial_dead_zone,
        look_controls_inverted, aircraft_controls_inverted)
};

// Controller settings for SET_PLAYER_SETTINGS (sentinel -1 = don't change)
struct PlayerSettingsData {
    int controller_preset = -1;         // 0-6, -1=don't change
    std::vector<int> button_mappings;   // 66 ints (empty=don't change)
    int vibration_disabled = -1;        // 0=ON, 1=OFF
    int crouch_lock_enabled = -1;       // 0=hold, 1=toggle
    int horizontal_look_sensitivity = -1;  // 1-10
    int vertical_look_sensitivity = -1;    // 1-10
    int look_acceleration = -1;            // 1-5
    float look_axial_dead_zone = -1.0f;
    float look_radial_dead_zone = -1.0f;
    int look_controls_inverted = -1;    // 0=normal, 1=inverted
    int aircraft_controls_inverted = -1;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlayerSettingsData,
        controller_preset, button_mappings, vibration_disabled, crouch_lock_enabled,
        horizontal_look_sensitivity, vertical_look_sensitivity, look_acceleration,
        look_axial_dead_zone, look_radial_dead_zone,
        look_controls_inverted, aircraft_controls_inverted)
};

// SET_PLAYER_SETTINGS payload (Server -> Client)
struct SetPlayerSettingsPayload {
    std::string command_id;
    int slot = 0;
    PlayerSettingsData settings;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SetPlayerSettingsPayload,
        command_id, slot, settings)
};

// Team assignment for a single player
struct TeamAssignment {
    int slot = 0;
    std::string team;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TeamAssignment, slot, team)
};

// ============================================================================
// Message Payloads
// ============================================================================

// REGISTER payload (Client -> Server)
struct RegisterPayload {
    std::string instance_id;
    std::string hostname;
    std::string session_id;
    int protocol_version = PROTOCOL_VERSION;
    std::vector<PlayerInfo> players;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RegisterPayload,
        instance_id, hostname, session_id, protocol_version, players)
};

// STATS_UPDATE payload (Client -> Server)
struct StatsUpdatePayload {
    std::string instance_id;
    std::string game_title;
    std::string game_mode_name;
    bool in_game = false;
    bool is_team_game = true;
    int match_epoch = 0;
    std::vector<PlayerInfo> players;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(StatsUpdatePayload,
        instance_id, game_title, game_mode_name, in_game, is_team_game, match_epoch, players)
};

// HEARTBEAT payload (Client -> Server)
struct HeartbeatPayload {
    std::string instance_id;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(HeartbeatPayload, instance_id)
};

// ACK payload (Client -> Server)
struct AckPayload {
    std::string command_id;
    bool success = true;
    std::string error_message;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AckPayload, command_id, success, error_message)
};

// SET_TEAMS payload (Server -> Client)
struct SetTeamsPayload {
    std::string command_id;
    std::vector<TeamAssignment> assignments;
    bool apply_immediately = true;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SetTeamsPayload,
        command_id, assignments, apply_immediately)
};

// SET_PLAYER_TEAM payload (Server -> Client)
struct SetPlayerTeamPayload {
    std::string command_id;
    int slot = 0;
    std::string team;
    bool apply_immediately = true;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SetPlayerTeamPayload,
        command_id, slot, team, apply_immediately)
};

// SET_ENABLED payload (Server -> Client) - Enable/disable AlphaRing
struct SetEnabledPayload {
    std::string command_id;
    bool enabled = true;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SetEnabledPayload, command_id, enabled)
};

// SERVER_CONFIG payload (Server -> Client)
struct ServerConfigPayload {
    int heartbeat_interval_ms = DEFAULT_HEARTBEAT_INTERVAL_MS;
    int stats_interval_ms = DEFAULT_STATS_INTERVAL_MS;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ServerConfigPayload,
        heartbeat_interval_ms, stats_interval_ms)
};

// WELCOME payload (Server -> Client)
struct WelcomePayload {
    int protocol_version = PROTOCOL_VERSION;
    std::string server_name;
    ServerConfigPayload config;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(WelcomePayload,
        protocol_version, server_name, config)
};

// PING payload (Server -> Client)
struct PingPayload {
    int64_t server_time_ms = 0;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PingPayload, server_time_ms)
};

// ============================================================================
// Message Envelope
// ============================================================================

struct Message {
    std::string type;
    int64_t timestamp_ms = 0;
    nlohmann::json payload;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Message, type, timestamp_ms, payload)

    // Serialize to JSON string with newline delimiter
    std::string toJson() const {
        nlohmann::json j = *this;
        return j.dump() + "\n";
    }

    // Parse from JSON string
    static Message fromJson(const std::string& jsonStr) {
        auto j = nlohmann::json::parse(jsonStr);
        return j.get<Message>();
    }

    // Helper to get payload as specific type
    template<typename T>
    T getPayload() const {
        return payload.get<T>();
    }

    // Helper to create message with specific payload
    template<typename T>
    static Message create(const char* msgType, const T& payload) {
        Message msg;
        msg.type = msgType;
        msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        msg.payload = payload;
        return msg;
    }
};

} // namespace Server
} // namespace MCC
