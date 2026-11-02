#include <Match3/Match3GameControl.h>
#include <gtest/gtest.h>

/**
 * Coverage of Match3GameControl: the state, the actions and the way to act, on a model that the test owns.
 * The adapter needs no scene: it gets the model from a provider.
 */
namespace BECore::Match3::Tests {

    namespace {

        constexpr uint32_t kDefaultSeed = 99;
        constexpr char kValidLetters[] = ".ROYGBP";

        std::string ErrorCodeOf(const ControlError& error) {
            return std::string(error.code.c_str(), error.code.size());
        }

        Json SwapAction(int64_t fromCol, int64_t fromRow, int64_t toCol, int64_t toRow) {
            Json from = Json::object();
            from["col"] = fromCol;
            from["row"] = fromRow;
            Json to = Json::object();
            to["col"] = toCol;
            to["row"] = toRow;
            Json action = Json::object();
            action["type"] = "swap";
            action["from"] = std::move(from);
            action["to"] = std::move(to);
            return action;
        }

        class Match3GameControlTest : public ::testing::Test {
        protected:
            Match3GameControlTest()
                : control([this] {
                      Match3ControlTarget target{active ? &model : nullptr, kDefaultSeed};
                      target.enterGame = [this] { ++enterGameCalls; };
                      return target;
                  }) {}

            void SetUp() override {
                model.Reset(42);
            }

            /// The state as the JSON object that GetState() returned; fails the test when it is an error
            Json State() {
                auto state = control.GetState();
                EXPECT_TRUE(state.has_value());
                return state ? *state : Json();
            }

            Json Actions() {
                auto actions = control.GetActions();
                EXPECT_TRUE(actions.has_value());
                return actions ? *actions : Json();
            }

            bool active = true;
            int enterGameCalls = 0;
            Match3GameModel model;
            Match3GameControl control;
        };

    }  // namespace

    TEST_F(Match3GameControlTest, AControllerThatPlaysEntersTheGame) {
        // Reading does not
        State();
        Actions();
        EXPECT_EQ(enterGameCalls, 0);

        // A reset does, and so does an action that went through
        ASSERT_TRUE(control.Reset(Json::object()).has_value());
        EXPECT_EQ(enterGameCalls, 1);
        const auto swaps = model.GetLegalSwaps();
        ASSERT_FALSE(swaps.empty());
        const auto& swap = swaps.front();
        ASSERT_TRUE(control.PerformAction(SwapAction(swap.first.col, swap.first.row, swap.second.col, swap.second.row)).has_value());
        EXPECT_EQ(enterGameCalls, 2);

        // A refused call does not: nothing was played
        EXPECT_FALSE(control.PerformAction(SwapAction(0, 0, 5, 5)).has_value());
        Json badSeed = Json::object();
        badSeed["seed"] = -1;
        EXPECT_FALSE(control.Reset(badSeed).has_value());
        EXPECT_EQ(enterGameCalls, 2);
    }

    TEST_F(Match3GameControlTest, WithoutABoardEveryMethodAnswersNoActiveModel) {
        active = false;

        EXPECT_EQ(ErrorCodeOf(control.GetState().error()), "no_active_model");
        EXPECT_EQ(ErrorCodeOf(control.GetActions().error()), "no_active_model");
        EXPECT_EQ(ErrorCodeOf(control.PerformAction(SwapAction(0, 0, 1, 0)).error()), "no_active_model");
        EXPECT_EQ(ErrorCodeOf(control.Reset(Json::object()).error()), "no_active_model");
        EXPECT_EQ(control.GetStateVersion(), 0u);
    }

