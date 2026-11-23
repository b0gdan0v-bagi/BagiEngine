#include <BECore/Config/ConfigManager.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/FileSystem/FakeFileWatchBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatchControl.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/RefCounted/New.h>
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
            std::error_code error;
            std::filesystem::last_write_time(HotPath(), std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2 * ++_writes), error);
        }

        /// Posts the request and runs it; a deferred answer arrives with the end of a frame, so @p frames of them are run. Returns the whole response
        Json Call(StringView method, const Json& params = Json(), int frames = 0) {
            Json response;
            _service.Post(MakeRequest(1, method, params), [&response](String text) {
                auto parsed = ParseJson(text);
                response = parsed ? std::move(*parsed) : Json();
            });
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

        std::filesystem::path HotPath() const {
            return _dir / "HotConfig.xml";
        }

        std::filesystem::path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        FileWatcher _watcher{_fileSystem};
        ControlService _service;
        int _writes = 0;
    };

    TEST_F(FileWatchControlTest, BothMethodsAnswerNotWatchingWhileTheWatcherIsNotRunning) {
        EXPECT_EQ(ErrorCode(Call("files_rescan")), "not_watching");
        EXPECT_EQ(ErrorCode(Call("files_apply")), "not_watching");
    }

    TEST_F(FileWatchControlTest, ApplyTakesOnlyAListOfStringsAsPaths) {
        StartWatching();
        for (const char* text : {R"({"paths": "config/HotConfig.xml"})", R"({"paths": [1]})", R"({"paths": {}})"}) {
            const auto params = ParseJson(text);
            ASSERT_TRUE(params.has_value());
            EXPECT_EQ(ErrorCode(Call("files_apply", *params)), "invalid_params") << text;
        }
    }

    TEST_F(FileWatchControlTest, ARescanOfAnUntouchedFolderListsNothing) {
        StartWatching();
        const Json response = Call("files_rescan");
        ASSERT_TRUE(IsOk(response));
        const Json* changes = FindJsonMember(*FindJsonMember(response, "result"), "changes");
        ASSERT_NE(changes, nullptr);
        EXPECT_TRUE(changes->is_array());
        EXPECT_TRUE(changes->empty());
    }

    TEST_F(FileWatchControlTest, AClientEditsAConfigOnDiskLooksAtItAppliesItAndSeesTheNewValue) {
        StartWatching();
        EditHot("<HotConfig value=\"two\"/>\n");

        const Json looked = Call("files_rescan");
        ASSERT_TRUE(IsOk(looked));
        const Json* changes = FindJsonMember(*FindJsonMember(looked, "result"), "changes");
        ASSERT_NE(changes, nullptr);
        ASSERT_EQ(changes->size(), 1u);
        EXPECT_EQ(StringOf(&(*changes)[0], "path"), kHotPath);
        EXPECT_EQ(StringOf(&(*changes)[0], "kind"), "changed");
        const Json* consumers = FindJsonMember((*changes)[0], "consumers");
        ASSERT_NE(consumers, nullptr);
        ASSERT_EQ(consumers->size(), 1u);
        EXPECT_EQ(HotValue(), "one") << "looking delivers nothing";

        Json params = Json::object();
        params["paths"] = Json::array({ToStd(kHotPath)});
        const Json applied = Call("files_apply", params, 1);
        ASSERT_TRUE(IsOk(applied));
        const Json* items = FindJsonMember(*FindJsonMember(applied, "result"), "applied");
        ASSERT_NE(items, nullptr);
        ASSERT_EQ(items->size(), 1u);
        EXPECT_EQ(BoolOf(&(*items)[0], "ok"), Optional<bool>(true));
        EXPECT_EQ(HotValue(), "two");

        const Json again = Call("files_rescan");
        EXPECT_TRUE(FindJsonMember(*FindJsonMember(again, "result"), "changes")->empty());
    }

    TEST_F(FileWatchControlTest, ApplyAnswersOnlyAfterAFrameHasEnded) {
        StartWatching();
        EditHot("<HotConfig value=\"two\"/>\n");

        const Json early = Call("files_apply", Json(), 0);

        EXPECT_TRUE(early.is_null()) << "the delivery ran, the answer waits for the frame";
        EXPECT_EQ(HotValue(), "two");
        _service.EndFrame();
    }

    TEST_F(FileWatchControlTest, AConfigThatDoesNotParseIsReportedAndTheOldOneKept) {
        StartWatching();
        EditHot(kBrokenText);

        const Json applied = Call("files_apply", Json(), 1);

        ASSERT_TRUE(IsOk(applied));
        const Json* items = FindJsonMember(*FindJsonMember(applied, "result"), "applied");
        ASSERT_NE(items, nullptr);
        ASSERT_EQ(items->size(), 1u);
        EXPECT_EQ(BoolOf(&(*items)[0], "ok"), Optional<bool>(false));
        EXPECT_FALSE(StringOf(&(*items)[0], "error").empty());
        EXPECT_EQ(HotValue(), "one");
    }

}  // namespace BECore::Tests
