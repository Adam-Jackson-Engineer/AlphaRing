#include "RingChiefClient.h"

// Prevent Windows min/max macros from conflicting with std::min/max
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <algorithm>
#include <fstream>

#include "log/Log.h"
#include "filesystem/Filesystem.h"
#include "mcc/mcc.h"
#include "mcc/CGameManager.h"
#include "mcc/CGameGlobal.h"
#include "mcc/CGameEngine.h"
#include "mcc/InstanceConfig.h"
#include "mcc/splitscreen/ProfileManager.h"
#include "global/Global.h"

#pragma comment(lib, "ws2_32.lib")

namespace MCC {
namespace Server {

// Global client instance
static RingChiefClient* s_client = nullptr;
static bool s_wsa_initialized = false;

// Convert wide string to UTF-8
static std::string WideToUtf8(const wchar_t* wide) {
    if (!wide || wide[0] == 0) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], size, nullptr, nullptr);
    return result;
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

RingChiefClient::RingChiefClient() {
    m_state = ConnectionState::Disconnected;
}

RingChiefClient::~RingChiefClient() {
    Shutdown();
}

bool RingChiefClient::Initialize(const std::string& server_ip, int port) {
    if (m_initialized) {
        LOG_WARNING("[SERVER] Client already initialized");
        return true;
    }

    m_server_ip = server_ip;
    m_server_port = port;
    m_shutdown_requested = false;
    m_state = ConnectionState::Disconnected;

    // Initialize timing
    auto now = std::chrono::steady_clock::now();
    m_last_heartbeat = now;
    m_last_stats = now;
    m_last_reconnect_attempt = now;

    m_initialized = true;
    LOG_INFO("[SERVER] RingChiefClient initialized for {}:{}", server_ip, port);

    // Start network thread
    m_network_thread = std::thread(&RingChiefClient::NetworkThread, this);

    return true;
}

void RingChiefClient::Shutdown() {
    if (!m_initialized) return;

    LOG_INFO("[SERVER] Shutting down RingChiefClient");

    m_shutdown_requested = true;
    m_initialized = false;

    // Close socket to unblock any recv calls
    CloseSocket();

    // Wait for network thread
    if (m_network_thread.joinable()) {
        m_network_thread.join();
    }

    m_state = ConnectionState::Disconnected;
    LOG_INFO("[SERVER] RingChiefClient shutdown complete");
}

void RingChiefClient::Update() {
    if (!m_initialized) return;

    // Process commands from server (must happen on main thread)
    ProcessPendingCommands();

    // Check if we need to send registration (after connection established)
    // This must happen on main thread to safely access game state
    if (m_registration_pending.exchange(false)) {
        SendRegistration();
    }

    // Check for stats update (must happen on main thread to safely access game state)
    if (m_state == ConnectionState::Connected) {
        auto now = std::chrono::steady_clock::now();
        auto stats_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_last_stats).count();
        if (stats_elapsed >= m_stats_interval_ms) {
            SendStatsUpdate();
            m_last_stats = now;
        }
    }
}

void RingChiefClient::Connect() {
    if (m_state == ConnectionState::Connected || m_state == ConnectionState::Connecting) {
        return;
    }
    m_state = ConnectionState::Connecting;
}

void RingChiefClient::Disconnect() {
    m_auto_reconnect = false;
    CloseSocket();
    m_state = ConnectionState::Disconnected;
}

