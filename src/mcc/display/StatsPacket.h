#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace MCC {
namespace Display {

// Emblem configuration for visual display
struct EmblemData {
    int foreground = 0;      // Foreground shape (0-127)
    int background = 0;      // Background shape (0-31)
    int primary_color = 0;   // Primary color index (0-31)
    int secondary_color = 1; // Secondary color index
    int tertiary_color = 2;  // Tertiary color index

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(EmblemData,
        foreground, background, primary_color, secondary_color, tertiary_color)
};

// Player stats for a single player
struct PlayerStats {
    int slot = 0;
    std::string name;
    std::string service_tag;  // 4-character tag
    std::string team;         // Empty for FFA
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int score = 0;

    // Objective stats (CTF, etc.)
    int flag_captures = 0;
    int flag_returns = 0;
    int objective_score = 0;

    // Rank info
    int rank_level = 1;       // 1-50
    std::string rank_name;    // "Recruit", "Private", etc.
    int rank_xp = 0;

    // Visual customization
    EmblemData emblem;
    int nameplate_color = 0;

    // Career stats
    int career_kills = 0;
    int career_deaths = 0;
    int career_assists = 0;
    int career_games = 0;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlayerStats,
        slot, name, service_tag, team, kills, deaths, assists, score,
        flag_captures, flag_returns, objective_score,
        rank_level, rank_name, rank_xp, emblem, nameplate_color,
        career_kills, career_deaths, career_assists, career_games)
};

// Complete stats packet sent to display application
struct StatsPacket {
    int version = 3;
    int64_t timestamp_ms = 0;
    std::string session_id;
    std::string instance_id;
    std::string game_title;      // "Halo Reach", "Halo 3", etc.
    std::string game_mode_name;  // "Team Slayer", "CTF", etc.
    int game_mode = 0;           // Game mode enum value
    bool in_game = false;
    bool is_team_game = true;    // True for team-based modes
    int match_epoch = 0;
    std::vector<PlayerStats> players;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(StatsPacket,
        version, timestamp_ms, session_id, instance_id, game_title,
        game_mode_name, game_mode, in_game, is_team_game, match_epoch, players)

    // Serialize to JSON string
    std::string toJson() const {
        nlohmann::json j = *this;
        return j.dump();
    }
};

// Named pipe configuration
constexpr const char* PIPE_NAME = "\\\\.\\pipe\\RingChiefDisplay";
constexpr int PIPE_BUFFER_SIZE = 65536;
constexpr int PIPE_TIMEOUT_MS = 100;

} // namespace Display
} // namespace MCC
