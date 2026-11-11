#include "RoomServerMain.h"

#include <BECore/Logger/ConsoleSink.h>
#include <RoomServer/RoomWebSocketServer.h>
#include <csignal>
#include <cstdio>
#include <thread>

namespace BECore {

    namespace {

        constexpr std::chrono::milliseconds kLogFlushInterval{50};

        std::atomic<bool> g_stopRequested{false};
        static_assert(std::atomic<bool>::is_always_lock_free, "a signal handler may only set a lock-free atomic");

        void OnStopSignal(int) {
            g_stopRequested.store(true);
        }

    }  // namespace

    int RunRoomServer(const SeatedGameRegistry& registry, const RoomServerArgs& args) {
        // Sinks come from a config in a game; a server without content files makes its own
        auto console = New<ConsoleSink>();
        console->Initialize();

        g_stopRequested = false;
        std::signal(SIGINT, OnStopSignal);
        std::signal(SIGTERM, OnStopSignal);

        RoomWebSocketServer server(registry, ToSettings(args));
        const auto port = server.Start();
        LogEvent::Flush();
        if (!port) {
            std::fprintf(stderr, "BagiRoomServer: %s\n", port.error().message.c_str());
            FlushLogsEvent::Emit();
            return 1;
        }

        std::printf("BagiRoomServer listening on ws://%s:%u\n", args.bind == SocketBind::AnyAddress ? "0.0.0.0" : "127.0.0.1", static_cast<unsigned>(*port));
        std::fflush(stdout);

        while (!g_stopRequested.load()) {
            LogEvent::Flush();
            std::this_thread::sleep_for(kLogFlushInterval);
        }

        server.Stop();
        LogEvent::Flush();
        FlushLogsEvent::Emit();
        return 0;
    }

}  // namespace BECore
