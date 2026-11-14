#include "TestClient.h"
#include "WireJudge.h"

namespace BECore::Tests {

    /// The hub in memory against a client that does what no honest client does (the sockets: RoomServerHostileTest). The harness judges every text
    /// against the live game as it is sent (a leak fails the test); the wire judge here reads the same texts again from the client's side.

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::minutes;
        using std::chrono::seconds;

        constexpr int kMaxSteps = 400;
        constexpr int kReplays = 100;

        Json CardAction(const String& card) {
            Json action = Json::object();
            action["type"] = "play";
            action["card"] = card.c_str();
            return action;
        }

        bool IsFinishedFor(HubHarness& harness, ConnectionId id) {
            const ReceivedMessage* view = harness.Client(id).Last(RoomMessageTypes::kView);
            return view && GetJsonBool(view->json, "finished").value_or(false);
        }

        /// Ticks the clock and lets the humans play their first action until every one has seen the end
        bool PlayToTheEnd(HubHarness& harness, const Vector<ConnectionId>& humans) {
            for (int step = 0; step < kMaxSteps; ++step) {
                harness.Tick(milliseconds(10));
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
        ConnectionId WaitForAHumanTurn(HubHarness& harness, const Vector<ConnectionId>& humans) {
            for (int step = 0; step < kMaxSteps; ++step) {
                for (const ConnectionId id : humans) {
                    const ReceivedMessage* view = harness.Client(id).Last(RoomMessageTypes::kView);
                    const Json* actions = view ? FindJsonMember(view->json, "actions") : nullptr;
                    if (actions && actions->is_array() && !actions->empty()) {
                        return id;
                    }
                }
                harness.Tick(milliseconds(10));
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

        /// The cards a client was sent that the wire judge finds hidden from its seat
        Vector<std::string> LeaksSeenBy(const TestClient& client) {
            Vector<std::string> leaks;
            for (const ReceivedMessage& message : client.received) {
                // std: meets std::string until the std::string of this file moves to String
                for (std::string& id : FindLeakedCardIds(std::string_view(message.text.c_str(), message.text.size()), client.seat)) {
                    leaks.push_back(std::move(id));
                }
            }
            return leaks;
        }

        /// A rate limit is not what these cases test: the clock moves a second between two raw messages
        void SendRaw(HubHarness& harness, ConnectionId id, StringView text) {
            harness.Advance(seconds(1));
            harness.SendText(id, text);
        }

    }  // namespace

    // ---- tokens and the codes of rooms ---------------------------------------------------------------------------------------------

    TEST(RoomHostileTest, TheTokenOfOneRoomWithTheCodeOfAnotherIsBadTokenAndCountsAsAFailedJoin) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.maxFailedJoinsInRow = 100;
        settings.addressJoinBurst = 3;
        settings.addressJoinRefill = seconds(30);
        HubHarness harness(settings);
        const ConnectionId hostA = harness.Connect();
        harness.Create(hostA, "Ann");
        const ConnectionId hostB = harness.Connect();
        harness.Create(hostB, "Bob");
        const String codeB = harness.Client(hostB).roomCode;
        const String tokenA = harness.Client(hostA).token;
        ASSERT_NE(harness.Client(hostA).roomCode, codeB);

        // The token of A is a member's, but not of B: the same refusal as for a token nobody has, and a failed join of the address
        const ConnectionId thief = harness.Connect("10.0.0.7");
        for (int i = 0; i < 3; ++i) {
            harness.JoinByToken(thief, codeB, tokenA);
            EXPECT_EQ(harness.Client(thief).LastErrorCode(), RoomErrors::kBadToken) << i;
        }
        EXPECT_EQ(harness.Client(thief).roomCode, "");
        EXPECT_FALSE(harness.Client(thief).closed);
        // The budget of the address took one for each, so the fourth guess is not even looked at
        harness.JoinByToken(thief, codeB, tokenA);
        EXPECT_EQ(harness.Client(thief).LastErrorCode(), RoomErrors::kRateLimited);
        EXPECT_EQ(harness.Client(thief).roomCode, "");

        // Nothing happened to B
        EXPECT_EQ(harness.Client(hostB).Count(RoomMessageTypes::kLobby), 1u) << "the lobby of B never changed";
        EXPECT_EQ(harness.Client(hostA).Count(RoomMessageTypes::kLobby), 1u);
    }

    TEST(RoomHostileTest, ListingAThousandTimesBetweenGuessesLearnsNoPrivateCodeAndTheCloseAfterWrongCodesStillComes) {
        HubHarness harness;
        Vector<String> privateCodes;
        Vector<ConnectionId> members;
        for (int i = 0; i < 3; ++i) {
            const ConnectionId host = harness.Connect();
            harness.Create(host, Format("Private{}", i).c_str());
            privateCodes.push_back(harness.Client(host).roomCode);
            members.push_back(host);
        }
        const ConnectionId publicHost = harness.Connect();
        harness.Create(publicHost, "Open", "fake", Json::object(), true);
        members.push_back(publicHost);

        // Many connections, each of which lists as much as it may and guesses a wrong code now and then; every one is closed in the end
        Vector<ConnectionId> attackers;
        int lists = 0;
        while (lists < 1000) {
            const ConnectionId attacker = harness.Connect("10.0.0.9");
            attackers.push_back(attacker);
            for (int i = 0; i < 100 && !harness.Client(attacker).closed; ++i) {
                String guess = Format("{}{}{}{}{}", "ABCDEFGH"[i % 8], "JKLMNPQR"[(i / 8) % 8], "STUVWXYZ"[lists % 8], "23456789"[i % 8], "ABCDEFGH"[(lists / 8) % 8]);
                SendRaw(harness, attacker, SerializeClientMessage(ListRoomsMessage{"fake"}));
                ++lists;
                if (i % 7 == 6) {
                    SendRaw(harness, attacker, SerializeClientMessage(JoinRoomMessage{guess, "Eve", {}}));
                }
            }
            harness.Advance(minutes(2));
        }
        EXPECT_GE(lists, 1000);
        for (const ConnectionId attacker : attackers) {
            for (const String& code : privateCodes) {
                EXPECT_EQ(CountTextsContaining(harness.Client(attacker), code), 0u) << code.c_str();
            }
            EXPECT_GE(harness.Client(attacker).Count(RoomMessageTypes::kRooms), 1u);
        }
        EXPECT_EQ(harness.GetLeakCount(), 0u);

        // The failed-join limit is the same as without lists: the fifth wrong code in a row closes the connection, lists in between or not
        harness.Advance(minutes(10));
        const ConnectionId guesser = harness.Connect("10.0.0.10");
        for (int i = 0; i < RoomLimits::kMaxFailedJoinsInRow; ++i) {
            EXPECT_FALSE(harness.Client(guesser).closed) << i;
            SendRaw(harness, guesser, SerializeClientMessage(ListRoomsMessage{"fake"}));
            SendRaw(harness, guesser, SerializeClientMessage(JoinRoomMessage{"ZZZZ2", "Eve", {}}));
        }
        EXPECT_TRUE(harness.Client(guesser).closed);
        EXPECT_EQ(harness.Client(guesser).closeReason, "too_many_failed_joins");
    }

    TEST(RoomHostileTest, AFewWrongTokensForOneRoomCloseTheConnectionLikeWrongCodes) {
        HubHarness harness;
        const ConnectionId hostA = harness.Connect();
        harness.Create(hostA, "Ann");
        const ConnectionId hostB = harness.Connect();
        harness.Create(hostB, "Bob");
        const String codeB = harness.Client(hostB).roomCode;
        const ConnectionId thief = harness.Connect();
        for (int i = 0; i < RoomLimits::kMaxFailedJoinsInRow; ++i) {
            EXPECT_FALSE(harness.Client(thief).closed);
            harness.JoinByToken(thief, codeB, harness.Client(hostA).token);
        }
        EXPECT_TRUE(harness.Client(thief).closed);
    }

    TEST(RoomHostileTest, ATokenIsInNoTextSentToAnotherConnection) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(3);
        const String code = harness.Client(ids[0]).roomCode;
        const String tokens[] = {harness.Client(ids[0]).token, harness.Client(ids[1]).token, harness.Client(ids[2]).token};
        harness.Start(ids[0]);

        // One that drops mid-game, a connection that takes its seat over and a stranger that tries every token against the code
        harness.Disconnect(ids[1]);
        harness.Tick(seconds(31));
        const ConnectionId stranger = harness.Connect();
        const ConnectionId back = harness.Connect();
        harness.JoinByToken(back, code, tokens[1]);
        ASSERT_EQ(harness.Client(back).Count(RoomMessageTypes::kJoined), 1u);
        const ConnectionId replacement = harness.Connect();
        harness.JoinByToken(replacement, code, tokens[1]);
        ASSERT_EQ(harness.Client(replacement).Count(RoomMessageTypes::kJoined), 1u);
        EXPECT_EQ(harness.Client(back).closeReason, "replaced");
        for (const String& token : tokens) {
            harness.JoinByToken(stranger, "ZZZZZ", token);
        }
        ASSERT_TRUE(PlayToTheEnd(harness, {ids[0], ids[2], replacement}));

        // Every connection saw its own token and no other
        struct Owner {
            ConnectionId connection;
            int token;
        };
        const Owner owners[] = {{ids[0], 0}, {ids[1], 1}, {ids[2], 2}, {back, 1}, {replacement, 1}, {stranger, -1}};
        for (const Owner& owner : owners) {
            for (int token = 0; token < 3; ++token) {
                if (token != owner.token) {
                    EXPECT_EQ(CountTextsContaining(harness.Client(owner.connection), tokens[token]), 0u) << "connection " << owner.connection << " was sent the token of member " << token;
                }
            }
        }
        EXPECT_EQ(harness.GetLeakCount(), 0u);
    }

    // ---- what a connection that is out of the room is sent -------------------------------------------------------------------------

    TEST(RoomHostileTest, AfterLeaveTheConnectionIsSentNothingOfTheRoom) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(3);
        const String code = harness.Client(ids[0]).roomCode;
        const String token = harness.Client(ids[2]).token;
        harness.Start(ids[0]);
        const ConnectionId leaver = ids[2];
        harness.Leave(leaver);
        ASSERT_NE(harness.Client(leaver).Last(RoomMessageTypes::kLeft), nullptr);
        const size_t receivedAtTheLeave = harness.Client(leaver).received.size();

        // The others play on to the end, the leaver's seat with them, and the leaver tries to come back and to act
        harness.Send(leaver, ActionMessage{0, CardAction("c2_0")});
        EXPECT_EQ(harness.Client(leaver).LastErrorCode(), RoomErrors::kNotInRoom);
        harness.JoinByToken(leaver, code, token);
        EXPECT_EQ(harness.Client(leaver).LastErrorCode(), RoomErrors::kBadToken);
        const size_t receivedAtTheRefusals = harness.Client(leaver).received.size();
        ASSERT_TRUE(PlayToTheEnd(harness, {ids[0], ids[1]}));
        EXPECT_EQ(harness.Client(leaver).received.size(), receivedAtTheRefusals);
        EXPECT_EQ(receivedAtTheRefusals, receivedAtTheLeave + 2) << "the two refusals and nothing else";
        EXPECT_EQ(harness.GetLeakCount(), 0u);
    }

