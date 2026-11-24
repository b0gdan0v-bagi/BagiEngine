#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Games/GameControl.h>
#include <Games/GameText.h>
#include <Stress/StressGameControl.h>
#include <gtest/gtest.h>
#include <tests/StressSceneFixture.h>

/// The adapter needs no scene: a scene change reaches the test as a counter.
namespace BECore::Stress::Tests {

    namespace {

        using BECore::Tests::IntOf;
        using BECore::Tests::ItemAt;
        using BECore::Tests::JsonDoc;
        using BECore::Tests::StringOf;

        /// The scenes `get_state` lists, in the order of the enum; `rmlui` only in a build with the RmlUi host
#ifdef STRESS_WITH_RMLUI
        constexpr size_t kSceneCount = 4;
        constexpr StringView kSceneListText = R"(["sprites","entities","ui","rmlui"])";
#else
        constexpr size_t kSceneCount = 3;
        constexpr StringView kSceneListText = R"(["sprites","entities","ui"])";
#endif

        String ErrorCodeOf(const ControlError& error) {
            return error.code;
        }

        /// The actions of the list, as the texts a client sends: members in byte order, as the adapter writes them
        String ActionOf(StringView type) {
            return Format(R"({{"type":"{}"}})", type);
        }

        String SetScene(StringView scene) {
            return Format(R"({{"scene":"{}","type":"set_scene"}})", scene);
        }

        /// @p count is JSON text: the cases send what is not a number too
        String SetCount(StringView count) {
            return Format(R"({{"count":{},"type":"set_count"}})", count);
        }

        String SetLanguage(StringView language) {
            return Format(R"({{"language":"{}","type":"set_language"}})", language);
        }

        /// @p text of one document written again with the keys in byte order: the form the adapter writes in
        String Sorted(StringView text) {
            const JsonDoc document(text);
            String sorted;
            JsonWriter writer(sorted);
            WriteJsonSorted(writer, document.Root());
            return sorted;
        }

        /// Whether @p item is the same document as @p expected, whatever the order of the keys
        bool IsAction(JsonView item, StringView expected) {
            return JsonEquals(item, JsonDoc(expected).Root());
        }

        class StressGameControlTest : public ::testing::Test {
        protected:
            StressGameControlTest() : control(state, [this] { ++sceneChanges; }) {}

            void SetUp() override {
                state.SetCount(100);
            }

            String State() {
                const auto text = StateText(control);
                EXPECT_TRUE(text.has_value());
                return text ? *text : String();
            }

            String Actions() {
                const auto text = ActionsText(control);
                EXPECT_TRUE(text.has_value());
                return text ? *text : String();
            }

            /// Performs @p action and expects the error with @p code, and that nothing moved
            void ExpectRefused(StringView action, const char* code) {
                const uint64_t version = state.Version();
                const auto result = PerformActionText(control, action);
                ASSERT_FALSE(result.has_value()) << action.data();
                EXPECT_EQ(ErrorCodeOf(result.error()), code) << action.data();
                EXPECT_FALSE(result.error().message.empty());
                EXPECT_EQ(state.Version(), version) << action.data();
                EXPECT_EQ(sceneChanges, 0);
            }

            StressState state;
            int sceneChanges = 0;
            StressGameControl control;
        };

    }  // namespace

    TEST_F(StressGameControlTest, TheStateShowsWhatTheAppShows) {
        const JsonDoc state(State());
        ASSERT_TRUE(state.IsParsed());
        EXPECT_EQ(StringOf(state.Root(), "scene"), "sprites");
        EXPECT_EQ(IntOf(state.Root(), "count"), 100);
        EXPECT_EQ(StringOf(state.Root(), "language"), "en");
        EXPECT_EQ(IntOf(state.Root(), "maxCount"), static_cast<int64_t>(kMaxCount));
        EXPECT_TRUE(JsonEquals(BECore::Tests::MemberOf(state.Root(), "scenes"), JsonDoc(kSceneListText).Root()));
    }

    TEST_F(StressGameControlTest, TheStateFollowsTheState) {
        this->state.SetScene(StressScene::Ui);
        this->state.SetLanguage(StressLanguage::Ru);
        const JsonDoc state(State());
        EXPECT_EQ(StringOf(state.Root(), "scene"), "ui");
        EXPECT_EQ(StringOf(state.Root(), "language"), "ru");
    }

