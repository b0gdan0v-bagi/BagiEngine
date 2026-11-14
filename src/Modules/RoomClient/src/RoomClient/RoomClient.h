#pragma once

#include <RoomClient/RoomTransport.h>
#include <Rooms/RoomProtocol.h>

namespace BECore {

    /// Error codes of the commands of RoomClient (the server's own codes come in events)
    namespace RoomClientErrors {
        /// The command does not fit the state of the client (Host while in a room, ChooseSeat without one, ...)
        inline constexpr StringView kWrongState = "wrong_state";
        /// No transport can be made here (no factory in the settings and no default one)
        inline constexpr StringView kTransportUnavailable = "transport_unavailable";
        /// The transport refused to start connecting (a bad address, `wss://` on a desktop platform without a TLS backend); the message says why
        inline constexpr StringView kConnectFailed = "connect_failed";
        /// An event: a text from the server that is not a well-formed message (it is dropped)
        inline constexpr StringView kBadServerMessage = "bad_server_message";
        /// Resume() was given a session that cannot be one (no room, no token, a malformed code or token)
        inline constexpr StringView kInvalidSession = "invalid_session";
    }  // namespace RoomClientErrors

    /// Why a connection ended for good, as the `code` of the event `Closed` (empty: an ordinary close, the message says what the transport saw)
    namespace RoomClientCloseReasons {
        /// The server does not know the room or the token any more (`unknown_room`, `bad_token`): it was closed, or the player left
        inline constexpr StringView kRoomGone = "room_gone";
        /// Another device took the seat (`left {replaced}`); this one does not fight for it
        inline constexpr StringView kReplaced = "replaced";
        /// The server closed with 1008 (the code is the server's own reason, `rate_limited` or `too_many_failed_joins`, when it gave one):
        /// the client broke its limits, and trying again would be the same abuse
        inline constexpr StringView kRateLimited = "rate_limited";
        /// The connection was not back within `reconnectGiveUp`
        inline constexpr StringView kGaveUp = "gave_up";
        /// The first connection did not open within `connectTimeout`
        inline constexpr StringView kConnectTimeout = "connect_timeout";
    }  // namespace RoomClientCloseReasons

    /// How many events wait for DrainEvents() before the oldest is dropped
    inline constexpr size_t kMaxQueuedRoomClientEvents = 256;

    /// The defaults are the numbers of the design (Decisions of slice 3); a test passes small ones
    struct RoomClientSettings {
        /// Makes the transport of a connection; empty uses MakeDefaultRoomTransport()
        RoomTransportFactory transportFactory;
        /// Nothing came for this long: a `ping` goes out (one at a time). 0 turns the heartbeat off
        std::chrono::milliseconds pingInterval = std::chrono::seconds(10);
        /// Nothing at all came for this long: the connection is lost. 0 turns the check off
        std::chrono::milliseconds silenceLimit = std::chrono::seconds(35);
        /// A transport that has not opened after this long counts as lost (or as a failed connect, outside a room)
        std::chrono::milliseconds connectTimeout = std::chrono::seconds(15);
        /// The wait before the first new connection after a loss; it doubles for every attempt that fails, up to backoffMax
        std::chrono::milliseconds backoffStart = std::chrono::milliseconds(500);
        std::chrono::milliseconds backoffMax = std::chrono::seconds(4);
        /// The client stops trying this long after the first loss (the server's abandoned-room time)
        std::chrono::milliseconds reconnectGiveUp = std::chrono::minutes(10);
        /// Whether a connection lost in a room is brought back by itself
        bool reconnect = true;
    };

    /// Where the client is in its life
    CORE_ENUM(RoomClientState, uint8_t,
              Disconnected,  // no connection
              Connecting,    // the transport is opening
              Connected,     // the connection is open, no room (before `joined`, after `left`, after an error of a Host / Join)
              InRoom,        // `joined` came: the lobby and the view follow
              Reconnecting   // the connection of a room was lost (or Resume() is under way): waiting to retry, or the new one is opening
    )

    CORE_ENUM(RoomClientEventKind, uint8_t,
              Connected,       // the transport opened
              Joined,          // `joined`: a room was created or entered
              LobbyChanged,    // GetLobby() changed
              ViewChanged,     // GetView() / GetActions() changed
              Error,           // `error` from the server, or a bad server message: code, message, ref
              Left,            // `left`: no longer in the room, code is the reason
              ConnectionLost,  // the connection of a room ended: willRetry says whether the client will bring it back (else Closed follows)
              Resumed,         // the new connection is in the room again (`joined` by token); the lobby and the view follow
              Closed,          // the connection ended for good: code is the reason (RoomClientCloseReasons), closeCode, message
              RoomList         // `rooms`: GetRoomList() was replaced by the answer to ListRooms()
    )

