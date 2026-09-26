#pragma once
// Minimal HTTPS + secure WebSocket client on WinHTTP (built into Windows, no extra DLLs).
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <mutex>
#include <string>

namespace RingChief {

    struct ServerAddr {
        bool secure = true;
        std::wstring host;
        INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
        std::wstring base_path;   // "" or "/something" (no trailing slash)
        std::string display;      // normalised form for the UI, e.g. "halo.dronedude.app"
    };

    // Accepts "halo.dronedude.app", "https://host", "http://192.168.1.50:3004", "host:3004/base".
    // A bare host means https on 443.
    bool ParseServer(const std::string& input, ServerAddr& out, std::string& err);

    struct HttpResult {
        int status = 0;           // 0 = network error (see error)
        std::string body;
        std::string error;
    };

    HttpResult PostJson(const ServerAddr& server, const std::string& path, const std::string& body,
                        const std::string& bearer = "", int timeout_ms = 10000);

    class WebSocket {
    public:
        WebSocket() = default;
        ~WebSocket();
        WebSocket(const WebSocket&) = delete;
        WebSocket& operator=(const WebSocket&) = delete;

        // Connects and upgrades. On failure returns false; http_status is the HTTP status
        // if the server answered (401 = token rejected), 0 for network errors.
        bool Connect(const ServerAddr& server, const std::string& path, const std::string& bearer,
                     int& http_status, std::string& err);
        // Thread-safe.
        bool Send(const std::string& text);
        // Blocks until a whole text message arrives. False when closed or on error.
        bool Receive(std::string& out);
        // Safe to call from any thread; unblocks Receive.
        void Close();
        bool IsOpen() const { return socket_ != nullptr && !closed_; }

    private:
        HINTERNET session_ = nullptr;
        HINTERNET connect_ = nullptr;
        HINTERNET socket_ = nullptr;
        std::mutex send_mutex_;
        std::mutex close_mutex_;
        std::atomic<bool> closed_{false};
    };
}
