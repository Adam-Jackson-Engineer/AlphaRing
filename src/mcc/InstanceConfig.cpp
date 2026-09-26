#include "InstanceConfig.h"
#include "common.h"
#include "filesystem/Filesystem.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace AlphaRing {

    static InstanceConfig s_instance_config;

    namespace Config {

        bool LoadInstanceConfig() {
            // Reset state
            s_instance_config = InstanceConfig();

            // Use DLL-relative path instead of CWD-relative
            // This ensures each Nucleus instance reads its own config
            std::string alphaRingDir = Filesystem::GetDllAlphaRingDir();
            std::string config_path = alphaRingDir + "/instance_config.json";

            LOG_INFO("[RINGCHIEF] InstanceConfig: Looking for config at: {}", config_path);

            if (!fs::exists(config_path)) {
                LOG_INFO("[RINGCHIEF] InstanceConfig: No instance_config.json found at {}, using manual mode", config_path);
                return false;
            }

            try {
                std::ifstream file(config_path);
                if (!file.is_open()) {
                    LOG_WARNING("[RINGCHIEF] InstanceConfig: Could not open {}", config_path);
                    return false;
                }

                json j = json::parse(file);

                // Check version (accept 1, 2, or 3)
                int version = j.value("version", 0);
                if (version < 1 || version > 3) {
                    LOG_WARNING("[RINGCHIEF] InstanceConfig: Unsupported config version {}", version);
                    return false;
                }

                LOG_INFO("[RINGCHIEF] InstanceConfig: Parsing version {} config", version);

                // Parse instance block
                if (j.contains("instance")) {
                    auto& inst = j["instance"];
                    s_instance_config.instance_id = inst.value("id", "");
                    s_instance_config.launched_at = inst.value("launched_at", "");
                }

                // V2: RingChief multi-player fields
                s_instance_config.ringchief_enabled = j.value("ringchief_enabled", false);

                if (version >= 2 && j.contains("players") && j["players"].is_array()) {
                    // V2 path: parse players array
                    s_instance_config.player_count = j.value("player_count", 1);
                    auto& players_arr = j["players"];

                    LOG_INFO("[RINGCHIEF] InstanceConfig: Parsing {} player entries", players_arr.size());

                    for (size_t i = 0; i < players_arr.size() && i < 4; i++) {
                        auto& p = players_arr[i];
                        PlayerConfig pc;
                        pc.slot = p.value("slot", static_cast<int>(i));
                        pc.profile_name = p.value("profile_name", "");
                        pc.controller_guid = p.value("controller_guid", "");
                        pc.controller_index = p.value("controller_index", static_cast<int>(i));
                        pc.display_name = p.value("display_name", "Player " + std::to_string(i + 1));
                        pc.is_primary = p.value("is_primary", false);
                        s_instance_config.players.push_back(pc);

                        LOG_INFO("[RINGCHIEF]   Player slot {}: profile='{}', name='{}', primary={}",
                            pc.slot, pc.profile_name, pc.display_name, pc.is_primary ? "yes" : "no");
                    }

                    // Populate v1 single-player fields from first player (backward compat)
                    if (!s_instance_config.players.empty()) {
                        auto& p0 = s_instance_config.players[0];
                        s_instance_config.profile_name = p0.profile_name;
                        s_instance_config.controller_guid = p0.controller_guid;
                        s_instance_config.controller_index = p0.controller_index;
                        s_instance_config.display_name = p0.display_name;
                        s_instance_config.is_primary = p0.is_primary;
                    }
                } else {
                    // V1 path: single player block
                    s_instance_config.player_count = 1;
                    if (j.contains("player")) {
                        auto& player = j["player"];
                        s_instance_config.profile_name = player.value("profile_name", "");
                        s_instance_config.controller_guid = player.value("controller_guid", "");
                        s_instance_config.controller_index = player.value("controller_index", 0);
                        s_instance_config.display_name = player.value("display_name", "");
                        s_instance_config.is_primary = player.value("is_primary", false);

                        // Also populate players vector for uniform access
                        PlayerConfig pc;
                        pc.slot = 0;
                        pc.profile_name = s_instance_config.profile_name;
                        pc.controller_guid = s_instance_config.controller_guid;
                        pc.controller_index = s_instance_config.controller_index;
                        pc.display_name = s_instance_config.display_name;
                        pc.is_primary = s_instance_config.is_primary;
                        s_instance_config.players.push_back(pc);
                    }
                }

                // Parse screen block
                if (j.contains("screen")) {
                    auto& screen = j["screen"];
                    s_instance_config.screen_position = screen.value("position", "");
                    s_instance_config.monitor_index = screen.value("monitor_index", 0);

                    if (screen.contains("bounds")) {
                        auto& bounds = screen["bounds"];
                        s_instance_config.bounds_x = bounds.value("x", 0);
                        s_instance_config.bounds_y = bounds.value("y", 0);
                        s_instance_config.bounds_width = bounds.value("width", 0);
                        s_instance_config.bounds_height = bounds.value("height", 0);
                    }
                }

                // Parse session block
                if (j.contains("session")) {
                    auto& session = j["session"];
                    s_instance_config.session_id = session.value("session_id", "");
                    s_instance_config.total_players = session.value("total_players", 1);
                    s_instance_config.player_index = session.value("player_index", 0);
                }

                // Parse server block (v3)
                if (j.contains("server")) {
                    auto& server = j["server"];
                    s_instance_config.server.enabled = server.value("enabled", false);
                    s_instance_config.server.ip = server.value("ip", "127.0.0.1");
                    s_instance_config.server.port = server.value("port", 42069);
                    s_instance_config.server.auto_reconnect = server.value("auto_reconnect", true);
                    s_instance_config.server.heartbeat_interval_ms = server.value("heartbeat_interval_ms", 5000);
                    s_instance_config.server.stats_interval_ms = server.value("stats_interval_ms", 250);
                }

                s_instance_config.loaded = true;

                LOG_INFO("[RINGCHIEF] InstanceConfig: Loaded successfully (v{})", version);
                LOG_INFO("[RINGCHIEF]   Instance ID: {}", s_instance_config.instance_id);
                LOG_INFO("[RINGCHIEF]   RingChief enabled: {}", s_instance_config.ringchief_enabled ? "yes" : "no");
                LOG_INFO("[RINGCHIEF]   Player count: {} (total session: {})",
                    s_instance_config.player_count, s_instance_config.total_players);
                for (size_t i = 0; i < s_instance_config.players.size(); i++) {
                    auto& pc = s_instance_config.players[i];
                    LOG_INFO("[RINGCHIEF]   Slot {}: '{}' profile='{}' ctrl={} primary={}",
                        pc.slot, pc.display_name, pc.profile_name,
                        pc.controller_index, pc.is_primary ? "yes" : "no");
                }
                LOG_INFO("[RINGCHIEF]   Screen: {} (monitor {})",
                    s_instance_config.screen_position, s_instance_config.monitor_index);
                LOG_INFO("[RINGCHIEF]   Session: {}", s_instance_config.session_id);
                if (s_instance_config.server.enabled) {
                    LOG_INFO("[RINGCHIEF]   Server: {}:{} (heartbeat={}ms, stats={}ms)",
                        s_instance_config.server.ip, s_instance_config.server.port,
                        s_instance_config.server.heartbeat_interval_ms,
                        s_instance_config.server.stats_interval_ms);
                }

                return true;

            } catch (const json::exception& e) {
                LOG_ERROR("[RINGCHIEF] InstanceConfig: JSON parse error: {}", e.what());
                s_instance_config = InstanceConfig();
                return false;
            } catch (const std::exception& e) {
                LOG_ERROR("[RINGCHIEF] InstanceConfig: Error loading config: {}", e.what());
                s_instance_config = InstanceConfig();
                return false;
            }
        }

        const InstanceConfig& GetInstanceConfig() {
            return s_instance_config;
        }

        bool ShouldAutoLoadProfile() {
            return s_instance_config.loaded && !s_instance_config.profile_name.empty();
        }

        const std::string& GetAutoLoadProfileName() {
            return s_instance_config.profile_name;
        }

    } // namespace Config
} // namespace AlphaRing