    /// One thing the game may want to show or react to
    struct RoomClientEvent {
        RoomClientEventKind kind = RoomClientEventKind::Closed;
        /// Error: the code. Left: the reason. Closed: why it ended for good (RoomClientCloseReasons or an error code of the server), empty for an ordinary close
        String code;
        /// Error: the server's text. Closed, ConnectionLost: the reason of the close
        String message;
        /// Error: the type of the message it answers; empty when the server gave none
        String ref;
        /// Closed, ConnectionLost: the code of the close
        uint16_t closeCode = kRoomTransportAbnormalClosure;
        /// ConnectionLost: the client will reconnect by itself
        bool willRetry = false;
    };

    /// What it takes to come back into a room after the client was closed: plain data the game can save. **The token is the player's
    /// secret** (whoever has it takes the seat); the server keeps it only while the player has not left.
    struct RoomSession {
        String url;
        String room;
        String token;

        [[nodiscard]] bool IsValid() const {
            return !room.empty() && !token.empty();
        }
    };

    /**
     * @brief The client of a room server: hosts or joins a room, follows the lobby and the view, sends the player's actions.
     *
     * A game component owns one and calls Poll() once per frame **on the main thread**, then DrainEvents(); no callback ever comes from
     * a network thread, so nothing is locked. The client keeps `view` and `actions` as the server sent them (Json) and reads no game: the
     * server is authoritative and the client only tells the game when something changed. The commands are asked of the server and their
     * outcome comes as events (`Joined`, `Error`, ...); a command returns an error itself only when it cannot be sent at all.
     *
     * One connection at a time. Host() and Join() open it (or use the open one, after an error or `left`, when the address is the same).
     * After the first message the state goes Connecting, Connected, InRoom. A `left` from the server leaves the connection open
     * (Connected). Leave() asks to leave the room, Disconnect() closes.
     *
     * **Heartbeat and reconnect.** The client sends `ping` after pingInterval of silence and counts the connection lost after silenceLimit
     * of nothing at all (any text is life; the room protocol is used because a browser shows a script no WebSocket ping). A connection
     * lost in a room (closed, failed, silent) goes to Reconnecting: after a backoff a new connection is opened and `join_room` goes out
     * with the token, until `joined` (the event Resumed, then the lobby and the view again), a rule that says stop (RoomClientCloseReasons) or
     * reconnectGiveUp. The old room, lobby and view stay readable while it waits (the game may show them frozen); commands that need the
     * room are `wrong_state` until Resumed. Resume() does the same for a session saved by an earlier run. The end of all this is one event
     * `Closed` and the state Disconnected.
     *
     * Not thread-safe: use it from one thread.
     */
    class RoomClient {
    public:
        explicit RoomClient(RoomClientSettings settings = {});
        ~RoomClient();

        RoomClient(const RoomClient&) = delete;
        RoomClient& operator=(const RoomClient&) = delete;

        // ---- Commands ----

        /// Opens a connection to @p url and creates a room of @p game with the player @p name; @p options go to the game's Start() (an
        /// object). @p isPublic asks the server to list the room for ListRooms() (a server that does not know the member keeps the room
        /// private). `wrong_state` unless Disconnected, or Connected to the same @p url with no room and no request pending
        [[nodiscard]] std::expected<void, ControlError> Host(StringView url, StringView game, StringView name, const Json& options = Json::object(), bool isPublic = false);

        /// Opens a connection to @p url and enters the room @p code (any case) as a new member named @p name. Same states as Host().
        /// A code that cannot be a room code is `unknown_room` at once, without a connection
        [[nodiscard]] std::expected<void, ControlError> Join(StringView url, StringView code, StringView name);

        /// Asks @p url for the public rooms of @p game; the answer is the event `RoomList` and GetRoomList(). Same states as Host(). It is not a
        /// join: the state stays Connected (no room, no reconnect) and the same connection serves a Join() of a listed code or another
        /// ListRooms(). The server answers a list at most once per `RoomLimits::kListRefill` on average (then `Error rate_limited` with
        /// `ref: list_rooms`, the connection stays open), so a screen asks again no faster than that
        [[nodiscard]] std::expected<void, ControlError> ListRooms(StringView url, StringView game);

