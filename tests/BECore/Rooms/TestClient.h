#pragma once

#include <BECore/Control/ControlParams.h>
#include <BECore/Format/Format.h>
#include <BECore/Rooms/FakeSeatedGame.h>
#include <BECore/Rooms/RoomTestTimings.h>
#include <BECore/Rooms/TestRandom.h>
#include <BECore/Rooms/WireJudge.h>
#include <BECore/Types/Types.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <Common/SharedPtr.h>
#include <Rooms/RoomErrors.h>
#include <Rooms/RoomHub.h>
#include <Rooms/RoomProtocol.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <utility>

namespace BECore::Tests {

    /// A text a client received and the tape that reads it; the copies of a message share the one document
    struct ReceivedMessage {
        String text;
        SharedPtr<const JsonDoc> doc;
        String type;

        /// The parsed text; not valid when the text was not JSON (or the message is a stand-in of `Require`)
        [[nodiscard]] JsonView Root() const {
            return doc ? doc->Root() : JsonView();
        }

        [[nodiscard]] Optional<JsonView> Find(StringView key) const {
            return FindParam(Root(), key);
        }

        [[nodiscard]] String GetString(StringView key) const {
            return GetParamString(Root(), key).value_or(String());
        }

        [[nodiscard]] int64_t GetInt(StringView key, int64_t fallback = -1000) const {
            return GetParamInt(Root(), key).value_or(fallback);
        }

        [[nodiscard]] bool GetBool(StringView key, bool fallback = false) const {
            return GetParamBool(Root(), key).value_or(fallback);
        }
    };

    /// What a client holds of one text the server sent: the text, its document and its `type`. A text that is not JSON fails the test
    [[nodiscard]] inline ReceivedMessage ReadReceivedMessage(String text) {
        ReceivedMessage message;
        message.doc = MakeShared<const JsonDoc>(StringView(text.data(), text.size()));
        EXPECT_TRUE(message.doc->IsParsed()) << "the server sent text that is not JSON: " << text.c_str();
        message.type = GetParamString(message.Root(), "type").value_or(String());
        message.text = std::move(text);
        return message;
    }

    // What the hub harness's TestClient and the socket tests' RoomSocketClient both read from the messages they received

    [[nodiscard]] inline size_t CountOfType(const Vector<ReceivedMessage>& received, StringView type) {
        return static_cast<size_t>(std::count_if(received.begin(), received.end(), [type](const ReceivedMessage& message) { return message.type == type; }));
    }

    /// The last message of @p type, or null
    [[nodiscard]] inline const ReceivedMessage* LastOfType(const Vector<ReceivedMessage>& received, StringView type) {
        for (size_t i = received.size(); i > 0; --i) {
            if (received[i - 1].type == type) {
                return &received[i - 1];
            }
        }
        return nullptr;
    }