void RingChiefClient::NetworkThread() {
    LOG_INFO("[SERVER] Network thread started");

    // Debug file for network thread
    std::string debugPath = AlphaRing::Filesystem::GetDllAlphaRingDir() + "/tcp_network_debug.txt";
    std::ofstream netDebug(debugPath);
    if (netDebug.is_open()) {
        netDebug << "=== Network Thread Started ===" << std::endl;
        netDebug << "PID: " << GetCurrentProcessId() << std::endl;
        netDebug << "Server IP: " << m_server_ip << std::endl;
        netDebug << "Server Port: " << m_server_port << std::endl;
    }

    int attempt_count = 0;

    while (!m_shutdown_requested) {
        auto now = std::chrono::steady_clock::now();

        // Handle all states that require a connection attempt
        if (m_state == ConnectionState::Disconnected ||
            m_state == ConnectionState::Reconnecting ||
            m_state == ConnectionState::Connecting) {
            // Check if we should attempt to connect/reconnect
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_last_reconnect_attempt).count();

            // For Connecting state, attempt immediately (no delay needed)
            bool should_attempt = (m_state == ConnectionState::Connecting) ||
                                  (elapsed >= m_reconnect_delay_ms);

            if (should_attempt) {
                m_last_reconnect_attempt = now;
                attempt_count++;

                if (netDebug.is_open()) {
                    netDebug << "Attempt #" << attempt_count << " - State: " << static_cast<int>(m_state.load()) << std::endl;
                    netDebug.flush();
                }

                if (ConnectSocket()) {
                    m_state = ConnectionState::Connected;
                    m_reconnect_delay_ms = 1000;  // Reset delay on success
                    m_registration_pending = true;  // Main thread will send registration
                    LOG_INFO("[SERVER] Connected to Ring Chief server");
                    if (netDebug.is_open()) {
                        netDebug << "*** CONNECTED SUCCESSFULLY ***" << std::endl;
                        netDebug.flush();
                    }
                } else {
                    if (netDebug.is_open()) {
                        netDebug << "Connection FAILED: " << m_last_error << std::endl;
                        netDebug.flush();
                    }
                    if (m_auto_reconnect) {
                        m_state = ConnectionState::Reconnecting;
                        // Exponential backoff up to 30 seconds
                        m_reconnect_delay_ms = std::min(m_reconnect_delay_ms * 2, 30000);
                        LOG_DEBUG("[SERVER] Connection failed, retry in {}ms", m_reconnect_delay_ms);
                    } else {
                        m_state = ConnectionState::Disconnected;
                    }
                }
            }

            // Sleep a bit before checking again
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (m_state == ConnectionState::Connected) {
            // Send pending messages
            {
                std::lock_guard<std::mutex> lock(m_send_mutex);
                while (!m_send_queue.empty()) {
                    std::string data = m_send_queue.front();
                    m_send_queue.pop();

                    if (!SendData(data)) {
                        LOG_WARNING("[SERVER] Failed to send data, disconnecting");
                        CloseSocket();
                        m_state = m_auto_reconnect ? ConnectionState::Reconnecting : ConnectionState::Disconnected;
                        break;
                    }
                }
            }

            // Check for heartbeat
            auto heartbeat_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_last_heartbeat).count();
            if (heartbeat_elapsed >= m_heartbeat_interval_ms) {
                SendHeartbeat();
                m_last_heartbeat = now;
            }

            // Note: Stats update is now called from Update() on main thread
            // to avoid data races when accessing game state

            // Receive data (non-blocking)
            if (!ReceiveData()) {
                // Connection lost
                LOG_WARNING("[SERVER] Connection lost");
                CloseSocket();
                m_state = m_auto_reconnect ? ConnectionState::Reconnecting : ConnectionState::Disconnected;
                continue;
            }
        }

        // Small sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CloseSocket();
    LOG_INFO("[SERVER] Network thread exiting");
}

bool RingChiefClient::ConnectSocket() {
    // Debug logging
    std::string debugPath = AlphaRing::Filesystem::GetDllAlphaRingDir() + "/tcp_connect_debug.txt";
    std::ofstream connDebug(debugPath, std::ios::app);
    if (connDebug.is_open()) {
        connDebug << "--- ConnectSocket() called ---" << std::endl;
        connDebug << "Target: " << m_server_ip << ":" << m_server_port << std::endl;
    }

    // Create socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        m_last_error = "Failed to create socket: " + std::to_string(WSAGetLastError());
        LOG_ERROR("[SERVER] {}", m_last_error);
        if (connDebug.is_open()) connDebug << "FAILED: " << m_last_error << std::endl;
        return false;
    }
    if (connDebug.is_open()) connDebug << "Socket created: " << sock << std::endl;

    // Set non-blocking mode
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
    if (connDebug.is_open()) connDebug << "Set non-blocking mode" << std::endl;

    // Connect
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<u_short>(m_server_port));
    inet_pton(AF_INET, m_server_ip.c_str(), &server_addr.sin_addr);

    if (connDebug.is_open()) connDebug << "Calling connect()..." << std::endl;
    int result = connect(sock, (sockaddr*)&server_addr, sizeof(server_addr));
    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (connDebug.is_open()) connDebug << "connect() returned error: " << err << std::endl;
        if (err != WSAEWOULDBLOCK) {
            m_last_error = "Connect failed: " + std::to_string(err);
            if (connDebug.is_open()) connDebug << "FAILED (not WOULDBLOCK): " << m_last_error << std::endl;
            closesocket(sock);
            return false;
        }

        // Wait for connection with timeout
        if (connDebug.is_open()) connDebug << "Waiting for connection (select with 5s timeout)..." << std::endl;
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(sock, &write_set);

        timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        result = select(0, nullptr, &write_set, nullptr, &timeout);
        if (connDebug.is_open()) connDebug << "select() returned: " << result << std::endl;

        if (result <= 0) {
            m_last_error = "Connection timeout (select returned " + std::to_string(result) + ")";
            if (connDebug.is_open()) connDebug << "FAILED: " << m_last_error << std::endl;
            closesocket(sock);
            return false;
        }

        // Check SO_ERROR to verify connection succeeded
        // This is the authoritative check on Windows
        int sockError = 0;
        int sockErrorLen = sizeof(sockError);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&sockError, &sockErrorLen);

        if (connDebug.is_open()) {
            connDebug << "write_set=" << (FD_ISSET(sock, &write_set) ? "YES" : "NO")
                      << ", SO_ERROR=" << sockError << std::endl;
        }

        if (sockError != 0) {
            m_last_error = "Connection failed: SO_ERROR=" + std::to_string(sockError);
            if (connDebug.is_open()) connDebug << "FAILED: " << m_last_error << std::endl;
            closesocket(sock);
            return false;
        }

        if (connDebug.is_open()) connDebug << "Connection verified via SO_ERROR=0" << std::endl;
    }

    // Set back to blocking mode for simplicity
    mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);
    if (connDebug.is_open()) connDebug << "Set back to blocking mode" << std::endl;

    // Set receive timeout
    DWORD recv_timeout = 100;  // 100ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recv_timeout, sizeof(recv_timeout));

    m_socket = static_cast<uintptr_t>(sock);
    m_last_error.clear();
    if (connDebug.is_open()) {
        connDebug << "*** CONNECTION SUCCESSFUL ***" << std::endl;
        connDebug << "Socket stored: " << m_socket << std::endl;
        connDebug.close();
    }
    return true;
}

