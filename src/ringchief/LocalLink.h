#pragma once
// Loopback link between the MCC instances on one PC. The first instance to bind
// 127.0.0.1:<port> becomes the hub (it owns the internet connection); the rest connect
// to it as followers. Loopback is the one path that works for every instance under
// Nucleus/ForceBindIP. Frames are newline-delimited JSON. Non-blocking; call Poll()
// from the main thread every frame.
#include <cstdint>
#include <string>
#include <vector>

namespace RingChief {

    class LocalLink {
    public:
        enum class Role { None, Hub, Follower };

        struct Incoming {
            int peer = 0;          // follower id (hub side); 0 = from the hub (follower side)
            std::string line;
        };

        LocalLink();
        ~LocalLink();
        LocalLink(const LocalLink&) = delete;
        LocalLink& operator=(const LocalLink&) = delete;

        // Become the hub if the port is free, otherwise connect as a follower.
        Role Start(uint16_t port);
        void Stop();
        Role GetRole() const { return role_; }

        // Reads everything available. Hub: new/closed followers are reported via
        // `joined`/`left`. Follower: `lost` becomes true if the hub went away.
        void Poll(std::vector<Incoming>& in, std::vector<int>& joined, std::vector<int>& left, bool& lost);

        void Send(int peer, const std::string& line);     // hub -> one follower
        void Broadcast(const std::string& line);          // hub -> all followers
        void SendToHub(const std::string& line);          // follower -> hub
        size_t FollowerCount() const { return peers_.size(); }

    private:
        struct Peer {
            int id;
            uintptr_t sock;
            std::string inbuf;
            std::string outbuf;
        };

        bool Flush(Peer& p);
        bool ReadLines(Peer& p, int peer_id, std::vector<Incoming>& in);

        Role role_ = Role::None;
        uintptr_t listen_ = ~(uintptr_t)0;
        std::vector<Peer> peers_;   // hub: followers; follower: [hub]
        int next_id_ = 1;
        bool wsa_ok_ = false;
    };
}