    TEST_F(Match3GameControlTest, StateDescribesTheBoard) {
        const Json state = State();

        EXPECT_EQ(GetJsonInt(state, "size"), 8);
        EXPECT_EQ(GetJsonInt(state, "score"), 0);
        EXPECT_EQ(GetJsonInt(state, "seed"), 42);
        EXPECT_EQ(GetJsonString(state, "status").value_or("").c_str(), std::string("playing"));

        const Json* board = FindJsonMember(state, "board");
        ASSERT_NE(board, nullptr);
        ASSERT_TRUE(board->is_array());
        ASSERT_EQ(board->size(), 8u);
        for (size_t row = 0; row < board->size(); ++row) {
            ASSERT_TRUE((*board)[row].is_string());
            const std::string& line = (*board)[row].get_ref<const std::string&>();
            ASSERT_EQ(line.size(), 8u) << "row " << row;
            for (size_t col = 0; col < line.size(); ++col) {
                const char letter = line[col];
                EXPECT_NE(std::string(kValidLetters).find(letter), std::string::npos) << "row " << row << " col " << col;
                // Row 0 is the top and column 0 the left, the order of the model
                const GemType gem = model.GetCellAt(Cell{static_cast<int8_t>(col), static_cast<int8_t>(row)});
                EXPECT_EQ(letter, kValidLetters[static_cast<size_t>(gem)]);
            }
        }
    }

    TEST_F(Match3GameControlTest, StateCountsTheLegalSwaps) {
        const Json state = State();

        EXPECT_EQ(GetJsonInt(state, "legalSwapCount"), static_cast<int64_t>(model.GetLegalSwaps().size()));
        EXPECT_EQ(static_cast<size_t>(GetJsonInt(state, "legalSwapCount").value_or(-1)), Actions().size());
    }

    TEST_F(Match3GameControlTest, ActionsListEveryLegalSwapOnce) {
        const Json actions = Actions();
        const auto legal = model.GetLegalSwaps();

        ASSERT_TRUE(actions.is_array());
        ASSERT_EQ(actions.size(), legal.size());
        for (size_t index = 0; index < legal.size(); ++index) {
            const Json& action = actions[index];
            EXPECT_EQ(GetJsonString(action, "type").value_or("").c_str(), std::string("swap"));
            const Json* from = FindJsonMember(action, "from");
            const Json* to = FindJsonMember(action, "to");
            ASSERT_TRUE(from != nullptr && to != nullptr);
            EXPECT_EQ(GetJsonInt(*from, "col"), legal[index].first.col);
            EXPECT_EQ(GetJsonInt(*from, "row"), legal[index].first.row);
            EXPECT_EQ(GetJsonInt(*to, "col"), legal[index].second.col);
            EXPECT_EQ(GetJsonInt(*to, "row"), legal[index].second.row);
        }
    }

    TEST_F(Match3GameControlTest, EveryListedActionIsAcceptedAsItIs) {
        const Json actions = Actions();
        ASSERT_FALSE(actions.empty());

        for (size_t index = 0; index < actions.size(); ++index) {
            model.Reset(42);  // each action on the same fresh board
            const auto outcome = control.PerformAction(actions[index]);

            ASSERT_TRUE(outcome.has_value()) << "action " << index << ": " << (outcome ? "" : outcome.error().message.c_str());
            EXPECT_GT(GetJsonInt(*outcome, "scoreGained").value_or(0), 0) << "action " << index;
            EXPECT_EQ(GetJsonInt(*outcome, "score"), model.GetScore());
        }
    }

    TEST_F(Match3GameControlTest, ASwapMayNameTheCellsInEitherOrder) {
        const auto legal = model.GetLegalSwaps();
        ASSERT_FALSE(legal.empty());
        const auto [from, to] = legal.front();

        const auto outcome = control.PerformAction(SwapAction(to.col, to.row, from.col, from.row));

        EXPECT_TRUE(outcome.has_value());
        EXPECT_GT(model.GetScore(), 0);
    }