void RingChiefClient::CloseSocket() {
    if (m_socket != ~0ULL) {
        closesocket(static_cast<SOCKET>(m_socket));
        m_socket = ~0ULL;
    }
}

bool RingChiefClient::SendData(const std::string& data) {
    if (m_socket == ~0ULL) return false;

    SOCKET sock = static_cast<SOCKET>(m_socket);
    int total_sent = 0;
    int remaining = static_cast<int>(data.size());

    while (remaining > 0) {
        int sent = send(sock, data.c_str() + total_sent, remaining, 0);
        if (sent == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                // Wait a bit and retry
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            m_last_error = "Send failed: " + std::to_string(err);
            return false;
        }
        total_sent += sent;
        remaining -= sent;
    }

    return true;
}

bool RingChiefClient::ReceiveData() {
    if (m_socket == ~0ULL) return false;

    SOCKET sock = static_cast<SOCKET>(m_socket);
    char buffer[4096];

    int received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (received == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
            // No data available, that's fine
            return true;
        }
        m_last_error = "Recv failed: " + std::to_string(err);
        return false;
    }
    if (received == 0) {
        // Connection closed
        m_last_error = "Connection closed by server";
        return false;
    }

    buffer[received] = '\0';
    m_recv_buffer += buffer;

    // Process complete messages (newline delimited)
    size_t pos;
    while ((pos = m_recv_buffer.find('\n')) != std::string::npos) {
        std::string line = m_recv_buffer.substr(0, pos);
        m_recv_buffer.erase(0, pos + 1);

        if (line.empty()) continue;

        try {
            Message msg = Message::fromJson(line);
            HandleMessage(msg);
        } catch (const std::exception& e) {
            LOG_WARNING("[SERVER] Failed to parse message: {}", e.what());
        }
    }

    return true;
}

void RingChiefClient::QueueMessage(const Message& msg) {
    std::lock_guard<std::mutex> lock(m_send_mutex);
    m_send_queue.push(msg.toJson());
}

