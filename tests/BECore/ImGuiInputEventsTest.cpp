#include <BECore/Input/ImGuiInputEvents.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{800.0f, 600.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr ImVec2 kPanelSize{400.0f, 300.0f};
        constexpr float kListHeight = 80.0f;
        constexpr int kListLines = 60;
        constexpr int kSettleFrames = 3;
        /// More than any request of these tests needs to drain
        constexpr int kMaxDrainFrames = 30;
        constexpr size_t kFieldSize = 64;
        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        constexpr float kPixelDensity = 2.0f;

        ImGuiInputEvent MakeClick(float x, float y, int count = 1) {
            ImGuiInputEvent event;
            event.kind = ImGuiInputKind::Click;
            event.hasPosition = true;
            event.x = x;
            event.y = y;
            event.count = count;
            return event;
        }

        ImGuiInputEvent MakeKey(StringView name, ImGuiKeyAction action = ImGuiKeyAction::Press) {
            ImGuiInputEvent event;
            event.kind = ImGuiInputKind::Key;
            event.key = String{name};
            event.action = action;
            return event;
        }

        ImGuiInputEvent MakeText(StringView text) {
            ImGuiInputEvent event;
            event.kind = ImGuiInputKind::Text;
            event.text = String{text};
            return event;
        }

        ImGuiInputEvent MakeMove(float x, float y) {
            ImGuiInputEvent event;
            event.kind = ImGuiInputKind::Move;
            event.hasPosition = true;
            event.x = x;
            event.y = y;
            return event;
        }

        /// A context with no backend and one window: a button, a text field and a list that scrolls. Every frame leaves what the widgets
        /// saw in the members, so a test reads the reaction of ImGui and not its internals
        class ImGuiInputEventsTest : public ::testing::Test {
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
                io.Fonts->AddFontDefault();
                for (int i = 0; i < kSettleFrames; ++i) {
                    RunFrame();
                }
            }

            void TearDown() override {
                ImGui::DestroyContext();
            }

            void RunFrame() {
                ImGuiIO& io = ImGui::GetIO();
                io.DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                maxClickedCount = std::max(maxClickedCount, static_cast<int>(io.MouseClickedCount[0]));
                if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) {
                    ++chordPresses;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
                    ++enterPresses;
                }
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(kPanelSize);
                ImGui::Begin("Panel", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
                if (ImGui::Button("Press")) {
                    ++buttonPresses;
                }
                buttonCenter = Center();
                ImGui::InputText("##field", field, kFieldSize);
                fieldCenter = Center();
                ImGui::BeginChild("List", ImVec2(0.0f, kListHeight), ImGuiChildFlags_Borders);
                for (int i = 0; i < kListLines; ++i) {
                    ImGui::Text("line %d", i);
                }
                scrollY = ImGui::GetScrollY();
                ImGui::EndChild();
                listCenter = Center();
                ImGui::End();
                ImGui::Render();
            }

            /// The centre of the item submitted last, in ImGui units
            static ImVec2 Center() {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            }

            /// Runs frames until ImGui has taken the whole queue
            /// @return how many frames it took
            int Drain() {
                int frames = 0;
                while (!ImGuiInputQueueEmpty(ImGui::GetIO()) && frames < kMaxDrainFrames) {
                    RunFrame();
                    ++frames;
                }
                return frames;
            }

            [[nodiscard]] std::expected<void, String> Queue(const Vector<ImGuiInputEvent>& events, ImGuiInputSpace space = ImGuiInputSpace::Window) {
                return QueueImGuiEvents(ImGui::GetIO(), events, space);
            }

            int buttonPresses = 0;
            int chordPresses = 0;
            int enterPresses = 0;
            int maxClickedCount = 0;
            float scrollY = 0.0f;
            char field[kFieldSize] = {};
            ImVec2 buttonCenter;
            ImVec2 fieldCenter;
            ImVec2 listCenter;
        };

    }  // namespace

    TEST_F(ImGuiInputEventsTest, AClickPressesTheButtonOnceAfterTheQueueDrained) {
        ASSERT_TRUE(Queue({MakeClick(buttonCenter.x, buttonCenter.y)}));
        EXPECT_FALSE(ImGuiInputQueueEmpty(ImGui::GetIO()));

        const int frames = Drain();

        EXPECT_LT(frames, kMaxDrainFrames);
        EXPECT_TRUE(ImGuiInputQueueEmpty(ImGui::GetIO()));
        EXPECT_EQ(buttonPresses, 1);
        RunFrame();
        EXPECT_EQ(buttonPresses, 1);
    }

    TEST_F(ImGuiInputEventsTest, ADoubleClickIsTwoPressesThatImGuiReadsFromItsClock) {
        ASSERT_TRUE(Queue({MakeClick(buttonCenter.x, buttonCenter.y, 2)}));
        Drain();

        EXPECT_EQ(maxClickedCount, 2);
        EXPECT_EQ(buttonPresses, 2);
    }

    TEST_F(ImGuiInputEventsTest, ASingleClickIsNotADoubleClick) {
        ASSERT_TRUE(Queue({MakeClick(buttonCenter.x, buttonCenter.y)}));
        Drain();

        EXPECT_EQ(maxClickedCount, 1);
    }

    TEST_F(ImGuiInputEventsTest, TheWheelScrollsTheWindowUnderThePointer) {
        ImGuiInputEvent wheel;
        wheel.kind = ImGuiInputKind::Wheel;
        wheel.dy = -2.0f;
        ASSERT_TRUE(Queue({MakeMove(listCenter.x, listCenter.y), wheel}));
        Drain();
        RunFrame();

        EXPECT_GT(scrollY, 0.0f);
    }

    TEST_F(ImGuiInputEventsTest, AKeyNameIsMatchedWithoutRegardToLetterCase) {
        ASSERT_TRUE(Queue({MakeKey("enter")}));
        Drain();
        RunFrame();
        EXPECT_EQ(enterPresses, 1);

        ASSERT_TRUE(Queue({MakeKey("ENTER")}));
        Drain();
        EXPECT_EQ(enterPresses, 2);
    }

    TEST_F(ImGuiInputEventsTest, ACtrlChordReachesTheCodeThatAsksForCtrlAndTheKey) {
        ImGuiInputEvent chord = MakeKey("A");
        chord.mods.ctrl = true;
        ASSERT_TRUE(Queue({chord}));
        Drain();
        RunFrame();

        EXPECT_EQ(chordPresses, 1);
        // The modifier is let go again: a later plain key is not a chord
        EXPECT_FALSE(ImGui::GetIO().KeyCtrl);
        ASSERT_TRUE(Queue({MakeKey("A")}));
        Drain();
        RunFrame();
        EXPECT_EQ(chordPresses, 1);
    }

    TEST_F(ImGuiInputEventsTest, TextLandsInTheFocusedField) {
        ASSERT_TRUE(Queue({MakeClick(fieldCenter.x, fieldCenter.y)}));
        Drain();
        RunFrame();
        ASSERT_TRUE(ImGui::GetIO().WantTextInput);

        ASSERT_TRUE(Queue({MakeText("h\xC3\xA9llo")}));
        Drain();
        RunFrame();

        EXPECT_EQ(StringView{field}, "h\xC3\xA9llo");
    }

    TEST_F(ImGuiInputEventsTest, OutputPixelsAreDividedByTheFramebufferScale) {
        ImGui::GetIO().DisplayFramebufferScale = ImVec2(kPixelDensity, kPixelDensity);

        ASSERT_TRUE(Queue({MakeMove(buttonCenter.x * kPixelDensity, buttonCenter.y * kPixelDensity)}, ImGuiInputSpace::Output));
        Drain();

        EXPECT_FLOAT_EQ(ImGui::GetIO().MousePos.x, std::floor(buttonCenter.x));
        EXPECT_FLOAT_EQ(ImGui::GetIO().MousePos.y, std::floor(buttonCenter.y));

        ASSERT_TRUE(Queue({MakeClick(buttonCenter.x * kPixelDensity, buttonCenter.y * kPixelDensity)}, ImGuiInputSpace::Output));
        Drain();
        EXPECT_EQ(buttonPresses, 1);
    }

    TEST_F(ImGuiInputEventsTest, WindowUnitsAreTakenAsTheyAre) {
        ImGui::GetIO().DisplayFramebufferScale = ImVec2(kPixelDensity, kPixelDensity);

        ASSERT_TRUE(Queue({MakeClick(buttonCenter.x, buttonCenter.y)}, ImGuiInputSpace::Window));
        Drain();

        EXPECT_EQ(buttonPresses, 1);
    }

    TEST_F(ImGuiInputEventsTest, ABadEventQueuesNothingNotEvenTheGoodOnesBeforeIt) {
        const String badUtf8{"ab\xC3\x28"};
        ImGuiInputEvent overlong;
        overlong.kind = ImGuiInputKind::Text;
        overlong.text = String{"\xC0\xAF"};
        ImGuiInputEvent nul = MakeText("a");
        nul.text.push_back('\0');
        nul.text.push_back('b');

        const Vector<ImGuiInputEvent> bad[] = {
            {MakeMove(1.0f, 2.0f), MakeKey("NoSuchKey")},
            {MakeMove(1.0f, 2.0f), MakeMove(kNaN, 2.0f)},
            {MakeMove(1.0f, 2.0f), MakeMove(2.0e6f, 2.0f)},
            {MakeMove(1.0f, 2.0f), MakeClick(1.0f, std::numeric_limits<float>::infinity())},
            {MakeMove(1.0f, 2.0f), MakeText(badUtf8)},
            {MakeMove(1.0f, 2.0f), overlong},
            {MakeMove(1.0f, 2.0f), nul},
            {MakeMove(1.0f, 2.0f), MakeText("")},
            {MakeMove(1.0f, 2.0f), MakeClick(1.0f, 2.0f, 0)},
            {MakeMove(1.0f, 2.0f), MakeClick(1.0f, 2.0f, kMaxImGuiClickCount + 1)},
        };
        for (const Vector<ImGuiInputEvent>& request : bad) {
            const auto result = Queue(request);
            EXPECT_FALSE(result);
            if (!result) {
                EXPECT_NE(result.error().find("events[1]"), String::npos) << result.error();
            }
            EXPECT_TRUE(ImGuiInputQueueEmpty(ImGui::GetIO()));
        }
    }

    TEST_F(ImGuiInputEventsTest, AMoveWithoutAPositionAndModsOnAKeyReleaseAreRefused) {
        ImGuiInputEvent move;
        move.kind = ImGuiInputKind::Move;
        EXPECT_FALSE(Queue({move}));

        ImGuiInputEvent keyUp = MakeKey("A", ImGuiKeyAction::Up);
        keyUp.mods.shift = true;
        EXPECT_FALSE(Queue({keyUp}));
        EXPECT_TRUE(ImGuiInputQueueEmpty(ImGui::GetIO()));
    }

    TEST_F(ImGuiInputEventsTest, ARequestOverTheLimitOrWithoutEventsIsRefused) {
        Vector<ImGuiInputEvent> many(kMaxImGuiInputEvents + 1, MakeKey("A"));
        EXPECT_FALSE(Queue(many));
        EXPECT_FALSE(Queue({}));
        many.pop_back();
        EXPECT_TRUE(ValidateImGuiEvents(many));
        EXPECT_TRUE(ImGuiInputQueueEmpty(ImGui::GetIO()));
    }

    TEST_F(ImGuiInputEventsTest, ATextOverTheLimitIsRefused) {
        const String text(kMaxImGuiInputTextBytes + 1, 'a');
        EXPECT_FALSE(Queue({MakeText(text)}));
        EXPECT_TRUE(ImGuiInputQueueEmpty(ImGui::GetIO()));
    }

}  // namespace BECore::Tests
