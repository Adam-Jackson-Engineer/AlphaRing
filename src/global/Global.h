#pragma once

#include <string>

#define DefGlobal(name) \
    struct name##_t;    \
    extern name##_t s_##name; \
    inline name##_t* name() {return &s_##name;} \
    struct name##_t

#define ImplGlobal(name) \
    name##_t s_##name;

namespace AlphaRing::Global {
    DefGlobal(Global) {
        bool wireframe;
        bool wireframe_model;
        bool wireframe_structure;
        bool show_imgui = true;
        // on menu
        bool show_imgui_mouse = true;
        bool pause_game_on_menu_shown = true;
        bool disable_input_on_menu_shown = true;
    };

    namespace Halo3 {
        DefGlobal(Physics) {
            bool enable_bump_possession;
        };

        DefGlobal(Render) {
            bool model;
            bool structure;
        };
    }

    namespace MCC {
        DefGlobal(Splitscreen) {
            bool b_override;
            int player_count = 4;

            // player 0
            bool b_player0_use_km = false;  // Default OFF - user must enable manually
            bool b_override_profile = false;
            bool b_use_player0_profile = true;
        };

        DefGlobal(Display) {
            bool enabled = true;
            int broadcast_interval_ms = 250;  // 4Hz update rate
        };
    }

    // Instance configuration from Nucleus Co-op (via instance_config.json)
    // Populated on startup if config file exists
    DefGlobal(InstanceConfig) {
        bool loaded = false;

        // Instance info
        std::string instance_id;
        std::string launched_at;

        // V1 single-player info (backward compat; v2 populates from players[0])
        std::string profile_name;      // e.g., "Spartan_Blue.json"
        std::string controller_guid;   // Windows device GUID
        int controller_index = 0;      // 0-3, XInput slot
        std::string display_name;      // Human-readable name
        bool is_primary = false;       // Is this the "host" instance?

        // Screen info
        std::string screen_position;   // "top-left", "fullscreen", etc.
        int monitor_index = 0;
        int bounds_x = 0;
        int bounds_y = 0;
        int bounds_width = 0;
        int bounds_height = 0;

        // Session info
        std::string session_id;        // Shared across all instances
        int total_players = 1;
        int player_index = 0;          // 0-based index

        // V2: RingChief multi-player support
        bool ringchief_enabled = false;
        int player_count = 1;          // Players in THIS instance
        struct PlayerSlot {
            std::string profile_name;
            std::string controller_guid;
            int controller_index = 0;
            std::string display_name;
            bool is_primary = false;
        } players[4];                  // Fixed 4 slots (max per instance)

        // V3: Ring Chief Server connection
        struct ServerConfig {
            bool enabled = false;
            std::string ip = "127.0.0.1";
            int port = 42069;
            bool auto_reconnect = true;
            int heartbeat_interval_ms = 5000;
            int stats_interval_ms = 250;
        } server;
    };
}