void RingChiefClient::SendRegistration() {
    RegisterPayload payload;

    const auto& config = AlphaRing::Config::GetInstanceConfig();
    if (config.loaded) {
        payload.instance_id = config.instance_id;
        payload.session_id = config.session_id;
    } else {
        payload.instance_id = "local-" + std::to_string(GetCurrentProcessId());
        payload.session_id = "local";
    }

    payload.hostname = GetHostname();
    payload.protocol_version = PROTOCOL_VERSION;

    // Gather player info
    auto* p_setting = AlphaRing::Global::MCC::Splitscreen();
    int player_count = p_setting ? p_setting->player_count : 1;

    for (int i = 0; i < player_count && i < 4; i++) {
        auto* p_slot = CGameManager::get_profile(i);
        if (!p_slot) continue;

        PlayerInfo player;
        player.slot = i;

        // Get profile for this slot
        Splitscreen::PersistentProfile* profile = nullptr;
        const std::string& profileKey = Splitscreen::ProfileManager::selected_profile_key[i];
        if (!profileKey.empty()) {
            profile = Splitscreen::ProfileManager::GetProfileByKey(profileKey);
        }

        if (p_slot->name[0] != 0) {
            player.name = WideToUtf8(p_slot->name);
        } else if (profile && !profile->display_name.empty()) {
            player.name = WideToUtf8(profile->display_name.c_str());
        } else {
            player.name = "Player " + std::to_string(i + 1);
        }

        if (profile && profile->service_tag[0] != 0) {
            player.service_tag = WideToUtf8(profile->service_tag);
        }

        // Team - use ProfileManager's current_team (UI state)
        int team_index = Splitscreen::ProfileManager::current_team[i];
        if (p_slot->team_pending) {
            team_index = p_slot->pending_team;
        }
        player.team = TeamToString(static_cast<Team>(team_index));

        payload.players.push_back(player);
    }

    Message msg = Message::create(MessageType::REGISTER, payload);
    QueueMessage(msg);

    LOG_INFO("[SERVER] Sent registration (instance={}, {} players)",
        payload.instance_id, payload.players.size());
}

void RingChiefClient::SendStatsUpdate() {
    StatsUpdatePayload payload;

    const auto& config = AlphaRing::Config::GetInstanceConfig();
    if (config.loaded) {
        payload.instance_id = config.instance_id;
    } else {
        payload.instance_id = "local-" + std::to_string(GetCurrentProcessId());
    }

    payload.game_title = GetGameTitle();
    payload.game_mode_name = "Team Slayer";
    payload.in_game = MCC::IsInGame();
    payload.is_team_game = true;
    payload.match_epoch = Splitscreen::ProfileManager::GetMatchEpoch();

    // Gather player stats
    auto* p_setting = AlphaRing::Global::MCC::Splitscreen();
    int player_count = p_setting ? p_setting->player_count : 1;

    for (int i = 0; i < player_count && i < 4; i++) {
        auto* p_slot = CGameManager::get_profile(i);
        if (!p_slot) continue;

        PlayerInfo player;
        player.slot = i;

        Splitscreen::PersistentProfile* profile = nullptr;
        const std::string& profileKey = Splitscreen::ProfileManager::selected_profile_key[i];
        if (!profileKey.empty()) {
            profile = Splitscreen::ProfileManager::GetProfileByKey(profileKey);
        }

        if (p_slot->name[0] != 0) {
            player.name = WideToUtf8(p_slot->name);
        } else if (profile && !profile->display_name.empty()) {
            player.name = WideToUtf8(profile->display_name.c_str());
        } else {
            player.name = "Player " + std::to_string(i + 1);
        }

        if (profile && profile->service_tag[0] != 0) {
            player.service_tag = WideToUtf8(profile->service_tag);
        }

        // Team - use ProfileManager's current_team (UI state) as primary source
        int team_index = Splitscreen::ProfileManager::current_team[i];
        if (p_slot->team_pending) {
            team_index = p_slot->pending_team;
        }
        player.team = TeamToString(static_cast<Team>(team_index));

        // Stats
        player.kills = p_slot->stats_kills;
        player.deaths = p_slot->stats_deaths;
        player.assists = p_slot->stats_assists;
        player.score = p_slot->stats_score;

        // Controller settings
        player.controller_preset = Splitscreen::ProfileManager::current_preset[i];
        player.vibration_disabled = p_slot->profile.VibrationDisabled ? 1 : 0;
        player.crouch_lock_enabled = p_slot->profile.CrouchLockEnabled ? 1 : 0;
        player.horizontal_look_sensitivity = p_slot->profile.HorizontalLookSensitivity;
        player.vertical_look_sensitivity = p_slot->profile.VerticalLookSensitivity;
        player.look_acceleration = p_slot->profile.LookAcceleration;
        player.look_axial_dead_zone = p_slot->profile.LookAxialDeadZone;
        player.look_radial_dead_zone = p_slot->profile.LookRadialDeadZone;
        player.look_controls_inverted = p_slot->profile.LookControlsInverted ? 1 : 0;
        player.aircraft_controls_inverted = p_slot->profile.AircraftControlsInverted ? 1 : 0;

        // Button mappings only for Custom preset (saves bandwidth)
        if (player.controller_preset == static_cast<int>(Splitscreen::ControllerPreset::Custom)) {
            player.button_mappings.resize(66);
            for (int a = 0; a < 66; a++) {
                player.button_mappings[a] = static_cast<int>(p_slot->mapping.actions[a]);
            }
        }

        payload.players.push_back(player);
    }

    Message msg = Message::create(MessageType::STATS_UPDATE, payload);
    QueueMessage(msg);
}

