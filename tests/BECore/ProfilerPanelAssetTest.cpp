#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelInterpreter.h>
#include <Widgets/Profiler/ProfilerPanelModel.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 1400.0f};
        constexpr ImVec2 kWindowSize{900.0f, 1300.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr int kDoubleClickFrames = 40;
        /// Far past any number field's range, in pixels: a drag over it ends on the bound
        constexpr float kDragPixels = 40000.0f;
        constexpr float kRecordButtonEm = 6.0f;
        constexpr float kPixelTolerance = 0.5f;
        constexpr double kMinCustomBudgetMs = 1.0;
        constexpr double kMaxCustomBudgetMs = 1000.0;
        constexpr int32_t kCustomBudget = 2;

        constexpr float kMinComboHeight = 18.0f;
        constexpr float kMaxComboHeight = 32.0f;
        constexpr float kMinComboWidth = 100.0f;
        constexpr float kMaxComboWidth = 400.0f;
        constexpr StringView kWindowLabel = "Profiler###Profiler";

        constexpr uint64_t kSteadyFrameTicks = 16;
        constexpr uint64_t kSpikeFrameTicks = 80;  // over the default threshold of 50 ms
        constexpr double kNanosecondsPerTestTick = 1.0e6;
        constexpr StringView kNothingToWrite = "No frame in the ring to write";

        std::atomic<uint64_t> g_tick{0};
        uint64_t ManualNow() {
            return g_tick.load();
        }

        Path ProfilerPanelFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "ProfilerPanel.xml";
        }

        bool IsKnownCommand(StringView name) {
            return FindEditorCommand(name) != nullptr;
        }

        bool Contains(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

    }  // namespace

    /// The shipped `ProfilerPanel.xml` drawn over `ProfilerPanelModel` and its six slots in an ImGui context with no backend, found and driven by
    /// the labels of `ImGuiItemIndex` like a control client does
    class ProfilerPanelAssetTest : public ::testing::Test {
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
            const PanelLoadResult loaded = LoadPanelAsset(ReadFile(ProfilerPanelFile()), context);
            ASSERT_TRUE(loaded.asset);
            ASSERT_TRUE(loaded.issues.empty());
            _asset = loaded.asset;
            _windowName = Format("{}###{}", _asset->title, _asset->id);
            _interpreter.Prepare(_asset);

            g_tick = 0;
            _ring = MakeUnique<FrameRingBackend>(FrameRingBackend::Options{.now = &ManualNow, .nanosecondsPerTick = kNanosecondsPerTestTick});
            _model = New<ProfilerPanelModel>();
            _model->SetFrameStats(&_stats);
            _model->SetFrameRing(_ring.get());
            // The counts are paused over a draw in the profile build; nothing here checks them
            _model->SetPausesAllocCounting(false);
            _model->Initialize();
            ProfilerPanelModel* model = _model.Get();
            _handle = _sources.Register("profiler", [model] { return model->GetObject(); });
        }

        void TearDown() override {
            _handle.Reset();
            _model = {};
            _interpreter.Prepare(IntrusivePtr<PanelAsset>{});
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
        }

        ProfilerPanelView& View() {
            return const_cast<ProfilerPanelView&>(_model->GetView());
        }

        /// One more frame of the ring, @p ticks long, and one of the collector
        void AddFrame(uint64_t ticks) {
            g_tick = g_tick + ticks;
            _ring->FrameMark();
            _stats.BeginFrame();
            _stats.EndFrame({}, AllocTotals{});
        }

        /// The frame of the widget: the model updates, the window is drawn, the model's frame ends and a click is run as a command after it
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
            _model->EndUpdate();
            ImGui::Render();
            if (_result.clicked) {
                _clicked.push_back(_result.command);
                EXPECT_TRUE(_model->RunCommand(_result.command, _result.argument)) << _result.command.c_str();
            }
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        /// Makes a build with no zone recording draw the sections that need them: the slots read the model and the fake ring, not the build
        void ShowZoneSections() {
            View().zonesRecorded = true;
            Settle();
        }

        const ImGuiItemRecord* Find(StringView part) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && Contains(item.label, part)) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// The @p which th combo of the asset in drawing order: ImGui reports a combo with no label, and what else has none in the window is
        /// the title bar (as wide as the window) and small marks
        const ImGuiItemRecord* Combo(size_t which) {
            size_t seen = 0;
            for (const ImGuiItemRecord& item : _index.Items()) {
                const float height = item.maxY - item.minY;
                const float width = item.maxX - item.minX;
                const bool shaped = height > kMinComboHeight && height < kMaxComboHeight && width > kMinComboWidth && width < kMaxComboWidth;
                if (item.visible && item.label.empty() && shaped && item.window == kWindowLabel && seen++ == which) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// Opens the combo and picks the row named @p row
        void Choose(size_t combo, StringView row) {
            const ImGuiItemRecord* item = Combo(combo);
            ASSERT_NE(item, nullptr) << combo;
            ClickAt(ImVec2(item->CenterX(), item->CenterY()));
            ASSERT_NO_FATAL_FAILURE(Click(row));
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

        /// Presses on the item, moves the mouse @p pixels to the right and lets go: a drag field takes the movement
        void DragBy(StringView part, float pixels) {
            const ImGuiItemRecord* item = Find(part);
            ASSERT_NE(item, nullptr) << String(part).c_str();
            ImGuiIO& io = ImGui::GetIO();
            const ImVec2 start(item->CenterX(), item->CenterY());
            io.AddMousePosEvent(start.x, start.y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMousePosEvent(start.x + pixels, start.y);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Frame();
            // A second press within the double click time would turn a drag field into a text field
            for (int i = 0; i < kDoubleClickFrames; ++i) {
                Frame();
            }
        }

        void Type(const char* text) {
            ImGui::GetIO().AddInputCharactersUTF8(text);
            Settle();
        }

        bool Clicked(StringView command) const {
            for (const String& seen : _clicked) {
                if (seen == command) {
                    return true;
                }
            }
            return false;
        }

        FrameStatsCollector _stats;
        UniquePtr<FrameRingBackend> _ring;
        IntrusivePtr<ProfilerPanelModel> _model;
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<PanelAsset> _asset;
        String _windowName;
        PanelDataSources _sources;
        PanelSourceHandle _handle;
        PanelInterpreter _interpreter;
        PanelDrawResult _result;
        Vector<String> _clicked;
    };

    TEST_F(ProfilerPanelAssetTest, TheAssetIsTheHiddenProfilerWindowThatTheViewProfilerCommandOpens) {
        EXPECT_EQ(_asset->id, "Profiler");
        EXPECT_EQ(_asset->command, "view.profiler");
        EXPECT_FALSE(_asset->open);
        EXPECT_EQ(_windowName, "Profiler###Profiler");
    }

    TEST_F(ProfilerPanelAssetTest, WithNoFrameYetOnlyTheNoteAndTheSectionsThatNeedNoFrameAreThere) {
        Settle();
        EXPECT_EQ(Find("Pause"), nullptr);
        EXPECT_EQ(Combo(0), nullptr);
        EXPECT_NE(Find("Memory"), nullptr);
        EXPECT_NE(Find("Chrome trace"), nullptr);

        AddFrame(kSteadyFrameTicks);
        Settle();
        EXPECT_NE(Find("Pause"), nullptr);
        EXPECT_NE(Combo(1), nullptr);
    }

    TEST_F(ProfilerPanelAssetTest, ABuildWithNoZoneRecordingHasNoSectionsThatNeedZones) {
        AddFrame(kSteadyFrameTicks);
        Settle();
        EXPECT_EQ(Find("Zones") != nullptr, BE_PROFILER != 0);
        EXPECT_EQ(Find("Spikes") != nullptr, BE_PROFILER != 0);
        EXPECT_NE(Find("Timeline"), nullptr) << "the section stays with its note";
    }

    TEST_F(ProfilerPanelAssetTest, PauseAndWorkOnlyWriteTheViewAndAClickOnThemAgainTakesThemBack) {
        AddFrame(kSteadyFrameTicks);
        Settle();

        ASSERT_NO_FATAL_FAILURE(Click("###pause"));
        EXPECT_TRUE(View().paused);
        ASSERT_NO_FATAL_FAILURE(Click("###workOnly"));
        EXPECT_TRUE(View().workOnly);

        ASSERT_NO_FATAL_FAILURE(Click("###pause"));
        EXPECT_FALSE(View().paused);
        EXPECT_TRUE(View().workOnly);
    }

    TEST_F(ProfilerPanelAssetTest, TheCustomBudgetNumberShowsOnlyForTheCustomChoiceAndADragIsClamped) {
        AddFrame(kSteadyFrameTicks);
        Settle();
        EXPECT_EQ(Find("##customBudget"), nullptr);

        ASSERT_NO_FATAL_FAILURE(Choose(0, "Custom"));
        EXPECT_EQ(View().budget, kCustomBudget);
        ASSERT_NE(Find("##customBudget"), nullptr);

        ASSERT_NO_FATAL_FAILURE(DragBy("##customBudget", kDragPixels));
        EXPECT_DOUBLE_EQ(View().customBudgetMs, kMaxCustomBudgetMs);
        ASSERT_NO_FATAL_FAILURE(DragBy("##customBudget", -kDragPixels));
        EXPECT_DOUBLE_EQ(View().customBudgetMs, kMinCustomBudgetMs);

        ASSERT_NO_FATAL_FAILURE(Choose(0, "33.3 ms"));
        EXPECT_EQ(Find("##customBudget"), nullptr);
    }

    TEST_F(ProfilerPanelAssetTest, TheBarModeComboWritesTheIndexOfTheChosenRow) {
        AddFrame(kSteadyFrameTicks);
        Settle();
        ASSERT_NO_FATAL_FAILURE(Choose(1, "Stage split"));
        EXPECT_EQ(View().barMode, 1);
    }

    TEST_F(ProfilerPanelAssetTest, TheZoneFilterTextReachesTheView) {
        AddFrame(kSteadyFrameTicks);
        ShowZoneSections();
        ASSERT_NO_FATAL_FAILURE(Click("##zoneFilter"));
        Type("Render");

        EXPECT_EQ(View().zoneFilter, "Render");
    }

    TEST_F(ProfilerPanelAssetTest, TheSpikeModeComboSwapsTheThresholdForTheFactorAndTheCaptureFollows) {
        AddFrame(kSteadyFrameTicks);
        ShowZoneSections();
        ASSERT_NO_FATAL_FAILURE(Click("Spikes"));
        EXPECT_NE(Find("##spikeThreshold"), nullptr);
        EXPECT_EQ(Find("##spikeFactor"), nullptr);
        EXPECT_EQ(_model->GetSpikes().GetSettings().mode, SpikeMode::AbsoluteMs);

        ASSERT_NO_FATAL_FAILURE(Choose(3, "Over the median"));
        EXPECT_EQ(Find("##spikeThreshold"), nullptr);
        EXPECT_NE(Find("##spikeFactor"), nullptr);
        EXPECT_EQ(_model->GetSpikes().GetSettings().mode, SpikeMode::TimesMedian);
    }

    TEST_F(ProfilerPanelAssetTest, LiveDoesNothingUntilASpikeIsOpenedAndThenGoesBackToTheLiveRing) {
        AddFrame(kSteadyFrameTicks);
        AddFrame(kSpikeFrameTicks);
        ShowZoneSections();
        ASSERT_NO_FATAL_FAILURE(Click("Spikes"));

        ASSERT_NO_FATAL_FAILURE(Click("Live"));
        EXPECT_FALSE(Clicked("profiler.spikeLive")) << "the button is disabled";

        _model->OpenSpike(0);
        Settle();
        ASSERT_TRUE(_model->GetOpenedSpike().has_value());
        ASSERT_NO_FATAL_FAILURE(Click("Live"));
        EXPECT_TRUE(Clicked("profiler.spikeLive"));
        EXPECT_FALSE(_model->GetOpenedSpike().has_value());
    }

    TEST_F(ProfilerPanelAssetTest, WriteWithNothingToWriteEndsInTheModelsMessageAndTouchesNoBackend) {
        ShowZoneSections();
        ASSERT_NO_FATAL_FAILURE(Click("Chrome trace"));
        ASSERT_NO_FATAL_FAILURE(Click("Write selected frame"));

        EXPECT_TRUE(Clicked("profiler.trace"));
        EXPECT_EQ(View().traceMessage, kNothingToWrite);
        EXPECT_FALSE(View().recording);
    }

    TEST_F(ProfilerPanelAssetTest, AButtonWithAWidthIsThatManyEmWide) {
        ShowZoneSections();
        ASSERT_NO_FATAL_FAILURE(Click("Chrome trace"));

        const ImGuiItemRecord* record = Find("Record");
        ASSERT_NE(record, nullptr);
        EXPECT_NEAR(record->maxX - record->minX, kRecordButtonEm * ImGui::GetFontSize(), kPixelTolerance);
    }

}  // namespace BECore::Tests
