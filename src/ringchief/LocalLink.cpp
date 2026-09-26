#include "LocalLink.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>

namespace RingChief {

    namespace {
        constexpr size_t kMaxLine = 4 * 1024 * 1024;

        void NonBlocking(SOCKET s) {
            u_long on = 1;
            ioctlsocket(s, FIONBIO, &on);
        }

        sockaddr_in Loopback(uint16_t port) {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(port);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            return a;
        }
    }

    LocalLink::LocalLink() {
        WSADATA wsa;
        wsa_ok_ = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }

    LocalLink::~LocalLink() {
        Stop();
        if (wsa_ok_) WSACleanup();
    }

    LocalLink::Role LocalLink::Start(uint16_t port) {
        Stop();
        if (!wsa_ok_) return role_;

        // Try to be the hub. SO_EXCLUSIVEADDRUSE: no other process can share the port.
        SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ls != INVALID_SOCKET) {
            BOOL excl = TRUE;
            setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
            sockaddr_in a = Loopback(port);
            if (bind(ls, (sockaddr*)&a, sizeof(a)) == 0 && listen(ls, 8) == 0) {
                NonBlocking(ls);
                listen_ = (uintptr_t)ls;
                role_ = Role::Hub;
                return role_;
            }
            closesocket(ls);
        }

        // Someone else is the hub: connect to it (loopback connects are effectively instant).
        SOCKET cs = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (cs == INVALID_SOCKET) return role_;
        sockaddr_in a = Loopback(port);
        if (connect(cs, (sockaddr*)&a, sizeof(a)) != 0) {
            closesocket(cs);
            return role_;
        }
        NonBlocking(cs);
        BOOL nodelay = TRUE;
        setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
        peers_.push_back(Peer{0, (uintptr_t)cs, {}, {}});
        role_ = Role::Follower;
        return role_;
    }

    void LocalLink::Stop() {
        for (auto& p : peers_) closesocket((SOCKET)p.sock);
        peers_.clear();
        if (listen_ != ~(uintptr_t)0) { closesocket((SOCKET)listen_); listen_ = ~(uintptr_t)0; }
        role_ = Role::None;
    }

    bool LocalLink::Flush(Peer& p) {
        while (!p.outbuf.empty()) {
            int n = send((SOCKET)p.sock, p.outbuf.data(), (int)std::min<size_t>(p.outbuf.size(), 64 * 1024), 0);
            if (n > 0) { p.outbuf.erase(0, n); continue; }
            return n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK;
        }
        return true;
    }

    bool LocalLink::ReadLines(Peer& p, int peer_id, std::vector<Incoming>& in) {
        char buf[16384];
        for (;;) {
            int n = recv((SOCKET)p.sock, buf, sizeof(buf), 0);
            if (n > 0) {
                p.inbuf.append(buf, n);
                if (p.inbuf.size() > kMaxLine) return false;
                continue;
            }
            if (n == 0) return false;  // closed
            if (WSAGetLastError() == WSAEWOULDBLOCK) break;
            return false;
        }
        size_t pos;
        while ((pos = p.inbuf.find('\n')) != std::string::npos) {
            std::string line = p.inbuf.substr(0, pos);
            p.inbuf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) in.push_back({peer_id, std::move(line)});
        }
        return true;
    }

    void LocalLink::Poll(std::vector<Incoming>& in, std::vector<int>& joined, std::vector<int>& left, bool& lost) {
        lost = false;
        if (role_ == Role::Hub) {
            for (;;) {
                SOCKET s = accept((SOCKET)listen_, nullptr, nullptr);
                if (s == INVALID_SOCKET) break;
                NonBlocking(s);
                BOOL nodelay = TRUE;
                setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
                int id = next_id_++;
                peers_.push_back(Peer{id, (uintptr_t)s, {}, {}});
                joined.push_back(id);
            }
        }
        for (size_t i = 0; i < peers_.size();) {
            Peer& p = peers_[i];
            bool ok = ReadLines(p, role_ == Role::Hub ? p.id : 0, in) && Flush(p);
            if (ok) { i++; continue; }
            closesocket((SOCKET)p.sock);
            if (role_ == Role::Hub) left.push_back(p.id); else lost = true;
            peers_.erase(peers_.begin() + i);
        }
        if (role_ == Role::Follower && peers_.empty()) { role_ = Role::None; lost = true; }
    }

    void LocalLink::Send(int peer, const std::string& line) {
        for (auto& p : peers_) if (p.id == peer) { p.outbuf += line; p.outbuf += '\n'; Flush(p); }
    }

    void LocalLink::Broadcast(const std::string& line) {
        if (role_ != Role::Hub) return;
        for (auto& p : peers_) { p.outbuf += line; p.outbuf += '\n'; Flush(p); }
    }

    void LocalLink::SendToHub(const std::string& line) {
        if (role_ != Role::Follower || peers_.empty()) return;
        peers_[0].outbuf += line;
        peers_[0].outbuf += '\n';
        Flush(peers_[0]);
    }
}
