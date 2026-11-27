#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/PollingFileWatchBackend.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/New.h>
#include <Common/ConfigReloadSupport.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/TestFiles.h>
#include <Events/ConfigEvents.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        const PoolString kHot = "HotConfig"_intern;
        constexpr StringView kHotText = "<HotConfig value=\"one\"/>\n";
        constexpr StringView kOtherText = "<OtherConfig value=\"other\"/>\n";

        /// A reason is one line for a banner, never a dump of the file
        constexpr size_t kMaxReasonLength = 200;

    }  // namespace

    /// The folder holds two configs: the one the cases change and the one they leave alone
    class ConfigHotReloadTest : public ConfigReloadFixture {
    protected:
        const char* TempPrefix() const override {
            return "cfg_hot";
        }

        void WriteConfigFiles() override {
            WriteFile(_dir / "HotConfig.xml", kHotText);
            WriteFile(_dir / "OtherConfig.xml", kOtherText);
        }

        String HotValue() const {
            const auto value = _manager.GetConfig(kHot).ParseAttribute<StringView>("value");
            return value ? String(value->data(), value->size()) : String();
        }

        Path HotPath() const {
            return _dir / "HotConfig.xml";
        }
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

    TEST_F(ConfigHotReloadTest, TheEventSaysWhetherTheDocumentWasSavedReloadedOrEdited) {
        using Cause = ConfigEvents::ConfigChangeCause;
        StartOnFake();
        ChangeRecorder recorder;

        ASSERT_TRUE(_manager.SaveConfig(kHot));
        ASSERT_TRUE(_manager.ReloadConfig(kHot));
        _manager.NotifyConfigChanged(kHot);
        WriteFile(HotPath(), "<HotConfig value=\"two\"/>\n");
        PostAndSettle(HotPath());
        ConfigEvents::ConfigChangedEvent::Emit(kHot);

        const Vector<Cause> expected = {Cause::Saved, Cause::Reloaded, Cause::Edited, Cause::Reloaded, Cause::Edited};
        EXPECT_TRUE(recorder.causes == expected);
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
