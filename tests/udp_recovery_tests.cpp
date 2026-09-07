// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What happens when the tracker port is already taken.
//
// The case this locks is the ordinary one: the player starts SnowRunner while
// the game they were playing before is still running and still holding UDP
// 4242, notices, and closes it. Nothing tells the mod the port came free, so
// the only thing that reclaims it is the receiver's own retry loop - and a loop
// that runs every few seconds reads, from the driving seat, as the mod being
// broken.
//
// These tests drive the real UdpReceiver against a real socket and MEASURE the
// two numbers that decide whether that experience is acceptable: how long after
// the port frees the receiver is bound, and how long after that the first
// tracker packet is in hand. Reading kRetryIntervalMs out of the header proves
// neither, because the supervisor's own 100ms tick and the join in Stop() both
// add to what the player actually waits.

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/protocol/opentrack_packet.h"
#include "cameraunlock/protocol/socket_types.h"

#include "test_support.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using cameraunlock::UdpReceiver;
using sr_test::Check;

namespace {

// Not 4242, and not a fixed number either. A developer running OpenTrack while
// the tests run would hand the suite a port that is genuinely in use and a
// failure that is not a regression - and a hardcoded port does the same thing to
// two copies of this suite running at once, which is what a CI runner building
// several mods on one machine does. Chosen at startup by binding 0 and asking
// the OS what it gave us, which is not a reservation - the probe socket is
// closed before the suite rebinds it - but it picks from the ephemeral range the
// OS is handing out, so a collision needs two runs to land on the same number in
// the same instant rather than being guaranteed by a shared constant.
uint16_t kTestPort = 0;

uint16_t PickFreePort() {
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;                 // "any free port"
    addr.sin_addr.s_addr = INADDR_ANY;
    uint16_t port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != SOCKET_ERROR) {
        sockaddr_in bound = {};
        int len = sizeof(bound);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) != SOCKET_ERROR) {
            port = ntohs(bound.sin_port);
        }
    }
    closesocket(s);
    return port;
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Stands in for the other game: an ordinary UDP socket holding the port, with
// no SO_REUSEADDR, which is what every consumer of this port does.
class PortHolder {
public:
    bool Take(uint16_t port) {
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) return false;
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            return false;
        }
        return true;
    }

    void Release() {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }

    ~PortHolder() { Release(); }

private:
    SOCKET m_socket = INVALID_SOCKET;
};

// The error code the OS really gives for this conflict, read from our own bind
// rather than assumed to be WSAEADDRINUSE. The log line is then checked for
// THIS number, so the test says "the receiver reported what the socket said"
// without itself deciding what the socket should have said.
int BindErrorFor(uint16_t port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    const int code = bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR
                         ? WSAGetLastError()
                         : 0;
    closesocket(s);
    return code;
}

// A tracker that keeps sending throughout, which is what OpenTrack and every
// phone app do: they never learn that the game they are aimed at could not
// bind, so the first packet after a successful bind is at most one sample away.
class Tracker {
public:
    void Start(uint16_t port) {
        m_stop.store(false);
        m_thread = std::thread([this, port] {
            SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in dest = {};
            dest.sin_family = AF_INET;
            dest.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);

            const double payload[6] = {0.0, 0.0, 0.0, 12.5, -3.25, 1.5};
            char packet[48];
            std::memcpy(packet, payload, sizeof(packet));

            while (!m_stop.load(std::memory_order_relaxed)) {
                sendto(s, packet, sizeof(packet), 0,
                       reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
                std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60Hz
            }
            closesocket(s);
        });
    }

    void Stop() {
        m_stop.store(true);
        if (m_thread.joinable()) m_thread.join();
    }

    ~Tracker() { Stop(); }

private:
    std::thread m_thread;
    std::atomic<bool> m_stop{true};
};

// The receiver logs from both the caller thread and the supervisor thread, so
// the sink has to be able to take both.
class LogCapture {
public:
    std::function<void(const std::string&)> Sink() {
        return [this](const std::string& line) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lines.push_back(line);
        };
    }

    bool Any(const std::string& needle) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& line : m_lines) {
            if (line.find(needle) != std::string::npos) return true;
        }
        return false;
    }

    void Print() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& line : m_lines) {
            std::printf("        [udp] %s\n", line.c_str());
        }
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_lines;
};

// Polls at 1ms so the measurement's own granularity is far below the interval
// being measured. Returns -1 if the deadline passes.
template <typename Predicate>
int64_t WaitFor(Predicate pred, int timeoutMs) {
    const int64_t start = NowMs();
    while (NowMs() - start < timeoutMs) {
        if (pred()) return NowMs() - start;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return -1;
}

}  // namespace

