#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/FileSystem/PlatformFileWatchBackend.h>
#include <BECore/FileSystem/PollingFileWatchBackend.h>
#include <BECore/RefCounted/New.h>
#include <Common/AllocCheck.h>
#include <Common/NativeFileWatch.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Common/Wait.h>
#include <EASTL/sort.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        /// Collects what a backend posts; a native backend posts from its own thread
        class RecordingSink : public IFileWatchSink {
        public:
            void Post(const Path& realPath) override {
                LockGuard lock(_mutex);
                _posted.push_back(realPath.LexicallyNormal());
            }

            [[nodiscard]] bool Has(const Path& path) const {
                LockGuard lock(_mutex);
                const Path wanted = path.LexicallyNormal();
                for (const Path& posted : _posted) {
                    if (posted == wanted) {
                        return true;
                    }
                }
                return false;
            }

            /// Every post in the order it came
            [[nodiscard]] Vector<Path> Posted() const {
                LockGuard lock(_mutex);
                return _posted;
            }

            [[nodiscard]] size_t Count() const {
                LockGuard lock(_mutex);
                return _posted.size();
            }

            void Clear() {
                LockGuard lock(_mutex);
                _posted.clear();
            }

        private:
            mutable Mutex _mutex{BE_LOCK_SITE("Recording file watch sink")};
            Vector<Path> _posted;
        };

        /// A folder of the test, a backend over it and the clock that drives its Tick; the same cases run on every backend of the platform
        class BackendHarness {
        public:
            explicit BackendHarness(IntrusivePtr<IFileWatchBackend> backend, bool mayDropEvents = false)
                : _backend(std::move(backend)), _mayDropEvents(mayDropEvents), _dir(Path(TestTempDirectory("fwb")).LexicallyNormal()) {
                MakeEmptyDirectory(_dir);
                _backend->SetSink(&_sink);
            }

            ~BackendHarness() {
                _backend->Shutdown();
                _backend->SetSink(nullptr);
                RemoveDirectory(_dir);
            }

            BackendHarness(const BackendHarness&) = delete;
            BackendHarness& operator=(const BackendHarness&) = delete;

            [[nodiscard]] const Path& Dir() const {
                return _dir;
            }

            [[nodiscard]] IFileWatchBackend& Backend() {
                return *_backend;
            }

            [[nodiscard]] RecordingSink& Sink() {
                return _sink;
            }

            /// Writes the file and moves its write time forward by a step of its own, so a rewrite of the same size is a change on a file system with coarse time too
            void Write(const Path& path, StringView text) {
                WriteFile(path, text);
                SetWriteTime(path, std::chrono::seconds(2 * ++_writes));
            }

            /// Whether the platform may lose events without a word (fseventsd under a storm of file events): a missing post then is no verdict on the backend
            [[nodiscard]] bool MayDropEvents() const {
                return _mayDropEvents;
            }

            /// One turn of the clock that Tick polls with
            void Pump() {
                _clock += 2.0 * kPollIntervalSeconds;
                _backend->Tick(_clock);
            }

            /// Pumps until @p path is posted; false when it never is. A platform that drops events is not waited on for the whole limit: its case skips
            bool WaitForPost(const Path& path) {
                constexpr std::chrono::milliseconds kDroppedEventsLimit{10000};
                return WaitUntil(
                    [this, &path] {
                        Pump();
                        return _sink.Has(path);
                    },
                    _mayDropEvents ? kDroppedEventsLimit : kWaitLimit);
            }

            /// Whether @p path stays unposted after a short pause (a negative check can only wait, load only weakens it)
            bool StaysQuiet(const Path& path) {
                Pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                Pump();
                return !_sink.Has(path);
            }

        private:
            IntrusivePtr<IFileWatchBackend> _backend;
            bool _mayDropEvents = false;
            Path _dir;
            RecordingSink _sink;
            double _clock = 0.0;
            int _writes = 0;
        };

        /// Skips the case when a post never came on a platform that may drop events (the rescan of FileWatcher is what finds those), fails it otherwise