    TEST_F(StressGameControlTest, TheActionsListTheOtherScenesTheHalfTheDoubleAndTheOtherLanguage) {
        const JsonDoc actions(Actions());
        const size_t otherScenes = kSceneCount - 1;
        ASSERT_EQ(actions.Root().Size(), otherScenes + 3);
        EXPECT_TRUE(IsAction(ItemAt(actions.Root(), 0), SetScene("entities")));
        EXPECT_TRUE(IsAction(ItemAt(actions.Root(), 1), SetScene("ui")));
#ifdef STRESS_WITH_RMLUI
        EXPECT_TRUE(IsAction(ItemAt(actions.Root(), 2), SetScene("rmlui")));
#endif
        EXPECT_TRUE(IsAction(ItemAt(actions.Root(), otherScenes), SetCount("50")));
        EXPECT_TRUE(IsAction(ItemAt(actions.Root(), otherScenes + 1), SetCount("200")));
        EXPECT_TRUE(IsAction(ItemAt(actions.Root(), otherScenes + 2), SetLanguage("ru")));
    }

    TEST_F(StressGameControlTest, NoActionNamesTheActiveSceneOrLanguage) {
        state.SetScene(StressScene::Entities);
        state.SetLanguage(StressLanguage::Ru);
        const JsonDoc actions(Actions());
        for (const JsonView action : actions.Root().Items()) {
            EXPECT_FALSE(IsAction(action, SetScene("entities")));
            EXPECT_FALSE(IsAction(action, SetLanguage("ru")));
        }
    }

    TEST_F(StressGameControlTest, TheCountActionsStayInsideTheRange) {
        state.SetCount(1);
        const JsonDoc small(Actions());
        ASSERT_EQ(small.Root().Size(), kSceneCount + 1);  // the other scenes, the double (the half is 0), the language
        EXPECT_TRUE(IsAction(ItemAt(small.Root(), kSceneCount - 1), SetCount("2")));

        state.SetCount(kMaxCount);
        const JsonDoc large(Actions());
        ASSERT_EQ(large.Root().Size(), kSceneCount + 1);  // the double is above the maximum
        EXPECT_TRUE(IsAction(ItemAt(large.Root(), kSceneCount - 1), SetCount(Format("{}", kMaxCount / 2))));

        state.SetCount(kMaxCount / 2 + 1);
        const JsonDoc middle(Actions());
        for (const JsonView action : middle.Root().Items()) {
            if (StringOf(action, "type") == "set_count") {
                EXPECT_LE(IntOf(action, "count").value_or(-1), static_cast<int64_t>(kMaxCount));
                EXPECT_GE(IntOf(action, "count").value_or(-1), 1);
            }
        }
    }

    TEST_F(StressGameControlTest, EveryListedActionPerforms) {
        const JsonDoc actions(Actions());
        for (const JsonView action : actions.Root().Items()) {
            StressState fresh;
            fresh.SetCount(100);
            StressGameControl other(fresh, [] {});
            const uint64_t version = fresh.Version();
            const auto result = PerformActionText(other, action.Raw());
            EXPECT_TRUE(result.has_value()) << String(action.Raw()).c_str();
            EXPECT_GT(fresh.Version(), version) << String(action.Raw()).c_str();
        }
    }

    TEST_F(StressGameControlTest, ASceneChangeSwitchesTheScene) {
        const auto result = PerformActionText(control, SetScene("ui"));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(state.GetScene(), StressScene::Ui);
        EXPECT_EQ(sceneChanges, 1);
        const JsonDoc outcome(*result);
        EXPECT_EQ(StringOf(outcome.Root(), "scene"), "ui");
        EXPECT_EQ(IntOf(outcome.Root(), "count"), 100);
    }

    TEST_F(StressGameControlTest, TheSameSceneIsAcceptedAndChangesNothing) {
        const uint64_t version = state.Version();
        ASSERT_TRUE(PerformActionText(control, SetScene("sprites")).has_value());
        EXPECT_EQ(state.Version(), version);
        EXPECT_EQ(sceneChanges, 0);
    }

    TEST_F(StressGameControlTest, AnyCountInRangeIsAccepted) {
        for (const int64_t count : {int64_t{1}, int64_t{7}, int64_t{12345}, static_cast<int64_t>(kMaxCount)}) {
            const auto result = PerformActionText(control, SetCount(Format("{}", count)));
            ASSERT_TRUE(result.has_value()) << count;
            EXPECT_EQ(state.GetCount(), static_cast<uint32_t>(count));
            EXPECT_EQ(IntOf(JsonDoc(*result).Root(), "count"), count);
        }
        EXPECT_EQ(sceneChanges, 0);
    }

