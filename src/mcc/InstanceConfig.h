#pragma once

#include <string>
#include <vector>

namespace AlphaRing {

    // Per-player configuration (v2 multi-player support)
    struct PlayerConfig {
        int slot = 0;                      // Local slot index (0-3)
        std::string profile_name;          // Profile filename to auto-load
        std::string controller_guid;       // Windows controller GUID
        int controller_index = 0;          // XInput slot
        std::string display_name;          // Human-readable name
        bool is_primary = false;           // True if this is the host player
    };

    // Server connection configuration (v3)
    struct ServerConfig {
        bool enabled = false;              // Whether to connect to Ring Chief server
        std::string ip = "127.0.0.1";      // Server IP address
        int port = 42069;                  // Server port
        bool auto_reconnect = true;        // Auto-reconnect on disconnect
        int heartbeat_interval_ms = 5000;  // Heartbeat interval
        int stats_interval_ms = 250;       // Stats update interval
    };

    // Configuration passed from Nucleus Co-op to identify this instance
    struct InstanceConfig {
        // Instance identification
        std::string instance_id;
        std::string launched_at;

        // V1 single-player fields (backward compat; v2 populates from players[0])
        std::string profile_name;      // Filename of profile to auto-load
        std::string controller_guid;   // Windows controller GUID
        int controller_index = 0;      // 0-3 controller slot
        std::string display_name;      // Human-readable name
        bool is_primary = false;       // True if this is the host instance

        // Screen position
        std::string screen_position;   // "top-left", "top-right", etc.
        int monitor_index = 0;
        int bounds_x = 0;
        int bounds_y = 0;
        int bounds_width = 0;
        int bounds_height = 0;

        // Session tracking
        std::string session_id;
        int total_players = 1;
        int player_index = 0;          // 0-based index in session

        // V2: RingChief multi-player support
        bool ringchief_enabled = false;    // True if RingChief wrote this config
        int player_count = 1;              // Number of players in THIS instance
        std::vector<PlayerConfig> players; // Per-player config (populated for v2)

        // V3: Ring Chief Server connection
        ServerConfig server;

        // Load status
        bool loaded = false;
    };

    namespace Config {
        // Load instance config from alpha_ring/instance_config.json
        // Returns true if config was loaded successfully
        // Returns false if file doesn't exist or is invalid (graceful fallback)
        bool LoadInstanceConfig();

        // Get the loaded config (check .loaded field to see if valid)
        const InstanceConfig& GetInstanceConfig();

        // Check if we should auto-load a profile based on config
        bool ShouldAutoLoadProfile();

        // Get the profile filename to auto-load (empty if none)
        const std::string& GetAutoLoadProfileName();
    }
}
