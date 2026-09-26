#include "InstanceConfig.h"
#include "common.h"
#include "../../global/Global.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace MCC::Splitscreen {

    bool LoadInstanceConfig() {
        std::string config_path = "./alpha_ring/instance_config.json";

        auto p_cfg = AlphaRing::Global::InstanceConfig();
        p_cfg->loaded = false;

        if (!fs::exists(config_path)) {
            LOG_INFO("[RINGCHIEF] No instance_config.json found - using manual profile selection");
            return false;
        }

        try {
            std::ifstream file(config_path);
            if (!file.is_open()) {
                LOG_WARNING("[RINGCHIEF] Could not open instance_config.json");
                return false;
            }

            json j = json::parse(file);

            // Version check (accept 1, 2, or 3)
            int version = j.value("version", 0);
            if (version < 1 || version > 3) {
                LOG_WARNING("[RINGCHIEF] instance_config.json has unsupported version {}", version);
                return false;
            }

            LOG_INFO("[RINGCHIEF] Splitscreen::LoadInstanceConfig: parsing version {}", version);

            // Instance info
            if (j.contains("instance")) {
                auto& inst = j["instance"];
                p_cfg->instance_id = inst.value("id", "unknown");
                p_cfg->launched_at = inst.value("launched_at", "");
            } else {
                p_cfg->instance_id = "unknown";
            }

            // V2: RingChief multi-player fields
            p_cfg->ringchief_enabled = j.value("ringchief_enabled", false);

            if (version >= 2 && j.contains("players") && j["players"].is_array()) {
                // V2 path: parse players array
                p_cfg->player_count = j.value("player_count", 1);
                auto& players_arr = j["players"];

                LOG_INFO("[RINGCHIEF] Parsing {} player entries for global config", players_arr.size());

                for (size_t i = 0; i < players_arr.size() && i < 4; i++) {
                    auto& p = players_arr[i];
                    p_cfg->players[i].profile_name = p.value("profile_name", "");
                    p_cfg->players[i].controller_guid = p.value("controller_guid", "");
                    p_cfg->players[i].controller_index = p.value("controller_index", static_cast<int>(i));
                    p_cfg->players[i].display_name = p.value("display_name", "Player " + std::to_string(i + 1));
                    p_cfg->players[i].is_primary = p.value("is_primary", false);

                    LOG_INFO("[RINGCHIEF]   Global slot {}: profile='{}', name='{}', ctrl={}",
                        i, p_cfg->players[i].profile_name,
                        p_cfg->players[i].display_name,
                        p_cfg->players[i].controller_index);
                }

                // Populate v1 fields from first player (backward compat)
                if (p_cfg->player_count > 0) {
                    p_cfg->profile_name = p_cfg->players[0].profile_name;
                    p_cfg->controller_guid = p_cfg->players[0].controller_guid;
                    p_cfg->controller_index = p_cfg->players[0].controller_index;
                    p_cfg->display_name = p_cfg->players[0].display_name;
                    p_cfg->is_primary = p_cfg->players[0].is_primary;
                }
            } else {
                // V1 path: single player block
                p_cfg->player_count = 1;

                if (!j.contains("player")) {
                    LOG_ERROR("[RINGCHIEF] instance_config.json v1 missing 'player' section");
                    return false;
                }
                auto& player = j["player"];
                p_cfg->profile_name = player.value("profile_name", "");
                p_cfg->controller_guid = player.value("controller_guid", "");
                p_cfg->controller_index = player.value("controller_index", 0);
                p_cfg->display_name = player.value("display_name", "Player");
                p_cfg->is_primary = player.value("is_primary", false);

                // Also populate players[0] for uniform access
                p_cfg->players[0].profile_name = p_cfg->profile_name;
                p_cfg->players[0].controller_guid = p_cfg->controller_guid;
                p_cfg->players[0].controller_index = p_cfg->controller_index;
                p_cfg->players[0].display_name = p_cfg->display_name;
                p_cfg->players[0].is_primary = p_cfg->is_primary;
            }

            // Screen info
            if (j.contains("screen")) {
                auto& screen = j["screen"];
                p_cfg->screen_position = screen.value("position", "fullscreen");
                p_cfg->monitor_index = screen.value("monitor_index", 0);
                if (screen.contains("bounds")) {
                    auto& bounds = screen["bounds"];
                    p_cfg->bounds_x = bounds.value("x", 0);
                    p_cfg->bounds_y = bounds.value("y", 0);
                    p_cfg->bounds_width = bounds.value("width", 0);
                    p_cfg->bounds_height = bounds.value("height", 0);
                }
            } else {
                p_cfg->screen_position = "fullscreen";
                p_cfg->monitor_index = 0;
            }

            // Session info
            if (j.contains("session")) {
                auto& session = j["session"];
                p_cfg->session_id = session.value("session_id", "");
                p_cfg->total_players = session.value("total_players", 1);
                p_cfg->player_index = session.value("player_index", 0);
            }

            // Server config (v3)
            if (j.contains("server")) {
                auto& server = j["server"];
                p_cfg->server.enabled = server.value("enabled", false);
                p_cfg->server.ip = server.value("ip", "127.0.0.1");
                p_cfg->server.port = server.value("port", 42069);
                p_cfg->server.auto_reconnect = server.value("auto_reconnect", true);
                p_cfg->server.heartbeat_interval_ms = server.value("heartbeat_interval_ms", 5000);
                p_cfg->server.stats_interval_ms = server.value("stats_interval_ms", 250);
            }

            p_cfg->loaded = true;

            LOG_INFO("[RINGCHIEF] === Loaded Instance Config (v{}) ===", version);
            LOG_INFO("[RINGCHIEF]   Instance: {}", p_cfg->instance_id);
            LOG_INFO("[RINGCHIEF]   RingChief enabled: {}", p_cfg->ringchief_enabled ? "yes" : "no");
            LOG_INFO("[RINGCHIEF]   Player count: {} (total session: {})",
                p_cfg->player_count, p_cfg->total_players);
            for (int i = 0; i < p_cfg->player_count && i < 4; i++) {
                LOG_INFO("[RINGCHIEF]   Slot {}: '{}' profile='{}' ctrl={} primary={}",
                    i, p_cfg->players[i].display_name, p_cfg->players[i].profile_name,
                    p_cfg->players[i].controller_index,
                    p_cfg->players[i].is_primary ? "yes" : "no");
            }
            LOG_INFO("[RINGCHIEF]   Screen: {} on monitor {}", p_cfg->screen_position, p_cfg->monitor_index);
            if (p_cfg->bounds_width > 0) {
                LOG_INFO("[RINGCHIEF]   Bounds: {}x{} at ({},{})",
                    p_cfg->bounds_width, p_cfg->bounds_height,
                    p_cfg->bounds_x, p_cfg->bounds_y);
            }
            LOG_INFO("[RINGCHIEF]   Session: {}", p_cfg->session_id);
            if (p_cfg->server.enabled) {
                LOG_INFO("[RINGCHIEF]   Server: {}:{} (heartbeat={}ms, stats={}ms)",
                    p_cfg->server.ip, p_cfg->server.port,
                    p_cfg->server.heartbeat_interval_ms,
                    p_cfg->server.stats_interval_ms);
            }
            LOG_INFO("[RINGCHIEF] ==============================");

            return true;

        } catch (const json::exception& e) {
            LOG_ERROR("[RINGCHIEF] Failed to parse instance_config.json: {}", e.what());
            return false;
        } catch (const std::exception& e) {
            LOG_ERROR("[RINGCHIEF] Error loading instance_config.json: {}", e.what());
            return false;
        }
    }

    const std::string& GetSessionId() {
        static std::string empty_string;
        auto p_cfg = AlphaRing::Global::InstanceConfig();
        return p_cfg->loaded ? p_cfg->session_id : empty_string;
    }

    bool ShouldAutoLoadProfile() {
        auto p_cfg = AlphaRing::Global::InstanceConfig();
        return p_cfg->loaded && !p_cfg->profile_name.empty();
    }

}