    TEST(RoomHostileTest, AfterReplacedTheOldConnectionIsSentNothingMore) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const String code = harness.Client(ids[0]).roomCode;
        const String token = harness.Client(ids[1]).token;
        harness.Start(ids[0]);
        harness.Tick(seconds(1));

        const ConnectionId replacement = harness.Connect();
        harness.JoinByToken(replacement, code, token);
        const TestClient& old = harness.Client(ids[1]);
        ASSERT_TRUE(old.closed);
        ASSERT_EQ(old.Last(RoomMessageTypes::kLeft)->GetString("reason"), "replaced");
        const size_t receivedAtTheEnd = old.received.size();
        EXPECT_EQ(old.received.back().type, RoomMessageTypes::kLeft);

        ASSERT_TRUE(PlayToTheEnd(harness, {ids[0], replacement}));
        EXPECT_EQ(old.received.size(), receivedAtTheEnd);
        EXPECT_EQ(harness.GetLeakCount(), 0u);
    }

    TEST(RoomHostileTest, WhenTheRoomIsClosedNothingOfItReachesTheConnectionsAnyMore) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const String code = harness.Client(ids[0]).roomCode;
        const String token = harness.Client(ids[1]).token;
        harness.Start(ids[0]);
        ASSERT_TRUE(PlayToTheEnd(harness, ids));

        // The finished room is kept for the abandoned timeout, then closed: `left`, and silence
        harness.Tick(minutes(10));
        ASSERT_FALSE(harness.Hub().HasRoom(code));
        for (const ConnectionId id : ids) {
            ASSERT_EQ(harness.Client(id).received.back().type, RoomMessageTypes::kLeft);
            EXPECT_EQ(harness.Client(id).received.back().GetString("reason"), "room_finished");
        }
        const size_t sizes[] = {harness.Client(ids[0]).received.size(), harness.Client(ids[1]).received.size()};

        // The code and the token of the closed room are nothing now
        const ConnectionId late = harness.Connect();
        harness.JoinByToken(late, code, token);
        EXPECT_EQ(harness.Client(late).LastErrorCode(), RoomErrors::kUnknownRoom);
        harness.JoinByName(late, code, "Late");
        EXPECT_EQ(harness.Client(late).LastErrorCode(), RoomErrors::kUnknownRoom);
        harness.Tick(seconds(5));
        harness.Send(ids[0], ActionMessage{0, CardAction("c0_0")});
        EXPECT_EQ(harness.Client(ids[0]).LastErrorCode(), RoomErrors::kNotInRoom);
        EXPECT_EQ(harness.Client(ids[0]).received.size(), sizes[0] + 1);
        EXPECT_EQ(harness.Client(ids[1]).received.size(), sizes[1]);

        // A room closed because its last human left: the code is free and the one who left hears only its own `left`
        const ConnectionId lone = harness.Connect();
        harness.Create(lone, "Lone");
        const String loneCode = harness.Client(lone).roomCode;
        harness.Leave(lone);
        EXPECT_FALSE(harness.Hub().HasRoom(loneCode));
        EXPECT_EQ(harness.Client(lone).Types(), "joined,lobby,left");
    }

    TEST(RoomHostileTest, ASeatIsSentNoViewBeforeTheStart) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const String code = harness.Client(ids[0]).roomCode;
        const ConnectionId third = harness.Connect();
        harness.JoinByName(third, code, "Cid");
        harness.Send(ids[1], ChooseSeatMessage{3});
        harness.Send(ids[0], ChooseSeatMessage{2});
        harness.Send(third, ChooseSeatMessage{0});
        harness.Leave(third);
        const ConnectionId fourth = harness.Connect();
        harness.JoinByName(fourth, code, "Dee");
        harness.Tick(seconds(5));
        harness.Ping(ids[0]);
        for (const ConnectionId id : {ids[0], ids[1], third, fourth}) {
            EXPECT_EQ(harness.Client(id).Count(RoomMessageTypes::kView), 0u) << "connection " << id;
        }
        EXPECT_GT(harness.Client(ids[0]).Count(RoomMessageTypes::kLobby), 1u);

        harness.Start(ids[0]);
        for (const ConnectionId id : {ids[0], ids[1], fourth}) {
            EXPECT_EQ(harness.Client(id).Count(RoomMessageTypes::kView), 1u) << "connection " << id;
        }
        EXPECT_EQ(harness.Client(third).Count(RoomMessageTypes::kView), 0u) << "the one who left is not sent the game";
    }

    TEST(RoomHostileTest, TheHandOfAComputerSeatIsNeverOnTheWireOfAHumanAlsoAfterASeatChangeAndAReconnect) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const String code = harness.Client(ids[0]).roomCode;
        const String token = harness.Client(ids[1]).token;
        // The humans end up on seats 2 and 3; the computers hold 0 and 1
        harness.Send(ids[1], ChooseSeatMessage{3});
        harness.Send(ids[0], ChooseSeatMessage{2});
        harness.Start(ids[0]);
        ASSERT_EQ(harness.Client(ids[0]).seat, 2);
        ASSERT_EQ(harness.Client(ids[1]).seat, 3);
        for (int i = 0; i < 20; ++i) {
            harness.Tick(milliseconds(10));
            harness.PlayFirstAction(ids[0]);
            harness.PlayFirstAction(ids[1]);
        }
        harness.Disconnect(ids[1]);
        harness.Tick(seconds(31));
        const ConnectionId back = harness.Connect();
        harness.JoinByToken(back, code, token);
        ASSERT_EQ(harness.Client(back).seat, 3);
        ASSERT_TRUE(PlayToTheEnd(harness, {ids[0], back}));

        for (const ConnectionId id : {ids[0], ids[1], back}) {
            const auto leaks = LeaksSeenBy(harness.Client(id));
            EXPECT_TRUE(leaks.empty()) << "connection " << id << " was sent the hidden card " << (leaks.empty() ? "" : leaks.front().c_str());
        }
        EXPECT_EQ(harness.GetLeakCount(), 0u);
    }

    // ---- the game ----------------------------------------------------------------------------------------------------------------

    TEST(RoomHostileTest, ActionsOfASeatThatIsNotOnTurnReplayedLeaveTheGameAndTheOthersAlone) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        harness.Start(ids[0]);
        const FakeSeatedGame* game = harness.GetGame(harness.Client(ids[0]).roomCode);
        ASSERT_NE(game, nullptr);
        const ConnectionId onTurn = WaitForAHumanTurn(harness, ids);
        ASSERT_NE(onTurn, 0u);
        const ConnectionId other = onTurn == ids[0] ? ids[1] : ids[0];
        const int otherSeat = harness.Client(other).seat;
        const uint64_t version = game->GetStateVersion();
        const int plays = game->GetPlayCount();
        const size_t onTurnReceived = harness.Client(onTurn).received.size();
        const size_t otherViews = harness.Client(other).Count(RoomMessageTypes::kView);
        const size_t otherErrors = harness.Client(other).Count(RoomMessageTypes::kError);

        // The right version and a card the seat does hold: only the turn is wrong
        ActionMessage early;
        early.version = version;
        early.action = CardAction(Format("c{}_0", otherSeat));
        for (int i = 0; i < kReplays; ++i) {
            harness.Advance(seconds(1));
            harness.Send(other, early);
            ASSERT_EQ(harness.Client(other).LastErrorCode(), RoomErrors::kNotYourTurn) << i;
        }
        EXPECT_EQ(harness.Client(other).Count(RoomMessageTypes::kError), otherErrors + kReplays);
        EXPECT_EQ(harness.Client(other).Count(RoomMessageTypes::kView), otherViews);
        EXPECT_FALSE(harness.Client(other).closed);
        EXPECT_EQ(game->GetStateVersion(), version);
        EXPECT_EQ(game->GetPlayCount(), plays);
        EXPECT_EQ(harness.Client(onTurn).received.size(), onTurnReceived) << "the seat on turn heard of nothing";

        // The game goes on as if nothing had happened
        ASSERT_TRUE(PlayToTheEnd(harness, ids));
        EXPECT_EQ(harness.GetLeakCount(), 0u);
    }

    TEST(RoomHostileTest, ChooseSeatWithHugeNegativeAndNonIntegerNumbersChangesNothing) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const TestClient& client = harness.Client(ids[1]);
        const int seat = client.seat;
        const size_t lobbies = harness.Client(ids[0]).Count(RoomMessageTypes::kLobby);
        const char* const seats[] = {
            "9223372036854775807", "-9223372036854775808", "-1", "4", "2147483648", "1.5", "1e308", "18446744073709551615", "18446744073709551616", "\"1\"", "null", "true", "[1]", "{}", "-0.5"};
        int sent = 0;
        for (const char* text : seats) {
            SendRaw(harness, ids[1], Format("{{\"v\":1,\"type\":\"choose_seat\",\"seat\":{}}}", text));
            ++sent;
            EXPECT_NE(client.LastErrorCode(), "") << text;
            EXPECT_EQ(client.Count(RoomMessageTypes::kError), static_cast<size_t>(sent)) << text;
        }
        SendRaw(harness, ids[1], "{\"v\":1,\"type\":\"choose_seat\"}");
        EXPECT_EQ(client.LastErrorCode(), RoomErrors::kBadMessage);
        EXPECT_EQ(client.seat, seat);
        EXPECT_FALSE(client.closed);
        EXPECT_EQ(harness.Client(ids[0]).Count(RoomMessageTypes::kLobby), lobbies) << "the room never changed";
        // And a right number still works
        harness.Send(ids[1], ChooseSeatMessage{3});
        EXPECT_EQ(harness.Client(ids[0]).Count(RoomMessageTypes::kLobby), lobbies + 1);
    }

    TEST(RoomHostileTest, StartByAGuestOrASecondStartChangesNothing) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const String code = harness.Client(ids[0]).roomCode;
        const size_t views = harness.Client(ids[0]).Count(RoomMessageTypes::kView);
        for (int i = 0; i < 3; ++i) {
            harness.Start(ids[1]);
            EXPECT_EQ(harness.Client(ids[1]).LastErrorCode(), RoomErrors::kNotHost);
        }
        EXPECT_EQ(harness.GetGame(code), nullptr) << "a guest cannot start a game";
        EXPECT_EQ(harness.Client(ids[0]).Count(RoomMessageTypes::kView), views);

        harness.Start(ids[0]);
        const FakeSeatedGame* game = harness.GetGame(code);
        ASSERT_NE(game, nullptr);
        const uint64_t version = game->GetStateVersion();
        const size_t viewsAfterStart = harness.Client(ids[0]).Count(RoomMessageTypes::kView);
        for (int i = 0; i < 3; ++i) {
            harness.Start(ids[0]);
            EXPECT_EQ(harness.Client(ids[0]).LastErrorCode(), RoomErrors::kGameStarted);
            harness.Start(ids[1]);
            EXPECT_NE(harness.Client(ids[1]).LastErrorCode(), "");
        }
        EXPECT_EQ(harness.GetGame(code), game) << "the game was not replaced";
        EXPECT_EQ(game->GetStateVersion(), version);
        EXPECT_EQ(harness.Client(ids[0]).Count(RoomMessageTypes::kView), viewsAfterStart);
    }

    // ---- names ---------------------------------------------------------------------------------------------------------------------

    TEST(RoomHostileTest, ANameOfControlCharactersOrBrokenUtf8IsRefusedAndIsInNoLobby) {
        HubHarness harness;
        const ConnectionId host = harness.Connect();
        harness.Create(host, "Host");
        const String code = harness.Client(host).roomCode;

        const String names[] = {"\\u0007bell", "a\\u0000b",  "line\\nbreak", "tab\\tname", "\\u001fus", "\\u007fdel", "c1\\u0085",
                                "\\u200Bzero", "\\u202Ertl", "\\u2028sep",   "\\ufeffbom", "   ",       "",           "aaaaaaaaaaaaaaaaaaaaaaaaa",
                                "\\ud800lone", "\\udc00low"};
        const ConnectionId guest = harness.Connect();
        for (const String& name : names) {
            const size_t errors = harness.Client(guest).Count(RoomMessageTypes::kError);
            SendRaw(harness, guest, Format("{{\"v\":1,\"type\":\"join_room\",\"code\":\"{}\",\"name\":\"{}\"}}", code, name));
            EXPECT_EQ(harness.Client(guest).Count(RoomMessageTypes::kError), errors + 1) << name.c_str();
            EXPECT_EQ(harness.Client(guest).Count(RoomMessageTypes::kJoined), 0u) << name.c_str();
        }
        // Bytes that are not UTF-8 at all, raw in the text: not a message
        for (const char* raw : {"\xC3\x28", "\xC0\xAF", "\xFF\xFE", "\xED\xA0\x80", "\xF8\x88\x80\x80\x80", "\xE2\x82"}) {
            const size_t errors = harness.Client(guest).Count(RoomMessageTypes::kError);
            SendRaw(harness, guest, Format("{{\"v\":1,\"type\":\"join_room\",\"code\":\"{}\",\"name\":\"x{}\"}}", code, raw));
            EXPECT_EQ(harness.Client(guest).Count(RoomMessageTypes::kError), errors + 1);
            EXPECT_EQ(harness.Client(guest).Count(RoomMessageTypes::kJoined), 0u);
        }
        // The same for a room that is being created
        const ConnectionId maker = harness.Connect();
        SendRaw(harness, maker, "{\"v\":1,\"type\":\"create_room\",\"game\":\"fake\",\"name\":\"a\\u0007\"}");
        EXPECT_EQ(harness.Client(maker).LastErrorCode(), RoomErrors::kBadName);
        EXPECT_EQ(harness.Client(maker).Count(RoomMessageTypes::kJoined), 0u);

        // A guest with a good name is let in, and every lobby anybody saw lists only real names
        harness.JoinByName(guest, code, "Guest");
        ASSERT_EQ(harness.Client(guest).Count(RoomMessageTypes::kJoined), 1u);
        for (const ConnectionId id : {host, guest}) {
            for (const ReceivedMessage& message : harness.Client(id).received) {
                if (message.type != RoomMessageTypes::kLobby) {
                    continue;
                }
                const Json* seats = FindJsonMember(message.json, "seats");
                ASSERT_NE(seats, nullptr);
                for (const Json& seat : *seats) {
                    const String name = GetJsonString(seat, "name").value_or(String());
                    EXPECT_TRUE(name.empty() || name == "Host" || name == "Guest") << name.c_str();
                }
            }
        }
        EXPECT_EQ(harness.Hub().GetRoomCount(), 1u);
    }

}  // namespace BECore::Tests