        /// Asks for a seat. In a room only (`wrong_state`). The lobby message does not say which seat is the receiver's, so GetSeat() takes the
        /// asked seat at once and goes back to the old one if the server answers with an error; a `view` always corrects it
        [[nodiscard]] std::expected<void, ControlError> ChooseSeat(int seat);

        /// The host asks to start the game. In a room only
        [[nodiscard]] std::expected<void, ControlError> Start();

        /// Sends @p action to the game, stamped with the version of the last `view` (the server answers `stale_action` when the game moved
        /// on). `wrong_state` without a room and a view. The client never sends an action again by itself
        [[nodiscard]] std::expected<void, ControlError> SendAction(const Json& action);

        /// Asks to leave the room; the connection stays open and `left` follows. The session is forgotten at once. In a room only; while
        /// Reconnecting there is no connection to ask on, so it is Disconnect() plus the forgotten session
        [[nodiscard]] std::expected<void, ControlError> Leave();

        /// Comes back into the room of a session saved by an earlier run (GetSession()): connects to its address and joins by token. The
        /// state is Reconnecting at once, so the retry and give-up rules of a lost connection apply; `Resumed` is the success and
        /// `Closed {room_gone}` the answer of a server that no longer has the room or the token. Only Disconnected; `invalid_session`
        /// for a session that cannot be one, and the errors of Host() for a connection that cannot start
        [[nodiscard]] std::expected<void, ControlError> Resume(const RoomSession& session);

        /// Closes the connection (gracefully) in any state, stops any retry, and forgets the room but not the session. `Closed` is queued
        /// at once, the state is Disconnected, and a new Host() may follow without waiting for the old connection to finish
        void Disconnect();

        // ---- Polling ----

        /// Takes everything the transport has: parses it, updates the state and queues events, and runs the clocks (heartbeat, connect
        /// timeout, backoff, give-up) against @p now, the time of the frame
        void Poll(RoomTime now);

        /// Poll() with the steady clock
        void Poll();

        /// Appends the queued events to @p out in the order they happened and forgets them. Keep @p out as a member so it keeps its capacity
        void DrainEvents(Vector<RoomClientEvent>& out);

        // ---- State ----

        [[nodiscard]] RoomClientState GetState() const {
            return _state;
        }

        [[nodiscard]] bool IsInRoom() const {
            return _state == RoomClientState::InRoom;
        }

        /// The code of the room; empty when not in one
        [[nodiscard]] const String& GetRoom() const {
            return _room;
        }

        /// The room's number for this player (0 when not in a room)
        [[nodiscard]] PlayerId GetPlayer() const {
            return _player;
        }

        /// The seat of this player (see ChooseSeat() for the lobby); -1 when not in a room
        [[nodiscard]] int GetSeat() const {
            return _seat;
        }

        /// The last `rooms` answer; empty before the first one, replaced by each answer, cleared when the room or the connection is forgotten
        [[nodiscard]] const RoomListMessage& GetRoomList() const {
            return _roomList;
        }

        /// Whether this player is the host: the seat the lobby names as the host's
        [[nodiscard]] bool IsHost() const {
            return _host;
        }

        /// The last `lobby`; empty (no seats) when none has come
        [[nodiscard]] const LobbySnapshot& GetLobby() const {
            return _lobby;
        }

        /// Whether a `view` has come since the room was entered
        [[nodiscard]] bool HasView() const {
            return _hasView;
        }

        /// What the game says this seat sees and may send, as the server sent it; empty objects before the first `view`
        [[nodiscard]] const Json& GetView() const {
            return _view;
        }

        [[nodiscard]] const Json& GetActions() const {
            return _actions;
        }

        /// The game version of the last `view` (what SendAction stamps); 0 before the first
        [[nodiscard]] uint64_t GetViewVersion() const {
            return _viewVersion;
        }

        /// Whether the game of the last `view` is over
        [[nodiscard]] bool IsFinished() const {
            return _finished;
        }

        /// The room, the token and the address, valid from `joined` until `left` or Leave(); not valid before. It survives a lost connection
        /// and Disconnect() (that is its use). The game saves it to Resume() later; it is a secret
        [[nodiscard]] const RoomSession& GetSession() const {
            return _session;
        }

