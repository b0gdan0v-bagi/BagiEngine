#pragma once

#include <RoomServer/RoomServerArgs.h>

namespace BECore {

    class SeatedGameRegistry;

    /**
     * @brief The whole life of a room server process: what the `main` of a game's own server calls after it filled the registry.
     *
     * Brings up logging (a console sink made in code: a server has no content files; stdout is made line-buffered so a platform sees the log at once), starts a RoomWebSocketServer from @p args,
     * prints the line `BagiRoomServer listening on ws://<address>:<port>` to stdout (flushed: a program that starts the server waits for it) and the settings it runs with to the log, then runs until
     * SIGINT or SIGTERM, stops the server and flushes the log.
     *
     * With `args.profiler` set it starts the profiler first (a flag the build cannot honour: message on stderr, exit 1, nothing listens) and writes the trace after the server
     * and its threads have ended; a trace that cannot be written makes a clean run exit 1.
     *
     * Call it once per process, from the main thread, which also drains the log (the logger wants one consumer).
     *
     * @return 0 after a clean end; 1 when the server could not listen or the profiler could not start or write its trace (the message is on stderr)
     */
    int RunRoomServer(const SeatedGameRegistry& registry, const RoomServerArgs& args);

}  // namespace BECore
