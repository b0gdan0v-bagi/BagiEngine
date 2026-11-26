#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/GameControlTest.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Reflection/ClassMeta.h>
#include <Common/ControlTestSupport.h>
#include <Common/DestroyTally.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Common/Require.h>
#include <Games/GameEditorTools.h>
#include <Games/GameManager.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kDeal = "fakecontrollable.deal";

        std::expected<void, ControlError> SayNothing(JsonView, JsonWriter& result) {
            result.Null();
            return {};
        }

        /// A command that does nothing, for the cases that are about the registry
        GameToolCommand PlainCommand(StringView name) {
            GameToolCommand command;
            command.name = name;
            command.run = SayNothing;
            return command;
        }

        GameToolPanel PlainPanel(StringView id) {
            GameToolPanel panel;
            panel.id = id;
            panel.draw = [] {};
            return panel;
        }

        /// A game of a given name that no factory knows, for a registry with a control service of its own
        class NamedToolGame : public IGame {
        public:
            explicit NamedToolGame(PoolString name) : _name(name) {}

            PoolString GetName() const override {
                return _name;
            }

            PoolString GetSceneName() const override {
                return PoolString();
            }

            void RegisterEditorTools(GameEditorTools& tools) override {
                tools.AddCommand(PlainCommand("deal"));
                tools.AddPanel(PlainPanel("state"));
            }

        private:
            PoolString _name;
        };

        class GameEditorToolsTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                GameManager::GetInstance().Initialize();
                SwitchToPlain();
            }

            void TearDown() override {
                GameManager::GetInstance().Shutdown();
            }

            static void SwitchToPlain() {
                GameManager::GetInstance().SwitchTo("FakePlain"_intern);
            }

            static void SwitchToFake() {
                GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
            }

            static GameEditorTools& Tools() {
                return GameManager::GetInstance().GetEditorTools();
            }

            static ControlService& Service() {
                return CoreManager::GetControlService();
            }

            static FakeControllableGame& Fake() {
                FakeControllableGame* found = nullptr;
                for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
                    if (auto* fake = game->Cast<FakeControllableGame>()) {
                        found = fake;
                    }
                }
                return Require(found);
            }

            /// Makes the fake game active with @p hook as its RegisterEditorTools()
            static void ActivateFake(MoveOnlyFunction<void(GameEditorTools&)> hook) {
                SwitchToPlain();
                Fake().editorTools = std::move(hook);
                SwitchToFake();
            }

            ControlReply Call(StringView method, StringView paramsText = {}) {
                return CallControlText(Service(), ++_lastId, method, paramsText);
            }

            static size_t CountListed(StringView method) {
                size_t count = 0;
                for (const ControlMethodListing& listing : Service().GetMethods()) {
                    count += listing.name == method ? 1 : 0;
                }
                return count;
            }

            int64_t _lastId = 0;
        };

    }  // namespace

    TEST_F(GameEditorToolsTest, ACommandIsAControlMethodListedWithItsParameters) {
        ActivateFake([](GameEditorTools& tools) {
            GameToolCommand deal = PlainCommand("deal");
            deal.summary = "Deals a wall";
            deal.params = {{"seed", GameCommandParamType::Integer, true, "The seed of the wall"},
                           {"label", GameCommandParamType::String, false, ""},
                           {"speed", GameCommandParamType::Number, false, ""},
                           {"open", GameCommandParamType::Boolean, false, ""},
                           {"hands", GameCommandParamType::Json, false, ""}};
            tools.AddCommand(std::move(deal));
        });

        const ControlReply response = Call("list_methods");

        ASSERT_TRUE(response.IsOk());
        JsonView listed;
        for (const JsonView method : MemberOf(response.Result(), "methods").Items()) {
            if (StringOf(method, "name") == kDeal) {
                listed = method;
            }
        }
        ASSERT_TRUE(listed.IsValid());
        EXPECT_EQ(StringOf(listed, "summary"), "Deals a wall");
        Vector<String> params;
        for (const JsonView param : MemberOf(listed, "params").Items()) {
            params.push_back(Format("{} {} {} {}", StringOf(param, "name"), StringOf(param, "type"), BoolOf(param, "required").value_or(false), StringOf(param, "description")));
        }
        EXPECT_EQ(params, (Vector<String>{"seed integer true The seed of the wall", "label string false ", "speed number false ", "open boolean false ", "hands any false "}));
    }

    TEST_F(GameEditorToolsTest, TheArgumentsReachTheCallbackAndWhatItWritesIsTheResult) {
        int64_t seed = 0;
        ActivateFake([&seed](GameEditorTools& tools) {
            GameToolCommand deal = PlainCommand("deal");
            deal.params = {{"seed", GameCommandParamType::Integer, true, ""}};
            deal.run = [&seed](JsonView arguments, JsonWriter& result) -> std::expected<void, ControlError> {
                seed = GetParamInt(arguments, "seed").value_or(0);
                result.BeginObject();
                result.Member("dealt", seed);
                result.EndObject();
                return {};
            };
            tools.AddCommand(std::move(deal));
        });

        const ControlReply response = Call(kDeal, R"({"seed":7,"undeclared":true})");

        ASSERT_TRUE(response.IsOk());
        EXPECT_EQ(seed, 7);
        EXPECT_EQ(IntOf(response.Result(), "dealt"), 7);
    }

    TEST_F(GameEditorToolsTest, TheEntriesOfTheActiveGameAreReadableFromTheGameManager) {
        ActivateFake([](GameEditorTools& tools) {
            GameToolCommand deal = PlainCommand("deal");
            deal.title = "Deal a wall";
            tools.AddCommand(std::move(deal));
            tools.AddCommand(PlainCommand("Show_2"));
            tools.AddPanel(PlainPanel("state"));
        });

        EXPECT_EQ(Tools().GetPrefix(), "fakecontrollable");
        EXPECT_EQ(Tools().GetGameName(), "FakeControllable"_intern);
        ASSERT_EQ(Tools().GetCommands().size(), 2u);
        EXPECT_EQ(Tools().GetCommands()[0]->method, kDeal);
        EXPECT_EQ(Tools().GetCommands()[0]->command.title, "Deal a wall");
        EXPECT_EQ(Tools().GetCommands()[1]->method, "fakecontrollable.Show_2");
        EXPECT_EQ(Tools().GetCommands()[1]->command.title, "Show_2");
        ASSERT_EQ(Tools().GetPanels().size(), 1u);
        EXPECT_EQ(Tools().GetPanels()[0]->panel.id, "state");
        EXPECT_EQ(Tools().GetPanels()[0]->panel.title, "state");
        EXPECT_TRUE(Tools().GetPanels()[0]->open);
    }

    TEST_F(GameEditorToolsTest, ArgumentsThatBreakTheDeclarationNeverReachTheCallback) {
        int calls = 0;
        ActivateFake([&calls](GameEditorTools& tools) {
            GameToolCommand deal = PlainCommand("deal");
            deal.params = {{"seed", GameCommandParamType::Integer, true, ""},
                           {"label", GameCommandParamType::String, false, ""},
                           {"speed", GameCommandParamType::Number, false, ""},
                           {"open", GameCommandParamType::Boolean, false, ""},
                           {"hands", GameCommandParamType::Json, false, ""}};
            deal.run = [&calls](JsonView arguments, JsonWriter& result) {
                ++calls;
                return SayNothing(arguments, result);
            };
            tools.AddCommand(std::move(deal));
        });
        struct Case {
            StringView params;
            StringView named;
        };
        const Case refused[] = {{R"({})", "'seed'"},
                                {R"({"seed":1.5})", "'seed'"},
                                {R"({"seed":null})", "'seed'"},
                                {R"({"seed":1,"label":2})", "'label'"},
                                {R"({"seed":1,"speed":"2"})", "'speed'"},
                                {R"({"seed":1,"speed":1e999})", "'speed'"},
                                {R"({"seed":1,"open":1})", "'open'"}};

        for (const Case& test : refused) {
            const ControlReply response = Call(kDeal, test.params);
            EXPECT_EQ(response.ErrorCode(), "invalid_params") << String(test.params).c_str();
            EXPECT_NE(response.ErrorMessage().find(String(test.named)), String::npos) << response.ErrorMessage().c_str();
        }
        EXPECT_EQ(calls, 0);

        EXPECT_TRUE(Call(kDeal, R"({"seed":1,"label":"a","speed":2,"open":false,"hands":null})").IsOk());
        EXPECT_TRUE(Call(kDeal, R"({"seed":1,"speed":0.5,"hands":[{}]})").IsOk());
        EXPECT_EQ(calls, 2);
    }

    TEST_F(GameEditorToolsTest, TheErrorOfTheCallbackIsTheAnswerAndWhatItWroteIsDropped) {
        ActivateFake([](GameEditorTools& tools) {
            GameToolCommand fails = PlainCommand("fails");
            fails.run = [](JsonView, JsonWriter& result) -> std::expected<void, ControlError> {
                result.BeginObject();
                result.Member("half", 1);
                return std::unexpected(MakeControlError("no_wall", "there is no wall"));
            };
            tools.AddCommand(std::move(fails));
        });

        const ControlReply failed = Call("fakecontrollable.fails");

        EXPECT_EQ(failed.ErrorCode(), "no_wall");
        EXPECT_EQ(failed.ErrorMessage(), "there is no wall");
        EXPECT_FALSE(failed.Result().IsValid());
    }

    TEST_F(GameEditorToolsTest, InvokeChecksAndRunsAsAControlRequestDoes) {
        int calls = 0;
        ActivateFake([&calls](GameEditorTools& tools) {
            GameToolCommand deal = PlainCommand("deal");
            deal.params = {{"seed", GameCommandParamType::Integer, true, ""}};
            deal.run = [&calls](JsonView arguments, JsonWriter& result) {
                ++calls;
                return SayNothing(arguments, result);
            };
            tools.AddCommand(std::move(deal));
        });
        const JsonDoc good(R"({"seed":3})");
        String text;
        JsonWriter writer(text);

        const auto unknown = Tools().Invoke("fakecontrollable.other", good.Root(), writer);
        const auto missing = Tools().Invoke(kDeal, EmptyParamsView(), writer);
        const auto ran = Tools().Invoke(kDeal, good.Root(), writer);

        ASSERT_FALSE(unknown.has_value());
        EXPECT_EQ(unknown.error().code, "unknown_method");
        ASSERT_FALSE(missing.has_value());
        EXPECT_EQ(missing.error().code, "invalid_params");
        EXPECT_TRUE(ran.has_value());
        EXPECT_EQ(text, "null");
        EXPECT_EQ(calls, 1);
    }

    TEST_F(GameEditorToolsTest, ABadOrRepeatedEntryIsRefusedWithOneErrorEach) {
        int kept = 0;
        LogRecorder log;
        ActivateFake([&kept](GameEditorTools& tools) {
            GameToolCommand deal = PlainCommand("deal");
            deal.run = [&kept](JsonView arguments, JsonWriter& result) {
                ++kept;
                return SayNothing(arguments, result);
            };
            tools.AddCommand(std::move(deal));
            tools.AddCommand(PlainCommand("deal"));
            tools.AddCommand(PlainCommand(""));
            tools.AddCommand(PlainCommand("view.state"));
            tools.AddCommand(PlainCommand("two words"));
            GameToolCommand empty;
            empty.name = "empty";
            tools.AddCommand(std::move(empty));
            GameToolCommand badParam = PlainCommand("badParam");
            badParam.params = {{"a-b", GameCommandParamType::String, false, ""}};
            tools.AddCommand(std::move(badParam));
            GameToolCommand repeatedParam = PlainCommand("repeatedParam");
            repeatedParam.params = {{"a", GameCommandParamType::String, false, ""}, {"a", GameCommandParamType::Integer, false, ""}};
            tools.AddCommand(std::move(repeatedParam));

            tools.AddPanel(PlainPanel("state"));
            tools.AddPanel(PlainPanel("state"));
            tools.AddPanel(PlainPanel(""));
            tools.AddPanel(PlainPanel("a/b"));
            GameToolPanel noDraw;
            noDraw.id = "noDraw";
            tools.AddPanel(std::move(noDraw));
        });
        LogEvent::Flush();

        EXPECT_EQ(log.Count(LogLevel::Error, {"[GameEditorTools] the command", "refused"}), 7u);
        EXPECT_EQ(log.Count(LogLevel::Error, {"[GameEditorTools] the panel", "refused"}), 4u);
        EXPECT_EQ(Tools().GetCommands().size(), 1u);
        EXPECT_EQ(Tools().GetPanels().size(), 1u);
        EXPECT_TRUE(Call(kDeal).IsOk());
        EXPECT_EQ(kept, 1);  // the repeat did not replace the first
    }

    TEST_F(GameEditorToolsTest, ACategoryStaysWhileItsGameIsActiveAndABadOrTakenNameIsRefused) {
        LogRecorder log;
        ActivateFake([](GameEditorTools& tools) {
            tools.AddCategory({"turn-based-2", Color(1, 2, 3, 255)});
            tools.AddCategory({"turn-based-2", Color(9, 9, 9, 255)});
            tools.AddCategory({"", {}});
            tools.AddCategory({"Combat", {}});
            tools.AddCategory({"two words", {}});
            tools.AddCategory({"render", {}});
        });
        LogEvent::Flush();

        EXPECT_EQ(log.Count(LogLevel::Error, {"[GameEditorTools] the category", "refused"}), 5u);
        ASSERT_EQ(Tools().GetCategories().size(), 1u);
        EXPECT_EQ(Require(Tools().FindCategory("turn-based-2")).color.r, 1);  // the repeat did not replace the first
        const uint64_t version = Tools().GetVersion();
        Tools().AddCategory({"loot", {}});
        EXPECT_GT(Tools().GetVersion(), version);

        SwitchToPlain();

        EXPECT_TRUE(Tools().GetCategories().empty());
    }

    TEST_F(GameEditorToolsTest, NothingIsAcceptedWithoutAnActivatedGame) {
        GameManager::GetInstance().Shutdown();
        LogRecorder log;
        const uint64_t version = Tools().GetVersion();

        Tools().AddCommand(PlainCommand("deal"));
        Tools().AddPanel(PlainPanel("state"));
        Tools().AddCategory({"combat", {}});
        LogEvent::Flush();

        EXPECT_EQ(log.Count(LogLevel::Error, {"refused", "no game is activated"}), 3u);
        EXPECT_TRUE(Tools().GetCategories().empty());
        EXPECT_TRUE(Tools().GetCommands().empty());
        EXPECT_TRUE(Tools().GetPanels().empty());
        EXPECT_EQ(Tools().GetVersion(), version);
        EXPECT_EQ(Call(".deal").ErrorCode(), "unknown_method");
    }

    TEST_F(GameEditorToolsTest, ThePrefixIsTheNameInLowerCaseAndAnEmptyOneRefusesEverything) {
        ControlService service;
        GameEditorTools tools;
        NamedToolGame named("My Game-2.0_x"_intern);
        NamedToolGame nameless{PoolString()};

        tools.Activate(named, service);
        EXPECT_EQ(tools.GetPrefix(), "my_game_2_0_x");
        EXPECT_TRUE(CallControlText(service, 1, "my_game_2_0_x.deal").IsOk());

        LogRecorder log;
        tools.Activate(nameless, service);
        LogEvent::Flush();

        EXPECT_EQ(log.Count(LogLevel::Error, {"refused"}), 2u);
        EXPECT_TRUE(tools.GetCommands().empty());
        EXPECT_TRUE(tools.GetPanels().empty());
        EXPECT_TRUE(service.GetMethods().empty());  // the entries of the game before went with the new activation
    }

    TEST_F(GameEditorToolsTest, ASwitchRemovesEverythingAndSwitchingBackRegistersOnce) {
        const auto hook = [](GameEditorTools& tools) {
            tools.AddCommand(PlainCommand("deal"));
            tools.AddPanel(PlainPanel("state"));
        };
        ActivateFake(hook);
        const uint64_t registered = Tools().GetVersion();

        SwitchToPlain();

        EXPECT_EQ(Call(kDeal).ErrorCode(), "unknown_method");
        EXPECT_EQ(CountListed(kDeal), 0u);
        EXPECT_TRUE(Tools().GetCommands().empty());
        EXPECT_TRUE(Tools().GetPanels().empty());
        const uint64_t removed = Tools().GetVersion();
        EXPECT_GT(removed, registered);

        SwitchToFake();

        EXPECT_TRUE(Call(kDeal).IsOk());
        EXPECT_EQ(CountListed(kDeal), 1u);
        EXPECT_EQ(Tools().GetCommands().size(), 1u);
        EXPECT_EQ(Tools().GetPanels().size(), 1u);
        EXPECT_GT(Tools().GetVersion(), removed);
    }

    TEST_F(GameEditorToolsTest, ShutdownRemovesEverythingAndDropsTheCallbacks) {
        int destroyed = 0;
        ActivateFake([&destroyed](GameEditorTools& tools) {
            GameToolCommand deal = PlainCommand("deal");
            deal.run = [owner = DestroyTally(destroyed)](JsonView arguments, JsonWriter& result) { return SayNothing(arguments, result); };
            tools.AddCommand(std::move(deal));
            tools.AddPanel(PlainPanel("state"));
        });
        const uint64_t registered = Tools().GetVersion();

        GameManager::GetInstance().Shutdown();

        EXPECT_EQ(destroyed, 1);
        EXPECT_EQ(Call(kDeal).ErrorCode(), "unknown_method");
        EXPECT_TRUE(Tools().GetCommands().empty());
        EXPECT_TRUE(Tools().GetPanels().empty());
        EXPECT_GT(Tools().GetVersion(), registered);
    }

    TEST_F(GameEditorToolsTest, ASwitchToTheActiveGameAndAResetLeaveTheToolsAlone) {
        ActivateFake([](GameEditorTools& tools) { tools.AddCommand(PlainCommand("deal")); });
        const uint64_t version = Tools().GetVersion();

        SwitchToFake();
        GameManager::GetInstance().ResetActive();

        EXPECT_EQ(Tools().GetVersion(), version);
        EXPECT_TRUE(Call(kDeal).IsOk());
    }

    TEST_F(GameEditorToolsTest, ACommandThatSwitchesTheGameFromInsideItselfAnswersAndKeepsItsCapture) {
        int destroyed = 0;
        int destroyedInsideTheCall = -1;
        ActivateFake([&destroyed, &destroyedInsideTheCall](GameEditorTools& tools) {
            GameToolCommand leave = PlainCommand("leave");
            leave.run = [owner = DestroyTally(destroyed), answer = String("left"), &destroyed, &destroyedInsideTheCall](JsonView, JsonWriter& result) -> std::expected<void, ControlError> {
                SwitchToPlain();
                destroyedInsideTheCall = destroyed;
                result.Value(answer);
                return {};
            };
            tools.AddCommand(std::move(leave));
        });

        const ControlReply response = Call("fakecontrollable.leave");

        ASSERT_TRUE(response.IsOk());
        EXPECT_TRUE(response.Result().TextEquals("left"));
        EXPECT_EQ(destroyedInsideTheCall, 0);
        EXPECT_EQ(destroyed, 1);
        EXPECT_TRUE(Tools().GetCommands().empty());
        EXPECT_EQ(Call("fakecontrollable.leave").ErrorCode(), "unknown_method");

        SwitchToFake();
        const JsonDoc nothing("{}");
        String text;
        JsonWriter writer(text);
        EXPECT_TRUE(Tools().Invoke("fakecontrollable.leave", nothing.Root(), writer).has_value());
        EXPECT_EQ(text, R"("left")");
        EXPECT_EQ(destroyed, 2);
    }

    TEST_F(GameEditorToolsTest, AGameWithoutTheHookRegistersNothing) {
        const size_t methods = Service().GetMethods().size();

        SwitchToFake();  // its hook is empty, as the default of IGame is

        EXPECT_TRUE(Tools().GetCommands().empty());
        EXPECT_TRUE(Tools().GetPanels().empty());
        EXPECT_EQ(Service().GetMethods().size(), methods);

        SwitchToPlain();

        EXPECT_TRUE(Tools().GetCommands().empty());
        EXPECT_EQ(Service().GetMethods().size(), methods);
    }

}  // namespace BECore::Tests