    /// The types, in order, as one text ("joined,lobby,view")
    [[nodiscard]] inline String TypesOf(const Vector<ReceivedMessage>& received) {
        String result;
        for (const ReceivedMessage& message : received) {
            if (!result.empty()) {
                result += ",";
            }
            result += message.type;
        }
        return result;
    }

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
            return CountOfType(received, type);
        }

        /// The last message of the type, or null
        [[nodiscard]] const ReceivedMessage* Last(StringView type) const {
            return LastOfType(received, type);
        }

        /// The `code` of the last `error` message ("" when none)
        [[nodiscard]] String LastErrorCode() const {
            const ReceivedMessage* error = Last(RoomMessageTypes::kError);
            return error ? error->GetString("code") : String();
        }

        /// The types received, in order, as one text ("joined,lobby,view")
        [[nodiscard]] String Types() const {
            return TypesOf(received);
        }

        void Clear() {
            received.clear();
        }
    };

    /**
     * @brief A RoomHub over the fake game (or a game's own) with fake clients, an injected clock and a leak check on the sink side.
     *
     * Every text the hub sends a client is judged, inside the send callback and against the live game of that client's room, by
     * FindLeakedCards (hidden hands are what the game holds *now*, so a check made after the fact would find nothing). A leak is a
     * test failure unless failOnLeak is off, then it is only counted (the negative control with the leaky game). The check knows the
     * fake games only: a game's own are judged by its observer.
     *
     * For a game's tests: include `<BECore/Rooms/TestClient.h>` (namespace `BECore::Tests`) and link the test target with
     * `RoomsModule RoomClientModule RoomServerModule` (the fake game is RoomServer's demo game, so desktop only). What the registrar's
     * factories and the observer capture must outlive the harness: declare it before the harness.
     */
    class HubHarness {
    public:
        /// Registers the games of a harness instead of the fake games; a factory makes its game with MakeTracked() of the harness
        using GameRegistrar = MoveOnlyFunction<void(HubHarness&, SeatedGameRegistry&)>;
        /// Sees every message the hub sends a client, inside the send, so the game is still as it was when the hub sent it. The
        /// client already has the room, seat and token the message names, still has them on `left`, and does not have the message in
        /// `received` yet. It must not call back into the hub
        using MessageObserver = MoveOnlyFunction<void(HubHarness&, const TestClient&, const ReceivedMessage&)>;

        /// @param leaky The fake games send every hand; no effect with a registrar
        /// @param registrar Called once, by this constructor
        explicit HubHarness(RoomHubSettings settings = MakeSettings(), bool leaky = false, RandomFn random = MakeSequence(42), GameRegistrar registrar = {}, MessageObserver observer = {})
            : _observer(std::move(observer)), _ownGames(static_cast<bool>(registrar)), _hub(_registry, settings, std::move(random), MakeSink()) {
            if (registrar) {
                registrar(*this, _registry);
            } else {
                RegisterFakeGames(leaky);
            }
        }

        static RoomHubSettings MakeSettings() {
            RoomHubSettings settings;
            settings.timings = NoComputerDelayTimings();
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

        void Create(ConnectionId id, StringView name, StringView game = "fake", StringView options = "{}", bool isPublic = false) {
            Send(id, CreateRoomMessage{String(game), String(name), String(options), isPublic});
        }

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

        /// Creates a room of @p humans humans, the host and the guests who joined it by its code; returns the connections (host first). The room is in the lobby.
        Vector<ConnectionId> MakeLobby(int humans, StringView game = "fake", StringView options = "{}") {
            Vector<ConnectionId> ids;
            const ConnectionId host = Connect();
            Create(host, "P0", game, options);
            ids.push_back(host);
            const String code = Client(host).roomCode;
            for (int i = 1; i < humans; ++i) {
                const ConnectionId guest = Connect();
                JoinByName(guest, code, Format("P{}", i).c_str());
                ids.push_back(guest);
            }
            return ids;
        }

        /// A game for the factory of a registrar: GetSeatedGame() finds it under the room of the client whose message the hub is
        /// handling, until it is destroyed. GameType must not be `final`
        template <typename GameType, typename... Args>
        [[nodiscard]] UniquePtr<ISeatedGame> MakeTracked(Args&&... args) {
            return MakeUnique<Tracked<GameType>>(this, _startingRoom, std::forward<Args>(args)...);
        }

        /// The live tracked game of a room, or null
        [[nodiscard]] ISeatedGame* GetSeatedGame(const String& room) const {
            const auto found = _games.find(room);
            return found == _games.end() ? nullptr : found->second;
        }

        /// The live fake game of a room, or null; always null with a registrar, whose types the harness does not know
        [[nodiscard]] FakeSeatedGame* GetGame(const String& room) const {
            return _ownGames ? nullptr : static_cast<FakeSeatedGame*>(GetSeatedGame(room));
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
            const Optional<JsonView> actions = view->Find("actions");
            if (!actions || !actions->IsArray() || actions->Size() == 0) {
                return false;
            }
            ActionMessage action;
            action.version = static_cast<uint64_t>(view->GetInt("version", 0));
            action.action = String((*actions->Items().begin()).Raw());
            Send(id, action);
            return true;
        }

    private:
        template <typename GameType>
        class Tracked : public GameType {
        public:
            template <typename... Args>
            Tracked(HubHarness* harness, String room, Args&&... args) : GameType(std::forward<Args>(args)...), _harness(harness), _room(std::move(room)) {
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

        void RegisterFakeGames(bool leaky) {
            const auto factory = [this, leaky]() -> UniquePtr<ISeatedGame> {
                if (leaky) {
                    return MakeTracked<LeakyFakeSeatedGame>();
                }
                return MakeTracked<FakeSeatedGame>();
            };
            EXPECT_TRUE(_registry.Register(SeatedGameInfo{"fake", FakeSeatedGame::kSeatCount}, factory));
            // The same game with public options: the lobby shows `open` and `count` of `create_room`
            EXPECT_TRUE(_registry.Register(SeatedGameInfo{"fake-public", FakeSeatedGame::kSeatCount, {"open", "count"}}, factory));
            // The same game that also lists its started public rooms that have a free seat
            EXPECT_TRUE(_registry.Register(SeatedGameInfo{"fake-started", FakeSeatedGame::kSeatCount, {}, true}, factory));
        }

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
            ReceivedMessage message = ReadReceivedMessage(text);
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
            if (_observer) {
                _observer(*this, client, message);
            }
            if (message.type == RoomMessageTypes::kLeft) {
                client.roomCode.clear();
                client.seat = -1;
            }
            client.received.push_back(std::move(message));
        }

        // Declaration order matters: the hub (and with it the games) is destroyed before the maps its games unregister from
        HashMap<String, ISeatedGame*> _games;
        // A node-based map: a test may hold a TestClient& while other clients connect
        Map<ConnectionId, TestClient> _clients;
        SeatedGameRegistry _registry;
        MessageObserver _observer;
        /// A registrar replaced the fake games
        const bool _ownGames;
        RoomHub _hub;
        RoomTime _now{std::chrono::seconds(1000)};
        ConnectionId _nextConnection = 1;
        String _startingRoom;
        size_t _leaks = 0;
        bool _failOnLeak = true;
    };

    /// How many ticks the play helpers below give a hand before they give up: far more than a hand of the fake game takes
    inline constexpr int kHubPlaySteps = 400;

    /// The number of texts the client received that contain @p needle
    [[nodiscard]] inline size_t CountTextsContaining(const TestClient& client, const String& needle) {
        size_t count = 0;
        for (const ReceivedMessage& message : client.received) {
            count += message.text.find(needle) != String::npos ? 1 : 0;
        }
        return count;
    }

    /// What the wire judge says of every text in @p received, sent to @p seat: the ids of the cards it must not have seen
    [[nodiscard]] inline Vector<String> LeaksOf(const Vector<ReceivedMessage>& received, int seat) {
        Vector<String> leaks;
        for (const ReceivedMessage& message : received) {
            for (String& id : FindLeakedCardIds(StringView(message.text.c_str(), message.text.size()), seat)) {
                leaks.push_back(std::move(id));
            }
        }
        return leaks;
    }

    [[nodiscard]] inline Vector<String> LeaksOf(const TestClient& client) {
        return LeaksOf(client.received, client.seat);
    }

    /// The last view the client got says the hand is over
    [[nodiscard]] inline bool IsFinishedFor(HubHarness& harness, ConnectionId id) {
        const ReceivedMessage* view = harness.Client(id).Last(RoomMessageTypes::kView);
        return view && view->GetBool("finished");
    }

    /// The first of @p humans whose last view offers an action (the one whose turn it is), or 0
    [[nodiscard]] inline ConnectionId FindHumanOnTurn(HubHarness& harness, const Vector<ConnectionId>& humans) {
        for (const ConnectionId id : humans) {
            const ReceivedMessage* view = harness.Client(id).Last(RoomMessageTypes::kView);
            const Optional<JsonView> actions = view ? view->Find("actions") : std::nullopt;
            if (actions && actions->IsArray() && actions->Size() > 0) {
                return id;
            }
        }
        return 0;
    }

    /// Ticks the clock and lets the humans play their first action until every one has seen the end
    inline bool PlayToTheEnd(HubHarness& harness, const Vector<ConnectionId>& humans) {
        for (int step = 0; step < kHubPlaySteps; ++step) {
            harness.Tick(std::chrono::milliseconds(10));
            bool allDone = true;
            for (const ConnectionId id : humans) {
                harness.PlayFirstAction(id);
                allDone = allDone && IsFinishedFor(harness, id);
            }
            if (allDone) {
                return true;
            }
        }
        return false;
    }

    /// Ticks until one of the humans has an action; the connection, or 0
    inline ConnectionId WaitForAHumanTurn(HubHarness& harness, const Vector<ConnectionId>& humans) {
        for (int step = 0; step < kHubPlaySteps; ++step) {
            if (const ConnectionId id = FindHumanOnTurn(harness, humans)) {
                return id;
            }
            harness.Tick(std::chrono::milliseconds(10));
        }
        return 0;
    }

}  // namespace BECore::Tests
