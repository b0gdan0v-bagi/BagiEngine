// The editor's motion: the values that move by the frame's time, the hover blend of a button, the fade of an asset panel and of the command palette.

#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/PanelHotReloadProbe.h>
#include <BECore/RefCounted/New.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/CommandPalette.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Feedback/UIMotion.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 720.0f};
        /// A few frames to a fade
        constexpr float kFrameSeconds = kHoverBlendSeconds / 4.0f;
        /// Longer than every fade, whatever the rounding of the frames' sum
        constexpr int kWholeFadeFrames = 10;
        constexpr float kTinyFrameSeconds = 1.0e-6f;
        constexpr int kSteadyFrames = 8;
        constexpr int kOpaque = 255;
        constexpr const char* kWindowName = "Moving###Moving";
        constexpr const char* kHostName = "##Host";
        constexpr StringView kConfigName = "MovingPanel";

        /// `{}` slot: the `open` attribute. More lines than the window is high, so it scrolls
        constexpr const char* kAsset = R"(<PanelAsset id="Moving" title="Moving" command="view.fileChanges" open="{}" width="300" height="100">
  <sources><source name="a" model="HookPanelModel"/></sources>
  <elements>
    <element type="PanelButton" id="go" text="Go" command="fileChanges.rescan"/>
    <element type="PanelText" id="one" text="one"/>
    <element type="PanelText" id="two" text="two"/>
    <element type="PanelText" id="three" text="three"/>
    <element type="PanelText" id="four" text="four"/>
    <element type="PanelText" id="five" text="five"/>
    <element type="PanelText" id="six" text="six"/>
  </elements>
