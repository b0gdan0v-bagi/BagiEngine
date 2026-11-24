#include <BECore/FileSystem/FakeFileWatchBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/RefCounted/New.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/FileChanges/FileChangesModel.h>
#include <chrono>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The panel's logic over a watcher on a fake backend and a real folder
    class FileChangesModelTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _dir = Path(TestTempDirectory("fcm"));
            MakeEmptyDirectory(_dir);
            ASSERT_TRUE(_fileSystem.Mount("fcmtest"_intern, _dir));
            Write(_dir / "a.xml", "one");
            Write(_dir / "b.xml", "two");
            _watcher.Start(IntrusivePtr<IFileWatchBackend>(New<FakeFileWatchBackend>()));
            _handle = _watcher.WatchFolder("fcmtest", [this](FileChange& change) { _delivered.push_back(String(change.virtualPath.ToStringView())); }, "Test");
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

        Path _dir;
        FileSystem _fileSystem;
        FileWatcher _watcher{_fileSystem};
        FileWatchHandle _handle;
        FileChangesModel _model;
        Vector<String> _delivered;
        int _writes = 0;
    };

    TEST_F(FileChangesModelTest, ARescanFillsTheListAndTheModelRemembersItLooked) {
        EXPECT_FALSE(_model.HasScanned());
        Write(_dir / "a.xml", "one, rewritten");

        _model.Rescan(_watcher);

        EXPECT_TRUE(_model.HasScanned());
        ASSERT_EQ(_model.GetPending().size(), 1u);
        EXPECT_EQ(_model.GetPending()[0].virtualPath, "fcmtest/a.xml"_intern);
        EXPECT_TRUE(_delivered.empty());
    }

    TEST_F(FileChangesModelTest, ApplyingOneRowKeepsTheOthersAndShowsItsResult) {
        Write(_dir / "a.xml", "one, rewritten");
        Write(_dir / "b.xml", "two, rewritten");
        _model.Rescan(_watcher);
        ASSERT_EQ(_model.GetPending().size(), 2u);

        _model.ApplyOne(_watcher, "fcmtest/a.xml"_intern);

        ASSERT_EQ(_model.GetPending().size(), 1u);
        EXPECT_EQ(_model.GetPending()[0].virtualPath, "fcmtest/b.xml"_intern);
        ASSERT_EQ(_model.GetApplied().size(), 1u);
        EXPECT_EQ(_model.GetApplied()[0].virtualPath, "fcmtest/a.xml"_intern);
        EXPECT_EQ(_delivered.size(), 1u);
    }

    TEST_F(FileChangesModelTest, ApplyAllEmptiesTheListAndAFollowingRescanFindsNothing) {
        Write(_dir / "a.xml", "one, rewritten");
        Write(_dir / "b.xml", "two, rewritten");
        _model.Rescan(_watcher);

        _model.ApplyAll(_watcher);

        EXPECT_TRUE(_model.GetPending().empty());
        EXPECT_EQ(_model.GetApplied().size(), 2u);
        EXPECT_EQ(_delivered.size(), 2u);
        _model.Rescan(_watcher);
        EXPECT_TRUE(_model.GetPending().empty());
    }

}  // namespace BECore::Tests
