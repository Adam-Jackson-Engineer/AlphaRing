#include "DisplayBroadcaster.h"
#include "StatsPacket.h"

#include <chrono>
#include "log/Log.h"

#include "global/Global.h"
#include "mcc/mcc.h"
#include "mcc/CGameManager.h"
#include "mcc/CGameGlobal.h"
#include "mcc/splitscreen/ProfileManager.h"
#include "mcc/server/RingChiefClient.h"

namespace MCC {
namespace Display {

// Static state
static bool s_enabled = true;
static int s_broadcast_interval_ms = 250;
static std::string s_last_error;
static bool s_connected = false;

// Last broadcast time
static std::chrono::steady_clock::time_point s_last_broadcast;

// Get current timestamp in milliseconds
static int64_t GetTimestampMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Get game title from current game enum
static std::string GetGameTitle() {
    auto* pGameGlobal = GameGlobal();
    if (!pGameGlobal) return "Unknown";

    switch (pGameGlobal->current_game) {
        case CGameGlobal::Halo1: return "Halo CE";
        case CGameGlobal::Halo2: return "Halo 2";
        case CGameGlobal::Halo3: return "Halo 3";
        case CGameGlobal::Halo4: return "Halo 4";
        case CGameGlobal::GroundHog: return "Halo 2 Anniversary";
        case CGameGlobal::Halo3ODST: return "Halo 3 ODST";
        case CGameGlobal::HaloReach: return "Halo Reach";
        default: return "Unknown";
    }
}

// Convert wide string to UTF-8
static std::string WideToUtf8(const wchar_t* wide) {
    if (!wide || wide[0] == 0) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], size, nullptr, nullptr);
    return result;
}

// Build stats packet from current game state
static StatsPacket BuildPacket() {
    StatsPacket packet;
    packet.version = 2;
    packet.timestamp_ms = GetTimestampMs();

    // Get instance config for session/instance IDs
    // ALWAYS append PID to instance_id to ensure uniqueness across multiple instances,
    // even when Nucleus Coop provides the same instance_id in config files
    auto* instanceConfig = AlphaRing::Global::InstanceConfig();
    std::string pid_suffix = "-" + std::to_string(GetCurrentProcessId());
    if (instanceConfig && instanceConfig->loaded) {
        packet.session_id = instanceConfig->session_id;
        packet.instance_id = instanceConfig->instance_id + pid_suffix;
    } else {
        packet.session_id = "local";
        packet.instance_id = "local" + pid_suffix;
    }

    packet.game_title = GetGameTitle();
    packet.game_mode_name = "Team Slayer";  // TODO: detect actual game mode
    packet.game_mode = 2;  // TeamSlayer
    packet.in_game = MCC::IsInGame();
    packet.is_team_game = true;  // TODO: detect from game mode
    packet.match_epoch = Splitscreen::ProfileManager::GetMatchEpoch();

    // Get player stats from all active slots
    auto* p_setting = AlphaRing::Global::MCC::Splitscreen();
    int player_count = p_setting ? p_setting->player_count : 1;

    for (int i = 0; i < player_count && i < 4; i++) {
        auto* p_slot = CGameManager::get_profile(i);
        if (!p_slot) continue;

        PlayerStats player;
        player.slot = i;

        // Get the persistent profile for this slot (if available)
        Splitscreen::PersistentProfile* profile = nullptr;
        const std::string& profileKey = Splitscreen::ProfileManager::selected_profile_key[i];
        if (!profileKey.empty()) {
            profile = Splitscreen::ProfileManager::GetProfileByKey(profileKey);
        }

        // Player name
        if (p_slot->name[0] != 0) {
            player.name = WideToUtf8(p_slot->name);
        } else if (profile && !profile->display_name.empty()) {
            player.name = WideToUtf8(profile->display_name.c_str());
        } else {
            player.name = "Player " + std::to_string(i + 1);
        }

        // Service tag
        if (profile && profile->service_tag[0] != 0) {
            player.service_tag = WideToUtf8(profile->service_tag);
        }

        // Team assignment - use ProfileManager's current_team (UI state) as primary source
        // This reflects the actual team selection in the UI for each slot
        int team_index = Splitscreen::ProfileManager::current_team[i];

        // If pending team is set and differs, use pending (game state)
        if (p_slot->team_pending) {
            team_index = p_slot->pending_team;
        }

        // Map team index to team name (supports all 8 teams)
        switch (team_index) {
            case 0: player.team = "Red"; break;
            case 1: player.team = "Blue"; break;
            case 2: player.team = "Green"; break;
            case 3: player.team = "Orange"; break;
            case 4: player.team = "Purple"; break;
            case 5: player.team = "Gold"; break;
            case 6: player.team = "Brown"; break;
            case 7: player.team = "Pink"; break;
            default: player.team = "FFA"; break;
        }

        // Current match stats
        player.kills = p_slot->stats_kills;
        player.deaths = p_slot->stats_deaths;
        player.assists = p_slot->stats_assists;
        player.score = p_slot->stats_score;

        // Rank info from profile
        if (profile) {
            player.rank_level = profile->rank_level;
            player.rank_name = Splitscreen::ProfileManager::GetRankName(profile->rank_level);
            player.rank_xp = profile->rank_xp;

            // Emblem data
            player.emblem.foreground = profile->emblem.foreground;
            player.emblem.background = profile->emblem.background;
            player.emblem.primary_color = profile->emblem.primary_color;
            player.emblem.secondary_color = profile->emblem.secondary_color;
            player.emblem.tertiary_color = profile->emblem.background_color;

            // Nameplate color (use emblem primary as nameplate)
            player.nameplate_color = profile->emblem.primary_color;

            // Career stats
            player.career_kills = profile->career_stats.total_kills;
            player.career_deaths = profile->career_stats.total_deaths;
            player.career_assists = profile->career_stats.total_assists;
            player.career_games = profile->career_stats.games_played;
        } else {
            // Defaults if no profile
            player.rank_level = 1;
            player.rank_name = "Recruit";
            player.rank_xp = 0;

            // Default emblem colors based on slot
            player.emblem.foreground = i;
            player.emblem.background = 0;
            player.emblem.primary_color = (i % 2 == 0) ? 1 : 2;  // Red or Blue
            player.emblem.secondary_color = 0;
            player.emblem.tertiary_color = 10;  // Black background
            player.nameplate_color = player.emblem.primary_color;
        }

        packet.players.push_back(player);
    }

    return packet;
}

