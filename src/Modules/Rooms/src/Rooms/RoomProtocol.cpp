#include "RoomProtocol.h"

#include <Rooms/RoomCode.h>
#include <Rooms/RoomErrors.h>
#include <Rooms/RoomWire.h>

namespace BECore {

    namespace {

        struct DecodedChar {
            uint32_t codePoint = 0;
            size_t length = 0;
        };

        constexpr uint32_t kMaxCodePoint = 0x10FFFF;
        constexpr uint32_t kSurrogateFirst = 0xD800;
        constexpr uint32_t kSurrogateLast = 0xDFFF;

        [[nodiscard]] bool IsContinuation(unsigned char byte) {
            return (byte & 0xC0) == 0x80;
        }

        /// Reads one strictly valid UTF-8 character at @p pos; nullopt for a bad lead byte, a missing or bad continuation byte,
        /// an overlong form, a surrogate or a value above U+10FFFF
        [[nodiscard]] Optional<DecodedChar> DecodeUtf8(StringView text, size_t pos) {
            const auto byteAt = [&](size_t index) { return static_cast<unsigned char>(text[index]); };
            const unsigned char lead = byteAt(pos);
            size_t length = 0;
            uint32_t codePoint = 0;
            uint32_t minimum = 0;
            if (lead < 0x80) {
                return DecodedChar{lead, 1};
            }
            if ((lead & 0xE0) == 0xC0) {
                length = 2;
                codePoint = lead & 0x1F;
                minimum = 0x80;
            } else if ((lead & 0xF0) == 0xE0) {
                length = 3;
                codePoint = lead & 0x0F;
                minimum = 0x800;
            } else if ((lead & 0xF8) == 0xF0) {
                length = 4;
                codePoint = lead & 0x07;
                minimum = 0x10000;
            } else {
                return std::nullopt;
            }
            if (pos + length > text.size()) {
                return std::nullopt;
            }
            for (size_t i = 1; i < length; ++i) {
                const unsigned char next = byteAt(pos + i);
                if (!IsContinuation(next)) {
                    return std::nullopt;
                }
                codePoint = (codePoint << 6) | (next & 0x3F);
            }
            if (codePoint < minimum || codePoint > kMaxCodePoint || (codePoint >= kSurrogateFirst && codePoint <= kSurrogateLast)) {
                return std::nullopt;
            }
            return DecodedChar{codePoint, length};
        }

        /// Characters that are no part of a printable name: controls, line breaks, and the format characters of Unicode (General_Category Cf), which are invisible or change the direction of the text
        /// around them
        [[nodiscard]] bool IsForbiddenInName(uint32_t codePoint) {
            return codePoint < 0x20 || (codePoint >= 0x7F && codePoint <= 0x9F)                                                     // C0, DEL and C1 controls
                   || codePoint == 0xAD                                                                                             // soft hyphen
                   || (codePoint >= 0x600 && codePoint <= 0x605) || codePoint == 0x61C || codePoint == 0x6DD || codePoint == 0x70F  // Arabic and Syriac format marks
                   || (codePoint >= 0x890 && codePoint <= 0x891) || codePoint == 0x8E2                                              // Arabic number marks
                   || codePoint == 0x180E                                                                                           // Mongolian vowel separator
                   || (codePoint >= 0x200B && codePoint <= 0x200F)                                                                  // zero-width characters and direction marks
                   || codePoint == 0x2028 || codePoint == 0x2029                                                                    // line and paragraph separators
                   || (codePoint >= 0x202A && codePoint <= 0x202E)                                                                  // embedding and override of direction
                   || (codePoint >= 0x2060 && codePoint <= 0x206F)                                                                  // word joiner, invisible operators, isolates of direction
                   || codePoint == 0xFEFF                                                                                           // byte order mark
                   || (codePoint >= 0xFFF9 && codePoint <= 0xFFFB)                                                                  // interlinear annotation
                   || codePoint == 0x110BD || codePoint == 0x110CD || (codePoint >= 0x13430 && codePoint <= 0x1343F)                // format marks of historic scripts
                   || (codePoint >= 0x1BCA0 && codePoint <= 0x1BCA3) || (codePoint >= 0x1D173 && codePoint <= 0x1D17A) || (codePoint >= 0xE0000 && codePoint <= 0xE007F);  // tag characters
        }

