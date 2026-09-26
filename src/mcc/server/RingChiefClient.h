#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

#include "ServerProtocol.h"

namespace MCC {
namespace Server {

// Connection state
enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting
};

// Pending command from server
struct PendingCommand {
    std::string type;
    std::string command_id;
    nlohmann::json payload;
};

class RingChiefClient {
public:
    RingChiefClient();
    ~RingChiefClient();

    // Initialize client with server address (call once at startup)
    bool Initialize(const std::string& server_ip, int port);

    // Shutdown client and close connections
    void Shutdown();

    // Update called from main thread (processes pending commands)
    void Update();

    // Connection management
    void Connect();
    void Disconnect();
    bool IsConnected() const { return m_state == ConnectionState::Connected; }
    ConnectionState GetState() const { return m_state; }

    // Configuration
    void SetAutoReconnect(bool enabled) { m_auto_reconnect = enabled; }
    void SetHeartbeatInterval(int ms) { m_heartbeat_interval_ms = ms; }
    void SetStatsInterval(int ms) { m_stats_interval_ms = ms; }

    // Get status info
    const std::string& GetLastError() const { return m_last_error; }
    const std::string& GetServerName() const { return m_server_name; }

private:
    // Network thread function
    void NetworkThread();

    // Message sending (thread-safe)
    void QueueMessage(const Message& msg);
    void SendRegistration();
    void SendStatsUpdate();
    void SendHeartbeat();
    void SendAck(const std::string& command_id, bool success, const std::string& error = "");

    // Message handling (called on network thread)
    void HandleMessage(const Message& msg);
    void HandleWelcome(const WelcomePayload& payload);
    void HandleSetTeams(const SetTeamsPayload& payload);
    void HandleSetPlayerTeam(const SetPlayerTeamPayload& payload);
    void HandleSetEnabled(const SetEnabledPayload& payload);
    void HandleSetPlayerSettings(const SetPlayerSettingsPayload& payload);
    void HandleServerConfig(const ServerConfigPayload& payload);
    void HandlePing(const PingPayload& payload);

    // Process commands on main thread
    void ProcessPendingCommands();

    // Helper to get hostname
    static std::string GetHostname();

    // Socket operations
    bool ConnectSocket();
    void CloseSocket();
    bool SendData(const std::string& data);
    bool ReceiveData();

private:
    // Connection settings
    std::string m_server_ip;
    int m_server_port = DEFAULT_PORT;
    bool m_auto_reconnect = true;
    int m_heartbeat_interval_ms = DEFAULT_HEARTBEAT_INTERVAL_MS;
    int m_stats_interval_ms = DEFAULT_STATS_INTERVAL_MS;

    // State
    std::atomic<ConnectionState> m_state{ConnectionState::Disconnected};
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_shutdown_requested{false};
    std::atomic<bool> m_registration_pending{false};  // Set by network thread, cleared by main thread
    std::string m_last_error;
    std::string m_server_name;

    // Socket
    uintptr_t m_socket = ~0ULL;  // INVALID_SOCKET

    // Threading
    std::thread m_network_thread;
    std::mutex m_send_mutex;
    std::mutex m_command_mutex;
    std::queue<std::string> m_send_queue;
    std::queue<PendingCommand> m_pending_commands;

    // Receive buffer
    std::string m_recv_buffer;

    // Timing
    std::chrono::steady_clock::time_point m_last_heartbeat;
    std::chrono::steady_clock::time_point m_last_stats;
    std::chrono::steady_clock::time_point m_last_reconnect_attempt;
    int m_reconnect_delay_ms = 1000;
};

// Global client instance access
namespace Client {
    bool Initialize();
    void Shutdown();
    void Update();
    RingChiefClient* Get();
    bool IsConnected();
}

} // namespace Server
} // namespace MCC
