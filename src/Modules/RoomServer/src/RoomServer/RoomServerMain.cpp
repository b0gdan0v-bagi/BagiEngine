#include "RoomServerMain.h"

#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Profiler/Profiler.h>
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

        /// The effective settings, for the log of the platform (nothing secret is in them)
        String DescribeSettings(const RoomServerArgs& args, uint16_t port) {
            String origins;
            for (const String& origin : args.origins) {
                origins += origins.empty() ? "" : ", ";
                origins += origin;
            }
            return Format("Room server settings: port {}, bind {}, origins {} [{}], max connections {} (+{} in the handshake), max per address {}, per-address rules {}, max rooms per address {}, "
                          "lobby timeout {} ms, trusted proxy hops {}, computer delay {} ms, reconnect grace {} ms, "
                          "ping interval {} ms, stats interval {} ms, shutdown grace {} ms",
                          port, args.bind == SocketBind::AnyAddress ? "any" : "loopback", args.origins.size(), origins, args.maxConnections, kHandshakeConnectionReserve, args.maxConnectionsPerAddress,
                          args.maxConnectionsPerAddress != 0 || args.trustedProxyHops != 0 ? "on" : "off", args.maxRoomsPerAddress, args.lobbyTimeout.count(), args.trustedProxyHops,
                          args.computerDelay.count(), args.reconnectGrace.count(), args.pingInterval.count(), args.statsInterval.count(), args.shutdownGrace.count());
        }

        int Serve(const SeatedGameRegistry& registry, const RoomServerArgs& args) {
            // A piped stdout (a container, a test) is block-buffered by default: the platform would see the log in chunks or at exit.
            // Line buffering fixes that on POSIX; the Windows CRT treats _IOLBF as full buffering, which is why the loop below also flushes
            std::setvbuf(stdout, nullptr, _IOLBF, kStdoutBufferBytes);

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
                std::fflush(stdout);
                std::this_thread::sleep_for(kLogFlushInterval);
            }

            server.Stop();
            LogEvent::Flush();
            FlushLogsEvent::Emit();
            return 0;
        }

    }  // namespace

    int RunRoomServer(const SeatedGameRegistry& registry, const RoomServerArgs& args) {
        // First: the trace covers the start-up, and a profiler flag that cannot be honoured stops the process before it listens
        BE_PROFILE_THREAD_ORDER("Main", 0);
        if (const auto started = Profiler::Start(args.profiler); !started) {
            std::fprintf(stderr, "BagiRoomServer: Profiler: %s\n", started.error().c_str());
            return 1;
        }

        int code = Serve(registry, args);

        // After Serve: the accept, reader, writer and hub threads have been joined, so every zone is complete in the file. The log sink is gone, hence stderr
        if (const auto finished = Profiler::Finish(); !finished) {
            std::fprintf(stderr, "BagiRoomServer: Profiler: %s\n", finished.error().c_str());
            code = code == 0 ? 1 : code;
        }
        return code;
    }

}  // namespace BECore
