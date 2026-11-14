#pragma once

#include "FakeSeatedGame.h"
#include "TestRandom.h"

#include <Common/Printers.h>
#include <Rooms/RoomErrors.h>
#include <Rooms/RoomHub.h>
#include <Rooms/RoomProtocol.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// One message a client received, parsed
    struct ReceivedMessage {
        String text;
        Json json;
        String type;

        [[nodiscard]] String GetString(StringView key) const {
            return GetJsonString(json, key).value_or(String());
        }

        [[nodiscard]] int64_t GetInt(StringView key, int64_t fallback = -1000) const {
            return GetJsonInt(json, key).value_or(fallback);
        }
    };

    /// A fake client connection: records everything the hub sent it and whether the hub closed it
    struct TestClient {
        ConnectionId id = 0;
        Vector<ReceivedMessage> received;
        bool closed = false;
        String closeReason;
        /// Learned from the messages: the room it is in and its seat ("" and -1 when none)
        String roomCode;
        int seat = -1;
        String token;
        /// The last text the client sent: an `error` may repeat what the client itself named (a card it does not hold), which tells it nothing
        String lastSent;

        [[nodiscard]] size_t Count(StringView type) const {
            size_t count = 0;
            for (const ReceivedMessage& message : received) {
                count += message.type == type ? 1 : 0;
            }
            return count;
        }

        /// The last message of the type, or null
        [[nodiscard]] const ReceivedMessage* Last(StringView type) const {
            for (size_t i = received.size(); i > 0; --i) {
                if (received[i - 1].type == type) {
                    return &received[i - 1];
                }
            }
            return nullptr;
        }

        /// The `code` of the last `error` message ("" when none)
        [[nodiscard]] String LastErrorCode() const {
            const ReceivedMessage* error = Last(RoomMessageTypes::kError);
            return error ? error->GetString("code") : String();
        }

        /// The types received, in order, as one text ("joined,lobby,view")
        [[nodiscard]] String Types() const {
            String result;
            for (const ReceivedMessage& message : received) {
                if (!result.empty()) {
                    result += ",";
                }
                result += message.type;
            }
            return result;
        }

        void Clear() {
            received.clear();
        }
    };

    /**
     * @brief A RoomHub over the fake game with fake clients, an injected clock and a leak check on the sink side.
     *
     * Every text the hub sends a client is judged, inside the send callback and against the live game of that client's room, by
     * FindLeakedCards (hidden hands are what the game holds *now*, so a check made after the fact would find nothing). A leak is a
     * test failure unless failOnLeak is off, then it is only counted (the negative control with the leaky game).
     */
    class HubHarness {
    public:
        explicit HubHarness(RoomHubSettings settings = MakeSettings(), bool leaky = false, RandomFn random = MakeSequence(42)) : _hub(_registry, settings, std::move(random), MakeSink()) {
            const auto factory = [this, leaky]() -> UniquePtr<ISeatedGame> {
                const String room = _startingRoom;
                if (leaky) {
                    return eastl::make_unique<Tracked<LeakyFakeSeatedGame>>(this, room);
                }
                return eastl::make_unique<Tracked<FakeSeatedGame>>(this, room);
            };
            EXPECT_TRUE(_registry.Register(SeatedGameInfo{"fake", FakeSeatedGame::kSeatCount}, factory));
            // The same game with public options: the lobby shows `open` and `count` of `create_room`
            EXPECT_TRUE(_registry.Register(SeatedGameInfo{"fake-public", FakeSeatedGame::kSeatCount, {"open", "count"}}, factory));
            // The same game that also lists its started public rooms that have a free seat
            EXPECT_TRUE(_registry.Register(SeatedGameInfo{"fake-started", FakeSeatedGame::kSeatCount, {}, true}, factory));
        }

        /// Small times for tests: no computer delay, the usual grace and timeouts
        static RoomHubSettings MakeSettings() {
            RoomHubSettings settings;
            settings.timings = RoomTimings{std::chrono::seconds(30), std::chrono::minutes(10), std::chrono::milliseconds(0)};
            return settings;
        }

        RoomHub& Hub() {
            return _hub;
        }

        [[nodiscard]] RoomTime Now() const {
            return _now;
        }

        void Advance(std::chrono::milliseconds step) {
            _now += step;
        }

        /// Advances the clock and ticks the hub
        void Tick(std::chrono::milliseconds step = std::chrono::milliseconds(0)) {
            _now += step;
            _hub.Tick(_now);
        }

        /// @param address The client's address handed to the hub ("" is unknown: no per-address rule)
        ConnectionId Connect(StringView address = {}) {
            const ConnectionId id = _nextConnection++;
            TestClient client;
            client.id = id;
            _clients.emplace(id, std::move(client));
            _hub.OnConnected(id, _now, address);
            return id;
        }

        void Disconnect(ConnectionId id) {
            _hub.OnDisconnected(id, _now);
        }

        TestClient& Client(ConnectionId id) {
            return _clients[id];
        }

        /// Every client the harness has made, with everything each was sent
        [[nodiscard]] const Map<ConnectionId, TestClient>& Clients() const {
            return _clients;
        }

        /// Sends raw text as if it came from the connection
        void SendText(ConnectionId id, StringView text) {
            _startingRoom = Client(id).roomCode;
            Client(id).lastSent = String(text);
            _hub.OnMessage(id, text, _now);
        }

        void Send(ConnectionId id, const ClientMessage& message) {
            SendText(id, SerializeClientMessage(message));
        }

        void Create(ConnectionId id, StringView name, StringView game = "fake", Json options = Json::object(), bool isPublic = false) {
            Send(id, CreateRoomMessage{String(game), String(name), std::move(options), isPublic});
        }

        /// `list_rooms` for @p game
        void List(ConnectionId id, StringView game = "fake") {
            Send(id, ListRoomsMessage{String(game)});
        }

        void JoinByName(ConnectionId id, StringView code, StringView name) {
            JoinRoomMessage message;
            message.code = String(code);
            message.name = String(name);
            Send(id, message);
        }

        void JoinByToken(ConnectionId id, StringView code, StringView token) {
            JoinRoomMessage message;
            message.code = String(code);
            message.token = String(token);
            Send(id, message);
        }

        void Start(ConnectionId id) {
            Send(id, StartMessage{});
        }

        void Leave(ConnectionId id) {
            Send(id, LeaveMessage{});
        }

        void Ping(ConnectionId id) {
            Send(id, PingMessage{});
        }

        /// Creates a room with a host and @p guests more humans; returns the connections (host first). The room is in the lobby.
        Vector<ConnectionId> MakeLobby(int humans) {
            Vector<ConnectionId> ids;
            const ConnectionId host = Connect();
            Create(host, "P0");
            ids.push_back(host);
            const String code = Client(host).roomCode;
            for (int i = 1; i < humans; ++i) {
                const ConnectionId guest = Connect();
                JoinByName(guest, code, Format("P{}", i).c_str());
                ids.push_back(guest);
            }
            return ids;
        }

        /// The live game of a room, or null
        [[nodiscard]] FakeSeatedGame* GetGame(const String& room) const {
            const auto found = _games.find(room);
            return found == _games.end() ? nullptr : found->second;
        }

        /// The texts that contained a hidden card (counted only)
        [[nodiscard]] size_t GetLeakCount() const {
            return _leaks;
        }

        void SetFailOnLeak(bool fail) {
            _failOnLeak = fail;
        }

        /// A human plays: the first action its last view offers, with the version of that view. False when it cannot act.
        bool PlayFirstAction(ConnectionId id) {
            const ReceivedMessage* view = Client(id).Last(RoomMessageTypes::kView);
            if (!view) {
                return false;
            }
            const Json* actions = FindJsonMember(view->json, "actions");
            if (!actions || !actions->is_array() || actions->empty()) {
                return false;
            }
            ActionMessage action;
            action.version = static_cast<uint64_t>(view->GetInt("version", 0));
            action.action = (*actions)[0];
            Send(id, action);
            return true;
        }

    private:
        template <typename GameType>
        class Tracked : public GameType {
        public:
            Tracked(HubHarness* harness, String room) : _harness(harness), _room(std::move(room)) {
                _harness->_games[_room] = this;
            }

            ~Tracked() override {
                const auto found = _harness->_games.find(_room);
                if (found != _harness->_games.end() && found->second == this) {
                    _harness->_games.erase(found);
                }
            }

        private:
            HubHarness* _harness;
            String _room;
        };

        HubSink MakeSink() {
            HubSink sink;
            sink.send = [this](ConnectionId id, String text) { OnSent(id, std::move(text)); };
            sink.close = [this](ConnectionId id, String reason) {
                TestClient& client = Client(id);
                client.closed = true;
                client.closeReason = std::move(reason);
            };
            return sink;
        }

        void OnSent(ConnectionId id, String text) {
            TestClient& client = Client(id);
            ReceivedMessage message;
            message.text = text;
            auto parsed = ParseJson(text);
            EXPECT_TRUE(parsed.has_value()) << "the hub sent text that is not JSON: " << text.c_str();
            if (parsed) {
                message.json = std::move(*parsed);
                message.type = GetJsonString(message.json, "type").value_or(String());
            }
            if (message.type == RoomMessageTypes::kJoined) {
                client.roomCode = message.GetString("room");
                client.seat = static_cast<int>(message.GetInt("seat"));
                client.token = message.GetString("token");
            } else if (message.type == RoomMessageTypes::kView) {
                client.seat = static_cast<int>(message.GetInt("seat"));
            }
            // The game of the client's room as it is right now
            if (const FakeSeatedGame* game = GetGame(client.roomCode); game && client.seat >= 0) {
                Vector<String> leaked = FindLeakedCards(text, client.seat, *game);
                if (message.type == RoomMessageTypes::kError) {
                    // A refusal that quotes the card the client itself sent ("seat 0 does not hold 'c3_1'") gives away nothing
                    leaked.erase(eastl::remove_if(leaked.begin(), leaked.end(), [&](const String& card) { return client.lastSent.find(card) != String::npos; }), leaked.end());
                }
                if (!leaked.empty()) {
                    ++_leaks;
                    if (_failOnLeak) {
                        ADD_FAILURE() << "seat " << client.seat << " was sent a hidden card (" << leaked[0].c_str() << "): " << text.c_str();
                    }
                }
            }
            if (message.type == RoomMessageTypes::kLeft) {
                client.roomCode.clear();
                client.seat = -1;
            }
            client.received.push_back(std::move(message));
        }

        // Declaration order matters: the hub (and with it the games) is destroyed before the maps its games unregister from
        HashMap<String, FakeSeatedGame*> _games;
        // A node-based map: a test may hold a TestClient& while other clients connect
        Map<ConnectionId, TestClient> _clients;
        SeatedGameRegistry _registry;
        RoomHub _hub;
        RoomTime _now{std::chrono::seconds(1000)};
        ConnectionId _nextConnection = 1;
        String _startingRoom;
        size_t _leaks = 0;
        bool _failOnLeak = true;
    };

}  // namespace BECore::Tests
