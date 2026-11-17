#include <BECore/Format/Format.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/Sets/ArtGenSetStore.h>
#include <Widgets/EditorLayout/UserFileIo.h>
#include <filesystem>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        ArtGenSet MakeSet(StringView name, StringView created) {
            ArtGenSet set;
            set.name = String(name);
            set.created = String(created);
            set.preset = "pixel_pieces";
            set.count = 2;
            set.seed = 100;
            for (const StringView jobName : {"knight", "rook"}) {
                ArtGenSetJob job;
                job.name = String(jobName);
                job.subject = Format("a {}", jobName);
                job.state = ArtGenSetJobState::Done;
                job.variants.push_back({100, Format("{}/{}_100.png", jobName, jobName), Format("{}/{}_100.artgen.json", jobName, jobName), "image"});
                job.pick = 100;
                set.jobs.push_back(std::move(job));
            }
            return set;
        }

        String GenericOf(const std::filesystem::path& path) {
            const std::string text = path.generic_string();  //-NoStd: the text of a std::filesystem::path
            return String(text.c_str(), text.size());
        }

        class ArtGenSetStoreTest : public ::testing::Test {
        protected:
            void SetUp() override {
                _root = TestTempDirectory("artgen_set_store");
                std::filesystem::remove_all(_root);
                std::filesystem::create_directories(_root);
            }

            void TearDown() override {
                std::error_code error;
                std::filesystem::remove_all(_root, error);
            }

            static void Write(const std::filesystem::path& file, StringView text) {
                ASSERT_TRUE(WriteTextFileAtomically(file, text).has_value());
            }

            static String Read(const std::filesystem::path& file) {
                auto text = ReadTextFile(file);
                return text ? *text : String("<unreadable>");
            }

            std::filesystem::path _root;
        };

    }  // namespace

    TEST_F(ArtGenSetStoreTest, PathsAndOutDir) {
        EXPECT_EQ(SetFolder(_root, "s"), _root / "artgen" / "out" / "s");
        EXPECT_EQ(ManifestPath(_root, "s"), _root / "artgen" / "out" / "s" / "manifest.json");
        EXPECT_EQ(JobFolder(_root, "s", "j"), _root / "artgen" / "out" / "s" / "j");
        EXPECT_EQ(OutDirOf("s", "j"), "s/j");
    }

    TEST_F(ArtGenSetStoreTest, WriteThenLoadEqual) {
        const ArtGenSet set = MakeSet("knights", "2026-11-17T10:00:00Z");
        ASSERT_TRUE(WriteManifest(_root, set).has_value());
        EXPECT_TRUE(std::filesystem::exists(ManifestPath(_root, "knights")));
        String temporary = GenericOf(ManifestPath(_root, "knights"));
        temporary.append(kTempFileSuffix.data(), kTempFileSuffix.size());
        EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(ToStd(temporary))));

        const Vector<ArtGenSet> loaded = LoadSets(_root);
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_EQ(ArtGenSetToJson(loaded[0]), ArtGenSetToJson(set));
    }

    TEST_F(ArtGenSetStoreTest, WriteReplacesTheManifest) {
        ArtGenSet set = MakeSet("knights", "2026-11-17T10:00:00Z");
        ASSERT_TRUE(WriteManifest(_root, set).has_value());
        set.jobs[0].pick.reset();
        ASSERT_TRUE(WriteManifest(_root, set).has_value());
        const Vector<ArtGenSet> loaded = LoadSets(_root);
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_FALSE(loaded[0].jobs[0].pick.has_value());
    }

    TEST_F(ArtGenSetStoreTest, WriteRefusesABadSetName) {
        const ArtGenSet set = MakeSet("../escape", "2026-11-17T10:00:00Z");
        EXPECT_FALSE(WriteManifest(_root, set).has_value());
        EXPECT_FALSE(std::filesystem::exists(_root / "artgen"));
    }

    TEST_F(ArtGenSetStoreTest, NothingToLoadWithoutTheFolder) {
        EXPECT_TRUE(LoadSets(_root).empty());
        EXPECT_TRUE(LoadSets(_root / "no_such_game").empty());
    }

    TEST_F(ArtGenSetStoreTest, InterruptedJobsComeBackCancelled) {
        ArtGenSet set = MakeSet("knights", "2026-11-17T10:00:00Z");
        set.jobs[0].state = ArtGenSetJobState::Running;
        set.jobs[0].variants.clear();
        set.jobs[0].pick.reset();
        set.jobs[1].state = ArtGenSetJobState::Pending;
        ArtGenSetJob failed;
        failed.name = "bishop";
        failed.subject = "a bishop";
        failed.state = ArtGenSetJobState::Failed;
        failed.error = "boom";
        set.jobs.push_back(std::move(failed));
        ASSERT_TRUE(WriteManifest(_root, set).has_value());

        const Vector<ArtGenSet> loaded = LoadSets(_root);
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_FALSE(IsActive(loaded[0]));
        EXPECT_EQ(loaded[0].jobs[0].state, ArtGenSetJobState::Cancelled);
        EXPECT_EQ(loaded[0].jobs[0].error, kEditorClosedReason);
        EXPECT_EQ(loaded[0].jobs[1].state, ArtGenSetJobState::Cancelled);
        EXPECT_EQ(loaded[0].jobs[2].state, ArtGenSetJobState::Failed);
        EXPECT_EQ(loaded[0].jobs[2].error, "boom");
    }

    TEST_F(ArtGenSetStoreTest, LoadingRewritesNothing) {
        ArtGenSet set = MakeSet("knights", "2026-11-17T10:00:00Z");
        set.jobs[0].state = ArtGenSetJobState::Running;
        ASSERT_TRUE(WriteManifest(_root, set).has_value());
        const String before = Read(ManifestPath(_root, "knights"));
        EXPECT_EQ(LoadSets(_root).size(), 1u);
        EXPECT_EQ(Read(ManifestPath(_root, "knights")), before);
    }

    TEST_F(ArtGenSetStoreTest, ABrokenManifestIsSkippedAndKept) {
        ASSERT_TRUE(WriteManifest(_root, MakeSet("good", "2026-11-17T10:00:00Z")).has_value());
        Write(ManifestPath(_root, "garbage"), "{not json");
        Write(ManifestPath(_root, "newer"), R"({"manifest": 2, "set": "newer"})");
        Write(ManifestPath(_root, "wrongtype"), R"({"manifest": 1, "set": "wrongtype", "kind": "image", "count": "four", "seed": 1, "jobs": []})");
        ASSERT_TRUE(std::filesystem::create_directories(SetFolder(_root, "no_manifest")));

        const Vector<ArtGenSet> loaded = LoadSets(_root);
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_EQ(loaded[0].name, "good");
        EXPECT_EQ(Read(ManifestPath(_root, "garbage")), "{not json");
        EXPECT_EQ(Read(ManifestPath(_root, "newer")), R"({"manifest": 2, "set": "newer"})");
    }

    TEST_F(ArtGenSetStoreTest, TheFoldersNameIsTheSetsName) {
        ASSERT_TRUE(WriteManifest(_root, MakeSet("original", "2026-11-17T10:00:00Z")).has_value());
        std::filesystem::rename(SetFolder(_root, "original"), SetFolder(_root, "moved"));
        const Vector<ArtGenSet> loaded = LoadSets(_root);
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_EQ(loaded[0].name, "moved");

        std::filesystem::rename(SetFolder(_root, "moved"), SetFolder(_root, "not a name"));
        EXPECT_TRUE(LoadSets(_root).empty());
    }

    TEST_F(ArtGenSetStoreTest, SetsAreSortedByCreatedThenName) {
        ASSERT_TRUE(WriteManifest(_root, MakeSet("b_late", "2026-11-17T12:00:00Z")).has_value());
        ASSERT_TRUE(WriteManifest(_root, MakeSet("z_early", "2026-11-17T09:00:00Z")).has_value());
        ASSERT_TRUE(WriteManifest(_root, MakeSet("a_late", "2026-11-17T12:00:00Z")).has_value());
        const Vector<ArtGenSet> loaded = LoadSets(_root);
        ASSERT_EQ(loaded.size(), 3u);
        EXPECT_EQ(loaded[0].name, "z_early");
        EXPECT_EQ(loaded[1].name, "a_late");
        EXPECT_EQ(loaded[2].name, "b_late");
    }

    TEST_F(ArtGenSetStoreTest, AFileThatVanishedStaysInTheManifest) {
        ASSERT_TRUE(WriteManifest(_root, MakeSet("knights", "2026-11-17T10:00:00Z")).has_value());
        ASSERT_FALSE(std::filesystem::exists(AbsoluteInSet(_root, "knights", "knight/knight_100.png")));

        const Vector<ArtGenSet> loaded = LoadSets(_root);
        ASSERT_EQ(loaded.size(), 1u);
        ASSERT_EQ(loaded[0].jobs[0].variants.size(), 1u);
        EXPECT_EQ(loaded[0].jobs[0].variants[0].file, "knight/knight_100.png");
        EXPECT_EQ(loaded[0].jobs[0].pick, Optional<int64_t>(100));
    }

    TEST_F(ArtGenSetStoreTest, NewSetFolderFree) {
        EXPECT_TRUE(NewSetFolderFree(_root, "knights"));
        EXPECT_FALSE(NewSetFolderFree(_root, "bad name"));
        EXPECT_FALSE(NewSetFolderFree(_root, ""));
        ASSERT_TRUE(WriteManifest(_root, MakeSet("knights", "2026-11-17T10:00:00Z")).has_value());
        EXPECT_FALSE(NewSetFolderFree(_root, "knights"));
        // A folder with no manifest, or a broken one, is taken too: nothing in it is overwritten
        ASSERT_TRUE(std::filesystem::create_directories(SetFolder(_root, "empty")));
        EXPECT_FALSE(NewSetFolderFree(_root, "empty"));
    }

    TEST_F(ArtGenSetStoreTest, RelativeToSetUsesForwardSlashes) {
        const std::filesystem::path folder = SetFolder(_root, "knights");
        const String real = GenericOf(folder / "knight" / "knight_100.png");
        EXPECT_EQ(RelativeToSet(folder, real), "knight/knight_100.png");

        // A path written with the separators of Windows reads the same on every platform
        String windows = real;
        for (char& c : windows) {
            if (c == '/') {
                c = '\\';
            }
        }
        EXPECT_EQ(RelativeToSet(folder, windows), "knight/knight_100.png");
    }

    TEST_F(ArtGenSetStoreTest, APathOutsideTheFolderIsKeptAsGiven) {
        const std::filesystem::path folder = SetFolder(_root, "knights");
        const String outside = GenericOf(_root / "elsewhere" / "x.png");
        EXPECT_EQ(RelativeToSet(folder, outside), outside);
        EXPECT_EQ(RelativeToSet(folder, "x/y.png"), "x/y.png");
        const String sibling = GenericOf(folder.parent_path() / "other" / "x.png");
        EXPECT_EQ(RelativeToSet(folder, sibling), sibling);
    }

    TEST_F(ArtGenSetStoreTest, AbsoluteInSet) {
        const std::filesystem::path expected = SetFolder(_root, "knights") / "knight" / "knight_100.png";
        EXPECT_EQ(AbsoluteInSet(_root, "knights", "knight/knight_100.png"), expected);
        EXPECT_EQ(AbsoluteInSet(_root, "knights", "knight\\knight_100.png"), expected);
        EXPECT_TRUE(AbsoluteInSet(_root, "knights", "").empty());
        EXPECT_TRUE(AbsoluteInSet(_root, "knights", "../other/x.png").empty());
        EXPECT_TRUE(AbsoluteInSet(_root, "knights", "a/../../x.png").empty());

        const String absolute = GenericOf(_root / "elsewhere" / "x.png");
        EXPECT_EQ(AbsoluteInSet(_root, "knights", absolute), _root / "elsewhere" / "x.png");
    }

    TEST_F(ArtGenSetStoreTest, AnInterruptedMoreRunComesBackDoneWithTheNote) {
        ArtGenSet set = MakeSet("knights", "2026-11-17T10:00:00Z");
        set.jobs[0].state = ArtGenSetJobState::Running;
        set.jobs[0].more = ArtGenSetMore{500, 2};
        set.jobs[1].state = ArtGenSetJobState::Pending;
        set.jobs[1].more = ArtGenSetMore{500, 2};
        ASSERT_TRUE(WriteManifest(_root, set).has_value());

        const Vector<ArtGenSet> loaded = LoadSets(_root);
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_FALSE(IsActive(loaded[0]));
        for (const ArtGenSetJob& job : loaded[0].jobs) {
            EXPECT_EQ(job.state, ArtGenSetJobState::Done);
            EXPECT_EQ(job.error, kEditorClosedReason);
            EXPECT_FALSE(job.more.has_value());
            ASSERT_EQ(job.variants.size(), 1u);
            EXPECT_EQ(job.pick, Optional<int64_t>(100));
        }
    }

}  // namespace BECore::Tests
