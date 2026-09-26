#include "InstanceConfig.h"
#include "common.h"
#include "filesystem/Filesystem.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace AlphaRing {

    static InstanceConfig s_instance_config;

    namespace Config {

        bool LoadInstanceConfig() {
            s_instance_config = InstanceConfig();

            // DLL-relative so each Nucleus instance reads its own copy.
            std::string path = Filesystem::GetDllAlphaRingDir() + "/instance_config.json";
            if (!fs::exists(path)) {
                LOG_INFO("[RINGCHIEF] No instance_config.json (standalone play)");
                return false;
            }

            try {
                std::ifstream file(path);
                json j = json::parse(file);
                InstanceConfig cfg;
                cfg.version = j.value("version", 1);
                cfg.player_count = std::clamp(j.value("player_count", 1), 1, 4);
                if (j.contains("ringchief") && j["ringchief"].is_object()) {
                    cfg.ringchief_server = j["ringchief"].value("server", "");
                    cfg.ringchief_group = j["ringchief"].value("groupId", "");
                }
                cfg.loaded = true;
                s_instance_config = cfg;
                LOG_INFO("[RINGCHIEF] instance_config v{}: player_count={} group='{}'",
                         cfg.version, cfg.player_count, cfg.ringchief_group);
                return true;
            } catch (const std::exception& e) {
                LOG_ERROR("[RINGCHIEF] instance_config.json unreadable: {}", e.what());
                return false;
            }
        }

        const InstanceConfig& GetInstanceConfig() {
            return s_instance_config;
        }
    }
}
