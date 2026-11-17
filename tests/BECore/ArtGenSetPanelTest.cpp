#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>
#include <Widgets/ArtGen/Sets/ArtGenSetPanel.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr const char* kHostName = "##Host";
        /// How far apart the mouse is moved when a control is looked for on the screen
        constexpr float kSearchStep = 6.0f;
        constexpr float kSearchHeight = 700.0f;
        constexpr float kSearchWidth = 900.0f;

        String PathText(const std::filesystem::path& path) {
            const std::string text = path.generic_string();  //-NoStd: the text of a std::filesystem::path
            return String(text.c_str(), text.size());
        }

    }  // namespace

    /// The Set tab in an ImGui context of its own with no backend, driven by the mouse and the keyboard events like a person
    class ArtGenSetPanelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        ArtGenSetPanelTest() : session([this] { return now; }) {}

        void SetUp() override {
            _root = TestTempDirectory("artgen_set_panel");
            std::filesystem::remove_all(_root);
            std::filesystem::create_directories(_root);
            session.SetGameRoot(PathText(_root));
            form.New("workflow");

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            fonts = LoadImGuiFonts(io, buffers);
            SetActiveImGuiFonts(fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            Frame();
        }

        void TearDown() override {
            panel = ArtGenSetPanel();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            std::error_code error;
            std::filesystem::remove_all(_root, error);
        }

        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(kDisplaySize);
            constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
            ImGui::Begin(kHostName, nullptr, hostFlags);
            panel.DrawQueue(session, ArtGenSetPanelForm{form});
            ImGui::End();
            ImGui::Render();
        }

        static ImGuiID HostId() {
            return ImGui::FindWindowByName(kHostName)->ID;
        }

        /// The id of the first row's Subject field
        static ImGuiID SubjectId() {
            const int row = 0;
            return ImHashStr("##subject", 0, ImHashData(&row, sizeof(row), HostId()));
        }

        static ImGuiID QueueId() {
            return ImHashStr(ICON_LC_LIST_PLUS " Queue", 0, HostId());
        }

        /// A point of the screen over the control @p id: the mouse is moved over a grid until ImGui says it is over it
        ImVec2 Locate(ImGuiID id) {
            ImGuiIO& io = ImGui::GetIO();
            for (float y = 0.0f; y < kSearchHeight; y += kSearchStep) {
                for (float x = 0.0f; x < kSearchWidth; x += kSearchStep) {
                    io.AddMousePosEvent(x, y);
                    Frame();
                    Frame();
                    if (ImGui::GetCurrentContext()->HoveredId == id) {
                        return ImVec2(x, y);
                    }
                }
            }
            ADD_FAILURE() << "control not found on the screen";
            return ImVec2(0.0f, 0.0f);
        }

        void ClickAt(ImVec2 point) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Frame();
        }

        /// Clicks the first row's Subject and types @p text there
        void TypeSubject(const char* text) {
            ClickAt(Locate(SubjectId()));
            ImGui::GetIO().AddInputCharactersUTF8(text);
            Frame();
            Frame();
        }

        /// Presses Queue where it is; true when that queued a set
        bool PressQueue() {
            const ImVec2 point = Locate(QueueId());
            const size_t before = session.GetSets().size();
            ClickAt(point);
            return session.GetSets().size() > before;
        }

        double now = 0.0;
        ArtGenSession session;
        ArtGenPresetForm form;
        ArtGenSetPanel panel;
        ImGuiFontBuffers buffers;
        ImGuiFonts fonts;

    private:
        std::filesystem::path _root;
    };

    TEST_F(ArtGenSetPanelTest, ASecondSetCanBeQueuedAfterTheFirst) {
        TypeSubject("queen");
        EXPECT_TRUE(PressQueue());
        ASSERT_EQ(session.GetSets().size(), 1u);

        TypeSubject("white king");
        EXPECT_TRUE(PressQueue());
        EXPECT_EQ(session.GetSets().size(), 2u);
    }

    TEST_F(ArtGenSetPanelTest, QueueIsOffUntilTheNextSubjectIsTypedBecauseAQueuedSetClearsTheRows) {
        TypeSubject("queen");
        ASSERT_TRUE(PressQueue());

        EXPECT_FALSE(PressQueue());
        EXPECT_EQ(session.GetSets().size(), 1u);
    }

    TEST(ArtGenSetPanelLayoutTest, TheFieldsOfARowHaveFixedWidthsInEms) {
        constexpr float kFontSize = 15.0f;
        constexpr float kWide = 4000.0f;
        const ArtGenRowWidths wide = ArtGenRowFieldWidths(kWide, kFontSize);
        EXPECT_FLOAT_EQ(wide.subject, 24.0f * kFontSize);
        EXPECT_FLOAT_EQ(wide.name, 11.0f * kFontSize);

        const ArtGenRowWidths bigger = ArtGenRowFieldWidths(kWide, 2.0f * kFontSize);
        EXPECT_FLOAT_EQ(bigger.subject, 2.0f * wide.subject);
    }

    TEST(ArtGenSetPanelLayoutTest, ANarrowPanelShrinksBothFieldsToFitTheRoom) {
        constexpr float kFontSize = 15.0f;
        constexpr float kNarrow = 200.0f;
        const ArtGenRowWidths narrow = ArtGenRowFieldWidths(kNarrow, kFontSize);
        EXPECT_NEAR(narrow.subject + narrow.name, kNarrow, 0.01f);
        EXPECT_GT(narrow.subject, narrow.name);

        const ArtGenRowWidths none = ArtGenRowFieldWidths(0.0f, kFontSize);
        EXPECT_GE(none.subject, 1.0f);
        EXPECT_GE(none.name, 1.0f);
    }

    TEST(ArtGenSetPanelLayoutTest, AFieldIsCutToTheRoomThatIsLeft) {
        EXPECT_FLOAT_EQ(ArtGenFieldWidth(10.0f, 15.0f, 1000.0f), 150.0f);
        EXPECT_FLOAT_EQ(ArtGenFieldWidth(10.0f, 15.0f, 90.0f), 90.0f);
        EXPECT_FLOAT_EQ(ArtGenFieldWidth(10.0f, 15.0f, -5.0f), 1.0f);
    }

    TEST(ArtGenSetPanelLayoutTest, QueueSaysWhyItIsOff) {
        Vector<ArtGenSubjectRow> rows(1);
        EXPECT_NE(ArtGenQueueBlocker(false, rows, true), nullptr);
        EXPECT_NE(ArtGenQueueBlocker(true, rows, true), nullptr) << "no subject";

        std::snprintf(rows[0].subject, sizeof(rows[0].subject), "   ");
        EXPECT_NE(ArtGenQueueBlocker(true, rows, true), nullptr) << "a blank subject is no subject";

        std::snprintf(rows[0].subject, sizeof(rows[0].subject), "queen");
        EXPECT_EQ(ArtGenQueueBlocker(true, rows, true), nullptr);
        EXPECT_NE(ArtGenQueueBlocker(true, rows, false), nullptr) << "a bad set name";
    }

}  // namespace BECore::Tests
