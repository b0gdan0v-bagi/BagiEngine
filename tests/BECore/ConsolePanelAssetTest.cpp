#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TestFiles.h>
#include <Widgets/Console/ConsolePanelModel.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelInterpreter.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        /// Wider than the asset's 720 px: the toolbar is longer than that, and a button outside the window cannot be clicked
        constexpr ImVec2 kWindowSize{1000.0f, 280.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr int kTooltipFrames = 40;
        constexpr float kRightOfTheButtons = 12.0f;
        constexpr StringView kTime = "12:34:56.789";
        constexpr StringView kChessFile = "a/src/Chess/Game/X.cpp";
        constexpr StringView kVulkanFile = "a/src/Modules/Vulkan/src/X.cpp";
        constexpr StringView kRowLabel = "##row";

        Path ConsolePanelFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "ConsolePanel.xml";
        }

        bool IsKnownCommand(StringView name) {
            return FindEditorCommand(name) != nullptr;
        }

        bool Contains(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        void AddLine(LogHistory& history, LogLevel level, StringView message, StringView file) {
            history.Add(level, message, LogSite{file.data(), 1U}, kTime);
        }

    }  // namespace

    /// The shipped `ConsolePanel.xml` drawn over `ConsolePanelModel` and its three slots in an ImGui context with no backend, found and driven by the
    /// labels of `ImGuiItemIndex` like a control client does
    class ConsolePanelAssetTest : public ::testing::Test {
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
            const PanelLoadResult loaded = LoadPanelAsset(ReadFile(ConsolePanelFile()), context);
            ASSERT_TRUE(loaded.asset);
            ASSERT_TRUE(loaded.issues.empty());
            _asset = loaded.asset;
            _windowName = Format("{}###{}", _asset->title, _asset->id);
            _interpreter.Prepare(_asset);

            AddLine(_history, LogLevel::Debug, "Texture loaded", kVulkanFile);
            AddLine(_history, LogLevel::Info, "Move played", kChessFile);
            AddLine(_history, LogLevel::Warning, "texture missing", kVulkanFile);
            AddLine(_history, LogLevel::Error, "Bad move", kChessFile);
            _model = New<ConsolePanelModel>();
            _model->SetHistory(&_history);
            _model->Initialize();
            ConsolePanelModel* model = _model.Get();
            _handle = _sources.Register("console", [model] { return model->GetObject(); });
            Settle();
        }

        void TearDown() override {
            _handle.Reset();
            _model = {};
            _interpreter.Prepare(IntrusivePtr<PanelAsset>{});
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
        }

        ConsolePanelView& View() {
            return const_cast<ConsolePanelView&>(_model->GetView());
        }

        /// The frame of the widget: the model updates, the window is drawn, and a click is run as a command after it
        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            _model->Update();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(kWindowSize);
            if (ImGui::Begin(_windowName.c_str())) {
                _interpreter.Draw(_sources, _result, nullptr);
            }
            ImGui::End();
            ImGui::Render();
            if (_result.clicked) {
                EXPECT_TRUE(_model->RunCommand(_result.command, _result.argument)) << _result.command.c_str();
            }
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        const ImGuiItemRecord* Find(StringView part) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && Contains(item.label, part)) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// The category combo: ImGui reports a combo with no label, and the toolbar adds it right before the filter
        const ImGuiItemRecord* Combo() {
            const Span<const ImGuiItemRecord> items = _index.Items();
            for (size_t i = 1; i < items.size(); ++i) {
                if (items[i].label == "##filter" && items[i - 1].label.empty()) {
                    return &items[i - 1];
                }
            }
            return nullptr;
        }

        /// Whether a tooltip shows after the mouse rests at @p point
        bool TooltipAt(ImVec2 point) {
            ImGui::GetIO().AddMousePosEvent(point.x, point.y);
            for (int i = 0; i < kTooltipFrames; ++i) {
                Frame();
            }
            const ImGuiWindow* tooltip = ImGui::FindWindowByName("##Tooltip_00");
            return tooltip != nullptr && tooltip->WasActive;
        }

        /// The log rows the rows slot drew: one selectable per row
        size_t CountRows() {
            size_t count = 0;
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && item.label == kRowLabel) {
                    ++count;
                }
            }
            return count;
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

        LogHistory _history{16};
        IntrusivePtr<ConsolePanelModel> _model;
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<PanelAsset> _asset;
        String _windowName;
        PanelDataSources _sources;
        PanelSourceHandle _handle;
        PanelInterpreter _interpreter;
        PanelDrawResult _result;
    };

    TEST_F(ConsolePanelAssetTest, TheAssetIsTheHiddenConsoleWindowThatTheViewConsoleCommandOpens) {
        EXPECT_EQ(_asset->id, "Console");
        EXPECT_EQ(_asset->command, "view.console");
        EXPECT_FALSE(_asset->open);
        EXPECT_EQ(_asset->titleBind, "console.title");
        EXPECT_EQ(_windowName, "Console###Console");
    }

    TEST_F(ConsolePanelAssetTest, TheToolbarTheRowsAndTheCommandLineHaveTheLabelsAClientFindsThemBy) {
        EXPECT_NE(Find("Debug 1###levelDebug"), nullptr);
        EXPECT_NE(Find("Error 1###levelError"), nullptr);
        EXPECT_NE(Combo(), nullptr);
        EXPECT_NE(Find("##filter"), nullptr);
        EXPECT_NE(Find("Pause"), nullptr);
        EXPECT_NE(Find("Clear"), nullptr);
        EXPECT_NE(Find("##consoleCommand"), nullptr);
        EXPECT_EQ(CountRows(), 4U);
    }

    TEST_F(ConsolePanelAssetTest, AClickOnALevelToggleHidesItsLinesAndAnotherBringsThemBack) {
        ASSERT_NO_FATAL_FAILURE(Click("###levelDebug"));
        EXPECT_FALSE(View().showDebug);
        EXPECT_EQ(CountRows(), 3U);

        ASSERT_NO_FATAL_FAILURE(Click("###levelDebug"));
        EXPECT_TRUE(View().showDebug);
        EXPECT_EQ(CountRows(), 4U);
    }

    TEST_F(ConsolePanelAssetTest, TheCategoryComboNarrowsTheRowsToTheChosenModule) {
        const ImGuiItemRecord* combo = Combo();
        ASSERT_NE(combo, nullptr);
        ClickAt(ImVec2(combo->CenterX(), combo->CenterY()));
        ASSERT_NO_FATAL_FAILURE(Click("Chess"));

        EXPECT_GT(View().category, 0);
        EXPECT_EQ(CountRows(), 2U);
    }

    TEST_F(ConsolePanelAssetTest, ATextTypedIntoTheFilterNarrowsTheRows) {
        ASSERT_NO_FATAL_FAILURE(Click("##filter"));
        Type("texture");

        EXPECT_EQ(View().filterText, "texture");
        EXPECT_EQ(CountRows(), 2U);
    }

    TEST_F(ConsolePanelAssetTest, PauseBecomesResumeAndTheListStopsGrowingUntilItIsPressed) {
        ASSERT_NO_FATAL_FAILURE(Click("Pause"));
        EXPECT_TRUE(View().paused);
        EXPECT_EQ(Find("Pause"), nullptr);
        EXPECT_NE(Find("Resume"), nullptr);

        AddLine(_history, LogLevel::Info, "Came while paused", kChessFile);
        Settle();
        EXPECT_EQ(CountRows(), 4U);

        ASSERT_NO_FATAL_FAILURE(Click("Resume"));
        EXPECT_FALSE(View().paused);
        EXPECT_EQ(CountRows(), 5U);
    }

    TEST_F(ConsolePanelAssetTest, ClearEmptiesTheHistory) {
        ASSERT_NO_FATAL_FAILURE(Click("Clear"));

        EXPECT_EQ(_history.Size(), 0U);
        EXPECT_EQ(CountRows(), 0U);
    }

    TEST_F(ConsolePanelAssetTest, TheLostCounterIsOnTheToolbarOnlyAboveZero) {
        const ImGuiItemRecord* clear = Find("Clear");
        ASSERT_NE(clear, nullptr);
        const ImVec2 afterTheButtons(clear->maxX + kRightOfTheButtons, clear->CenterY());
        EXPECT_FALSE(TooltipAt(afterTheButtons));

        // The model reads the process-wide queue's counter, which a test cannot overflow: the view is set as the model would set it
        View().lost = 3;
        View().lostText = "3 lost";
        Settle();

        EXPECT_TRUE(TooltipAt(afterTheButtons));
    }

}  // namespace BECore::Tests
