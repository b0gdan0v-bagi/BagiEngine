#pragma once

#include <BECore/Control/Json.h>
#include <Rooms/RoomLimits.h>
#include <Rooms/RoomTypes.h>

namespace BECore {

    /// The version of the room protocol (`"v"` of every message). A change that an old client cannot read bumps it.
    inline constexpr int64_t kRoomProtocolVersion = 1;

    /// The `type` of every message of the protocol
    namespace RoomMessageTypes {
        // client to server
        inline constexpr StringView kCreateRoom = "create_room";
        inline constexpr StringView kJoinRoom = "join_room";
        inline constexpr StringView kChooseSeat = "choose_seat";
        inline constexpr StringView kStart = "start";
        inline constexpr StringView kAction = "action";
        inline constexpr StringView kLeave = "leave";
        inline constexpr StringView kPing = "ping";
        inline constexpr StringView kListRooms = "list_rooms";
        // server to client
        inline constexpr StringView kJoined = "joined";
        inline constexpr StringView kLobby = "lobby";
        inline constexpr StringView kView = "view";
        inline constexpr StringView kError = "error";
        inline constexpr StringView kLeft = "left";
        inline constexpr StringView kPong = "pong";
        inline constexpr StringView kRooms = "rooms";
    }  // namespace RoomMessageTypes

    // ---- Client to server -------------------------------------------------------------------------------------------------------

    /// `create_room {game, name, options?, public?}`: a new room; the sender takes the lowest seat and is the host
    struct CreateRoomMessage {
        String game;
        /// Already sanitised (SanitizePlayerName)
        String name;
        /// An object (empty when the client sent none); the game reads it in Start(), untrusted
        Json options = Json::object();
        /// The host's wish that the room is listed for `list_rooms`; absent means false (the code is then known to its members only)
        bool isPublic = false;
    };

    /// `join_room {code, name}` (a new member) or `join_room {code, token}` (a member returns). Exactly one of name and token is set.
    struct JoinRoomMessage {
        /// Normalised (upper case, NormalizeRoomCode)
        String code;
        /// Sanitised; empty for a reconnect
        String name;
        /// Well-formed (IsWellFormedToken); empty for a new member
        String token;

        [[nodiscard]] bool IsReconnect() const {
            return !token.empty();
        }
    };

    /// `choose_seat {seat}`: any integer; the room says `invalid_seat` for one out of range
    struct ChooseSeatMessage {
        int64_t seat = 0;
    };

    /// `start {}`: the host starts the game
    struct StartMessage {};

    /// `action {version, action}`: @c version is the game version the client saw; @c action is an object for the game, untrusted
    struct ActionMessage {
        uint64_t version = 0;
        Json action = Json::object();
    };

    /// `leave {}`
    struct LeaveMessage {};

    /// `ping {}`
    struct PingMessage {};

    /// `list_rooms {game}`: the public rooms of one game that a connection in no room could join
    struct ListRoomsMessage {
        String game;
    };

    using ClientMessage = Variant<CreateRoomMessage, JoinRoomMessage, ChooseSeatMessage, StartMessage, ActionMessage, LeaveMessage, PingMessage, ListRoomsMessage>;

    /**
     * @brief Reads one text message of a client. Never throws, aborts or asserts, whatever the bytes are.
     *
     * Checks in this order: the size (@p maxBytes; `message_too_large`), no raw NUL byte (the JSON lexer takes it for the end of the
     * text and would ignore what follows; `bad_message`), the text is a JSON object (`bad_message`), `v` is the integer
     * kRoomProtocolVersion (a missing or non-integer `v` is `bad_message`, another number `unsupported_version`), `type` is one of the
     * eight (`bad_message`), then each field of that type. Members that the type does not read are ignored.
     *
     * Errors by field: a missing or wrong-typed field, a fractional or huge number, a negative `action.version`, an `options` that is
     * not an object, a `public` that is not a boolean, a `game` that is empty or longer than kMaxGameNameBytes, a `join_room` with both or neither of `name` and
     * `token`: `bad_message`. A name that SanitizePlayerName refuses: `bad_name`. A `code` that is not a room code: `unknown_room`
     * (a code that could not exist is the same answer as one that does not). A `token` that is not well formed: `bad_token`.
     *
     * @param text The message exactly as it came from the transport.
     * @param maxBytes The size limit (kMaxMessageBytes by default).
     */
    [[nodiscard]] std::expected<ClientMessage, ControlError> ParseClientMessage(StringView text, size_t maxBytes = RoomLimits::kMaxMessageBytes);

    /// The `type` of a parsed message
    [[nodiscard]] StringView GetClientMessageType(const ClientMessage& message);

    /// The text a client sends for @p message (the inverse of ParseClientMessage; for clients and tests)
    [[nodiscard]] String SerializeClientMessage(const ClientMessage& message);

    /**
     * @brief A player name as the server keeps it, or nullopt when it cannot be one.
     *
     * Valid UTF-8 (no overlong forms, surrogates or values above U+10FFFF), no control characters (U+0000 to U+001F, U+007F to U+009F,
     * the line and paragraph separators, zero-width and bidirectional control characters, the byte order mark), then the spaces at
     * both ends removed, and kMinNameBytes to kMaxNameBytes bytes left.
     */
    [[nodiscard]] Optional<String> SanitizePlayerName(StringView text);

    // ---- Server to client -------------------------------------------------------------------------------------------------------