void RingChiefClient::SendHeartbeat() {
    HeartbeatPayload payload;

    const auto& config = AlphaRing::Config::GetInstanceConfig();
    if (config.loaded) {
        payload.instance_id = config.instance_id;
    } else {
        payload.instance_id = "local-" + std::to_string(GetCurrentProcessId());
    }

    Message msg = Message::create(MessageType::HEARTBEAT, payload);
    QueueMessage(msg);
}

void RingChiefClient::SendAck(const std::string& command_id, bool success, const std::string& error) {
    AckPayload payload;
    payload.command_id = command_id;
    payload.success = success;
    payload.error_message = error;

    Message msg = Message::create(MessageType::ACK, payload);
    QueueMessage(msg);
}

void RingChiefClient::HandleMessage(const Message& msg) {
    LOG_DEBUG("[SERVER] Received message type: {}", msg.type);

    if (msg.type == MessageType::WELCOME) {
        HandleWelcome(msg.getPayload<WelcomePayload>());
    }
    else if (msg.type == MessageType::SET_TEAMS) {
        HandleSetTeams(msg.getPayload<SetTeamsPayload>());
    }
    else if (msg.type == MessageType::SET_PLAYER_TEAM) {
        HandleSetPlayerTeam(msg.getPayload<SetPlayerTeamPayload>());
    }
    else if (msg.type == MessageType::SET_ENABLED) {
        HandleSetEnabled(msg.getPayload<SetEnabledPayload>());
    }
    else if (msg.type == MessageType::SET_PLAYER_SETTINGS) {
        HandleSetPlayerSettings(msg.getPayload<SetPlayerSettingsPayload>());
    }
    else if (msg.type == MessageType::SERVER_CONFIG) {
        HandleServerConfig(msg.getPayload<ServerConfigPayload>());
    }
    else if (msg.type == MessageType::PING) {
        HandlePing(msg.getPayload<PingPayload>());
    }
    else {
        LOG_WARNING("[SERVER] Unknown message type: {}", msg.type);
    }
}

void RingChiefClient::HandleWelcome(const WelcomePayload& payload) {
    LOG_INFO("[SERVER] Received welcome from server '{}' (protocol v{})",
        payload.server_name, payload.protocol_version);

    m_server_name = payload.server_name;

    // Apply server config
    HandleServerConfig(payload.config);
}

void RingChiefClient::HandleSetTeams(const SetTeamsPayload& payload) {
    LOG_INFO("[SERVER] Received SET_TEAMS command ({} assignments)",
        payload.assignments.size());

    // Queue for main thread processing
    PendingCommand cmd;
    cmd.type = MessageType::SET_TEAMS;
    cmd.command_id = payload.command_id;
    cmd.payload = payload;

    {
        std::lock_guard<std::mutex> lock(m_command_mutex);
        m_pending_commands.push(cmd);
    }
}

void RingChiefClient::HandleSetPlayerTeam(const SetPlayerTeamPayload& payload) {
    LOG_INFO("[SERVER] Received SET_PLAYER_TEAM command (slot={}, team={})",
        payload.slot, payload.team);

    // Queue for main thread processing
    PendingCommand cmd;
    cmd.type = MessageType::SET_PLAYER_TEAM;
    cmd.command_id = payload.command_id;
    cmd.payload = payload;

    {
        std::lock_guard<std::mutex> lock(m_command_mutex);
        m_pending_commands.push(cmd);
    }
}

void RingChiefClient::HandleSetPlayerSettings(const SetPlayerSettingsPayload& payload) {
    LOG_INFO("[SERVER] Received SET_PLAYER_SETTINGS command (slot={})", payload.slot);

    PendingCommand cmd;
    cmd.type = MessageType::SET_PLAYER_SETTINGS;
    cmd.command_id = payload.command_id;
    cmd.payload = payload;

    {
        std::lock_guard<std::mutex> lock(m_command_mutex);
        m_pending_commands.push(cmd);
    }
}

void RingChiefClient::HandleServerConfig(const ServerConfigPayload& payload) {
    LOG_INFO("[SERVER] Received config: heartbeat={}ms, stats={}ms",
        payload.heartbeat_interval_ms, payload.stats_interval_ms);

    m_heartbeat_interval_ms = payload.heartbeat_interval_ms;
    m_stats_interval_ms = payload.stats_interval_ms;
}

void RingChiefClient::HandlePing(const PingPayload& payload) {
    // Respond with heartbeat
    SendHeartbeat();
}