int main() {
    WSADATA wsa;
    // Keeps Winsock up for the whole run: the receiver calls WSACleanup on
    // every failed Open, and without our own reference the holder socket would
    // go with it.
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::printf("  FAIL: WSAStartup\n");
        return 1;
    }

    kTestPort = PickFreePort();
    if (!Check(kTestPort != 0, "the OS handed the suite a free UDP port")) {
        WSACleanup();
        return sr_test::Summary("udp_recovery");
    }
    std::printf("      using UDP port %u for this run\n", static_cast<unsigned>(kTestPort));

    std::printf("\nthe port is held, so the bind fails and says why\n");
    PortHolder other_game;
    if (!Check(other_game.Take(kTestPort), "a stand-in for the previous game holds the port")) {
        WSACleanup();
        return sr_test::Summary("udp_recovery");
    }

    const int expectedBindError = BindErrorFor(kTestPort);
    Check(expectedBindError != 0, "the OS refuses a second bind on that port");

    LogCapture log;
    UdpReceiver receiver;
    receiver.SetLog(log.Sink());

    const bool started = receiver.Start(kTestPort);
    Check(!started, "Start reports the bind failed");
    Check(!receiver.IsRunning(), "no receive thread while the port is taken");
    Check(receiver.IsRetrying(), "the supervisor is retrying");
    Check(receiver.IsFailed(), "the failure is visible to the mod");

    Check(log.Any("Failed to bind UDP port"), "the failure is logged");
    Check(log.Any(std::to_string(expectedBindError)),
          "the log carries the error code the socket actually returned");
    Check(log.Any("retrying every"), "the log says a retry is coming");
    // The cause has to come from the OS. A port conflict is what this test
    // creates, but the same bind fails with WSAEACCES inside a Hyper-V reserved
    // range, where a line naming an app sends the user hunting one that is not
    // running.
    Check(!log.Any("another program"), "the log does not assert a cause of its own");
    log.Print();

    std::printf("\nthe port frees and the receiver takes it, without being told\n");
    Tracker tracker;
    tracker.Start(kTestPort);

    const int64_t releasedAt = NowMs();
    other_game.Release();

    const int64_t boundAfterMs = WaitFor([&] { return receiver.IsRunning(); }, 5000);
    Check(boundAfterMs >= 0, "the receiver binds on its own after the port frees");

    const int64_t poseAfterMs = WaitFor(
        [&] {
            float y, p, r;
            return receiver.IsReceiving() && receiver.GetRotation(y, p, r);
        },
        5000);
    const int64_t totalMs = poseAfterMs < 0 ? -1 : (NowMs() - releasedAt);

    std::printf("      port freed -> bound:      %lldms\n", static_cast<long long>(boundAfterMs));
    std::printf("      port freed -> first pose: %lldms\n", static_cast<long long>(totalMs));

    Check(poseAfterMs >= 0, "a tracker packet is in hand once it is bound");
    // One supervisor tick (100ms) past one retry interval (500ms) is the worst
    // case the design allows; 900ms leaves room for a loaded CI runner without
    // letting a genuine regression to seconds through.
    Check(boundAfterMs >= 0 && boundAfterMs < 900,
          "the bind lands within one retry interval plus a tick");
    Check(totalMs >= 0 && totalMs < 1000,
          "the whole recovery, port to pose, is under a second");

    Check(!receiver.IsRetrying(), "the retry state clears once it binds");
    Check(!receiver.IsFailed(), "the failure state clears once it binds");
    Check(log.Any("Bound UDP port"), "the recovery is logged");

    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    Check(receiver.GetRotation(yaw, pitch, roll), "a pose is readable");
    sr_test::CheckClose(yaw, 12.5f, "the pose is the one the tracker sent");

    tracker.Stop();
    receiver.Stop();

    // Every trial releases the port at a different point in the retry cycle, so
    // the latencies map the cadence rather than sampling one lucky phase of it.
    // A 500ms period shows up as a latency that falls as the release slides
    // later into a cycle and jumps back up when it crosses into the next one.
    std::printf("\nthe retry cadence, sampled across the cycle\n");
    const int kOffsets[] = {60, 310, 560, 810};
    int64_t worst = 0;
    bool allBound = true;
    for (int offsetMs : kOffsets) {
        PortHolder holder;
        if (!holder.Take(kTestPort)) {
            Check(false, "the port could be retaken for a cadence trial");
            allBound = false;
            break;
        }
        UdpReceiver trial;
        trial.SetLog([](const std::string&) {});
        trial.Start(kTestPort);
        std::this_thread::sleep_for(std::chrono::milliseconds(offsetMs));
        holder.Release();
        const int64_t latency = WaitFor([&] { return trial.IsRunning(); }, 5000);
        std::printf("      released %4dms after Start -> bound %lldms later\n",
                    offsetMs, static_cast<long long>(latency));
        if (latency < 0) {
            allBound = false;
        } else if (latency > worst) {
            worst = latency;
        }
        trial.Stop();
    }
    Check(allBound, "every trial recovered, wherever in the cycle the port freed");
    Check(allBound && worst < 900, "the worst case across the cycle stays under 900ms");

    std::printf("\nshutting down mid-retry does not hang\n");
    {
        PortHolder holder;
        Check(holder.Take(kTestPort), "the port is held again");
        UdpReceiver stuck;
        stuck.SetLog([](const std::string&) {});
        stuck.Start(kTestPort);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const int64_t stopStart = NowMs();
        stuck.Stop();
        const int64_t stopMs = NowMs() - stopStart;
        std::printf("      Stop while retrying took %lldms\n", static_cast<long long>(stopMs));
        Check(stopMs < 500, "Stop returns within a supervisor tick or two");
    }

    WSACleanup();
    return sr_test::Summary("udp_recovery");
}
