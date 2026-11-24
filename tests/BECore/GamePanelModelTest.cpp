#include <BECore/GameControlTest.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <Widgets/GamePanel/GamePanelModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        StringView ViewOf(const String& text) {
            return {text.data(), text.size()};
        }

        String LabelOf(StringView action) {
            const JsonDoc doc(action);
            EXPECT_TRUE(doc.IsParsed());
            return FormatGameActionLabel(doc);
        }

    }  // namespace

    TEST(GamePanelModelTest, NoGameShowsNothing) {
        GamePanelModel model;
        EXPECT_TRUE(model.Sync(nullptr));
        EXPECT_EQ(model.GetGame(), nullptr);
        EXPECT_FALSE(model.IsControllable());
        EXPECT_TRUE(model.GetActions().empty());
        EXPECT_TRUE(model.GetMethods().empty());
        EXPECT_FALSE(model.Sync(nullptr));
    }

    TEST(GamePanelModelTest, ShowsStateAndActionsOfAControllableGame) {
        FakeControllableGame game;
        GamePanelModel model;
        ASSERT_TRUE(model.Sync(&game));
        EXPECT_TRUE(model.IsControllable());
        EXPECT_EQ(model.GetGameName(), "FakeControllable"_intern);
        EXPECT_TRUE(Contains(model.GetStateText(), "\"counter\": 0"));
        ASSERT_EQ(model.GetActions().size(), 1u);
        EXPECT_EQ(ViewOf(model.GetActions()[0].label), "inc");
        EXPECT_EQ(ViewOf(model.GetActions()[0].action), "{\"type\":\"inc\"}");
        ASSERT_EQ(model.GetVisibleActions().size(), 1u);
    }

    TEST(GamePanelModelTest, RebuildsOnlyWhenTheVersionMoves) {
        FakeControllableGame game;
        GamePanelModel model;
        model.Sync(&game);
        const uint64_t version = model.GetVersion();
        EXPECT_FALSE(model.Sync(&game));
        EXPECT_EQ(model.GetVersion(), version);

        // A change the panel did not make (a control client, the game itself) is seen too
        ++game.counter;
        ++game.version;
        EXPECT_TRUE(model.Sync(&game));
        EXPECT_TRUE(Contains(model.GetStateText(), "\"counter\": 1"));
    }

    TEST(GamePanelModelTest, PerformRunsTheActionAndShowsTheOutcome) {
        FakeControllableGame game;
        GamePanelModel model;
        model.Sync(&game);
        model.Perform(0);
        EXPECT_EQ(game.counter, 1);
        EXPECT_FALSE(model.IsLastResultError());
        EXPECT_EQ(ViewOf(model.GetLastResult()), "{\"counter\":1}");

        EXPECT_TRUE(model.Sync(&game));
        EXPECT_TRUE(Contains(model.GetStateText(), "\"counter\": 1"));

        model.Perform(5);  // out of range: ignored
        EXPECT_EQ(game.counter, 1);
    }

    TEST(GamePanelModelTest, ResetUsesTheControllableResetOrThePlainOne) {
        FakeControllableGame controllable;
        controllable.counter = 4;
        GamePanelModel model;
        model.Sync(&controllable);
        model.Reset();
        EXPECT_EQ(controllable.counter, 0);
        EXPECT_EQ(controllable.plainResetCalls, 0);

        FakePlainGame plain;
        model.Sync(&plain);
        EXPECT_FALSE(model.IsControllable());
        EXPECT_TRUE(model.GetStateText().empty());
        EXPECT_TRUE(model.GetLastResult().empty());  // a new game drops the result of the old one
        model.Reset();
        EXPECT_EQ(plain.resetCalls, 1);
    }

    TEST(GamePanelModelTest, FilterKeepsTheMatchingActions) {
        FakeControllableGame game;
        GamePanelModel model;
        model.Sync(&game);
        model.SetFilter("INC");
        EXPECT_EQ(model.GetVisibleActions().size(), 1u);
        model.SetFilter("dec");
        EXPECT_TRUE(model.GetVisibleActions().empty());
        model.SetFilter("");
        EXPECT_EQ(model.GetVisibleActions().size(), 1u);
    }

    TEST(GamePanelModelTest, CallsAReflectedMethodWithConsoleArguments) {
        FakeControllableGame game;
        GamePanelModel model;
        model.Sync(&game);
        const Vector<GamePanelMethod>& methods = model.GetMethods();
        size_t index = methods.size();
        for (size_t i = 0; i < methods.size(); ++i) {
            if (methods[i].name == "AddToCounter") {
                index = i;
            }
        }
        ASSERT_LT(index, methods.size());
        EXPECT_TRUE(methods[index].callable);
        EXPECT_TRUE(methods[index].hasParams);
        EXPECT_EQ(ViewOf(methods[index].signature), "AddToCounter(amount)");

        model.Call(index, "amount=5");
        EXPECT_FALSE(model.IsLastResultError()) << model.GetLastResult().c_str();
        EXPECT_EQ(game.counter, 5);

        model.Call(index, "{\"amount\": 2}");
        EXPECT_EQ(game.counter, 7);

        model.Call(index, "amount=text");
        EXPECT_TRUE(model.IsLastResultError());
        EXPECT_TRUE(Contains(model.GetLastResult(), "bad_arguments"));
        EXPECT_EQ(game.counter, 7);

        model.Call(index, "amount=");
        EXPECT_TRUE(model.IsLastResultError());
    }

    TEST(GamePanelModelTest, ActionLabelsUseTheConsoleSyntax) {
        EXPECT_EQ(ViewOf(LabelOf(R"({"type":"set_count","count":50})")), "set_count count=50");
        EXPECT_EQ(ViewOf(LabelOf(R"({"type":"set_scene","scene":"ui"})")), "set_scene scene=ui");
        // A string that reads as JSON or holds a blank keeps its quotes, so the label reads back as the same action
        EXPECT_EQ(ViewOf(LabelOf(R"({"type":"say","text":"a b"})")), "say text=\"a b\"");
        EXPECT_EQ(ViewOf(LabelOf(R"({"type":"say","text":"true"})")), "say text=\"true\"");
        EXPECT_EQ(ViewOf(LabelOf(R"({"type":"say","text":"5"})")), "say text=\"5\"");
        EXPECT_EQ(ViewOf(LabelOf(R"({"move":"e2e4"})")), "{\"move\":\"e2e4\"}");
    }

    TEST(GamePanelModelTest, ALabelListsTheOtherMembersInKeyOrderAfterTheType) {
        EXPECT_EQ(ViewOf(LabelOf(R"({"zeta":1,"type":"go","alpha":"x","mid":[1,2]})")), "go alpha=x mid=[1,2] zeta=1");
        EXPECT_EQ(ViewOf(LabelOf(R"({"type":"go","pos":{"x":1,"y":2}})")), "go pos={\"x\":1,\"y\":2}");
    }

    TEST(GamePanelModelTest, OfRepeatedKeysOfAnActionTheFirstCounts) {
        EXPECT_EQ(ViewOf(LabelOf(R"({"type":"go","a":1,"a":2})")), "go a=1");
    }

    TEST(GamePanelModelTest, AnActionWithoutAStringTypeIsItsText) {
        EXPECT_EQ(ViewOf(LabelOf(R"({"type":5,"a":1})")), "{\"type\":5,\"a\":1}");
        EXPECT_EQ(ViewOf(LabelOf("[1,2]")), "[1,2]");
    }

    TEST(GamePanelModelTest, AMethodThatReturnsAnObjectShowsItAsWritten) {
        FakeControllableGame game;
        game.counter = 3;
        GamePanelModel model;
        model.Sync(&game);
        const Vector<GamePanelMethod>& methods = model.GetMethods();
        size_t index = methods.size();
        for (size_t i = 0; i < methods.size(); ++i) {
            if (methods[i].name == "Report") {
                index = i;
            }
        }
        ASSERT_LT(index, methods.size());

        model.Call(index, "scale=2");
        EXPECT_FALSE(model.IsLastResultError()) << model.GetLastResult().c_str();
        EXPECT_EQ(ViewOf(model.GetLastResult()), "{\"total\":6,\"label\":\"counter\"}");
    }

    TEST(GamePanelModelTest, AMethodThatReturnsNothingShowsOk) {
        FakeControllableGame game;
        GamePanelModel model;
        model.Sync(&game);
        const Vector<GamePanelMethod>& methods = model.GetMethods();
        for (size_t i = 0; i < methods.size(); ++i) {
            if (methods[i].name == "AddToCounter") {
                model.Call(i, "amount=1");
                EXPECT_EQ(ViewOf(model.GetLastResult()), "ok");
            }
        }
    }

}  // namespace BECore::Tests
