#pragma once

#include <Rooms/RateLimiter.h>
#include <Rooms/Room.h>
#include <Rooms/RoomLimits.h>

namespace BECore {

    /// Where the hub's output goes. Both calls are made from inside OnConnected / OnMessage / OnDisconnected / Tick, on the caller's
    /// thread; a sink must not call back into the hub.
    struct HubSink {
        /// A text message for a connection
        std::function<void(ConnectionId, eastl::string)> send;
        /// The hub is done with the connection: the transport closes it (and need not report it with OnDisconnected, which would be
        /// ignored). The reason is for the log: `rate_limited`, `too_many_failed_joins`, `idle`, `replaced`, `server_full`.
        std::function<void(ConnectionId, eastl::string)> close;
    };

    /**
     * @brief The dispatcher of a room server: connections in, rooms behind them, texts out.
     *
     * Text in (OnMessage), text out (HubSink). No socket, no thread, no clock and no randomness of its own: the transport reports
     * connections and messages, the caller passes the time, the RandomFn makes room codes and seeds. **Single-threaded**: the caller
     * serializes every call (slice 2 posts transport events to a queue that one thread drains, like ControlService).
     *
     * Nothing a client sends is trusted, and a handler never throws: every failure is an `error` message with a stable code
     * (RoomErrors). After every handled message and every Tick the hub flushes: for each member of the affected room it compares the
     * lobby revision and the game version with what that member was last sent, and sends `lobby` and `view` only when they moved, so
     * the room logic never names who gets what.
     *
     * The order of messages a client sees: `joined` first (to its owner only: it holds the secret token), then `lobby`, then `view`
     * (only once the game has started). An `error` answers the message that failed; a `stale_action` error is followed by the
     * current `view`. `left` is the last message before a connection is out of a room.
     */
    class RoomHub {
    public:
        /// @param registry Must outlive the hub; rooms create their games through it.
        RoomHub(const SeatedGameRegistry& registry, RoomHubSettings settings, RandomFn random, HubSink sink);

        /// A transport connection opened. Over the connection limit it is closed at once (`server_full`).
        void OnConnected(ConnectionId connection, RoomTime now);

        /// A text message of a connection (untrusted, any bytes). An unknown connection is ignored.
        void OnMessage(ConnectionId connection, eastl::string_view text, RoomTime now);

        /// The connection dropped. In a lobby its member is removed; in a game the member is marked away (the grace, then a
        /// computer). An unknown connection (one the hub closed itself) is ignored.
        void OnDisconnected(ConnectionId connection, RoomTime now);

        /// Time passes: idle connections are closed, computers move (at most one move per room per call, and per `computerDelay`),
        /// rooms that are abandoned or have been finished for the abandoned timeout are closed, and what changed is sent.
        void Tick(RoomTime now);

        [[nodiscard]] size_t GetRoomCount() const {
            return _rooms.size();
        }

        [[nodiscard]] size_t GetConnectionCount() const {
            return _connections.size();
        }

        [[nodiscard]] bool HasRoom(eastl::string_view code) const;

    private:
        struct Connection {
            RateLimiter limiter;
            RoomTime lastActivity{};
            int rateLimitedInRow = 0;
            int failedJoinsInRow = 0;
            /// The room it is in, empty when none
            eastl::string roomCode;
            PlayerId player = 0;
            /// What this connection was last sent (0 and false: nothing yet)
            uint64_t sentLobbyRevision = 0;
            bool sentView = false;
            uint64_t sentGameVersion = 0;
            int sentSeat = -1;
        };

        struct RoomEntry {
            eastl::unique_ptr<Room> room;
            /// The `options` of `create_room`, handed to the game at `start`
            Json options = Json::object();
            /// The connections of the members that are connected, in the order they joined
            eastl::vector<ConnectionId> connections;
            /// Since when the game is finished (valid when finished is true)
            bool finished = false;
            RoomTime finishedAt{};
        };

        void HandleCreate(ConnectionId id, Connection& connection, const CreateRoomMessage& message, RoomTime now);
        void HandleJoin(ConnectionId id, Connection& connection, const JoinRoomMessage& message, RoomTime now);
        void HandleChooseSeat(ConnectionId id, Connection& connection, const ChooseSeatMessage& message, RoomTime now);
        void HandleStart(ConnectionId id, Connection& connection, RoomTime now);
        void HandleAction(ConnectionId id, Connection& connection, const ActionMessage& message, RoomTime now);
        void HandleLeave(ConnectionId id, Connection& connection, RoomTime now);

        /// Sends `error`; counts a failed join and closes the connection when the limit is reached
        void SendError(ConnectionId id, const ControlError& error, eastl::string_view ref);
        void SendError(ConnectionId id, eastl::string_view code, eastl::string_view message, eastl::string_view ref);
        void SendText(ConnectionId id, eastl::string text);
        void FailJoin(ConnectionId id, Connection& connection, const ControlError& error, RoomTime now);

        /// Sends `lobby` and `view` to every connection of the room whose copy is out of date
        void Flush(RoomEntry& entry, RoomTime now);
        /// Sends the current view to one member again (after `stale_action`)
        void ResendView(ConnectionId id, Connection& connection, RoomEntry& entry);

        RoomEntry* FindRoom(eastl::string_view code);
        void Detach(ConnectionId id, Connection& connection);
        /// Forgets the connection: its member is removed (lobby) or marked away (game), then the room is closed or flushed
        void DropConnection(ConnectionId id, RoomTime now);
        /// DropConnection, then tells the transport to close it
        void CloseConnection(ConnectionId id, eastl::string_view reason, RoomTime now);
        void CloseRoom(const eastl::string& code, eastl::string_view reason, RoomTime now);
        /// Closes the room when the room says so; true when it did
        bool CloseIfDone(const eastl::string& code, RoomTime now);
        [[nodiscard]] eastl::string MakeUnusedRoomCode();

        const SeatedGameRegistry& _registry;
        RoomHubSettings _settings;
        RandomFn _random;
        HubSink _sink;
        eastl::hash_map<ConnectionId, Connection> _connections;
        eastl::hash_map<eastl::string, eastl::unique_ptr<RoomEntry>> _rooms;
    };

}  // namespace BECore
