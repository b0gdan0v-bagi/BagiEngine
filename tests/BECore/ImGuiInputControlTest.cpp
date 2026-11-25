#include <BECore/Control/ControlService.h>
#include <BECore/Input/ImGuiInputControl.h>
#include <BECore/Input/ImGuiInputEvents.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/ImGuiWidget.h>
#include <cmath>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{800.0f, 600.0f};
        constexpr float kPixelDensity = 2.0f;
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        /// What the SDL backend would measure between two requests of a client: far above a manual frame
        constexpr float kWallSeconds = 0.4f;
        constexpr ImVec2 kPanelSize{400.0f, 300.0f};
        constexpr float kListHeight = 80.0f;
        constexpr int kListRows = 40;
        constexpr int kSettleFrames = 3;
        constexpr int kFrameGuard = 200;
        constexpr size_t kFieldSize = 64;
        constexpr double kTolerance = 0.01;
        constexpr int kSmallLimit = 5;
        /// A point of the display that no window covers, in output pixels
        constexpr double kEmptyPixelX = 1500.0;
        constexpr double kEmptyPixelY = 1100.0;

        struct Rect {
            ImVec2 min;
            ImVec2 max;

            [[nodiscard]] ImVec2 Center() const {
                return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            }
        };

        Rect LastItemRect() {
            return {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        }

        String ClickParams(double x, double y, StringView extra = {}) {
            return Format("{{\"events\":[{{\"type\":\"click\",\"x\":{},\"y\":{}{}}}]}}", x, y, extra);
        }

        String MoveParams(double x, double y) {
            return Format("{{\"events\":[{{\"type\":\"move\",\"x\":{},\"y\":{}}}]}}", x, y);
        }

        /// An editor-like frame: the order of `Application::RunFrame` (ImGui's frame opens, the commands run, the widgets submit, the frame ends)
        /// over a window with a button, a checkbox, a text field and a list that scrolls, on a display of pixel density 2
        class ImGuiInputControlTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = kDisplaySize;
                io.DisplayFramebufferScale = ImVec2(kPixelDensity, kPixelDensity);
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                io.Fonts->AddFontDefault();
                RegisterImGuiInputControl(service);
                for (int i = 0; i < kSettleFrames; ++i) {
                    RunFrame();
                }
            }

            void TearDown() override {
                ImGui::DestroyContext();
            }

            void RunFrame() {
                ImGuiIO& io = ImGui::GetIO();
                io.DeltaTime = ImGuiFrameSeconds(true, static_cast<double>(kFrameSeconds), kWallSeconds);
                ImGui::NewFrame();
                maxClickedCount = std::max(maxClickedCount, static_cast<int>(io.MouseClickedCount[0]));
                if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) {
                    ++chordPresses;
                }
                service.Drain();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(kPanelSize);
                ImGui::Begin("Panel", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
                if (ImGui::Button("Press")) {
                    ++buttonPresses;
                }
                buttonId = ImGui::GetItemID();
                button = LastItemRect();
                ImGui::Checkbox("Check##box", &checked);
                ImGui::InputText("##field", field, kFieldSize);
                fieldRect = LastItemRect();
                ImGui::BeginChild("List", ImVec2(0.0f, kListHeight), ImGuiChildFlags_Borders);
                for (int row = 0; row < kListRows; ++row) {
                    bool value = false;
                    ImGui::Checkbox(Format("row {}", row).c_str(), &value);
                }
                scrollY = ImGui::GetScrollY();
                ImGui::EndChild();
                listRect = LastItemRect();
                ImGui::End();
                ImGui::Render();
                service.EndFrame();
            }

            /// Posts the request and runs frames until it is answered
            ControlReply Call(StringView method, StringView params) {
                const size_t before = responses.size();
                service.Post(MakeRequestText(++lastId, method, params), [this](const String& text) { responses.emplace_back(StringView(text.c_str(), text.size())); });
                for (int frame = 0; frame < kFrameGuard && responses.size() == before; ++frame) {
                    RunFrame();
                }
                return responses.size() > before ? std::move(responses.back()) : ControlReply();
            }

            /// The answer of a request that fails before any frame is needed
            ControlReply CallNow(StringView method, StringView params) {
                return CallControlText(service, ++lastId, method, params);
            }

            static Vector<JsonView> MatchesOf(const ControlReply& reply) {
                Vector<JsonView> matches;
                for (const JsonView item : MemberOf(reply.Result(), "matches").Items()) {
                    matches.push_back(item);
                }
                return matches;
            }

            static bool HasLabel(const Vector<JsonView>& matches, StringView label) {
                for (const JsonView match : matches) {
                    if (StringOf(match, "label") == label) {
                        return true;
                    }
                }
                return false;
            }

            ControlService service;
            Vector<ControlReply> responses;
            int64_t lastId = 0;
            int buttonPresses = 0;
            int chordPresses = 0;
            int maxClickedCount = 0;
            bool checked = false;
            float scrollY = 0.0f;
            ImGuiID buttonId = 0;
            Rect button;
            Rect fieldRect;
            Rect listRect;
            char field[kFieldSize] = {};
        };

    }  // namespace

    TEST(ImGuiFrameSecondsTest, TheManualClockSetsTheFrameAndNeverZero) {
        EXPECT_FLOAT_EQ(ImGuiFrameSeconds(true, 0.25, kWallSeconds), 0.25f);
        EXPECT_FLOAT_EQ(ImGuiFrameSeconds(true, 0.0, kWallSeconds), kMinImGuiFrameSeconds);
        EXPECT_GT(ImGuiFrameSeconds(true, -1.0, kWallSeconds), 0.0f);
    }

    TEST(ImGuiFrameSecondsTest, TheRealClockKeepsWhatTheBackendMeasured) {
        EXPECT_FLOAT_EQ(ImGuiFrameSeconds(false, 0.25, kWallSeconds), kWallSeconds);
        EXPECT_FLOAT_EQ(ImGuiFrameSeconds(false, 0.0, 0.0f), 0.0f);
    }

    TEST_F(ImGuiInputControlTest, FindGivesTheRectOfAnItemInOutputPixels) {
        const ControlReply reply = Call("imgui_find", R"({"label":"Press"})");

        ASSERT_TRUE(reply.IsOk()) << String(reply.Text()).c_str();
        const Vector<JsonView> matches = MatchesOf(reply);
        ASSERT_EQ(matches.size(), 1u);
        EXPECT_EQ(StringOf(matches[0], "kind"), "item");
        EXPECT_EQ(StringOf(matches[0], "window"), "Panel");
        EXPECT_EQ(StringOf(matches[0], "id"), Format("0x{:08X}", buttonId));
        EXPECT_NEAR(NumberOf(matches[0], "x"), static_cast<double>(button.min.x) * kPixelDensity, kTolerance);
        EXPECT_NEAR(NumberOf(matches[0], "y"), static_cast<double>(button.min.y) * kPixelDensity, kTolerance);
        EXPECT_NEAR(NumberOf(matches[0], "width"), static_cast<double>(button.max.x - button.min.x) * kPixelDensity, kTolerance);
        EXPECT_NEAR(NumberOf(matches[0], "height"), static_cast<double>(button.max.y - button.min.y) * kPixelDensity, kTolerance);
        EXPECT_EQ(BoolOf(matches[0], "visible"), true);
        const JsonView display = MemberOf(reply.Result(), "display");
        EXPECT_NEAR(NumberOf(display, "scaleX"), static_cast<double>(kPixelDensity), kTolerance);
        EXPECT_NEAR(NumberOf(display, "width"), static_cast<double>(kDisplaySize.x * kPixelDensity), kTolerance);
        EXPECT_FALSE(MemberOf(reply.Result(), "truncated").IsValid());
    }

    TEST_F(ImGuiInputControlTest, FindWithNoFilterListsTheWindowsOnly) {
        const ControlReply reply = Call("imgui_find", "{}");

        ASSERT_TRUE(reply.IsOk()) << String(reply.Text()).c_str();
        const Vector<JsonView> matches = MatchesOf(reply);
        ASSERT_FALSE(matches.empty());
        bool panel = false;
        for (const JsonView match : matches) {
            EXPECT_EQ(StringOf(match, "kind"), "window");
            panel = panel || StringOf(match, "label") == "Panel";
        }
        EXPECT_TRUE(panel);
    }

    TEST_F(ImGuiInputControlTest, FindMatchesTheWholeLabelItsVisiblePartItsIdAndAPartOfIt) {
        EXPECT_EQ(MatchesOf(Call("imgui_find", R"({"label":"Check"})")).size(), 1u);
        EXPECT_EQ(MatchesOf(Call("imgui_find", R"({"label":"Check##box"})")).size(), 1u);
        EXPECT_EQ(MatchesOf(Call("imgui_find", R"({"label":"Chec"})")).size(), 0u);
        EXPECT_EQ(MatchesOf(Call("imgui_find", R"({"labelContains":"heck"})")).size(), 1u);

        const ControlReply byId = Call("imgui_find", Format(R"({{"id":"{:x}"}})", buttonId));
        ASSERT_TRUE(byId.IsOk()) << String(byId.Text()).c_str();
        const Vector<JsonView> matches = MatchesOf(byId);
        ASSERT_EQ(matches.size(), 1u);
        EXPECT_EQ(StringOf(matches[0], "label"), "Press");
    }

    TEST_F(ImGuiInputControlTest, FindNarrowsByWindowAndKindAndAnUnknownNameIsAnEmptyList) {
        const ControlReply inPanelReply = Call("imgui_find", R"({"window":"Panel"})");
        const Vector<JsonView> inPanel = MatchesOf(inPanelReply);
        EXPECT_TRUE(HasLabel(inPanel, "Press"));
        EXPECT_TRUE(HasLabel(inPanel, "Panel"));

        const ControlReply itemsReply = Call("imgui_find", R"({"window":"Panel","kind":"item"})");
        const Vector<JsonView> itemsOfPanel = MatchesOf(itemsReply);
        EXPECT_TRUE(HasLabel(itemsOfPanel, "Press"));
        EXPECT_FALSE(HasLabel(itemsOfPanel, "Panel"));

        const ControlReply none = Call("imgui_find", R"({"window":"Nowhere"})");
        ASSERT_TRUE(none.IsOk()) << String(none.Text()).c_str();
        EXPECT_TRUE(MemberOf(none.Result(), "matches").IsArray());
        EXPECT_EQ(MatchesOf(none).size(), 0u);
    }

    TEST_F(ImGuiInputControlTest, FindLeavesOutWhatIsScrolledOutWhenAskedAndCutsAtTheLimit) {
        const ControlReply allReply = Call("imgui_find", R"({"labelContains":"row ","limit":500})");
        const ControlReply visibleReply = Call("imgui_find", R"({"labelContains":"row ","limit":500,"visibleOnly":true})");
        const Vector<JsonView> all = MatchesOf(allReply);
        const Vector<JsonView> visible = MatchesOf(visibleReply);
        EXPECT_EQ(all.size(), static_cast<size_t>(kListRows));
        EXPECT_GT(visible.size(), 0u);
        EXPECT_LT(visible.size(), all.size());
        for (const JsonView match : visible) {
            EXPECT_EQ(BoolOf(match, "visible"), true);
        }

        const ControlReply cut = Call("imgui_find", Format(R"({{"labelContains":"row ","limit":{}}})", kSmallLimit));
        EXPECT_EQ(MatchesOf(cut).size(), static_cast<size_t>(kSmallLimit));
        EXPECT_EQ(BoolOf(cut.Result(), "truncated"), true);
    }

    TEST_F(ImGuiInputControlTest, FindRefusesWhatItCannotRead) {
        for (const char* params :
             {R"({"id":"zz"})", R"({"id":"123456789"})", R"({"kind":"panel"})", R"({"limit":0})", R"({"limit":501})", R"({"label":""})", R"({"label":3})", R"({"visibleOnly":"yes"})"}) {
            const ControlReply reply = CallNow("imgui_find", params);
            EXPECT_EQ(reply.ErrorCode(), "invalid_params") << params;
        }
    }

    TEST_F(ImGuiInputControlTest, AClickAtTheCentreOfAFoundItemPressesIt) {
        const ControlReply found = Call("imgui_find", R"({"label":"Press"})");
        const Vector<JsonView> matches = MatchesOf(found);
        ASSERT_EQ(matches.size(), 1u);
        const double x = NumberOf(matches[0], "x") + NumberOf(matches[0], "width") / 2.0;
        const double y = NumberOf(matches[0], "y") + NumberOf(matches[0], "height") / 2.0;

        const ControlReply reply = Call("imgui_input", ClickParams(x, y));

        ASSERT_TRUE(reply.IsOk()) << String(reply.Text()).c_str();
        EXPECT_EQ(buttonPresses, 1);
        EXPECT_EQ(IntOf(reply.Result(), "events"), 1);
        // A click is a move, a press and a release, one change of the button per frame, and a frame after them
        EXPECT_GE(IntOf(reply.Result(), "frames").value_or(0), 3);
        EXPECT_TRUE(ImGuiInputQueueEmpty(ImGui::GetIO()));
    }

    TEST_F(ImGuiInputControlTest, ACountOfTwoMakesTwoPresses) {
        const ImVec2 center = button.Center();
        const ControlReply reply = Call("imgui_input", ClickParams(center.x * kPixelDensity, center.y * kPixelDensity, R"(,"count":2)"));

        ASSERT_TRUE(reply.IsOk()) << String(reply.Text()).c_str();
        EXPECT_EQ(buttonPresses, 2);
    }

    TEST_F(ImGuiInputControlTest, ADoubleClickIsOneToImGuiOnTheClockOfTheFrames) {
        const ImVec2 center = button.Center();
        const ControlReply reply = Call("imgui_input", ClickParams(center.x * kPixelDensity, center.y * kPixelDensity, R"(,"count":2)"));

        ASSERT_TRUE(reply.IsOk()) << String(reply.Text()).c_str();
        EXPECT_EQ(maxClickedCount, 2);
    }

    TEST_F(ImGuiInputControlTest, TheImGuiSpaceTakesPositionsInImGuiUnits) {
        const ImVec2 center = button.Center();
        const ControlReply reply = Call("imgui_input", Format(R"({{"space":"imgui","events":[{{"type":"click","x":{},"y":{}}}]}})", center.x, center.y));

        ASSERT_TRUE(reply.IsOk()) << String(reply.Text()).c_str();
        EXPECT_EQ(buttonPresses, 1);
    }

    TEST_F(ImGuiInputControlTest, ACtrlChordReachesTheCodeThatAsksForIt) {
        const ControlReply reply = Call("imgui_input", R"({"events":[{"type":"key","key":"A","mods":["ctrl"]}]})");

        ASSERT_TRUE(reply.IsOk()) << String(reply.Text()).c_str();
        EXPECT_EQ(chordPresses, 1);
    }

    TEST_F(ImGuiInputControlTest, TextGoesIntoTheFocusedField) {
        const ImVec2 center = fieldRect.Center();
        const ControlReply focus = Call("imgui_input", ClickParams(center.x * kPixelDensity, center.y * kPixelDensity));
        ASSERT_TRUE(focus.IsOk()) << String(focus.Text()).c_str();
        EXPECT_EQ(BoolOf(focus.Result(), "wantTextInput"), true);
        EXPECT_EQ(BoolOf(focus.Result(), "wantCaptureKeyboard"), true);

        const ControlReply typed = Call("imgui_input", R"({"events":[{"type":"text","text":"abc"}]})");

        ASSERT_TRUE(typed.IsOk()) << String(typed.Text()).c_str();
        EXPECT_STREQ(field, "abc");
    }

    TEST_F(ImGuiInputControlTest, TheWheelScrollsTheListUnderThePointer) {
        const ImVec2 center = listRect.Center();
        const ControlReply reply = Call("imgui_input", Format(R"({{"events":[{{"type":"move","x":{},"y":{}}},{{"type":"wheel","dy":-2}}]}})", center.x * kPixelDensity, center.y * kPixelDensity));

        ASSERT_TRUE(reply.IsOk()) << String(reply.Text()).c_str();
        EXPECT_GT(scrollY, 0.0f);
    }

    TEST_F(ImGuiInputControlTest, TheAnswerSaysWhetherImGuiTookThePointer) {
        const ImVec2 center = button.Center();
        const ControlReply over = Call("imgui_input", MoveParams(center.x * kPixelDensity, center.y * kPixelDensity));
        ASSERT_TRUE(over.IsOk()) << String(over.Text()).c_str();
        EXPECT_EQ(BoolOf(over.Result(), "wantCaptureMouse"), true);

        const ControlReply away = Call("imgui_input", MoveParams(kEmptyPixelX, kEmptyPixelY));
        ASSERT_TRUE(away.IsOk()) << String(away.Text()).c_str();
        EXPECT_EQ(BoolOf(away.Result(), "wantCaptureMouse"), false);
    }

    TEST_F(ImGuiInputControlTest, ABadRequestIsRefusedWithTheIndexOfTheEventAndQueuesNothing) {
        const ControlReply badKey = CallNow("imgui_input", R"({"events":[{"type":"move","x":1,"y":1},{"type":"key","key":"NoSuchKey"}]})");
        EXPECT_EQ(badKey.ErrorCode(), "invalid_params");
        EXPECT_NE(badKey.ErrorMessage().find("events[1]"), String::npos) << badKey.ErrorMessage().c_str();
        EXPECT_TRUE(ImGuiInputQueueEmpty(ImGui::GetIO()));

        for (const char* params : {"{}", R"({"events":{}})", R"({"events":[]})", R"({"events":[3]})", R"({"events":[{"type":"jump"}]})", R"({"events":[{"type":"move","x":1}]})",
                                   R"({"events":[{"type":"move","x":1e30,"y":0}]})", R"({"events":[{"type":"click","button":"back"}]})", R"({"events":[{"type":"click","count":4}]})",
                                   R"({"events":[{"type":"text","text":""}]})", R"({"events":[{"type":"key","key":"A","mods":["hyper"]}]})", R"({"space":"screen","events":[{"type":"up"}]})"}) {
            const ControlReply reply = CallNow("imgui_input", params);
            EXPECT_EQ(reply.ErrorCode(), "invalid_params") << params;
            EXPECT_TRUE(ImGuiInputQueueEmpty(ImGui::GetIO())) << params;
        }
    }

    TEST_F(ImGuiInputControlTest, WithoutAContextBothMethodsAnswerNoImGui) {
        ImGuiContext* context = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(nullptr);

        const ControlReply input = CallNow("imgui_input", MoveParams(1.0, 1.0));
        const ControlReply find = CallNow("imgui_find", "{}");
        ImGui::SetCurrentContext(context);

        EXPECT_EQ(input.ErrorCode(), "no_imgui");
        EXPECT_EQ(find.ErrorCode(), "no_imgui");
    }

    TEST_F(ImGuiInputControlTest, InputGivesUpWhenImGuiTakesNoFrames) {
        service.Post(MakeRequestText(++lastId, "imgui_input", ClickParams(1.0, 1.0)), [this](const String& text) { responses.emplace_back(StringView(text.c_str(), text.size())); });

        // The frames of the application end, but ImGui's own frame never starts, so its queue never drains
        for (uint32_t frame = 0; frame < kMaxImGuiWaitFrames + 2 && responses.empty(); ++frame) {
            service.Drain();
            service.EndFrame();
        }

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "imgui_timeout");
    }

    TEST_F(ImGuiInputControlTest, FindGivesUpAndDisarmsTheIndexWhenImGuiTakesNoFrames) {
        service.Post(MakeRequestText(++lastId, "imgui_find", "{}"), [this](const String& text) { responses.emplace_back(StringView(text.c_str(), text.size())); });

        for (uint32_t frame = 0; frame < kMaxImGuiWaitFrames + 2 && responses.empty(); ++frame) {
            service.Drain();
            service.EndFrame();
        }

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "imgui_timeout");
        EXPECT_FALSE(ImGui::GetCurrentContext()->TestEngineHookItems);
    }

    TEST_F(ImGuiInputControlTest, BothMethodsAreListedWithTheirParams) {
        const Vector<ControlMethodListing> methods = service.GetMethods();
        const ControlMethodListing* input = nullptr;
        const ControlMethodListing* find = nullptr;
        for (const ControlMethodListing& method : methods) {
            if (method.name == "imgui_input") {
                input = &method;
            } else if (method.name == "imgui_find") {
                find = &method;
            }
        }
        ASSERT_NE(input, nullptr);
        ASSERT_NE(find, nullptr);
        ASSERT_NE(input->info, nullptr);
        ASSERT_NE(find->info, nullptr);

        auto names = [](const ControlMethodListing& method) {
            Vector<String> result;
            for (const ControlParamInfo& param : method.info->params) {
                result.push_back(param.name);
            }
            return result;
        };
        EXPECT_EQ(names(*input), (Vector<String>{"events", "space"}));
        EXPECT_EQ(names(*find), (Vector<String>{"label", "labelContains", "id", "window", "kind", "visibleOnly", "limit"}));
        EXPECT_TRUE(input->info->params[0].required);
    }

}  // namespace BECore::Tests