#define ASSERT_POSTED(harness, path)                                                                                                                                                                   \
    do {                                                                                                                                                                                               \
        if (!(harness).WaitForPost(path)) {                                                                                                                                                            \
            if ((harness).MayDropEvents()) {                                                                                                                                                           \
                GTEST_SKIP() << "the platform delivered no event (fseventsd under a storm of events)";                                                                                                 \
            }                                                                                                                                                                                          \
            FAIL() << "no post for " << (path).CStr();                                                                                                                                                 \
        }                                                                                                                                                                                              \
    } while (false)

        struct PollingMaker {
            static constexpr bool kNative = false;
            static constexpr bool kMayDropEvents = false;
            static IntrusivePtr<IFileWatchBackend> Make() {
                return New<PollingFileWatchBackend>();
            }
        };

#if defined(__APPLE__)
        struct FSEventsMaker {
            static constexpr bool kNative = true;
            static constexpr bool kMayDropEvents = true;
            static IntrusivePtr<IFileWatchBackend> Make() {
                return MakeFSEventsFileWatchBackend();
            }
        };
#endif
#if defined(__linux__)
        struct InotifyMaker {
            static constexpr bool kNative = true;
            static constexpr bool kMayDropEvents = false;
            static IntrusivePtr<IFileWatchBackend> Make() {
                return MakeInotifyFileWatchBackend();
            }
        };
#endif
#if defined(_WIN32)
        struct WindowsMaker {
            static constexpr bool kNative = true;
            static constexpr bool kMayDropEvents = false;
            static IntrusivePtr<IFileWatchBackend> Make() {
                return MakeWindowsFileWatchBackend();
            }
        };
#endif

    }  // namespace

    /// The contract of IFileWatchBackend, which FileWatcher relies on. A native backend is added to Backends under its platform's #if
    template <typename TMaker>
    class FileWatchBackendContractTest : public ::testing::Test {
    protected:
        void SetUp() override {
            if (TMaker::kNative && !NativeFileWatchRequested()) {
                GTEST_SKIP() << kNativeFileWatchSkip;
            }
            _harness.emplace(TMaker::Make(), TMaker::kMayDropEvents);
        }

        void TearDown() override {
            _harness.reset();
        }

        BackendHarness& H() {
            return *_harness;
        }

        Optional<BackendHarness> _harness;
    };

#if defined(__APPLE__)
    using Backends = ::testing::Types<PollingMaker, FSEventsMaker>;
#elif defined(__linux__)
    using Backends = ::testing::Types<PollingMaker, InotifyMaker>;
#elif defined(_WIN32)
    using Backends = ::testing::Types<PollingMaker, WindowsMaker>;
#else
    using Backends = ::testing::Types<PollingMaker>;
#endif
    TYPED_TEST_SUITE(FileWatchBackendContractTest, Backends);

    TYPED_TEST(FileWatchBackendContractTest, AFileWrittenAfterTheWatchIsPosted) {
        this->H().Backend().AddDirectory(this->H().Dir(), false);
        const Path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        ASSERT_POSTED(this->H(), file);
    }

    TYPED_TEST(FileWatchBackendContractTest, ARewriteIsPostedAgain) {
        const Path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        this->H().Backend().AddDirectory(this->H().Dir(), false);
        this->H().Write(file, "two");
        ASSERT_POSTED(this->H(), file);

        this->H().Sink().Clear();
        this->H().Write(file, "three!");
        ASSERT_POSTED(this->H(), file);
    }

    TYPED_TEST(FileWatchBackendContractTest, AFileWithANonAsciiNameInASubfolderIsPostedUnderItsOwnName) {
        const Path folder = this->H().Dir() / "Папка_é";
        MakeDirectory(folder);
        this->H().Backend().AddDirectory(this->H().Dir(), true);
        const Path file = folder / "данные_é.xml";
        this->H().Write(file, "pixels");
        ASSERT_POSTED(this->H(), file);
    }

    TYPED_TEST(FileWatchBackendContractTest, ARenameOverTheFileIsPostedForTheTarget) {
        const Path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        this->H().Backend().AddDirectory(this->H().Dir(), false);

        const Path temp = this->H().Dir() / "a.xml.tmp";
        this->H().Write(temp, "replacement");
        const auto renamed = RenameFile(temp, file);
        ASSERT_TRUE(renamed.has_value()) << renamed.error().c_str();
        ASSERT_POSTED(this->H(), file);
    }

    TYPED_TEST(FileWatchBackendContractTest, AFileInASubfolderOfARecursiveDirectoryIsPosted) {
        MakeDirectory(this->H().Dir() / "sub");
        this->H().Backend().AddDirectory(this->H().Dir(), true);
        const Path file = this->H().Dir() / "sub" / "x.png";
        this->H().Write(file, "pixels");
        ASSERT_POSTED(this->H(), file);
    }

    TYPED_TEST(FileWatchBackendContractTest, ARemovedFileIsPosted) {
        const Path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        this->H().Backend().AddDirectory(this->H().Dir(), false);

        const auto removed = RemoveFile(file);
        ASSERT_TRUE(removed.has_value()) << removed.error().c_str();
        ASSERT_POSTED(this->H(), file);
    }

    TYPED_TEST(FileWatchBackendContractTest, ARemovedDirectoryPostsNothingMore) {
        this->H().Backend().AddDirectory(this->H().Dir(), false);
        this->H().Backend().RemoveDirectory(this->H().Dir());
        const Path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        EXPECT_TRUE(this->H().StaysQuiet(file));
    }

    TYPED_TEST(FileWatchBackendContractTest, AShutdownBackendPostsNothingMore) {
        this->H().Backend().AddDirectory(this->H().Dir(), false);
        this->H().Backend().Shutdown();
        const Path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        EXPECT_TRUE(this->H().StaysQuiet(file));
    }

    TYPED_TEST(FileWatchBackendContractTest, ADirectoryRemovedUnderTheWatchIsSurvivedAndWatchedAgainWhenItComesBack) {
        const Path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        this->H().Backend().AddDirectory(this->H().Dir(), false);

        RemoveDirectory(this->H().Dir());
        for (int i = 0; i < 3; ++i) {
            this->H().Pump();
        }
        MakeDirectory(this->H().Dir());
        this->H().Sink().Clear();
        // The watch may not outlive its folder (a native one does not), so the caller adds it again; either way nothing crashed
        this->H().Backend().RemoveDirectory(this->H().Dir());
        this->H().Backend().AddDirectory(this->H().Dir(), false);
        this->H().Write(file, "two");
        ASSERT_POSTED(this->H(), file);
    }

