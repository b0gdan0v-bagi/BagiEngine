#include "FuzzRoomInput.h"
#include "FuzzTargets.h"

#include <BECore/Rooms/WireJudge.h>
#include <EASTL/hash_set.h>
#include <RoomServer/DemoCardGame.h>
#include <Rooms/RoomCode.h>
#include <Rooms/RoomHub.h>
#include <Rooms/RoomProtocol.h>

namespace BECore::Fuzz {

    namespace {

        using namespace RoomHubInput;

        constexpr RoomTime kStart{std::chrono::seconds(1000)};
        /// After the stream: everyone is gone and this much time passes in each of a few ticks, which is more than every timeout of the hub together
        constexpr std::chrono::hours kFinalStep{2};
        constexpr int kFinalTicks = 3;

        bool IsHexDigit(char letter) {
            return (letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'f');
        }

        bool IsWordLetter(char letter) {
            return IsHexDigit(letter) || (letter >= 'g' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || letter == '_';
        }

        /// The runs of exactly kTokenLength lower-case hexadecimal digits that are whole words in @p text: what could be a reconnect token
        eastl::vector<eastl::string> FindTokenLikeWords(eastl::string_view text) {
            eastl::vector<eastl::string> found;
            for (size_t at = 0; at < text.size();) {
                if (!IsWordLetter(text[at])) {
                    ++at;
                    continue;
                }
                size_t end = at;
                bool allHex = true;
                while (end < text.size() && IsWordLetter(text[end])) {
                    allHex = allHex && IsHexDigit(text[end]);
                    ++end;
                }
                if (allHex && end - at == RoomLimits::kTokenLength) {
                    found.emplace_back(text.data() + at, end - at);
                }
                at = end;
            }
            return found;
        }

        /// What the target knows of one connection from what the hub sent it and what it sent the hub
        struct ConnectionModel {
            bool open = false;
            eastl::string lastSent;
            eastl::string room;  ///< From `joined`, until `left`; empty when none
            int joinedSeat = -1;
            int viewSeat = -1;  ///< The seat of the last `view` (-1 before the first)
            bool askedForSeat = false;
            eastl::vector<eastl::string> ownTokens;
        };

        /// What the last text of a connection, as the hub read it, said about listing the room: none when it was no create_room
        Optional<bool> AsksToListTheRoom(eastl::string_view lastSent) {
            const auto parsed = ParseClientMessage(lastSent, kMaxMessageBytes);
            const auto* create = parsed ? std::get_if<CreateRoomMessage>(&*parsed) : nullptr;
            if (create == nullptr) {
                return std::nullopt;
            }
            return create->isPublic;
        }

        class HubRun {
        public:
            HubRun() : _hub(_registry, MakeSettings(), MakeRandom(), MakeSink()) {
                const bool registered = _registry.Register(SeatedGameInfo{"demo", DemoCardGame::kSeatCount}, [] { return eastl::unique_ptr<ISeatedGame>(eastl::make_unique<DemoCardGame>()); });
                FUZZ_CHECK(registered);
            }

            void Apply(Op op, uint8_t number, uint16_t length, eastl::string_view text) {
                const ConnectionId id = ConnectionOf(number);
                switch (op) {
                    case Op::Message: {
                        ConnectionModel& connection = _connections[id];
                        connection.lastSent.assign(text.data(), text.size());
                        connection.askedForSeat = connection.askedForSeat || text.find("choose_seat") != eastl::string_view::npos;
                        _hub.OnMessage(id, text, _now);
                        break;
                    }
                    case Op::Connect: {
                        ConnectionModel& connection = _connections[id];
                        if (connection.open) {
                            return;  // a transport never reuses a number while it is alive
                        }
                        connection = ConnectionModel{};
                        connection.open = true;
                        _hub.OnConnected(id, _now);
                        break;
                    }
                    case Op::Disconnect:
                        _connections[id].open = false;
                        _hub.OnDisconnected(id, _now);
                        break;
                    case Op::Tick:
                        _now += std::chrono::milliseconds(length);
                        _hub.Tick(_now);
                        break;
                }
                CheckState();
            }

            /// Everyone leaves and a long time passes: nothing may be left in the hub
            void Finish() {
                for (auto& [id, connection] : _connections) {
                    if (connection.open) {
                        connection.open = false;
                        _hub.OnDisconnected(id, _now);
                    }
                }
                for (int tick = 0; tick < kFinalTicks; ++tick) {
                    _now += kFinalStep;
                    _hub.Tick(_now);
                }
                FUZZ_CHECK(_hub.GetConnectionCount() == 0);
                FUZZ_CHECK(_hub.GetRoomCount() == 0);
            }

        private:
            HubSink MakeSink() {
                HubSink sink;
                sink.send = [this](ConnectionId id, eastl::string text) { Judge(id, text); };
                sink.close = [this](ConnectionId id, eastl::string reason) {
                    FUZZ_CHECK(!reason.empty());
                    ConnectionModel& connection = _connections[id];
                    connection.open = false;
                    connection.room.clear();
                };
                return sink;
            }

            /// Called for every text the hub sends: the properties of a message to one connection
            void Judge(ConnectionId id, const eastl::string& text) {
                ConnectionModel& connection = _connections[id];

                const auto parsed = ParseJson(text);
                FUZZ_CHECK(parsed.has_value() && parsed->is_object());
                FUZZ_CHECK(GetJsonInt(*parsed, "v") == Optional<int64_t>(kRoomProtocolVersion));
                const auto type = GetJsonString(*parsed, "type");
                FUZZ_CHECK(type.has_value() && !type->empty());

                if (*type == RoomMessageTypes::kJoined) {
                    OnJoined(connection, *parsed);
                } else if (*type == RoomMessageTypes::kView) {
                    OnView(connection, *parsed);
                } else if (*type == RoomMessageTypes::kRooms) {
                    OnRooms(connection, *parsed);
                } else if (*type == RoomMessageTypes::kLeft) {
                    connection.room.clear();
                    connection.joinedSeat = -1;
                    connection.viewSeat = -1;
                }

                // A secret goes to its owner only: a token in a text is one the connection holds or one it sent itself (an error may repeat what the sender wrote)
                for (const eastl::string& word : FindTokenLikeWords(text)) {
                    const bool own = std::find(connection.ownTokens.begin(), connection.ownTokens.end(), word) != connection.ownTokens.end();
                    FUZZ_CHECK(own || connection.lastSent.find(word) != eastl::string::npos);
                }

                // A card the connection may not know: the judge of the socket scenarios, on the text alone
                const int seat = connection.viewSeat >= 0 ? connection.viewSeat : connection.joinedSeat;
                const std::vector<std::string> leaked = Tests::FindLeakedCardIds(std::string_view(text.data(), text.size()), seat);
                for (const std::string& card : leaked) {
                    // An error that quotes the card the client itself sent tells it nothing
                    FUZZ_CHECK(*type == RoomMessageTypes::kError && connection.lastSent.find(card.c_str()) != eastl::string::npos);
                }
            }

            void OnJoined(ConnectionModel& connection, const Json& message) {
                const auto room = GetJsonString(message, "room");
                const auto token = GetJsonString(message, "token");
                const auto player = GetJsonInt(message, "player");
                const auto seat = GetJsonInt(message, "seat");
                FUZZ_CHECK(room && token && player && seat);
                FUZZ_CHECK(IsWellFormedToken(*token));
                FUZZ_CHECK(*seat >= 0 && *seat < DemoCardGame::kSeatCount);

                // A token names one member of one room, wherever it is told again; only the owner, who sent it, gets it again
                const eastl::string member = Format("{}/{}", *room, *player);
                const auto known = _tokenOwners.find(*token);
                if (known == _tokenOwners.end()) {
                    _tokenOwners.emplace(*token, member);
                } else {
                    FUZZ_CHECK(known->second == member);
                    FUZZ_CHECK(connection.lastSent.find(*token) != eastl::string::npos);
                }
                // The answer to a create_room is the only way a code becomes listable, and only when it asked for it (a code can come round again for a private room)
                if (const auto listed = AsksToListTheRoom(connection.lastSent)) {
                    if (*listed) {
                        _publicRooms.insert(*room);
                    } else {
                        _publicRooms.erase(*room);
                    }
                }
                connection.room = *room;
                connection.joinedSeat = static_cast<int>(*seat);
                connection.viewSeat = -1;
                connection.askedForSeat = false;
                connection.ownTokens.push_back(*token);
            }

            /// A list goes to a connection in no room, holds only rooms that their host made public (a private code is told to its members and nobody else), and
            /// nothing the hub could not offer: the cap, a free seat, a lobby (the demo game lists no started room)
            void OnRooms(const ConnectionModel& connection, const Json& message) {
                FUZZ_CHECK(connection.room.empty());
                FUZZ_CHECK(GetJsonString(message, "game") == Optional<eastl::string>("demo"));
                const Json* rooms = FindJsonMember(message, "rooms");
                FUZZ_CHECK(rooms != nullptr && rooms->is_array() && rooms->size() <= RoomLimits::kMaxListedRooms);
                eastl::hash_set<eastl::string> seen;
                for (const Json& entry : *rooms) {
                    const auto code = GetJsonString(entry, "room");
                    const auto taken = GetJsonInt(entry, "taken");
                    const auto seats = GetJsonInt(entry, "seats");
                    FUZZ_CHECK(code && taken && seats);
                    FUZZ_CHECK(_publicRooms.find(*code) != _publicRooms.end());
                    FUZZ_CHECK(_hub.HasRoom(*code));
                    FUZZ_CHECK(seen.insert(*code).second);
                    FUZZ_CHECK(*seats == DemoCardGame::kSeatCount);
                    FUZZ_CHECK(*taken >= 0 && *taken < *seats);
                    FUZZ_CHECK(GetJsonString(entry, "host").has_value());
                    FUZZ_CHECK(GetJsonBool(entry, "started") == Optional<bool>(false));
                }
            }

            void OnView(ConnectionModel& connection, const Json& message) {
                const auto seat = GetJsonInt(message, "seat");
                FUZZ_CHECK(seat && *seat >= 0 && *seat < DemoCardGame::kSeatCount);
                const int viewSeat = static_cast<int>(*seat);
                // The seat of a member moves only by its own `choose_seat`, and not at all once the game has started
                if (connection.viewSeat >= 0) {
                    FUZZ_CHECK(viewSeat == connection.viewSeat);
                } else if (!connection.askedForSeat) {
                    FUZZ_CHECK(viewSeat == connection.joinedSeat);
                }
                connection.viewSeat = viewSeat;
            }

            /// After every operation: what the hub counts agrees with what the transport was told
            void CheckState() {
                size_t open = 0;
                for (const auto& [id, connection] : _connections) {
                    open += connection.open ? 1 : 0;
                    if (connection.open && !connection.room.empty()) {
                        FUZZ_CHECK(_hub.HasRoom(connection.room));  // a member of a room that is gone was told `left`
                    }
                }
                FUZZ_CHECK(_hub.GetConnectionCount() == open);
                FUZZ_CHECK(_hub.GetConnectionCount() <= kMaxConnections);
                FUZZ_CHECK(_hub.GetRoomCount() <= kMaxRooms);
            }

            // The registry first (the hub keeps a reference), the hub last
            SeatedGameRegistry _registry;
            eastl::map<ConnectionId, ConnectionModel> _connections;
            eastl::hash_map<eastl::string, eastl::string> _tokenOwners;
            eastl::hash_set<eastl::string> _publicRooms;  ///< The codes that were answered to a create_room with "public":true
            RoomTime _now = kStart;
            RoomHub _hub;
        };

    }  // namespace

    void FuzzRoomHub(const uint8_t* data, size_t size) {
        HubRun run;
        size_t at = 0;
        for (size_t operation = 0; operation < kMaxOperations && at + kOperationHeaderBytes <= size; ++operation) {
            const auto op = static_cast<Op>(data[at] & kOpMask);
            const auto number = static_cast<uint8_t>(data[at] >> kConnectionShift);
            const auto length = static_cast<uint16_t>(data[at + 1] | (data[at + 2] << 8));
            at += kOperationHeaderBytes;

            eastl::string_view text;
            if (op == Op::Message) {
                const size_t textBytes = std::min<size_t>(length, size - at);
                text = eastl::string_view(reinterpret_cast<const char*>(data + at), textBytes);
                at += textBytes;
            }
            run.Apply(op, number, length, text);
        }
        run.Finish();
    }

}  // namespace BECore::Fuzz
