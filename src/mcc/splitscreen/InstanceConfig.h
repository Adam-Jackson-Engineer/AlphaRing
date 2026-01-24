#pragma once

#include <string>

namespace MCC::Splitscreen {
    // Load instance configuration from ./alpha_ring/instance_config.json
    // Returns true if config was loaded successfully, false if missing or invalid
    // Missing config is not an error - it just means manual profile selection
    bool LoadInstanceConfig();

    // Get the current session ID (empty string if no config loaded)
    const std::string& GetSessionId();

    // Check if auto-load from config should be attempted
    bool ShouldAutoLoadProfile();
}
