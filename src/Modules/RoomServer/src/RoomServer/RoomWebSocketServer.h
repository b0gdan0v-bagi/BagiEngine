#pragma once

#include <BECore/Control/ControlTypes.h>
#include <RemoteControl/WebSocketListener.h>
#include <RoomServer/RoomServerSettings.h>
#include <Rooms/RoomHub.h>

namespace BECore {

    class SeatedGameRegistry;

    /// What the periodic `Room server: <n> connections, <m> rooms` line says: the hub's own counts
    struct RoomServerStats {
        size_t connections = 0;
        size_t rooms = 0;

        [[nodiscard]] bool operator==(const RoomServerStats&) const = default;
    };

    /// Whether the hub thread logs the stats line now: the numbers differ from the ones last logged, and @p interval has passed since that line (or since the start).
    /// A zero or negative @p interval is off
    [[nodiscard]] bool ShouldLogRoomServerStats(const RoomServerStats& current, const RoomServerStats& lastLogged, RoomTime now, RoomTime lastLoggedAt, std::chrono::milliseconds interval);

    /**
     * @brief The Rooms hub on the network: a WebSocket server whose every text message is a message of the room protocol.
     *
     * A WebSocketListener (RemoteControl) owns the sockets and the reader and writer threads of the connections. The hub (RoomHub)
     * is single-threaded, so **one hub thread** owns it: the reader threads push what happens (a connection opened, a text, a
     * connection closed) into a bounded queue, the hub thread drains it, feeds the hub in order and ticks it every `tickInterval`.
     * What the hub sends goes through the HubSink: `send` looks the connection up by its id and queues a text frame (it never
     * writes to a socket, so a slow client cannot stall the hub), `close` queues a close frame. For one connection the order of
     * the frames is the order the hub produced them.
     *
     * Liveness: on every tick the hub thread looks at the byte counter of each open connection. One that sent nothing for
     * `pingInterval` gets a ping, one that sent nothing (a pong counts) for twice that is dropped, which the hub learns as a
     * disconnect.
     *
     * The queue holds at most kMaxQueuedEvents events and kMaxQueuedBytes of text: a reader that finds it full waits, so TCP slows
     * the fast sender and the memory stays bounded.
     *
     * Stop() with `shutdownGrace` first tells every client (close 1001 `server_stopping`) and waits, on the real clock, until the connections
     * have ended or the grace has passed; the hub thread runs meanwhile.
     *
     * Start() and Stop() belong to one thread. See RoomServer/CLAUDE.md for the limits and the close codes.
     */
    class RoomWebSocketServer {
    public:
        static constexpr size_t kMaxQueuedEvents = 4096;
        static constexpr size_t kMaxQueuedBytes = 4 * 1024 * 1024;

        /// @param registry Must outlive the server; rooms create their games through it. Not changed while the server runs
        RoomWebSocketServer(const SeatedGameRegistry& registry, RoomServerSettings settings);
        ~RoomWebSocketServer();

        RoomWebSocketServer(const RoomWebSocketServer&) = delete;
        RoomWebSocketServer& operator=(const RoomWebSocketServer&) = delete;

        /// Starts listening on `settings.port` and runs the hub thread; returns the port in use. The error code is `listen_failed` (a port that is taken, for example)
        [[nodiscard]] std::expected<uint16_t, ControlError> Start();

        /// Ends every connection (after `shutdownGrace`, if any, for the clients to take the close 1001), joins every thread. Safe to call twice and when never started. After it returns no callback
        /// runs and nothing is sent
        void Stop();

        [[nodiscard]] bool IsRunning() const {
            return _listener.IsRunning();
        }

        /// The port in use; 0 when the server is not running
        [[nodiscard]] uint16_t GetPort() const {
            return _listener.GetPort();
        }

    private:
        enum class EventKind : uint8_t { Opened, Message, Closed };

        struct Event {
            EventKind kind = EventKind::Opened;
            ConnectionId connection = 0;
            /// The text of a message; for `Opened` the client's address (empty when the hub is not to apply per-address rules), so the queue's
            /// byte count covers it
            String text;
        };

        /// Puts an event in the queue (the reader thread of a connection); waits while the queue is full. False when the server is stopping
        bool PushEvent(EventKind kind, ConnectionId connection, String text);

        /// What the hub thread knows about the life of one connection
        struct Liveness {
            uint64_t bytesReceived = 0;  // the counter when it last changed
            RoomTime lastLife;           // when anything last arrived
            bool pingSent = false;       // a ping went out since then
        };
        using LivenessMap = HashMap<ConnectionId, Liveness>;

        [[nodiscard]] WebSocketHandler MakeHandler();
        void RunHub();

        /// The first part of Stop() with a grace: queues close 1001 `server_stopping` on every open connection (and, through `_draining`, on one that opens meanwhile)
        /// and waits until all have ended or `shutdownGrace` has passed
        void DrainConnections();

        /// Logs the stats line when ShouldLogRoomServerStats() says so (hub thread, on the tick)
        void LogStats(const RoomHub& hub, RoomServerStats& lastLogged, RoomTime& lastLoggedAt, RoomTime now) const;

        /// Pings the connections that went quiet and drops the ones that did not answer (hub thread, on the tick)
        void CheckLiveness(LivenessMap& liveness, RoomTime now);

        /// The HubSink
        void SendTo(ConnectionId connection, String text);
        void CloseConnection(ConnectionId connection, const String& reason);

        [[nodiscard]] SharedPtr<WebSocketConnection> FindConnection(ConnectionId connection);
        [[nodiscard]] RoomTime Now() const;

        const SeatedGameRegistry& _registry;
        const RoomServerSettings _settings;
        const bool _passAddress;  // the hub applies per-address rules: a limit of sockets per address, or an address behind trusted proxies
        const std::function<RoomTime()> _clock;
        const RandomFn _random;

        Mutex _connectionsMutex{BE_LOCK_SITE("Room connections")};
        HashMap<ConnectionId, SharedPtr<WebSocketConnection>> _connections;  // open connections by id; guarded by _connectionsMutex
        bool _draining = false;                                              // Stop() is telling the clients; guarded by _connectionsMutex
        ConditionVariable _connectionsChanged;                               // a connection ended (with _connectionsMutex); Stop() waits here during the drain

        Mutex _queueMutex{BE_LOCK_SITE("Room event queue")};  // guards the queue, its byte count and _stopping
        ConditionVariable _hubWake;                           // the hub thread waits here for an event, the tick or the stop
        ConditionVariable _notFull;                           // a reader waits here while the queue is full
        eastl::deque<Event> _queue;
        size_t _queueBytes = 0;
        bool _stopping = false;
        Thread _hubThread;

        WebSocketListener _listener;  // last: it is destroyed first, and its threads use the members above
    };

}  // namespace BECore