// Send packet to named pipe
static bool SendPacket(const StatsPacket& packet) {
    std::string json = packet.toJson() + "\n";

    HANDLE pipe = CreateFileA(
        PIPE_NAME,
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );

    if (pipe == INVALID_HANDLE_VALUE) {
        DWORD error = ::GetLastError();
        if (error == ERROR_PIPE_BUSY) {
            // Try waiting briefly
            if (!WaitNamedPipeA(PIPE_NAME, PIPE_TIMEOUT_MS)) {
                s_last_error = "Pipe busy";
                s_connected = false;
                return false;
            }
            pipe = CreateFileA(PIPE_NAME, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        }

        if (pipe == INVALID_HANDLE_VALUE) {
            s_last_error = "Display not running";
            s_connected = false;
            return false;
        }
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    DWORD bytesWritten = 0;
    BOOL success = WriteFile(
        pipe,
        json.c_str(),
        static_cast<DWORD>(json.size()),
        &bytesWritten,
        nullptr
    );

    CloseHandle(pipe);

    if (!success) {
        s_last_error = "Write failed: " + std::to_string(::GetLastError());
        s_connected = false;
        return false;
    }

    s_connected = true;
    s_last_error.clear();
    return true;
}

bool Initialize() {
    s_enabled = true;
    s_broadcast_interval_ms = 250;
    s_last_broadcast = std::chrono::steady_clock::now();
    s_connected = false;
    s_last_error.clear();

    LOG_INFO("[DISPLAY] DisplayBroadcaster initialized");
    return true;
}

void Shutdown() {
    s_enabled = false;
    s_connected = false;
    LOG_INFO("[DISPLAY] DisplayBroadcaster shutdown");
}

void BroadcastStats() {
    if (!s_enabled) return;

    // Skip named pipe broadcast if TCP is connected (TCP handles stats updates)
    // This prevents duplicate players appearing in the display
    if (Server::Client::IsConnected()) {
        return;
    }

    // Check if enough time has passed since last broadcast
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_broadcast).count();

    if (elapsed < s_broadcast_interval_ms) {
        return;
    }

    s_last_broadcast = now;

    // Build and send packet (only when TCP is not connected)
    StatsPacket packet = BuildPacket();
    SendPacket(packet);
}

void SetEnabled(bool enabled) {
    s_enabled = enabled;
    if (!enabled) {
        s_connected = false;
    }
}

bool IsEnabled() {
    return s_enabled;
}

void SetBroadcastInterval(int ms) {
    if (ms >= 50 && ms <= 5000) {
        s_broadcast_interval_ms = ms;
    }
}

int GetBroadcastInterval() {
    return s_broadcast_interval_ms;
}

const std::string& GetLastError() {
    return s_last_error;
}

bool IsConnected() {
    return s_connected;
}

} // namespace Display
} // namespace MCC