        /// The last error: a server `error`, a bad server message, or a connect that failed (empty code when none)
        [[nodiscard]] const ControlError& GetLastError() const {
            return _lastError;
        }

    private:
        /// @p awaitsJoin: the request is a `create_room` / `join_room` (its answer is `joined` or `error`); a `list_rooms` is not
        [[nodiscard]] std::expected<void, ControlError> BeginRequest(StringView url, String firstMessage, bool awaitsJoin = true);
        /// Makes and opens a transport for @p url; @p firstMessage goes out when it opens
        [[nodiscard]] std::expected<void, ControlError> StartConnection(StringView url, String firstMessage, bool awaitsJoin = true);
        [[nodiscard]] std::expected<void, ControlError> RequireRoom() const;
        std::expected<void, ControlError> SendToServer(const ClientMessage& message);

        void HandleTransportEvent(const RoomTransportEvent& event);
        void HandleText(StringView text);
        void OnJoined(JoinedMessage&& message);
        void OnLobby(LobbyMessage&& message);
        void OnView(ViewMessage&& message);
        void OnError(ErrorMessage&& message);
        void OnLeft(LeftMessage&& message);
        void OnRoomList(RoomListMessage&& message);
        void OnTransportClosed(const RoomTransportEvent& event);
        /// The connection is over (the transport is gone or on its way out): reconnect, or end for good
        void OnConnectionEnded(uint16_t closeCode, StringView reason, StringView endCode = {});
        void ScheduleRetry();
        void StartAttempt();
        /// Ends the client for good: forgets the room, Disconnected, the event Closed
        void GiveUp(StringView code, StringView message, bool forgetSession, uint16_t closeCode = kRoomTransportAbnormalClosure);
        void CheckTimers();
        /// Forgets what belongs to the transport that is gone (the first message, the clocks of the connection)
        void ResetConnection();
        /// Forgets the retry state: the first loss, the next attempt, the failures
        void ClearReconnect();
        void ForgetRoom();
        /// Hands the transport to _retired after a graceful Close()
        void Retire();
        /// Hands the transport to _retired after Abort()
        void RetireAborted();
        [[nodiscard]] std::chrono::milliseconds GetBackoff() const;

        void Emit(RoomClientEventKind kind, StringView code = {}, StringView message = {}, StringView ref = {}, uint16_t closeCode = kRoomTransportAbnormalClosure, bool willRetry = false);

        RoomClientSettings _settings;

        UniquePtr<IRoomTransport> _transport;
        /// A transport that was told to close: polled until its Closed, then freed
        UniquePtr<IRoomTransport> _retired;
        RoomClientState _state = RoomClientState::Disconnected;
        String _url;
        /// Sent when the transport opens
        String _firstMessage;
        /// Whether the first message is a join (sets _awaitingJoin when it goes out)
        bool _firstMessageAwaitsJoin = true;
        /// A create_room or join_room was sent and has no answer (`joined` or `error`) yet
        bool _awaitingJoin = false;
        /// The transport has reported Opened
        bool _transportOpened = false;

        // The clocks, all against the `now` of Poll(); an empty one is armed by the next Poll(), since a command knows no time
        RoomTime _now{};
        /// Since when the transport has been opening
        Optional<RoomTime> _connectStart;
        /// The last time anything came from the server, and whether a `ping` since then is still unanswered
        RoomTime _lastReceived{};
        bool _pingOutstanding = false;
        /// Reconnecting: when the connection was first lost, when the next attempt is due (empty while one is under way), how many failed
        Optional<RoomTime> _lostAt;
        Optional<RoomTime> _retryAt;
        int _failedAttempts = 0;
        /// Why the connection will end for good when the transport's Closed arrives (`replaced`); the code of that Closed event
        String _endReason;

        String _room;
        PlayerId _player = 0;
        int _seat = -1;
        int _seatBeforeRequest = -1;
        bool _host = false;
        LobbySnapshot _lobby;
        RoomListMessage _roomList;
        bool _hasView = false;
        Json _view = Json::object();
        Json _actions = Json::array();
        uint64_t _viewVersion = 0;
        bool _finished = false;
        RoomSession _session;
        ControlError _lastError;

        Vector<RoomClientEvent> _events;
        /// Where PollEvent() writes: a member, so the buffers keep their capacity
        RoomTransportEvent _polled;
    };

}  // namespace BECore
