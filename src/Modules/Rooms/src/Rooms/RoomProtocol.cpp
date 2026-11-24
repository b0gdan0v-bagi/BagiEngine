#include "RoomProtocol.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Memory/ScratchArena.h>
#include <EASTL/sort.h>
#include <Rooms/RoomCode.h>
#include <Rooms/RoomErrors.h>
#include <Rooms/RoomWire.h>

namespace BECore {

    namespace {

        struct DecodedChar {
            uint32_t codePoint = 0;
            size_t length = 0;
        };

        constexpr StringView kEmptyDocument = "{}";

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

        /// The checks both parsers share, in their order: the size, no raw NUL byte (its own message), the nesting (a text scan before the tape: the
        /// limits are the protocol's, the tape's own bound is far deeper), valid JSON, an object at the root
        [[nodiscard]] std::expected<JsonView, ControlError> TapeMessage(JsonTape& tape, StringView text, size_t maxBytes, size_t maxDepth) {
            if (text.size() > maxBytes) {
                return Fail(RoomErrors::kMessageTooLarge, Format("a message is at most {} bytes", maxBytes));
            }
            if (text.find('\0') != StringView::npos) {
                return BadMessage("the message must not contain a NUL byte");
            }
            if (IsJsonNestedDeeperThan(text, maxDepth)) {
                return BadMessage(Format("the message nests deeper than {} levels", maxDepth));
            }
            if (!tape.Parse(text)) {
                return BadMessage("the message is not valid JSON");
            }
            const JsonView root(tape, 0);
            if (!root.IsObject()) {
                return BadMessage("the message must be a JSON object");
            }
            return root;
        }

        [[nodiscard]] std::expected<String, ControlError> RequireString(JsonView root, StringView key) {
            auto value = GetParamString(root, key);
            if (!value) {
                return BadMessage(Format("'{}' must be a string", key));
            }
            return std::move(*value);
        }

