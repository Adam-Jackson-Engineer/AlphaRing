#include "Http.h"

#include <algorithm>
#include <vector>

namespace RingChief {

    namespace {
        const wchar_t* kUserAgent = L"AlphaRing-RingChief/1.0";

        std::wstring Widen(const std::string& s) {
            if (s.empty()) return L"";
            int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
            std::wstring w(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
            return w;
        }

        std::string LastError(const char* what) {
            DWORD code = GetLastError();
            switch (code) {
                case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "Can't find that server (check the address and your internet)";
                case ERROR_WINHTTP_CANNOT_CONNECT: return "Can't connect to the server";
                case ERROR_WINHTTP_TIMEOUT: return "The server didn't answer in time";
                case ERROR_WINHTTP_SECURE_FAILURE: return "Secure connection failed (certificate problem)";
                case ERROR_WINHTTP_CONNECTION_ERROR: return "Connection dropped";
                default: return std::string(what) + " failed (error " + std::to_string(code) + ")";
            }
        }

        HINTERNET OpenSession(int timeout_ms) {
            HINTERNET s = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!s) s = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (s) WinHttpSetTimeouts(s, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
            return s;
        }

        int StatusOf(HINTERNET request) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            return (int)status;
        }

        std::string ReadBody(HINTERNET request) {
            std::string body;
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(request, &avail) && avail > 0) {
                std::vector<char> buf(avail);
                DWORD read = 0;
                if (!WinHttpReadData(request, buf.data(), avail, &read) || read == 0) break;
                body.append(buf.data(), read);
                if (body.size() > 4 * 1024 * 1024) break;
            }
            return body;
        }
    }

    bool ParseServer(const std::string& input, ServerAddr& out, std::string& err) {
        std::string s = input;
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n/") + 1);
        if (s.empty()) { err = "Enter a server address"; return false; }

        ServerAddr a;
        std::string lower = s;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.rfind("https://", 0) == 0) { a.secure = true; s = s.substr(8); }
        else if (lower.rfind("http://", 0) == 0) { a.secure = false; s = s.substr(7); a.port = INTERNET_DEFAULT_HTTP_PORT; }
        else if (lower.rfind("wss://", 0) == 0) { a.secure = true; s = s.substr(6); }
        else if (lower.rfind("ws://", 0) == 0) { a.secure = false; s = s.substr(5); a.port = INTERNET_DEFAULT_HTTP_PORT; }

        std::string path;
        auto slash = s.find('/');
        if (slash != std::string::npos) { path = s.substr(slash); s = s.substr(0, slash); }
        while (!path.empty() && path.back() == '/') path.pop_back();

        auto colon = s.find(':');
        std::string host = colon == std::string::npos ? s : s.substr(0, colon);
        if (colon != std::string::npos) {
            std::string port = s.substr(colon + 1);
            if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos) {
                err = "The port after ':' must be a number"; return false;
            }
            int p = std::stoi(port);
            if (p < 1 || p > 65535) { err = "Port out of range"; return false; }
            a.port = (INTERNET_PORT)p;
        }
        std::transform(host.begin(), host.end(), host.begin(), ::tolower);
        if (host.empty() || host.find_first_of(" @?#") != std::string::npos) { err = "That doesn't look like a server address"; return false; }

        a.host = Widen(host);
        a.base_path = Widen(path);
        bool default_port = (a.secure && a.port == 443) || (!a.secure && a.port == 80);
        a.display = (a.secure ? "" : "http://") + host + (default_port ? "" : ":" + std::to_string(a.port)) + path;
        out = a;
        return true;
    }

    HttpResult PostJson(const ServerAddr& server, const std::string& path, const std::string& body,
                        const std::string& bearer, int timeout_ms) {
        HttpResult r;
        HINTERNET session = OpenSession(timeout_ms);
        if (!session) { r.error = LastError("WinHttpOpen"); return r; }
        HINTERNET connect = WinHttpConnect(session, server.host.c_str(), server.port, 0);
        HINTERNET request = connect ? WinHttpOpenRequest(connect, L"POST", (server.base_path + Widen(path)).c_str(), nullptr,
                                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                         server.secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;
        if (!request) {
            r.error = LastError("Connect");
        } else {
            std::wstring headers = L"Content-Type: application/json\r\n";
            if (!bearer.empty()) headers += L"Authorization: Bearer " + Widen(bearer) + L"\r\n";
            BOOL ok = WinHttpSendRequest(request, headers.c_str(), (DWORD)-1L, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0)
                      && WinHttpReceiveResponse(request, nullptr);
            if (!ok) r.error = LastError("Request");
            else { r.status = StatusOf(request); r.body = ReadBody(request); }
        }
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return r;
    }

    WebSocket::~WebSocket() { Close(); }

    bool WebSocket::Connect(const ServerAddr& server, const std::string& path, const std::string& bearer,
                            int& http_status, std::string& err) {
        http_status = 0;
        closed_ = false;
        session_ = OpenSession(10000);
        if (!session_) { err = LastError("WinHttpOpen"); return false; }
        connect_ = WinHttpConnect(session_, server.host.c_str(), server.port, 0);
        if (!connect_) { err = LastError("Connect"); Close(); return false; }
        HINTERNET request = WinHttpOpenRequest(connect_, L"GET", (server.base_path + Widen(path)).c_str(), nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               server.secure ? WINHTTP_FLAG_SECURE : 0);
        if (!request) { err = LastError("Open request"); Close(); return false; }
        WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
        std::wstring headers = L"Authorization: Bearer " + Widen(bearer) + L"\r\n";
        if (!WinHttpSendRequest(request, headers.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request, nullptr)) {
            err = LastError("WebSocket request");
            WinHttpCloseHandle(request); Close(); return false;
        }
        http_status = StatusOf(request);
        if (http_status != 101) {
            err = http_status == 401 ? "Login expired, sign in again" : "Server refused the connection (HTTP " + std::to_string(http_status) + ")";
            WinHttpCloseHandle(request); Close(); return false;
        }
        socket_ = WinHttpWebSocketCompleteUpgrade(request, 0);
        WinHttpCloseHandle(request);
        if (!socket_) { err = LastError("WebSocket upgrade"); Close(); return false; }
        // The receive loop blocks on purpose; the hub pings every 20 s to keep it alive.
        DWORD infinite = 0;
        WinHttpSetOption(socket_, WINHTTP_OPTION_RECEIVE_TIMEOUT, &infinite, sizeof(infinite));
        return true;
    }

    bool WebSocket::Send(const std::string& text) {
        std::lock_guard<std::mutex> lock(send_mutex_);
        if (!socket_ || closed_) return false;
        DWORD rc = WinHttpWebSocketSend(socket_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)text.data(), (DWORD)text.size());
        return rc == NO_ERROR;
    }

    bool WebSocket::Receive(std::string& out) {
        out.clear();
        char buf[8192];
        for (;;) {
            if (!socket_ || closed_) return false;
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            DWORD rc = WinHttpWebSocketReceive(socket_, buf, sizeof(buf), &read, &type);
            if (rc != NO_ERROR) return false;
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return false;
            out.append(buf, read);
            if (out.size() > 8 * 1024 * 1024) return false;
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) return true;
            // *_FRAGMENT_BUFFER_TYPE: keep reading
        }
    }

    void WebSocket::Close() {
        std::lock_guard<std::mutex> lock(close_mutex_);
        std::lock_guard<std::mutex> send_lock(send_mutex_);  // no Send() mid-close
        closed_ = true;
        if (socket_) {
            WinHttpWebSocketClose(socket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            WinHttpCloseHandle(socket_);
            socket_ = nullptr;
        }
        if (connect_) { WinHttpCloseHandle(connect_); connect_ = nullptr; }
        if (session_) { WinHttpCloseHandle(session_); session_ = nullptr; }
    }
}
