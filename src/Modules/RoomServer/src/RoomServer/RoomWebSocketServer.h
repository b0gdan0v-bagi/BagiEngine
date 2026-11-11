#pragma once

#include <BECore/Control/ControlTypes.h>
#include <RemoteControl/WebSocketListener.h>
#include <RoomServer/RoomServerSettings.h>
#include <Rooms/RoomHub.h>

namespace BECore {

    class SeatedGameRegistry;

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
     * The queue holds at most kMaxQueuedEvents events and kMaxQueuedBytes of text: a reader that finds it full waits, so TCP slows
     * the fast sender and the memory stays bounded.
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

        /// Ends every connection, joins every thread. Safe to call twice and when never started. After it returns no callback runs and nothing is sent
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
            eastl::string text;
        };

        /// Puts an event in the queue (the reader thread of a connection); waits while the queue is full. False when the server is stopping
        bool PushEvent(EventKind kind, ConnectionId connection, eastl::string text);

        [[nodiscard]] WebSocketHandler MakeHandler();
        void RunHub();

        /// The HubSink
        void SendTo(ConnectionId connection, eastl::string text);
        void CloseConnection(ConnectionId connection, const eastl::string& reason);

        [[nodiscard]] eastl::shared_ptr<WebSocketConnection> FindConnection(ConnectionId connection);
        [[nodiscard]] RoomTime Now() const;

        const SeatedGameRegistry& _registry;
        const RoomServerSettings _settings;
        const std::function<RoomTime()> _clock;
        const RandomFn _random;

        std::mutex _connectionsMutex;
        eastl::hash_map<ConnectionId, eastl::shared_ptr<WebSocketConnection>> _connections;  // open connections by id; guarded by _connectionsMutex

        std::mutex _queueMutex;            // guards the queue, its byte count and _stopping
        std::condition_variable _hubWake;  // the hub thread waits here for an event, the tick or the stop
        std::condition_variable _notFull;  // a reader waits here while the queue is full
        eastl::deque<Event> _queue;
        size_t _queueBytes = 0;
        bool _stopping = false;
        std::thread _hubThread;

        WebSocketListener _listener;  // last: it is destroyed first, and its threads use the members above
    };

}  // namespace BECore
