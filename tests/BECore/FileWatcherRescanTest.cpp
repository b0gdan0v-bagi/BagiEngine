#include <BECore/FileSystem/FakeFileWatchBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/New.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <chrono>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr double kSettle = kFileWatchSettleSeconds;

        constexpr double After(double postedAt) {
            return postedAt + 2.0 * kSettle;
        }

    }  // namespace

    /// A watcher over a fake backend and a folder watch on a mount of two layers; the files are real, so a rescan has a disk to look at
    class FileWatcherRescanTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _lower = TestTempDirectory("fwr_lower");
            _upper = TestTempDirectory("fwr_upper");
            MakeEmptyDirectory(_lower);
            MakeEmptyDirectory(_upper);
            _fake = New<FakeFileWatchBackend>();
            _watcher.Start(IntrusivePtrAtomic<IFileWatchBackend>(_fake));
        }

        void TearDown() override {
            _handle.Reset();
            _watcher.Stop();
            RemoveDirectory(_lower);
            RemoveDirectory(_upper);
        }

        /// Writes the file and moves its write time forward by a step of its own, so a rewrite of the same size is a change on a file system with coarse time too
        void Write(const std::filesystem::path& path, StringView text) {
            WriteFile(path, text);
            std::error_code error;
            std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2 * ++_writes), error);
        }

        /// Files of the lower layer that exist before the watch, then the watch of the whole folder
        void WatchFolder() {
            ASSERT_TRUE(_fileSystem.Mount("fwrtest"_intern, _lower));
            Write(_lower / "a.xml", "one");
            Write(_lower / "b.xml", "two");
            _handle = _watcher.WatchFolder(
                "fwrtest",
                [this](FileChange& change) {
                    _seen.push_back(String(change.virtualPath.ToStringView()));
                    if (!_failure.empty()) {
                        change.ok = false;
                        change.error = _failure;
                    }
                },
                "Consumer");
            ASSERT_TRUE(_handle.IsActive());
        }

        static Vector<String> Paths(const Vector<PendingFileChange>& changes) {
            Vector<String> paths;
            for (const PendingFileChange& change : changes) {
                paths.push_back(String(change.virtualPath.ToStringView()));
            }
            return paths;
        }

        std::filesystem::path _lower;
        std::filesystem::path _upper;
        FileSystem _fileSystem;
        FileWatcher _watcher{_fileSystem};
        IntrusivePtrAtomic<FakeFileWatchBackend> _fake;
        FileWatchHandle _handle;
        Vector<String> _seen;
        String _failure;
        int _writes = 0;
    };

    TEST_F(FileWatcherRescanTest, ARescanListsAChangeAnAdditionAndARemovalAndDeliversNothing) {
        ASSERT_NO_FATAL_FAILURE(WatchFolder());
        Write(_lower / "a.xml", "one, rewritten");
        Write(_lower / "c.xml", "three");
        std::error_code error;
        ASSERT_TRUE(std::filesystem::remove(_lower / "b.xml", error)) << error.message();

        const Vector<PendingFileChange> changes = _watcher.Rescan();

        ASSERT_EQ(changes.size(), 3u);
        EXPECT_EQ(changes[0].virtualPath, "fwrtest/a.xml"_intern);
        EXPECT_EQ(changes[0].kind, FileChangeKind::Changed);
        EXPECT_EQ(changes[1].virtualPath, "fwrtest/b.xml"_intern);
        EXPECT_EQ(changes[1].kind, FileChangeKind::Removed);
        EXPECT_EQ(changes[2].virtualPath, "fwrtest/c.xml"_intern);
        EXPECT_EQ(changes[2].kind, FileChangeKind::Added);
        ASSERT_EQ(changes[0].consumers.size(), 1u);
        EXPECT_EQ(changes[0].consumers[0], "Consumer"_intern);

        _watcher.Update(0.0);
        _watcher.Update(After(0.0));
        EXPECT_TRUE(_seen.empty()) << "a rescan only looks";
        EXPECT_EQ(_watcher.Rescan().size(), 3u) << "and looking again changes nothing";
    }

    TEST_F(FileWatcherRescanTest, ApplyDeliversEachChangeOnceAndARescanAfterwardsListsNothing) {
        ASSERT_NO_FATAL_FAILURE(WatchFolder());
        Write(_lower / "a.xml", "one, rewritten");
        Write(_lower / "c.xml", "three");

        const Vector<AppliedFileChange> applied = _watcher.Apply();

        ASSERT_EQ(applied.size(), 2u);
        EXPECT_TRUE(applied[0].ok);
        EXPECT_EQ(_seen.size(), 2u);
        EXPECT_TRUE(_watcher.Rescan().empty());
        EXPECT_TRUE(_watcher.Apply().empty());
        EXPECT_EQ(_seen.size(), 2u);
    }

    TEST_F(FileWatcherRescanTest, ApplyOfListedPathsLeavesTheOthersPending) {
        ASSERT_NO_FATAL_FAILURE(WatchFolder());
        Write(_lower / "a.xml", "one, rewritten");
        Write(_lower / "c.xml", "three");

        const StringView only[] = {"fwrtest/c.xml"};
        const Vector<AppliedFileChange> applied = _watcher.Apply(Span<const StringView>(only, 1));

        ASSERT_EQ(applied.size(), 1u);
        EXPECT_EQ(applied[0].virtualPath, "fwrtest/c.xml"_intern);
        const Vector<PendingFileChange> left = _watcher.Rescan();
        ASSERT_EQ(left.size(), 1u);
        EXPECT_EQ(left[0].virtualPath, "fwrtest/a.xml"_intern);
    }

    TEST_F(FileWatcherRescanTest, AChangeTheNativeNotificationDeliveredIsNotListed) {
        ASSERT_NO_FATAL_FAILURE(WatchFolder());
        Write(_lower / "a.xml", "one, rewritten");
        _fake->Emit(_lower / "a.xml");
        _watcher.Update(0.0);
        _watcher.Update(After(0.0));
        ASSERT_EQ(_seen.size(), 1u);

        EXPECT_TRUE(_watcher.Rescan().empty());
    }

    TEST_F(FileWatcherRescanTest, ApplyTakesTheNoticeOfTheStreamThatIsStillSettlingSoNothingIsDeliveredTwice) {
        ASSERT_NO_FATAL_FAILURE(WatchFolder());
        Write(_lower / "a.xml", "one, rewritten");
        _fake->Emit(_lower / "a.xml");
        _watcher.Update(0.0);
        ASSERT_TRUE(_seen.empty()) << "inside the settle window";

        EXPECT_EQ(_watcher.Apply().size(), 1u);
        _watcher.Update(After(0.0));
        _watcher.Update(After(1.0));
        EXPECT_EQ(_seen.size(), 1u);
    }

    TEST_F(FileWatcherRescanTest, AConsumerThatCouldNotUseTheChangeIsReportedWithItsReason) {
        ASSERT_NO_FATAL_FAILURE(WatchFolder());
        _failure = "does not parse";
        Write(_lower / "a.xml", "one, rewritten");

        const Vector<AppliedFileChange> applied = _watcher.Apply();

        ASSERT_EQ(applied.size(), 1u);
        EXPECT_FALSE(applied[0].ok);
        EXPECT_EQ(applied[0].error, "does not parse");
        EXPECT_TRUE(_watcher.Rescan().empty()) << "reported once, the baseline moved";
    }

    TEST_F(FileWatcherRescanTest, TheLayersOfOneVirtualPathAreOneItem) {
        ASSERT_TRUE(_fileSystem.Mount("fwrtest"_intern, _lower));
        ASSERT_TRUE(_fileSystem.Mount("fwrtest"_intern, _upper));
        Write(_lower / "a.xml", "lower");
        _handle = _watcher.WatchFolder("fwrtest", [this](FileChange& change) { _seen.push_back(String(change.virtualPath.ToStringView())); });
        ASSERT_TRUE(_handle.IsActive());

        Write(_upper / "a.xml", "upper");  // the file now exists in both layers: the consumer already had a.xml
        Write(_lower / "a.xml", "lower, rewritten");
        const Vector<PendingFileChange> changes = _watcher.Rescan();

        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes[0].kind, FileChangeKind::Changed);
        EXPECT_EQ(_watcher.Apply().size(), 1u);
        EXPECT_EQ(_seen.size(), 1u);
        EXPECT_TRUE(_watcher.Rescan().empty());
    }

    TEST_F(FileWatcherRescanTest, AFileWatchIsListedByItsOwnPathAndNamedByItsConsumer) {
        ASSERT_TRUE(_fileSystem.Mount("fwrtest"_intern, _lower));
        Write(_lower / "a.xml", "one");
        _handle = _watcher.WatchFile("fwrtest/a.xml", [this](FileChange& change) { _seen.push_back(String(change.virtualPath.ToStringView())); });
        Write(_lower / "a.xml", "one, rewritten");
        Write(_lower / "other.xml", "not watched");

        const Vector<PendingFileChange> changes = _watcher.Rescan();

        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes[0].virtualPath, "fwrtest/a.xml"_intern);
        ASSERT_EQ(changes[0].consumers.size(), 1u);
        EXPECT_EQ(changes[0].consumers[0], "fwrtest/a.xml"_intern) << "without a consumer name the watched path stands for it";
    }

    TEST_F(FileWatcherRescanTest, AWatcherThatIsNotRunningFindsNothing) {
        FileWatcher idle(_fileSystem);
        EXPECT_TRUE(idle.Rescan().empty());
        EXPECT_TRUE(idle.Apply().empty());
    }

}  // namespace BECore::Tests
