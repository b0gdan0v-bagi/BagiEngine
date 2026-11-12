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
     * Call it once per process, from the main thread, which also drains the log (the logger wants one consumer).
     *
     * @return 0 after a clean end; 1 when the server could not listen (the message is on stderr)
     */
    int RunRoomServer(const SeatedGameRegistry& registry, const RoomServerArgs& args);

}  // namespace BECore