    TEST_F(StressGameControlTest, TheSameCountIsAcceptedAndChangesNothing) {
        const uint64_t version = state.Version();
        ASSERT_TRUE(PerformActionText(control, SetCount("100")).has_value());
        EXPECT_EQ(state.Version(), version);
    }

    TEST_F(StressGameControlTest, ALanguageChangeIsAccepted) {
        ASSERT_TRUE(PerformActionText(control, SetLanguage("ru")).has_value());
        EXPECT_EQ(state.GetLanguage(), StressLanguage::Ru);
        ASSERT_TRUE(PerformActionText(control, SetLanguage("en")).has_value());
        EXPECT_EQ(state.GetLanguage(), StressLanguage::En);
    }

    TEST_F(StressGameControlTest, ABadCountIsInvalidParams) {
        ExpectRefused(SetCount("0"), "invalid_params");
        ExpectRefused(SetCount("-3"), "invalid_params");
        ExpectRefused(SetCount(Format("{}", static_cast<int64_t>(kMaxCount) + 1)), "invalid_params");
        ExpectRefused(SetCount("9000000000"), "invalid_params");
        ExpectRefused(SetCount("1.5"), "invalid_params");
        ExpectRefused(SetCount(R"("12")"), "invalid_params");
        ExpectRefused(SetCount("null"), "invalid_params");
        ExpectRefused(SetCount("true"), "invalid_params");
        ExpectRefused(ActionOf("set_count"), "invalid_params");
    }

    TEST_F(StressGameControlTest, ABadSceneOrLanguageIsInvalidParams) {
        ExpectRefused(SetScene("3d"), "invalid_params");
        ExpectRefused(SetScene(""), "invalid_params");
        ExpectRefused(ActionOf("set_scene"), "invalid_params");
        ExpectRefused(SetLanguage("de"), "invalid_params");
        ExpectRefused(ActionOf("set_language"), "invalid_params");

        ExpectRefused(R"({"scene":1,"type":"set_scene"})", "invalid_params");
    }

    TEST_F(StressGameControlTest, ATypeThatIsNotAStringIsInvalidParamsAndAnUnknownOneIsIllegal) {
        ExpectRefused("{}", "invalid_params");
        ExpectRefused("null", "invalid_params");
        ExpectRefused("[]", "invalid_params");
        ExpectRefused(R"({"type":5})", "invalid_params");
        ExpectRefused(ActionOf("explode"), "illegal_action");
        ExpectRefused(ActionOf(""), "illegal_action");
    }

    TEST_F(StressGameControlTest, TheVersionMovesOnlyOnAChange) {
        EXPECT_EQ(control.GetStateVersion(), state.Version());
        const uint64_t version = control.GetStateVersion();
        State();
        Actions();
        EXPECT_EQ(control.GetStateVersion(), version);

        ASSERT_TRUE(PerformActionText(control, SetCount("101")).has_value());
        EXPECT_GT(control.GetStateVersion(), version);
        EXPECT_EQ(control.GetStateVersion(), state.Version());
    }

    TEST_F(StressGameControlTest, ResetRestoresTheDefaults) {
        state.SetScene(StressScene::Ui);
        state.SetLanguage(StressLanguage::Ru);
        ASSERT_TRUE(ResetText(control).has_value());
        EXPECT_EQ(state.GetScene(), StressScene::Sprites);
        EXPECT_EQ(state.GetCount(), kDefaultCount);
        EXPECT_EQ(state.GetLanguage(), StressLanguage::En);
        EXPECT_EQ(sceneChanges, 1);
    }

    TEST_F(StressGameControlTest, ResetTakesASceneAndACount) {
        ASSERT_TRUE(ResetText(control, R"({"count":777,"scene":"entities"})").has_value());
        EXPECT_EQ(state.GetScene(), StressScene::Entities);
        EXPECT_EQ(state.GetCount(), 777u);
        EXPECT_EQ(sceneChanges, 1);
    }

    TEST_F(StressGameControlTest, ResetWithNoOptionsObjectIsTheDefaults) {
        state.SetCount(5);
        ASSERT_TRUE(ResetText(control, "null").has_value());
        EXPECT_EQ(state.GetCount(), kDefaultCount);
    }

    TEST_F(StressGameControlTest, ABadResetOptionChangesNothing) {
        state.SetScene(StressScene::Ui);
        const uint64_t version = state.Version();
        auto result = ResetText(control, R"({"count":0,"scene":"entities"})");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(ErrorCodeOf(result.error()), "invalid_params");
        EXPECT_EQ(state.Version(), version);

        result = ResetText(control, R"({"count":5,"scene":"3d"})");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(ErrorCodeOf(result.error()), "invalid_params");
        EXPECT_EQ(state.Version(), version);
        EXPECT_EQ(state.GetScene(), StressScene::Ui);
    }