void RingChiefClient::HandleSetEnabled(const SetEnabledPayload& payload) {
    LOG_INFO("[SERVER] Received SET_ENABLED command (enabled={})", payload.enabled);

    // Directly set the override flag - this is safe as it's a simple bool
    auto* p_setting = AlphaRing::Global::MCC::Splitscreen();
    if (p_setting) {
        p_setting->b_override = payload.enabled;
        LOG_INFO("[SERVER] AlphaRing splitscreen now {}", payload.enabled ? "ENABLED" : "DISABLED");
    }

    // Send acknowledgment
    if (!payload.command_id.empty()) {
        SendAck(payload.command_id, true);
    }
}

void RingChiefClient::ProcessPendingCommands() {
    std::queue<PendingCommand> commands;

    {
        std::lock_guard<std::mutex> lock(m_command_mutex);
        std::swap(commands, m_pending_commands);
    }

    while (!commands.empty()) {
        PendingCommand cmd = commands.front();
        commands.pop();

        bool success = true;
        std::string error;

        try {
            if (cmd.type == MessageType::SET_TEAMS) {
                SetTeamsPayload payload = cmd.payload.get<SetTeamsPayload>();

                for (const auto& assignment : payload.assignments) {
                    Team team = StringToTeam(assignment.team);
                    int slot = assignment.slot;

                    if (slot >= 0 && slot < 4) {
                        LOG_INFO("[SERVER] Setting slot {} to team {}", slot, assignment.team);

                        // Get the profile for this slot
                        const std::string& profileKey = Splitscreen::ProfileManager::selected_profile_key[slot];
                        Splitscreen::PersistentProfile* profile = nullptr;
                        if (!profileKey.empty()) {
                            profile = Splitscreen::ProfileManager::GetProfileByKey(profileKey);
                        }

                        // Update profile's team preference and save to disk
                        if (profile) {
                            profile->team_preference = static_cast<Splitscreen::Team>(static_cast<int>(team));
                            Splitscreen::ProfileManager::SaveProfile(*profile, profile->filename);
                            LOG_INFO("[SERVER]   Saved team {} to profile {}", assignment.team, profile->filename);
                        }

                        // Update UI state
                        Splitscreen::ProfileManager::current_team[slot] = static_cast<int>(team);

                        // Apply team change in game
                        Splitscreen::Team spTeam = static_cast<Splitscreen::Team>(static_cast<int>(team));
                        Splitscreen::ChangePlayerTeam(slot, spTeam);

                        // Clear dirty flag since we just saved
                        Splitscreen::ProfileManager::ClearDirty(slot);
                    }
                }

                if (payload.apply_immediately) {
                    Splitscreen::ProfileManager::ApplyTeamsNow("server_command");
                }
            }
            else if (cmd.type == MessageType::SET_PLAYER_TEAM) {
                SetPlayerTeamPayload payload = cmd.payload.get<SetPlayerTeamPayload>();

                Team team = StringToTeam(payload.team);
                int slot = payload.slot;

                if (slot >= 0 && slot < 4) {
                    LOG_INFO("[SERVER] Setting slot {} to team {}", slot, payload.team);

                    // Get the profile for this slot
                    const std::string& profileKey = Splitscreen::ProfileManager::selected_profile_key[slot];
                    Splitscreen::PersistentProfile* profile = nullptr;
                    if (!profileKey.empty()) {
                        profile = Splitscreen::ProfileManager::GetProfileByKey(profileKey);
                    }

                    // Update profile's team preference and save to disk
                    if (profile) {
                        profile->team_preference = static_cast<Splitscreen::Team>(static_cast<int>(team));
                        Splitscreen::ProfileManager::SaveProfile(*profile, profile->filename);
                        LOG_INFO("[SERVER]   Saved team {} to profile {}", payload.team, profile->filename);
                    }

                    // Update UI state
                    Splitscreen::ProfileManager::current_team[slot] = static_cast<int>(team);

                    // Apply team change in game
                    Splitscreen::Team spTeam = static_cast<Splitscreen::Team>(static_cast<int>(team));
                    Splitscreen::ChangePlayerTeam(slot, spTeam);

                    // Clear dirty flag since we just saved
                    Splitscreen::ProfileManager::ClearDirty(slot);

                    if (payload.apply_immediately) {
                        Splitscreen::ProfileManager::ApplyTeamsNow("server_command_single");
                    }
                } else {
                    success = false;
                    error = "Invalid slot index";
                }
            }
            else if (cmd.type == MessageType::SET_PLAYER_SETTINGS) {
                SetPlayerSettingsPayload payload = cmd.payload.get<SetPlayerSettingsPayload>();
                int slot = payload.slot;
                const auto& settings = payload.settings;

                if (slot >= 0 && slot < 4) {
                    auto* p_slot = CGameManager::get_profile(slot);
                    if (!p_slot) {
                        success = false;
                        error = "No profile in slot";
                    } else {
                        LOG_INFO("[SERVER] Applying settings to slot {}", slot);

                        // Apply controller preset if specified
                        if (settings.controller_preset >= 0 && settings.controller_preset <= 6) {
                            auto preset = static_cast<Splitscreen::ControllerPreset>(settings.controller_preset);
                            if (preset != Splitscreen::ControllerPreset::Custom) {
                                Splitscreen::ApplyControllerPreset(p_slot->mapping, preset);
                            }
                            Splitscreen::ProfileManager::current_preset[slot] = settings.controller_preset;
                            LOG_INFO("[SERVER]   Preset -> {}", settings.controller_preset);
                        }

                        // Apply custom button mappings if provided
                        if (!settings.button_mappings.empty() && settings.button_mappings.size() == 66) {
                            for (int a = 0; a < 66; a++) {
                                int btn = settings.button_mappings[a];
                                if (btn >= 0 && btn <= 15) {
                                    p_slot->mapping.actions[a] = static_cast<CGamepadMapping::eButton>(btn);
                                }
                            }
                            Splitscreen::ProfileManager::current_preset[slot] = static_cast<int>(Splitscreen::ControllerPreset::Custom);
                            LOG_INFO("[SERVER]   Applied custom button mappings");
                        }

                        // Apply auxiliary settings (only if not sentinel -1)
                        if (settings.vibration_disabled >= 0)
                            p_slot->profile.VibrationDisabled = (settings.vibration_disabled != 0);
                        if (settings.crouch_lock_enabled >= 0)
                            p_slot->profile.CrouchLockEnabled = (settings.crouch_lock_enabled != 0);
                        if (settings.horizontal_look_sensitivity >= 1 && settings.horizontal_look_sensitivity <= 10)
                            p_slot->profile.HorizontalLookSensitivity = static_cast<uint8_t>(settings.horizontal_look_sensitivity);
                        if (settings.vertical_look_sensitivity >= 1 && settings.vertical_look_sensitivity <= 10)
                            p_slot->profile.VerticalLookSensitivity = static_cast<uint8_t>(settings.vertical_look_sensitivity);
                        if (settings.look_acceleration >= 1 && settings.look_acceleration <= 5)
                            p_slot->profile.LookAcceleration = static_cast<uint8_t>(settings.look_acceleration);
                        if (settings.look_axial_dead_zone >= 0.0f && settings.look_axial_dead_zone <= 1.0f)
                            p_slot->profile.LookAxialDeadZone = settings.look_axial_dead_zone;
                        if (settings.look_radial_dead_zone >= 0.0f && settings.look_radial_dead_zone <= 1.0f)
                            p_slot->profile.LookRadialDeadZone = settings.look_radial_dead_zone;
                        if (settings.look_controls_inverted >= 0)
                            p_slot->profile.LookControlsInverted = (settings.look_controls_inverted != 0);
                        if (settings.aircraft_controls_inverted >= 0)
                            p_slot->profile.AircraftControlsInverted = (settings.aircraft_controls_inverted != 0);

                        // Reload settings in-game
                        auto p_engine = GameEngine();
                        if (MCC::IsInGame() && p_engine) {
                            p_engine->load_setting();
                            LOG_INFO("[SERVER]   Called load_setting()");
                        }

                        // Save to persistent profile
                        const std::string& profileKey = Splitscreen::ProfileManager::selected_profile_key[slot];
                        Splitscreen::PersistentProfile* profile = nullptr;
                        if (!profileKey.empty()) {
                            profile = Splitscreen::ProfileManager::GetProfileByKey(profileKey);
                        }
                        if (profile) {
                            profile->user_profile = p_slot->profile;
                            profile->gamepad_mapping = p_slot->mapping;
                            profile->controller_preset = static_cast<Splitscreen::ControllerPreset>(
                                Splitscreen::ProfileManager::current_preset[slot]);
                            Splitscreen::ProfileManager::SaveProfile(*profile, profile->filename);
                            Splitscreen::ProfileManager::ClearDirty(slot);
                            LOG_INFO("[SERVER]   Saved settings to profile {}", profile->filename);
                        }
                    }
                } else {
                    success = false;
                    error = "Invalid slot index";
                }
            }
        }
        catch (const std::exception& e) {
            success = false;
            error = e.what();
            LOG_ERROR("[SERVER] Error processing command: {}", e.what());
        }

        // Send acknowledgment
        if (!cmd.command_id.empty()) {
            SendAck(cmd.command_id, success, error);
        }
    }
}

