#pragma once

#include <RoomClient/RoomTransport.h>
#include <Rooms/RoomProtocol.h>

namespace BECore {

    /// Error codes of the commands of RoomClient (the server's own codes come in events)
    namespace RoomClientErrors {
        /// The command does not fit the state of the client (Host while in a room, ChooseSeat without one, ...)
        inline constexpr eastl::string_view kWrongState = "wrong_state";
        /// No transport can be made here (no factory in the settings and no default one)
        inline constexpr eastl::string_view kTransportUnavailable = "transport_unavailable";
        /// The transport refused to start connecting (a bad address, `wss://` on the desktop); the message says why
        inline constexpr eastl::string_view kConnectFailed = "connect_failed";
        /// An event: a text from the server that is not a well-formed message (it is dropped)
        inline constexpr eastl::string_view kBadServerMessage = "bad_server_message";
    }  // namespace RoomClientErrors

    /// How many events wait for DrainEvents() before the oldest is dropped
    inline constexpr size_t kMaxQueuedRoomClientEvents = 256;

    struct RoomClientSettings {
        /// Makes the transport of a connection; empty uses MakeDefaultRoomTransport()
        RoomTransportFactory transportFactory;
    };

    /// Where the client is in its life
    CORE_ENUM(RoomClientState, uint8_t,
              Disconnected,  // no connection
              Connecting,    // the transport is opening
              Connected,     // the connection is open, no room (before `joined`, after `left`, after an error of a Host / Join)
              InRoom         // `joined` came: the lobby and the view follow
    )

    CORE_ENUM(RoomClientEventKind, uint8_t,
              Connected,     // the transport opened
              Joined,        // `joined`: a room was created or entered
              LobbyChanged,  // GetLobby() changed
              ViewChanged,   // GetView() / GetActions() changed
              Error,         // `error` from the server, or a bad server message: code, message, ref
              Left,          // `left`: no longer in the room, code is the reason
              Closed         // the connection ended: closeCode, message is the reason
    )

    /// One thing the game may want to show or react to
    struct RoomClientEvent {
        RoomClientEventKind kind = RoomClientEventKind::Closed;
        /// Error: the code. Left: the reason
        eastl::string code;
        /// Error: the server's text. Closed: the reason of the close
        eastl::string message;
        /// Error: the type of the message it answers; empty when the server gave none
        eastl::string ref;
        /// Closed: the code of the close
        uint16_t closeCode = kRoomTransportAbnormalClosure;
    };

    /// What it takes to come back into a room after the client was closed: plain data the game can save. **The token is the player's
    /// secret** (whoever has it takes the seat); the server keeps it only while the player has not left.
    struct RoomSession {
        eastl::string url;
        eastl::string room;
        eastl::string token;

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
     * (Connected); the end of the connection is the event `Closed` and the state Disconnected, with the room forgotten but the session
     * kept. Leave() asks to leave the room, Disconnect() closes.
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
        /// object). `wrong_state` unless Disconnected, or Connected to the same @p url with no room and no request pending
        [[nodiscard]] std::expected<void, ControlError> Host(eastl::string_view url, eastl::string_view game, eastl::string_view name, const Json& options = Json::object());

        /// Opens a connection to @p url and enters the room @p code (any case) as a new member named @p name. Same states as Host().
        /// A code that cannot be a room code is `unknown_room` at once, without a connection
        [[nodiscard]] std::expected<void, ControlError> Join(eastl::string_view url, eastl::string_view code, eastl::string_view name);

        /// Asks for a seat. In a room only (`wrong_state`). The lobby message does not say which seat is the receiver's, so GetSeat() takes the
        /// asked seat at once and goes back to the old one if the server answers with an error; a `view` always corrects it
        [[nodiscard]] std::expected<void, ControlError> ChooseSeat(int seat);

        /// The host asks to start the game. In a room only
        [[nodiscard]] std::expected<void, ControlError> Start();

        /// Sends @p action to the game, stamped with the version of the last `view` (the server answers `stale_action` when the game moved
        /// on). `wrong_state` without a room and a view. The client never sends an action again by itself
        [[nodiscard]] std::expected<void, ControlError> SendAction(const Json& action);

        /// Asks to leave the room; the connection stays open and `left` follows. The session is forgotten at once. In a room only
        [[nodiscard]] std::expected<void, ControlError> Leave();

        /// Closes the connection (gracefully) in any state and forgets the room but not the session. `Closed` is queued at once, the
        /// state is Disconnected, and a new Host() may follow without waiting for the old connection to finish
        void Disconnect();

        // ---- Polling ----

        /// Takes everything the transport has: parses it, updates the state and queues events. @p now is the time of the frame
        void Poll(RoomTime now);

        /// Poll() with the steady clock
        void Poll();

        /// Appends the queued events to @p out in the order they happened and forgets them. Keep @p out as a member so it keeps its capacity
        void DrainEvents(eastl::vector<RoomClientEvent>& out);

        // ---- State ----

        [[nodiscard]] RoomClientState GetState() const {
            return _state;
        }

        [[nodiscard]] bool IsInRoom() const {
            return _state == RoomClientState::InRoom;
        }

        /// The code of the room; empty when not in one
        [[nodiscard]] const eastl::string& GetRoom() const {
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

        /// The room, the token and the address, valid from `joined` until `left` or Leave(); not valid before. The game saves it
        [[nodiscard]] const RoomSession& GetSession() const {
            return _session;
        }

        /// The last error: a server `error`, a bad server message, or a connect that failed (empty code when none)
        [[nodiscard]] const ControlError& GetLastError() const {
            return _lastError;
        }

    private:
        [[nodiscard]] std::expected<void, ControlError> BeginRequest(eastl::string_view url, eastl::string firstMessage);
        [[nodiscard]] std::expected<void, ControlError> RequireRoom() const;
        std::expected<void, ControlError> SendToServer(const ClientMessage& message);

        void HandleTransportEvent(const RoomTransportEvent& event);
        void HandleText(eastl::string_view text);
        void OnJoined(JoinedMessage&& message);
        void OnLobby(LobbyMessage&& message);
        void OnView(ViewMessage&& message);
        void OnError(ErrorMessage&& message);
        void OnLeft(LeftMessage&& message);
        void OnTransportClosed(const RoomTransportEvent& event);
        void ForgetRoom();
        void Retire();

        void Emit(RoomClientEventKind kind, eastl::string_view code = {}, eastl::string_view message = {}, eastl::string_view ref = {}, uint16_t closeCode = kRoomTransportAbnormalClosure);

        RoomClientSettings _settings;

        eastl::unique_ptr<IRoomTransport> _transport;
        /// A transport that was told to close: polled until its Closed, then freed
        eastl::unique_ptr<IRoomTransport> _retired;
        RoomClientState _state = RoomClientState::Disconnected;
        eastl::string _url;
        /// Sent when the transport opens
        eastl::string _firstMessage;
        /// A create_room or join_room was sent and has no answer (`joined` or `error`) yet
        bool _awaitingJoin = false;

        eastl::string _room;
        PlayerId _player = 0;
        int _seat = -1;
        int _seatBeforeRequest = -1;
        bool _host = false;
        LobbySnapshot _lobby;
        bool _hasView = false;
        Json _view = Json::object();
        Json _actions = Json::array();
        uint64_t _viewVersion = 0;
        bool _finished = false;
        RoomSession _session;
        ControlError _lastError;

        eastl::vector<RoomClientEvent> _events;
        /// Where PollEvent() writes: a member, so the buffers keep their capacity
        RoomTransportEvent _polled;
    };

}  // namespace BECore
