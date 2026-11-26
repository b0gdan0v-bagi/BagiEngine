#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/PoolString/PoolStringMonitor.h>
#include <BECore/PoolStringTestSupport.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelInterpreter.h>
#include <Widgets/PoolStrings/PoolStringsPanelModel.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 1400.0f};
        constexpr ImVec2 kWindowSize{900.0f, 1300.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr StringView kModelSource = "pool";
        /// Far fewer than the strings of a large pool, far more than the table shows without scrolling
        constexpr size_t kMostRowsOnScreen = 40;
        constexpr size_t kManyStrings = 100000;
        constexpr size_t kHugeBytes = 100000;
        constexpr int kLengthColumn = 1;

        Path ShippedPanel(StringView file) {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / Path(file);
        }

        bool IsKnownCommand(StringView name) {
            return FindEditorCommand(name) != nullptr;
        }

        IntrusivePtr<PanelAsset> LoadShipped(StringView file) {
            const FunctionRef<bool(StringView)> knownCommand(IsKnownCommand);
            const FunctionRef<bool(StringView)> knownModel(IsKnownPanelModel);
            PanelValidationContext context;
            context.isKnownCommand = knownCommand;
            context.isKnownModel = knownModel;
            const PanelLoadResult loaded = LoadPanelAsset(ReadFile(ShippedPanel(file)), context);
            EXPECT_TRUE(loaded.issues.empty()) << String(file).c_str();
            return loaded.asset;
        }

        const PanelElement* FindElement(const Vector<IntrusivePtr<PanelElement>>& elements, StringView id) {
            for (const IntrusivePtr<PanelElement>& element : elements) {
                if (element->id == id) {
                    return element.Get();
                }
                if (const PanelElement* inner = FindElement(element->elements, id)) {
                    return inner;
                }
            }
            return nullptr;
        }

    }  // namespace

    /// The shipped `PoolStringsPanel.xml` drawn over `PoolStringsPanelModel` and its slots in an ImGui context with no backend, over a pool of
    /// its own
    class PoolStringsPanelAssetTest : public ::testing::Test {
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

            _asset = LoadShipped("PoolStringsPanel.xml");
            ASSERT_TRUE(_asset);
            _windowName = Format("{}###{}", _asset->title, _asset->id);
            _interpreter.Prepare(_asset);

            static_cast<void>(_pool.storage.Intern("panel_asset_first"));
            _model = New<PoolStringsPanelModel>();
            _model->SetMonitor(&_monitor);
            _model->Initialize();
            _model->OnOpenChanged(true);
            PoolStringsPanelModel* model = _model.Get();
            _handle = _sources.Register(kModelSource, [model] { return model->GetObject(); });
        }

        void TearDown() override {
            _handle.Reset();
            _model = {};
            _interpreter.Prepare(IntrusivePtr<PanelAsset>{});
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
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
            _model->EndUpdate();
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
                if (item.visible && StringView(item.label).find(part) != StringView::npos) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// A click on the centre of the item, which must be there
        void Click(StringView part) {
            const ImGuiItemRecord* item = Find(part);
            ASSERT_NE(item, nullptr) << String(part).c_str();
            ClickAt(ImVec2(item->CenterX(), item->CenterY()));
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

        void Refresh() {
            ASSERT_TRUE(_model->RunCommand("poolStrings.refresh", {}));
            Settle();
        }

        void Intern(StringView text) {
            static_cast<void>(_pool.storage.Intern(text));
        }

        /// The scrolling child window of the list of strings
        const ImGuiWindow* ContentsWindow() const {
            for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
                if (StringView(window->Name).find("##poolContents") != StringView::npos) {
                    return window;
                }
            }
            return nullptr;
        }

        /// A click on the header of a column of the list; ImGui tells no item of a header to the index
        void ClickHeader(int column) {
            const ImGuiWindow* window = ContentsWindow();
            ASSERT_NE(window, nullptr);
            ImGuiContext& context = *ImGui::GetCurrentContext();
            for (int i = 0; i < context.Tables.GetMapSize(); ++i) {
                const ImGuiTable* table = context.Tables.TryGetMapData(i);
                if (table != nullptr && table->InnerWindow == window) {
                    const ImGuiTableColumn& header = table->Columns[column];
                    ClickAt(ImVec2((header.MinX + header.MaxX) * 0.5f, window->Pos.y + ImGui::GetStyle().CellPadding.y + context.FontSize * 0.5f));
                    return;
                }
            }
            FAIL() << "the list of strings has no table";
        }

        IsolatedPool _pool;
        PoolStringMonitor _monitor{_pool.storage};
        IntrusivePtr<PoolStringsPanelModel> _model;
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

    TEST_F(PoolStringsPanelAssetTest, TheAssetIsTheHiddenWindowThatTheViewCommandOpens) {
        EXPECT_EQ(_asset->id, "PoolStrings");
        EXPECT_EQ(_asset->command, "view.poolStrings");
        EXPECT_FALSE(_asset->open);
        EXPECT_EQ(_windowName, "Pool Strings###PoolStrings");
    }

    TEST_F(PoolStringsPanelAssetTest, TheBlocksAreDrawnAndRefreshShowsTheBlockThatWasAdded) {
        Settle();
        EXPECT_NE(Find("Summary"), nullptr);
        EXPECT_NE(Find("Checks"), nullptr);
        EXPECT_NE(Find("Blocks"), nullptr);
        EXPECT_EQ(_model->GetSlots().GetDrawnBlockRows(), 1u);

        // Longer than a standard block, so it takes a block of its own
        static_cast<void>(_pool.storage.Intern(BlockText(PoolString::kBlockBytes + 1, 'b')));
        Settle();
        EXPECT_EQ(_model->GetSlots().GetDrawnBlockRows(), 1u);

        ASSERT_NO_FATAL_FAILURE(Click("Refresh"));
        EXPECT_EQ(_model->GetSlots().GetDrawnBlockRows(), 2u);
    }

    TEST_F(PoolStringsPanelAssetTest, TypingInTheSearchFieldNarrowsTheRows) {
        Intern("zeta_one");
        Intern("zeta_two");
        Intern("alpha");
        Settle();
        ASSERT_NO_FATAL_FAILURE(Refresh());
        EXPECT_EQ(_model->GetSlots().GetDrawnContentRows(), 4u);

        ASSERT_NO_FATAL_FAILURE(Click("##search"));
        ImGui::GetIO().AddInputCharactersUTF8("ZETA");
        Settle();
        EXPECT_EQ(_model->GetView().search, "ZETA");
        EXPECT_EQ(_model->GetSlots().GetDrawnContentRows(), 2u);
        EXPECT_EQ(_model->GetView().matchText, "2 of 4 strings");
    }

    TEST_F(PoolStringsPanelAssetTest, AClickOnAHeaderSortsTheListAndASecondOneTurnsItRound) {
        Intern("zz");
        Settle();
        ASSERT_NO_FATAL_FAILURE(Refresh());
        const auto first = [this] { return _model->GetSnapshot().strings[_model->GetMatches().front()].text.ToStringView(); };
        EXPECT_EQ(first(), "panel_asset_first");

        ASSERT_NO_FATAL_FAILURE(ClickHeader(kLengthColumn));
        EXPECT_EQ(first(), "zz");
        ASSERT_NO_FATAL_FAILURE(ClickHeader(kLengthColumn));
        EXPECT_EQ(first(), "panel_asset_first");
    }

    TEST_F(PoolStringsPanelAssetTest, OfAHundredThousandStringsOnlyAScreenOfRowsIsDrawn) {
        for (size_t i = 0; i < kManyStrings; ++i) {
            Intern(CounterName("many_", i));
        }
        Settle();
        ASSERT_NO_FATAL_FAILURE(Refresh());

        EXPECT_EQ(_model->GetMatches().size(), kManyStrings + 1);
        EXPECT_GT(_model->GetSlots().GetDrawnContentRows(), 0u);
        EXPECT_LT(_model->GetSlots().GetDrawnContentRows(), kMostRowsOnScreen);
    }

    TEST_F(PoolStringsPanelAssetTest, AStringOfSeveralLinesOrAVeryLongOneTakesARowOfTheSameHeight) {
        for (size_t i = 0; i < kMostRowsOnScreen; ++i) {
            Intern(CounterName("row_", i));
        }
        Settle();
        ASSERT_NO_FATAL_FAILURE(Refresh());
        ASSERT_NE(ContentsWindow(), nullptr);
        const float before = ContentsWindow()->ContentSize.y;

        Intern("plain_one");
        Intern("plain_two");
        ASSERT_NO_FATAL_FAILURE(Refresh());
        const float withPlain = ContentsWindow()->ContentSize.y;
        ASSERT_GT(withPlain, before);

        // Both sort first, so they are on the screen; the list takes the height of every row from the first one
        Intern("a_0_several\nlines\nof\ntext");
        Intern(String("a_huge") + String(kHugeBytes, 'x'));
        ASSERT_NO_FATAL_FAILURE(Refresh());
        EXPECT_FLOAT_EQ(ContentsWindow()->ContentSize.y - withPlain, withPlain - before);
    }

    TEST(PoolStringsPanelMenuTest, TheViewMenuHasTheItemOfThePanel) {
        EngineEnvironment::EnsureCoreServices();
        const IntrusivePtr<PanelAsset> menu = LoadShipped("MainMenuPanel.xml");
        ASSERT_TRUE(menu);
        const PanelElement* element = FindElement(menu->elements, "poolStrings");
        ASSERT_NE(element, nullptr);
        const PanelMenuItem* item = element->Cast<PanelMenuItem>();
        ASSERT_NE(item, nullptr);
        EXPECT_EQ(item->command, "view.poolStrings");
    }

}  // namespace BECore::Tests