    namespace {

        String WrittenState(const StressGameControl& control) {
            String text;
            JsonWriter writer(text);
            EXPECT_TRUE(control.WriteState(writer).has_value());
            EXPECT_TRUE(writer.IsComplete());
            return text;
        }

        String WrittenActions(const StressGameControl& control) {
            String text;
            JsonWriter writer(text);
            EXPECT_TRUE(control.WriteActions(writer).has_value());
            EXPECT_TRUE(writer.IsComplete());
            return text;
        }

    }  // namespace

    TEST_F(StressGameControlTest, TheWritingFormsAreTheSortedCompactDocument) {
        for (const StressScene scene : EnumUtils<StressScene>::Values()) {
            for (const StressLanguage language : {StressLanguage::En, StressLanguage::Ru}) {
                for (const uint32_t count : {1u, 2u, 3u, 100u, kMaxCount / 2, kMaxCount / 2 + 1, kMaxCount}) {
                    state.SetScene(scene);
                    state.SetLanguage(language);
                    state.SetCount(count);
                    const String where = Format("scene {} language {} count {}", ToFlagValue(scene), ToFlagValue(language), count);

                    const String state = WrittenState(control);
                    const String actions = WrittenActions(control);
                    EXPECT_EQ(state, Sorted(state)) << where.c_str();
                    EXPECT_EQ(actions, Sorted(actions)) << where.c_str();
                    EXPECT_EQ(state, State()) << where.c_str();
                    EXPECT_EQ(actions, Actions()) << where.c_str();
                }
            }
        }
    }

    TEST_F(StressGameControlTest, TheWritingFormsAreAlphabetical) {
        EXPECT_EQ(WrittenState(control), Format(R"({{"count":100,"language":"en","maxCount":1000000,"scene":"sprites","scenes":{}}})", kSceneListText));
#ifdef STRESS_WITH_RMLUI
        constexpr StringView rmlUiAction = R"({"scene":"rmlui","type":"set_scene"},)";
#else
        constexpr StringView rmlUiAction = "";
#endif
        EXPECT_EQ(WrittenActions(control), Format(R"([{{"scene":"entities","type":"set_scene"}},{{"scene":"ui","type":"set_scene"}},{}{{"count":50,"type":"set_count"}},)"
                                                  R"({{"count":200,"type":"set_count"}},{{"language":"ru","type":"set_language"}}])",
                                                  rmlUiAction));
    }

    TEST_F(StressGameControlTest, TheSecondWritingIntoAKeptTextAllocatesNothing) {
        String text;
        const auto write = [&] {
            text.clear();
            JsonWriter writer(text);
            EXPECT_TRUE(control.WriteState(writer).has_value());
            text.clear();
            JsonWriter actions(text);
            EXPECT_TRUE(control.WriteActions(actions).has_value());
        };
        write();
        BECore::Tests::ExpectNoAllocations(write);
    }

    namespace {

        /// The game that owns the adapter, active in the manager, behind a ControlService with the handlers of the games
        class StressGameControlServiceTest : public StressSceneFixture {
        protected:
            void SetUp() override {
                StartGame();
                RegisterGameControl(service);
            }

            void TearDown() override {
                service.Shutdown();
                StressSceneFixture::TearDown();
            }

            String Ask(StringView method) {
                String text;
                service.Post(BECore::Tests::MakeRequestText(1, method), [&text](const String& reply) { text = reply; });
                service.Drain();
                return text;
            }

            ControlService service;
        };

    }  // namespace

    TEST_F(StressGameControlServiceTest, GetStateAndListActionsWriteTheDocumentsOfTheAdapter) {
        StressGame& game = *StressGame::GetActive();
        game.GetState().SetCount(40);
        IControllableGame& control = *game.AsControllable();

        // The answer is the document of the adapter inside `result`, with the game and the version; the members of the answer are in byte order
        const String name(game.GetName().ToStringView());
        const uint64_t version = control.GetStateVersion();
        EXPECT_EQ(Ask("get_state"), Format(R"({{"id":1,"ok":true,"result":{{"game":"{}","state":{},"version":{}}}}})", name, *StateText(control), version));
        EXPECT_EQ(Ask("list_actions"), Format(R"({{"id":1,"ok":true,"result":{{"actions":{},"game":"{}","version":{}}}}})", *ActionsText(control), name, version));
    }

}  // namespace BECore::Stress::Tests
