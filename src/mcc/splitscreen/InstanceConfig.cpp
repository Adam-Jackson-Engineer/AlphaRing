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
            LOG_INFO("No instance_config.json found - using manual profile selection");
            return false;
        }

        try {
            std::ifstream file(config_path);
            if (!file.is_open()) {
                LOG_WARNING("Could not open instance_config.json");
                return false;
            }

            json j = json::parse(file);

            // Version check
            int version = j.value("version", 0);
            if (version != 1) {
                LOG_WARNING("instance_config.json has unsupported version {}", version);
                return false;
            }

            // Instance info
            if (j.contains("instance")) {
                auto& inst = j["instance"];
                p_cfg->instance_id = inst.value("id", "unknown");
                p_cfg->launched_at = inst.value("launched_at", "");
            } else {
                p_cfg->instance_id = "unknown";
            }

            // Player info (required section)
            if (!j.contains("player")) {
                LOG_ERROR("instance_config.json missing 'player' section");
                return false;
            }
            auto& player = j["player"];
            p_cfg->profile_name = player.value("profile_name", "");
            p_cfg->controller_guid = player.value("controller_guid", "");
            p_cfg->controller_index = player.value("controller_index", 0);
            p_cfg->display_name = player.value("display_name", "Player");
            p_cfg->is_primary = player.value("is_primary", false);

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

            // Session info (required section)
            if (!j.contains("session")) {
                LOG_ERROR("instance_config.json missing 'session' section");
                return false;
            }
            auto& session = j["session"];
            p_cfg->session_id = session.value("session_id", "");
            p_cfg->total_players = session.value("total_players", 1);
            p_cfg->player_index = session.value("player_index", 0);

            p_cfg->loaded = true;

            LOG_INFO("=== Loaded Instance Config ===");
            LOG_INFO("  Instance: {} (player {}/{})",
                p_cfg->instance_id, p_cfg->player_index + 1, p_cfg->total_players);
            LOG_INFO("  Profile: {}", p_cfg->profile_name);
            LOG_INFO("  Display name: {}", p_cfg->display_name);
            LOG_INFO("  Controller index: {}", p_cfg->controller_index);
            LOG_INFO("  Screen: {} on monitor {}", p_cfg->screen_position, p_cfg->monitor_index);
            if (p_cfg->bounds_width > 0) {
                LOG_INFO("  Bounds: {}x{} at ({},{})",
                    p_cfg->bounds_width, p_cfg->bounds_height,
                    p_cfg->bounds_x, p_cfg->bounds_y);
            }
            LOG_INFO("  Session: {}", p_cfg->session_id);
            LOG_INFO("  Primary: {}", p_cfg->is_primary ? "yes" : "no");
            LOG_INFO("==============================");

            return true;

        } catch (const json::exception& e) {
            LOG_ERROR("Failed to parse instance_config.json: {}", e.what());
            return false;
        } catch (const std::exception& e) {
            LOG_ERROR("Error loading instance_config.json: {}", e.what());
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
