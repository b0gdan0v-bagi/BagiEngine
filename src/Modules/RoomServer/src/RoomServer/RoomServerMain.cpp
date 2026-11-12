#include "RoomServerMain.h"

#include <BECore/Logger/ConsoleSink.h>
#include <RoomServer/RoomWebSocketServer.h>
#include <csignal>
#include <cstdio>
#include <thread>

namespace BECore {

    namespace {

        constexpr std::chrono::milliseconds kLogFlushInterval{50};
        constexpr size_t kStdoutBufferBytes = 4096;

        std::atomic<bool> g_stopRequested{false};
        static_assert(std::atomic<bool>::is_always_lock_free, "a signal handler may only set a lock-free atomic");

        void OnStopSignal(int) {
            g_stopRequested.store(true);
        }

    }  // namespace

    namespace {

        /// The effective settings, for the log of the platform (nothing secret is in them)
        eastl::string DescribeSettings(const RoomServerArgs& args, uint16_t port) {
            eastl::string origins;
            for (const eastl::string& origin : args.origins) {
                origins += origins.empty() ? "" : ", ";
                origins += origin;
            }
            return Format("Room server settings: port {}, bind {}, origins {} [{}], max connections {} (+{} in the handshake), max per address {}, computer delay {} ms, reconnect grace {} ms, "
                          "ping interval {} ms, stats interval {} ms, shutdown grace {} ms",
                          port, args.bind == SocketBind::AnyAddress ? "any" : "loopback", args.origins.size(), origins, args.maxConnections, kHandshakeConnectionReserve, args.maxConnectionsPerAddress,
                          args.computerDelay.count(), args.reconnectGrace.count(), args.pingInterval.count(), args.statsInterval.count(), args.shutdownGrace.count());
        }

    }  // namespace

    int RunRoomServer(const SeatedGameRegistry& registry, const RoomServerArgs& args) {
        // Piped (a container, a test) stdout is block-buffered: the platform would see the log in chunks or at exit. A size above 1, which MSVC does not take
        std::setvbuf(stdout, nullptr, _IOLBF, kStdoutBufferBytes);

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

        LOG_INFO(DescribeSettings(args, *port));
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
