#include "FuzzRoomInput.h"
#include "FuzzTargets.h"

#include <Rooms/RoomCode.h>
#include <Rooms/RoomErrors.h>
#include <Rooms/RoomProtocol.h>

namespace BECore::Fuzz {

    namespace {

        using RoomProtocolInput::kMessageLimits;
        using RoomProtocolInput::kServerBit;

        /// The codes a refusal of a client message may carry (the ones ParseClientMessage documents)
        bool IsClientRefusalCode(eastl::string_view code) {
            return code == RoomErrors::kBadMessage || code == RoomErrors::kUnsupportedVersion || code == RoomErrors::kMessageTooLarge || code == RoomErrors::kBadName ||
                   code == RoomErrors::kUnknownRoom || code == RoomErrors::kBadToken;
        }

        bool IsServerRefusalCode(eastl::string_view code) {
            return code == RoomErrors::kBadMessage || code == RoomErrors::kUnsupportedVersion || code == RoomErrors::kMessageTooLarge;
        }

        /// A refusal reaches a log and a reply: a message of printable ASCII, not empty, not long
        bool IsTidyRefusal(const ControlError& error, bool (*isKnownCode)(eastl::string_view)) {
            constexpr size_t kMaxRefusalBytes = 200;
            const eastl::string_view message = error.message;
            return isKnownCode(error.code) && !message.empty() && message.size() <= kMaxRefusalBytes &&
                   std::all_of(message.begin(), message.end(), [](char letter) { return letter >= ' ' && letter <= '~'; });
        }

        // ---- Strict UTF-8, from the ranges of the Unicode standard (Table 3-7), not the code under test ------------------------------

        /// The length of the well-formed character at @p pos, or 0
        size_t Utf8Length(eastl::string_view text, size_t pos) {
            const auto at = [&](size_t index) -> int { return index < text.size() ? static_cast<unsigned char>(text[index]) : -1; };
            const auto in = [](int value, int low, int high) { return value >= low && value <= high; };
            const int b0 = at(pos);
            if (in(b0, 0x00, 0x7F)) {
                return 1;
            }
            if (in(b0, 0xC2, 0xDF)) {
                return in(at(pos + 1), 0x80, 0xBF) ? 2 : 0;
            }
            if (in(b0, 0xE0, 0xEF)) {
                const int low = b0 == 0xE0 ? 0xA0 : 0x80;
                const int high = b0 == 0xED ? 0x9F : 0xBF;
                return in(at(pos + 1), low, high) && in(at(pos + 2), 0x80, 0xBF) ? 3 : 0;
            }
            if (in(b0, 0xF0, 0xF4)) {
                const int low = b0 == 0xF0 ? 0x90 : 0x80;
                const int high = b0 == 0xF4 ? 0x8F : 0xBF;
                return in(at(pos + 1), low, high) && in(at(pos + 2), 0x80, 0xBF) && in(at(pos + 3), 0x80, 0xBF) ? 4 : 0;
            }
            return 0;
        }

        bool IsValidUtf8WithoutControls(eastl::string_view text) {
            for (size_t pos = 0; pos < text.size();) {
                const size_t length = Utf8Length(text, pos);
                if (length == 0) {
                    return false;
                }
                const auto byteAt = [&](size_t index) { return static_cast<unsigned char>(text[pos + index]); };
                const bool isC0OrDelete = length == 1 && (byteAt(0) < 0x20 || byteAt(0) == 0x7F);
                const bool isC1 = length == 2 && byteAt(0) == 0xC2 && byteAt(1) >= 0x80 && byteAt(1) <= 0x9F;
                if (isC0OrDelete || isC1) {
                    return false;
                }
                pos += length;
            }
            return true;
        }

        // ---- Client messages ------------------------------------------------------------------------------------------------------------

        bool Same(const CreateRoomMessage& a, const CreateRoomMessage& b) {
            return a.game == b.game && a.name == b.name && a.options == b.options && a.isPublic == b.isPublic;
        }
        bool Same(const JoinRoomMessage& a, const JoinRoomMessage& b) {
            return a.code == b.code && a.name == b.name && a.token == b.token;
        }
        bool Same(const ChooseSeatMessage& a, const ChooseSeatMessage& b) {
            return a.seat == b.seat;
        }
        bool Same(const ActionMessage& a, const ActionMessage& b) {
            return a.version == b.version && a.action == b.action;
        }
        bool Same(const StartMessage&, const StartMessage&) {
            return true;
        }
        bool Same(const LeaveMessage&, const LeaveMessage&) {
            return true;
        }
        bool Same(const PingMessage&, const PingMessage&) {
            return true;
        }
        bool Same(const ListRoomsMessage& a, const ListRoomsMessage& b) {
            return a.game == b.game;
        }

