#include "TestClient.h"

namespace BECore::Tests {

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::seconds;

        constexpr int kMaxSteps = 400;

        Json CardAction(const eastl::string& card) {
            Json action = Json::object();
            action["type"] = "play";
            action["card"] = card.c_str();
            return action;
        }

        bool IsFinishedFor(HubHarness& harness, ConnectionId id) {
            const ReceivedMessage* view = harness.Client(id).Last(RoomMessageTypes::kView);
            return view && GetJsonBool(view->json, "finished").value_or(false);
        }

        /// The number of cards on the table in the last view the client got (-1 when it has no view)
        int TableSize(HubHarness& harness, ConnectionId id) {
            const ReceivedMessage* view = harness.Client(id).Last(RoomMessageTypes::kView);
            const Json* inner = view ? FindJsonMember(view->json, "view") : nullptr;
            const Json* table = inner ? FindJsonMember(*inner, "table") : nullptr;
            return table && table->is_array() ? static_cast<int>(table->size()) : -1;
        }

        /// Ticks the clock and lets the humans play their first action until every human has seen the end
        bool PlayToTheEnd(HubHarness& harness, const eastl::vector<ConnectionId>& humans) {
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
        ConnectionId WaitForAHumanTurn(HubHarness& harness, const eastl::vector<ConnectionId>& humans) {
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

        void RunFourSeatHand(bool leaky) {
            HubHarness harness(HubHarness::MakeSettings(), leaky);
            harness.SetFailOnLeak(!leaky);
            const auto ids = harness.MakeLobby(2);
            harness.Start(ids[0]);
            ASSERT_TRUE(PlayToTheEnd(harness, ids));
            for (const ConnectionId id : ids) {
                EXPECT_GT(harness.Client(id).Count(RoomMessageTypes::kView), 0u);
                EXPECT_EQ(harness.Client(id).LastErrorCode(), "");
                // The end: every card is public
                EXPECT_EQ(TableSize(harness, id), FakeSeatedGame::kTotalPlays);
            }
            if (leaky) {
                EXPECT_GT(harness.GetLeakCount(), 0u);
            } else {
                EXPECT_EQ(harness.GetLeakCount(), 0u);
            }
        }

    }  // namespace

    TEST(RoomsScenarioTest, TwoHumansAndTwoComputersPlayAHandAndNobodyPeeks) {
        RunFourSeatHand(false);
    }

    TEST(RoomsScenarioTest, TheLeakCheckCatchesALeakyGame) {
        // The scenario must be able to fail: the same hand with a game that sends every hand is caught
        RunFourSeatHand(true);
    }

    TEST(RoomsScenarioTest, AHumanDropsMidHandAndReturnsWithTheToken) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        const eastl::string code = harness.Client(ids[0]).roomCode;
        const eastl::string token = harness.Client(ids[1]).token;
        harness.Start(ids[0]);
        const FakeSeatedGame* game = harness.GetGame(code);
        ASSERT_NE(game, nullptr);

        harness.Disconnect(ids[1]);
        // Inside the grace the host plays and the computers play theirs; seat 1 gets its turn and nobody acts for it
        for (int step = 0; step < 200; ++step) {
            harness.Tick(milliseconds(100));
            harness.PlayFirstAction(ids[0]);
        }
        ASSERT_EQ(game->GetTurn(), 1);
        EXPECT_EQ(game->GetHand(1).size(), static_cast<size_t>(FakeSeatedGame::kCardsPerSeat));
        const int stalledAt = game->GetPlayCount();
        harness.Tick(seconds(5));
        EXPECT_EQ(game->GetPlayCount(), stalledAt);

        // After the grace a computer plays the seat
        harness.Tick(seconds(30));
        for (int step = 0; step < kMaxSteps && game->GetHand(1).size() == FakeSeatedGame::kCardsPerSeat; ++step) {
            harness.Tick(milliseconds(10));
            harness.PlayFirstAction(ids[0]);
        }
        EXPECT_LT(game->GetHand(1).size(), static_cast<size_t>(FakeSeatedGame::kCardsPerSeat));
        ASSERT_FALSE(game->IsFinished());

        // The human returns on a new connection: the current view, and control at once
        const ConnectionId back = harness.Connect();
        harness.JoinByToken(back, code, token);
        EXPECT_EQ(harness.Client(back).Types(), "joined,lobby,view");
        EXPECT_EQ(harness.Client(back).seat, 1);
        const ReceivedMessage* view = harness.Client(back).Last(RoomMessageTypes::kView);
        ASSERT_NE(view, nullptr);
        EXPECT_EQ(static_cast<uint64_t>(view->GetInt("version")), game->GetStateVersion());

        // A third connection with the same token replaces it during play
        const ConnectionId replacement = harness.Connect();
        harness.JoinByToken(replacement, code, token);
        EXPECT_TRUE(harness.Client(back).closed);
        EXPECT_EQ(harness.Client(back).closeReason, "replaced");
        EXPECT_EQ(harness.Client(replacement).Types(), "joined,lobby,view");
        EXPECT_EQ(harness.Client(replacement).seat, 1);

        // Both play on to the end
        ASSERT_TRUE(PlayToTheEnd(harness, {ids[0], replacement}));
        EXPECT_TRUE(game->IsFinished());
        EXPECT_EQ(harness.GetLeakCount(), 0u);

        // After leaving for good the token is refused
        harness.Leave(replacement);
        const ConnectionId late = harness.Connect();
        harness.JoinByToken(late, code, token);
        EXPECT_EQ(harness.Client(late).LastErrorCode(), RoomErrors::kBadToken);
    }

