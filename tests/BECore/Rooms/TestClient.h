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
        eastl::string text;
        Json json;
        eastl::string type;

        [[nodiscard]] eastl::string GetString(eastl::string_view key) const {
            return GetJsonString(json, key).value_or(eastl::string());
        }

        [[nodiscard]] int64_t GetInt(eastl::string_view key, int64_t fallback = -1000) const {
            return GetJsonInt(json, key).value_or(fallback);
        }
    };

    /// A fake client connection: records everything the hub sent it and whether the hub closed it
    struct TestClient {
        ConnectionId id = 0;
        eastl::vector<ReceivedMessage> received;
        bool closed = false;
        eastl::string closeReason;
        /// Learned from the messages: the room it is in and its seat ("" and -1 when none)
        eastl::string roomCode;
        int seat = -1;
        eastl::string token;

        [[nodiscard]] size_t Count(eastl::string_view type) const {
            size_t count = 0;
            for (const ReceivedMessage& message : received) {
                count += message.type == type ? 1 : 0;
            }
            return count;
        }

        /// The last message of the type, or null
        [[nodiscard]] const ReceivedMessage* Last(eastl::string_view type) const {
            for (size_t i = received.size(); i > 0; --i) {
                if (received[i - 1].type == type) {
                    return &received[i - 1];
                }
            }
            return nullptr;
        }

        /// The `code` of the last `error` message ("" when none)
        [[nodiscard]] eastl::string LastErrorCode() const {
            const ReceivedMessage* error = Last(RoomMessageTypes::kError);
            return error ? error->GetString("code") : eastl::string();
        }

        /// The types received, in order, as one text ("joined,lobby,view")
        [[nodiscard]] eastl::string Types() const {
            eastl::string result;
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
            const auto factory = [this, leaky]() -> eastl::unique_ptr<ISeatedGame> {
                const eastl::string room = _startingRoom;
                if (leaky) {
                    return eastl::make_unique<Tracked<LeakyFakeSeatedGame>>(this, room);
                }
                return eastl::make_unique<Tracked<FakeSeatedGame>>(this, room);
            };
            EXPECT_TRUE(_registry.Register(SeatedGameInfo{"fake", FakeSeatedGame::kSeatCount}, factory));
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

        ConnectionId Connect() {
            const ConnectionId id = _nextConnection++;
            TestClient client;
            client.id = id;
            _clients.emplace(id, std::move(client));
            _hub.OnConnected(id, _now);
            return id;
        }

        void Disconnect(ConnectionId id) {
            _hub.OnDisconnected(id, _now);
        }

        TestClient& Client(ConnectionId id) {
            return _clients[id];
        }

        /// Sends raw text as if it came from the connection
        void SendText(ConnectionId id, eastl::string_view text) {
            _startingRoom = Client(id).roomCode;
            _hub.OnMessage(id, text, _now);
        }

        void Send(ConnectionId id, const ClientMessage& message) {
            SendText(id, SerializeClientMessage(message));
        }

        void Create(ConnectionId id, eastl::string_view name, eastl::string_view game = "fake") {
            Send(id, CreateRoomMessage{eastl::string(game), eastl::string(name), Json::object()});
        }

        void JoinByName(ConnectionId id, eastl::string_view code, eastl::string_view name) {
            JoinRoomMessage message;
            message.code = eastl::string(code);
            message.name = eastl::string(name);
            Send(id, message);
        }

        void JoinByToken(ConnectionId id, eastl::string_view code, eastl::string_view token) {
            JoinRoomMessage message;
            message.code = eastl::string(code);
            message.token = eastl::string(token);
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
        eastl::vector<ConnectionId> MakeLobby(int humans) {
            eastl::vector<ConnectionId> ids;
            const ConnectionId host = Connect();
            Create(host, "P0");
            ids.push_back(host);
            const eastl::string code = Client(host).roomCode;
            for (int i = 1; i < humans; ++i) {
                const ConnectionId guest = Connect();
                JoinByName(guest, code, Format("P{}", i).c_str());
                ids.push_back(guest);
            }
            return ids;
        }

        /// The live game of a room, or null
        [[nodiscard]] FakeSeatedGame* GetGame(const eastl::string& room) const {
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
            Tracked(HubHarness* harness, eastl::string room) : _harness(harness), _room(std::move(room)) {
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
            eastl::string _room;
        };

        HubSink MakeSink() {
            HubSink sink;
            sink.send = [this](ConnectionId id, eastl::string text) { OnSent(id, std::move(text)); };
            sink.close = [this](ConnectionId id, eastl::string reason) {
                TestClient& client = Client(id);
                client.closed = true;
                client.closeReason = std::move(reason);
            };
            return sink;
        }

        void OnSent(ConnectionId id, eastl::string text) {
            TestClient& client = Client(id);
            ReceivedMessage message;
            message.text = text;
            auto parsed = ParseJson(text);
            EXPECT_TRUE(parsed.has_value()) << "the hub sent text that is not JSON: " << text.c_str();
            if (parsed) {
                message.json = std::move(*parsed);
                message.type = GetJsonString(message.json, "type").value_or(eastl::string());
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
                const eastl::vector<eastl::string> leaked = FindLeakedCards(text, client.seat, *game);
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
        eastl::hash_map<eastl::string, FakeSeatedGame*> _games;
        // A node-based map: a test may hold a TestClient& while other clients connect
        eastl::map<ConnectionId, TestClient> _clients;
        SeatedGameRegistry _registry;
        RoomHub _hub;
        RoomTime _now{std::chrono::seconds(1000)};
        ConnectionId _nextConnection = 1;
        eastl::string _startingRoom;
        size_t _leaks = 0;
        bool _failOnLeak = true;
    };

}  // namespace BECore::Tests
