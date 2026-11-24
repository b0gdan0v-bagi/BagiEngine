#include <BECore/Config/ConfigManager.h>
#include <BECore/Control/ControlService.h>
#include <BECore/FileSystem/FakeFileWatchBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatchControl.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Memory/ScratchArena.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <chrono>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        const PoolString kHot = "HotConfig"_intern;
        constexpr StringView kHotPath = "config/HotConfig.xml";
        constexpr StringView kBrokenText = "<HotConfig value=\"two\"";

    }  // namespace

    /// The two file methods over a ControlService, a ConfigManager and a FileWatcher of their own on a config folder in a temporary directory: the engine's
    /// configs are never written, and no transport or frame loop is needed
    class FileWatchControlTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("fwc");
            MakeEmptyDirectory(_dir);
            WriteFile(HotPath(), "<HotConfig value=\"one\"/>\n");
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            RegisterFileWatchControl(_service, _watcher);
        }

        void TearDown() override {
            _manager.StopWatching();
            _watcher.Stop();
            RemoveDirectory(_dir);
        }

        void StartWatching() {
            _watcher.Start(IntrusivePtr<IFileWatchBackend>(New<FakeFileWatchBackend>()));
            _manager.StartWatching(_watcher);
        }

        /// A rewrite that is a change on a file system with coarse time too
        void EditHot(StringView text) {
            WriteFile(HotPath(), text);
            SetWriteTime(HotPath(), std::chrono::seconds(2 * ++_writes));
        }

        /// Posts the request (with @p paramsText as written, so an escaped path stays escaped) and runs it; a deferred answer arrives with the end of
        /// a frame, so @p frames of them are run. Returns the whole response, an empty one when none came
        ControlReply Call(StringView method, StringView paramsText = {}, int frames = 0) {
            ControlReply response;
            _service.Post(MakeRequestText(1, method, paramsText), [&response](String text) { response = ControlReply(StringView(text.c_str(), text.size())); });
            _service.Drain();
            for (int i = 0; i < frames; ++i) {
                _service.EndFrame();
            }
            return response;
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
        ControlService _service;
        int _writes = 0;
    };

    TEST_F(FileWatchControlTest, BothMethodsAnswerNotWatchingWhileTheWatcherIsNotRunning) {
        EXPECT_EQ(Call("files_rescan").ErrorCode(), "not_watching");
        EXPECT_EQ(Call("files_apply").ErrorCode(), "not_watching");
    }

    TEST_F(FileWatchControlTest, ApplyTakesOnlyAListOfStringsAsPaths) {
        StartWatching();
        for (const char* text : {R"({"paths": "config/HotConfig.xml"})", R"({"paths": [1]})", R"({"paths": {}})"}) {
            EXPECT_EQ(Call("files_apply", text).ErrorCode(), "invalid_params") << text;
        }
    }

    TEST_F(FileWatchControlTest, AnEscapedPathIsDecodedBeforeItIsApplied) {
        StartWatching();
        EditHot("<HotConfig value=\"two\"/>\n");
        ASSERT_TRUE(Call("files_rescan").IsOk());

        const ControlReply applied = Call("files_apply", R"({"paths":["config\/HotConfig.xml"]})", 1);

        ASSERT_TRUE(applied.IsOk());
        EXPECT_EQ(HotValue(), "two");
    }

    TEST_F(FileWatchControlTest, ARescanOfAnUntouchedFolderListsNothing) {
        StartWatching();
        const ControlReply response = Call("files_rescan");
        ASSERT_TRUE(response.IsOk());
        ASSERT_TRUE(response.Result().IsObject());
        EXPECT_EQ(response.Result().Raw(), R"({"changes":[]})");
    }

    TEST_F(FileWatchControlTest, AClientEditsAConfigOnDiskLooksAtItAppliesItAndSeesTheNewValue) {
        StartWatching();
        EditHot("<HotConfig value=\"two\"/>\n");

        const ControlReply looked = Call("files_rescan");
        ASSERT_TRUE(looked.IsOk());
        const JsonView changes = MemberOf(looked.Result(), "changes");
        ASSERT_TRUE(changes.IsArray());
        ASSERT_EQ(changes.Size(), 1u);
        EXPECT_EQ(StringOf(ItemAt(changes, 0), "path"), kHotPath);
        EXPECT_EQ(StringOf(ItemAt(changes, 0), "kind"), "changed");
        const JsonView consumers = MemberOf(ItemAt(changes, 0), "consumers");
        ASSERT_TRUE(consumers.IsArray());
        ASSERT_EQ(consumers.Size(), 1u);
        EXPECT_EQ(HotValue(), "one") << "looking delivers nothing";

        String params;
        {
            JsonWriter writer(params);
            writer.BeginObject();
            writer.Key("paths");
            writer.BeginArray();
            writer.Value(StringView(kHotPath));
            writer.EndArray();
            writer.EndObject();
        }
        const ControlReply applied = Call("files_apply", StringView(params.c_str(), params.size()), 1);
        ASSERT_TRUE(applied.IsOk());
        const JsonView items = MemberOf(applied.Result(), "applied");
        ASSERT_TRUE(items.IsArray());
        ASSERT_EQ(items.Size(), 1u);
        EXPECT_EQ(BoolOf(ItemAt(items, 0), "ok"), Optional<bool>(true));
        EXPECT_EQ(HotValue(), "two");

        const ControlReply again = Call("files_rescan");
        ASSERT_TRUE(MemberOf(again.Result(), "changes").IsArray());
        EXPECT_EQ(MemberOf(again.Result(), "changes").Size(), 0u);
    }

    TEST_F(FileWatchControlTest, ApplyAnswersOnlyAfterAFrameHasEnded) {
        StartWatching();
        EditHot("<HotConfig value=\"two\"/>\n");

        const ControlReply early = Call("files_apply", {}, 0);

        EXPECT_TRUE(early.Text().empty()) << "the delivery ran, the answer waits for the frame";
        EXPECT_EQ(HotValue(), "two");
        _service.EndFrame();
    }

    TEST_F(FileWatchControlTest, AConfigThatDoesNotParseIsReportedAndTheOldOneKept) {
        StartWatching();
        EditHot(kBrokenText);

        const ControlReply applied = Call("files_apply", {}, 1);

        ASSERT_TRUE(applied.IsOk());
        const JsonView items = MemberOf(applied.Result(), "applied");
        ASSERT_TRUE(items.IsArray());
        ASSERT_EQ(items.Size(), 1u);
        EXPECT_EQ(BoolOf(ItemAt(items, 0), "ok"), Optional<bool>(false));
        EXPECT_FALSE(StringOf(ItemAt(items, 0), "error").empty());
        EXPECT_EQ(HotValue(), "one");
    }

    TEST_F(FileWatchControlTest, ADeferredApplyWritesTheErrorAndTheConsumersAfterTheHandlerScopeEnded) {
        StartWatching();
        EditHot(kBrokenText);
        const size_t usedBefore = ScratchArena::Get().GetBytesUsed();

        const ControlReply applied = Call("files_apply", {}, 1);

        // The handler's scope ended before the frame: an answer built there and captured would read freed arena memory (the Debug free pattern) instead of these
        ASSERT_TRUE(applied.IsOk());
        const JsonView items = MemberOf(applied.Result(), "applied");
        ASSERT_TRUE(items.IsArray());
        ASSERT_EQ(items.Size(), 1u);
        const JsonView item = ItemAt(items, 0);
        EXPECT_EQ(StringOf(item, "path"), kHotPath);
        EXPECT_EQ(BoolOf(item, "ok"), Optional<bool>(false));
        EXPECT_FALSE(StringOf(item, "error").empty());
        const JsonView consumers = MemberOf(item, "consumers");
        ASSERT_TRUE(consumers.IsArray());
        ASSERT_EQ(consumers.Size(), 1u);
        EXPECT_TRUE(ItemAt(consumers, 0).IsString());
        EXPECT_NE(ItemAt(consumers, 0).Raw(), R"("")");
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), usedBefore) << "the answer's memory went back with its scope";
    }

}  // namespace BECore::Tests