        [[nodiscard]] std::expected<String, ControlError> RequireName(JsonView root) {
            auto raw = GetParamString(root, "name");
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
        [[nodiscard]] std::expected<String, ControlError> RequireGameName(JsonView root) {
            auto game = RequireString(root, "game");
            if (!game) {
                return std::unexpected(game.error());
            }
            if (game->empty() || game->size() > RoomLimits::kMaxGameNameBytes) {
                return BadMessage(Format("'game' must be 1 to {} bytes", RoomLimits::kMaxGameNameBytes));
            }
            return std::move(*game);
        }

        /// The text of a document member of a message: an empty one is the empty object
        [[nodiscard]] StringView DocumentText(const String& text) {
            return text.empty() ? kEmptyDocument : StringView(text.data(), text.size());
        }

        /// The bridge of the `view` and `actions` of a `ViewMessage`, which are still a `Json`: removed with their text form (the next step of the slice).
        /// A document the tape accepted and the DOM refuses (`1e999`) is the `bad_message` it was before the tape
        [[nodiscard]] std::expected<Json, ControlError> PayloadToJson(JsonView payload) {
            auto parsed = ParseJson(payload.Raw());
            if (!parsed) {
                return BadMessage("the message is not valid JSON");
            }
            return std::move(*parsed);
        }

        /// A document as compact text: what a message keeps of the game's `options` and `action` (blanks of the client's text are dropped)
        [[nodiscard]] String CompactText(JsonView document) {
            String text;
            JsonWriter writer(text);
            WriteJson(writer, document);
            return text;
        }

        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseCreateRoom(JsonView root) {
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
            if (const Optional<JsonView> options = FindParam(root, "options")) {
                if (!options->IsObject()) {
                    return BadMessage("'options' must be an object");
                }
                message.options = CompactText(*options);
            } else {
                message.options = kEmptyDocument;
            }
            if (const Optional<JsonView> isPublic = FindParam(root, "public")) {
                if (!isPublic->IsBool()) {
                    return BadMessage("'public' must be a boolean");
                }
                message.isPublic = isPublic->AsBool().value_or(false);
            }
            return ClientMessage(std::move(message));
        }

        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseListRooms(JsonView root) {
            auto game = RequireGameName(root);
            if (!game) {
                return std::unexpected(game.error());
            }
            return ClientMessage(ListRoomsMessage{std::move(*game)});
        }

        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseJoinRoom(JsonView root) {
            auto rawCode = RequireString(root, "code");
            if (!rawCode) {
                return std::unexpected(rawCode.error());
            }
            const bool hasName = FindParam(root, "name").has_value();
            const bool hasToken = FindParam(root, "token").has_value();
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

        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseChooseSeat(JsonView root) {
            const auto seat = GetParamInt(root, "seat");
            if (!seat) {
                return BadMessage("'seat' must be an integer");
            }
            return ClientMessage(ChooseSeatMessage{*seat});
        }

        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseAction(JsonView root) {
            const auto version = GetParamInt(root, "version");
            if (!version || *version < 0) {
                return BadMessage("'version' must be a non-negative integer");
            }
            const Optional<JsonView> action = FindParam(root, "action");
            if (!action || !action->IsObject()) {
                return BadMessage("'action' must be an object");
            }
            ActionMessage message;
            message.version = static_cast<uint64_t>(*version);
            message.action = CompactText(*action);
            return ClientMessage(std::move(message));
        }

        /// `options` of `lobby` and of a `rooms` listing: absent unless there are members
        [[nodiscard]] Optional<JsonPayload> PublicOptions(const String& options) {
            if (!options.empty() && options != kEmptyDocument) {
                return JsonPayload::Text(StringView(options.data(), options.size()));
            }
            return std::nullopt;
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
        [[nodiscard]] std::expected<int64_t, ControlError> RequireInt(JsonView root, StringView key, int64_t min, int64_t max) {
            const auto value = GetParamInt(root, key);
            if (!value || *value < min || *value > max) {
                return BadMessage(Format("'{}' must be an integer from {} to {}", key, min, max));
            }
            return *value;
        }

        [[nodiscard]] std::expected<bool, ControlError> RequireBool(JsonView root, StringView key) {
            const auto value = GetParamBool(root, key);
            if (!value) {
                return BadMessage(Format("'{}' must be a boolean", key));
            }
            return *value;
        }

        [[nodiscard]] std::expected<LobbySeat, ControlError> ParseLobbySeat(JsonView entry) {
            if (!entry.IsObject()) {
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

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseJoined(JsonView root) {
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

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseLobby(JsonView root) {
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
            const Optional<JsonView> seats = FindParam(root, "seats");
            if (!seats || !seats->IsArray() || seats->Size() > RoomLimits::kMaxLobbySeats) {
                return BadMessage(Format("'seats' must be an array of at most {} seats", RoomLimits::kMaxLobbySeats));
            }
            message.seats.reserve(seats->Size());
            for (const JsonView entry : seats->Items()) {
                auto seat = ParseLobbySeat(entry);
                if (!seat) {
                    return std::unexpected(seat.error());
                }
                message.seats.push_back(std::move(*seat));
            }
            if (const Optional<JsonView> options = FindParam(root, "options")) {
                if (!options->IsObject() || options->Size() > RoomLimits::kMaxLobbyOptions) {
                    return BadMessage(Format("'options' must be an object of at most {} members", RoomLimits::kMaxLobbyOptions));
                }
                // What a reader keeps of a `lobby`: the booleans and integers (the other members are left out, not refused)
                Vector<PublicOptionRef> kept;
                for (const JsonMember member : options->Members()) {
                    if (IsPublicOptionValue(member.value)) {
                        PublicOptionRef& option = kept.emplace_back();
                        member.key.ReadText(option.key);
                        option.value = member.value;
                    }
                }
                AppendPublicOptions(message.options, kept);
            }
            return ServerMessage(std::move(message));
        }

        [[nodiscard]] std::expected<RoomListing, ControlError> ParseRoomListing(JsonView entry) {
            if (!entry.IsObject()) {
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
            if (const Optional<JsonView> options = FindParam(entry, "options")) {
                if (!options->IsObject() || options->Size() > RoomLimits::kMaxLobbyOptions) {
                    return BadMessage(Format("'options' must be an object of at most {} members", RoomLimits::kMaxLobbyOptions));
                }
                Vector<PublicOptionRef> kept;
                for (const JsonMember member : options->Members()) {
                    if (!IsPublicOptionValue(member.value)) {
                        return BadMessage("'options' may hold only booleans and integers");
                    }
                    PublicOptionRef& option = kept.emplace_back();
                    member.key.ReadText(option.key);
                    option.value = member.value;
                }
                AppendPublicOptions(listing.options, kept);
            }
            return listing;
        }

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseRoomList(JsonView root) {
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
            const Optional<JsonView> rooms = FindParam(root, "rooms");
            if (!rooms || !rooms->IsArray() || rooms->Size() > RoomLimits::kMaxListedRooms) {
                return BadMessage(Format("'rooms' must be an array of at most {} rooms", RoomLimits::kMaxListedRooms));
            }
            message.rooms.reserve(rooms->Size());
            for (const JsonView entry : rooms->Items()) {
                auto listing = ParseRoomListing(entry);
                if (!listing) {
                    return std::unexpected(listing.error());
                }
                message.rooms.push_back(std::move(*listing));
            }
            return ServerMessage(std::move(message));
        }

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseView(JsonView root) {
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
            const Optional<JsonView> view = FindParam(root, "view");
            const Optional<JsonView> actions = FindParam(root, "actions");
            if (!view || !actions) {
                return BadMessage("'view' and 'actions' must be there");
            }
            auto viewDocument = PayloadToJson(*view);
            if (!viewDocument) {
                return std::unexpected(viewDocument.error());
            }
            auto actionsDocument = PayloadToJson(*actions);
            if (!actionsDocument) {
                return std::unexpected(actionsDocument.error());
            }
            message.view = std::move(*viewDocument);
            message.actions = std::move(*actionsDocument);
            return ServerMessage(std::move(message));
        }

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseError(JsonView root) {
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
            if (FindParam(root, "ref")) {
                auto ref = RequireString(root, "ref");
                if (!ref) {
                    return std::unexpected(ref.error());
                }
                message.ref = std::move(*ref);
            }
            return ServerMessage(std::move(message));
        }

        [[nodiscard]] std::expected<ServerMessage, ControlError> ParseLeft(JsonView root) {
            auto reason = RequireString(root, "reason");
            if (!reason) {
                return std::unexpected(reason.error());
            }
            return ServerMessage(LeftMessage{std::move(*reason)});
        }

    }  // namespace

    bool IsPublicOptionValue(JsonView value) {
        return value.IsBool() || value.AsInt64() || value.AsUint64();
    }

    void AppendPublicOptions(String& out, Vector<PublicOptionRef>& options) {
        if (options.empty()) {
            return;
        }
        // Alphabetical, as the DOM kept them (the bytes of the `lobby` message); of two equal keys the first of the document counts
        eastl::sort(options.begin(), options.end(), [](const PublicOptionRef& a, const PublicOptionRef& b) { return a.key != b.key ? a.key < b.key : a.value.Index() < b.value.Index(); });
        JsonWriter writer(out);
        writer.BeginObject();
        for (size_t i = 0; i < options.size(); ++i) {
            if (i == 0 || options[i].key != options[i - 1].key) {
                writer.Key(StringView(options[i].key.data(), options[i].key.size()));
                WriteJson(writer, options[i].value);
            }
        }
        writer.EndObject();
    }

    std::expected<ClientMessage, ControlError> ParseClientMessage(StringView text, size_t maxBytes) {
        JsonTape tape;
        return ParseClientMessage(tape, text, maxBytes);
    }

    std::expected<ClientMessage, ControlError> ParseClientMessage(JsonTape& tape, StringView text, size_t maxBytes) {
        const auto parsed = TapeMessage(tape, text, maxBytes, RoomLimits::kMaxClientJsonDepth);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        const JsonView root = *parsed;
        const auto version = GetParamInt(root, "v");
        if (!version) {
            return BadMessage("'v' must be an integer");
        }
        if (*version != kRoomProtocolVersion) {
            return Fail(RoomErrors::kUnsupportedVersion, Format("the server speaks protocol version {}", kRoomProtocolVersion));
        }
        const Optional<JsonView> type = FindParam(root, "type");
        if (!type || !type->IsString()) {
            return BadMessage("'type' must be a string");
        }
        if (type->TextEquals(RoomMessageTypes::kCreateRoom)) {
            return ParseCreateRoom(root);
        }
        if (type->TextEquals(RoomMessageTypes::kJoinRoom)) {
            return ParseJoinRoom(root);
        }
        if (type->TextEquals(RoomMessageTypes::kChooseSeat)) {
            return ParseChooseSeat(root);
        }
        if (type->TextEquals(RoomMessageTypes::kStart)) {
            return ClientMessage(StartMessage{});
        }
        if (type->TextEquals(RoomMessageTypes::kAction)) {
            return ParseAction(root);
        }
        if (type->TextEquals(RoomMessageTypes::kLeave)) {
            return ClientMessage(LeaveMessage{});
        }
        if (type->TextEquals(RoomMessageTypes::kPing)) {
            return ClientMessage(PingMessage{});
        }
        if (type->TextEquals(RoomMessageTypes::kListRooms)) {
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
        ScratchScope scratch;  // above the wire structs: they die first, then the scope rewinds
        JsonWriter writer(out);
        std::visit(overload{
                       [&](const CreateRoomMessage& m) {
                           RoomWireCreateRoom wire;
                           wire.game = TextView(m.game);
                           wire.name = TextView(m.name);
                           wire.options = JsonPayload::Text(DocumentText(m.options));
                           if (m.isPublic) {
                               wire._public = true;
                           }
                           wire.WriteJson(writer);
                       },
                       [&](const ActionMessage& m) {
                           RoomWireAction wire;
                           wire.action = JsonPayload::Text(DocumentText(m.action));
                           wire.version = m.version;
                           wire.WriteJson(writer);
                       },
                       [&](const JoinRoomMessage& m) {
                           RoomWireJoinRoom wire;
                           wire.code = TextView(m.code);
                           if (m.IsReconnect()) {
                               wire.token = TextView(m.token);
                           } else {
                               wire.name = TextView(m.name);
                           }
                           wire.WriteJson(writer);
                       },
                       [&](const ChooseSeatMessage& m) {
                           RoomWireChooseSeat wire;
                           wire.seat = m.seat;
                           wire.WriteJson(writer);
                       },
                       [&](const ListRoomsMessage& m) {
                           RoomWireListRooms wire;
                           wire.game = TextView(m.game);
                           wire.WriteJson(writer);
                       },
                       [&](const StartMessage&) { RoomWireStart().WriteJson(writer); },
                       [&](const LeaveMessage&) { RoomWireLeave().WriteJson(writer); },
                       [&](const PingMessage&) { RoomWirePing().WriteJson(writer); },
                   },
                   message);
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
        ScratchScope scratch;
        RoomWireJoined wire;
        wire.host = host;
        wire.player = player;
        wire.room = TextView(room);
        wire.seat = seat;
        wire.token = TextView(token);
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
        ScratchScope scratch;  // the seats are an `ArenaVector`; they go with the message
        RoomWireLobby wire;
        wire.game = TextView(lobby.game);
        wire.host = lobby.hostSeat;
        wire.options = PublicOptions(lobby.options);
        wire.phase = TextView(GetPhaseName(lobby.phase));
        wire.room = TextView(lobby.room);
        wire.seats.reserve(lobby.seats.size());
        for (const LobbySeat& seat : lobby.seats) {
            RoomWireSeat& wireSeat = wire.seats.emplace_back();
            if (seat.kind == SeatKind::Human) {
                wireSeat.connected = seat.connected;
                wireSeat.name = TextView(seat.name);
            }
            wireSeat.kind = TextView(GetSeatKindName(seat.kind));
            wireSeat.seat = seat.seat;
        }
        JsonWriter writer(out);
        wire.WriteJson(writer);
        EXPECT(writer.IsComplete());
    }

    String MakeLobbyMessage(const LobbySnapshot& lobby) {
        String out;
        AppendLobbyMessage(out, lobby);
        return out;
    }

    void AppendRoomListMessage(String& out, const RoomListMessage& list) {
        ScratchScope scratch;
        RoomWireRooms wire;
        wire.game = TextView(list.game);
        wire.more = list.more;
        wire.rooms.reserve(list.rooms.size());
        for (const RoomListing& listing : list.rooms) {
            RoomWireListing& wireListing = wire.rooms.emplace_back();
            wireListing.host = TextView(listing.host);
            wireListing.options = PublicOptions(listing.options);
            wireListing.room = TextView(listing.room);
            wireListing.seats = listing.seats;
            wireListing.started = listing.started;
            wireListing.taken = listing.taken;
        }
        JsonWriter writer(out);
        wire.WriteJson(writer);
        EXPECT(writer.IsComplete());
    }

    String MakeRoomListMessage(const RoomListMessage& list) {
        String out;
        AppendRoomListMessage(out, list);
        return out;
    }

    void AppendViewMessage(String& out, StringView room, int seat, uint64_t version, bool finished, const Json& view, const Json& actions) {
        ScratchScope scratch;
        RoomWireView wire;
        wire.actions = JsonPayload::Borrowed(actions);
        wire.finished = finished;
        wire.room = TextView(room);
        wire.seat = seat;
        wire.version = version;
        wire.view = JsonPayload::Borrowed(view);
        JsonWriter writer(out);
        wire.WriteJson(writer);
        EXPECT(writer.IsComplete());
    }

    String MakeViewMessage(StringView room, int seat, uint64_t version, bool finished, const Json& view, const Json& actions) {
        String out;
        AppendViewMessage(out, room, seat, version, finished, view, actions);
        return out;
    }

    void AppendViewMessage(String& out, StringView room, int seat, uint64_t version, bool finished, const SeatedView& view, const SeatedActions& actions) {
        ScratchScope scratch;
        // The payloads are views of these named callables, which outlive the one write below (JsonPayload.h: a temporary lambda does not compile)
        const auto writeView = [&view](JsonWriter& writer) -> std::expected<void, ControlError> {
            view.WriteJson(writer);
            return {};
        };
        const auto writeActions = [&actions](JsonWriter& writer) -> std::expected<void, ControlError> {
            actions.WriteJson(writer);
            return {};
        };
        RoomWireView wire;
        wire.actions = JsonPayload::Streamed(writeActions);
        wire.finished = finished;
        wire.room = TextView(room);
        wire.seat = seat;
        wire.version = version;
        wire.view = JsonPayload::Streamed(writeView);
        JsonWriter writer(out);
        wire.WriteJson(writer);
        EXPECT(writer.IsComplete());
    }

    String MakeViewMessage(StringView room, int seat, uint64_t version, bool finished, const SeatedView& view, const SeatedActions& actions) {
        String out;
        AppendViewMessage(out, room, seat, version, finished, view, actions);
        return out;
    }

    void AppendErrorMessage(String& out, StringView code, StringView message, StringView ref) {
        ScratchScope scratch;
        RoomWireError wire;
        wire.code = TextView(code);
        wire.message = TextView(message);
        if (!ref.empty()) {
            wire.ref = TextView(ref);
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
        ScratchScope scratch;
        RoomWireLeft wire;
        wire.reason = TextView(reason);
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
        ScratchScope scratch;
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
        JsonTape tape;
        return ParseServerMessage(tape, text, maxBytes);
    }

    std::expected<ServerMessage, ControlError> ParseServerMessage(JsonTape& tape, StringView text, size_t maxBytes) {
        const auto parsed = TapeMessage(tape, text, maxBytes, RoomLimits::kMaxServerJsonDepth);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        const JsonView root = *parsed;
        const auto version = GetParamInt(root, "v");
        if (!version) {
            return BadMessage("'v' must be an integer");
        }
        if (*version != kRoomProtocolVersion) {
            return Fail(RoomErrors::kUnsupportedVersion, Format("this client speaks protocol version {}", kRoomProtocolVersion));
        }
        const Optional<JsonView> type = FindParam(root, "type");
        if (!type || !type->IsString()) {
            return BadMessage("'type' must be a string");
        }
        if (type->TextEquals(RoomMessageTypes::kJoined)) {
            return ParseJoined(root);
        }
        if (type->TextEquals(RoomMessageTypes::kLobby)) {
            return ParseLobby(root);
        }
        if (type->TextEquals(RoomMessageTypes::kView)) {
            return ParseView(root);
        }
        if (type->TextEquals(RoomMessageTypes::kError)) {
            return ParseError(root);
        }
        if (type->TextEquals(RoomMessageTypes::kLeft)) {
            return ParseLeft(root);
        }
        if (type->TextEquals(RoomMessageTypes::kPong)) {
            return ServerMessage(PongMessage{});
        }
        if (type->TextEquals(RoomMessageTypes::kRooms)) {
            return ParseRoomList(root);
        }
        UnknownServerMessage unknown;
        type->ReadText(unknown.type);
        return ServerMessage(std::move(unknown));
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