    /// One seat in a `lobby` message
    struct LobbySeat {
        int seat = 0;
        SeatKind kind = SeatKind::Empty;
        /// The name of a human; not sent for the other kinds
        String name;
        /// Whether the human's connection is up; not sent for the other kinds
        bool connected = false;
    };

    /// What a `lobby` message says, as plain data (the protocol knows nothing of the Room class that fills it)
    struct LobbySnapshot {
        String room;
        String game;
        RoomPhase phase = RoomPhase::Lobby;
        /// The seat of the host, or -1 when no human is left
        int hostSeat = -1;
        Vector<LobbySeat> seats;
        /// The public options of the room (`SeatedGameInfo::publicOptions`): an object of booleans and integers, empty when the game
        /// names none. Not written when empty; a reader keeps only boolean and integer members.
        Json options = Json::object();
    };

    /// One room in a `rooms` message
    struct RoomListing {
        /// The code: the key of `join_room`, public by the host's choice
        String room;
        /// The name of the host
        String host;
        /// The humans in the room
        int taken = 0;
        /// The seat count of the game
        int seats = 0;
        bool started = false;
        /// The public options of the room, as the `lobby` message carries them; not written when empty
        Json options = Json::object();
    };

    /// What a `rooms` message says: the answer to `list_rooms`, as plain data
    struct RoomListMessage {
        String game;
        /// At most RoomLimits::kMaxListedRooms, the oldest room first
        Vector<RoomListing> rooms;
        /// The answer was cut at the cap: more rooms are open
        bool more = false;
    };

    /// `joined {room, player, token, seat, host}`. The token is a secret: this message goes to its owner only.
    [[nodiscard]] String MakeJoinedMessage(StringView room, PlayerId player, StringView token, int seat, bool host);

    /// `lobby {room, game, phase, host, seats:[{seat, kind, name?, connected?}], options?}`; to every member when it changes
    [[nodiscard]] String MakeLobbyMessage(const LobbySnapshot& lobby);

    /// `view {room, seat, version, finished, view, actions}`: what the game says the seat sees and may send now (no hidden information
    /// of the others: the game's contract)
    [[nodiscard]] String MakeViewMessage(StringView room, int seat, uint64_t version, bool finished, const Json& view, const Json& actions);

    /// `rooms {game, rooms:[{room, host, taken, seats, started, options?}], more}`: the answer to `list_rooms`
    [[nodiscard]] String MakeRoomListMessage(const RoomListMessage& list);

    /// `error {code, message, ref?}`; @p ref is the type of the message it answers (left out when empty)
    [[nodiscard]] String MakeErrorMessage(StringView code, StringView message, StringView ref = {});

    /// `left {reason}`: the sender is no longer in the room
    [[nodiscard]] String MakeLeftMessage(StringView reason);

    /// `pong {}`
    [[nodiscard]] String MakePongMessage();

    // ---- Server to client, read by a client -------------------------------------------------------------------------------------

    /// `joined`, as the client reads it
    struct JoinedMessage {
        String room;
        PlayerId player = 0;
        /// A secret of this player; well-formed (IsWellFormedToken)
        String token;
        int seat = 0;
        bool host = false;
    };

    /// `lobby`, as the client reads it
    using LobbyMessage = LobbySnapshot;

    /// `view`, as the client reads it. @c view and @c actions are the game's own JSON, kept as the server sent them.
    struct ViewMessage {
        String room;
        int seat = 0;
        uint64_t version = 0;
        bool finished = false;
        Json view = Json::object();
        Json actions = Json::array();
    };

    /// `error`
    struct ErrorMessage {
        String code;
        String message;
        /// The type of the message it answers; empty when the server gave none
        String ref;
    };

    /// `left`
    struct LeftMessage {
        String reason;
    };

    /// `pong`
    struct PongMessage {};

    /// A message with a good `v` and a `type` this client does not know (a newer server may add some); the client ignores it
    struct UnknownServerMessage {
        String type;
    };

    using ServerMessage = Variant<JoinedMessage, LobbyMessage, ViewMessage, ErrorMessage, LeftMessage, PongMessage, RoomListMessage, UnknownServerMessage>;

    /**
     * @brief Reads one text message of the server (the inverse of the Make*Message builders). Never throws, aborts or asserts.
     *
     * Checks in this order: the size (@p maxBytes; `message_too_large`), no raw NUL byte, nesting not deeper than kMaxServerJsonDepth (`bad_message`), a JSON object, `v` an integer
     * (`bad_message`; another number than kRoomProtocolVersion is `unsupported_version`), `type` a string, then the fields of that
     * type. A missing or wrong-typed field, a fractional or out-of-range number, an unknown `phase` or seat `kind`, a token that is
     * not well formed, more than kMaxLobbySeats seats are `bad_message`. In `rooms`: more than kMaxListedRooms entries, a code that
     * NormalizeRoomCode does not keep as it is, `seats` outside 1..kMaxLobbySeats, `taken` outside 0..`seats`, `options` that is not an
     * object of at most kMaxLobbyOptions booleans and integers are `bad_message` too. Members the type does not define are ignored. An unknown
     * `type` is UnknownServerMessage. A string is copied, never interned into a PoolString.
     */
    [[nodiscard]] std::expected<ServerMessage, ControlError> ParseServerMessage(StringView text, size_t maxBytes = RoomLimits::kMaxServerMessageBytes);

    /// The `type` of a parsed server message (for the log)
    [[nodiscard]] StringView GetServerMessageType(const ServerMessage& message);

}  // namespace BECore
