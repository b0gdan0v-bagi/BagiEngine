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
        inline constexpr eastl::string_view kCreateRoom = "create_room";
        inline constexpr eastl::string_view kJoinRoom = "join_room";
        inline constexpr eastl::string_view kChooseSeat = "choose_seat";
        inline constexpr eastl::string_view kStart = "start";
        inline constexpr eastl::string_view kAction = "action";
        inline constexpr eastl::string_view kLeave = "leave";
        inline constexpr eastl::string_view kPing = "ping";
        // server to client
        inline constexpr eastl::string_view kJoined = "joined";
        inline constexpr eastl::string_view kLobby = "lobby";
        inline constexpr eastl::string_view kView = "view";
        inline constexpr eastl::string_view kError = "error";
        inline constexpr eastl::string_view kLeft = "left";
        inline constexpr eastl::string_view kPong = "pong";
    }  // namespace RoomMessageTypes

    // ---- Client to server -------------------------------------------------------------------------------------------------------

    /// `create_room {game, name, options?}`: a new room; the sender takes the lowest seat and is the host
    struct CreateRoomMessage {
        eastl::string game;
        /// Already sanitised (SanitizePlayerName)
        eastl::string name;
        /// An object (empty when the client sent none); the game reads it in Start(), untrusted
        Json options = Json::object();
    };

    /// `join_room {code, name}` (a new member) or `join_room {code, token}` (a member returns). Exactly one of name and token is set.
    struct JoinRoomMessage {
        /// Normalised (upper case, NormalizeRoomCode)
        eastl::string code;
        /// Sanitised; empty for a reconnect
        eastl::string name;
        /// Well-formed (IsWellFormedToken); empty for a new member
        eastl::string token;

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

    using ClientMessage = std::variant<CreateRoomMessage, JoinRoomMessage, ChooseSeatMessage, StartMessage, ActionMessage, LeaveMessage, PingMessage>;

    /**
     * @brief Reads one text message of a client. Never throws, aborts or asserts, whatever the bytes are.
     *
     * Checks in this order: the size (@p maxBytes; `message_too_large`), no raw NUL byte (the JSON lexer takes it for the end of the
     * text and would ignore what follows; `bad_message`), the text is a JSON object (`bad_message`), `v` is the integer
     * kRoomProtocolVersion (a missing or non-integer `v` is `bad_message`, another number `unsupported_version`), `type` is one of the
     * seven (`bad_message`), then each field of that type. Members that the type does not read are ignored.
     *
     * Errors by field: a missing or wrong-typed field, a fractional or huge number, a negative `action.version`, an `options` that is
     * not an object, a `game` that is empty or longer than kMaxGameNameBytes, a `join_room` with both or neither of `name` and
     * `token`: `bad_message`. A name that SanitizePlayerName refuses: `bad_name`. A `code` that is not a room code: `unknown_room`
     * (a code that could not exist is the same answer as one that does not). A `token` that is not well formed: `bad_token`.
     *
     * @param text The message exactly as it came from the transport.
     * @param maxBytes The size limit (kMaxMessageBytes by default).
     */
    [[nodiscard]] std::expected<ClientMessage, ControlError> ParseClientMessage(eastl::string_view text, size_t maxBytes = RoomLimits::kMaxMessageBytes);

    /// The `type` of a parsed message
    [[nodiscard]] eastl::string_view GetClientMessageType(const ClientMessage& message);

    /// The text a client sends for @p message (the inverse of ParseClientMessage; for clients and tests)
    [[nodiscard]] eastl::string SerializeClientMessage(const ClientMessage& message);

    /**
     * @brief A player name as the server keeps it, or nullopt when it cannot be one.
     *
     * Valid UTF-8 (no overlong forms, surrogates or values above U+10FFFF), no control characters (U+0000 to U+001F, U+007F to U+009F,
     * the line and paragraph separators, zero-width and bidirectional control characters, the byte order mark), then the spaces at
     * both ends removed, and kMinNameBytes to kMaxNameBytes bytes left.
     */
    [[nodiscard]] std::optional<eastl::string> SanitizePlayerName(eastl::string_view text);

    // ---- Server to client -------------------------------------------------------------------------------------------------------

    /// One seat in a `lobby` message
    struct LobbySeat {
        int seat = 0;
        SeatKind kind = SeatKind::Empty;
        /// The name of a human; not sent for the other kinds
        eastl::string name;
        /// Whether the human's connection is up; not sent for the other kinds
        bool connected = false;
    };

    /// What a `lobby` message says, as plain data (the protocol knows nothing of the Room class that fills it)
    struct LobbySnapshot {
        eastl::string room;
        eastl::string game;
        RoomPhase phase = RoomPhase::Lobby;
        /// The seat of the host, or -1 when no human is left
        int hostSeat = -1;
        eastl::vector<LobbySeat> seats;
        /// The public options of the room (`SeatedGameInfo::publicOptions`): an object of booleans and integers, empty when the game
        /// names none. Not written when empty; a reader keeps only boolean and integer members.
        Json options = Json::object();
    };

    /// `joined {room, player, token, seat, host}`. The token is a secret: this message goes to its owner only.
    [[nodiscard]] eastl::string MakeJoinedMessage(eastl::string_view room, PlayerId player, eastl::string_view token, int seat, bool host);

    /// `lobby {room, game, phase, host, seats:[{seat, kind, name?, connected?}], options?}`; to every member when it changes
    [[nodiscard]] eastl::string MakeLobbyMessage(const LobbySnapshot& lobby);

    /// `view {room, seat, version, finished, view, actions}`: what the game says the seat sees and may send now (no hidden information
    /// of the others: the game's contract)
    [[nodiscard]] eastl::string MakeViewMessage(eastl::string_view room, int seat, uint64_t version, bool finished, const Json& view, const Json& actions);

    /// `error {code, message, ref?}`; @p ref is the type of the message it answers (left out when empty)
    [[nodiscard]] eastl::string MakeErrorMessage(eastl::string_view code, eastl::string_view message, eastl::string_view ref = {});

    /// `left {reason}`: the sender is no longer in the room
    [[nodiscard]] eastl::string MakeLeftMessage(eastl::string_view reason);

    /// `pong {}`
    [[nodiscard]] eastl::string MakePongMessage();

    // ---- Server to client, read by a client -------------------------------------------------------------------------------------

    /// `joined`, as the client reads it
    struct JoinedMessage {
        eastl::string room;
        PlayerId player = 0;
        /// A secret of this player; well-formed (IsWellFormedToken)
        eastl::string token;
        int seat = 0;
        bool host = false;
    };

    /// `lobby`, as the client reads it
    using LobbyMessage = LobbySnapshot;

    /// `view`, as the client reads it. @c view and @c actions are the game's own JSON, kept as the server sent them.
    struct ViewMessage {
        eastl::string room;
        int seat = 0;
        uint64_t version = 0;
        bool finished = false;
        Json view = Json::object();
        Json actions = Json::array();
    };

    /// `error`
    struct ErrorMessage {
        eastl::string code;
        eastl::string message;
        /// The type of the message it answers; empty when the server gave none
        eastl::string ref;
    };

    /// `left`
    struct LeftMessage {
        eastl::string reason;
    };

    /// `pong`
    struct PongMessage {};

    /// A message with a good `v` and a `type` this client does not know (a newer server may add some); the client ignores it
    struct UnknownServerMessage {
        eastl::string type;
    };

    using ServerMessage = std::variant<JoinedMessage, LobbyMessage, ViewMessage, ErrorMessage, LeftMessage, PongMessage, UnknownServerMessage>;

    /**
     * @brief Reads one text message of the server (the inverse of the Make*Message builders). Never throws, aborts or asserts.
     *
     * Checks in this order: the size (@p maxBytes; `message_too_large`), no raw NUL byte, nesting not deeper than kMaxServerJsonDepth (`bad_message`), a JSON object, `v` an integer
     * (`bad_message`; another number than kRoomProtocolVersion is `unsupported_version`), `type` a string, then the fields of that
     * type. A missing or wrong-typed field, a fractional or out-of-range number, an unknown `phase` or seat `kind`, a token that is
     * not well formed, more than kMaxLobbySeats seats are `bad_message`. Members the type does not define are ignored. An unknown
     * `type` is UnknownServerMessage. A string is copied, never interned into a PoolString.
     */
    [[nodiscard]] std::expected<ServerMessage, ControlError> ParseServerMessage(eastl::string_view text, size_t maxBytes = RoomLimits::kMaxServerMessageBytes);

    /// The `type` of a parsed server message (for the log)
    [[nodiscard]] eastl::string_view GetServerMessageType(const ServerMessage& message);

}  // namespace BECore
