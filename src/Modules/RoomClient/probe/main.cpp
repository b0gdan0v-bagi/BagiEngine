// The smallest client of the rooms: hosts a room on a room server, starts it at once (the other seats are computers), plays every turn
// with the first action it is offered, and exits 0 when the game is over. tools/rooms/smoke.py runs it against BagiRoomServer; a game's
// component does the same per frame instead of in a loop (RoomClient/CLAUDE.md, "How a game uses it")

#include <BECore/Control/Json.h>
#include <BECore/Logger/ConsoleSink.h>
#include <RoomClient/RoomClient.h>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

    using namespace BECore;
    using namespace std::chrono_literals;

    constexpr auto kPollInterval = 10ms;
    constexpr auto kTimeLimit = 30s;
    constexpr int kExitOk = 0;
    constexpr int kExitFailed = 1;
    constexpr int kExitUsage = 2;

    struct ProbeArgs {
        eastl::string url;
        eastl::string game = "cards";
        eastl::string name = "Probe";
    };

    void PrintUsage() {
        std::printf("BagiRoomClientProbe --url ws://host:port [--game cards] [--name Probe]\n");
    }

    /// Reads the arguments; false (after a message) when they are not usable
    bool ParseArgs(int argc, char* argv[], ProbeArgs& args) {
        for (int i = 1; i < argc; ++i) {
            const eastl::string_view flag = argv[i];
            if (flag == "--help" || flag == "-h") {
                PrintUsage();
                return false;
            }
            if (i + 1 >= argc || (flag != "--url" && flag != "--game" && flag != "--name")) {
                std::fprintf(stderr, "BagiRoomClientProbe: unknown argument or a missing value: %s\n", argv[i]);
                return false;
            }
            const eastl::string value = argv[++i];
            (flag == "--url" ? args.url : flag == "--game" ? args.game : args.name) = value;
        }
        if (args.url.empty()) {
            std::fprintf(stderr, "BagiRoomClientProbe: --url is required\n");
            return false;
        }
        return true;
    }

    int Fail(const eastl::string& message) {
        std::printf("PROBE FAILED %s\n", message.c_str());
        std::fflush(stdout);
        return kExitFailed;
    }

}  // namespace

int main(int argc, char* argv[]) {
    ProbeArgs args;
    if (!ParseArgs(argc, argv, args)) {
        return kExitUsage;
    }

    auto console = New<ConsoleSink>();
    console->Initialize();

    // A lost connection ends the probe at once: it is a check, not a player, so it does not wait for a reconnect
    RoomClientSettings settings;
    settings.reconnect = false;
    RoomClient client(std::move(settings));
    if (const auto hosted = client.Host(args.url, args.game, args.name); !hosted) {
        return Fail(Format("{}: {}", hosted.error().code, hosted.error().message));
    }

    eastl::vector<RoomClientEvent> events;
    bool started = false;
    bool sawView = false;
    uint64_t actedVersion = 0;
    const auto deadline = std::chrono::steady_clock::now() + kTimeLimit;

    while (std::chrono::steady_clock::now() < deadline) {
        client.Poll();
        LogEvent::Flush();
        events.clear();
        client.DrainEvents(events);
        for (const RoomClientEvent& event : events) {
            switch (event.kind) {
                case RoomClientEventKind::Error:
                case RoomClientEventKind::Closed:
                    return Fail(Format("{} {} {}", EnumUtils<RoomClientEventKind>::ToString(event.kind), event.code, event.message));
                case RoomClientEventKind::ConnectionLost:
                    return Fail(Format("connection lost: {}", event.message));
                case RoomClientEventKind::Joined:
                    if (!started) {
                        started = true;
                        if (const auto result = client.Start(); !result) {
                            return Fail(Format("{}: {}", result.error().code, result.error().message));
                        }
                    }
                    break;
                case RoomClientEventKind::ViewChanged:
                    if (!sawView) {
                        sawView = true;
                        // smoke.py waits for this line before it takes the server away
                        std::printf("PROBE first view version=%llu\n", static_cast<unsigned long long>(client.GetViewVersion()));
                        std::fflush(stdout);
                    }
                    break;
                default:
                    break;
            }
        }

        if (client.IsFinished()) {
            std::printf("PROBE OK finished version=%llu\n", static_cast<unsigned long long>(client.GetViewVersion()));
            std::fflush(stdout);
            client.Disconnect();
            return kExitOk;
        }

        // Its turn: the view carries actions, and one version is answered once
        const Json& actions = client.GetActions();
        if (client.HasView() && actions.is_array() && !actions.empty() && client.GetViewVersion() != actedVersion) {
            actedVersion = client.GetViewVersion();
            if (const auto result = client.SendAction(actions.front()); !result) {
                return Fail(Format("{}: {}", result.error().code, result.error().message));
            }
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return Fail("no end of the game in 30 s");
}