    TEST_F(Match3GameControlTest, ASwapPlaysLikeTrySwap) {
        Match3GameModel reference = model;
        const auto legal = model.GetLegalSwaps();
        ASSERT_FALSE(legal.empty());
        const auto [from, to] = legal.front();
        ASSERT_TRUE(reference.TrySwap(from, to));

        const auto outcome = control.PerformAction(SwapAction(from.col, from.row, to.col, to.row));

        ASSERT_TRUE(outcome.has_value());
        EXPECT_EQ(model.GetScore(), reference.GetScore());
        EXPECT_EQ(GetJsonInt(*outcome, "scoreGained"), reference.GetScore());
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                EXPECT_EQ(model.GetCellAt(Cell{col, row}), reference.GetCellAt(Cell{col, row}));
            }
        }
    }

    TEST_F(Match3GameControlTest, IllegalSwapsAreRefusedAndChangeNothing) {
        // A neighbour pair that makes no match, and cells that are not neighbours
        Cell quiet{};
        bool foundQuiet = false;
        for (int8_t col = 0; col + 1 < kBoardSize && !foundQuiet; ++col) {
            if (!model.WouldSwapMatch(Cell{col, 0}, Cell{static_cast<int8_t>(col + 1), 0})) {
                quiet = Cell{col, 0};
                foundQuiet = true;
            }
        }
        ASSERT_TRUE(foundQuiet);

        const uint64_t version = model.GetVersion();
        const Json notNeighbours = SwapAction(0, 0, 2, 0);
        const Json diagonal = SwapAction(0, 0, 1, 1);
        const Json sameCell = SwapAction(3, 3, 3, 3);
        const Json noMatch = SwapAction(quiet.col, quiet.row, quiet.col + 1, quiet.row);

        for (const Json* action : {&notNeighbours, &diagonal, &sameCell, &noMatch}) {
            const auto outcome = control.PerformAction(*action);
            ASSERT_FALSE(outcome.has_value()) << DumpJson(*action).c_str();
            EXPECT_EQ(ErrorCodeOf(outcome.error()), "illegal_action") << DumpJson(*action).c_str();
        }
        EXPECT_EQ(model.GetVersion(), version);
        EXPECT_EQ(model.GetScore(), 0);
    }

    TEST_F(Match3GameControlTest, MalformedActionsAreInvalidParams) {
        Json missingTo = SwapAction(0, 0, 1, 0);
        missingTo.erase("to");
        Json wrongType = SwapAction(0, 0, 1, 0);
        wrongType["type"] = "slide";
        Json noType = SwapAction(0, 0, 1, 0);
        noType.erase("type");
        Json typeNotString = SwapAction(0, 0, 1, 0);
        typeNotString["type"] = 5;
        Json colAsString = SwapAction(0, 0, 1, 0);
        colAsString["from"]["col"] = "0";
        Json colFraction = SwapAction(0, 0, 1, 0);
        colFraction["from"]["col"] = 0.5;
        Json noRow = SwapAction(0, 0, 1, 0);
        noRow["to"].erase("row");
        Json fromNotObject = SwapAction(0, 0, 1, 0);
        fromNotObject["from"] = Json::array({0, 0});

        const Json cases[] = {
            Json::object(),
            Json::array({1, 2}),
            missingTo,
            wrongType,
            noType,
            typeNotString,
            colAsString,
            colFraction,
            noRow,
            fromNotObject,
            SwapAction(8, 0, 7, 0),  // outside the board
            SwapAction(-1, 0, 0, 0),
            SwapAction(0, 8, 0, 7),
            SwapAction(0, -1, 0, 0),
            SwapAction(4294967296, 0, 1, 0),  // would wrap into the board if narrowed first
            SwapAction(0, 0, 9223372036854775807LL, 0),
            SwapAction(std::numeric_limits<int64_t>::min(), 0, 1, 0),
        };

        const uint64_t version = model.GetVersion();
        for (const Json& action : cases) {
            const auto outcome = control.PerformAction(action);
            ASSERT_FALSE(outcome.has_value()) << DumpJson(action).c_str();
            EXPECT_EQ(ErrorCodeOf(outcome.error()), "invalid_params") << DumpJson(action).c_str();
        }
        EXPECT_EQ(model.GetVersion(), version);
    }

    TEST_F(Match3GameControlTest, VersionGrowsOnlyWithRealChanges) {
        const uint64_t initial = control.GetStateVersion();
        EXPECT_EQ(initial, model.GetVersion());

        // A refused action and the reads do not change it
        (void)control.PerformAction(SwapAction(0, 0, 2, 0));
        (void)control.GetState();
        (void)control.GetActions();
        EXPECT_EQ(control.GetStateVersion(), initial);

        const auto legal = model.GetLegalSwaps();
        ASSERT_FALSE(legal.empty());
        ASSERT_TRUE(control.PerformAction(SwapAction(legal[0].first.col, legal[0].first.row, legal[0].second.col, legal[0].second.row)).has_value());
        const uint64_t afterSwap = control.GetStateVersion();
        EXPECT_GT(afterSwap, initial);

        ASSERT_TRUE(control.Reset(Json::object()).has_value());
        EXPECT_GT(control.GetStateVersion(), afterSwap);
    }

    TEST_F(Match3GameControlTest, ResetWithASeedStartsTheBoardOfThatSeed) {
        Json options = Json::object();
        options["seed"] = 7;

        ASSERT_TRUE(control.Reset(options).has_value());

        Match3GameModel reference;
        reference.Reset(7);
        EXPECT_EQ(model.GetSeed(), 7u);
        EXPECT_EQ(GetJsonInt(State(), "seed"), 7);
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                EXPECT_EQ(model.GetCellAt(Cell{col, row}), reference.GetCellAt(Cell{col, row}));
            }
        }
    }

    TEST_F(Match3GameControlTest, ResetWithoutOptionsUsesTheDefaultSeedAndClearsTheScore) {
        const auto legal = model.GetLegalSwaps();
        ASSERT_FALSE(legal.empty());
        ASSERT_TRUE(model.TrySwap(legal.front().first, legal.front().second));
        ASSERT_GT(model.GetScore(), 0);

        ASSERT_TRUE(control.Reset(Json::object()).has_value());

        EXPECT_EQ(model.GetSeed(), kDefaultSeed);
        EXPECT_EQ(model.GetScore(), 0);
    }

    TEST_F(Match3GameControlTest, ResetAcceptsTheWholeSeedRange) {
        Json lowest = Json::object();
        lowest["seed"] = 0;
        Json highest = Json::object();
        highest["seed"] = 4294967295LL;

        EXPECT_TRUE(control.Reset(lowest).has_value());
        EXPECT_EQ(model.GetSeed(), 0u);
        EXPECT_TRUE(control.Reset(highest).has_value());
        EXPECT_EQ(model.GetSeed(), 4294967295u);
    }

    TEST_F(Match3GameControlTest, ResetRefusesABadSeedAndChangesNothing) {
        Json negative = Json::object();
        negative["seed"] = -1;
        Json tooBig = Json::object();
        tooBig["seed"] = 4294967296LL;
        Json huge = Json::object();
        huge["seed"] = 18446744073709551615ULL;
        Json text = Json::object();
        text["seed"] = "7";
        Json fraction = Json::object();
        fraction["seed"] = 1.5;
        Json nothing = Json::object();
        nothing["seed"] = nullptr;

        const uint64_t version = model.GetVersion();
        for (const Json& options : {negative, tooBig, huge, text, fraction, nothing}) {
            const auto result = control.Reset(options);
            ASSERT_FALSE(result.has_value()) << DumpJson(options).c_str();
            EXPECT_EQ(ErrorCodeOf(result.error()), "invalid_params") << DumpJson(options).c_str();
        }
        EXPECT_EQ(model.GetVersion(), version);
        EXPECT_EQ(model.GetSeed(), 42u);
    }

    TEST_F(Match3GameControlTest, ABoardWithNoMovesIsReported) {
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                model.SetCellForTest(Cell{col, row}, static_cast<GemType>(1 + (col + row) % kGemTypeCount));
            }
        }

        const Json state = State();

        EXPECT_EQ(GetJsonString(state, "status").value_or("").c_str(), std::string("no_moves"));
        EXPECT_EQ(GetJsonInt(state, "legalSwapCount"), 0);
        EXPECT_TRUE(Actions().empty());
        EXPECT_EQ(ErrorCodeOf(control.PerformAction(SwapAction(0, 0, 1, 0)).error()), "illegal_action");
    }

    TEST_F(Match3GameControlTest, SetStateIsNotSupported) {
        const auto result = control.SetState(Json::object());

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(ErrorCodeOf(result.error()), "unsupported");
    }

    TEST_F(Match3GameControlTest, StateAndActionsSurviveARoundTripThroughText) {
        // What a client gets is the text of these values
        const auto parsedState = ParseJson(DumpJson(State()));
        const auto parsedActions = ParseJson(DumpJson(Actions()));

        ASSERT_TRUE(parsedState.has_value());
        ASSERT_TRUE(parsedActions.has_value());
        EXPECT_EQ(*parsedState, State());
        EXPECT_EQ(*parsedActions, Actions());
    }

}  // namespace BECore::Match3::Tests
