// The smallest client of the rooms: hosts a room on a room server, starts it at once (the other seats are computers), plays every turn
// with the first action it is offered, and exits 0 when the game is over. tools/rooms/smoke.py runs it against BagiRoomServer; a game's
// component does the same per frame instead of in a loop (RoomClient/CLAUDE.md, "A component in ten lines")

#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerSettings.h>
#include <ProbeLoad.h>
#include <RoomClient/RoomClient.h>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

    using namespace BECore;
    using namespace std::chrono_literals;

    constexpr auto kPollInterval = 10ms;
    constexpr auto kTimeLimit = 30s;
    // The load mode: a client is paced by ProbeClient (a few messages a second, and a hand takes some besides its actions), so the time grows with M
    constexpr auto kLoadTimeBase = 30s;
    constexpr auto kLoadTimePerAction = 400ms;
    constexpr int kMaxClients = 64;
    constexpr int kMaxMessages = 100000;
    constexpr int kExitOk = 0;
    constexpr int kExitFailed = 1;
    constexpr int kExitUsage = 2;

    struct ProbeArgs {
        String url;
        String game = "cards";
        String name = "Probe";
        int clients = 0;  // 0: the single probe
        int messages = 0;
        ProfilerSettings profiler;
    };

    void PrintUsage() {
        std::printf("BagiRoomClientProbe --url ws://host:port [--game cards] [--name Probe] [--clients N --messages M] [--trace=FILE] [--profiler=none|chrome|tracy]\n");
    }

    /// Reads a whole number of a flag in [1, maxValue]; false (after a message) when it is not one
    bool ParseCount(StringView flag, StringView text, int maxValue, int& out) {
        int value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() || value < 1 || value > maxValue) {
            std::fprintf(stderr, "BagiRoomClientProbe: %.*s needs a number from 1 to %d, got '%.*s'\n", static_cast<int>(flag.size()), flag.data(), maxValue, static_cast<int>(text.size()),
                         text.data());
            return false;
        }
        out = value;
        return true;
    }

    /// Reads the arguments; false (after a message) when they are not usable
    bool ParseArgs(int argc, char* argv[], ProbeArgs& args) {
        Vector<String> profilerArguments;
        for (int i = 1; i < argc; ++i) {
            const StringView flag = argv[i];
            if (flag == "--help" || flag == "-h") {
                PrintUsage();
                return false;
            }
            if (flag.starts_with(ProfilerArguments::kTracePrefix) || flag.starts_with(ProfilerArguments::kProfilerPrefix)) {
                profilerArguments.emplace_back(flag);
                continue;
            }
            if (i + 1 >= argc || (flag != "--url" && flag != "--game" && flag != "--name" && flag != "--clients" && flag != "--messages")) {
                std::fprintf(stderr, "BagiRoomClientProbe: unknown argument or a missing value: %s\n", argv[i]);
                return false;
            }
            const String value = argv[++i];
            if (flag == "--url") {
                args.url = value;
            } else if (flag == "--game") {
                args.game = value;
            } else if (flag == "--clients") {
                if (!ParseCount(flag, value, kMaxClients, args.clients)) {
                    return false;
                }
            } else if (flag == "--messages") {
                if (!ParseCount(flag, value, kMaxMessages, args.messages)) {
                    return false;
                }
            } else {
                args.name = value;
            }
        }
        if (args.url.empty()) {
            std::fprintf(stderr, "BagiRoomClientProbe: --url is required\n");
            return false;
        }
        if ((args.clients > 0) != (args.messages > 0)) {
            std::fprintf(stderr, "BagiRoomClientProbe: --clients and --messages go together\n");
            return false;
        }
        const auto profiler = ResolveProfilerSettings(profilerArguments);
        if (!profiler) {
            std::fprintf(stderr, "BagiRoomClientProbe: Profiler: %s\n", profiler.error().c_str());
            return false;
        }
        args.profiler = *profiler;
        return true;
    }

    int Fail(const String& message) {
        std::printf("PROBE FAILED %s\n", message.c_str());
        std::fflush(stdout);
        return kExitFailed;
    }

    int RunProbe(const ProbeArgs& args) {
        // A lost connection ends the probe at once: it is a check, not a player, so it does not wait for a reconnect
        RoomClientSettings settings;
        settings.reconnect = false;
        RoomClient client(std::move(settings));
        if (const auto hosted = client.Host(args.url, args.game, args.name); !hosted) {
            return Fail(Format("{}: {}", hosted.error().code, hosted.error().message));
        }

        Vector<RoomClientEvent> events;
        bool started = false;
        bool sawView = false;
        uint64_t actedVersion = 0;
        JsonTape actionsTape;
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
            const StringView action = FirstActionText(actionsTape, client.GetActionsText());
            if (client.HasView() && !action.empty() && client.GetViewVersion() != actedVersion) {
                actedVersion = client.GetViewVersion();
                if (const auto result = client.SendAction(action); !result) {
                    return Fail(Format("{}: {}", result.error().code, result.error().message));
                }
            }
            std::this_thread::sleep_for(kPollInterval);
        }
        return Fail("no end of the game in 30 s");
    }

    /// N clients on this thread, each playing hands until it has sent its share of actions
    int RunLoad(const ProbeArgs& args) {
        Vector<UniquePtr<ProbeClient>> clients;
        for (int i = 0; i < args.clients; ++i) {
            clients.push_back(MakeUnique<ProbeClient>(args.url, args.game, Format("{}{}", args.name, i), static_cast<uint64_t>(args.messages)));
        }
        Vector<ProbeStep> steps(clients.size(), ProbeStep::Running);
        size_t remaining = clients.size();

        const auto start = std::chrono::steady_clock::now();
        const auto deadline = start + kLoadTimeBase + kLoadTimePerAction * args.messages;
        while (remaining > 0) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return Fail(
                    Format("{} of {} clients have not sent {} actions in {} s", remaining, clients.size(), args.messages, std::chrono::duration_cast<std::chrono::seconds>(deadline - start).count()));
            }
            bool active = false;
            for (size_t i = 0; i < clients.size(); ++i) {
                if (steps[i] != ProbeStep::Running) {
                    continue;
                }
                steps[i] = clients[i]->Step(now);
                active = active || clients[i]->HadActivity();
                if (steps[i] == ProbeStep::Failed) {
                    return Fail(Format("client {}: {}", i, clients[i]->GetFailure()));
                }
                if (steps[i] == ProbeStep::Done) {
                    --remaining;
                }
            }
            LogEvent::Flush();
            if (!active) {
                std::this_thread::sleep_for(kPollInterval);
            }
        }

        uint64_t actions = 0;
        uint64_t hands = 0;
        for (const auto& client : clients) {
            actions += client->GetActions();
            hands += client->GetHands();
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("PROBE LOAD OK clients=%d actions=%llu hands=%llu seconds=%.2f\n", args.clients, static_cast<unsigned long long>(actions), static_cast<unsigned long long>(hands), seconds);
        std::fflush(stdout);
        return kExitOk;
    }

}  // namespace

int main(int argc, char* argv[]) {
    ProbeArgs args;
    if (!ParseArgs(argc, argv, args)) {
        return kExitUsage;
    }

    auto console = New<ConsoleSink>();
    console->Initialize();

    // First: the trace covers the whole run, and a profiler flag that cannot be honoured stops the probe before it connects
    BE_PROFILE_THREAD_ORDER("Main", 0);
    if (const auto started = Profiler::Start(args.profiler); !started) {
        std::fprintf(stderr, "BagiRoomClientProbe: Profiler: %s\n", started.error().c_str());
        return kExitUsage;
    }

    int code = args.clients > 0 ? RunLoad(args) : RunProbe(args);

    // After the run: its clients are destroyed, so the reader and the writer threads are joined and every zone is complete in the file
    if (const auto finished = Profiler::Finish(); !finished) {
        std::fprintf(stderr, "BagiRoomClientProbe: Profiler: %s\n", finished.error().c_str());
        code = code == kExitOk ? kExitFailed : code;
    }
    return code;
}