    TEST(RoomsScenarioTest, TwoRoomsDoNotSeeEachOther) {
        HubHarness harness;
        const auto roomA = harness.MakeLobby(2);
        const auto roomB = harness.MakeLobby(2);
        const eastl::string codeA = harness.Client(roomA[0]).roomCode;
        const eastl::string codeB = harness.Client(roomB[0]).roomCode;
        ASSERT_NE(codeA, codeB);
        harness.Start(roomA[0]);
        harness.Start(roomB[0]);
        const FakeSeatedGame* gameA = harness.GetGame(codeA);
        const FakeSeatedGame* gameB = harness.GetGame(codeB);
        ASSERT_NE(gameA, nullptr);
        ASSERT_NE(gameB, nullptr);
        ASSERT_NE(gameA, gameB);

        // A move in room A does not touch room B
        const ConnectionId mover = WaitForAHumanTurn(harness, roomA);
        ASSERT_NE(mover, 0u);
        const uint64_t versionB = gameB->GetStateVersion();
        const int playsB = gameB->GetPlayCount();
        const int playsA = gameA->GetPlayCount();
        ASSERT_TRUE(harness.PlayFirstAction(mover));
        EXPECT_EQ(gameA->GetPlayCount(), playsA + 1);
        EXPECT_EQ(gameB->GetPlayCount(), playsB);
        EXPECT_EQ(gameB->GetStateVersion(), versionB);

        eastl::vector<ConnectionId> all = roomA;
        for (const ConnectionId id : roomB) {
            all.push_back(id);
        }
        ASSERT_TRUE(PlayToTheEnd(harness, all));
        EXPECT_EQ(harness.GetLeakCount(), 0u);
        for (const ConnectionId id : all) {
            const eastl::string& own = harness.Client(id).roomCode;
            for (const ReceivedMessage& message : harness.Client(id).received) {
                if (message.type == RoomMessageTypes::kView || message.type == RoomMessageTypes::kLobby) {
                    EXPECT_EQ(message.GetString("room"), own);
                }
            }
        }
    }

    TEST(RoomsScenarioTest, ActionsOfOtherSeatsAndOfOldVersionsNeverChangeTheGame) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(2);
        harness.Start(ids[0]);
        const FakeSeatedGame* game = harness.GetGame(harness.Client(ids[0]).roomCode);
        ASSERT_NE(game, nullptr);

        const ConnectionId onTurn = WaitForAHumanTurn(harness, ids);
        ASSERT_NE(onTurn, 0u);
        const ConnectionId other = onTurn == ids[0] ? ids[1] : ids[0];
        const int seat = harness.Client(onTurn).seat;
        const int otherSeat = harness.Client(other).seat;
        const uint64_t version = game->GetStateVersion();
        const int plays = game->GetPlayCount();
        const auto expectUnchanged = [&]() {
            EXPECT_EQ(game->GetStateVersion(), version);
            EXPECT_EQ(game->GetPlayCount(), plays);
        };

        // A card that belongs to another seat
        ActionMessage steal;
        steal.version = version;
        steal.action = CardAction(Format("c{}_0", otherSeat));
        harness.Send(onTurn, steal);
        EXPECT_EQ(harness.Client(onTurn).LastErrorCode(), RoomErrors::kIllegalAction);
        expectUnchanged();

        // A card that does not exist, and an action of the wrong shape
        ActionMessage nothing;
        nothing.version = version;
        nothing.action = CardAction("c9_9");
        harness.Send(onTurn, nothing);
        EXPECT_EQ(harness.Client(onTurn).LastErrorCode(), RoomErrors::kIllegalAction);
        ActionMessage shapeless;
        shapeless.version = version;
        harness.Send(onTurn, shapeless);
        EXPECT_NE(harness.Client(onTurn).LastErrorCode(), "");
        expectUnchanged();