        bool SameClientMessage(const ClientMessage& a, const ClientMessage& b) {
            return std::visit(
                [&b](const auto& left) {
                    const auto* right = std::get_if<std::decay_t<decltype(left)>>(&b);
                    return right != nullptr && Same(left, *right);
                },
                a);
        }

        void CheckClientMessage(eastl::string_view text, size_t maxBytes) {
            const auto parsed = ParseClientMessage(text, maxBytes);
            if (!parsed.has_value()) {
                FUZZ_CHECK(IsTidyRefusal(parsed.error(), &IsClientRefusalCode));
                FUZZ_CHECK((text.size() > maxBytes) == (parsed.error().code == RoomErrors::kMessageTooLarge));
                return;
            }
            FUZZ_CHECK(text.size() <= maxBytes);
            FUZZ_CHECK(!GetClientMessageType(*parsed).empty());

            // The text a client sends for it reads back as the same message (the limit is lifted: the compact form is never longer, but the limit is not what is tested here)
            const eastl::string serialized = SerializeClientMessage(*parsed);
            const auto again = ParseClientMessage(serialized, std::max(maxBytes, serialized.size()));
            FUZZ_CHECK(again.has_value());
            FUZZ_CHECK(SameClientMessage(*parsed, *again));
            FUZZ_CHECK(SerializeClientMessage(*again) == serialized);

            // What the server stores about a sender is clean: a name the sanitizer would keep as it is, a code and a token of the right shape
            if (const auto* create = std::get_if<CreateRoomMessage>(&*parsed)) {
                FUZZ_CHECK(!create->game.empty() && create->game.size() <= RoomLimits::kMaxGameNameBytes);
                FUZZ_CHECK(SanitizePlayerName(create->name) == Optional<eastl::string>(create->name));
                FUZZ_CHECK(create->options.is_object());
            } else if (const auto* join = std::get_if<JoinRoomMessage>(&*parsed)) {
                FUZZ_CHECK(join->code.size() == RoomLimits::kRoomCodeLength);
                FUZZ_CHECK(join->name.empty() != join->token.empty());
                FUZZ_CHECK(join->token.empty() || join->token.size() == RoomLimits::kTokenLength);
                FUZZ_CHECK(join->name.empty() || SanitizePlayerName(join->name) == Optional<eastl::string>(join->name));
            } else if (const auto* action = std::get_if<ActionMessage>(&*parsed)) {
                FUZZ_CHECK(action->action.is_object());
            } else if (const auto* list = std::get_if<ListRoomsMessage>(&*parsed)) {
                FUZZ_CHECK(!list->game.empty() && list->game.size() <= RoomLimits::kMaxGameNameBytes);
            }
        }

        // ---- Server messages ------------------------------------------------------------------------------------------------------------

        /// The text the server would send for a message it read: the builders are the inverse of the parser. Empty for a type the builders do not know
        eastl::string RebuildServerText(const ServerMessage& message) {
            if (const auto* joined = std::get_if<JoinedMessage>(&message)) {
                return MakeJoinedMessage(joined->room, joined->player, joined->token, joined->seat, joined->host);
            }
            if (const auto* lobby = std::get_if<LobbyMessage>(&message)) {
                return MakeLobbyMessage(*lobby);
            }
            if (const auto* view = std::get_if<ViewMessage>(&message)) {
                return MakeViewMessage(view->room, view->seat, view->version, view->finished, view->view, view->actions);
            }
            if (const auto* error = std::get_if<ErrorMessage>(&message)) {
                return MakeErrorMessage(error->code, error->message, error->ref);
            }
            if (const auto* left = std::get_if<LeftMessage>(&message)) {
                return MakeLeftMessage(left->reason);
            }
            if (std::holds_alternative<PongMessage>(message)) {
                return MakePongMessage();
            }
            if (const auto* rooms = std::get_if<RoomListMessage>(&message)) {
                return MakeRoomListMessage(*rooms);
            }
            return {};
        }

