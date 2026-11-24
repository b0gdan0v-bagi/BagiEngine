#include <BECore/File/File.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/ArtGen/Sets/ArtGenSetStore.h>
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

        class ArtGenSetStoreTest : public ::testing::Test {
        protected:
            void SetUp() override {
                _root = TestTempDirectory("artgen_set_store");
                MakeEmptyDirectory(_root);
            }

            void TearDown() override {
                RemoveDirectory(_root);
            }

            static void Write(const Path& file, StringView text) {
                ASSERT_TRUE(WriteFileAtomically(file, text).has_value());
            }

            static String Read(const Path& file) {
                auto text = ReadTextFile(file);
                return text ? *text : String("<unreadable>");
            }

            Path _root;
        };

    }  // namespace

    TEST_F(ArtGenSetStoreTest, PathsAndOutDir) {
        EXPECT_EQ(SetFolder(_root, "s"), Path(_root / "artgen" / "out" / "s"));
        EXPECT_EQ(ManifestPath(_root, "s"), Path(_root / "artgen" / "out" / "s" / "manifest.json"));
        EXPECT_EQ(JobFolder(_root, "s", "j"), Path(_root / "artgen" / "out" / "s" / "j"));
        EXPECT_EQ(OutDirOf("s", "j"), "s/j");
    }

    TEST_F(ArtGenSetStoreTest, WriteThenLoadEqual) {
        const ArtGenSet set = MakeSet("knights", "2026-11-17T10:00:00Z");
        ASSERT_TRUE(WriteManifest(_root, set).has_value());
        EXPECT_TRUE(GetFileInfo(ManifestPath(_root, "knights")).has_value());
        String temporary(ManifestPath(_root, "knights").View());
        temporary.append(kAtomicTempSuffix.data(), kAtomicTempSuffix.size());
        EXPECT_FALSE(GetFileInfo(temporary).has_value());

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
        EXPECT_FALSE(PathExists(_root / "artgen"));
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
        MakeDirectory(SetFolder(_root, "no_manifest"));

        const Vector<ArtGenSet> loaded = LoadSets(_root);
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_EQ(loaded[0].name, "good");
        EXPECT_EQ(Read(ManifestPath(_root, "garbage")), "{not json");
        EXPECT_EQ(Read(ManifestPath(_root, "newer")), R"({"manifest": 2, "set": "newer"})");
    }

    TEST_F(ArtGenSetStoreTest, TheFoldersNameIsTheSetsName) {
        ASSERT_TRUE(WriteManifest(_root, MakeSet("original", "2026-11-17T10:00:00Z")).has_value());
        ASSERT_TRUE(RenameFile(SetFolder(_root, "original"), SetFolder(_root, "moved")).has_value());
        const Vector<ArtGenSet> loaded = LoadSets(_root);
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_EQ(loaded[0].name, "moved");

        ASSERT_TRUE(RenameFile(SetFolder(_root, "moved"), SetFolder(_root, "not a name")).has_value());
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
        ASSERT_FALSE(PathExists(AbsoluteInSet(_root, "knights", "knight/knight_100.png")));

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
        MakeDirectory(SetFolder(_root, "empty"));
        EXPECT_FALSE(NewSetFolderFree(_root, "empty"));
    }

    TEST_F(ArtGenSetStoreTest, RelativeToSetUsesForwardSlashes) {
        const Path folder = SetFolder(_root, "knights");
        const String real(Path(folder / "knight" / "knight_100.png").View());
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
        const Path folder = SetFolder(_root, "knights");
        const String outside((_root / "elsewhere" / "x.png").View());
        EXPECT_EQ(RelativeToSet(folder, outside), outside);
        EXPECT_EQ(RelativeToSet(folder, "x/y.png"), "x/y.png");
        const String sibling(Path(folder.Parent() / "other" / "x.png").View());
        EXPECT_EQ(RelativeToSet(folder, sibling), sibling);
    }

    TEST_F(ArtGenSetStoreTest, AbsoluteInSet) {
        const Path expected = SetFolder(_root, "knights") / "knight" / "knight_100.png";
        EXPECT_EQ(AbsoluteInSet(_root, "knights", "knight/knight_100.png"), expected);
        EXPECT_EQ(AbsoluteInSet(_root, "knights", "knight\\knight_100.png"), expected);
        EXPECT_TRUE(AbsoluteInSet(_root, "knights", "").Empty());
        EXPECT_TRUE(AbsoluteInSet(_root, "knights", "../other/x.png").Empty());
        EXPECT_TRUE(AbsoluteInSet(_root, "knights", "a/../../x.png").Empty());

        const String absolute((_root / "elsewhere" / "x.png").View());
        EXPECT_EQ(AbsoluteInSet(_root, "knights", absolute), Path(_root / "elsewhere" / "x.png"));
    }

    TEST_F(ArtGenSetStoreTest, AbsoluteInSetRefusesEveryWayOutOfTheFolder) {
        for (const StringView escape : {"..", "../x", "a/../b", "a/..", "a/../../x", "./../x", "a//../x", "..\\x", "a\\..\\b", "caf\xC3\xA9/../x.png", "../caf\xC3\xA9/x.png"}) {
            EXPECT_TRUE(AbsoluteInSet(_root, "knights", escape).Empty()) << escape.data();
        }
    }

    TEST_F(ArtGenSetStoreTest, AbsoluteInSetKeepsNamesThatOnlyLookLikeAnEscape) {
        const Path folder = SetFolder(_root, "knights");
        EXPECT_EQ(AbsoluteInSet(_root, "knights", "..a/b.png"), folder / "..a" / "b.png");
        EXPECT_EQ(AbsoluteInSet(_root, "knights", "a../b.png"), folder / "a.." / "b.png");
        EXPECT_EQ(AbsoluteInSet(_root, "knights", ".../b.png"), folder / "..." / "b.png");
        EXPECT_EQ(AbsoluteInSet(_root, "knights", "a/./b.png"), folder / "a" / "." / "b.png");
        EXPECT_EQ(AbsoluteInSet(_root, "knights", "caf\xC3\xA9/b\xC3\xA9.png"), folder / "caf\xC3\xA9" / "b\xC3\xA9.png");
        EXPECT_EQ(AbsoluteInSet(_root, "knights", "caf\xC3\xA9\\b.png"), folder / "caf\xC3\xA9" / "b.png");
    }

    TEST_F(ArtGenSetStoreTest, AbsoluteInSetTakesAnAbsolutePathAsItIs) {
        EXPECT_EQ(AbsoluteInSet(_root, "knights", "/elsewhere/caf\xC3\xA9/x.png"), Path("/elsewhere/caf\xC3\xA9/x.png"));
        // The service wrote it there: the manifest is not asked to prove it is inside
        EXPECT_EQ(AbsoluteInSet(_root, "knights", "/elsewhere/../x.png"), Path("/elsewhere/../x.png"));
    }

    TEST_F(ArtGenSetStoreTest, RelativeToSetKeepsWhatIsNotBelowTheFolder) {
        const Path folder = SetFolder(_root, "caf\xC3\xA9");
        const String inside(Path(folder / "knight" / "b\xC3\xA9.png").View());
        EXPECT_EQ(RelativeToSet(folder, inside), "knight/b\xC3\xA9.png");

        // The text of a folder with the same start is not that folder
        const String neighbour(Path(SetFolder(_root, "caf\xC3\xA9s") / "x.png").View());
        EXPECT_EQ(RelativeToSet(folder, neighbour), neighbour);

        // The path climbs out of the folder and back in, or out for good: normalised first
        const String back(Path(folder / "a" / ".." / "knight" / "x.png").View());
        EXPECT_EQ(RelativeToSet(folder, back), "knight/x.png");
        const String out(Path(folder / ".." / "other" / "x.png").View());
        EXPECT_EQ(RelativeToSet(folder, out), out);

        // The folder itself is not a file of it
        const String same(folder.View());
        EXPECT_EQ(RelativeToSet(folder, same), same);
    }

    TEST_F(ArtGenSetStoreTest, NewSetFolderFreeSaysWhetherTheFolderExists) {
        EXPECT_TRUE(NewSetFolderFree(_root, "knights"));
        ASSERT_TRUE(CreateDirectories(SetFolder(_root, "knights").View()).has_value());
        EXPECT_FALSE(NewSetFolderFree(_root, "knights"));
        EXPECT_TRUE(NewSetFolderFree(_root, "rooks"));
        EXPECT_FALSE(NewSetFolderFree(_root, "../escape"));
        EXPECT_FALSE(NewSetFolderFree(_root, ""));
    }

    TEST_F(ArtGenSetStoreTest, AGameRootWithANonAsciiNameWritesAndLoadsItsSets) {
        const Path game(_root / "caf\xC3\xA9 game");
        ASSERT_TRUE(WriteManifest(game, MakeSet("knights", "2026-11-17T10:00:00Z")).has_value());
        EXPECT_TRUE(GetFileInfo(ManifestPath(game, "knights")).has_value());

        // A folder whose name is no set name is skipped, and so is one without a manifest
        Write(_root / "caf\xC3\xA9 game" / "artgen" / "out" / "bad name" / "manifest.json", "{}");
        ASSERT_TRUE(CreateDirectories(SetFolder(game, "empty").View()).has_value());
        Write(_root / "caf\xC3\xA9 game" / "artgen" / "out" / "note.txt", "not a folder");

        const Vector<ArtGenSet> loaded = LoadSets(game);
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_EQ(loaded[0].name, "knights");

        // A set name is ASCII letters, digits, `_` and `-`: a folder named with other letters is skipped whatever it holds
        ASSERT_TRUE(CreateDirectories(SetFolder(game, "caf\xC3\xA9").View()).has_value());
        Write(_root / "caf\xC3\xA9 game" / "artgen" / "out" / "caf\xC3\xA9" / "manifest.json", Read(ManifestPath(game, "knights")));
        EXPECT_EQ(LoadSets(game).size(), 1u);
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