        // The right card on an old and on a future version: stale, and the current view comes again
        const size_t viewsBefore = harness.Client(onTurn).Count(RoomMessageTypes::kView);
        ActionMessage stale;
        stale.version = version + 1;
        stale.action = CardAction(Format("c{}_0", seat));
        harness.Send(onTurn, stale);
        EXPECT_EQ(harness.Client(onTurn).LastErrorCode(), RoomErrors::kStaleAction);
        EXPECT_EQ(harness.Client(onTurn).Count(RoomMessageTypes::kView), viewsBefore + 1);
        stale.version = version == 0 ? 7 : version - 1;
        harness.Send(onTurn, stale);
        EXPECT_EQ(harness.Client(onTurn).LastErrorCode(), RoomErrors::kStaleAction);
        expectUnchanged();

        // The seat that is not on turn plays its own card with the right version
        ActionMessage early;
        early.version = version;
        early.action = CardAction(Format("c{}_0", otherSeat));
        harness.Send(other, early);
        EXPECT_NE(harness.Client(other).LastErrorCode(), "");
        expectUnchanged();
        EXPECT_EQ(harness.GetLeakCount(), 0u);

        // The game still goes on normally after all that
        ASSERT_TRUE(PlayToTheEnd(harness, ids));
    }

    TEST(RoomsScenarioTest, RandomMalformedAndOutOfOrderMessagesNeverCrashOrLeak) {
        constexpr int kSlots = 4;
        for (const uint64_t seed : {1u, 2u, 3u, 4u, 5u, 6u}) {
            HubHarness harness;
            const RandomFn random = MakeSequence(seed * 7919);
            ConnectionId slots[kSlots] = {};
            eastl::vector<eastl::pair<eastl::string, eastl::string>> knownTokens;

            const auto pick = [&](uint64_t count) { return random() % count; };
            const auto anyCode = [&]() -> eastl::string {
                for (const ConnectionId slot : slots) {
                    if (slot != 0 && !harness.Client(slot).roomCode.empty() && pick(2) == 0) {
                        return harness.Client(slot).roomCode;
                    }
                }
                return pick(2) == 0 ? eastl::string("ABCDE") : eastl::string("zz");
            };

            for (int step = 0; step < 500; ++step) {
                const size_t slotIndex = static_cast<size_t>(pick(kSlots));
                if (slots[slotIndex] != 0 && harness.Client(slots[slotIndex]).closed) {
                    slots[slotIndex] = 0;
                }
                if (slots[slotIndex] == 0) {
                    slots[slotIndex] = harness.Connect();
                }
                const ConnectionId id = slots[slotIndex];
                TestClient& client = harness.Client(id);
                if (!client.token.empty()) {
                    knownTokens.push_back({client.roomCode, client.token});
                }
                harness.Advance(milliseconds(pick(200)));

                switch (pick(13)) {
                    case 0: {
                        eastl::string bytes;
                        const uint64_t length = pick(120);
                        for (uint64_t i = 0; i < length; ++i) {
                            bytes.push_back(static_cast<char>(pick(256)));
                        }
                        harness.SendText(id, bytes);
                        break;
                    }
                    case 1: {
                        const eastl::string valid = SerializeClientMessage(ClientMessage{ChooseSeatMessage{static_cast<int64_t>(pick(5))}});
                        harness.SendText(id, StringView(valid.data(), static_cast<size_t>(pick(valid.size() + 1))));
                        break;
                    }
                    case 2:
                        harness.Create(id, "Fuzz", pick(5) == 0 ? "nogame" : "fake");
                        break;
                    case 3:
                        harness.JoinByName(id, anyCode(), pick(6) == 0 ? "" : "Guest");
                        break;
                    case 4: {
                        if (!knownTokens.empty()) {
                            const auto& [code, token] = knownTokens[static_cast<size_t>(pick(knownTokens.size()))];
                            harness.JoinByToken(id, pick(4) == 0 ? anyCode() : code, token);
                        } else {
                            harness.JoinByToken(id, anyCode(), eastl::string(RoomLimits::kTokenLength, 'f'));
                        }
                        break;
                    }
                    case 5:
                        harness.Send(id, ChooseSeatMessage{static_cast<int64_t>(pick(9)) - 3});
                        break;
                    case 6:
                        harness.Start(id);
                        break;
                    case 7:
                    case 8: {
                        ActionMessage action;
                        const FakeSeatedGame* game = harness.GetGame(client.roomCode);
                        action.version = game && pick(3) != 0 ? game->GetStateVersion() : pick(30);
                        action.action = CardAction(Format("c{}_{}", pick(5), pick(4)));
                        harness.Send(id, action);
                        break;
                    }
                    case 9:
                        if (pick(3) == 0) {
                            harness.Leave(id);
                        } else {
                            harness.Ping(id);
                        }
                        break;
                    case 10:
                        harness.Tick(milliseconds(pick(40000)));
                        break;
                    case 11:
                        harness.PlayFirstAction(id);
                        break;
                    default:
                        harness.Disconnect(id);
                        slots[slotIndex] = 0;
                        break;
                }
            }
            harness.Tick(std::chrono::minutes(30));
            EXPECT_EQ(harness.GetLeakCount(), 0u) << "seed " << seed;
        }
    }

}  // namespace BECore::Tests
