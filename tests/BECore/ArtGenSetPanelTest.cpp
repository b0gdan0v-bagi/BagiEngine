#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>
#include <Widgets/ArtGen/Sets/ArtGenSetPanel.h>
#include <Widgets/ArtGen/Sets/ArtGenSetStore.h>
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
        constexpr float kSearchHeight = 890.0f;
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
            const auto play = [](const AudioClip&) {};
            const auto stop = [] {};
            panel.DrawSetTab(session, ArtGenSetPanelForm{form}, ArtGenSetPanelSound{play, stop, false});
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

        static ImGuiID AcceptAllId() {
            return ImHashStr(ICON_LC_CHECK_CHECK " Accept all", 0, HostId());
        }

        static ImGuiID RetryId() {
            return ImHashStr(ICON_LC_ROTATE_CCW " Retry", 0, HostId());
        }

        /// A window of the frame whose name has @p part, a child of the host: the list of sets, the rows
        static const ImGuiWindow* FindChild(const char* part) {
            for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
                if (window->ParentWindow != nullptr && std::strstr(window->Name, part) != nullptr) {
                    return window;
                }
            }
            return nullptr;
        }

        /// The entry of set @p name in the list of sets: the id stack is the list's, the set's name, then the item
        static ImGuiID SetEntryId(const char* name) {
            const ImGuiWindow* list = FindChild("##setList");
            if (list == nullptr) {
                ADD_FAILURE() << "no list of sets";
                return 0;
            }
            return ImHashStr("##select", 0, ImHashStr(name, 0, list->ID));
        }

        /// Writes a manifest of @p jobs into a second game root and makes it the session's, as a person who opens another game would
        void LoadSets(std::initializer_list<ArtGenSet> sets) {
            const std::filesystem::path second = _root / "second";
            for (const ArtGenSet& set : sets) {
                ASSERT_TRUE(WriteManifest(second, set).has_value());
            }
            session.SetGameRoot(PathText(second));
            Frame();
        }

        static ArtGenSet MakeSet(const char* name, const char* created, ArtGenSetJobState state) {
            ArtGenSet set;
            set.name = name;
            set.created = created;
            set.preset = "workflow";
            set.count = 2;
            ArtGenSetJob job;
            job.name = "king";
            job.subject = "king";
            job.state = state;
            for (int64_t seed : {5, 6}) {
                ArtGenSetVariant variant;
                variant.seed = seed;
                variant.file = Format("king/king_{}.png", seed);
                job.variants.push_back(variant);
            }
            job.pick = 5;
            set.jobs.push_back(job);
            return set;
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

    TEST_F(ArtGenSetPanelTest, ASetOfTheGameIsSelectedAndItsRowsAreDrawnBelowTheQueue) {
        EXPECT_EQ(FindChild("##setRows"), nullptr) << "no set, no rows";

        ASSERT_NO_FATAL_FAILURE(LoadSets({MakeSet("chess", "2026-11-17T12:00:00Z", ArtGenSetJobState::Done)}));
        Frame();

        EXPECT_NE(FindChild("##setList"), nullptr);
        EXPECT_NE(FindChild("##setRows"), nullptr);
        EXPECT_NE(FindChild("##row"), nullptr) << "the row of the job";
        const ImVec2 acceptAll = Locate(AcceptAllId());
        EXPECT_GT(acceptAll.y, 0.0f) << "the footer is under the rows";
    }

    TEST_F(ArtGenSetPanelTest, TheNewestSetIsSelectedAndAClickOnAnotherSetSelectsIt) {
        ArtGenSet older = MakeSet("older", "2026-11-17T10:00:00Z", ArtGenSetJobState::Failed);
        older.jobs[0].error = "boom";
        ASSERT_NO_FATAL_FAILURE(LoadSets({older, MakeSet("newer", "2026-11-17T11:00:00Z", ArtGenSetJobState::Done)}));

        // Retry belongs to the selected set: the newer one has nothing to retry
        ClickAt(Locate(RetryId()));
        ASSERT_NE(session.FindSet("older"), nullptr);
        EXPECT_EQ(session.FindSet("older")->jobs[0].state, ArtGenSetJobState::Failed);

        ClickAt(Locate(SetEntryId("older")));
        Frame();
        ClickAt(Locate(RetryId()));
        EXPECT_EQ(session.FindSet("older")->jobs[0].state, ArtGenSetJobState::Pending);
    }

    TEST_F(ArtGenSetPanelTest, AQueuedSetIsSelected) {
        ASSERT_NO_FATAL_FAILURE(LoadSets({MakeSet("chess", "2026-11-17T12:00:00Z", ArtGenSetJobState::Done)}));
        TypeSubject("queen");
        ASSERT_TRUE(PressQueue());
        ASSERT_EQ(session.GetSets().size(), 2u);
        const String queued = session.GetSets().back().name;
        ASSERT_NE(queued, String("chess"));
        Frame();

        // The new set has a job waiting: its Cancel set is on, the older set's is off
        ClickAt(Locate(ImHashStr(ICON_LC_X " Cancel set", 0, HostId())));
        EXPECT_EQ(session.FindSet(StringView(queued))->jobs[0].state, ArtGenSetJobState::Cancelled);
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
