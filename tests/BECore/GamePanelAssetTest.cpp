#include <BECore/GameControlTest.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/GamePanel/GamePanelAssetModel.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelInterpreter.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr ImVec2 kWindowSize{420.0f, 560.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr float kSameRowTolerance = 2.0f;

        Path GamePanelFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "GamePanel.xml";
        }

        bool IsKnownCommand(StringView name) {
            return FindEditorCommand(name) != nullptr;
        }

        bool Contains(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

    }  // namespace

    /// The shipped `GamePanel.xml` drawn over the two models of the Game panel in an ImGui context with no backend, found and driven the way a control
    /// client does it: by the labels of `ImGuiItemIndex`, with the mouse and the keyboard of ImGui
    class GamePanelAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            _fonts = LoadImGuiFonts(io, _buffers);
            SetActiveImGuiFonts(_fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            _index.Arm(*ImGui::GetCurrentContext());

            const FunctionRef<bool(StringView)> knownCommand(IsKnownCommand);
            const FunctionRef<bool(StringView)> knownModel(IsKnownPanelModel);
            PanelValidationContext context;
            context.isKnownCommand = knownCommand;
            context.isKnownModel = knownModel;
            const PanelLoadResult loaded = LoadPanelAsset(ReadFile(GamePanelFile()), context);
            ASSERT_TRUE(loaded.asset);
            ASSERT_TRUE(loaded.issues.empty());
            _asset = loaded.asset;
            _windowName = Format("{}###{}", _asset->title, _asset->id);
            _interpreter.Prepare(_asset);

            _panelHandle = _sources.Register("panel", [this] { return _panel.GetObject(); });
            _gameHandle = _sources.Register("game", [this] { return _active.GetObject(); });
            SetGame(&_game);
        }

        void TearDown() override {
            _panelHandle.Reset();
            _gameHandle.Reset();
            _interpreter.Prepare(IntrusivePtr<PanelAsset>{});
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
        }

        void SetGame(IGame* game) {
            _panel.SetGame(game);
            _active.SetGame(game);
            Settle();
        }

        /// The frame of the widget: the models update, the window is drawn, and a click is run as a command after it
        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            _panel.Update();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(kWindowSize);
            if (ImGui::Begin(_windowName.c_str())) {
                _interpreter.Draw(_sources, _result, nullptr);
            }
            ImGui::End();
            ImGui::Render();
            if (_result.clicked) {
                EXPECT_TRUE(_panel.RunCommand(_result.command, _result.argument)) << _result.command.c_str();
            }
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        /// The first visible item whose label holds @p part
        const ImGuiItemRecord* Find(StringView part) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && Contains(item.label, part)) {
                    return &item;
                }
            }
            return nullptr;
        }

        void ClickAt(ImVec2 point) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Frame();
            Settle();
        }

        /// A click on the centre of the item, which must be there
        void Click(StringView part) {
            const ImGuiItemRecord* item = Find(part);
            ASSERT_NE(item, nullptr) << String(part).c_str();
            ClickAt(ImVec2(item->CenterX(), item->CenterY()));
        }

        void Type(const char* text) {
            ImGui::GetIO().AddInputCharactersUTF8(text);
            Settle();
        }

        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<PanelAsset> _asset;
        String _windowName;
        PanelDataSources _sources;
        PanelSourceHandle _panelHandle;
        PanelSourceHandle _gameHandle;
        GamePanelAssetModel _panel;
        ActiveGameSourceModel _active;
        PanelInterpreter _interpreter;
        PanelDrawResult _result;
        FakeControllableGame _game;
    };

    TEST_F(GamePanelAssetTest, TheAssetIsTheHiddenWindowThatTheViewGameCommandOpens) {
        EXPECT_EQ(_asset->id, "GamePanel");
        EXPECT_EQ(_asset->title, "Game");
        EXPECT_EQ(_asset->command, "view.game");
        EXPECT_FALSE(_asset->open);
        EXPECT_EQ(_windowName, "Game###GamePanel");
    }

    TEST_F(GamePanelAssetTest, TheControlsHaveTheLabelsAClientFindsThemBy) {
        ASSERT_NE(Find("##gameActionFilter"), nullptr);
        ASSERT_NE(Find("inc"), nullptr);
        ASSERT_NE(Find("Reset"), nullptr);
        EXPECT_NE(Find("AddToCounter(amount)##call"), nullptr);
        EXPECT_NE(Find("Fields"), nullptr);
        EXPECT_NE(Find("State"), nullptr);
    }

    TEST_F(GamePanelAssetTest, ATextTypedIntoTheFilterNarrowsTheActions) {
        ASSERT_NO_FATAL_FAILURE(Click("##gameActionFilter"));
        Type("zzz");

        EXPECT_EQ(Find("inc##perform"), nullptr);
        EXPECT_TRUE(_panel.GetView().noActionMatches);
        EXPECT_EQ(_panel.GetView().filter, "zzz");
    }

    TEST_F(GamePanelAssetTest, AClickOnAnActionReachesTheGame) {
        ASSERT_NO_FATAL_FAILURE(Click("inc##perform"));

        EXPECT_EQ(_game.counter, 1);
        EXPECT_TRUE(_panel.GetView().resultOk);
        EXPECT_EQ(_panel.GetView().lastResult, "{\"counter\":1}");
    }

    TEST_F(GamePanelAssetTest, AClickOnResetStartsTheGameOver) {
        _game.counter = 4;
        ++_game.version;
        Settle();

        ASSERT_NO_FATAL_FAILURE(Click("Reset"));

        EXPECT_EQ(_game.counter, 0);
    }

    TEST_F(GamePanelAssetTest, AMethodRunsWithTheArgumentsTypedInItsOwnRow) {
        const ImGuiItemRecord* button = Find("AddToCounter(amount)##call");
        ASSERT_NE(button, nullptr);
        const float rowY = button->CenterY();
        const ImGuiItemRecord* arguments = nullptr;
        for (const ImGuiItemRecord& item : _index.Items()) {
            if (item.visible && Contains(item.label, "##arguments") && std::abs(item.CenterY() - rowY) < kSameRowTolerance) {
                arguments = &item;
            }
        }
        ASSERT_NE(arguments, nullptr) << "the input of the row is not on the line of its button";
        ClickAt(ImVec2(arguments->CenterX(), arguments->CenterY()));
        Type("amount=3");

        ASSERT_NO_FATAL_FAILURE(Click("AddToCounter(amount)##call"));

        EXPECT_EQ(_game.counter, 3);
    }

    TEST_F(GamePanelAssetTest, TheStateStartsCollapsedAndCopyPutsItOnTheClipboard) {
        EXPECT_EQ(Find("Copy"), nullptr);

        ASSERT_NO_FATAL_FAILURE(Click("State"));
        ASSERT_NO_FATAL_FAILURE(Click("Copy"));

        EXPECT_TRUE(Contains(ImGui::GetClipboardText(), "\"counter\": 0"));
    }

    TEST_F(GamePanelAssetTest, AGameThatIsNotControllableHasNoActionsOrState) {
        FakePlainGame plain;
        SetGame(&plain);

        EXPECT_EQ(Find("##gameActionFilter"), nullptr);
        EXPECT_EQ(Find("State"), nullptr);
        EXPECT_NE(Find("Reset"), nullptr);
    }

    TEST_F(GamePanelAssetTest, WithNoGameTheWindowHoldsTheEmptyStateOnly) {
        SetGame(nullptr);

        EXPECT_NE(ImGui::FindWindowByName(_windowName.c_str()), nullptr);
        EXPECT_EQ(Find("Reset"), nullptr);
        EXPECT_EQ(Find("##gameActionFilter"), nullptr);
        EXPECT_EQ(Find("Fields"), nullptr);
    }

}  // namespace BECore::Tests
