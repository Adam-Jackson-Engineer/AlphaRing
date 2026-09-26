#pragma once

#include <string>

namespace AlphaRing {

    // Optional per-instance settings written by the Nucleus Co-op handler into
    // <MCC>\mcc\binaries\win64\alpha_ring\instance_config.json. Standalone play needs none.
    //
    // v4:
    //   { "version": 4,
    //     "player_count": 2,                       // local players in this instance (1-4)
    //     "ringchief": { "server": "halo.dronedude.app", "groupId": "WarGames2027" } }
    //
    // Older v2/v3 files (Feb 2026) still work for player_count; their per-slot profile
    // names are ignored (players now pick themselves in the overlay or on their phone).
    struct InstanceConfig {
        bool loaded = false;
        int version = 0;
        int player_count = 1;
        std::string ringchief_server;   // pre-fills the login box
        std::string ringchief_group;
    };

    namespace Config {
        bool LoadInstanceConfig();
        const InstanceConfig& GetInstanceConfig();
    }
}
