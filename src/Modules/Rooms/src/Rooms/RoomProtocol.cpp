#include "RoomProtocol.h"

#include <Rooms/RoomCode.h>
#include <Rooms/RoomErrors.h>

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
        [[nodiscard]] std::optional<DecodedChar> DecodeUtf8(eastl::string_view text, size_t pos) {
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

        /// Characters that are no part of a printable name: controls, line breaks, invisible and direction-changing characters
        [[nodiscard]] bool IsForbiddenInName(uint32_t codePoint) {
            return codePoint < 0x20 || (codePoint >= 0x7F && codePoint <= 0x9F)  // C0, DEL and C1 controls
                   || (codePoint >= 0x200B && codePoint <= 0x200F)               // zero-width characters and direction marks
                   || codePoint == 0x2028 || codePoint == 0x2029                 // line and paragraph separators
                   || (codePoint >= 0x202A && codePoint <= 0x202E)               // embedding and override of direction
                   || (codePoint >= 0x2066 && codePoint <= 0x2069)               // isolates of direction
                   || codePoint == 0xFEFF;                                       // byte order mark
        }

        [[nodiscard]] std::unexpected<ControlError> Fail(eastl::string_view code, eastl::string message) {
            return std::unexpected(MakeControlError(code, std::move(message)));
        }

        [[nodiscard]] std::unexpected<ControlError> BadMessage(eastl::string message) {
            return Fail(RoomErrors::kBadMessage, std::move(message));
        }

        /// A string member that must be there
        [[nodiscard]] std::expected<eastl::string, ControlError> RequireString(const Json& root, eastl::string_view key) {
            auto value = GetJsonString(root, key);
            if (!value) {
                return BadMessage(Format("'{}' must be a string", key));
            }
            return std::move(*value);
        }

        [[nodiscard]] std::expected<eastl::string, ControlError> RequireName(const Json& root) {
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

        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseCreateRoom(const Json& root) {
            auto game = RequireString(root, "game");
            if (!game) {
                return std::unexpected(game.error());
            }
            if (game->empty() || game->size() > RoomLimits::kMaxGameNameBytes) {
                return BadMessage(Format("'game' must be 1 to {} bytes", RoomLimits::kMaxGameNameBytes));
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
            return ClientMessage(std::move(message));
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

        /// The common start of every message: `v` and `type`
        [[nodiscard]] Json MakeEnvelope(eastl::string_view type) {
            Json root = Json::object();
            root["v"] = kRoomProtocolVersion;
            root["type"] = MakeJsonString(type);
            return root;
        }

        // ---- Reading the server's messages ----

        [[nodiscard]] std::optional<RoomPhase> ParsePhaseName(eastl::string_view name) {
            for (const RoomPhase phase : {RoomPhase::Lobby, RoomPhase::Playing, RoomPhase::Finished}) {
                if (GetPhaseName(phase) == name) {
                    return phase;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<SeatKind> ParseSeatKindName(eastl::string_view name) {
            for (const SeatKind kind : {SeatKind::Human, SeatKind::Computer, SeatKind::Empty}) {
                if (GetSeatKindName(kind) == name) {
                    return kind;
                }
            }
            return std::nullopt;
        }

        /// An integer member in [min, max]
        [[nodiscard]] std::expected<int64_t, ControlError> RequireInt(const Json& root, eastl::string_view key, int64_t min, int64_t max) {
            const auto value = GetJsonInt(root, key);
            if (!value || *value < min || *value > max) {
                return BadMessage(Format("'{}' must be an integer from {} to {}", key, min, max));
            }
            return *value;
        }

        [[nodiscard]] std::expected<bool, ControlError> RequireBool(const Json& root, eastl::string_view key) {
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

    std::expected<ClientMessage, ControlError> ParseClientMessage(eastl::string_view text, size_t maxBytes) {
        if (text.size() > maxBytes) {
            return Fail(RoomErrors::kMessageTooLarge, Format("a message is at most {} bytes", maxBytes));
        }
        // The JSON lexer takes a NUL byte for the end of the input and would ignore what follows it
        if (text.find('\0') != eastl::string_view::npos) {
            return BadMessage("the message must not contain a NUL byte");
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
        return BadMessage("unknown message type");
    }

    eastl::string_view GetClientMessageType(const ClientMessage& message) {
        return std::visit(overload{
                              [](const CreateRoomMessage&) { return RoomMessageTypes::kCreateRoom; },
                              [](const JoinRoomMessage&) { return RoomMessageTypes::kJoinRoom; },
                              [](const ChooseSeatMessage&) { return RoomMessageTypes::kChooseSeat; },
                              [](const StartMessage&) { return RoomMessageTypes::kStart; },
                              [](const ActionMessage&) { return RoomMessageTypes::kAction; },
                              [](const LeaveMessage&) { return RoomMessageTypes::kLeave; },
                              [](const PingMessage&) { return RoomMessageTypes::kPing; },
                          },
                          message);
    }

    eastl::string SerializeClientMessage(const ClientMessage& message) {
        Json root = MakeEnvelope(GetClientMessageType(message));
        std::visit(overload{
                       [&](const CreateRoomMessage& m) {
                           root["game"] = MakeJsonString(m.game);
                           root["name"] = MakeJsonString(m.name);
                           root["options"] = m.options;
                       },
                       [&](const JoinRoomMessage& m) {
                           root["code"] = MakeJsonString(m.code);
                           if (m.IsReconnect()) {
                               root["token"] = MakeJsonString(m.token);
                           } else {
                               root["name"] = MakeJsonString(m.name);
                           }
                       },
                       [&](const ChooseSeatMessage& m) { root["seat"] = m.seat; },
                       [&](const ActionMessage& m) {
                           root["version"] = m.version;
                           root["action"] = m.action;
                       },
                       [](const StartMessage&) {},
                       [](const LeaveMessage&) {},
                       [](const PingMessage&) {},
                   },
                   message);
        return DumpJson(root);
    }

    std::optional<eastl::string> SanitizePlayerName(eastl::string_view text) {
        for (size_t pos = 0; pos < text.size();) {
            const auto decoded = DecodeUtf8(text, pos);
            if (!decoded || IsForbiddenInName(decoded->codePoint)) {
                return std::nullopt;
            }
            pos += decoded->length;
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
        return eastl::string(text.data() + first, length);
    }

    eastl::string MakeJoinedMessage(eastl::string_view room, PlayerId player, eastl::string_view token, int seat, bool host) {
        Json root = MakeEnvelope(RoomMessageTypes::kJoined);
        root["room"] = MakeJsonString(room);
        root["player"] = player;
        root["token"] = MakeJsonString(token);
        root["seat"] = seat;
        root["host"] = host;
        return DumpJson(root);
    }

    eastl::string MakeLobbyMessage(const LobbySnapshot& lobby) {
        Json root = MakeEnvelope(RoomMessageTypes::kLobby);
        root["room"] = MakeJsonString(lobby.room);
        root["game"] = MakeJsonString(lobby.game);
        root["phase"] = MakeJsonString(GetPhaseName(lobby.phase));
        root["host"] = lobby.hostSeat;
        Json seats = Json::array();
        for (const LobbySeat& seat : lobby.seats) {
            Json entry = Json::object();
            entry["seat"] = seat.seat;
            entry["kind"] = MakeJsonString(GetSeatKindName(seat.kind));
            if (seat.kind == SeatKind::Human) {
                entry["name"] = MakeJsonString(seat.name);
                entry["connected"] = seat.connected;
            }
            seats.push_back(std::move(entry));
        }
        root["seats"] = std::move(seats);
        return DumpJson(root);
    }

    eastl::string MakeViewMessage(eastl::string_view room, int seat, uint64_t version, bool finished, const Json& view, const Json& actions) {
        Json root = MakeEnvelope(RoomMessageTypes::kView);
        root["room"] = MakeJsonString(room);
        root["seat"] = seat;
        root["version"] = version;
        root["finished"] = finished;
        root["view"] = view;
        root["actions"] = actions;
        return DumpJson(root);
    }

    eastl::string MakeErrorMessage(eastl::string_view code, eastl::string_view message, eastl::string_view ref) {
        Json root = MakeEnvelope(RoomMessageTypes::kError);
        root["code"] = MakeJsonString(code);
        root["message"] = MakeJsonString(message);
        if (!ref.empty()) {
            root["ref"] = MakeJsonString(ref);
        }
        return DumpJson(root);
    }

    eastl::string MakeLeftMessage(eastl::string_view reason) {
        Json root = MakeEnvelope(RoomMessageTypes::kLeft);
        root["reason"] = MakeJsonString(reason);
        return DumpJson(root);
    }

    eastl::string MakePongMessage() {
        return DumpJson(MakeEnvelope(RoomMessageTypes::kPong));
    }

    std::expected<ServerMessage, ControlError> ParseServerMessage(eastl::string_view text, size_t maxBytes) {
        if (text.size() > maxBytes) {
            return Fail(RoomErrors::kMessageTooLarge, Format("a message is at most {} bytes", maxBytes));
        }
        if (text.find('\0') != eastl::string_view::npos) {
            return BadMessage("the message must not contain a NUL byte");
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
        return ServerMessage(UnknownServerMessage{std::move(*type)});
    }

    eastl::string_view GetServerMessageType(const ServerMessage& message) {
        return std::visit(overload{
                              [](const JoinedMessage&) { return RoomMessageTypes::kJoined; },
                              [](const LobbyMessage&) { return RoomMessageTypes::kLobby; },
                              [](const ViewMessage&) { return RoomMessageTypes::kView; },
                              [](const ErrorMessage&) { return RoomMessageTypes::kError; },
                              [](const LeftMessage&) { return RoomMessageTypes::kLeft; },
                              [](const PongMessage&) { return RoomMessageTypes::kPong; },
                              [](const UnknownServerMessage& m) { return eastl::string_view(m.type); },
                          },
                          message);
    }

}  // namespace BECore
