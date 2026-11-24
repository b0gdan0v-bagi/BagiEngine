#include <BECore/Rooms/FakeSeatedGame.h>
#include <BECore/Rooms/TestClient.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Games/SeatedGameRegistry.h>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        template <typename T>
        String ErrorCode(const std::expected<T, ControlError>& value) {
            return value ? String() : value.error().code;
        }

        /// A card on the table, as the test records it while it plays: the game does not hand its table out
        struct PlayedCard {
            int seat = 0;
            String card;
        };

        // The reference answers: the `Json` the demo game built before it answered typed structs, from the truth the game exposes (hands, turn)
        // and the plays the test recorded. The typed answers must write the same text.

        Json ReferenceView(const FakeSeatedGame& game, int seat, const Vector<PlayedCard>& table) {
            Json view = Json::object();
            view["seat"] = seat;
            Json hand = Json::array();
            for (const String& card : game.GetHand(seat)) {
                hand.push_back(card.c_str());
            }
            view["hand"] = hand;
            Json counts = Json::array();
            for (int other = 0; other < FakeSeatedGame::kSeatCount; ++other) {
                counts.push_back(static_cast<int>(game.GetHand(other).size()));
            }
            view["cardCounts"] = counts;
            Json played = Json::array();
            for (const PlayedCard& play : table) {
                Json entry = Json::object();
                entry["seat"] = play.seat;
                entry["card"] = play.card.c_str();
                played.push_back(entry);
            }
            view["table"] = played;
            view["turn"] = game.IsFinished() ? -1 : game.GetTurn();
            view["finished"] = game.IsFinished();
            return view;
        }

        Json ReferenceActions(const FakeSeatedGame& game, int seat) {
            Json actions = Json::array();
            if (game.GetTurn() == seat) {
                for (const String& card : game.GetHand(seat)) {
                    actions.push_back(CardAction(card));
                }
            }
            return actions;
        }

        Json ReferenceOutcome(int seat, const String& card) {
            Json outcome = Json::object();
            outcome["seat"] = seat;
            outcome["card"] = card.c_str();
            return outcome;
        }

        /// Plays a started game to the end by the computer's choice; @p atPoint runs at the start, after every play and at the end, with the plays so far
        template <typename TAtPoint>
        void PlayChecked(FakeSeatedGame& game, TAtPoint&& atPoint) {
            Vector<PlayedCard> table;
            atPoint(table);
            while (!game.IsFinished()) {
                const int seat = game.GetTurn();
                const auto action = game.ChooseComputerAction(seat);
                ASSERT_TRUE(action.has_value());
                const auto outcome = game.PerformSeatAction(seat, *action);
                ASSERT_TRUE(outcome.has_value());
                const String card = Require(GetJsonString(*action, "card"));
                EXPECT_EQ(DumpJson(OutcomeJson(**outcome)), DumpJson(ReferenceOutcome(seat, card)));
                table.push_back(PlayedCard{seat, card});
                atPoint(table);
            }
        }

        bool Holds(const Vector<String>& cards, const String& card) {
            return std::find(cards.begin(), cards.end(), card) != cards.end();
        }

        /// A started game whose first seat is 0
        FakeSeatedGame MakeStartedGame(uint64_t seed = 7) {
            FakeSeatedGame game;
            Json options = Json::object();
            options["firstSeat"] = 0;
            EXPECT_TRUE(game.Start(seed, options).has_value());
            return game;
        }

        /// Plays the lowest card of the seat on turn until the game is over
        void PlayToEnd(FakeSeatedGame& game) {
            while (!game.IsFinished()) {
                const int seat = game.GetTurn();
                const auto action = game.ChooseComputerAction(seat);
                ASSERT_TRUE(action.has_value());
                ASSERT_TRUE(game.PerformSeatAction(seat, *action).has_value());
            }
        }

    }  // namespace

    static_assert(FakeSeatedGame::kTotalPlays == 12);

    TEST(SeatedGameTest, CompileTimeChecks) {
        static_assert(std::is_abstract_v<ISeatedGame>);
        static_assert(std::is_base_of_v<ISeatedGame, FakeSeatedGame>);
        static_assert(std::is_base_of_v<FakeSeatedGame, LeakyFakeSeatedGame>);
        SUCCEED();
    }

    TEST(SeatedGameTest, StartDealsThreeUniqueCardsPerSeat) {
        FakeSeatedGame game = MakeStartedGame();
        Vector<String> all;
        for (int seat = 0; seat < FakeSeatedGame::kSeatCount; ++seat) {
            ASSERT_EQ(game.GetHand(seat).size(), 3u);
            for (const String& card : game.GetHand(seat)) {
                all.push_back(card);
            }
        }
        eastl::sort(all.begin(), all.end());
        EXPECT_EQ(eastl::unique(all.begin(), all.end()), all.end());
        EXPECT_EQ(all.size(), 12u);
        EXPECT_EQ(game.GetSeatCount(), 4);
    }

    TEST(SeatedGameTest, StartIsDeterministicFromTheSeed) {
        FakeSeatedGame first;
        FakeSeatedGame second;
        FakeSeatedGame other;
        ASSERT_TRUE(first.Start(42, Json::object()).has_value());
        ASSERT_TRUE(second.Start(42, Json::object()).has_value());
        EXPECT_EQ(first.GetTurn(), second.GetTurn());
        for (int seat = 0; seat < FakeSeatedGame::kSeatCount; ++seat) {
            EXPECT_EQ(first.GetHand(seat), second.GetHand(seat));
        }
        bool differs = false;
        for (uint64_t seed = 1; seed < 20 && !differs; ++seed) {
            ASSERT_TRUE(other.Start(seed, Json::object()).has_value());
            for (int seat = 0; seat < FakeSeatedGame::kSeatCount; ++seat) {
                differs = differs || other.GetHand(seat) != first.GetHand(seat);
            }
        }
        EXPECT_TRUE(differs) << "the seed does not influence the deal";
    }

    TEST(SeatedGameTest, StartRejectsBadOptionsAndChangesNothing) {
        FakeSeatedGame game;
        for (const char* text : {R"({"firstSeat":4})", R"({"firstSeat":-1})", R"({"firstSeat":"1"})", R"({"firstSeat":1.5})", R"({"firstSeat":null})"}) {
            const auto options = ParseJson(text);
            ASSERT_TRUE(options.has_value());
            const auto result = game.Start(1, *options);
            EXPECT_EQ(ErrorCode(result), ControlErrors::kInvalidParams.data()) << text;
        }
        EXPECT_EQ(game.GetStateVersion(), 0u);
        EXPECT_EQ(game.GetTurn(), -1);
    }

    TEST(SeatedGameTest, ActionsAreEmptyOffTurnAndListTheHandOnTurn) {
        FakeSeatedGame game = MakeStartedGame();
        for (int seat = 0; seat < FakeSeatedGame::kSeatCount; ++seat) {
            const auto actions = game.GetSeatActions(seat);
            ASSERT_TRUE(actions.has_value());
            const Json listed = ActionsJson(**actions);
            ASSERT_TRUE(listed.is_array());
            EXPECT_EQ(listed.size(), seat == 0 ? 3u : 0u) << "seat " << seat;
        }
    }

    TEST(SeatedGameTest, EveryListedActionIsAccepted) {
        for (int index = 0; index < 3; ++index) {
            FakeSeatedGame game = MakeStartedGame();
            const auto actions = game.GetSeatActions(0);
            ASSERT_TRUE(actions.has_value());
            const Json listed = ActionsJson(**actions);
            ASSERT_EQ(listed.size(), 3u);
            const auto outcome = game.PerformSeatAction(0, listed[static_cast<size_t>(index)]);
            ASSERT_TRUE(outcome.has_value());
            EXPECT_EQ(GetJsonInt(OutcomeJson(**outcome), "seat"), 0);
        }
    }

    TEST(SeatedGameTest, PlayOffTurnIsNotYourTurn) {
        FakeSeatedGame game = MakeStartedGame();
        const auto result = game.PerformSeatAction(2, CardAction(game.GetHand(2).front()));
        EXPECT_EQ(ErrorCode(result), SeatedGameErrors::kNotYourTurn.data());
        EXPECT_EQ(game.GetStateVersion(), 1u);
        EXPECT_EQ(game.GetHand(2).size(), 3u);
    }

    TEST(SeatedGameTest, ACardOfAnotherSeatIsIllegal) {
        FakeSeatedGame game = MakeStartedGame();
        const auto result = game.PerformSeatAction(0, CardAction(game.GetHand(1).front()));
        EXPECT_EQ(ErrorCode(result), GameControlErrors::kIllegalAction.data());
        EXPECT_EQ(game.GetHand(1).size(), 3u);
        EXPECT_EQ(game.GetHand(0).size(), 3u);
        EXPECT_EQ(game.GetPlayCount(), 0);
    }

    TEST(SeatedGameTest, UnknownCardAndUnknownTypeAreIllegal) {
        FakeSeatedGame game = MakeStartedGame();
        EXPECT_EQ(ErrorCode(game.PerformSeatAction(0, CardAction("c9_9"))), "illegal_action");
        Json discard = CardAction(game.GetHand(0).front());
        discard["type"] = "discard";
        EXPECT_EQ(ErrorCode(game.PerformSeatAction(0, discard)), "illegal_action");
        EXPECT_EQ(game.GetPlayCount(), 0);
    }

    TEST(SeatedGameTest, MalformedActionsAreInvalidParams) {
        FakeSeatedGame game = MakeStartedGame();
        for (const char* text : {"{}", R"({"type":"play"})", R"({"card":"c0_0"})", R"({"type":1,"card":"c0_0"})", R"({"type":"play","card":5})", R"({"type":"play","card":null})", "[]", "7", "null"}) {
            const auto action = ParseJson(text);
            ASSERT_TRUE(action.has_value()) << text;
            EXPECT_EQ(ErrorCode(game.PerformSeatAction(0, *action)), "invalid_params") << text;
        }
        EXPECT_EQ(game.GetPlayCount(), 0);
    }

    TEST(SeatedGameTest, SeatsOutOfRangeAreInvalidSeat) {
        FakeSeatedGame game = MakeStartedGame();
        for (const int seat : {-1, 4, 100, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
            EXPECT_EQ(ErrorCode(game.GetSeatView(seat)), "invalid_seat") << seat;
            EXPECT_EQ(ErrorCode(game.GetSeatActions(seat)), "invalid_seat") << seat;
            EXPECT_EQ(ErrorCode(game.PerformSeatAction(seat, CardAction("c0_0"))), "invalid_seat") << seat;
            EXPECT_FALSE(game.ChooseComputerAction(seat).has_value()) << seat;
        }
        EXPECT_EQ(game.GetStateVersion(), 1u);
    }

    TEST(SeatedGameTest, VersionGrowsPerPlayAndNeverGoesBack) {
        FakeSeatedGame game;
        EXPECT_EQ(game.GetStateVersion(), 0u);
        ASSERT_TRUE(game.Start(3, Json::object()).has_value());
        uint64_t previous = game.GetStateVersion();
        EXPECT_EQ(previous, 1u);
        while (!game.IsFinished()) {
            const int seat = game.GetTurn();
            EXPECT_FALSE(game.PerformSeatAction((seat + 1) % 4, CardAction("c0_0")).has_value());
            EXPECT_EQ(game.GetStateVersion(), previous);
            ASSERT_TRUE(game.PerformSeatAction(seat, *game.ChooseComputerAction(seat)).has_value());
            EXPECT_EQ(game.GetStateVersion(), previous + 1);
            previous = game.GetStateVersion();
        }
        // a second Start is a new game and still goes forward
        ASSERT_TRUE(game.Start(3, Json::object()).has_value());
        EXPECT_EQ(game.GetStateVersion(), previous + 1);
        EXPECT_FALSE(game.IsFinished());
    }

    TEST(SeatedGameTest, ComputerActionOnlyOnItsTurnAndAcceptedAsListed) {
        FakeSeatedGame game = MakeStartedGame();
        for (int seat = 1; seat < FakeSeatedGame::kSeatCount; ++seat) {
            EXPECT_FALSE(game.ChooseComputerAction(seat).has_value());
        }
        const auto chosen = game.ChooseComputerAction(0);
        ASSERT_TRUE(chosen.has_value());
        const auto actions = game.GetSeatActions(0);
        ASSERT_TRUE(actions.has_value());
        const Json listed = ActionsJson(**actions);
        EXPECT_NE(std::find(listed.begin(), listed.end(), *chosen), listed.end());
        Vector<String> hand = game.GetHand(0);
        eastl::sort(hand.begin(), hand.end());
        EXPECT_EQ(GetJsonString(*chosen, "card"), hand.front());
        EXPECT_EQ(game.GetPlayCount(), 0);
        EXPECT_TRUE(game.PerformSeatAction(0, *chosen).has_value());
        EXPECT_FALSE(game.ChooseComputerAction(0).has_value());
    }

    TEST(SeatedGameTest, FinishesAfterTwelvePlaysAndNobodyActsAfterwards) {
        FakeSeatedGame game = MakeStartedGame();
        int plays = 0;
        while (!game.IsFinished()) {
            const int seat = game.GetTurn();
            ASSERT_TRUE(game.PerformSeatAction(seat, *game.ChooseComputerAction(seat)).has_value());
            ++plays;
            ASSERT_LE(plays, 12);
        }
        EXPECT_EQ(plays, 12);
        EXPECT_EQ(game.GetTurn(), -1);
        for (int seat = 0; seat < FakeSeatedGame::kSeatCount; ++seat) {
            const auto actions = game.GetSeatActions(seat);
            ASSERT_TRUE(actions.has_value());
            EXPECT_EQ((*actions)->Count(), 0u);
            EXPECT_FALSE(game.ChooseComputerAction(seat).has_value());
            EXPECT_EQ(ErrorCode(game.PerformSeatAction(seat, CardAction("c0_0"))), "not_your_turn");
            const auto view = game.GetSeatView(seat);
            ASSERT_TRUE(view.has_value());
            const Json viewJson = ViewJson(**view);
            EXPECT_EQ(GetJsonBool(viewJson, "finished"), true);
            const Json* table = FindJsonMember(viewJson, "table");
            ASSERT_NE(table, nullptr);
            EXPECT_EQ(table->size(), 12u) << "all cards played are public";
        }
    }

    TEST(SeatedGameTest, EachSeatPlaysItsThreeCards) {
        FakeSeatedGame game = MakeStartedGame();
        PlayToEnd(game);
        for (int seat = 0; seat < FakeSeatedGame::kSeatCount; ++seat) {
            EXPECT_TRUE(game.GetHand(seat).empty());
        }
    }

    TEST(SeatedGameTest, ViewHoldsOwnCardsCountsAndTableOnly) {
        FakeSeatedGame game = MakeStartedGame();
        ASSERT_TRUE(game.PerformSeatAction(0, *game.ChooseComputerAction(0)).has_value());
        const auto answer = game.GetSeatView(2);
        ASSERT_TRUE(answer.has_value());
        const Json view = ViewJson(**answer);
        ASSERT_TRUE(view.is_object());
        Vector<String> keys;
        for (const auto& item : view.items()) {
            keys.push_back(item.key().c_str());
        }
        eastl::sort(keys.begin(), keys.end());
        const Vector<String> expected = {"cardCounts", "finished", "hand", "seat", "table", "turn"};
        EXPECT_EQ(keys, expected);
        EXPECT_EQ(Require(FindJsonMember(view, "hand")).size(), 3u);
        const Json* counts = FindJsonMember(view, "cardCounts");
        ASSERT_NE(counts, nullptr);
        EXPECT_EQ(*counts, Json::array({2, 3, 3, 3}));
        EXPECT_EQ(GetJsonInt(view, "turn"), 1);
    }

    TEST(SeatedGameTest, NoSeatViewLeaksAtAnyStep) {
        FakeSeatedGame game = MakeStartedGame();
        while (true) {
            for (int seat = 0; seat < FakeSeatedGame::kSeatCount; ++seat) {
                ExpectNoLeak(Dump(game.GetSeatView(seat)), seat, game);
                ExpectNoLeak(Dump(game.GetSeatActions(seat)), seat, game);
            }
            if (game.IsFinished()) {
                break;
            }
            const int seat = game.GetTurn();
            const auto outcome = game.PerformSeatAction(seat, *game.ChooseComputerAction(seat));
            ASSERT_TRUE(outcome.has_value());
            for (int other = 0; other < FakeSeatedGame::kSeatCount; ++other) {
                ExpectNoLeak(Dump(outcome), other, game);
            }
        }
    }

    TEST(SeatedGameTest, LeakCheckerCatchesTheLeakyGame) {
        LeakyFakeSeatedGame leaky;
        ASSERT_TRUE(leaky.Start(5, Json::object()).has_value());
        const String text = Dump(leaky.GetSeatView(1));
        const auto leaked = FindLeakedCards(text, 1, leaky);
        EXPECT_EQ(leaked.size(), 9u) << "the cards of the other three seats";
        EXPECT_NONFATAL_FAILURE(ExpectNoLeak(text, 1, leaky), "hidden cards");

        FakeSeatedGame honest;
        ASSERT_TRUE(honest.Start(5, Json::object()).has_value());
        const String honestText = Dump(honest.GetSeatView(1));
        EXPECT_TRUE(FindLeakedCards(honestText, 1, honest).empty());
        ExpectNoLeak(honestText, 1, honest);
    }

    TEST(SeatedGameTest, LeakCheckerDoesNotMatchLongerIdsOrOwnCards) {
        FakeSeatedGame game = MakeStartedGame();
        EXPECT_TRUE(FindLeakedCards("c1_20 c1_9 c9_1", 0, game).empty());
        EXPECT_EQ(FindLeakedCards("c1_2", 0, game).size(), 1u);
        EXPECT_TRUE(FindLeakedCards("c0_1 c0_2", 0, game).empty()) << "a seat may name its own cards";
        EXPECT_EQ(FindLeakedCards("c1_2 c1_2 c1_2", 0, game).size(), 1u);
    }

    TEST(SeatedGameTest, TypedAnswersWriteTheBytesOfTheOldJsonAtEveryPoint) {
        FakeSeatedGame game = MakeStartedGame();
        PlayChecked(game, [&game](const Vector<PlayedCard>& table) {
            for (int seat = 0; seat < FakeSeatedGame::kSeatCount; ++seat) {
                const auto view = game.GetSeatView(seat);
                ASSERT_TRUE(view.has_value());
                EXPECT_EQ(DumpJson(ViewJson(**view)), DumpJson(ReferenceView(game, seat, table))) << "seat " << seat << ", plays " << table.size();
                const auto actions = game.GetSeatActions(seat);
                ASSERT_TRUE(actions.has_value());
                EXPECT_EQ(DumpJson(ActionsJson(**actions)), DumpJson(ReferenceActions(game, seat))) << "seat " << seat << ", plays " << table.size();
            }
        });
        EXPECT_TRUE(game.IsFinished());
    }

    TEST(SeatedGameTest, AViewMessageOfTheHubEqualsTheOneOfTheJsonWriter) {
        HubHarness harness;
        const auto ids = harness.MakeLobby(FakeSeatedGame::kSeatCount);
        harness.Start(ids[0]);
        const FakeSeatedGame* game = harness.GetGame(harness.Client(ids[0]).roomCode);
        ASSERT_NE(game, nullptr);
        Vector<PlayedCard> table;
        // The first message, then the ones after plays until the end (the hub sends a `view` after every committed action)
        for (int step = 0; step <= FakeSeatedGame::kTotalPlays; ++step) {
            for (const ConnectionId id : ids) {
                const TestClient& client = harness.Client(id);
                const ReceivedMessage* message = client.Last(RoomMessageTypes::kView);
                ASSERT_NE(message, nullptr) << "step " << step;
                const uint64_t version = game->GetStateVersion();
                EXPECT_EQ(message->text, MakeViewMessage(client.roomCode, client.seat, version, game->IsFinished(), DumpJson(ReferenceView(*game, client.seat, table)),
                                                         DumpJson(ReferenceActions(*game, client.seat))))
                    << "step " << step << ", seat " << client.seat;
            }
            if (game->IsFinished()) {
                break;
            }
            const int turn = game->GetTurn();
            const auto card = game->GetHand(turn).front();
            const ConnectionId player = *std::find_if(ids.begin(), ids.end(), [&](ConnectionId id) { return harness.Client(id).seat == turn; });
            ASSERT_TRUE(harness.PlayFirstAction(player));
            // PlayFirstAction sends the first listed action, which is the first card of the hand
            table.push_back(PlayedCard{turn, card});
        }
        EXPECT_TRUE(game->IsFinished());
    }

    TEST(SeatedGameTest, EverySeatViewHoldsOnlyItsOwnCardsCountsAndPlayedCards) {
        FakeSeatedGame game = MakeStartedGame(11);
        PlayChecked(game, [&game](const Vector<PlayedCard>& table) {
            for (int seat = 0; seat < FakeSeatedGame::kSeatCount; ++seat) {
                const auto answer = game.GetSeatView(seat);
                ASSERT_TRUE(answer.has_value());
                const auto& view = static_cast<const DemoSeatView&>(**answer);
                EXPECT_EQ(view.hand, game.GetHand(seat)) << "seat " << seat;
                for (int other = 0; other < FakeSeatedGame::kSeatCount; ++other) {
                    if (other == seat) {
                        continue;
                    }
                    for (const String& card : game.GetHand(other)) {
                        EXPECT_FALSE(Holds(view.hand, card)) << card.c_str() << " of seat " << other << " is in the hand of seat " << seat;
                        for (const DemoTablePlay& play : view.table) {
                            EXPECT_NE(play.card, card) << "a card still in a hand is on the table";
                        }
                    }
                }
                ASSERT_EQ(view.cardCounts.size(), static_cast<size_t>(FakeSeatedGame::kSeatCount));
                for (int other = 0; other < FakeSeatedGame::kSeatCount; ++other) {
                    EXPECT_EQ(view.cardCounts[static_cast<size_t>(other)], static_cast<int32_t>(game.GetHand(other).size())) << "count of seat " << other;
                }
                ASSERT_EQ(view.table.size(), table.size());
                for (size_t i = 0; i < table.size(); ++i) {
                    EXPECT_EQ(view.table[i].card, table[i].card);
                    EXPECT_EQ(view.table[i].seat, table[i].seat);
                }
                ExpectNoLeak(Dump(answer), seat, game);
            }
        });
    }

    TEST(SeatedGameTest, TheLeakyGamesTypedViewIsCaughtByTheLeakCheck) {
        LeakyFakeSeatedGame leaky;
        ASSERT_TRUE(leaky.Start(5, Json::object()).has_value());
        const auto answer = leaky.GetSeatView(1);
        ASSERT_TRUE(answer.has_value());
        const auto& view = static_cast<const LeakyDemoSeatView&>(**answer);
        ASSERT_EQ(view.allHands.size(), static_cast<size_t>(FakeSeatedGame::kSeatCount));
        EXPECT_EQ(view.allHands[2].cards, leaky.GetHand(2));
        EXPECT_FALSE(FindHiddenCards(Dump(answer), 1, leaky).empty());

        // Through the hub the same game is flagged by the judge of every message (counted, not failed)
        HubHarness harness(HubHarness::MakeSettings(), true);
        harness.SetFailOnLeak(false);
        const auto ids = harness.MakeLobby(2);
        harness.Start(ids[0]);
        EXPECT_GT(harness.GetLeakCount(), 0u);
    }

    TEST(SeatedGameTest, TheErrorsOfTheTypedAnswersKeepTheirCodes) {
        FakeSeatedGame game = MakeStartedGame();
        EXPECT_EQ(ErrorCode(game.GetSeatView(FakeSeatedGame::kSeatCount)), SeatedGameErrors::kInvalidSeat.data());
        EXPECT_EQ(ErrorCode(game.GetSeatActions(-1)), SeatedGameErrors::kInvalidSeat.data());
        EXPECT_EQ(ErrorCode(game.PerformSeatAction(-1, CardAction("c0_0"))), SeatedGameErrors::kInvalidSeat.data());
        EXPECT_EQ(ErrorCode(game.PerformSeatAction(1, CardAction("c1_0"))), SeatedGameErrors::kNotYourTurn.data());
        EXPECT_EQ(ErrorCode(game.PerformSeatAction(0, CardAction("c3_0"))), GameControlErrors::kIllegalAction.data());
        EXPECT_EQ(ErrorCode(game.PerformSeatAction(0, Json::object())), ControlErrors::kInvalidParams.data());
        EXPECT_EQ(game.GetPlayCount(), 0);
    }

    TEST(SeatedGameRegistryTest, UnknownNameFindsAndCreatesNothing) {
        SeatedGameRegistry registry;
        EXPECT_EQ(registry.GetCount(), 0u);
        EXPECT_FALSE(registry.Find("Nope").has_value());
        EXPECT_TRUE(registry.Create("Nope") == nullptr);
        EXPECT_TRUE(registry.Create("") == nullptr);
    }

    TEST(SeatedGameRegistryTest, RegisterFindCreate) {
        SeatedGameRegistry registry;
        ASSERT_TRUE(registry.Register({"Fake", 4}, [] { return MakeUnique<FakeSeatedGame>(); }));
        const auto info = registry.Find("Fake");
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->name, "Fake");
        EXPECT_EQ(info->seatCount, 4);
        const auto game = registry.Create("Fake");
        ASSERT_TRUE(game != nullptr);
        EXPECT_EQ(game->GetSeatCount(), 4);
        EXPECT_FALSE(registry.Find("fake").has_value()) << "names are case-sensitive";
    }

    TEST(SeatedGameRegistryTest, DuplicateRegistrationReplaces) {
        SeatedGameRegistry registry;
        ASSERT_TRUE(registry.Register({"G", 4}, [] { return MakeUnique<FakeSeatedGame>(); }));
        ASSERT_TRUE(registry.Register({"G", 3}, [] { return MakeUnique<LeakyFakeSeatedGame>(); }));
        EXPECT_EQ(registry.GetCount(), 1u);
        EXPECT_EQ(Require(registry.Find("G")).seatCount, 3);
        const auto game = registry.Create("G");
        ASSERT_TRUE(game != nullptr);
        EXPECT_NE(dynamic_cast<LeakyFakeSeatedGame*>(game.get()), nullptr);
    }

    TEST(SeatedGameRegistryTest, RefusesAnInvalidRegistration) {
        SeatedGameRegistry registry;
        const auto makeFactory = [] { return SeatedGameFactory([] { return MakeUnique<FakeSeatedGame>(); }); };
        EXPECT_FALSE(registry.Register({"", 4}, makeFactory()));
        EXPECT_FALSE(registry.Register({"G", 0}, makeFactory()));
        EXPECT_FALSE(registry.Register({"G", -2}, makeFactory()));
        EXPECT_FALSE(registry.Register({"G", 4}, SeatedGameFactory()));
        EXPECT_EQ(registry.GetCount(), 0u);
        ASSERT_TRUE(registry.Register({"G", 4}, makeFactory()));
        EXPECT_FALSE(registry.Register({"G", 0}, makeFactory())) << "a refused registration keeps the old one";
        EXPECT_EQ(Require(registry.Find("G")).seatCount, 4);
    }

    TEST(SeatedGameRegistryTest, AFactoryThatOwnsAMoveOnlyCaptureMakesGamesAndIsDestroyedOnceWithTheRegistry) {
        struct Counts {
            int destroyed = 0;
        };
        struct Sentinel {
            explicit Sentinel(Counts& target) : counts(&target) {}
            Sentinel(Sentinel&& other) noexcept : counts(std::exchange(other.counts, nullptr)) {}
            Sentinel(const Sentinel&) = delete;
            ~Sentinel() {
                if (counts != nullptr) {
                    ++counts->destroyed;
                }
            }
            Counts* counts;
        };
        Counts counts;
        {
            SeatedGameRegistry registry;
            ASSERT_TRUE(registry.Register({"Owned", 4}, [made = MakeUnique<int>(0), sentinel = Sentinel(counts)]() mutable -> UniquePtr<ISeatedGame> {
                ++*made;
                return MakeUnique<FakeSeatedGame>();
            }));
            EXPECT_NE(registry.Create("Owned"), nullptr);
            EXPECT_NE(registry.Create("Owned"), nullptr);
            EXPECT_EQ(counts.destroyed, 0);
        }
        EXPECT_EQ(counts.destroyed, 1);
    }

    TEST(SeatedGameRegistryTest, CreateMakesIndependentInstances) {
        SeatedGameRegistry registry;
        ASSERT_TRUE(registry.Register({"Fake", 4}, [] { return MakeUnique<FakeSeatedGame>(); }));
        auto first = registry.Create("Fake");
        auto second = registry.Create("Fake");
        ASSERT_TRUE(first != nullptr);
        ASSERT_TRUE(second != nullptr);
        EXPECT_NE(first.get(), second.get());
        ASSERT_TRUE(first->Start(1, Json::object()).has_value());
        EXPECT_EQ(first->GetStateVersion(), 1u);
        EXPECT_EQ(second->GetStateVersion(), 0u);
    }

    TEST(SeatedGameRegistryTest, ClientNamesAreNotInterned) {
        SeatedGameRegistry registry;
        ASSERT_TRUE(registry.Register({"Fake", 4}, [] { return MakeUnique<FakeSeatedGame>(); }));
        const String name = "NeverInternedGameName_5d1f";
        EXPECT_FALSE(PoolString::Find(name.c_str()).has_value());
        EXPECT_FALSE(registry.Find(name).has_value());
        EXPECT_TRUE(registry.Create(name) == nullptr);
        EXPECT_FALSE(PoolString::Find(name.c_str()).has_value());
    }

}  // namespace BECore::Tests