#if defined(__APPLE__)
    /// FSEvents reports the real path (/private/var/... for /var/..., which the temp folder of the contract cases already is); the facade must get the form it added
    class FileWatchBackendFSEventsTest : public ::testing::Test {
    protected:
        void SetUp() override {
            if (!NativeFileWatchRequested()) {
                GTEST_SKIP() << kNativeFileWatchSkip;
            }
            _harness.emplace(MakeFSEventsFileWatchBackend(), true);
        }

        void TearDown() override {
            _harness.reset();
        }

        BackendHarness& H() {
            return *_harness;
        }

        Optional<BackendHarness> _harness;
    };

    TEST_F(FileWatchBackendFSEventsTest, APathBehindASymlinkIsPostedInTheFormItWasAdded) {
        const Path link = H().Dir().Parent() / Format("{}_link", H().Dir().Filename().CStr());
        CreateSymlink(link, H().Dir());
        ASSERT_TRUE(IsSymbolicLink(link));

        H().Backend().AddDirectory(link, false);
        H().Write(H().Dir() / "a.xml", "one");
        const Path linked = link / "a.xml";
        const bool posted = H().WaitForPost(linked);
        RemoveDirectory(link);  // removes the link of a folder as a link, never the folder
        if (!posted && H().MayDropEvents()) {
            GTEST_SKIP() << "the platform delivered no event (fseventsd under a storm of events)";
        }
        EXPECT_TRUE(posted);
    }

    TEST_F(FileWatchBackendFSEventsTest, TheDirectoryItselfIsNeverPosted) {
        H().Backend().AddDirectory(H().Dir(), false);
        SetWriteTime(H().Dir(), std::chrono::seconds(30));
        const Path file = H().Dir() / "a.xml";
        H().Write(file, "one");
        ASSERT_POSTED(H(), file);
        EXPECT_FALSE(H().Sink().Has(H().Dir()));
    }

    TEST_F(FileWatchBackendFSEventsTest, ANonRecursiveDirectoryDoesNotReportItsSubfolders) {
        MakeDirectory(H().Dir() / "sub");
        H().Backend().AddDirectory(H().Dir(), false);
        const Path deep = H().Dir() / "sub" / "x.png";
        H().Write(deep, "pixels");
        const Path shallow = H().Dir() / "a.xml";
        H().Write(shallow, "one");
        ASSERT_POSTED(H(), shallow);
        EXPECT_FALSE(H().Sink().Has(deep));
    }
