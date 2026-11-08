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

}  // namespace BECore
