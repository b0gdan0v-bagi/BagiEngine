#include "FakeSeatedGame.h"

#include <Common/Printers.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Games/SeatedGameRegistry.h>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        Json PlayAction(const String& card) {
            Json action = Json::object();
            action["type"] = "play";
            action["card"] = card.c_str();
            return action;
        }

        String Dump(const std::expected<Json, ControlError>& value) {
            return value ? DumpJson(*value) : String("<error>");
        }

        template <typename T>
        std::string ErrorCode(const std::expected<T, ControlError>& value) {
            return value ? std::string() : std::string(value.error().code.c_str());
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
        eastl::vector<String> all;
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
            ASSERT_TRUE(actions->is_array());
            EXPECT_EQ(actions->size(), seat == 0 ? 3u : 0u) << "seat " << seat;
        }
    }

    TEST(SeatedGameTest, EveryListedActionIsAccepted) {
        for (int index = 0; index < 3; ++index) {
            FakeSeatedGame game = MakeStartedGame();
            const auto actions = game.GetSeatActions(0);
            ASSERT_TRUE(actions.has_value());
            ASSERT_EQ(actions->size(), 3u);
            const auto outcome = game.PerformSeatAction(0, (*actions)[static_cast<size_t>(index)]);
            ASSERT_TRUE(outcome.has_value());
            EXPECT_EQ(GetJsonInt(*outcome, "seat"), 0);
        }
    }

    TEST(SeatedGameTest, PlayOffTurnIsNotYourTurn) {
        FakeSeatedGame game = MakeStartedGame();
        const auto result = game.PerformSeatAction(2, PlayAction(game.GetHand(2).front()));
        EXPECT_EQ(ErrorCode(result), SeatedGameErrors::kNotYourTurn.data());
        EXPECT_EQ(game.GetStateVersion(), 1u);
        EXPECT_EQ(game.GetHand(2).size(), 3u);
    }

    TEST(SeatedGameTest, ACardOfAnotherSeatIsIllegal) {
        FakeSeatedGame game = MakeStartedGame();
        const auto result = game.PerformSeatAction(0, PlayAction(game.GetHand(1).front()));
        EXPECT_EQ(ErrorCode(result), GameControlErrors::kIllegalAction.data());
        EXPECT_EQ(game.GetHand(1).size(), 3u);
        EXPECT_EQ(game.GetHand(0).size(), 3u);
        EXPECT_EQ(game.GetPlayCount(), 0);
    }

    TEST(SeatedGameTest, UnknownCardAndUnknownTypeAreIllegal) {
        FakeSeatedGame game = MakeStartedGame();
        EXPECT_EQ(ErrorCode(game.PerformSeatAction(0, PlayAction("c9_9"))), "illegal_action");
        Json discard = PlayAction(game.GetHand(0).front());
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
            EXPECT_EQ(ErrorCode(game.PerformSeatAction(seat, PlayAction("c0_0"))), "invalid_seat") << seat;
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
            // an error does not move it
            EXPECT_FALSE(game.PerformSeatAction((seat + 1) % 4, PlayAction("c0_0")).has_value());
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
        EXPECT_NE(std::find(actions->begin(), actions->end(), *chosen), actions->end());
        // the lowest card id of the hand
        eastl::vector<String> hand = game.GetHand(0);
        eastl::sort(hand.begin(), hand.end());
        EXPECT_EQ(GetJsonString(*chosen, "card"), hand.front());
        // choosing does not play
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
            EXPECT_TRUE(actions->empty());
            EXPECT_FALSE(game.ChooseComputerAction(seat).has_value());
            EXPECT_EQ(ErrorCode(game.PerformSeatAction(seat, PlayAction("c0_0"))), "not_your_turn");
            const auto view = game.GetSeatView(seat);
            ASSERT_TRUE(view.has_value());
            EXPECT_EQ(GetJsonBool(*view, "finished"), true);
            const Json* table = FindJsonMember(*view, "table");
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
        const auto view = game.GetSeatView(2);
        ASSERT_TRUE(view.has_value());
        ASSERT_TRUE(view->is_object());
        eastl::vector<String> keys;
        for (const auto& item : view->items()) {
            keys.push_back(item.key().c_str());
        }
        eastl::sort(keys.begin(), keys.end());
        const eastl::vector<String> expected = {"cardCounts", "finished", "hand", "seat", "table", "turn"};
        EXPECT_EQ(keys, expected);
        EXPECT_EQ(FindJsonMember(*view, "hand")->size(), 3u);
        const Json* counts = FindJsonMember(*view, "cardCounts");
        ASSERT_NE(counts, nullptr);
        EXPECT_EQ(*counts, Json::array({2, 3, 3, 3}));
        EXPECT_EQ(GetJsonInt(*view, "turn"), 1);
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
                ExpectNoLeak(DumpJson(*outcome), other, game);
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

        // the same check passes the honest game in the same position
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

    TEST(SeatedGameRegistryTest, UnknownNameFindsAndCreatesNothing) {
        SeatedGameRegistry registry;
        EXPECT_EQ(registry.GetCount(), 0u);
        EXPECT_FALSE(registry.Find("Nope").has_value());
        EXPECT_TRUE(registry.Create("Nope") == nullptr);
        EXPECT_TRUE(registry.Create("") == nullptr);
    }

    TEST(SeatedGameRegistryTest, RegisterFindCreate) {
        SeatedGameRegistry registry;
        ASSERT_TRUE(registry.Register({"Fake", 4}, [] { return eastl::make_unique<FakeSeatedGame>(); }));
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
        ASSERT_TRUE(registry.Register({"G", 4}, [] { return eastl::make_unique<FakeSeatedGame>(); }));
        ASSERT_TRUE(registry.Register({"G", 3}, [] { return eastl::make_unique<LeakyFakeSeatedGame>(); }));
        EXPECT_EQ(registry.GetCount(), 1u);
        EXPECT_EQ(registry.Find("G")->seatCount, 3);
        const auto game = registry.Create("G");
        ASSERT_TRUE(game != nullptr);
        EXPECT_NE(dynamic_cast<LeakyFakeSeatedGame*>(game.get()), nullptr);
    }

    TEST(SeatedGameRegistryTest, RefusesAnInvalidRegistration) {
        SeatedGameRegistry registry;
        const SeatedGameFactory factory = [] { return eastl::make_unique<FakeSeatedGame>(); };
        EXPECT_FALSE(registry.Register({"", 4}, factory));
        EXPECT_FALSE(registry.Register({"G", 0}, factory));
        EXPECT_FALSE(registry.Register({"G", -2}, factory));
        EXPECT_FALSE(registry.Register({"G", 4}, SeatedGameFactory()));
        EXPECT_EQ(registry.GetCount(), 0u);
        ASSERT_TRUE(registry.Register({"G", 4}, factory));
        EXPECT_FALSE(registry.Register({"G", 0}, factory)) << "a refused registration keeps the old one";
        EXPECT_EQ(registry.Find("G")->seatCount, 4);
    }

    TEST(SeatedGameRegistryTest, CreateMakesIndependentInstances) {
        SeatedGameRegistry registry;
        ASSERT_TRUE(registry.Register({"Fake", 4}, [] { return eastl::make_unique<FakeSeatedGame>(); }));
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
        ASSERT_TRUE(registry.Register({"Fake", 4}, [] { return eastl::make_unique<FakeSeatedGame>(); }));
        const String name = "NeverInternedGameName_5d1f";
        EXPECT_FALSE(PoolString::Find(name.c_str()).has_value());
        EXPECT_FALSE(registry.Find(name).has_value());
        EXPECT_TRUE(registry.Create(name) == nullptr);
        EXPECT_FALSE(PoolString::Find(name.c_str()).has_value());
    }

}  // namespace BECore::Tests
