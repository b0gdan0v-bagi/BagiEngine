#include <BECore/GameControlTest.h>
#include <BECore/Reflection/TypedRef.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/GamePanel/GamePanelAssetModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// The row of `GamePanelView::methods` whose signature starts with @p name
        size_t MethodRow(const GamePanelView& view, StringView name) {
            for (size_t i = 0; i < view.methods.size(); ++i) {
                if (StringView(view.methods[i].signature).starts_with(name)) {
                    return i;
                }
            }
            ADD_FAILURE() << "no method row " << String(name).c_str();
            return 0;
        }

        String IndexText(size_t index) {
            return ToString(index);
        }

    }  // namespace

    class GamePanelAssetModelTest : public ::testing::Test {
    protected:
        const GamePanelView& View() const {
            return _model.GetView();
        }

        /// The view the way the panel's input writes it: through the object the model hands out
        GamePanelView& Edit() {
            return *static_cast<GamePanelView*>(_model.GetObject().Address());
        }

        bool Run(const PoolString command, StringView argument = {}) {
            return _model.RunCommand(command.ToStringView(), argument);
        }

        FakeControllableGame _game;
        GamePanelAssetModel _model;
    };

    TEST_F(GamePanelAssetModelTest, TheViewOfAControllableGameHasItsNameVersionActionsAndState) {
        _model.SetGame(&_game);

        EXPECT_TRUE(View().hasGame);
        EXPECT_TRUE(View().isControllable);
        EXPECT_FALSE(View().notControllable);
        EXPECT_TRUE(Contains(View().gameName, "FakeControllable"));
        EXPECT_EQ(View().versionText, "v0");
        EXPECT_EQ(View().countText, "1 / 1");
        EXPECT_TRUE(View().showActionsList);
        ASSERT_EQ(View().actions.size(), 1u);
        EXPECT_EQ(View().actions[0].label, "inc");
        EXPECT_EQ(View().actions[0].index, 0u);
        EXPECT_TRUE(Contains(View().stateText, "\"counter\": 0"));
        EXPECT_TRUE(View().showMethods);
        EXPECT_FALSE(View().hasResult);
    }

    TEST_F(GamePanelAssetModelTest, TheFilterTypedIntoTheViewNarrowsTheActions) {
        _model.SetGame(&_game);

        Edit().filter = "zzz";
        _model.Update();
        EXPECT_FALSE(View().showActionsList);
        EXPECT_TRUE(View().actions.empty());
        EXPECT_TRUE(View().noActionMatches);
        EXPECT_FALSE(View().noActionsLegal);
        EXPECT_EQ(View().countText, "0 / 1");

        Edit().filter = "INC";
        _model.Update();
        EXPECT_EQ(View().actions.size(), 1u);
        EXPECT_FALSE(View().noActionMatches);
    }

    TEST_F(GamePanelAssetModelTest, PerformByIndexChangesTheGameAndShowsTheResult) {
        _model.SetGame(&_game);

        EXPECT_TRUE(Run(EditorCommandIds::kGamePanelPerform, "0"));

        EXPECT_EQ(_game.counter, 1);
        EXPECT_TRUE(View().hasResult);
        EXPECT_TRUE(View().resultOk);
        EXPECT_FALSE(View().resultError);
        EXPECT_EQ(View().lastResult, "{\"counter\":1}");
        EXPECT_TRUE(Contains(View().stateText, "\"counter\": 1"));
    }

    TEST_F(GamePanelAssetModelTest, AnArgumentThatIsNoIndexOrIsPastTheListDoesNothing) {
        _model.SetGame(&_game);

        for (const char* argument : {"", "x", "-1", "1", "99999999999999999999", "0 "}) {
            EXPECT_TRUE(Run(EditorCommandIds::kGamePanelPerform, argument)) << argument;
        }
        EXPECT_EQ(_game.counter, 0);
        EXPECT_FALSE(View().hasResult);

        const size_t past = View().methods.size();
        EXPECT_TRUE(Run(EditorCommandIds::kGamePanelCall, IndexText(past)));
        EXPECT_FALSE(View().hasResult);
    }

    TEST_F(GamePanelAssetModelTest, ResetByCommandStartsTheGameOver) {
        _game.counter = 4;
        ++_game.version;
        _model.SetGame(&_game);

        EXPECT_TRUE(Run(EditorCommandIds::kGamePanelReset));

        EXPECT_EQ(_game.counter, 0);
        EXPECT_TRUE(View().resultOk);
        EXPECT_EQ(View().lastResult, "ok");
    }

    TEST_F(GamePanelAssetModelTest, ACallRunsTheMethodWithTheArgumentsTypedInItsRow) {
        _model.SetGame(&_game);
        const size_t row = MethodRow(View(), "AddToCounter");
        EXPECT_TRUE(View().methods[row].callable);
        EXPECT_TRUE(View().methods[row].hasParams);

        Edit().methods[row].arguments = "amount=5";
        EXPECT_TRUE(Run(EditorCommandIds::kGamePanelCall, IndexText(row)));

        EXPECT_EQ(_game.counter, 5);
        EXPECT_TRUE(View().resultOk);

        Edit().methods[row].arguments = "amount=text";
        EXPECT_TRUE(Run(EditorCommandIds::kGamePanelCall, IndexText(row)));
        EXPECT_TRUE(View().resultError);
        EXPECT_EQ(_game.counter, 5);
    }

    TEST_F(GamePanelAssetModelTest, TheArgumentsTypedForAMethodSurviveAStateChangeAndAreDroppedWithAnotherGame) {
        _model.SetGame(&_game);
        const size_t row = MethodRow(View(), "AddToCounter");
        Edit().methods[row].arguments = "amount=2";

        EXPECT_TRUE(Run(EditorCommandIds::kGamePanelPerform, "0"));
        EXPECT_TRUE(Run(EditorCommandIds::kGamePanelPerform, "0"));
        EXPECT_EQ(View().methods[row].arguments, "amount=2");

        FakeControllableGame other;
        _model.SetGame(&other);
        EXPECT_TRUE(View().methods[MethodRow(View(), "AddToCounter")].arguments.empty());
    }

    TEST_F(GamePanelAssetModelTest, AGameThatIsNotControllableHasNoActionsOrState) {
        FakePlainGame plain;
        _model.SetGame(&plain);

        EXPECT_TRUE(View().hasGame);
        EXPECT_FALSE(View().isControllable);
        EXPECT_TRUE(View().notControllable);
        EXPECT_FALSE(View().noActionsLegal);
        EXPECT_FALSE(View().showActionsList);
        EXPECT_TRUE(View().versionText.empty());
        EXPECT_TRUE(View().stateText.empty());
        EXPECT_TRUE(View().actions.empty());

        EXPECT_TRUE(Run(EditorCommandIds::kGamePanelReset));
        EXPECT_EQ(plain.resetCalls, 1);
    }

    TEST_F(GamePanelAssetModelTest, NoGameShowsOnlyTheEmptyState) {
        _model.SetGame(nullptr);

        EXPECT_FALSE(View().hasGame);
        EXPECT_FALSE(View().isControllable);
        EXPECT_FALSE(View().notControllable);
        EXPECT_TRUE(View().gameName.empty());
        EXPECT_TRUE(View().actions.empty());
        EXPECT_TRUE(View().methods.empty());
        EXPECT_FALSE(View().showMethods);

        EXPECT_TRUE(Run(EditorCommandIds::kGamePanelPerform, "0"));
        EXPECT_TRUE(Run(EditorCommandIds::kGamePanelReset));
        EXPECT_FALSE(View().hasResult);
    }

    TEST_F(GamePanelAssetModelTest, ACommandOfAnotherModelIsNotTaken) {
        _model.SetGame(&_game);

        EXPECT_FALSE(_model.RunCommand("fileChanges.rescan", ""));
        EXPECT_FALSE(_model.RunCommand("gamePanel.unknown", "0"));
        EXPECT_FALSE(_model.RunCommand("", ""));
    }

    TEST_F(GamePanelAssetModelTest, AChangeOfTheGameOutsideThePanelShowsOnTheNextUpdate) {
        _model.SetGame(&_game);

        _game.counter = 7;
        ++_game.version;
        _model.Update();

        EXPECT_EQ(View().versionText, "v1");
        EXPECT_TRUE(Contains(View().stateText, "\"counter\": 7"));
    }

    TEST_F(GamePanelAssetModelTest, TheSourceOfTheGameGivesTheGameItselfAndNothingWithoutOne) {
        ActiveGameSourceModel source;
        source.SetGame(nullptr);
        EXPECT_FALSE(source.GetObject());

        source.SetGame(&_game);
        const TypedRef object = source.GetObject();
        ASSERT_TRUE(object);
        EXPECT_EQ(object.object, static_cast<void*>(&_game));
        EXPECT_FALSE(source.RunCommand("gamePanel.reset", ""));
    }

    TEST_F(GamePanelAssetModelTest, ASteadyUpdateAllocatesNothing) {
        _model.SetGame(&_game);
        _model.Update();

        ExpectNoAllocations([&] {
            for (int frame = 0; frame < 20; ++frame) {
                _model.Update();
            }
        });
    }

}  // namespace BECore::Tests
