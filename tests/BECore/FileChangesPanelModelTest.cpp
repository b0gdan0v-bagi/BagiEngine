#include <BECore/FileSystem/FakeFileWatchBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelBindingReader.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/FileChanges/FileChangesPanelModel.h>
#include <chrono>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The File changes panel's model over a watcher on a fake backend and a real folder
    class FileChangesPanelModelTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _dir = TestTempDirectory("fcpm");
            MakeEmptyDirectory(_dir);
            ASSERT_TRUE(_fileSystem.Mount("fcpmtest"_intern, _dir));
            Write(_dir / "a.xml", "one");
            Write(_dir / "b.xml", "two");
            _watcher.Start(IntrusivePtr<IFileWatchBackend>(New<FakeFileWatchBackend>()));
            _handle = _watcher.WatchFolder("fcpmtest", [](FileChange&) {}, "Test");
            _model.SetWatcher(_watcher);
        }

        void TearDown() override {
            _handle.Reset();
            _watcher.Stop();
            RemoveDirectory(_dir);
        }

        void Write(const Path& path, StringView text) {
            WriteFile(path, text);
            SetWriteTime(path, std::chrono::seconds(2 * ++_writes));
        }

        const FileChangesView& View() const {
            return _model.GetView();
        }

        Path _dir;
        FileSystem _fileSystem;
        FileWatcher _watcher{_fileSystem};
        FileWatchHandle _handle;
        FileChangesPanelModel _model;
        int _writes = 0;
    };

    TEST_F(FileChangesPanelModelTest, BeforeAnyRescanThePanelAsksForOne) {
        EXPECT_TRUE(View().running);
        EXPECT_FALSE(View().notRunning);
        EXPECT_TRUE(View().showPressRescan);
        EXPECT_FALSE(View().showNothingChanged);
        EXPECT_FALSE(View().showPending);
        EXPECT_FALSE(View().canApplyAll);
    }

    TEST_F(FileChangesPanelModelTest, ARescanListsTheChangedFilesAsRows) {
        Write(_dir / "a.xml", "one, rewritten");

        EXPECT_TRUE(_model.RunCommand("fileChanges.rescan", ""));

        EXPECT_FALSE(View().showPressRescan);
        EXPECT_TRUE(View().showPending);
        EXPECT_TRUE(View().canApplyAll);
        ASSERT_EQ(View().pending.size(), 1u);
        EXPECT_EQ(View().pending[0].path, "fcpmtest/a.xml");
        EXPECT_EQ(View().pending[0].kind, "Changed");
        EXPECT_EQ(View().pending[0].consumers, "Test");
    }

    TEST_F(FileChangesPanelModelTest, ARescanThatFindsNothingSaysNothingChanged) {
        EXPECT_TRUE(_model.RunCommand("fileChanges.rescan", ""));

        EXPECT_TRUE(View().showNothingChanged);
        EXPECT_FALSE(View().showPending);
        EXPECT_FALSE(View().canApplyAll);
    }

    TEST_F(FileChangesPanelModelTest, ApplyAllEmptiesTheRowsAndShowsTheResult) {
        Write(_dir / "a.xml", "one, rewritten");
        Write(_dir / "b.xml", "two, rewritten");
        _model.RunCommand("fileChanges.rescan", "");

        EXPECT_TRUE(_model.RunCommand("fileChanges.applyAll", ""));

        EXPECT_TRUE(View().pending.empty());
        EXPECT_FALSE(View().canApplyAll);
        EXPECT_TRUE(View().showApplied);
        ASSERT_EQ(View().applied.size(), 2u);
        EXPECT_EQ(View().applied[0].style, PanelTextStyle::Success);
        EXPECT_TRUE(View().applied[0].error.empty());
    }

    TEST_F(FileChangesPanelModelTest, ApplyOneKeepsTheOtherRows) {
        Write(_dir / "a.xml", "one, rewritten");
        Write(_dir / "b.xml", "two, rewritten");
        _model.RunCommand("fileChanges.rescan", "");

        EXPECT_TRUE(_model.RunCommand("fileChanges.apply", "fcpmtest/a.xml"));

        ASSERT_EQ(View().pending.size(), 1u);
        EXPECT_EQ(View().pending[0].path, "fcpmtest/b.xml");
        ASSERT_EQ(View().applied.size(), 1u);
        EXPECT_EQ(View().applied[0].path, "fcpmtest/a.xml");
    }

    TEST_F(FileChangesPanelModelTest, AFailedConsumerShowsTheErrorStyleAndText) {
        _handle.Reset();
        _handle = _watcher.WatchFolder(
            "fcpmtest",
            [](FileChange& change) {
                change.ok = false;
                change.error = "cannot use it";
            },
            "Picky");
        Write(_dir / "a.xml", "one, rewritten");
        _model.RunCommand("fileChanges.rescan", "");

        _model.RunCommand("fileChanges.applyAll", "");

        ASSERT_EQ(View().applied.size(), 1u);
        EXPECT_EQ(View().applied[0].style, PanelTextStyle::Error);
        EXPECT_EQ(View().applied[0].error, "cannot use it");
    }

    TEST_F(FileChangesPanelModelTest, AnArgumentThatIsNoPendingRowIsIgnoredAndNeverInterned) {
        Write(_dir / "a.xml", "one, rewritten");
        _model.RunCommand("fileChanges.rescan", "");
        const String stranger = FreshText("fcpmtest/stranger");

        EXPECT_TRUE(_model.RunCommand("fileChanges.apply", stranger));
        EXPECT_TRUE(_model.RunCommand("fileChanges.apply", ""));

        EXPECT_EQ(View().pending.size(), 1u);
        EXPECT_TRUE(View().applied.empty());
        EXPECT_FALSE(PoolString::Find(StringView(stranger)).has_value());
    }

    TEST_F(FileChangesPanelModelTest, ACommandOfAnotherModelIsNotTaken) {
        EXPECT_FALSE(_model.RunCommand("game.pause", ""));
        EXPECT_FALSE(_model.RunCommand("fileChanges.unknown", ""));
    }

    TEST_F(FileChangesPanelModelTest, AWatcherThatIsNotRunningGivesNotRunning) {
        _handle.Reset();
        _watcher.Stop();

        _model.Update();

        EXPECT_FALSE(View().running);
        EXPECT_TRUE(View().notRunning);
        EXPECT_FALSE(View().showPressRescan);
        EXPECT_FALSE(View().canApplyAll);
        EXPECT_TRUE(_model.RunCommand("fileChanges.rescan", ""));
        EXPECT_FALSE(View().showNothingChanged);
    }

    TEST_F(FileChangesPanelModelTest, UpdateDoesNotRebuildTheViewWhenNothingMoved) {
        Write(_dir / "a.xml", "one, rewritten");
        _model.RunCommand("fileChanges.rescan", "");
        // The view is the object the asset binds to: a mark left in it survives an Update only if Update did not rebuild it
        auto* view = static_cast<FileChangesView*>(_model.GetObject().Address());
        view->pending[0].path = "mark";

        _model.Update();

        EXPECT_EQ(View().pending[0].path, "mark");
    }

    TEST_F(FileChangesPanelModelTest, UpdateRebuildsTheViewWhenTheWatcherStops) {
        _model.RunCommand("fileChanges.rescan", "");
        EXPECT_TRUE(View().showNothingChanged);
        _handle.Reset();
        _watcher.Stop();

        _model.Update();

        EXPECT_FALSE(View().showNothingChanged);
        EXPECT_TRUE(View().notRunning);
    }

    TEST_F(FileChangesPanelModelTest, TheViewIsReadableThroughABindingOfTheAsset) {
        Write(_dir / "a.xml", "one, rewritten");
        _model.RunCommand("fileChanges.rescan", "");
        PanelDataSources sources;
        const PanelSourceHandle handle = sources.Register("files", [this] { return _model.GetObject(); });
        PanelBindingReader reader;

        bool pending = false;
        EXPECT_EQ(reader.ReadBool(sources, *ParseBinding("files.showPending"), pending), PanelReadStatus::Ok);
        EXPECT_TRUE(pending);

        String scratch;
        StringView path;
        const PanelBinding list = *ParseBinding("files.pending");
        EXPECT_EQ(reader.ReadRowText(sources, list, 0, "path", scratch, path), PanelReadStatus::Ok);
        EXPECT_EQ(path, "fcpmtest/a.xml");
    }

}  // namespace BECore::Tests
