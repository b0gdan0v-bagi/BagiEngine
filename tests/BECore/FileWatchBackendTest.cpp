#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/FileSystem/PollingFileWatchBackend.h>
#include <BECore/RefCounted/New.h>
#include <Common/AllocCheck.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Common/Wait.h>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        /// Collects what a backend posts; a native backend posts from its own thread
        class RecordingSink : public IFileWatchSink {
        public:
            void Post(const std::filesystem::path& realPath) override {
                LockGuard lock(_mutex);
                _posted.push_back(realPath.lexically_normal());
            }

            [[nodiscard]] bool Has(const std::filesystem::path& path) const {
                LockGuard lock(_mutex);
                const std::filesystem::path wanted = path.lexically_normal();
                for (const std::filesystem::path& posted : _posted) {
                    if (posted == wanted) {
                        return true;
                    }
                }
                return false;
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
            Vector<std::filesystem::path> _posted;
        };

        /// A folder of the test, a backend over it and the clock that drives its Tick; the same cases run on every backend of the platform
        class BackendHarness {
        public:
            explicit BackendHarness(IntrusivePtrAtomic<IFileWatchBackend> backend) : _backend(std::move(backend)), _dir(std::filesystem::absolute(TestTempDirectory("fwb")).lexically_normal()) {
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

            [[nodiscard]] const std::filesystem::path& Dir() const {
                return _dir;
            }

            [[nodiscard]] IFileWatchBackend& Backend() {
                return *_backend;
            }

            [[nodiscard]] RecordingSink& Sink() {
                return _sink;
            }

            /// Writes the file and moves its write time forward by a step of its own, so a rewrite of the same size is a change on a file system with coarse time too
            void Write(const std::filesystem::path& path, StringView text) {
                WriteFile(path, text);
                std::error_code error;
                std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2 * ++_writes), error);
            }

            /// One turn of the clock that Tick polls with
            void Pump() {
                _clock += 2.0 * kPollIntervalSeconds;
                _backend->Tick(_clock);
            }

            /// Pumps until @p path is posted; false when it never is
            bool WaitForPost(const std::filesystem::path& path) {
                return WaitUntil([this, &path] {
                    Pump();
                    return _sink.Has(path);
                });
            }

            /// Whether @p path stays unposted after a short pause (a negative check can only wait, load only weakens it)
            bool StaysQuiet(const std::filesystem::path& path) {
                Pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                Pump();
                return !_sink.Has(path);
            }

        private:
            IntrusivePtrAtomic<IFileWatchBackend> _backend;
            std::filesystem::path _dir;
            RecordingSink _sink;
            double _clock = 0.0;
            int _writes = 0;
        };

        struct PollingMaker {
            static IntrusivePtrAtomic<IFileWatchBackend> Make() {
                return New<PollingFileWatchBackend>();
            }
        };

    }  // namespace

    /// The contract of IFileWatchBackend, which FileWatcher relies on. WP 4 and 5 add their backend to Backends
    template <typename TMaker>
    class FileWatchBackendContractTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _harness.emplace(TMaker::Make());
        }

        void TearDown() override {
            _harness.reset();
        }

        BackendHarness& H() {
            return *_harness;
        }

        Optional<BackendHarness> _harness;
    };

    using Backends = ::testing::Types<PollingMaker>;
    TYPED_TEST_SUITE(FileWatchBackendContractTest, Backends);

    TYPED_TEST(FileWatchBackendContractTest, AFileWrittenAfterTheWatchIsPosted) {
        this->H().Backend().AddDirectory(this->H().Dir(), false);
        const std::filesystem::path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        EXPECT_TRUE(this->H().WaitForPost(file));
    }

    TYPED_TEST(FileWatchBackendContractTest, ARewriteIsPostedAgain) {
        const std::filesystem::path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        this->H().Backend().AddDirectory(this->H().Dir(), false);
        this->H().Write(file, "two");
        ASSERT_TRUE(this->H().WaitForPost(file));

        this->H().Sink().Clear();
        this->H().Write(file, "three!");
        EXPECT_TRUE(this->H().WaitForPost(file));
    }

    TYPED_TEST(FileWatchBackendContractTest, ARenameOverTheFileIsPostedForTheTarget) {
        const std::filesystem::path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        this->H().Backend().AddDirectory(this->H().Dir(), false);

        const std::filesystem::path temp = this->H().Dir() / "a.xml.tmp";
        this->H().Write(temp, "replacement");
        std::error_code error;
        std::filesystem::rename(temp, file, error);
        ASSERT_FALSE(error) << error.message();
        EXPECT_TRUE(this->H().WaitForPost(file));
    }

    TYPED_TEST(FileWatchBackendContractTest, AFileInASubfolderOfARecursiveDirectoryIsPosted) {
        MakeDirectory(this->H().Dir() / "sub");
        this->H().Backend().AddDirectory(this->H().Dir(), true);
        const std::filesystem::path file = this->H().Dir() / "sub" / "x.png";
        this->H().Write(file, "pixels");
        EXPECT_TRUE(this->H().WaitForPost(file));
    }

    TYPED_TEST(FileWatchBackendContractTest, ARemovedFileIsPosted) {
        const std::filesystem::path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        this->H().Backend().AddDirectory(this->H().Dir(), false);

        std::error_code error;
        std::filesystem::remove(file, error);
        ASSERT_FALSE(error) << error.message();
        EXPECT_TRUE(this->H().WaitForPost(file));
    }

    TYPED_TEST(FileWatchBackendContractTest, ARemovedDirectoryPostsNothingMore) {
        this->H().Backend().AddDirectory(this->H().Dir(), false);
        this->H().Backend().RemoveDirectory(this->H().Dir());
        const std::filesystem::path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        EXPECT_TRUE(this->H().StaysQuiet(file));
    }

    TYPED_TEST(FileWatchBackendContractTest, AShutdownBackendPostsNothingMore) {
        this->H().Backend().AddDirectory(this->H().Dir(), false);
        this->H().Backend().Shutdown();
        const std::filesystem::path file = this->H().Dir() / "a.xml";
        this->H().Write(file, "one");
        EXPECT_TRUE(this->H().StaysQuiet(file));
    }

    TYPED_TEST(FileWatchBackendContractTest, ADirectoryRemovedUnderTheWatchIsSurvivedAndWatchedAgainWhenItComesBack) {
        const std::filesystem::path file = this->H().Dir() / "a.xml";
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
        EXPECT_TRUE(this->H().WaitForPost(file));
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
        const std::filesystem::path file = H().Dir() / "a.xml";
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
