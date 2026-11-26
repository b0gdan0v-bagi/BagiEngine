#include <BECore/Clipboard/Clipboard.h>
#include <BECore/Clipboard/FakeClipboard.h>
#include <BECore/RefCounted/New.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLEvents.h>
#include <Widgets/EditorLayout/BrowserClipboardBridge.h>
#include <Widgets/EditorLayout/EditorBrowserInput.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kField = 7;
        constexpr uint32_t kOtherField = 8;
        /// Polls a deferred read of the fake says "not yet" to
        constexpr int kDeferredPolls = 3;
        /// Frames in which a replayed press, spread by ImGui's event queue, has surely run
        constexpr int kPasteFrames = 12;
        constexpr size_t kFieldBytes = 128;

        class ProbePasteTarget : public IPasteTarget {
        public:
            [[nodiscard]] uint32_t GetFocusedField() const override {
                return field;
            }

            [[nodiscard]] int GetFrame() const override {
                return frame;
            }

            void ReplayPaste() override {
                ++replays;
            }

            uint32_t field = kField;
            int frame = 100;
            int replays = 0;
        };

    }  // namespace

    /// A bridge over a clipboard of its own with the fake backend
    class BrowserClipboardBridgeTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            fake = New<FakeClipboard>("copied");
            fake->SetDeferred(kDeferredPolls);
            clipboard.SetBackend(IntrusivePtr<IClipboardBackend>(fake));
        }

        void Answer() {
            for (int i = 0; i <= kDeferredPolls; ++i) {
                clipboard.Update();
            }
        }

        IntrusivePtr<FakeClipboard> fake;
        Clipboard clipboard;
        ProbePasteTarget target;
    };

    TEST_F(BrowserClipboardBridgeTest, APasteAnswersNothingThenReplaysAndGivesTheTextToTheReplayedPressOnce) {
        BrowserClipboardBridge bridge(clipboard, target);
        EXPECT_EQ(bridge.GetText(), nullptr);
        clipboard.Update();
        EXPECT_EQ(bridge.GetText(), nullptr) << "a press while the read runs";
        Answer();

        EXPECT_EQ(target.replays, 1);
        EXPECT_EQ(fake->ReadCount(), 1) << "presses share the read";
        target.frame += 2;
        const char* text = bridge.GetText();
        ASSERT_NE(text, nullptr);
        EXPECT_STREQ(text, "copied");
        EXPECT_EQ(bridge.GetText(), nullptr) << "the next press reads the clipboard again";
    }

    TEST_F(BrowserClipboardBridgeTest, ATextTheReplayedPressNeverAskedForIsNotGivenToALaterPaste) {
        BrowserClipboardBridge bridge(clipboard, target);
        EXPECT_EQ(bridge.GetText(), nullptr);
        Answer();
        target.frame += BrowserClipboardBridge::kReplayFrames + 1;

        EXPECT_EQ(bridge.GetText(), nullptr);
    }

    TEST_F(BrowserClipboardBridgeTest, ARefusedAnEmptyOrAnUnavailableReadChangesNothing) {
        BrowserClipboardBridge bridge(clipboard, target);
        fake->SetRefuse(true);
        EXPECT_EQ(bridge.GetText(), nullptr);
        Answer();
        fake->SetRefuse(false);
        fake->SetContent("");
        EXPECT_EQ(bridge.GetText(), nullptr);
        Answer();
        fake->SetAvailable(false);
        EXPECT_EQ(bridge.GetText(), nullptr);
        Answer();

        EXPECT_EQ(target.replays, 0);
    }

    TEST_F(BrowserClipboardBridgeTest, ATextForAFieldThatLostTheKeyboardIsDropped) {
        BrowserClipboardBridge bridge(clipboard, target);
        EXPECT_EQ(bridge.GetText(), nullptr);
        target.field = kOtherField;
        Answer();

        EXPECT_EQ(target.replays, 0);
        EXPECT_EQ(bridge.GetText(), nullptr);
    }

    TEST_F(BrowserClipboardBridgeTest, ABridgeThatDiesWithAReadInFlightIsNotCalledBack) {
        {
            BrowserClipboardBridge bridge(clipboard, target);
            EXPECT_EQ(bridge.GetText(), nullptr);
            clipboard.Update();
        }
        Answer();

        EXPECT_FALSE(clipboard.HasPending());
        EXPECT_EQ(target.replays, 0);
    }

    /// The editor's browser input over a real ImGui context with no backend: one text field, and the keys given as the SDL backend gives them
    class EditorBrowserInputTest : public ::testing::TestWithParam<bool> {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(640.0f, 480.0f);
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            fonts = LoadImGuiFonts(io, buffers);
            SetActiveImGuiFonts(fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            fake = New<FakeClipboard>("pasted");
            fake->SetDeferred(kDeferredPolls);
            clipboard.SetBackend(IntrusivePtr<IClipboardBackend>(fake));
            builtMacBehaviors = io.ConfigMacOSXBehaviors;
            input.emplace(clipboard, MacPage());
        }

        void TearDown() override {
            input.reset();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
        }

        static bool MacPage() {
            return GetParam();
        }

        /// The modifier of the page's shortcuts: Cmd on a Mac page
        static ImGuiKey PageModifier() {
            return MacPage() ? ImGuiMod_Super : ImGuiMod_Ctrl;
        }

        void Frame() {
            clipboard.Update();
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImGui::Begin("##Host");
            if (focus) {
                ImGui::SetKeyboardFocusHere();
                focus = false;
            }
            ImGui::InputText("##field", field, sizeof(field));
            ImGui::End();
            ImGui::Render();
        }

        void Frames(int count) {
            for (int i = 0; i < count; ++i) {
                Frame();
            }
        }

        static void Key(ImGuiKey key, bool down) {
            ImGui::GetIO().AddKeyEvent(key, down);
        }

        ImGuiFontBuffers buffers;
        ImGuiFonts fonts;
        IntrusivePtr<FakeClipboard> fake;
        Clipboard clipboard;
        Optional<EditorBrowserInput> input;
        char field[kFieldBytes] = {};
        bool focus = true;
        bool builtMacBehaviors = false;  // ImGui's own, before the editor's input changed it
    };

    TEST_P(EditorBrowserInputTest, APasteChordPutsTheLateTextIntoTheFieldOnceWhileItsKeysAreStillHeld) {
        Frames(3);
        Key(PageModifier(), true);
        Key(ImGuiKey_V, true);
        Frames(kPasteFrames);

        EXPECT_STREQ(field, "pasted");
        EXPECT_EQ(fake->ReadCount(), 1);
        EXPECT_FALSE(ImGui::GetIO().KeyCtrl) << "no keyboard holds a modifier here, so none may stay down";
        EXPECT_FALSE(ImGui::IsKeyDown(ImGuiKey_V));
    }

    TEST_P(EditorBrowserInputTest, CopyWritesTheSelectionToTheClipboard) {
        String("edited").copy(field, sizeof(field) - 1);
        Frames(3);
        Key(PageModifier(), true);
        Key(ImGuiKey_A, true);
        Frames(3);
        Key(ImGuiKey_A, false);
        Key(ImGuiKey_C, true);
        Frames(3);

        EXPECT_EQ(fake->Content(), "edited");
    }

    TEST_P(EditorBrowserInputTest, ARefusedPasteLeavesTheFieldAsItWas) {
        String("kept").copy(field, sizeof(field) - 1);
        fake->SetRefuse(true);
        Frames(3);
        Key(PageModifier(), true);
        Key(ImGuiKey_V, true);
        Frames(kPasteFrames);

        EXPECT_STREQ(field, "kept");
    }

    TEST_P(EditorBrowserInputTest, TheEndOfTheEditorGivesImGuiItsClipboardAndKeyboardBack) {
        const ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
        const auto installed = platform.Platform_GetClipboardTextFn;
        EXPECT_EQ(ImGui::GetIO().ConfigMacOSXBehaviors, MacPage());
        input.reset();

        EXPECT_NE(platform.Platform_GetClipboardTextFn, installed);
        EXPECT_EQ(platform.Platform_ClipboardUserData, nullptr);
        EXPECT_EQ(ImGui::GetIO().ConfigMacOSXBehaviors, builtMacBehaviors);
    }

    INSTANTIATE_TEST_SUITE_P(Pages, EditorBrowserInputTest, ::testing::Bool(), [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "Mac" : "Other"; });

    TEST(EditorBrowserKeysTest, TheEditorsKeysAndTheDefaultHotkeysAreTakenFromTheBrowserAndTheEditingKeysNever) {
        const String keys = EditorBrowserKeys(GetEditorCommands());
        for (const char key : StringView("spfd,lg")) {
            EXPECT_NE(keys.find(key), String::npos) << key;
        }
        for (const char key : StringView("cvx")) {
            EXPECT_EQ(keys.find(key), String::npos) << key;
        }
    }

    TEST(EditorBrowserKeysTest, AReleaseUnderCmdLetsTheSameShortcutBePressedAgain) {
        ImGuiContext* context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(640.0f, 480.0f);
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        const auto frame = [] {
            ImGui::NewFrame();
            const bool pressed = ImGui::IsKeyPressed(ImGuiKey_S, false);
            ImGui::EndFrame();
            return pressed;
        };
        io.AddKeyEvent(ImGuiKey_S, true);
        EXPECT_TRUE(frame());

        // The browser tells no release of S; Cmd goes up and the keys are released
        ReleaseKeysHeldUnderCommand(io);
        EXPECT_FALSE(frame());
        io.AddKeyEvent(ImGuiKey_S, true);
        EXPECT_TRUE(frame());
        ImGui::DestroyContext(context);
    }

}  // namespace BECore::Tests