        /// Characters that show nothing by themselves: a name of these alone would be an invisible player
        [[nodiscard]] bool IsBlankInName(uint32_t codePoint) {
            return codePoint == 0x20 || codePoint == 0xA0 || codePoint == 0x34F || codePoint == 0x115F || codePoint == 0x1160 || codePoint == 0x1680 || (codePoint >= 0x17B4 && codePoint <= 0x17B5) ||
                   (codePoint >= 0x2000 && codePoint <= 0x200A) || codePoint == 0x202F || codePoint == 0x205F || codePoint == 0x2800 || codePoint == 0x3000 || codePoint == 0x3164 ||
                   (codePoint >= 0xFE00 && codePoint <= 0xFE0F) || codePoint == 0xFFA0 || (codePoint >= 0xE0100 && codePoint <= 0xE01EF);
        }

        [[nodiscard]] std::unexpected<ControlError> Fail(StringView code, String message) {
            return std::unexpected(MakeControlError(code, std::move(message)));
        }

        [[nodiscard]] std::unexpected<ControlError> BadMessage(String message) {
            return Fail(RoomErrors::kBadMessage, std::move(message));
        }

        [[nodiscard]] std::expected<String, ControlError> RequireString(const Json& root, StringView key) {
            auto value = GetJsonString(root, key);
            if (!value) {
                return BadMessage(Format("'{}' must be a string", key));
            }
            return std::move(*value);
        }

        [[nodiscard]] std::expected<String, ControlError> RequireName(const Json& root) {
            auto raw = GetJsonString(root, "name");
            if (!raw) {
                return BadMessage("'name' must be a string");
            }
            auto name = SanitizePlayerName(*raw);
            if (!name) {
                return Fail(RoomErrors::kBadName, Format("'name' must be {} to {} bytes of printable text", RoomLimits::kMinNameBytes, RoomLimits::kMaxNameBytes));
            }
            return std::move(*name);
        }

        /// The `game` member of `create_room` and `list_rooms`: a string of 1 to kMaxGameNameBytes bytes
        [[nodiscard]] std::expected<String, ControlError> RequireGameName(const Json& root) {
            auto game = RequireString(root, "game");
            if (!game) {
                return std::unexpected(game.error());
            }
            if (game->empty() || game->size() > RoomLimits::kMaxGameNameBytes) {
                return BadMessage(Format("'game' must be 1 to {} bytes", RoomLimits::kMaxGameNameBytes));
            }
            return std::move(*game);
        }

        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseCreateRoom(const Json& root) {
            auto game = RequireGameName(root);
            if (!game) {
                return std::unexpected(game.error());
            }
            auto name = RequireName(root);
            if (!name) {
                return std::unexpected(name.error());
            }
            CreateRoomMessage message;
            message.game = std::move(*game);
            message.name = std::move(*name);
            if (const Json* options = FindJsonMember(root, "options")) {
                if (!options->is_object()) {
                    return BadMessage("'options' must be an object");
                }
                message.options = *options;
            }
            if (const Json* isPublic = FindJsonMember(root, "public")) {
                if (!isPublic->is_boolean()) {
                    return BadMessage("'public' must be a boolean");
                }
                message.isPublic = isPublic->get<bool>();
            }
            return ClientMessage(std::move(message));
        }

        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseListRooms(const Json& root) {
            auto game = RequireGameName(root);
            if (!game) {
                return std::unexpected(game.error());
            }
            return ClientMessage(ListRoomsMessage{std::move(*game)});
        }

        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseJoinRoom(const Json& root) {
            auto rawCode = RequireString(root, "code");
            if (!rawCode) {
                return std::unexpected(rawCode.error());
            }
            const bool hasName = FindJsonMember(root, "name") != nullptr;
            const bool hasToken = FindJsonMember(root, "token") != nullptr;
            if (hasName == hasToken) {
                return BadMessage("'join_room' needs exactly one of 'name' and 'token'");
            }
            JoinRoomMessage message;
            if (hasName) {
                auto name = RequireName(root);
                if (!name) {
                    return std::unexpected(name.error());
                }
                message.name = std::move(*name);
            } else {
                auto token = RequireString(root, "token");
                if (!token) {
                    return std::unexpected(token.error());
                }
                if (!IsWellFormedToken(*token)) {
                    return Fail(RoomErrors::kBadToken, "'token' is not a token");
                }
                message.token = std::move(*token);
            }
            auto code = NormalizeRoomCode(*rawCode);
            if (!code) {
                return Fail(RoomErrors::kUnknownRoom, "no room has this code");
            }
            message.code = std::move(*code);
            return ClientMessage(std::move(message));
        }

        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseChooseSeat(const Json& root) {
            const auto seat = GetJsonInt(root, "seat");
            if (!seat) {
                return BadMessage("'seat' must be an integer");
            }
            return ClientMessage(ChooseSeatMessage{*seat});
        }

        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseAction(const Json& root) {
            const auto version = GetJsonInt(root, "version");
            if (!version || *version < 0) {
                return BadMessage("'version' must be a non-negative integer");
            }
            const Json* action = FindJsonMember(root, "action");
            if (action == nullptr || !action->is_object()) {
                return BadMessage("'action' must be an object");
            }
            ActionMessage message;
            message.version = static_cast<uint64_t>(*version);
            message.action = *action;
            return ClientMessage(std::move(message));
        }