std::string RingChiefClient::GetHostname() {
    char hostname[256] = {};
    gethostname(hostname, sizeof(hostname));
    return std::string(hostname);
}

// ============================================================================
// Global client functions
// ============================================================================

namespace Client {

bool Initialize() {
    // Debug logging to file
    std::string debugPath = AlphaRing::Filesystem::GetDllAlphaRingDir() + "/tcp_client_debug.txt";
    std::ofstream debugFile(debugPath);
    if (debugFile.is_open()) {
        debugFile << "=== TCP Client Initialize ===" << std::endl;
        debugFile << "PID: " << GetCurrentProcessId() << std::endl;
    }

    // Initialize Winsock
    if (!s_wsa_initialized) {
        WSADATA wsa_data;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0) {
            LOG_ERROR("[SERVER] WSAStartup failed: {}", result);
            if (debugFile.is_open()) debugFile << "WSAStartup FAILED: " << result << std::endl;
            return false;
        }
        s_wsa_initialized = true;
        if (debugFile.is_open()) debugFile << "WSAStartup OK" << std::endl;
    } else {
        if (debugFile.is_open()) debugFile << "WSA already initialized" << std::endl;
    }

    // Get server config - use defaults if not configured
    const auto& config = AlphaRing::Config::GetInstanceConfig();

