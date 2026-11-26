#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/GameControlTest.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/Reflection/ClassMeta.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Events/SubscriptionHolder.h>
#include <Games/GameEditorTools.h>
#include <Games/GameManager.h>
#include <Widgets/EditorLayout/CommandFilter.h>
#include <Widgets/Feedback/ToastEvents.h>
#include <Widgets/GamePanel/GameCommandRunner.h>
#include <Widgets/GamePanel/GameEditorCommands.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 720.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 4;
        constexpr const char* kHostName = "##Host";
        constexpr StringView kDeal = "fakecontrollable.deal";

        GameToolCommand CommandSaying(StringView name, StringView said, int& runs) {
            GameToolCommand command;
            command.name = name;
            command.run = [text = String(said), &runs](JsonView, JsonWriter& result) -> std::expected<void, ControlError> {
                ++runs;
                result.Value(StringView(text.data(), text.size()));
                return {};
            };
            return command;
        }

        /// A game whose prefix is that of editor commands
        class EditorNamedGame : public IGame {
        public:
            PoolString GetName() const override {
                return "Game"_intern;
            }

            PoolString GetSceneName() const override {
                return PoolString();
            }

            void RegisterEditorTools(GameEditorTools& tools) override {
                tools.AddCommand(CommandSaying("pause", "paused", runs));
            }

            int runs = 0;
        };

        class ToastRecorder : public SubscriptionHolder {
        public:
            ToastRecorder() {
                Subscribe<EditorToastEvent, &ToastRecorder::OnToast>(this);
            }

            Vector<String> toasts;

        private:
            void OnToast(const EditorToastEvent& event) {
                toasts.push_back(Format("{}: {}", event.level == ToastLevel::Error ? "error" : "success", event.text));
            }
        };

        class GameEditorCommandsTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                GameManager::GetInstance().Initialize();
                SwitchTo("FakePlain"_intern);
                _runner.SetTools(&Tools());
                _runner.Connect();
            }

            void TearDown() override {
                GameManager::GetInstance().Shutdown();
            }

            static void SwitchTo(PoolString game) {
                GameManager::GetInstance().SwitchTo(game);
            }

            static GameEditorTools& Tools() {
                return GameManager::GetInstance().GetEditorTools();
            }

            /// Makes the fake game active with @p hook as its RegisterEditorTools()
            static void ActivateFake(MoveOnlyFunction<void(GameEditorTools&)> hook) {
                SwitchTo("FakePlain"_intern);
                for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
                    if (auto* fake = game->Cast<FakeControllableGame>()) {
                        fake->editorTools = std::move(hook);
                    }
                }
                SwitchTo("FakeControllable"_intern);
            }

            const EditorCommand* FindRow(StringView id) {
                for (const EditorCommand& command : _commands.GetCommands()) {
                    if (command.id.ToStringView() == id) {
                        return &command;
                    }
                }
                return nullptr;
            }

            GameEditorCommands _commands;
            GameCommandRunner _runner;
            ToastRecorder _toasts;
        };

        /// The runner's form in an ImGui context with no backend
        class GameCommandRunnerFormTest : public GameEditorCommandsTest {
        protected:
            void SetUp() override {
                GameEditorCommandsTest::SetUp();
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = kDisplaySize;
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                _index.Arm(*ImGui::GetCurrentContext());
            }

            void TearDown() override {
                _index.Disarm();
                ImGui::DestroyContext();
                GameEditorCommandsTest::TearDown();
            }

            void Frame() {
                ImGui::GetIO().DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                if (ImGui::Begin(kHostName)) {
                    _runner.Draw();
                }
                ImGui::End();
                ImGui::Render();
            }

            void Settle() {
                for (int i = 0; i < kSettleFrames; ++i) {
                    Frame();
                }
            }

            static bool IsFormOpen() {
                return !ImGui::GetCurrentContext()->OpenPopupStack.empty();
            }

            /// Clicks the item whose label ends with @p text
            void Click(StringView text) {
                const ImGuiItemRecord* found = nullptr;
                for (const ImGuiItemRecord& item : _index.Items()) {
                    if (item.visible && StringView(item.label.data(), item.label.size()).ends_with(text)) {
                        found = &item;
                    }
                }
                ASSERT_NE(found, nullptr) << String(text).c_str();
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent(found->CenterX(), found->CenterY());
                Frame();
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                Frame();
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                Frame();
            }

            /// A command with one optional switch; _arguments is what its last run got
            void ActivateShout() {
                ActivateFake([this](GameEditorTools& tools) {
                    GameToolCommand shout;
                    shout.name = "shout";
                    shout.params = {{"loud", GameCommandParamType::Boolean, false, ""}};
                    shout.run = [this](JsonView arguments, JsonWriter& result) -> std::expected<void, ControlError> {
                        _arguments.push_back(String(arguments.Raw()));
                        result.Null();
                        return {};
                    };
                    tools.AddCommand(std::move(shout));
                });
                EditorCommandEvent::Emit("fakecontrollable.shout"_intern);
                Settle();
            }

            ImGuiItemIndex _index;
            Vector<String> _arguments;
        };

    }  // namespace

    TEST_F(GameEditorCommandsTest, WithNoGameCommandTheListIsTheEditorsTable) {
        _commands.Sync(&Tools());

        EXPECT_EQ(_commands.GetCommands().data(), GetEditorCommands().data());
        EXPECT_EQ(_commands.GetCommands().size(), GetEditorCommands().size());

        _commands.Sync(nullptr);
        EXPECT_EQ(_commands.GetCommands().data(), GetEditorCommands().data());
    }

    TEST_F(GameEditorCommandsTest, AGameCommandIsARowAfterTheTableThatThePaletteFinds) {
        int runs = 0;
        ActivateFake([&runs](GameEditorTools& tools) {
            GameToolCommand deal = CommandSaying("deal", "dealt", runs);
            deal.title = "Deal a wall";
            tools.AddCommand(std::move(deal));
        });

        _commands.Sync(&Tools());

        ASSERT_EQ(_commands.GetCommands().size(), GetEditorCommands().size() + 1);
        const EditorCommand& row = _commands.GetCommands().back();
        EXPECT_EQ(row.id.ToStringView(), kDeal);
        EXPECT_EQ(row.title, "FakeControllable: Deal a wall");
        EXPECT_FALSE(row.defaultChord.has_value());
        EXPECT_FALSE(row.rebindable);
        const Vector<const EditorCommand*> matches = FilterCommands("deal a wall", _commands.GetCommands());
        ASSERT_FALSE(matches.empty());
        EXPECT_EQ(matches.front(), &row);
    }

    TEST_F(GameEditorCommandsTest, ASwitchOfTheGameTakesItsRowsAwayAndMovesTheVersion) {
        int runs = 0;
        ActivateFake([&runs](GameEditorTools& tools) { tools.AddCommand(CommandSaying("deal", "dealt", runs)); });
        _commands.Sync(&Tools());
        const uint64_t withGame = _commands.GetVersion();
        _commands.Sync(&Tools());
        EXPECT_EQ(_commands.GetVersion(), withGame);

        SwitchTo("FakePlain"_intern);
        _commands.Sync(&Tools());

        EXPECT_GT(_commands.GetVersion(), withGame);
        EXPECT_EQ(_commands.GetCommands().data(), GetEditorCommands().data());
    }

    TEST_F(GameEditorCommandsTest, EveryRowKeepsItsTitleWhenThereAreMany) {
        constexpr int kMany = 64;
        int runs = 0;
        ActivateFake([&runs](GameEditorTools& tools) {
            for (int i = 0; i < kMany; ++i) {
                const String name = Format("c{}", i);
                tools.AddCommand(CommandSaying(StringView(name.data(), name.size()), "", runs));
            }
        });

        _commands.Sync(&Tools());

        for (int i = 0; i < kMany; ++i) {
            const String id = Format("fakecontrollable.c{}", i);
            const EditorCommand& row = Require(FindRow(StringView(id.data(), id.size())));
            EXPECT_EQ(String(row.title), Format("FakeControllable: c{}", i));
        }
    }

    TEST_F(GameEditorCommandsTest, AMethodThatReadsLikeAnEditorCommandGetsNoRowAndIsNotRun) {
        ControlService service;
        GameEditorTools tools;
        EditorNamedGame game;
        tools.Activate(game, service);
        _runner.SetTools(&tools);

        _commands.Sync(&tools);
        EditorCommandEvent::Emit(EditorCommandIds::kGamePause);

        EXPECT_EQ(_commands.GetCommands().data(), GetEditorCommands().data());
        EXPECT_EQ(game.runs, 0);
        tools.Clear();
    }

    TEST_F(GameEditorCommandsTest, ACommandWithNoParametersRunsFromItsEventAndTellsTheResult) {
        int runs = 0;
        ActivateFake([&runs](GameEditorTools& tools) { tools.AddCommand(CommandSaying("deal", "dealt", runs)); });

        EditorCommandEvent::Emit(PoolString::Intern(kDeal));
        EditorCommandEvent::Emit(EditorCommandIds::kViewResetLayout);

        EXPECT_EQ(runs, 1);
        EXPECT_EQ(_toasts.toasts, (Vector<String>{"success: deal: \"dealt\""}));
    }

    TEST_F(GameEditorCommandsTest, AFailureAndAMissingResultAreErrorToasts) {
        ActivateFake([](GameEditorTools& tools) {
            GameToolCommand failing;
            failing.name = "failing";
            failing.run = [](JsonView, JsonWriter&) -> std::expected<void, ControlError> { return std::unexpected(MakeControlError("no_wall", "there is no wall")); };
            tools.AddCommand(std::move(failing));
            GameToolCommand silent;
            silent.name = "silent";
            silent.run = [](JsonView, JsonWriter&) -> std::expected<void, ControlError> { return {}; };
            tools.AddCommand(std::move(silent));
        });

        EditorCommandEvent::Emit("fakecontrollable.failing"_intern);
        EditorCommandEvent::Emit("fakecontrollable.silent"_intern);

        EXPECT_EQ(_toasts.toasts, (Vector<String>{"error: failing: there is no wall", "error: silent: the command did not write exactly one value"}));
    }

    TEST_F(GameEditorCommandsTest, ACommandMaySwitchTheGameInsideItsCall) {
        ActivateFake([](GameEditorTools& tools) {
            GameToolCommand leave;
            leave.name = "leave";
            leave.run = [](JsonView, JsonWriter& result) -> std::expected<void, ControlError> {
                GameManager::GetInstance().SwitchTo("FakePlain"_intern);
                result.Null();
                return {};
            };
            tools.AddCommand(std::move(leave));
        });

        EditorCommandEvent::Emit("fakecontrollable.leave"_intern);

        EXPECT_TRUE(Tools().GetCommands().empty());
        EXPECT_EQ(_toasts.toasts, (Vector<String>{"success: leave: done"}));
    }

    TEST_F(GameCommandRunnerFormTest, ACommandWithParametersRunsOnlyFromItsFormWithWhatWasEntered) {
        ActivateShout();
        ASSERT_TRUE(IsFormOpen());
        EXPECT_TRUE(_arguments.empty());

        ASSERT_NO_FATAL_FAILURE(Click("loud"));
        ASSERT_NO_FATAL_FAILURE(Click(" Run"));

        EXPECT_FALSE(IsFormOpen());
        EXPECT_EQ(_arguments, (Vector<String>{R"({"loud":true})"}));
    }

    TEST_F(GameCommandRunnerFormTest, EscapeClosesTheFormAndRunsNothing) {
        ActivateShout();
        ASSERT_TRUE(IsFormOpen());

        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        Settle();

        EXPECT_FALSE(IsFormOpen());
        EXPECT_TRUE(_arguments.empty());
    }

    TEST_F(GameCommandRunnerFormTest, ASwitchOfTheGameClosesTheFormAndRunsNothing) {
        ActivateShout();
        ASSERT_TRUE(IsFormOpen());

        SwitchTo("FakePlain"_intern);
        Settle();

        EXPECT_FALSE(IsFormOpen());
        EXPECT_TRUE(_arguments.empty());
        EXPECT_TRUE(_toasts.toasts.empty());
    }

}  // namespace BECore::Tests