</PanelAsset>)";

        /// An ImGui context with no backend
        class UIMotionImGuiTest : public ::testing::Test {
        protected:
            void SetUp() override {
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = kDisplaySize;
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                _fonts = LoadImGuiFonts(io, _buffers);
                SetActiveImGuiFonts(_fonts);
                ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            }

            void TearDown() override {
                ImGui::DestroyContext();
                SetActiveImGuiFonts({});
            }

            /// The alpha of the most opaque thing the window drew in the last frame; -1 for no window
            static int AlphaOf(const ImGuiWindow* window) {
                if (window == nullptr) {
                    return -1;
                }
                int alpha = 0;
                for (const ImDrawVert& vertex : window->DrawList->VtxBuffer) {
                    alpha = eastl::max(alpha, static_cast<int>((vertex.col & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT));
                }
                return alpha;
            }

            ImGuiFontBuffers _buffers;
            ImGuiFonts _fonts;
        };

        /// One button in a window of its own, seen through the colour of its frame
        class UIMotionButtonTest : public UIMotionImGuiTest {
        protected:
            /// Draws the button and returns the colour its frame was filled with
            ImU32 Frame(bool enabled = true) {
                ImGui::GetIO().DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(kDisplaySize);
                ImU32 colour = 0;
                if (ImGui::Begin(kHostName, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings)) {
                    ImGui::BeginDisabled(!enabled);
                    const int before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
                    static_cast<void>(BlendedButton("Press"));
                    // The first vertex of a button is the corner of its fill
                    colour = ImGui::GetWindowDrawList()->VtxBuffer[before].col;
                    _centre = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
                    ImGui::EndDisabled();
                }
                ImGui::End();
                ImGui::Render();
                return colour;
            }

            void Point(bool over) {
                const ImVec2 at = over ? _centre : ImVec2(kDisplaySize.x - 1.0f, kDisplaySize.y - 1.0f);
                ImGui::GetIO().AddMousePosEvent(at.x, at.y);
            }

            ImU32 Settled(bool enabled = true) {
                ImU32 colour = 0;
                for (int i = 0; i < kWholeFadeFrames; ++i) {
                    colour = Frame(enabled);
                }
                return colour;
            }

            ImVec2 _centre;
        };

        /// An asset panel over `HookPanelModel`, hosted by a real widget
        class UIMotionPanelTest : public UIMotionImGuiTest {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                UIMotionImGuiTest::SetUp();
                HookPanelModel::log.clear();
                _dir = TestTempDirectory("ui_motion");
                MakeEmptyDirectory(_dir);
                _index.Arm(*ImGui::GetCurrentContext());
            }

            void TearDown() override {
                _widget.Reset();
                _index.Disarm();
                UIMotionImGuiTest::TearDown();
                RemoveDirectory(_dir);
            }

            void Start(bool open) {
                WriteFile(_dir / "MovingPanel.xml", Format(RuntimeFormat(kAsset), open));
                ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
                _manager.Initialize();
                _widget = New<AssetPanelWidget>();
                _widget->SetConfigManager(_manager);
                _widget->SetAsset(kConfigName);
                const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(PoolString::Intern(kConfigName));
                ASSERT_TRUE(deserializer);
                ASSERT_TRUE(_widget->Initialize(*deserializer));
                ASSERT_TRUE(_widget->GetHolder().Issues().empty());
            }

            void Frame(float seconds = kFrameSeconds) {
                ImGui::GetIO().DeltaTime = seconds;
                ImGui::NewFrame();
                _widget->Update();
                ImGui::Render();
            }

            void Settle() {
                for (int i = 0; i < kWholeFadeFrames; ++i) {
                    Frame();
                }
            }

            static void Toggle() {
                EditorCommandEvent::Emit(EditorCommandIds::kViewFileChanges);
            }

            /// The panel's window; null before it was ever begun
            static ImGuiWindow* Window() {
                return ImGui::FindWindowByName(kWindowName);
            }

            /// Whether the last frame began the panel's window
            static bool Drawn() {
                return Window() != nullptr && Window()->Active;
            }

            bool HasButton() const {
                for (const ImGuiItemRecord& item : _index.Items()) {
                    if (item.visible && item.MatchesLabel("Go")) {
                        return true;
                    }
                }
                return false;
            }

            Path _dir;
            FileSystem _fileSystem;
            ConfigManager _manager{_fileSystem};
            ImGuiItemIndex _index;
            IntrusivePtr<AssetPanelWidget> _widget;
        };

        using UIMotionPaletteTest = UIMotionImGuiTest;

    }  // namespace

    TEST(UIMotionTest, AFadeStartsHiddenAndAFrameOfNextToNoTimeAlreadyShowsIt) {
        Fade fade;
        EXPECT_TRUE(fade.IsHidden());

        fade.Advance(true, kTinyFrameSeconds, kPanelFadeSeconds);

        EXPECT_FALSE(fade.IsHidden());
        EXPECT_GT(fade.Alpha(), 0.0f);
    }

    TEST(UIMotionTest, AFadeNeverPassesItsEndsAndATurnMidwayGoesBackFromWhereItIs) {
        Fade fade;
        fade.Advance(true, kPanelFadeSeconds * 0.5f, kPanelFadeSeconds);
        const float half = fade.Alpha();
        EXPECT_GT(half, 0.0f);
        EXPECT_LT(half, 1.0f);

        fade.Advance(false, kPanelFadeSeconds * 0.25f, kPanelFadeSeconds);
        EXPECT_FALSE(fade.IsHidden());
        EXPECT_LT(fade.Alpha(), half);

        fade.Advance(true, kPanelFadeSeconds * 2.0f, kPanelFadeSeconds);
        EXPECT_EQ(fade.Alpha(), 1.0f);
        fade.Advance(false, kPanelFadeSeconds * 2.0f, kPanelFadeSeconds);
        EXPECT_TRUE(fade.IsHidden());
        EXPECT_EQ(fade.Alpha(), 0.0f);
    }

    TEST(UIMotionTest, AnItemHoldsASlotOnlyWhileItIsAwayFromRest) {
        HoverBlendTable table;
        table.Advance(1, false, kHoverBlendSeconds, 1);
        EXPECT_EQ(table.Size(), 0u);

        table.Advance(1, true, kHoverBlendSeconds * 0.5f, 2);
        EXPECT_GT(table.Get(1, 3), 0.0f);
        EXPECT_LT(table.Get(1, 3), 1.0f);
        table.Advance(1, true, kHoverBlendSeconds, 3);
        EXPECT_EQ(table.Get(1, 4), 1.0f);

        table.Advance(1, false, kHoverBlendSeconds * 2.0f, 4);
        EXPECT_EQ(table.Size(), 0u);
        EXPECT_EQ(table.Get(1, 5), 0.0f);
    }

    TEST(UIMotionTest, AnItemThatMissedAFrameStartsFromRest) {
        HoverBlendTable table;
        table.Advance(1, true, kHoverBlendSeconds, 1);

        EXPECT_EQ(table.Get(1, 3), 0.0f);
        table.Advance(1, false, kHoverBlendSeconds * 0.25f, 3);
        EXPECT_EQ(table.Size(), 0u) << "nothing of the old blend fades out";
    }

    TEST(UIMotionTest, AFullTableStillBlendsANewItemAtTheCostOfTheFaintestOne) {
        HoverBlendTable table;
        const uint32_t faint = 1;
        table.Advance(faint, true, kHoverBlendSeconds * 0.25f, 1);
        for (uint32_t item = faint + 1; item <= HoverBlendTable::kCapacity; ++item) {
            table.Advance(item, true, kHoverBlendSeconds * 0.5f, 1);
        }
        const uint32_t newcomer = HoverBlendTable::kCapacity + 1;

        table.Advance(newcomer, true, kHoverBlendSeconds * 0.5f, 1);

        EXPECT_GT(table.Get(newcomer, 1), 0.0f);
        EXPECT_EQ(table.Get(faint, 1), 0.0f);
        EXPECT_GT(table.Get(faint + 1, 1), 0.0f);
    }

    TEST_F(UIMotionButtonTest, AButtonBlendsIntoItsHoverColourAndBack) {
        const ImU32 rest = Settled();
        const ImU32 hover = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
        ASSERT_NE(rest, hover);

        Point(true);
        const ImU32 first = Frame();
        const ImU32 second = Frame();
        EXPECT_EQ(first, rest) << "no snap on the frame the pointer arrives";
        EXPECT_NE(second, rest);
        EXPECT_NE(second, hover);
        EXPECT_EQ(Settled(), hover);

        Point(false);
        Frame();
        const ImU32 leaving = Frame();
        EXPECT_NE(leaving, rest);
        EXPECT_NE(leaving, hover);
        EXPECT_EQ(Settled(), rest);
    }

    TEST_F(UIMotionButtonTest, AButtonDisabledUnderThePointerKeepsNothingOfTheBlend) {
        const ImU32 disabledRest = Settled(false);
        Settled();
        Point(true);
        Settled();

        EXPECT_EQ(Frame(false), disabledRest);
    }

    TEST_F(UIMotionPanelTest, APanelOpenAtStartIsOpaqueAtOnce) {
        ASSERT_NO_FATAL_FAILURE(Start(true));

        // The first frame of a window is hidden by ImGui itself while it measures, the second is drawn
        Frame(kTinyFrameSeconds);
        Frame(kTinyFrameSeconds);

        EXPECT_EQ(AlphaOf(Window()), kOpaque);
    }

    TEST_F(UIMotionPanelTest, APanelFadesInAndOutAndIsGoneAfterTheFade) {
        ASSERT_NO_FATAL_FAILURE(Start(false));
        Frame();
        EXPECT_EQ(ImGui::FindWindowByName(kWindowName), nullptr);

        Toggle();
        Frame();
        Frame();
        const int opening = AlphaOf(Window());
        EXPECT_GT(opening, 0);
        EXPECT_LT(opening, kOpaque);
        Settle();
        EXPECT_EQ(AlphaOf(Window()), kOpaque);

        Toggle();
        HookPanelModel::log.clear();
        Frame();
        ASSERT_TRUE(Drawn());
        const int closing = AlphaOf(Window());
        EXPECT_LT(closing, kOpaque);
        EXPECT_FALSE(HasButton()) << "the body of a closing panel is empty";
        EXPECT_TRUE((Window()->Flags & ImGuiWindowFlags_NoMouseInputs) != 0);
        Frame();
        EXPECT_LT(AlphaOf(Window()), closing);
        Settle();
        EXPECT_FALSE(Drawn());
        EXPECT_EQ(HookPanelModel::log, "-") << "told once at the close, and neither updated nor ended while it fades";
    }

    TEST_F(UIMotionPanelTest, APanelReopenedMidwayComesBackFromWhereItWas) {
        ASSERT_NO_FATAL_FAILURE(Start(true));
        Settle();
        Toggle();
        Frame();
        const int closing = AlphaOf(Window());

        Toggle();
        Frame();

        EXPECT_GT(AlphaOf(Window()), closing);
        EXPECT_TRUE(HasButton());
    }

    TEST_F(UIMotionPanelTest, AClosedAndReopenedPanelKeepsItsScroll) {
        ASSERT_NO_FATAL_FAILURE(Start(true));
        Settle();
        ASSERT_NE(Window(), nullptr);
        ASSERT_GT(Window()->ScrollMax.y, 0.0f);
        ImGui::SetScrollY(Window(), Window()->ScrollMax.y);
        Settle();
        const float scroll = Window()->Scroll.y;

        Toggle();
        Settle();
        Toggle();
        Settle();

        EXPECT_EQ(Window()->Scroll.y, scroll);
    }

    TEST_F(UIMotionPanelTest, APanelAtRestAllocatesNothingOpenOrGone) {
        ASSERT_NO_FATAL_FAILURE(Start(false));
        _index.Disarm();
        Toggle();
        Settle();
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });

        Toggle();
        Settle();
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

    TEST_F(UIMotionPaletteTest, TheCommandPaletteFadesInWithItsQueryFocused) {
        CommandPalette palette;
        const HotkeyMap hotkeys;
        const auto frame = [&] {
            ImGui::GetIO().DeltaTime = kPaletteFadeSeconds / 4.0f;
            ImGui::NewFrame();
            if (ImGui::Begin(kHostName)) {
                static_cast<void>(palette.Draw(hotkeys));
            }
            ImGui::End();
            ImGui::Render();
        };
        const auto popup = [] { return ImGui::GetCurrentContext()->OpenPopupStack.empty() ? nullptr : ImGui::GetCurrentContext()->OpenPopupStack.back().Window; };

        palette.Open();
        frame();
        frame();
        ASSERT_NE(popup(), nullptr);
        EXPECT_LT(AlphaOf(popup()), kOpaque);
        for (int i = 0; i < kWholeFadeFrames; ++i) {
            frame();
        }
        ASSERT_NE(popup(), nullptr);
        EXPECT_EQ(AlphaOf(popup()), kOpaque);
        EXPECT_TRUE(ImGui::GetIO().WantTextInput);
    }

}  // namespace BECore::Tests
