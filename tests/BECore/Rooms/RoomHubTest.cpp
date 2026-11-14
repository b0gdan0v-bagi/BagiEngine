#include "TestClient.h"

namespace BECore::Tests {

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::seconds;

        /// The first human whose last view offers an action (the one whose turn it is), or 0
        ConnectionId FindPlayerOnTurn(HubHarness& harness, const Vector<ConnectionId>& ids) {
            for (const ConnectionId id : ids) {
                const ReceivedMessage* view = harness.Client(id).Last(RoomMessageTypes::kView);
                const Json* actions = view ? FindJsonMember(view->json, "actions") : nullptr;
                if (actions && actions->is_array() && !actions->empty()) {
                    return id;
                }
            }
            return 0;
        }

        size_t CountTextsContaining(const TestClient& client, const String& needle) {
            size_t count = 0;
            for (const ReceivedMessage& message : client.received) {
                count += message.text.find(needle) != String::npos ? 1 : 0;
            }
            return count;
        }

        Json CardAction(const String& card) {
            Json action = Json::object();
            action["type"] = "play";
            action["card"] = card.c_str();
            return action;
        }

    }  // namespace

    TEST(RoomHubTest, CreateThenJoin) {
        HubHarness harness;
        const ConnectionId host = harness.Connect();
        harness.Create(host, "Ann");
        TestClient& ann = harness.Client(host);
        EXPECT_EQ(ann.Types(), "joined,lobby");
        const ReceivedMessage* joined = ann.Last(RoomMessageTypes::kJoined);
        ASSERT_NE(joined, nullptr);
        EXPECT_EQ(joined->GetInt("seat"), 0);
        EXPECT_TRUE(GetJsonBool(joined->json, "host").value_or(false));
        EXPECT_EQ(joined->GetString("room").size(), RoomLimits::kRoomCodeLength);
        EXPECT_EQ(ann.token.size(), RoomLimits::kTokenLength);
        EXPECT_TRUE(harness.Hub().HasRoom(ann.roomCode));
        EXPECT_EQ(harness.Hub().GetRoomCount(), 1u);

        const ConnectionId guest = harness.Connect();
        // The code is accepted in lower case
        String lower = ann.roomCode;
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        harness.JoinByName(guest, lower, "Bob");
        TestClient& bob = harness.Client(guest);
        EXPECT_EQ(bob.Types(), "joined,lobby");
        EXPECT_EQ(bob.Last(RoomMessageTypes::kJoined)->GetInt("seat"), 1);
        EXPECT_FALSE(GetJsonBool(bob.Last(RoomMessageTypes::kJoined)->json, "host").value_or(true));
        // The host was told about the new member
        EXPECT_EQ(ann.Types(), "joined,lobby,lobby");
        const Json* seats = FindJsonMember(ann.Last(RoomMessageTypes::kLobby)->json, "seats");
        ASSERT_NE(seats, nullptr);
        ASSERT_EQ(seats->size(), 4u);
        EXPECT_EQ(GetJsonString((*seats)[1], "kind").value_or(String()), "human");
        EXPECT_EQ(GetJsonString((*seats)[1], "name").value_or(String()), "Bob");
        EXPECT_EQ(GetJsonString((*seats)[2], "kind").value_or(String()), "empty");
    }

    TEST(RoomHubTest, TheLobbyShowsEveryMemberThePublicOptionsOfTheRoomAndNothingElse) {
        HubHarness harness;
        const ConnectionId host = harness.Connect();
        Json options = Json::object();
        options["open"] = true;
        options["count"] = 3;
        options["secret"] = 7;      // not public
        options["label"] = "text";  // not public
        harness.Create(host, "Ann", "fake-public", options);
        const ConnectionId guest = harness.Connect();
        harness.JoinByName(guest, harness.Client(host).roomCode, "Bob");

        Json expected = Json::object();
        expected["open"] = true;
        expected["count"] = 3;
        for (const ConnectionId id : {host, guest}) {
            const ReceivedMessage* lobby = harness.Client(id).Last(RoomMessageTypes::kLobby);
            ASSERT_NE(lobby, nullptr);
            const Json* shown = FindJsonMember(lobby->json, "options");
            ASSERT_NE(shown, nullptr) << "connection " << id;
            EXPECT_EQ(*shown, expected) << "connection " << id;
        }

        // A game that names no public option sends none
        HubHarness plain;
        const ConnectionId alone = plain.Connect();
        plain.Create(alone, "Ann", "fake", options);
        const ReceivedMessage* lobby = plain.Client(alone).Last(RoomMessageTypes::kLobby);
        ASSERT_NE(lobby, nullptr);
        EXPECT_EQ(FindJsonMember(lobby->json, "options"), nullptr);
    }

    TEST(RoomHubTest, LobbyIsSentOnlyWhenSomethingChanged) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        harness.Client(ids[0]).Clear();
        harness.Client(ids[1]).Clear();

        harness.Ping(ids[0]);
        harness.Send(ids[1], ChooseSeatMessage{1});  // the seat it has
        EXPECT_EQ(harness.Client(ids[0]).Types(), "pong");
        EXPECT_EQ(harness.Client(ids[1]).Types(), "");

        harness.Send(ids[1], ChooseSeatMessage{3});
        EXPECT_EQ(harness.Client(ids[0]).Types(), "pong,lobby");
        EXPECT_EQ(harness.Client(ids[1]).Types(), "lobby");
        harness.Tick(seconds(1));
        EXPECT_EQ(harness.Client(ids[0]).Types(), "pong,lobby");
    }

    TEST(RoomHubTest, TheTokenGoesToItsOwnerOnly) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const TestClient& ann = harness.Client(ids[0]);
        const TestClient& bob = harness.Client(ids[1]);
        ASSERT_FALSE(ann.token.empty());
        ASSERT_FALSE(bob.token.empty());
        EXPECT_NE(ann.token, bob.token);
        EXPECT_EQ(CountTextsContaining(ann, ann.token), 1u);
        EXPECT_EQ(CountTextsContaining(bob, bob.token), 1u);
        EXPECT_EQ(CountTextsContaining(ann, bob.token), 0u);
        EXPECT_EQ(CountTextsContaining(bob, ann.token), 0u);
        harness.Start(ids[0]);
        harness.Tick(seconds(1));
        EXPECT_EQ(CountTextsContaining(ann, bob.token), 0u);
        EXPECT_EQ(CountTextsContaining(bob, ann.token), 0u);
    }

    TEST(RoomHubTest, ErrorsOfEachKind) {
        HubHarness harness;
        const ConnectionId stranger = harness.Connect();
        // Not in a room
        harness.Start(stranger);
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kNotInRoom);
        EXPECT_EQ(harness.Client(stranger).Last(RoomMessageTypes::kError)->GetString("ref"), "start");
        harness.Send(stranger, ChooseSeatMessage{1});
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kNotInRoom);
        harness.Leave(stranger);
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kNotInRoom);
        harness.Send(stranger, ActionMessage{0, Json::object()});
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kNotInRoom);
        harness.Create(stranger, "Zed", "no_such_game");
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kUnknownGame);
        harness.JoinByName(stranger, "ZZZZZ", "Zed");
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kUnknownRoom);
        harness.SendText(stranger, "{\"v\":1,\"type\":\"nonsense\"}");
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kBadMessage);
        harness.SendText(stranger, "{\"v\":2,\"type\":\"ping\"}");
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kUnsupportedVersion);
        harness.Create(stranger, "   ");
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kBadName);

        // A full lobby
        const auto ids = harness.MakeLobby(4);
        const String code = harness.Client(ids[0]).roomCode;
        harness.JoinByName(stranger, code, "Late");
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kRoomFull);
        harness.Send(ids[1], ChooseSeatMessage{0});
        EXPECT_EQ(harness.Client(ids[1]).LastErrorCode(), RoomErrors::kSeatTaken);
        harness.Send(ids[1], ChooseSeatMessage{9});
        EXPECT_EQ(harness.Client(ids[1]).LastErrorCode(), RoomErrors::kInvalidSeat);
        harness.Send(ids[1], ChooseSeatMessage{-1});
        EXPECT_EQ(harness.Client(ids[1]).LastErrorCode(), RoomErrors::kInvalidSeat);

        // Wrong role, wrong time
        harness.Start(ids[1]);
        EXPECT_EQ(harness.Client(ids[1]).LastErrorCode(), RoomErrors::kNotHost);
        harness.Send(ids[1], ActionMessage{0, CardAction("c1_0")});
        EXPECT_EQ(harness.Client(ids[1]).LastErrorCode(), RoomErrors::kNotStarted);
        harness.Start(ids[0]);
        EXPECT_EQ(harness.Client(ids[0]).LastErrorCode(), "");
        harness.Start(ids[0]);
        EXPECT_EQ(harness.Client(ids[0]).LastErrorCode(), RoomErrors::kGameStarted);
        harness.Send(ids[1], ChooseSeatMessage{2});
        EXPECT_EQ(harness.Client(ids[1]).LastErrorCode(), RoomErrors::kGameStarted);
        harness.JoinByName(stranger, code, "Late");
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kGameStarted);

        // Actions
        const ConnectionId onTurn = FindPlayerOnTurn(harness, ids);
        ASSERT_NE(onTurn, 0u);
        ConnectionId other = 0;
        for (const ConnectionId id : ids) {
            other = id != onTurn ? id : other;
        }
        const ReceivedMessage* view = harness.Client(onTurn).Last(RoomMessageTypes::kView);
        const uint64_t version = static_cast<uint64_t>(view->GetInt("version"));
        const int turnSeat = harness.Client(onTurn).seat;
        const int otherSeat = harness.Client(other).seat;
        // A card nobody holds: the game quotes what the client sent in its error, so a hidden card would be echoed back
        harness.Send(onTurn, ActionMessage{version, CardAction("c9_9")});
        EXPECT_EQ(harness.Client(onTurn).LastErrorCode(), RoomErrors::kIllegalAction);
        harness.Send(other, ActionMessage{version, CardAction(Format("c{}_0", otherSeat))});
        EXPECT_EQ(harness.Client(other).LastErrorCode(), RoomErrors::kNotYourTurn);
        const size_t viewsBefore = harness.Client(onTurn).Count(RoomMessageTypes::kView);
        harness.Send(onTurn, ActionMessage{version + 7, CardAction(Format("c{}_0", turnSeat))});
        EXPECT_EQ(harness.Client(onTurn).LastErrorCode(), RoomErrors::kStaleAction);
        // The current view comes again after a stale action
        EXPECT_EQ(harness.Client(onTurn).Count(RoomMessageTypes::kView), viewsBefore + 1);
        EXPECT_EQ(harness.Client(onTurn).Last(RoomMessageTypes::kView)->GetInt("version"), static_cast<int64_t>(version));
    }

    TEST(RoomHubTest, RateLimitAnswersThenClosesTheConnection) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.rateBurst = 2;
        settings.ratePerSecond = 0.0;
        settings.maxRateLimitedInRow = 3;
        HubHarness harness(settings);
        const ConnectionId id = harness.Connect();
        for (int i = 0; i < 4; ++i) {
            harness.Ping(id);
        }
        const TestClient& client = harness.Client(id);
        EXPECT_EQ(client.Count(RoomMessageTypes::kPong), 2u);
        EXPECT_EQ(client.Count(RoomMessageTypes::kError), 2u);
        EXPECT_EQ(client.LastErrorCode(), RoomErrors::kRateLimited);
        EXPECT_FALSE(client.closed);
        harness.Ping(id);
        EXPECT_TRUE(client.closed);
        EXPECT_EQ(client.closeReason, "rate_limited");
        EXPECT_EQ(harness.Hub().GetConnectionCount(), 0u);
        // The transport reports the disconnect later: ignored
        harness.Disconnect(id);
        harness.Ping(id);
        EXPECT_EQ(client.Count(RoomMessageTypes::kError), 3u);
    }

    TEST(RoomHubTest, ASlowSenderIsNotClosed) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.rateBurst = 2;
        settings.ratePerSecond = 1.0;
        settings.maxRateLimitedInRow = 2;
        HubHarness harness(settings);
        const ConnectionId id = harness.Connect();
        for (int round = 0; round < 5; ++round) {
            harness.Ping(id);
            harness.Ping(id);
            harness.Ping(id);  // limited, but the row is broken by the next accepted one
            harness.Advance(seconds(2));
        }
        EXPECT_FALSE(harness.Client(id).closed);
        EXPECT_EQ(harness.Client(id).Count(RoomMessageTypes::kPong), 10u);
    }

    TEST(RoomHubTest, RepeatedBadCodesCloseTheConnection) {
        HubHarness harness;
        const ConnectionId guesser = harness.Connect();
        for (int i = 0; i < 4; ++i) {
            harness.JoinByName(guesser, "ZZZZZ", "Mal");
        }
        EXPECT_FALSE(harness.Client(guesser).closed);
        harness.JoinByName(guesser, "ZZZZZ", "Mal");
        EXPECT_TRUE(harness.Client(guesser).closed);
        EXPECT_EQ(harness.Client(guesser).closeReason, "too_many_failed_joins");
        EXPECT_EQ(harness.Client(guesser).Count(RoomMessageTypes::kError), 5u);

        // Codes that cannot exist and bad tokens count too
        const ConnectionId second = harness.Connect();
        harness.SendText(second, "{\"v\":1,\"type\":\"join_room\",\"code\":\"I0I0I\",\"name\":\"Mal\"}");
        harness.SendText(second, "{\"v\":1,\"type\":\"join_room\",\"code\":\"ZZZZZ\",\"token\":\"nope\"}");
        EXPECT_EQ(harness.Client(second).LastErrorCode(), RoomErrors::kBadToken);
        harness.JoinByToken(second, "ZZZZZ", String(RoomLimits::kTokenLength, 'a'));
        harness.JoinByName(second, "ZZZZZ", "Mal");
        EXPECT_FALSE(harness.Client(second).closed);
        harness.JoinByName(second, "ZZZZZ", "Mal");
        EXPECT_TRUE(harness.Client(second).closed);
    }

    TEST(RoomHubTest, AGoodJoinBreaksTheRowOfGuesses) {
        HubHarness harness;
        const ConnectionId host = harness.Connect();
        harness.Create(host, "Ann");
        const String code = harness.Client(host).roomCode;
        const ConnectionId guest = harness.Connect();
        for (int i = 0; i < 4; ++i) {
            harness.JoinByName(guest, "ZZZZZ", "Bob");
        }
        harness.JoinByName(guest, code, "Bob");
        EXPECT_EQ(harness.Client(guest).Count(RoomMessageTypes::kJoined), 1u);
        harness.Leave(guest);
        for (int i = 0; i < 4; ++i) {
            harness.JoinByName(guest, "ZZZZZ", "Bob");
        }
        EXPECT_FALSE(harness.Client(guest).closed);
    }

    TEST(RoomHubTest, OversizeAndGarbageAreErrorsNotCrashes) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.rateBurst = 1000;
        HubHarness harness(settings);
        const ConnectionId id = harness.Connect();
        const TestClient& client = harness.Client(id);

        harness.SendText(id, String(RoomLimits::kMaxMessageBytes + 1, 'x'));
        EXPECT_EQ(client.LastErrorCode(), RoomErrors::kMessageTooLarge);
        harness.SendText(id, "");
        EXPECT_EQ(client.LastErrorCode(), RoomErrors::kBadMessage);
        harness.SendText(id, "\xff\xfe\x01{[");
        EXPECT_EQ(client.LastErrorCode(), RoomErrors::kBadMessage);
        const char withNul[] = "{\"v\":1,\"type\":\"ping\"}\0{\"x\":1}";
        harness.SendText(id, StringView(withNul, sizeof(withNul) - 1));
        EXPECT_EQ(client.LastErrorCode(), RoomErrors::kBadMessage);
        harness.SendText(id, String(5000, '['));
        EXPECT_EQ(client.LastErrorCode(), RoomErrors::kBadMessage);
        EXPECT_EQ(client.Count(RoomMessageTypes::kPong), 0u);
        EXPECT_FALSE(client.closed);
        harness.Ping(id);
        EXPECT_EQ(client.Count(RoomMessageTypes::kPong), 1u);
    }

    TEST(RoomHubTest, ASecondRoomOnTheSameConnectionIsRefused) {
        HubHarness harness;
        const ConnectionId id = harness.Connect();
        harness.Create(id, "Ann");
        const String code = harness.Client(id).roomCode;
        harness.Create(id, "Ann");
        EXPECT_EQ(harness.Client(id).LastErrorCode(), RoomErrors::kAlreadyInRoom);
        harness.JoinByName(id, code, "Ann");
        EXPECT_EQ(harness.Client(id).LastErrorCode(), RoomErrors::kAlreadyInRoom);
        EXPECT_EQ(harness.Hub().GetRoomCount(), 1u);
        EXPECT_EQ(harness.Client(id).roomCode, code);
    }

    TEST(RoomHubTest, AnIdleConnectionIsClosedButOneInARoomIsNot) {
        HubHarness harness;
        const ConnectionId idle = harness.Connect();
        const ConnectionId pinger = harness.Connect();
        const ConnectionId member = harness.Connect();
        harness.Create(member, "Ann");
        harness.Tick(seconds(20));
        harness.Ping(pinger);
        harness.Tick(seconds(9));
        EXPECT_FALSE(harness.Client(idle).closed);
        harness.Tick(seconds(1));
        EXPECT_TRUE(harness.Client(idle).closed);
        EXPECT_EQ(harness.Client(idle).closeReason, "idle");
        // A ping does not keep a connection that is in no room
        EXPECT_TRUE(harness.Client(pinger).closed);
        EXPECT_FALSE(harness.Client(member).closed);
        EXPECT_EQ(harness.Hub().GetConnectionCount(), 1u);
        harness.Tick(std::chrono::hours(1));
        EXPECT_FALSE(harness.Client(member).closed);
    }

    TEST(RoomHubTest, TheLimitOfRoomsAndConnections) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.maxRooms = 2;
        settings.maxConnections = 3;
        HubHarness harness(settings);
        const ConnectionId a = harness.Connect();
        const ConnectionId b = harness.Connect();
        const ConnectionId c = harness.Connect();
        const ConnectionId d = harness.Connect();
        EXPECT_TRUE(harness.Client(d).closed);
        EXPECT_EQ(harness.Client(d).closeReason, "server_full");
        EXPECT_EQ(harness.Hub().GetConnectionCount(), 3u);
        harness.Create(a, "A");
        harness.Create(b, "B");
        harness.Create(c, "C");
        EXPECT_EQ(harness.Client(c).LastErrorCode(), RoomErrors::kTooManyRooms);
        EXPECT_EQ(harness.Hub().GetRoomCount(), 2u);
        harness.Disconnect(a);
        harness.Create(c, "C");
        EXPECT_EQ(harness.Client(c).Count(RoomMessageTypes::kJoined), 1u);
        // A slot of a closed connection is free again
        const ConnectionId e = harness.Connect();
        EXPECT_FALSE(harness.Client(e).closed);
    }

    TEST(RoomHubTest, DisconnectInTheLobbyRemovesTheMemberAndHandsOverTheHost) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(3);
        harness.Client(ids[1]).Clear();
        harness.Client(ids[2]).Clear();
        harness.Disconnect(ids[0]);
        for (const ConnectionId id : {ids[1], ids[2]}) {
            const ReceivedMessage* lobby = harness.Client(id).Last(RoomMessageTypes::kLobby);
            ASSERT_NE(lobby, nullptr);
            EXPECT_EQ(lobby->GetInt("host"), 1);
            const Json* seats = FindJsonMember(lobby->json, "seats");
            ASSERT_NE(seats, nullptr);
            EXPECT_EQ(GetJsonString((*seats)[0], "kind").value_or(String()), "empty");
        }
        EXPECT_EQ(harness.Hub().GetRoomCount(), 1u);
        // The new host can start
        harness.Start(ids[1]);
        EXPECT_EQ(harness.Client(ids[1]).Count(RoomMessageTypes::kView), 1u);
    }

    TEST(RoomHubTest, TheLastHumanLeavingClosesTheRoomAndItsCodeCanBeUsedAgain) {
        // A constant random gives the same code every time: the room code retry must stop, and a free code is reused
        HubHarness harness(HubHarness::MakeSettings(), false, [] { return uint64_t{7}; });
        const ConnectionId a = harness.Connect();
        const ConnectionId b = harness.Connect();
        harness.Create(a, "Ann");
        const String code = harness.Client(a).roomCode;
        harness.Create(b, "Bob");
        EXPECT_EQ(harness.Client(b).LastErrorCode(), RoomErrors::kInternalError);
        EXPECT_EQ(harness.Hub().GetRoomCount(), 1u);

        harness.Leave(a);
        EXPECT_EQ(harness.Client(a).Last(RoomMessageTypes::kLeft)->GetString("reason"), "left");
        EXPECT_EQ(harness.Hub().GetRoomCount(), 0u);
        EXPECT_FALSE(harness.Hub().HasRoom(code));
        harness.Create(b, "Bob");
        EXPECT_EQ(harness.Client(b).roomCode, code);
        // A disconnect of the only member closes it too
        harness.Disconnect(b);
        EXPECT_EQ(harness.Hub().GetRoomCount(), 0u);
        harness.Create(a, "Ann");
        EXPECT_EQ(harness.Client(a).roomCode, code);
    }

    TEST(RoomHubTest, ALeftMemberCanCreateOrJoinAgain) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        harness.Leave(ids[1]);
        EXPECT_EQ(harness.Client(ids[1]).roomCode, "");
        harness.Create(ids[1], "Bob");
        EXPECT_EQ(harness.Hub().GetRoomCount(), 2u);
        EXPECT_EQ(harness.Client(ids[0]).Last(RoomMessageTypes::kLobby)->GetInt("host"), 0);
    }

    TEST(RoomHubTest, AConnectionWithTheTokenReplacesTheLiveOne) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const TestClient& old = harness.Client(ids[1]);
        const String code = old.roomCode;
        const String token = old.token;

        const ConnectionId fresh = harness.Connect();
        harness.JoinByToken(fresh, code, token);
        EXPECT_TRUE(old.closed);
        EXPECT_EQ(old.closeReason, "replaced");
        EXPECT_EQ(old.Last(RoomMessageTypes::kLeft)->GetString("reason"), "replaced");
        const TestClient& now = harness.Client(fresh);
        EXPECT_EQ(now.Types(), "joined,lobby");
        EXPECT_EQ(now.seat, 1);
        EXPECT_EQ(now.token, token);
        // Still two members: the seat did not move and nobody was added
        const Json* seats = FindJsonMember(now.Last(RoomMessageTypes::kLobby)->json, "seats");
        ASSERT_NE(seats, nullptr);
        EXPECT_EQ(GetJsonString((*seats)[1], "name").value_or(String()), "P1");
        EXPECT_EQ(GetJsonString((*seats)[2], "kind").value_or(String()), "empty");
        EXPECT_EQ(harness.Hub().GetConnectionCount(), 2u);

        // The old connection's disconnect later changes nothing
        harness.Disconnect(ids[1]);
        EXPECT_EQ(harness.Hub().GetRoomCount(), 1u);

        // A wrong token is refused
        const ConnectionId thief = harness.Connect();
        harness.JoinByToken(thief, code, String(RoomLimits::kTokenLength, 'a'));
        EXPECT_EQ(harness.Client(thief).LastErrorCode(), RoomErrors::kBadToken);
        EXPECT_EQ(harness.Client(thief).roomCode, "");
    }

    TEST(RoomHubTest, AGameWithComputersRunsToTheEndAndTheRoomIsClosedLater) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const String code = harness.Client(ids[0]).roomCode;
        for (const ConnectionId id : ids) {
            harness.Client(id).Clear();
        }
        harness.Start(ids[0]);
        // The order a client sees at the start: the lobby (phase playing), then its view
        for (const ConnectionId id : ids) {
            EXPECT_EQ(harness.Client(id).Types(), "lobby,view");
        }
        const Json* seats = FindJsonMember(harness.Client(ids[0]).Last(RoomMessageTypes::kLobby)->json, "seats");
        ASSERT_NE(seats, nullptr);
        EXPECT_EQ(GetJsonString((*seats)[2], "kind").value_or(String()), "computer");
        EXPECT_EQ(GetJsonString((*seats)[3], "kind").value_or(String()), "computer");

        bool finished = false;
        for (int step = 0; step < 200 && !finished; ++step) {
            harness.Tick(milliseconds(1));
            for (const ConnectionId id : ids) {
                harness.PlayFirstAction(id);
            }
            const ReceivedMessage* view = harness.Client(ids[0]).Last(RoomMessageTypes::kView);
            finished = view && GetJsonBool(view->json, "finished").value_or(false);
        }
        ASSERT_TRUE(finished);
        for (const ConnectionId id : ids) {
            const ReceivedMessage* view = harness.Client(id).Last(RoomMessageTypes::kView);
            ASSERT_NE(view, nullptr);
            EXPECT_TRUE(GetJsonBool(view->json, "finished").value_or(false));
            EXPECT_EQ(harness.Client(id).LastErrorCode(), "");
        }
        EXPECT_EQ(harness.GetLeakCount(), 0u);

        // A finished room stays while people are in it, then it is closed
        harness.Tick(std::chrono::minutes(9));
        EXPECT_TRUE(harness.Hub().HasRoom(code));
        harness.Tick(std::chrono::minutes(1));
        EXPECT_FALSE(harness.Hub().HasRoom(code));
        for (const ConnectionId id : ids) {
            EXPECT_EQ(harness.Client(id).Last(RoomMessageTypes::kLeft)->GetString("reason"), "room_finished");
            EXPECT_EQ(harness.Client(id).roomCode, "");
            EXPECT_FALSE(harness.Client(id).closed);
        }
    }

    TEST(RoomHubTest, ADroppedPlayerReturnsWithTheToken) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const String code = harness.Client(ids[0]).roomCode;
        const String token = harness.Client(ids[1]).token;
        harness.Start(ids[0]);
        harness.Client(ids[0]).Clear();

        harness.Disconnect(ids[1]);
        const ReceivedMessage* lobby = harness.Client(ids[0]).Last(RoomMessageTypes::kLobby);
        ASSERT_NE(lobby, nullptr);
        const Json* seats = FindJsonMember(lobby->json, "seats");
        ASSERT_NE(seats, nullptr);
        EXPECT_FALSE(GetJsonBool((*seats)[1], "connected").value_or(true));
        EXPECT_EQ(harness.Hub().GetRoomCount(), 1u);

        const ConnectionId back = harness.Connect();
        harness.JoinByToken(back, code, token);
        const TestClient& returned = harness.Client(back);
        EXPECT_EQ(returned.Types(), "joined,lobby,view");
        EXPECT_EQ(returned.seat, 1);
        EXPECT_TRUE(GetJsonBool((*FindJsonMember(harness.Client(ids[0]).Last(RoomMessageTypes::kLobby)->json, "seats"))[1], "connected").value_or(false));

        // After leaving for good the token is no use
        harness.Leave(back);
        const ConnectionId again = harness.Connect();
        harness.JoinByToken(again, code, token);
        EXPECT_EQ(harness.Client(again).LastErrorCode(), RoomErrors::kBadToken);
    }

    TEST(RoomHubTest, ARoomWhoseHumansAreAllAwayIsClosedAfterTheTimeout) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const String code = harness.Client(ids[0]).roomCode;
        harness.Start(ids[0]);
        harness.Disconnect(ids[0]);
        harness.Disconnect(ids[1]);
        harness.Tick(std::chrono::minutes(9));
        EXPECT_TRUE(harness.Hub().HasRoom(code));
        harness.Tick(std::chrono::minutes(1));
        EXPECT_FALSE(harness.Hub().HasRoom(code));
    }

    TEST(RoomHubTest, TheSeatOfAnAbsentPlayerIsPlayedByAComputerAfterTheGraceAndTheClientsSeeItInTheView) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        harness.Start(ids[0]);
        harness.Disconnect(ids[1]);
        // Nobody but the other human can see the game move; computers play their own seats and the absent seat waits out its grace
        const TestClient& host = harness.Client(ids[0]);
        for (int step = 0; step < 100; ++step) {
            harness.Tick(milliseconds(10));
            harness.PlayFirstAction(ids[0]);
        }
        const FakeSeatedGame* game = harness.GetGame(host.roomCode);
        ASSERT_NE(game, nullptr);
        // Seat 1 is silent inside the grace: it still holds all three cards unless it never had the turn
        EXPECT_EQ(game->GetHand(1).size(), static_cast<size_t>(FakeSeatedGame::kCardsPerSeat));
        harness.Tick(seconds(30));
        for (int step = 0; step < 100; ++step) {
            harness.Tick(milliseconds(10));
            harness.PlayFirstAction(ids[0]);
        }
        EXPECT_TRUE(game->IsFinished());
        // No view carried a hidden card, and the clients learn of the computer's moves through the version of the view
        EXPECT_EQ(harness.GetLeakCount(), 0u);
        EXPECT_TRUE(GetJsonBool(host.Last(RoomMessageTypes::kView)->json, "finished").value_or(false));
    }

    // ---- Limits per address, the failed-join budgets, the lobby timeout ------------------------------------------------------------

    TEST(RoomHubTest, RoomsPerAddressAreLimitedAndFollowTheRoomsLife) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.maxRoomsPerAddress = 2;
        HubHarness harness(settings);
        const ConnectionId first = harness.Connect("10.0.0.1");
        const ConnectionId second = harness.Connect("10.0.0.1");
        const ConnectionId third = harness.Connect("10.0.0.1");
        harness.Create(first, "A");
        harness.Create(second, "B");
        harness.Create(third, "C");
        EXPECT_EQ(harness.Client(third).LastErrorCode(), RoomErrors::kTooManyRooms);
        EXPECT_NE(harness.Client(third).Last(RoomMessageTypes::kError)->GetString("message").find("your address"), String::npos);
        EXPECT_EQ(harness.Hub().GetRoomCount(), 2u);

        // Another address, and a connection with no address, are not counted
        const ConnectionId other = harness.Connect("10.0.0.2");
        harness.Create(other, "D");
        EXPECT_EQ(harness.Client(other).Count(RoomMessageTypes::kJoined), 1u);
        const ConnectionId unknown = harness.Connect();
        harness.Create(unknown, "E");
        EXPECT_EQ(harness.Client(unknown).Count(RoomMessageTypes::kJoined), 1u);

        // A room stays the address's while it exists, also when its creator is gone
        const ConnectionId guest = harness.Connect("10.0.0.3");
        harness.JoinByName(guest, harness.Client(first).roomCode, "G");
        harness.Disconnect(first);
        harness.Create(third, "C");
        EXPECT_EQ(harness.Client(third).LastErrorCode(), RoomErrors::kTooManyRooms);
        // A closed room frees the place
        harness.Leave(guest);
        harness.Create(third, "C");
        EXPECT_EQ(harness.Client(third).Count(RoomMessageTypes::kJoined), 1u);
    }

    TEST(RoomHubTest, NoAddressMeansNoPerAddressLimit) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.maxRoomsPerAddress = 1;
        HubHarness harness(settings);
        for (int i = 0; i < 4; ++i) {
            const ConnectionId id = harness.Connect();
            harness.Create(id, "A");
            EXPECT_EQ(harness.Client(id).Count(RoomMessageTypes::kJoined), 1u);
        }
        EXPECT_EQ(harness.Hub().GetAddressCount(), 0u);
        // 0 is off for an address too
        settings.maxRoomsPerAddress = 0;
        HubHarness unlimited(settings);
        for (int i = 0; i < 5; ++i) {
            const ConnectionId id = unlimited.Connect("10.0.0.1");
            unlimited.Create(id, "A");
            EXPECT_EQ(unlimited.Client(id).Count(RoomMessageTypes::kJoined), 1u);
        }
    }

    TEST(RoomHubTest, GuessingAcrossReconnectsFromOneAddressIsStoppedByTheBudget) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.addressJoinBurst = 6;
        settings.addressJoinRefill = seconds(30);
        HubHarness harness(settings);
        const ConnectionId host = harness.Connect("10.0.0.9");
        harness.Create(host, "Ann");
        const String code = harness.Client(host).roomCode;

        // Five guesses close a connection, and the next one starts at zero: the budget of the address is what counts
        const ConnectionId one = harness.Connect("10.0.0.1");
        for (int i = 0; i < 5; ++i) {
            harness.JoinByName(one, "ZZZZZ", "Mal");
        }
        EXPECT_TRUE(harness.Client(one).closed);
        const ConnectionId two = harness.Connect("10.0.0.1");
        harness.JoinByName(two, "ZZZZZ", "Mal");
        EXPECT_EQ(harness.Client(two).LastErrorCode(), RoomErrors::kUnknownRoom);

        // The budget is empty: the answer is the same for a code that exists and for one that does not
        harness.JoinByName(two, "YYYYY", "Mal");
        const String absent = harness.Client(two).Last(RoomMessageTypes::kError)->text;
        harness.JoinByName(two, code, "Mal");
        const String present = harness.Client(two).Last(RoomMessageTypes::kError)->text;
        EXPECT_EQ(harness.Client(two).LastErrorCode(), RoomErrors::kRateLimited);
        EXPECT_EQ(absent, present);
        EXPECT_EQ(harness.Client(two).roomCode, "");

        // Another address is unaffected
        const ConnectionId neighbour = harness.Connect("10.0.0.2");
        harness.JoinByName(neighbour, "ZZZZZ", "Bob");
        EXPECT_EQ(harness.Client(neighbour).LastErrorCode(), RoomErrors::kUnknownRoom);
        harness.JoinByName(neighbour, code, "Bob");
        EXPECT_EQ(harness.Client(neighbour).Count(RoomMessageTypes::kJoined), 1u);

        // A refused join counts for the connection like a failed one: it is closed after the fifth in a row
        const ConnectionId three = harness.Connect("10.0.0.1");
        for (int i = 0; i < 4; ++i) {
            harness.JoinByName(three, code, "Mal");
        }
        EXPECT_FALSE(harness.Client(three).closed);
        harness.JoinByName(three, code, "Mal");
        EXPECT_TRUE(harness.Client(three).closed);

        // The budget refills with the clock: one guess per 30 s
        harness.Advance(seconds(30));
        const ConnectionId four = harness.Connect("10.0.0.1");
        harness.JoinByName(four, "ZZZZZ", "Mal");
        EXPECT_EQ(harness.Client(four).LastErrorCode(), RoomErrors::kUnknownRoom);
        harness.JoinByName(four, "ZZZZZ", "Mal");
        EXPECT_EQ(harness.Client(four).LastErrorCode(), RoomErrors::kRateLimited);
    }

    TEST(RoomHubTest, TheServerWideBudgetStopsGuessingFromManyAddresses) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.serverJoinBurst = 6;
        settings.serverJoinRefill = seconds(6);
        HubHarness harness(settings);
        const ConnectionId host = harness.Connect("10.0.0.99");
        harness.Create(host, "Ann");
        const String code = harness.Client(host).roomCode;
        for (int i = 0; i < 6; ++i) {
            const ConnectionId guesser = harness.Connect(Format("10.0.1.{}", i).c_str());
            harness.JoinByName(guesser, "ZZZZZ", "Mal");
            EXPECT_EQ(harness.Client(guesser).LastErrorCode(), RoomErrors::kUnknownRoom);
        }
        const ConnectionId next = harness.Connect("10.0.2.1");
        harness.JoinByName(next, code, "Mal");
        EXPECT_EQ(harness.Client(next).LastErrorCode(), RoomErrors::kRateLimited);
        // Also a connection with no address
        const ConnectionId unknown = harness.Connect();
        harness.JoinByName(unknown, "ZZZZZ", "Mal");
        EXPECT_EQ(harness.Client(unknown).LastErrorCode(), RoomErrors::kRateLimited);
        // create_room and ping are not touched
        harness.Ping(next);
        EXPECT_EQ(harness.Client(next).Count(RoomMessageTypes::kPong), 1u);
        harness.Create(next, "Mal");
        EXPECT_EQ(harness.Client(next).Count(RoomMessageTypes::kJoined), 1u);
        // One token comes back after 6 s
        harness.Advance(seconds(6));
        const ConnectionId later = harness.Connect("10.0.2.2");
        harness.JoinByName(later, code, "Bob");
        EXPECT_EQ(harness.Client(later).Count(RoomMessageTypes::kJoined), 1u);
    }

    TEST(RoomHubTest, ATypoAndARetryNeverTripTheBudgets) {
        HubHarness harness;
        const ConnectionId host = harness.Connect("10.0.0.9");
        harness.Create(host, "Ann");
        String typo = harness.Client(host).roomCode;
        typo[0] = typo[0] == 'A' ? 'B' : 'A';
        const ConnectionId guest = harness.Connect("10.0.0.1");
        harness.JoinByName(guest, typo, "Bob");
        EXPECT_EQ(harness.Client(guest).LastErrorCode(), RoomErrors::kUnknownRoom);
        harness.JoinByName(guest, harness.Client(host).roomCode, "Bob");
        EXPECT_EQ(harness.Client(guest).Count(RoomMessageTypes::kJoined), 1u);
    }

    TEST(RoomHubTest, AMemberWithItsTokenGetsBackWhileTheBudgetIsEmpty) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.serverJoinBurst = 2;
        settings.serverJoinRefill = std::chrono::hours(1);
        HubHarness harness(settings);
        const auto ids = harness.MakeLobby(2);
        harness.Start(ids[0]);
        const String code = harness.Client(ids[1]).roomCode;
        const String token = harness.Client(ids[1]).token;
        harness.Disconnect(ids[1]);
        for (int i = 0; i < 2; ++i) {
            const ConnectionId guesser = harness.Connect("10.0.5.1");
            harness.JoinByName(guesser, "ZZZZZ", "Mal");
        }
        // Empty now: a wrong token to this room and any join to an unknown room are the same answer
        const ConnectionId thief = harness.Connect("10.0.5.2");
        harness.JoinByToken(thief, code, String(RoomLimits::kTokenLength, 'a'));
        EXPECT_EQ(harness.Client(thief).LastErrorCode(), RoomErrors::kRateLimited);
        harness.JoinByToken(thief, "ZZZZZ", String(RoomLimits::kTokenLength, 'a'));
        EXPECT_EQ(harness.Client(thief).LastErrorCode(), RoomErrors::kRateLimited);
        // The member itself returns
        const ConnectionId back = harness.Connect("10.0.5.3");
        harness.JoinByToken(back, code, token);
        EXPECT_EQ(harness.Client(back).Count(RoomMessageTypes::kJoined), 1u);
        EXPECT_NE(harness.Client(back).Last(RoomMessageTypes::kView), nullptr);
    }

    TEST(RoomHubTest, TheAddressMapDoesNotGrow) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.addressJoinBurst = 4;
        settings.addressJoinRefill = seconds(1);
        HubHarness harness(settings);
        const ConnectionId open = harness.Connect("10.0.0.1");
        harness.Tick(seconds(10));
        // An address with a connection is kept
        EXPECT_EQ(harness.Hub().GetAddressCount(), 1u);
        harness.Ping(open);
        harness.Disconnect(open);
        harness.Tick();
        EXPECT_EQ(harness.Hub().GetAddressCount(), 0u);

        // An address with a budget not yet refilled is kept until it is
        const ConnectionId guesser = harness.Connect("10.0.0.2");
        harness.JoinByName(guesser, "ZZZZZ", "Mal");
        harness.Disconnect(guesser);
        harness.Tick(milliseconds(500));
        EXPECT_EQ(harness.Hub().GetAddressCount(), 1u);
        harness.Tick(seconds(1));
        EXPECT_EQ(harness.Hub().GetAddressCount(), 0u);

        for (int i = 0; i < 50; ++i) {
            harness.Disconnect(harness.Connect(Format("10.1.0.{}", i).c_str()));
        }
        harness.Tick();
        EXPECT_EQ(harness.Hub().GetAddressCount(), 0u);
    }

    TEST(RoomHubTest, ASilentLobbyIsClosedAndItsMembersFallToTheIdleRule) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.lobbyTimeout = std::chrono::minutes(1);
        HubHarness harness(settings);
        const auto ids = harness.MakeLobby(2);
        harness.Tick(seconds(59));
        EXPECT_EQ(harness.Hub().GetRoomCount(), 1u);
        harness.Tick(seconds(1));
        EXPECT_EQ(harness.Hub().GetRoomCount(), 0u);
        for (const ConnectionId id : ids) {
            const TestClient& client = harness.Client(id);
            ASSERT_NE(client.Last(RoomMessageTypes::kLeft), nullptr);
            EXPECT_EQ(client.Last(RoomMessageTypes::kLeft)->GetString("reason"), "lobby_timeout");
            EXPECT_FALSE(client.closed);
            EXPECT_EQ(client.roomCode, "");
        }
        // The connections are in no room now: the idle rule counts from the close
        harness.Tick(seconds(29));
        EXPECT_FALSE(harness.Client(ids[0]).closed);
        harness.Tick(seconds(1));
        EXPECT_TRUE(harness.Client(ids[0]).closed);
        EXPECT_EQ(harness.Client(ids[0]).closeReason, "idle");
    }

    TEST(RoomHubTest, OnlyARealMessageKeepsALobbyOpen) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.lobbyTimeout = std::chrono::minutes(1);
        HubHarness harness(settings);
        const ConnectionId host = harness.Connect();
        harness.Create(host, "Ann");
        // A ping does not count
        for (int i = 0; i < 5; ++i) {
            harness.Tick(seconds(10));
            harness.Ping(host);
        }
        harness.Tick(seconds(10));
        EXPECT_EQ(harness.Hub().GetRoomCount(), 0u);

        // choose_seat does, and so does a join
        const ConnectionId second = harness.Connect();
        harness.Create(second, "Bob");
        harness.Tick(seconds(50));
        harness.Send(second, ChooseSeatMessage{2});
        harness.Tick(seconds(50));
        const ConnectionId guest = harness.Connect();
        harness.JoinByName(guest, harness.Client(second).roomCode, "Cy");
        EXPECT_EQ(harness.Hub().GetRoomCount(), 1u);
        harness.Tick(seconds(50));
        EXPECT_EQ(harness.Hub().GetRoomCount(), 1u);
        harness.Tick(seconds(10));
        EXPECT_EQ(harness.Hub().GetRoomCount(), 0u);
    }

    TEST(RoomHubTest, AStartedRoomIsNotTouchedByTheLobbyTimeoutAndZeroSwitchesItOff) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.lobbyTimeout = std::chrono::minutes(1);
        HubHarness harness(settings);
        const auto ids = harness.MakeLobby(2);
        harness.Start(ids[0]);
        harness.Tick(std::chrono::minutes(5));
        EXPECT_EQ(harness.Hub().GetRoomCount(), 1u);
        EXPECT_EQ(harness.Client(ids[0]).Count(RoomMessageTypes::kLeft), 0u);

        settings.lobbyTimeout = milliseconds(0);
        HubHarness off(settings);
        const ConnectionId host = off.Connect();
        off.Create(host, "Ann");
        off.Tick(std::chrono::hours(5));
        EXPECT_EQ(off.Hub().GetRoomCount(), 1u);
    }

}  // namespace BECore::Tests