        /// What a client that read a `rooms` message may rely on: the cap, a code that is a code, counts that fit, options that are plain numbers and flags
        void CheckRoomList(const RoomListMessage& list) {
            FUZZ_CHECK(list.rooms.size() <= RoomLimits::kMaxListedRooms);
            for (const RoomListing& listing : list.rooms) {
                FUZZ_CHECK(NormalizeRoomCode(listing.room) == Optional<eastl::string>(listing.room));
                FUZZ_CHECK(listing.seats >= 1 && static_cast<size_t>(listing.seats) <= RoomLimits::kMaxLobbySeats);
                FUZZ_CHECK(listing.taken >= 0 && listing.taken <= listing.seats);
                FUZZ_CHECK(listing.options.is_object() && listing.options.size() <= RoomLimits::kMaxLobbyOptions);
                for (auto it = listing.options.begin(); it != listing.options.end(); ++it) {
                    FUZZ_CHECK(it->is_boolean() || it->is_number_integer());
                }
            }
        }

        void CheckServerMessage(eastl::string_view text, size_t maxBytes) {
            const auto parsed = ParseServerMessage(text, maxBytes);
            if (!parsed.has_value()) {
                FUZZ_CHECK(IsTidyRefusal(parsed.error(), &IsServerRefusalCode));
                FUZZ_CHECK((text.size() > maxBytes) == (parsed.error().code == RoomErrors::kMessageTooLarge));
                return;
            }
            FUZZ_CHECK(text.size() <= maxBytes);
            FUZZ_CHECK(std::holds_alternative<UnknownServerMessage>(*parsed) || !GetServerMessageType(*parsed).empty());  // an unknown type may be any string, an empty one too

            if (const auto* joined = std::get_if<JoinedMessage>(&*parsed)) {
                FUZZ_CHECK(joined->token.size() == RoomLimits::kTokenLength);
            } else if (const auto* lobby = std::get_if<LobbyMessage>(&*parsed)) {
                FUZZ_CHECK(lobby->seats.size() <= RoomLimits::kMaxLobbySeats);
            } else if (const auto* rooms = std::get_if<RoomListMessage>(&*parsed)) {
                CheckRoomList(*rooms);
            }

            // What the builders make of a message that was read reads back, and builds the same text again
            const eastl::string rebuilt = RebuildServerText(*parsed);
            if (rebuilt.empty()) {
                FUZZ_CHECK(std::holds_alternative<UnknownServerMessage>(*parsed));
                return;
            }
            const auto again = ParseServerMessage(rebuilt, std::max(maxBytes, rebuilt.size()));
            FUZZ_CHECK(again.has_value());
            FUZZ_CHECK(again->index() == parsed->index());
            FUZZ_CHECK(RebuildServerText(*again) == rebuilt);
        }

        void CheckPlayerName(eastl::string_view text) {
            const auto name = SanitizePlayerName(text);
            if (!name.has_value()) {
                return;
            }
            FUZZ_CHECK(name->size() >= RoomLimits::kMinNameBytes && name->size() <= RoomLimits::kMaxNameBytes);
            FUZZ_CHECK(IsValidUtf8WithoutControls(*name));
            FUZZ_CHECK((*name)[0] != ' ' && (*name)[name->size() - 1] != ' ');
            FUZZ_CHECK(text.find(name->c_str()) != eastl::string_view::npos);  // the name is a piece of the input: nothing is added
            FUZZ_CHECK(SanitizePlayerName(*name) == name);                     // idempotent
        }

    }  // namespace

    void FuzzRoomProtocol(const uint8_t* data, size_t size) {
        if (size == 0) {
            return;
        }
        const bool server = (data[0] & kServerBit) != 0;
        const size_t limitIndex = (data[0] >> RoomProtocolInput::kLimitShift) % std::size(kMessageLimits);
        const eastl::string_view text(reinterpret_cast<const char*>(data + 1), size - 1);

        const size_t maxBytes = kMessageLimits[limitIndex] != 0 ? kMessageLimits[limitIndex] : (server ? RoomLimits::kMaxServerMessageBytes : RoomLimits::kMaxMessageBytes);
        if (server) {
            CheckServerMessage(text, maxBytes);
        } else {
            CheckClientMessage(text, maxBytes);
        }
        CheckPlayerName(text);
    }

}  // namespace BECore::Fuzz