    if (debugFile.is_open()) {
        debugFile << "Config loaded: " << (config.loaded ? "YES" : "NO") << std::endl;
        debugFile << "Server enabled: " << (config.server.enabled ? "YES" : "NO") << std::endl;
        debugFile << "Server IP: " << config.server.ip << std::endl;
        debugFile << "Server port: " << config.server.port << std::endl;
        debugFile << "Instance ID: " << config.instance_id << std::endl;
    }

    std::string server_ip = "127.0.0.1";
    int server_port = 42069;
    bool auto_reconnect = true;
    int heartbeat_ms = 5000;
    int stats_ms = 250;

    if (config.loaded && config.server.enabled) {
        // Use config values
        server_ip = config.server.ip;
        server_port = config.server.port;
        auto_reconnect = config.server.auto_reconnect;
        heartbeat_ms = config.server.heartbeat_interval_ms;
        stats_ms = config.server.stats_interval_ms;
        LOG_INFO("[SERVER] Using server config: {}:{}", server_ip, server_port);
        if (debugFile.is_open()) debugFile << "Using CONFIG values" << std::endl;
    } else {
        // Auto-connect to localhost with defaults
        LOG_INFO("[SERVER] No server config - auto-connecting to localhost:{}", server_port);
        if (debugFile.is_open()) debugFile << "Using DEFAULT values (config not loaded or server disabled)" << std::endl;
    }

    if (debugFile.is_open()) {
        debugFile << "Final server_ip: " << server_ip << std::endl;
        debugFile << "Final server_port: " << server_port << std::endl;
    }

    // Create and initialize client
    if (debugFile.is_open()) debugFile << "Creating RingChiefClient..." << std::endl;
    s_client = new RingChiefClient();

    if (!s_client->Initialize(server_ip, server_port)) {
        LOG_ERROR("[SERVER] Failed to initialize RingChiefClient");
        if (debugFile.is_open()) debugFile << "Client Initialize FAILED" << std::endl;
        delete s_client;
        s_client = nullptr;
        return false;
    }
    if (debugFile.is_open()) debugFile << "Client Initialize OK" << std::endl;

    // Apply settings
    s_client->SetAutoReconnect(auto_reconnect);
    s_client->SetHeartbeatInterval(heartbeat_ms);
    s_client->SetStatsInterval(stats_ms);

    // Start connecting
    if (debugFile.is_open()) debugFile << "Calling Connect()..." << std::endl;
    s_client->Connect();

    LOG_INFO("[SERVER] Ring Chief client initialized, connecting to {}:{}",
        server_ip, server_port);

    if (debugFile.is_open()) {
        debugFile << "SUCCESS - Client started connecting to " << server_ip << ":" << server_port << std::endl;
        debugFile.close();
    }

    return true;
}

void Shutdown() {
    if (s_client) {
        s_client->Shutdown();
        delete s_client;
        s_client = nullptr;
    }

    if (s_wsa_initialized) {
        WSACleanup();
        s_wsa_initialized = false;
    }
}

void Update() {
    if (s_client) {
        s_client->Update();
    }
}

RingChiefClient* Get() {
    return s_client;
}

bool IsConnected() {
    return s_client && s_client->IsConnected();
}

} // namespace Client

} // namespace Server
} // namespace MCC