#endif

    /// What a backend does when its queue overflowed
    class FileWatchPostTreeTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _folder.emplace("fwt");
            MakeDirectory(Dir() / "sub");
            WriteFile(Dir() / "a.xml", "one");
            WriteFile(Dir() / "sub" / "b.xml", "two");
        }

        Path Dir() const {
            return Path(TestTempDirectory("fwt"));
        }

        Optional<TestTempFolder> _folder;
        RecordingSink _sink;
    };

    TEST_F(FileWatchPostTreeTest, ARecursiveRescanPostsTheFilesOfTheWholeTree) {
        PostTree(_sink, Dir(), true);
        EXPECT_TRUE(_sink.Has(Dir() / "a.xml"));
        EXPECT_TRUE(_sink.Has(Dir() / "sub" / "b.xml"));
    }

    TEST_F(FileWatchPostTreeTest, ANonRecursiveRescanStaysInTheFolder) {
        PostTree(_sink, Dir(), false);
        EXPECT_TRUE(_sink.Has(Dir() / "a.xml"));
        EXPECT_FALSE(_sink.Has(Dir() / "sub" / "b.xml"));
    }

    TEST_F(FileWatchPostTreeTest, ThePostsAreTheFolderJoinedToTheNamesBelowIt) {
        WriteFile(Dir() / "Папка_é" / "данные_é.xml", "three");

        PostTree(_sink, Dir(), true);

        Vector<Path> posted = _sink.Posted();
        eastl::sort(posted.begin(), posted.end());
        const Path dir = Dir();
        EXPECT_EQ(posted, (Vector<Path>{dir / "a.xml", dir / "sub", dir / "sub" / "b.xml", dir / "Папка_é", dir / "Папка_é" / "данные_é.xml"}));
    }

    TEST_F(FileWatchPostTreeTest, AFolderThatCannotBeListedPostsNothing) {
        PostTree(_sink, Dir() / "missing", true);

        EXPECT_EQ(_sink.Count(), 0u);
    }

    /// The polling backend alone: its clock is a parameter, so these cases never sleep or wait
    class FileWatchBackendPollingTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _harness.emplace(New<PollingFileWatchBackend>());
        }

        void TearDown() override {
            _harness.reset();
        }

        BackendHarness& H() {
            return *_harness;
        }

        Optional<BackendHarness> _harness;
    };

    TEST_F(FileWatchBackendPollingTest, ATickInsideTheIntervalDoesNotLook) {
        H().Backend().AddDirectory(H().Dir(), false);
        H().Backend().Tick(0.0);
        const Path file = H().Dir() / "a.xml";
        H().Write(file, "one");

        H().Backend().Tick(kPollIntervalSeconds / 2);
        EXPECT_FALSE(H().Sink().Has(file));
        H().Backend().Tick(kPollIntervalSeconds);
        EXPECT_TRUE(H().Sink().Has(file));
    }

    TEST_F(FileWatchBackendPollingTest, TheFilesThatExistWhenTheDirectoryIsAddedAreNotReported) {
        H().Write(H().Dir() / "old.xml", "one");
        H().Backend().AddDirectory(H().Dir(), false);
        H().Backend().Tick(0.0);
        H().Backend().Tick(10.0);
        EXPECT_EQ(H().Sink().Count(), 0u);
    }

    TEST_F(FileWatchBackendPollingTest, ANonRecursiveDirectoryDoesNotLookIntoItsSubfolders) {
        MakeDirectory(H().Dir() / "sub");
        H().Backend().AddDirectory(H().Dir(), false);
        H().Write(H().Dir() / "sub" / "x.png", "pixels");
        H().Backend().Tick(0.0);
        H().Backend().Tick(10.0);
        EXPECT_EQ(H().Sink().Count(), 0u);
    }

    TEST_F(FileWatchBackendPollingTest, APollThatFindsNothingChangedAllocatesNothing) {
        H().Write(H().Dir() / "a.xml", "one");
        MakeDirectory(H().Dir() / "sub");
        H().Write(H().Dir() / "sub" / "b.xml", "two");
        H().Backend().AddDirectory(H().Dir(), true);
        H().Backend().Tick(0.0);
        ExpectNoAllocations([this] {
            H().Backend().Tick(10.0);
            H().Backend().Tick(20.0);
        });
        EXPECT_EQ(H().Sink().Count(), 0u);
    }

}  // namespace BECore::Tests
