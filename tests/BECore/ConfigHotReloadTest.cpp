#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FakeFileWatchBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/FileSystem/PollingFileWatchBackend.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/New.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/ConfigEvents.h>
#include <Events/SubscriptionHolder.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        constexpr double kSettle = kFileWatchSettleSeconds;
        const PoolString kHot = "HotConfig"_intern;
        constexpr StringView kHotText = "<HotConfig value=\"one\"/>\n";
        constexpr StringView kOtherText = "<OtherConfig value=\"other\"/>\n";

        /// A time that is surely past the settle window of a post made at @p postedAt
        constexpr double After(double postedAt) {
            return postedAt + 2.0 * kSettle;
        }

        class ChangeRecorder : public SubscriptionHolder {
        public:
            ChangeRecorder() {
                Subscribe<ConfigEvents::ConfigChangedEvent, &ChangeRecorder::OnChanged>(this);
            }

            Vector<PoolString> names;
            std::thread::id lastThread;

        private:
            void OnChanged(const ConfigEvents::ConfigChangedEvent& event) {
                names.push_back(event.name);
                lastThread = std::this_thread::get_id();
            }
        };

        class FailureRecorder : public SubscriptionHolder {
        public:
            FailureRecorder() {
                Subscribe<ConfigEvents::ConfigReloadFailedEvent, &FailureRecorder::OnFailed>(this);
            }

            Vector<PoolString> names;
            Vector<String> reasons;
            std::thread::id lastThread;

        private:
            void OnFailed(const ConfigEvents::ConfigReloadFailedEvent& event) {
                names.push_back(event.name);
                reasons.push_back(event.reason);
                lastThread = std::this_thread::get_id();
            }
        };

        /// A reason is one line for a banner, never a dump of the file
        constexpr size_t kMaxReasonLength = 200;

    }  // namespace

    /// A manager and a watcher of their own over a `config` folder in a temporary directory: the engine's own configs are never written. The watcher runs
    /// on a fake backend (a post is an `Emit`, the clock is a parameter) unless a case says otherwise
    class ConfigHotReloadTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("cfg_hot");
            MakeEmptyDirectory(_dir);
            WriteFile(_dir / "HotConfig.xml", kHotText);
            WriteFile(_dir / "OtherConfig.xml", kOtherText);
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
        }

        void TearDown() override {
            _manager.StopWatching();
            _watcher.Stop();
            RemoveDirectory(_dir);
        }

        void StartOnFake() {
            _fake = New<FakeFileWatchBackend>();
            _watcher.Start(IntrusivePtr<IFileWatchBackend>(_fake));
            _manager.StartWatching(_watcher);
        }

        /// Posts @p path and lets the settle window pass
        void PostAndSettle(const Path& path) {
            _fake->Emit(path);
            _watcher.Update(_now);
            _now = After(_now);
            _watcher.Update(_now);
        }

        String HotValue() const {
            const auto value = _manager.GetConfig(kHot).ParseAttribute<StringView>("value");
            return value ? String(value->data(), value->size()) : String();
        }

        Path HotPath() const {
            return _dir / "HotConfig.xml";
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        FileWatcher _watcher{_fileSystem};
        IntrusivePtr<FakeFileWatchBackend> _fake;
        double _now = 0.0;
    };

    TEST_F(ConfigHotReloadTest, AnExternalWriteReloadsTheConfigAndEmitsOnce) {
        StartOnFake();
        ChangeRecorder recorder;
        WriteFile(HotPath(), "<HotConfig value=\"two\"/>\n");

        PostAndSettle(HotPath());

        ASSERT_EQ(recorder.names.size(), 1u);
        EXPECT_EQ(recorder.names[0], kHot);
        EXPECT_EQ(HotValue(), "two");
    }

    TEST_F(ConfigHotReloadTest, ARenameOverSaveReloadsTheConfigOnce) {
        StartOnFake();
        ChangeRecorder recorder;
        const Path temp = _dir / "HotConfig.xml.tmp";

        WriteFile(temp, "<HotConfig value=\"three\"/>\n");
        _fake->Emit(temp);
        _watcher.Update(_now);
        const auto renamed = RenameFile(temp, HotPath());
        ASSERT_TRUE(renamed) << renamed.error().c_str();
        PostAndSettle(HotPath());

        ASSERT_EQ(recorder.names.size(), 1u);
        EXPECT_EQ(HotValue(), "three");
    }

    TEST_F(ConfigHotReloadTest, TheManagersOwnSaveEmitsOnceAndItsEchoEmitsNothing) {
        StartOnFake();
        ChangeRecorder recorder;
        _manager.GetConfig(kHot).GetPugiNode().append_attribute("extra") = "saved";

        ASSERT_TRUE(_manager.SaveConfig(kHot));
        EXPECT_EQ(recorder.names.size(), 1u) << "the save";

        PostAndSettle(HotPath());
        EXPECT_EQ(recorder.names.size(), 1u) << "the echo of the save";
        EXPECT_EQ(String(_manager.GetConfig(kHot).GetPugiNode().attribute("extra").as_string()), "saved");
    }

    TEST_F(ConfigHotReloadTest, ARewriteWithTheSameTextEmitsNothing) {
        StartOnFake();
        ChangeRecorder recorder;
        WriteFile(HotPath(), kHotText);

        PostAndSettle(HotPath());

        EXPECT_TRUE(recorder.names.empty());
    }

    TEST_F(ConfigHotReloadTest, AFileThatDoesNotParseKeepsTheOldDocumentAndEmitsNothing) {
        StartOnFake();
        ChangeRecorder recorder;
        const LogRecorder log;
        WriteFile(HotPath(), "<HotConfig value=\"broken\"");

        PostAndSettle(HotPath());
        LogEvent::Flush();

        EXPECT_TRUE(recorder.names.empty());
        EXPECT_EQ(HotValue(), "one");
        EXPECT_EQ(log.Count(LogLevel::Error, {"HotConfig"}), 1u);
    }

    TEST_F(ConfigHotReloadTest, AFileThatDoesNotParseEmitsTheFailureOnceAndALaterGoodFileTheChange) {
        StartOnFake();
        ChangeRecorder changes;
        FailureRecorder failures;
        WriteFile(HotPath(), "<HotConfig value=\"broken\"");

        PostAndSettle(HotPath());

        ASSERT_EQ(failures.names.size(), 1u);
        EXPECT_EQ(failures.names[0], kHot);
        EXPECT_FALSE(failures.reasons[0].empty());
        EXPECT_LT(failures.reasons[0].size(), kMaxReasonLength);
        EXPECT_EQ(failures.lastThread, std::this_thread::get_id());
        EXPECT_TRUE(changes.names.empty());
        EXPECT_EQ(HotValue(), "one") << "the old document is kept";

        WriteFile(HotPath(), "<HotConfig value=\"two\"/>\n");
        PostAndSettle(HotPath());

        EXPECT_EQ(failures.names.size(), 1u);
        ASSERT_EQ(changes.names.size(), 1u);
        EXPECT_EQ(HotValue(), "two");
    }

    TEST_F(ConfigHotReloadTest, AFileThatIsGoneEmitsTheFailure) {
        StartOnFake();
        FailureRecorder failures;
        const auto removed = RemoveFile(HotPath());
        ASSERT_TRUE(removed) << removed.error().c_str();

        PostAndSettle(HotPath());

        ASSERT_EQ(failures.names.size(), 1u);
        EXPECT_EQ(failures.names[0], kHot);
        EXPECT_EQ(HotValue(), "one");
    }

    TEST_F(ConfigHotReloadTest, ReloadConfigByHandEmitsNoFailure) {
        FailureRecorder failures;
        WriteFile(HotPath(), "<HotConfig value=\"broken\"");

        EXPECT_FALSE(_manager.ReloadConfig(kHot));

        EXPECT_TRUE(failures.names.empty());
    }

    TEST_F(ConfigHotReloadTest, ANewFileARemovedFileAndAFileNoConfigOwnsEmitNothing) {
        StartOnFake();
        ChangeRecorder recorder;
        WriteFile(_dir / "NewConfig.xml", "<NewConfig/>\n");
        WriteFile(_dir / "notes.txt", "text");
        const auto removed = RemoveFile(_dir / "OtherConfig.xml");
        ASSERT_TRUE(removed) << removed.error().c_str();

        _fake->Emit(_dir / "NewConfig.xml");
        _fake->Emit(_dir / "notes.txt");
        PostAndSettle(_dir / "OtherConfig.xml");

        EXPECT_TRUE(recorder.names.empty());
        EXPECT_EQ(HotValue(), "one");
        EXPECT_TRUE(_manager.HasConfig("OtherConfig"_intern)) << "the loaded document stays";
    }

    TEST_F(ConfigHotReloadTest, TheEventArrivesOnTheThreadThatCalledUpdate) {
        StartOnFake();
        ChangeRecorder recorder;
        WriteFile(HotPath(), "<HotConfig value=\"two\"/>\n");

        std::thread poster([this] { _fake->Emit(HotPath()); });
        poster.join();
        _watcher.Update(_now);
        _watcher.Update(After(_now));

        ASSERT_EQ(recorder.names.size(), 1u);
        EXPECT_EQ(recorder.lastThread, std::this_thread::get_id());
    }

    TEST_F(ConfigHotReloadTest, ANodeFetchedAfterAReloadIsTheCurrentDocument) {
        StartOnFake();
        WriteFile(HotPath(), "<HotConfig value=\"two\"/>\n");
        PostAndSettle(HotPath());

        const XmlNode fresh = _manager.GetConfig(kHot);
        ASSERT_FALSE(fresh.IsEmpty());
        EXPECT_EQ(String(fresh.GetPugiNode().attribute("value").as_string()), "two");
    }

    TEST_F(ConfigHotReloadTest, ReloadConfigAndNotifyConfigChangedEmit) {
        ChangeRecorder recorder;

        ASSERT_TRUE(_manager.ReloadConfig(kHot));
        EXPECT_EQ(recorder.names.size(), 1u);
        _manager.NotifyConfigChanged(kHot);
        EXPECT_EQ(recorder.names.size(), 2u);
        EXPECT_FALSE(_manager.ReloadConfig("NoSuchConfig"_intern));
        EXPECT_EQ(recorder.names.size(), 2u);
    }

    TEST_F(ConfigHotReloadTest, TheStatPollingBackendReloadsARealWrite) {
        _watcher.Start(IntrusivePtr<IFileWatchBackend>(New<PollingFileWatchBackend>()));
        _manager.StartWatching(_watcher);
        ChangeRecorder recorder;
        _watcher.Update(_now);

        WriteFile(HotPath(), "<HotConfig value=\"a longer value than before\"/>\n");  // another size: a rewrite inside one timestamp tick still shows
        _now += kPollIntervalSeconds;
        _watcher.Update(_now);
        _now = After(_now);
        _watcher.Update(_now);

        ASSERT_EQ(recorder.names.size(), 1u);
        EXPECT_EQ(HotValue(), "a longer value than before");
    }

}  // namespace BECore::Tests
