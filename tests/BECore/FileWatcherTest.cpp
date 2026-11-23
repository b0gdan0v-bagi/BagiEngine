#include <BECore/FileSystem/FakeFileWatchBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/New.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        constexpr double kSettle = kFileWatchSettleSeconds;

        /// A time that is surely past the settle window of a post made at @p postedAt
        constexpr double After(double postedAt) {
            return postedAt + 2.0 * kSettle;
        }

    }  // namespace

    /// Two layers of one mount point over temporary folders, a watcher over a fake backend: no thread, no sleep, the clock is a parameter
    class FileWatcherTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _lower = TestTempDirectory("fw_lower");
            _upper = TestTempDirectory("fw_upper");
            MakeEmptyDirectory(_lower);
            MakeEmptyDirectory(_upper);
            _fake = New<FakeFileWatchBackend>();
            _watcher.Start(IntrusivePtr<IFileWatchBackend>(_fake));
        }

        void TearDown() override {
            _watcher.Stop();
            RemoveDirectory(_lower);
            RemoveDirectory(_upper);
        }

        void MountLower() {
            ASSERT_TRUE(_fileSystem.Mount("fwtest"_intern, _lower));
        }

        void MountBoth() {
            ASSERT_TRUE(_fileSystem.Mount("fwtest"_intern, _lower));
            ASSERT_TRUE(_fileSystem.Mount("fwtest"_intern, _upper));
        }

        FileWatchCallback Record() {
            return [this](const FileChange& change) { _seen.push_back(String(change.virtualPath.ToStringView())); };
        }

        std::filesystem::path _lower;
        std::filesystem::path _upper;
        FileSystem _fileSystem;
        FileWatcher _watcher{_fileSystem};
        IntrusivePtr<FakeFileWatchBackend> _fake;
        Vector<String> _seen;
    };

    TEST_F(FileWatcherTest, ChangeOfAWatchedFileNotifiesOnceWithTheVirtualPath) {
        MountLower();
        FileWatchHandle handle = _watcher.WatchFile("fwtest/a.xml", Record());
        ASSERT_TRUE(handle.IsActive());

        _fake->Emit(_lower / "a.xml");
        _watcher.Update(0.0);
        _watcher.Update(kSettle / 2);
        EXPECT_TRUE(_seen.empty()) << "inside the settle window";

        _watcher.Update(After(0.0));
        _watcher.Update(After(1.0));
        ASSERT_EQ(_seen.size(), 1u);
        EXPECT_EQ(_seen[0], "fwtest/a.xml");
    }

    TEST_F(FileWatcherTest, AFileWithANonAsciiNameInANonAsciiFolderNotifiesOnceUnderItsVirtualPath) {
        MountLower();
        const Path folder = Path(_lower) / "Папка_é";
        MakeDirectory(folder);
        FileWatchHandle handle = _watcher.WatchFile("fwtest/Папка_é/данные_é.xml", Record());
        ASSERT_TRUE(handle.IsActive());

        _fake->Emit(folder / "данные_é.xml");
        _fake->Emit(folder / "другой_é.xml");
        _watcher.Update(0.0);
        _watcher.Update(After(0.0));

        ASSERT_EQ(_seen.size(), 1u);
        EXPECT_EQ(_seen[0], "fwtest/Папка_é/данные_é.xml");
    }

    TEST_F(FileWatcherTest, AFolderWatchReportsANonAsciiFileOfANestedNonAsciiFolderOnce) {
        MountLower();
        const Path nested = Path(_lower) / "Папка_é" / "вложенная_é";
        MakeDirectory(nested);
        FileWatchHandle handle = _watcher.WatchFolder("fwtest/Папка_é", Record());
        ASSERT_TRUE(handle.IsActive());

        _fake->Emit(nested / "данные_é.png");
        _fake->Emit(nested / "данные_é.png");
        _watcher.Update(0.0);
        _watcher.Update(After(0.0));

        ASSERT_EQ(_seen.size(), 1u);
        EXPECT_EQ(_seen[0], "fwtest/Папка_é/вложенная_é/данные_é.png");
    }

    TEST_F(FileWatcherTest, ABurstIsOneCallbackAndTwoBurstsApartAreTwo) {
        MountLower();
        FileWatchHandle handle = _watcher.WatchFile("fwtest/a.xml", Record());

        double now = 0.0;
        for (int i = 0; i < 10; ++i) {
            _fake->Emit(_lower / "a.xml");
            _watcher.Update(now);
            now += kSettle / 4;  // every post moves the window: ten of them last longer than it
        }
        EXPECT_TRUE(_seen.empty());
        _watcher.Update(After(now));
        EXPECT_EQ(_seen.size(), 1u);

        now = After(now) + 1.0;
        _fake->Emit(_lower / "a.xml");
        _watcher.Update(now);
        _watcher.Update(After(now));
        EXPECT_EQ(_seen.size(), 2u);
    }

    TEST_F(FileWatcherTest, AFolderWatchReportsAFileInASubfolderAndNotTheFolderItself) {
        MountLower();
        MakeDirectory(_lower / "sub");
        FileWatchHandle handle = _watcher.WatchFolder("fwtest", Record());

        _fake->Emit(_lower / "sub" / "x.png");
        _fake->Emit(_lower);
        _watcher.Update(0.0);
        _watcher.Update(After(0.0));
        ASSERT_EQ(_seen.size(), 1u);
        EXPECT_EQ(_seen[0], "fwtest/sub/x.png");
        ASSERT_EQ(_fake->AddedDirectories().size(), 1u);
        EXPECT_TRUE(_fake->AddedDirectories()[0].recursive);
    }

    TEST_F(FileWatcherTest, ARenameOverSaveNotifiesForTheTargetAndNeverForTheTempName) {
        MountLower();
        FileWatchHandle handle = _watcher.WatchFile("fwtest/a.xml", Record());

        _fake->Emit(_lower / "a.xml.tmp");
        _watcher.Update(0.0);
        _fake->Emit(_lower / "a.xml");
        _watcher.Update(kSettle / 4);
        _watcher.Update(After(kSettle / 4));
        ASSERT_EQ(_seen.size(), 1u);
        EXPECT_EQ(_seen[0], "fwtest/a.xml");
    }

    TEST_F(FileWatcherTest, AFileThatIsNotWatchedDoesNotNotify) {
        MountLower();
        FileWatchHandle handle = _watcher.WatchFile("fwtest/a.xml", Record());

        _fake->Emit(_lower / "b.xml");
        _fake->Emit(_upper / "a.xml");  // another folder that is no layer
        _watcher.Update(0.0);
        _watcher.Update(After(0.0));
        EXPECT_TRUE(_seen.empty());
    }

    TEST_F(FileWatcherTest, AFileWatchedInTwoLayersReportsOneVirtualPath) {
        MountBoth();
        FileWatchHandle handle = _watcher.WatchFile("fwtest/a.xml", Record());
        EXPECT_EQ(_fake->AddedDirectories().size(), 2u);

        _fake->Emit(_lower / "a.xml");
        _fake->Emit(_upper / "a.xml");
        _watcher.Update(0.0);
        _watcher.Update(After(0.0));
        ASSERT_EQ(_seen.size(), 1u);
        EXPECT_EQ(_seen[0], "fwtest/a.xml");
    }

    TEST_F(FileWatcherTest, ThePostComesFromAnotherThreadAndTheCallbackRunsOnTheThreadOfUpdate) {
        MountLower();
        std::thread::id callbackThread;
        FileWatchHandle handle = _watcher.WatchFile("fwtest/a.xml", [&callbackThread](const FileChange&) { callbackThread = std::this_thread::get_id(); });

        Thread poster("TestPoster", [this] { _fake->Emit(_lower / "a.xml"); });
        poster.join();
        _watcher.Update(0.0);
        _watcher.Update(After(0.0));
        EXPECT_EQ(callbackThread, std::this_thread::get_id());
    }

    TEST_F(FileWatcherTest, AHandleThatEndsStopsTheNotificationsAndReleasesTheDirectoryOnce) {
        MountLower();
        FileWatchHandle handle = _watcher.WatchFile("fwtest/a.xml", Record());

        _fake->Emit(_lower / "a.xml");
        _watcher.Update(0.0);
        handle.Reset();  // the change is waiting in the settle window
        EXPECT_FALSE(handle.IsActive());
        EXPECT_EQ(_fake->RemovedDirectories().size(), 1u);

        _watcher.Update(After(0.0));
        EXPECT_TRUE(_seen.empty());
        _fake->Emit(_lower / "a.xml");
        _watcher.Update(After(1.0));
        _watcher.Update(After(2.0));
        EXPECT_TRUE(_seen.empty());
    }

    TEST_F(FileWatcherTest, AHandleDestroyedInsideItsOwnCallbackEndsTheWatchAfterTheUpdate) {
        MountLower();
        int calls = 0;
        FileWatchHandle handle;
        handle = _watcher.WatchFile("fwtest/a.xml", [&](const FileChange&) {
            ++calls;
            handle = FileWatchHandle();
            EXPECT_TRUE(_fake->RemovedDirectories().empty()) << "removal waits for the end of Update";
        });

        _fake->Emit(_lower / "a.xml");
        _watcher.Update(0.0);
        _watcher.Update(After(0.0));
        EXPECT_EQ(calls, 1);
        EXPECT_FALSE(handle.IsActive());
        EXPECT_EQ(_fake->RemovedDirectories().size(), 1u);

        _fake->Emit(_lower / "a.xml");
        _watcher.Update(After(1.0));
        _watcher.Update(After(2.0));
        EXPECT_EQ(calls, 1);
    }

    TEST_F(FileWatcherTest, ACallbackThatAddsWatchesLeavesTheRunningCallbackAlone) {
        MountLower();
        Vector<FileWatchHandle> added;
        int calls = 0;
        FileWatchHandle handle = _watcher.WatchFile("fwtest/a.xml", [&](const FileChange&) {
            // More watches than the storage holds: it grows while this callable runs
            for (int i = 0; i < 64; ++i) {
                added.push_back(_watcher.WatchFile("fwtest/other.xml", Record()));
            }
            ++calls;
        });

        _fake->Emit(_lower / "a.xml");
        _watcher.Update(0.0);
        _watcher.Update(After(0.0));
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(added.size(), 64u);
    }

    TEST_F(FileWatcherTest, WatchesOnOneDirectoryAddItOnceAndReleaseItWithTheLastWatch) {
        MountLower();
        FileWatchHandle first = _watcher.WatchFile("fwtest/a.xml", Record());
        FileWatchHandle second = _watcher.WatchFile("fwtest/b.xml", Record());
        EXPECT_EQ(_fake->AddedDirectories().size(), 1u);

        first.Reset();
        EXPECT_TRUE(_fake->RemovedDirectories().empty());
        second.Reset();
        EXPECT_EQ(_fake->RemovedDirectories().size(), 1u);
        EXPECT_TRUE(_fake->Directories().empty());
    }

    TEST_F(FileWatcherTest, AFolderWatchMakesTheSharedDirectoryRecursiveWhileItLasts) {
        MountLower();
        FileWatchHandle file = _watcher.WatchFile("fwtest/a.xml", Record());
        FileWatchHandle folder = _watcher.WatchFolder("fwtest", Record());
        ASSERT_EQ(_fake->Directories().size(), 1u);
        ASSERT_FALSE(_fake->AddedDirectories().empty());
        EXPECT_TRUE(_fake->AddedDirectories().back().recursive);

        folder.Reset();
        ASSERT_EQ(_fake->Directories().size(), 1u);
        EXPECT_FALSE(_fake->AddedDirectories().back().recursive);
    }

    TEST_F(FileWatcherTest, AWatcherThatIsNotRunningGivesInactiveHandlesAndUpdatesWithoutAllocating) {
        MountLower();
        FileWatcher idle(_fileSystem);
        FileWatchHandle handle = idle.WatchFile("fwtest/a.xml", Record());
        EXPECT_FALSE(handle.IsActive());
        EXPECT_FALSE(idle.IsRunning());
        ExpectNoAllocations([&idle] {
            idle.Update();
            idle.Update(1.0);
        });
    }

    TEST_F(FileWatcherTest, AnUpdateWithNothingPostedAllocatesNothing) {
        MountLower();
        FileWatchHandle handle = _watcher.WatchFile("fwtest/a.xml", Record());
        _fake->Emit(_lower / "a.xml");
        _watcher.Update(0.0);
        _watcher.Update(After(0.0));  // the first delivery grew the vectors; a quiet frame after it is the steady state
        ExpectNoAllocations([this] {
            _watcher.Update(After(1.0));
            _watcher.Update(After(2.0));
        });
    }

    TEST_F(FileWatcherTest, AWatchOnAPathWithoutAFolderIsInactive) {
        MountLower();
        EXPECT_FALSE(_watcher.WatchFile("fwtest/missing/a.xml", Record()).IsActive());
        EXPECT_FALSE(_watcher.WatchFile("nomount/a.xml", Record()).IsActive());
        EXPECT_FALSE(_watcher.WatchFile("fwtest", Record()).IsActive());
        EXPECT_TRUE(_fake->AddedDirectories().empty());
    }

    TEST_F(FileWatcherTest, StopEndsEveryWatchAndShutsTheBackendDown) {
        MountLower();
        FileWatchHandle handle = _watcher.WatchFile("fwtest/a.xml", Record());
        _fake->Emit(_lower / "a.xml");
        _watcher.Update(0.0);

        _watcher.Stop();
        EXPECT_FALSE(handle.IsActive());
        EXPECT_FALSE(_watcher.IsRunning());
        EXPECT_EQ(_fake->ShutdownCount(), 1);
        EXPECT_FALSE(_fake->HasSink());
        _watcher.Update(After(0.0));
        EXPECT_TRUE(_seen.empty());
    }

}  // namespace BECore::Tests
