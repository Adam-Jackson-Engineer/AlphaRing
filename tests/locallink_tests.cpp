// Loopback link between MCC windows on one PC.
#include <winsock2.h>
#include <ws2tcpip.h>

#include "test_util.h"

#include "ringchief/LocalLink.h"

using namespace RingChief;
using namespace testutil;

namespace {
    struct Pump {
        std::vector<LocalLink::Incoming> in;
        std::vector<int> joined, left;
        bool lost = false;
        void Poll(LocalLink& l) {
            bool lost_now = false;
            l.Poll(in, joined, left, lost_now);
            lost = lost || lost_now;
        }
    };

    // A raw socket client, to send bytes AlphaRing itself never would.
    struct RawClient {
        SOCKET s = INVALID_SOCKET;
        explicit RawClient(uint16_t port) {
            WSADATA w;
            WSAStartup(MAKEWORD(2, 2), &w);
            s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(port);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            REQUIRE(connect(s, (sockaddr*)&a, sizeof(a)) == 0);
        }
        void Send(const std::string& bytes) {
            size_t off = 0;
            while (off < bytes.size()) {
                int n = send(s, bytes.data() + off, (int)std::min<size_t>(bytes.size() - off, 65536), 0);
                if (n <= 0) break;
                off += n;
            }
        }
        ~RawClient() { closesocket(s); WSACleanup(); }
    };
}

TEST_SUITE("locallink") {

TEST_CASE("a hub serves several followers; broadcasts reach all, leaving is reported") {
    uint16_t port = RandomPort();
    LocalLink hub;
    REQUIRE(hub.Start(port) == LocalLink::Role::Hub);
    std::vector<std::unique_ptr<LocalLink>> fol;
    for (int i = 0; i < 5; i++) {
        fol.push_back(std::make_unique<LocalLink>());
        REQUIRE(fol.back()->Start(port) == LocalLink::Role::Follower);
    }
    Pump h;
    REQUIRE(PollUntil([&] { h.Poll(hub); }, [&] { return h.joined.size() == 5; }));
    CHECK(hub.FollowerCount() == 5);

    hub.Broadcast(R"({"type":"welcome"})");
    std::vector<Pump> fp(5);
    REQUIRE(PollUntil([&] { for (int i = 0; i < 5; i++) fp[i].Poll(*fol[i]); h.Poll(hub); },
                      [&] { for (auto& p : fp) if (p.in.size() != 1) return false; return true; }));

    // Direct send reaches only its target.
    hub.Send(h.joined[2], R"({"type":"only-2"})");
    PollUntil([&] { for (int i = 0; i < 5; i++) fp[i].Poll(*fol[i]); }, [&] { return fp[2].in.size() == 2; });
    CHECK(fp[2].in.back().line == R"({"type":"only-2"})");
    CHECK(fp[0].in.size() == 1);

    fol[1]->Stop();
    fol[3]->Stop();
    REQUIRE(PollUntil([&] { h.Poll(hub); }, [&] { return h.left.size() == 2; }));
    CHECK(hub.FollowerCount() == 3);
}

TEST_CASE("CRLF, blank lines and split writes are handled") {
    uint16_t port = RandomPort();
    LocalLink hub;
    REQUIRE(hub.Start(port) == LocalLink::Role::Hub);
    RawClient raw(port);
    Pump h;
    raw.Send("{\"a\":1}\r\n\n\n{\"b\":");
    PollUntil([&] { h.Poll(hub); }, [&] { return h.in.size() >= 1; });
    raw.Send("2}\n");
    REQUIRE(PollUntil([&] { h.Poll(hub); }, [&] { return h.in.size() == 2; }));
    CHECK(h.in[0].line == "{\"a\":1}");
    CHECK(h.in[1].line == "{\"b\":2}");
}

TEST_CASE("a peer flooding a line longer than 4 MB is dropped") {
    uint16_t port = RandomPort();
    LocalLink hub;
    REQUIRE(hub.Start(port) == LocalLink::Role::Hub);
    Pump h;
    {
        RawClient raw(port);
        PollUntil([&] { h.Poll(hub); }, [&] { return h.joined.size() == 1; });
        std::thread writer([&] { raw.Send(std::string(5 * 1024 * 1024, 'x')); });
        REQUIRE(PollUntil([&] { h.Poll(hub); }, [&] { return h.left.size() == 1; }, 10));
        writer.join();
    }
    CHECK(h.in.empty());
    CHECK(hub.FollowerCount() == 0);
}

TEST_CASE("Start fails over cleanly: stop, restart, roles swap") {
    uint16_t port = RandomPort();
    LocalLink a, b;
    REQUIRE(a.Start(port) == LocalLink::Role::Hub);
    REQUIRE(b.Start(port) == LocalLink::Role::Follower);
    a.Stop();
    CHECK(a.GetRole() == LocalLink::Role::None);
    Pump pb;
    REQUIRE(PollUntil([&] { pb.Poll(b); }, [&] { return pb.lost; }));
    CHECK(b.GetRole() == LocalLink::Role::None);
    CHECK(b.Start(port) == LocalLink::Role::Hub);
    CHECK(a.Start(port) == LocalLink::Role::Follower);
}

TEST_CASE("sending in the wrong role is a no-op, not a crash") {
    LocalLink idle;
    idle.Broadcast("x");
    idle.SendToHub("x");
    idle.Send(3, "x");
    Pump p;
    p.Poll(idle);
    CHECK(p.in.empty());
    uint16_t port = RandomPort();
    LocalLink hub;
    REQUIRE(hub.Start(port) == LocalLink::Role::Hub);
    hub.SendToHub("x");   // hubs have no hub
    hub.Send(999, "x");   // unknown follower
}

TEST_CASE("the port is exclusive: a second hub can't steal it") {
    uint16_t port = RandomPort();
    LocalLink hub, other;
    REQUIRE(hub.Start(port) == LocalLink::Role::Hub);
    CHECK(other.Start(port) == LocalLink::Role::Follower);
}

}  // TEST_SUITE