        /// `type` and `v` are written where alphabetical order puts them, in the middle of each message, so each function writes its members around this
        void WriteTypeAndVersion(JsonWriter& writer, StringView type) {
            writer.Member("type", type);
            writer.Member("v", kRoomProtocolVersion);
        }

        /// `options` of `lobby` and of a `rooms` listing: not written unless it is a non-empty object
        void WriteOptionsMember(JsonWriter& writer, const Json& options) {
            if (options.is_object() && !options.empty()) {
                writer.Key("options");
                WriteJson(writer, options);
            }
        }

        [[nodiscard]] Optional<RoomPhase> ParsePhaseName(StringView name) {
            for (const RoomPhase phase : {RoomPhase::Lobby, RoomPhase::Playing, RoomPhase::Finished}) {
                if (GetPhaseName(phase) == name) {
                    return phase;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] Optional<SeatKind> ParseSeatKindName(StringView name) {
            for (const SeatKind kind : {SeatKind::Human, SeatKind::Computer, SeatKind::Empty}) {
                if (GetSeatKindName(kind) == name) {
                    return kind;
                }
            }
            return std::nullopt;
        }

        /// An integer member in [min, max]
        [[nodiscard]] std::expected<int64_t, ControlError> RequireInt(const Json& root, StringView key, int64_t min, int64_t max) {
            const auto value = GetJsonInt(root, key);
            if (!value || *value < min || *value > max) {
                return BadMessage(Format("'{}' must be an integer from {} to {}", key, min, max));
            }
            return *value;
        }

        [[nodiscard]] std::expected<bool, ControlError> RequireBool(const Json& root, StringView key) {
            const auto value = GetJsonBool(root, key);
            if (!value) {
                return BadMessage(Format("'{}' must be a boolean", key));
            }
            return *value;
        }

        [[nodiscard]] std::expected<LobbySeat, ControlError> ParseLobbySeat(const Json& entry) {
            if (!entry.is_object()) {
                return BadMessage("a seat of 'seats' must be an object");
            }
            LobbySeat seat;
            const auto index = RequireInt(entry, "seat", 0, std::numeric_limits<int>::max());
            if (!index) {
                return std::unexpected(index.error());
            }
            seat.seat = static_cast<int>(*index);
            const auto kindName = RequireString(entry, "kind");
            if (!kindName) {
                return std::unexpected(kindName.error());
            }
            const auto kind = ParseSeatKindName(*kindName);
            if (!kind) {
                return BadMessage("a seat has an unknown 'kind'");
            }
            seat.kind = *kind;
            if (seat.kind == SeatKind::Human) {
                auto name = RequireString(entry, "name");
                if (!name) {
                    return std::unexpected(name.error());
                }
                const auto connected = RequireBool(entry, "connected");
                if (!connected) {
                    return std::unexpected(connected.error());
                }
                seat.name = std::move(*name);
                seat.connected = *connected;
            }
            return seat;
        }

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseJoined(const Json& root) {
            JoinedMessage message;
            auto room = RequireString(root, "room");
            if (!room) {
                return std::unexpected(room.error());
            }
            message.room = std::move(*room);
            const auto player = RequireInt(root, "player", 0, std::numeric_limits<PlayerId>::max());
            if (!player) {
                return std::unexpected(player.error());
            }
            message.player = static_cast<PlayerId>(*player);
            auto token = RequireString(root, "token");
            if (!token) {
                return std::unexpected(token.error());
            }
            if (!IsWellFormedToken(*token)) {
                return BadMessage("'token' is not a token");
            }
            message.token = std::move(*token);
            const auto seat = RequireInt(root, "seat", 0, std::numeric_limits<int>::max());
            if (!seat) {
                return std::unexpected(seat.error());
            }
            message.seat = static_cast<int>(*seat);
            const auto host = RequireBool(root, "host");
            if (!host) {
                return std::unexpected(host.error());
            }
            message.host = *host;
            return ServerMessage(std::move(message));
        }

        /// The boolean and integer members of `options`, at most kMaxLobbyOptions (what a `lobby` message may carry)
        [[nodiscard]] Json PublicOptionValues(const Json& options) {
            Json kept = Json::object();
            for (auto it = options.begin(); it != options.end() && kept.size() < RoomLimits::kMaxLobbyOptions; ++it) {
                if (it.value().is_boolean() || it.value().is_number_integer()) {
                    kept[it.key()] = it.value();
                }
            }
            return kept;
        }

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseLobby(const Json& root) {
            LobbyMessage message;
            auto room = RequireString(root, "room");
            if (!room) {
                return std::unexpected(room.error());
            }
            message.room = std::move(*room);
            auto game = RequireString(root, "game");
            if (!game) {
                return std::unexpected(game.error());
            }
            message.game = std::move(*game);
            const auto phaseName = RequireString(root, "phase");
            if (!phaseName) {
                return std::unexpected(phaseName.error());
            }
            const auto phase = ParsePhaseName(*phaseName);
            if (!phase) {
                return BadMessage("'phase' is unknown");
            }
            message.phase = *phase;
            const auto host = RequireInt(root, "host", -1, std::numeric_limits<int>::max());
            if (!host) {
                return std::unexpected(host.error());
            }
            message.hostSeat = static_cast<int>(*host);
            const Json* seats = FindJsonMember(root, "seats");
            if (seats == nullptr || !seats->is_array() || seats->size() > RoomLimits::kMaxLobbySeats) {
                return BadMessage(Format("'seats' must be an array of at most {} seats", RoomLimits::kMaxLobbySeats));
            }
            message.seats.reserve(seats->size());
            for (const Json& entry : *seats) {
                auto seat = ParseLobbySeat(entry);
                if (!seat) {
                    return std::unexpected(seat.error());
                }
                message.seats.push_back(std::move(*seat));
            }
            if (const Json* options = FindJsonMember(root, "options"); options != nullptr) {
                if (!options->is_object() || options->size() > RoomLimits::kMaxLobbyOptions) {
                    return BadMessage(Format("'options' must be an object of at most {} members", RoomLimits::kMaxLobbyOptions));
                }
                message.options = PublicOptionValues(*options);
            }
            return ServerMessage(std::move(message));
        }

        [[nodiscard]] std::expected<RoomListing, ControlError> ParseRoomListing(const Json& entry) {
            if (!entry.is_object()) {
                return BadMessage("an entry of 'rooms' must be an object");
            }
            RoomListing listing;
            auto room = RequireString(entry, "room");
            if (!room) {
                return std::unexpected(room.error());
            }
            if (NormalizeRoomCode(*room) != Optional<String>(*room)) {
                return BadMessage("a room of 'rooms' has no well-formed code");
            }
            listing.room = std::move(*room);
            auto host = RequireString(entry, "host");
            if (!host) {
                return std::unexpected(host.error());
            }
            listing.host = std::move(*host);
            const auto seats = RequireInt(entry, "seats", 1, static_cast<int64_t>(RoomLimits::kMaxLobbySeats));
            if (!seats) {
                return std::unexpected(seats.error());
            }
            listing.seats = static_cast<int>(*seats);
            const auto taken = RequireInt(entry, "taken", 0, *seats);
            if (!taken) {
                return std::unexpected(taken.error());
            }
            listing.taken = static_cast<int>(*taken);
            const auto started = RequireBool(entry, "started");
            if (!started) {
                return std::unexpected(started.error());
            }
            listing.started = *started;
            if (const Json* options = FindJsonMember(entry, "options"); options != nullptr) {
                if (!options->is_object() || options->size() > RoomLimits::kMaxLobbyOptions) {
                    return BadMessage(Format("'options' must be an object of at most {} members", RoomLimits::kMaxLobbyOptions));
                }
                for (auto it = options->begin(); it != options->end(); ++it) {
                    if (!it.value().is_boolean() && !it.value().is_number_integer()) {
                        return BadMessage("'options' may hold only booleans and integers");
                    }
                }
                listing.options = *options;
            }
            return listing;
        }

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseRoomList(const Json& root) {
            RoomListMessage message;
            auto game = RequireString(root, "game");
            if (!game) {
                return std::unexpected(game.error());
            }
            message.game = std::move(*game);
            const auto more = RequireBool(root, "more");
            if (!more) {
                return std::unexpected(more.error());
            }
            message.more = *more;
            const Json* rooms = FindJsonMember(root, "rooms");
            if (rooms == nullptr || !rooms->is_array() || rooms->size() > RoomLimits::kMaxListedRooms) {
                return BadMessage(Format("'rooms' must be an array of at most {} rooms", RoomLimits::kMaxListedRooms));
            }
            message.rooms.reserve(rooms->size());
            for (const Json& entry : *rooms) {
                auto listing = ParseRoomListing(entry);
                if (!listing) {
                    return std::unexpected(listing.error());
                }
                message.rooms.push_back(std::move(*listing));
            }
            return ServerMessage(std::move(message));
        }

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseView(const Json& root) {
            ViewMessage message;
            auto room = RequireString(root, "room");
            if (!room) {
                return std::unexpected(room.error());
            }
            message.room = std::move(*room);
            const auto seat = RequireInt(root, "seat", 0, std::numeric_limits<int>::max());
            if (!seat) {
                return std::unexpected(seat.error());
            }
            message.seat = static_cast<int>(*seat);
            const auto version = RequireInt(root, "version", 0, std::numeric_limits<int64_t>::max());
            if (!version) {
                return std::unexpected(version.error());
            }
            message.version = static_cast<uint64_t>(*version);
            const auto finished = RequireBool(root, "finished");
            if (!finished) {
                return std::unexpected(finished.error());
            }
            message.finished = *finished;
            const Json* view = FindJsonMember(root, "view");
            const Json* actions = FindJsonMember(root, "actions");
            if (view == nullptr || actions == nullptr) {
                return BadMessage("'view' and 'actions' must be there");
            }
            message.view = *view;
            message.actions = *actions;
            return ServerMessage(std::move(message));
        }

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseError(const Json& root) {
            ErrorMessage message;
            auto code = RequireString(root, "code");
            if (!code) {
                return std::unexpected(code.error());
            }
            message.code = std::move(*code);
            auto text = RequireString(root, "message");
            if (!text) {
                return std::unexpected(text.error());
            }
            message.message = std::move(*text);
            if (FindJsonMember(root, "ref") != nullptr) {
                auto ref = RequireString(root, "ref");
                if (!ref) {
                    return std::unexpected(ref.error());
                }
                message.ref = std::move(*ref);
            }
            return ServerMessage(std::move(message));
        }

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseLeft(const Json& root) {
            auto reason = RequireString(root, "reason");
            if (!reason) {
                return std::unexpected(reason.error());
            }
            return ServerMessage(LeftMessage{std::move(*reason)});
        }

    }  // namespace

    std::expected<ClientMessage, ControlError> ParseClientMessage(StringView text, size_t maxBytes) {
        if (text.size() > maxBytes) {
            return Fail(RoomErrors::kMessageTooLarge, Format("a message is at most {} bytes", maxBytes));
        }
        // The JSON lexer takes a NUL byte for the end of the input and would ignore what follows it
        if (text.find('\0') != StringView::npos) {
            return BadMessage("the message must not contain a NUL byte");
        }
        if (IsJsonNestedDeeperThan(text, RoomLimits::kMaxClientJsonDepth)) {
            return BadMessage(Format("the message nests deeper than {} levels", RoomLimits::kMaxClientJsonDepth));
        }
        const auto parsed = ParseJson(text);
        if (!parsed) {
            return BadMessage("the message is not valid JSON");
        }
        const Json& root = *parsed;
        if (!root.is_object()) {
            return BadMessage("the message must be a JSON object");
        }
        const auto version = GetJsonInt(root, "v");
        if (!version) {
            return BadMessage("'v' must be an integer");
        }
        if (*version != kRoomProtocolVersion) {
            return Fail(RoomErrors::kUnsupportedVersion, Format("the server speaks protocol version {}", kRoomProtocolVersion));
        }
        const auto type = GetJsonString(root, "type");
        if (!type) {
            return BadMessage("'type' must be a string");
        }
        if (*type == RoomMessageTypes::kCreateRoom) {
            return ParseCreateRoom(root);
        }
        if (*type == RoomMessageTypes::kJoinRoom) {
            return ParseJoinRoom(root);
        }
        if (*type == RoomMessageTypes::kChooseSeat) {
            return ParseChooseSeat(root);
        }
        if (*type == RoomMessageTypes::kStart) {
            return ClientMessage(StartMessage{});
        }
        if (*type == RoomMessageTypes::kAction) {
            return ParseAction(root);
        }
        if (*type == RoomMessageTypes::kLeave) {
            return ClientMessage(LeaveMessage{});
        }
        if (*type == RoomMessageTypes::kPing) {
            return ClientMessage(PingMessage{});
        }
        if (*type == RoomMessageTypes::kListRooms) {
            return ParseListRooms(root);
        }
        return BadMessage("unknown message type");
    }

    StringView GetClientMessageType(const ClientMessage& message) {
        return std::visit(overload{
                              [](const CreateRoomMessage&) { return RoomMessageTypes::kCreateRoom; },
                              [](const JoinRoomMessage&) { return RoomMessageTypes::kJoinRoom; },
                              [](const ChooseSeatMessage&) { return RoomMessageTypes::kChooseSeat; },
                              [](const StartMessage&) { return RoomMessageTypes::kStart; },
                              [](const ActionMessage&) { return RoomMessageTypes::kAction; },
                              [](const LeaveMessage&) { return RoomMessageTypes::kLeave; },
                              [](const PingMessage&) { return RoomMessageTypes::kPing; },
                              [](const ListRoomsMessage&) { return RoomMessageTypes::kListRooms; },
                          },
                          message);
    }

    void AppendClientMessage(String& out, const ClientMessage& message) {
        const StringView type = GetClientMessageType(message);
        JsonWriter writer(out);
        writer.BeginObject();
        std::visit(overload{
                       [&](const CreateRoomMessage& m) {
                           writer.Member("game", m.game);
                           writer.Member("name", m.name);
                           writer.Key("options");
                           WriteJson(writer, m.options);
                           if (m.isPublic) {
                               writer.Member("public", true);
                           }
                           WriteTypeAndVersion(writer, type);
                       },
                       [&](const JoinRoomMessage& m) {
                           writer.Member("code", m.code);
                           if (m.IsReconnect()) {
                               writer.Member("token", m.token);
                           } else {
                               writer.Member("name", m.name);
                           }
                           WriteTypeAndVersion(writer, type);
                       },
                       [&](const ChooseSeatMessage& m) {
                           writer.Member("seat", m.seat);
                           WriteTypeAndVersion(writer, type);
                       },
                       [&](const ActionMessage& m) {
                           writer.Key("action");
                           WriteJson(writer, m.action);
                           WriteTypeAndVersion(writer, type);
                           writer.Member("version", m.version);
                       },
                       [&](const ListRoomsMessage& m) {
                           writer.Member("game", m.game);
                           WriteTypeAndVersion(writer, type);
                       },
                       [&](const auto&) { WriteTypeAndVersion(writer, type); },
                   },
                   message);
        writer.EndObject();
        EXPECT(writer.IsComplete());
    }

    String SerializeClientMessage(const ClientMessage& message) {
        String out;
        AppendClientMessage(out, message);
        return out;
    }

    Optional<String> SanitizePlayerName(StringView text) {
        bool visible = false;
        for (size_t pos = 0; pos < text.size();) {
            const auto decoded = DecodeUtf8(text, pos);
            if (!decoded || IsForbiddenInName(decoded->codePoint)) {
                return std::nullopt;
            }
            visible = visible || !IsBlankInName(decoded->codePoint);
            pos += decoded->length;
        }
        if (!visible) {
            return std::nullopt;
        }
        // Spaces at both ends go. Other characters are all printable by now, so only the ASCII space can be trimmed.
        size_t first = 0;
        size_t last = text.size();
        while (first < last && text[first] == ' ') {
            ++first;
        }
        while (last > first && text[last - 1] == ' ') {
            --last;
        }
        const size_t length = last - first;
        if (length < RoomLimits::kMinNameBytes || length > RoomLimits::kMaxNameBytes) {
            return std::nullopt;
        }
        return String(text.data() + first, length);
    }

    void AppendJoinedMessage(String& out, StringView room, PlayerId player, StringView token, int seat, bool host) {
        RoomWireJoined wire;
        wire.host = host;
        wire.player = player;
        wire.room = String(room.data(), room.size());
        wire.seat = seat;
        wire.token = String(token.data(), token.size());
        JsonWriter writer(out);
        wire.WriteJson(writer);
        EXPECT(writer.IsComplete());
    }

    String MakeJoinedMessage(StringView room, PlayerId player, StringView token, int seat, bool host) {
        String out;
        AppendJoinedMessage(out, room, player, token, seat, host);
        return out;
    }

    void AppendLobbyMessage(String& out, const LobbySnapshot& lobby) {
        JsonWriter writer(out);
        writer.BeginObject();
        writer.Member("game", lobby.game);
        writer.Member("host", lobby.hostSeat);
        WriteOptionsMember(writer, lobby.options);
        writer.Member("phase", GetPhaseName(lobby.phase));
        writer.Member("room", lobby.room);
        writer.Key("seats");
        writer.BeginArray();
        for (const LobbySeat& seat : lobby.seats) {
            writer.BeginObject();
            if (seat.kind == SeatKind::Human) {
                writer.Member("connected", seat.connected);
            }
            writer.Member("kind", GetSeatKindName(seat.kind));
            if (seat.kind == SeatKind::Human) {
                writer.Member("name", seat.name);
            }
            writer.Member("seat", seat.seat);
            writer.EndObject();
        }
        writer.EndArray();
        WriteTypeAndVersion(writer, RoomMessageTypes::kLobby);
        writer.EndObject();
        EXPECT(writer.IsComplete());
    }

    String MakeLobbyMessage(const LobbySnapshot& lobby) {
        String out;
        AppendLobbyMessage(out, lobby);
        return out;
    }

    void AppendRoomListMessage(String& out, const RoomListMessage& list) {
        JsonWriter writer(out);
        writer.BeginObject();
        writer.Member("game", list.game);
        writer.Member("more", list.more);
        writer.Key("rooms");
        writer.BeginArray();
        for (const RoomListing& listing : list.rooms) {
            writer.BeginObject();
            writer.Member("host", listing.host);
            WriteOptionsMember(writer, listing.options);
            writer.Member("room", listing.room);
            writer.Member("seats", listing.seats);
            writer.Member("started", listing.started);
            writer.Member("taken", listing.taken);
            writer.EndObject();
        }
        writer.EndArray();
        WriteTypeAndVersion(writer, RoomMessageTypes::kRooms);
        writer.EndObject();
        EXPECT(writer.IsComplete());
    }

    String MakeRoomListMessage(const RoomListMessage& list) {
        String out;
        AppendRoomListMessage(out, list);
        return out;
    }

    void AppendViewMessage(String& out, StringView room, int seat, uint64_t version, bool finished, const Json& view, const Json& actions) {
        JsonWriter writer(out);
        writer.BeginObject();
        writer.Key("actions");
        WriteJson(writer, actions);
        writer.Member("finished", finished);
        writer.Member("room", room);
        writer.Member("seat", seat);
        WriteTypeAndVersion(writer, RoomMessageTypes::kView);
        writer.Member("version", version);
        writer.Key("view");
        WriteJson(writer, view);
        writer.EndObject();
        EXPECT(writer.IsComplete());
    }

    String MakeViewMessage(StringView room, int seat, uint64_t version, bool finished, const Json& view, const Json& actions) {
        String out;
        AppendViewMessage(out, room, seat, version, finished, view, actions);
        return out;
    }

    void AppendErrorMessage(String& out, StringView code, StringView message, StringView ref) {
        RoomWireError wire;
        wire.code = String(code.data(), code.size());
        wire.message = String(message.data(), message.size());
        if (!ref.empty()) {
            wire.ref = String(ref.data(), ref.size());
        }
        JsonWriter writer(out);
        wire.WriteJson(writer);
        EXPECT(writer.IsComplete());
    }

    String MakeErrorMessage(StringView code, StringView message, StringView ref) {
        String out;
        AppendErrorMessage(out, code, message, ref);
        return out;
    }

    void AppendLeftMessage(String& out, StringView reason) {
        RoomWireLeft wire;
        wire.reason = String(reason.data(), reason.size());
        JsonWriter writer(out);
        wire.WriteJson(writer);
        EXPECT(writer.IsComplete());
    }

    String MakeLeftMessage(StringView reason) {
        String out;
        AppendLeftMessage(out, reason);
        return out;
    }

    void AppendPongMessage(String& out) {
        const RoomWirePong wire;
        JsonWriter writer(out);
        wire.WriteJson(writer);
        EXPECT(writer.IsComplete());
    }

    String MakePongMessage() {
        String out;
        AppendPongMessage(out);
        return out;
    }

    std::expected<ServerMessage, ControlError> ParseServerMessage(StringView text, size_t maxBytes) {
        if (text.size() > maxBytes) {
            return Fail(RoomErrors::kMessageTooLarge, Format("a message is at most {} bytes", maxBytes));
        }
        if (text.find('\0') != StringView::npos) {
            return BadMessage("the message must not contain a NUL byte");
        }
        if (IsJsonNestedDeeperThan(text, RoomLimits::kMaxServerJsonDepth)) {
            return BadMessage(Format("the message nests deeper than {} levels", RoomLimits::kMaxServerJsonDepth));
        }
        const auto parsed = ParseJson(text);
        if (!parsed) {
            return BadMessage("the message is not valid JSON");
        }
        const Json& root = *parsed;
        if (!root.is_object()) {
            return BadMessage("the message must be a JSON object");
        }
        const auto version = GetJsonInt(root, "v");
        if (!version) {
            return BadMessage("'v' must be an integer");
        }
        if (*version != kRoomProtocolVersion) {
            return Fail(RoomErrors::kUnsupportedVersion, Format("this client speaks protocol version {}", kRoomProtocolVersion));
        }
        auto type = GetJsonString(root, "type");
        if (!type) {
            return BadMessage("'type' must be a string");
        }
        if (*type == RoomMessageTypes::kJoined) {
            return ParseJoined(root);
        }
        if (*type == RoomMessageTypes::kLobby) {
            return ParseLobby(root);
        }
        if (*type == RoomMessageTypes::kView) {
            return ParseView(root);
        }
        if (*type == RoomMessageTypes::kError) {
            return ParseError(root);
        }
        if (*type == RoomMessageTypes::kLeft) {
            return ParseLeft(root);
        }
        if (*type == RoomMessageTypes::kPong) {
            return ServerMessage(PongMessage{});
        }
        if (*type == RoomMessageTypes::kRooms) {
            return ParseRoomList(root);
        }
        return ServerMessage(UnknownServerMessage{std::move(*type)});
    }

    StringView GetServerMessageType(const ServerMessage& message) {
        return std::visit(overload{
                              [](const JoinedMessage&) { return RoomMessageTypes::kJoined; },
                              [](const LobbyMessage&) { return RoomMessageTypes::kLobby; },
                              [](const ViewMessage&) { return RoomMessageTypes::kView; },
                              [](const ErrorMessage&) { return RoomMessageTypes::kError; },
                              [](const LeftMessage&) { return RoomMessageTypes::kLeft; },
                              [](const PongMessage&) { return RoomMessageTypes::kPong; },
                              [](const RoomListMessage&) { return RoomMessageTypes::kRooms; },
                              [](const UnknownServerMessage& m) { return StringView(m.type); },
                          },
                          message);
    }

}  // namespace BECore
