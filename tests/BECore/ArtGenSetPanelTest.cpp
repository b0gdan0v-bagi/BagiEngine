#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenImage.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>
#include <Widgets/ArtGen/Sets/ArtGenSetPanel.h>
#include <Widgets/ArtGen/Sets/ArtGenSetStore.h>
#include <Widgets/EditorLayout/UserFileIo.h>
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
            panel.DrawSetTab(session, ArtGenSetPanelForm{form, makesImages}, ArtGenSetPanelSound{play, stop, false});
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

        static ImGuiID MoreVariantsId() {
            return ImHashStr(ICON_LC_COPY_PLUS " More variants", 0, HostId());
        }

        /// The text field of the count: InputInt with its buttons puts the field under an id stack of its label
        static ImGuiID MoreCountId() {
            return ImHashStr("", 0, ImHashStr("##moreCount", 0, HostId()));
        }

        static ImGuiID KeepAllId() {
            return ImHashStr(ICON_LC_LOCK " Keep all", 0, HostId());
        }

        static ImGuiID RetryId() {
            return ImHashStr(ICON_LC_ROTATE_CCW " Retry", 0, HostId());
        }

        /// The row @p index of the rows' child (by its place on the screen): an id inside a row is hashed from its id
        static ImGuiID RowId(size_t index) {
            Vector<const ImGuiWindow*> rows;
            for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
                if (window->ParentWindow != nullptr && std::strstr(window->Name, "/##row_") != nullptr) {
                    rows.push_back(window);
                }
            }
            std::sort(rows.begin(), rows.end(), [](const ImGuiWindow* left, const ImGuiWindow* right) { return left->Pos.y < right->Pos.y; });
            if (index >= rows.size()) {
                ADD_FAILURE() << "no row " << index;
                return 0;
            }
            return rows[index]->ID;
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

        /// A finished set of two rows: king (not kept) and queen (kept)
        static ArtGenSet MakeFinishedSet() {
            ArtGenSet set = MakeSet("chess", "2026-11-17T12:00:00Z", ArtGenSetJobState::Done);
            ArtGenSetJob queen = set.jobs[0];
            queen.name = "queen";
            queen.subject = "queen";
            queen.accepted = ArtGenSetAccepted{"queen", "2026-11-17T12:30:00Z", true};
            queen.variants.resize(1);
            set.jobs.push_back(queen);
            return set;
        }

        const ArtGenSetJob& JobOf(const char* job) {
            return *FindJob(*session.FindSet("chess"), job);
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

        /// The folder of a job's files in the set `chess` of the second game root that LoadSets makes
        std::filesystem::path VariantFolder(const char* job) const {
            return _root / "second" / "artgen" / "out" / "chess" / job;
        }

        static void WritePicture(const std::filesystem::path& file) {
            std::filesystem::create_directories(file.parent_path());
            ArtGenPixels pixels;
            pixels.width = 2;
            pixels.height = 2;
            pixels.rgba.assign(2 * 2 * 4, 255);
            ASSERT_TRUE(WriteArtGenPng(file, pixels).has_value());
        }

        static void WriteProvenance(const std::filesystem::path& file, StringView text) {
            ASSERT_TRUE(WriteTextFileAtomically(file, text).has_value());
        }

        static constexpr StringView kPassedChecks = R"({"seed":5,"checks":{"passed":true,"failed":[]}})";
        static constexpr StringView kFailedChecks = R"({"seed":6,"checks":{"passed":false,"failed":["edge","halo"]}})";
        static constexpr StringView kUnreadableChecks = R"({"seed":7,"checks":{"passed":null,"error":"not a PNG"}})";

        /// A finished set of two rows. King has three variants with a picture and a provenance file each: checks passed (seed 5), failed with
        /// edge and halo (6), unreadable (7). Queen has one with a picture and no provenance in the manifest (5), one whose provenance file has
        /// no checks (6) and one whose picture is gone though its provenance file is there (7)
        ArtGenSet MakeVerdictSet() {
            ArtGenSet set = MakeSet("chess", "2026-11-17T12:00:00Z", ArtGenSetJobState::Done);
            ArtGenSetJob& king = set.jobs[0];
            king.variants.clear();
            for (int64_t seed : {5, 6, 7}) {
                ArtGenSetVariant variant;
                variant.seed = seed;
                variant.file = Format("king/king_{}.png", seed);
                variant.provenance = Format("king/king_{}.artgen.json", seed);
                king.variants.push_back(variant);
                WritePicture(VariantFolder("king") / Format("king_{}.png", seed).c_str());
            }
            WriteProvenance(VariantFolder("king") / "king_5.artgen.json", kPassedChecks);
            WriteProvenance(VariantFolder("king") / "king_6.artgen.json", kFailedChecks);
            WriteProvenance(VariantFolder("king") / "king_7.artgen.json", kUnreadableChecks);

            ArtGenSetJob queen = king;
            queen.name = "queen";
            queen.subject = "queen";
            queen.variants.clear();
            ArtGenSetVariant bare;
            bare.seed = 5;
            bare.file = "queen/queen_5.png";
            queen.variants.push_back(bare);
            WritePicture(VariantFolder("queen") / "queen_5.png");
            ArtGenSetVariant noChecks;
            noChecks.seed = 6;
            noChecks.file = "queen/queen_6.png";
            noChecks.provenance = "queen/queen_6.artgen.json";
            queen.variants.push_back(noChecks);
            WritePicture(VariantFolder("queen") / "queen_6.png");
            WriteProvenance(VariantFolder("queen") / "queen_6.artgen.json", R"({"seed":6})");
            ArtGenSetVariant gone;
            gone.seed = 7;
            gone.file = "queen/queen_7.png";
            gone.provenance = "queen/queen_7.artgen.json";
            queen.variants.push_back(gone);
            WriteProvenance(VariantFolder("queen") / "queen_7.artgen.json", kFailedChecks);
            queen.pick = 5;
            set.jobs.push_back(queen);
            return set;
        }

        /// The pick button of a variant: the cell's own item, which the mouse is over anywhere on the tile
        static ImGuiID PickId(size_t row, int32_t variant) {
            return ImHashStr("##pick", 0, ImHashData(&variant, sizeof(variant), RowId(row)));
        }

        /// Puts the mouse over the mark of a variant's tile (its top-right corner) and draws two frames
        void HoverMarkOf(size_t row, int32_t variant) {
            const ImVec2 corner = Locate(PickId(row, variant));
            constexpr float kToMarkX = 104.0f;
            constexpr float kToMarkY = 10.0f;
            ImGui::GetIO().AddMousePosEvent(corner.x + kToMarkX, corner.y + kToMarkY);
            Frame();
            Frame();
        }

        static bool IsTooltipShown() {
            const ImGuiWindow* tooltip = ImGui::FindWindowByName("##Tooltip_00");
            return tooltip != nullptr && tooltip->Active;
        }

        double now = 0.0;
        bool makesImages = true;  // what the preset of the form makes, as the widget tells the tab
        ArtGenSession session;
        ArtGenPresetForm form;
        ArtGenSetPanel panel;
        ImGuiFontBuffers buffers;
        ImGuiFonts fonts;

        std::filesystem::path Root() const {
            return _root;
        }

    private:
        std::filesystem::path _root;
    };

    TEST_F(ArtGenSetPanelTest, QueueSendsTheSlotOfAnImagePresetAndNothingForASound) {
        const ArtGenReference slot{"style", "assets/artgen/white_knight.png", Optional<double>(0.7)};
        session.SetReferenceSlot(slot);
        TypeSubject("queen");
        ASSERT_TRUE(PressQueue());
        EXPECT_EQ(session.GetSets()[0].reference, Optional<ArtGenReference>(slot));

        makesImages = false;
        TypeSubject("a gong");
        ASSERT_TRUE(PressQueue());
        EXPECT_FALSE(session.GetSets()[1].reference.has_value()) << "a sound has no reference";

        makesImages = true;
        session.ClearReferenceSlot();
        TypeSubject("king");
        ASSERT_TRUE(PressQueue());
        EXPECT_FALSE(session.GetSets()[2].reference.has_value()) << "an empty slot sends nothing: the preset's own applies";
    }

    TEST_F(ArtGenSetPanelTest, AReferenceOfTheQueueAndOfTheSelectedSetCostsAFrameNoAllocation) {
        ArtGenSet set = MakeSet("styled", "2026-11-17T12:00:00Z", ArtGenSetJobState::Done);
        set.reference = ArtGenReference{"style", "assets/artgen/white_knight.png", {}};
        LoadSets({set});
        session.SetReferenceSlot(ArtGenReference{"init", "assets/artgen/a_rather_long_file_name_for_the_slot.png", {}});
        Frame();
        const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
        for (int i = 0; i < 3; ++i) {
            Frame();
        }
        const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(after.count, before.count);
        }
    }

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

    TEST_F(ArtGenSetPanelTest, MoreVariantsIsOffWhileTheSetRunsAndAClickDoesNothing) {
        TypeSubject("queen");
        ASSERT_TRUE(PressQueue());
        Frame();

        ClickAt(Locate(MoreVariantsId()));
        EXPECT_FALSE(session.GetSets()[0].jobs[0].more.has_value());
        EXPECT_EQ(session.GetSets()[0].jobs[0].state, ArtGenSetJobState::Pending);
    }

    TEST_F(ArtGenSetPanelTest, KeepAllIsOffWhileTheSetRunsAndAClickKeepsNothing) {
        session.SetKeeper([](StringView, StringView, int64_t, StringView) -> std::expected<void, ControlError> { return {}; });
        ArtGenSet set = MakeFinishedSet();
        set.jobs.erase(set.jobs.begin() + 1);
        set.jobs[0].accepted = ArtGenSetAccepted{"king", "2026-11-17T12:30:00Z", false};
        ArtGenSetJob waiting = set.jobs[0];
        waiting.name = "rook";
        waiting.accepted.reset();
        waiting.state = ArtGenSetJobState::Cancelled;
        set.jobs.push_back(waiting);
        ASSERT_NO_FATAL_FAILURE(LoadSets({set}));
        ASSERT_EQ(session.RetryJobs("chess"), ArtGenSetEdit::Ok);
        Frame();
        ASSERT_EQ(JobOf("rook").state, ArtGenSetJobState::Pending);

        ClickAt(Locate(KeepAllId()));
        EXPECT_FALSE(JobOf("king").accepted->kept);
        EXPECT_EQ(JobOf("king").variants.size(), 2u);

        session.CancelJob("chess", "rook");
        Frame();
        ClickAt(Locate(KeepAllId()));
        EXPECT_TRUE(JobOf("king").accepted->kept) << "the same click keeps once the set no longer runs";
    }

    TEST_F(ArtGenSetPanelTest, MoreVariantsIsOffWhenEveryRowIsKept) {
        ArtGenSet set = MakeFinishedSet();
        set.jobs.erase(set.jobs.begin());
        ASSERT_NO_FATAL_FAILURE(LoadSets({set}));

        ClickAt(Locate(MoreVariantsId()));
        EXPECT_FALSE(FindJob(*session.FindSet("chess"), "queen")->more.has_value());
        EXPECT_EQ(FindJob(*session.FindSet("chess"), "queen")->state, ArtGenSetJobState::Done);
    }

    TEST_F(ArtGenSetPanelTest, MoreVariantsOfAFinishedSetQueuesTheRowsThatAreNotKept) {
        ASSERT_NO_FATAL_FAILURE(LoadSets({MakeFinishedSet()}));

        ClickAt(Locate(MoreVariantsId()));

        const ArtGenSetJob& king = JobOf("king");
        EXPECT_EQ(king.state, ArtGenSetJobState::Pending);
        ASSERT_TRUE(king.more.has_value());
        EXPECT_EQ(king.more->count, 1) << "the field's default";
        EXPECT_EQ(king.variants.size(), 2u);
        EXPECT_EQ(king.pick, Optional<int64_t>(5));
        EXPECT_EQ(JobOf("queen").state, ArtGenSetJobState::Done);
        EXPECT_FALSE(JobOf("queen").more.has_value());
    }

    TEST_F(ArtGenSetPanelTest, TheCountFieldSetsHowManyVariantsAreAsked) {
        ASSERT_NO_FATAL_FAILURE(LoadSets({MakeFinishedSet()}));

        ClickAt(Locate(MoreCountId()));
        ImGui::GetIO().AddInputCharactersUTF8("2");
        Frame();
        Frame();
        ClickAt(Locate(MoreVariantsId()));

        ASSERT_TRUE(JobOf("king").more.has_value());
        EXPECT_EQ(JobOf("king").more->count, 2);
    }

    TEST_F(ArtGenSetPanelTest, UseAsReferenceOnAVariantFillsTheSlotWithItsFile) {
        const std::filesystem::path folder = Root() / "second" / "artgen" / "out" / "chess" / "king";
        std::filesystem::create_directories(folder);
        ArtGenPixels pixels;
        pixels.width = 2;
        pixels.height = 2;
        pixels.rgba.assign(2 * 2 * 4, 255);
        ASSERT_TRUE(WriteArtGenPng(folder / "king_5.png", pixels).has_value());
        ASSERT_NO_FATAL_FAILURE(LoadSets({MakeFinishedSet()}));
        constexpr int32_t kFirstVariant = 0;
        const ImGuiID button = ImHashStr(ICON_LC_IMAGE_PLUS "####reference", 0, ImHashData(&kFirstVariant, sizeof(kFirstVariant), RowId(0)));
        ASSERT_FALSE(session.GetReferenceSlot().has_value());

        ClickAt(Locate(button));

        ASSERT_TRUE(session.GetReferenceSlot().has_value());
        EXPECT_EQ(session.GetReferenceSlot()->mode, "init");
        EXPECT_EQ(session.GetReferenceSlot()->image, "artgen/out/chess/king/king_5.png");
    }

    TEST_F(ArtGenSetPanelTest, UseAsReferenceOnAnAcceptedRowFillsTheSlotWithItsSprite) {
        ASSERT_NO_FATAL_FAILURE(LoadSets({MakeFinishedSet()}));
        const ImGuiID button = ImHashStr(ICON_LC_IMAGE_PLUS "####referenceSprite", 0, RowId(1));

        ClickAt(Locate(button));

        ASSERT_TRUE(session.GetReferenceSlot().has_value());
        EXPECT_EQ(session.GetReferenceSlot()->image, "assets/artgen/queen.png");
    }

    TEST_F(ArtGenSetPanelTest, ThePanelHoldsTheVerdictOfEachVariantFromItsProvenanceFile) {
        ASSERT_NO_FATAL_FAILURE(LoadSets({MakeVerdictSet()}));
        constexpr size_t kKing = 0;
        constexpr size_t kQueen = 1;

        const ArtGenVerdict* passed = panel.VerdictOf(kKing, 5);
        ASSERT_NE(passed, nullptr);
        EXPECT_EQ(passed->kind, ArtGenVerdictKind::Passed);

        const ArtGenVerdict* failed = panel.VerdictOf(kKing, 6);
        ASSERT_NE(failed, nullptr);
        EXPECT_EQ(failed->kind, ArtGenVerdictKind::Failed);
        EXPECT_NE(failed->tip.find("edge"), String::npos);
        EXPECT_NE(failed->tip.find("halo"), String::npos);

        ASSERT_NE(panel.VerdictOf(kKing, 7), nullptr);
        EXPECT_EQ(panel.VerdictOf(kKing, 7)->kind, ArtGenVerdictKind::Unreadable);
        ASSERT_NE(panel.VerdictOf(kQueen, 5), nullptr);
        EXPECT_EQ(panel.VerdictOf(kQueen, 5)->kind, ArtGenVerdictKind::None) << "the manifest names no provenance";
        ASSERT_NE(panel.VerdictOf(kQueen, 6), nullptr);
        EXPECT_EQ(panel.VerdictOf(kQueen, 6)->kind, ArtGenVerdictKind::None) << "a provenance file without checks";
        ASSERT_NE(panel.VerdictOf(kQueen, 7), nullptr);
        EXPECT_EQ(panel.VerdictOf(kQueen, 7)->kind, ArtGenVerdictKind::None) << "the picture is gone, so the file is not read";
        EXPECT_EQ(panel.VerdictOf(kKing, 99), nullptr) << "no such variant";
        EXPECT_EQ(panel.VerdictOf(kQueen + 5, 5), nullptr) << "no such row";
    }

    TEST_F(ArtGenSetPanelTest, TheProvenanceFileIsReadWhenTheThumbnailLoadsAndNotAgain) {
        ASSERT_NO_FATAL_FAILURE(LoadSets({MakeVerdictSet()}));
        ASSERT_EQ(panel.VerdictOf(0, 5)->kind, ArtGenVerdictKind::Passed);

        ASSERT_NO_FATAL_FAILURE(WriteProvenance(VariantFolder("king") / "king_5.artgen.json", kFailedChecks));
        Frame();
        Frame();
        EXPECT_EQ(panel.VerdictOf(0, 5)->kind, ArtGenVerdictKind::Passed) << "a frame does not read the file";

        panel.DropThumbnails();
        Frame();
        ASSERT_NE(panel.VerdictOf(0, 5), nullptr);
        EXPECT_EQ(panel.VerdictOf(0, 5)->kind, ArtGenVerdictKind::Failed) << "a thumbnail loaded again reads it again";
    }

    TEST_F(ArtGenSetPanelTest, TheTooltipOfTheMarkShowsOverTheMarkAndNotOverTheRestOfTheTile) {
        ASSERT_NO_FATAL_FAILURE(LoadSets({MakeVerdictSet()}));
        constexpr int32_t kFailedVariant = 1;

        const ImVec2 corner = Locate(PickId(0, kFailedVariant));
        constexpr float kMiddle = 56.0f;
        ImGui::GetIO().AddMousePosEvent(corner.x + kMiddle, corner.y + kMiddle);
        Frame();
        Frame();
        EXPECT_FALSE(IsTooltipShown()) << "the tile's own tooltip waits for a hover delay";

        ASSERT_NO_FATAL_FAILURE(HoverMarkOf(0, kFailedVariant));
        EXPECT_TRUE(IsTooltipShown());
    }

    TEST(ArtGenSetPanelVerdictTest, EveryKindButNoneHasItsOwnIcon) {
        EXPECT_EQ(ArtGenVerdictIcon(ArtGenVerdictKind::None), nullptr);
        const char* passed = ArtGenVerdictIcon(ArtGenVerdictKind::Passed);
        const char* failed = ArtGenVerdictIcon(ArtGenVerdictKind::Failed);
        const char* unreadable = ArtGenVerdictIcon(ArtGenVerdictKind::Unreadable);
        ASSERT_NE(passed, nullptr);
        ASSERT_NE(failed, nullptr);
        ASSERT_NE(unreadable, nullptr);
        EXPECT_STRNE(passed, failed);
        EXPECT_STRNE(passed, unreadable);
        EXPECT_STRNE(failed, unreadable);
    }

    TEST_F(ArtGenSetPanelTest, ASteadyFrameWithVerdictsOnTheScreenAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(LoadSets({MakeVerdictSet()}));
        ASSERT_EQ(panel.VerdictOf(0, 7)->kind, ArtGenVerdictKind::Unreadable);
        constexpr int kWarmupFrames = 10;
        constexpr int kSteadyFrames = 5;
        constexpr int32_t kFailedVariant = 1;
        for (int i = 0; i < kWarmupFrames; ++i) {
            Frame();
        }
        ASSERT_NO_FATAL_FAILURE(HoverMarkOf(0, kFailedVariant));
        ASSERT_TRUE(IsTooltipShown());

        const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
        for (int i = 0; i < kSteadyFrames; ++i) {
            Frame();
        }
        const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();

        // The counters stay zero without ENGINE_PROFILER_ALLOC, so the check only bites in the profile preset
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(after.count, before.count);
        }
    }

    TEST_F(ArtGenSetPanelTest, ASteadyFrameWithASetSelectedAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(LoadSets({MakeFinishedSet()}));
        constexpr int kWarmupFrames = 10;
        constexpr int kSteadyFrames = 5;
        for (int i = 0; i < kWarmupFrames; ++i) {
            Frame();
        }

        const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
        for (int i = 0; i < kSteadyFrames; ++i) {
            Frame();
        }
        const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();

        // The counters stay zero without ENGINE_PROFILER_ALLOC, so the check only bites in the profile preset
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(after.count, before.count);
        }
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

    TEST(ArtGenSetPanelLayoutTest, QueueSendsTheSlotOnlyWithAnImagePreset) {
        const Optional<ArtGenReference> slot = ArtGenReference{"style", "assets/artgen/white_knight.png", {}};
        EXPECT_EQ(ArtGenQueueReference(slot, true), slot);
        EXPECT_FALSE(ArtGenQueueReference(slot, false).has_value()) << "a sound has no reference";
        EXPECT_FALSE(ArtGenQueueReference({}, true).has_value());
    }

    TEST(ArtGenSetPanelLayoutTest, TheQueueLineSaysWhatGoesOutAndWhatAppliesInstead) {
        char line[160];
        FormatArtGenQueueReference({}, true, line, sizeof(line));
        EXPECT_NE(StringView(line).find("preset's own reference"), StringView::npos) << "no slot: the preset's own applies";
        FormatArtGenQueueReference(ArtGenReference{"style", "assets/artgen/white_knight.png", {}}, true, line, sizeof(line));
        EXPECT_NE(StringView(line).find("white_knight.png (style, weight 0.80)"), StringView::npos) << line;
        EXPECT_EQ(StringView(line).find("assets/"), StringView::npos) << "the file name, not the path";
        FormatArtGenQueueReference(ArtGenReference{"style", "a.png", {}}, false, line, sizeof(line));
        EXPECT_NE(StringView(line).find("sound"), StringView::npos) << "a sound sends none, whatever the slot holds";
        EXPECT_EQ(StringView(line).find("a.png"), StringView::npos);
    }

    TEST(ArtGenSetPanelLayoutTest, AReferenceSummaryHasTheFileTheModeAndTheWeight) {
        char summary[160];
        FormatArtGenReferenceSummary(ArtGenReference{"init", "C:/pictures/knight.png", Optional<double>(0.25)}, summary, sizeof(summary));
        EXPECT_STREQ(summary, "knight.png (init, weight 0.25)");
        FormatArtGenReferenceSummary(ArtGenReference{"init", "knight.png", {}}, summary, sizeof(summary));
        EXPECT_STREQ(summary, "knight.png (init, weight 0.40)") << "the mode's default weight";
        char tiny[8];
        FormatArtGenReferenceSummary(ArtGenReference{"init", "knight.png", {}}, tiny, sizeof(tiny));
        EXPECT_EQ(std::strlen(tiny), 7u) << "a small buffer is cut, never overrun";
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
