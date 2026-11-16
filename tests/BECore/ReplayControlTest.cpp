#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/ReplayTestGame.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Games/GameManager.h>
#include <Games/ReplayControl.h>
#include <Games/ReplayFile.h>
#include <Games/SessionRecorder.h>
#include <filesystem>
#include <fstream>  //-NoStd: the test reads the file the recorder wrote
#include <gtest/gtest.h>
#include <sstream>  //-NoStd: the test reads the file the recorder wrote

namespace BECore::Tests {

    namespace {

        class ReplayControlTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _path = TestTempPath("replay_control_" + String(::testing::UnitTest::GetInstance()->current_test_info()->name()));
                const Span<const String> arguments = CoreManager::GetCommandLine().GetArguments();
                _arguments.assign(arguments.begin(), arguments.end());
                CoreManager::GetTime().Reset();
                ReplayTestGame::RestartSeedCounter();
                GameManager::GetInstance().Initialize();
                GameManager::GetInstance().SwitchTo("ReplayTest"_intern);
            }

            void TearDown() override {
                SessionRecorder::DestroyInstance();
                GameManager::GetInstance().Shutdown();
                CoreManager::GetCommandLine().Set(_arguments);
                CoreManager::GetTime().Reset();
                std::error_code ignored;
                std::filesystem::remove(_path, ignored);
            }

            static Json Params(StringView text) {
                auto parsed = ParseJson(text);
                return parsed ? std::move(*parsed) : Json();
            }

            [[nodiscard]] String PathText() const {
                return FromStdString(_path.string());
            }

            [[nodiscard]] Json PathParams() const {
                Json params = Json::object();
                params["path"] = MakeJsonString(PathText());
                return params;
            }

            [[nodiscard]] String ReadRecording() const {
                std::ifstream file(_path, std::ios::binary);  //-NoStd: see the include
                std::ostringstream text;                      //-NoStd: see the include
                text << file.rdbuf();
                return FromStdString(text.str());
            }

            [[nodiscard]] ParsedReplay ParseRecording() const {
                const String text = ReadRecording();
                auto parsed = ParseReplay(text);
                EXPECT_TRUE(parsed.has_value()) << (parsed ? "" : DescribeReplayError(parsed.error()).c_str());
                return parsed ? std::move(*parsed) : ParsedReplay{};
            }

            /// A control request the way a client sends it, answered on the spot
            static Json Call(StringView method, const Json& params) {
                Json request = Json::object();
                request["id"] = 1;
                request["method"] = ToStdString(method);
                request["params"] = params;
                Json response;
                CoreManager::GetControlService().Post(DumpJson(request), [&response](String text) {
                    auto parsed = ParseJson(text);
                    if (parsed) {
                        response = std::move(*parsed);
                    }
                });
                CoreManager::GetControlService().Drain();
                return response;
            }

            static bool Succeeded(const Json& response) {
                return GetJsonBool(response, "ok").value_or(false);
            }

            /// The `code` of a failed response, empty for a successful one
            static String ErrorCode(const Json& response) {
                const Json* error = FindJsonMember(response, "error");
                const auto code = error != nullptr ? GetJsonString(*error, "code") : Optional<String>();
                return code.value_or(String());
            }

            static void SetCommandLine(std::initializer_list<const char*> arguments) {
                Vector<String> list;
                for (const char* argument : arguments) {
                    list.push_back(String(argument));
                }
                CoreManager::GetCommandLine().Set(list);
            }

            std::filesystem::path _path;
            Vector<String> _arguments;
        };

        const Json* FindResult(const Json& response) {
            return FindJsonMember(response, "result");
        }

    }  // namespace

    TEST_F(ReplayControlTest, TheAcceptedCallsAreInTheFileAndTheRefusedOnesAreNot) {
        const Json started = Call("record_start", PathParams());
        ASSERT_TRUE(Succeeded(started));
        const Json* startedResult = FindResult(started);
        ASSERT_NE(startedResult, nullptr);
        EXPECT_EQ(GetJsonString(*startedResult, "game").value_or(String()), "ReplayTest");
        EXPECT_EQ(GetJsonString(*startedResult, "path").value_or(String()), PathText());

        ASSERT_TRUE(Succeeded(Call("reset", Params(R"({"seed":9})"))));
        ASSERT_TRUE(Succeeded(Call("perform_action", Params(R"({"type":"roll"})"))));
        ASSERT_TRUE(Succeeded(Call("perform_action", Params(R"({"type":"arm"})"))));
        EXPECT_EQ(ErrorCode(Call("perform_action", Params(R"({"type":"roll"})"))), "illegal_action");
        ASSERT_TRUE(Succeeded(Call("set_state", Params(R"({"rolls":[1,2]})"))));
        EXPECT_FALSE(Succeeded(Call("reset", Params(R"({"seed":"x"})"))));

        const Json stopped = Call("record_stop", Json::object());
        ASSERT_TRUE(Succeeded(stopped));
        ASSERT_NE(FindResult(stopped), nullptr);
        EXPECT_EQ(GetJsonInt(*FindResult(stopped), "entries").value_or(-1), 4);

        const ParsedReplay replay = ParseRecording();
        ASSERT_EQ(replay.entries.size(), 4u);
        EXPECT_EQ(replay.entries[0].op, ReplayOp::Reset);
        EXPECT_EQ(replay.entries[0].params, Params(R"({"seed":9})"));
        EXPECT_EQ(replay.entries[1].op, ReplayOp::Action);
        EXPECT_EQ(replay.entries[2].params, Params(R"({"type":"arm"})"));
        EXPECT_EQ(replay.entries[3].op, ReplayOp::State);
    }

    TEST_F(ReplayControlTest, ATitleLandsInTheHeader) {
        Json params = PathParams();
        params["title"] = "a short session";

        ASSERT_TRUE(Succeeded(Call("record_start", params)));
        ASSERT_TRUE(Succeeded(Call("record_stop", Json::object())));

        EXPECT_EQ(ParseRecording().header.title, "a short session");
    }

    TEST_F(ReplayControlTest, RecordStartRefusesWhatItCannotDo) {
        EXPECT_EQ(ErrorCode(Call("record_start", Json::object())), "invalid_params");
        EXPECT_EQ(ErrorCode(Call("record_start", Params(R"({"path":""})"))), "invalid_params");
        EXPECT_EQ(ErrorCode(Call("record_start", Params(R"({"path":5})"))), "invalid_params");
        Json badTitle = PathParams();
        badTitle["title"] = 5;
        EXPECT_EQ(ErrorCode(Call("record_start", badTitle)), "invalid_params");
        const String missing = FromStdString((_path / "no" / "such" / "folder.jsonl").string());
        Json unwritable = Json::object();
        unwritable["path"] = MakeJsonString(missing);
        EXPECT_EQ(ErrorCode(Call("record_start", unwritable)), "invalid_params");
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());

        ASSERT_TRUE(Succeeded(Call("record_start", PathParams())));
        EXPECT_EQ(ErrorCode(Call("record_start", PathParams())), "invalid_params");
        EXPECT_TRUE(SessionRecorder::GetInstance().IsRecording());
    }

    TEST_F(ReplayControlTest, AGameThatIsNotReplayableAnswersUnsupportedAndOneThatIsNotControllableNot) {
        GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
        ASSERT_EQ(GameManager::GetInstance().GetActive()->GetName(), "FakeControllable"_intern);
        EXPECT_EQ(ErrorCode(Call("record_start", PathParams())), "unsupported");
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);
        ASSERT_EQ(GameManager::GetInstance().GetActive()->GetName(), "FakePlain"_intern);
        EXPECT_EQ(ErrorCode(Call("record_start", PathParams())), "not_controllable");
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_FALSE(std::filesystem::exists(_path));
    }

    TEST_F(ReplayControlTest, RecordStopWithoutARecordingIsInvalidParams) {
        EXPECT_EQ(ErrorCode(Call("record_stop", Json::object())), "invalid_params");
    }

    TEST_F(ReplayControlTest, TheMethodsAreDescribed) {
        bool start = false;
        bool stop = false;
        for (const auto& listing : CoreManager::GetControlService().GetMethods()) {
            start = start || listing.name == "record_start";
            stop = stop || listing.name == "record_stop";
        }
        EXPECT_TRUE(start);
        EXPECT_TRUE(stop);
    }

    TEST_F(ReplayControlTest, TheRecordFlagStartsTheRecordingOnTheActiveGame) {
        SetCommandLine({"--record", "--other"});
        StartRecordingFromCommandLine();  // a flag without a file
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        SetCommandLine({"--record="});
        StartRecordingFromCommandLine();
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        SetCommandLine({"--control"});
        StartRecordingFromCommandLine();
        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());

        const String flag = "--record=" + PathText();
        CoreManager::GetCommandLine().Set(Vector<String>{flag});
        StartRecordingFromCommandLine();
        ASSERT_TRUE(SessionRecorder::GetInstance().IsRecording());
        ASSERT_TRUE(Succeeded(Call("reset", Params(R"({"seed":2})"))));
        EXPECT_EQ(SessionRecorder::GetInstance().Stop(), 1u);

        EXPECT_EQ(ParseRecording().header.game, "ReplayTest");
    }

    TEST_F(ReplayControlTest, TheRecordFlagOnAGameThatCannotRecordLeavesNoRecording) {
        GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
        const String flag = "--record=" + PathText();
        CoreManager::GetCommandLine().Set(Vector<String>{flag});

        StartRecordingFromCommandLine();

        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_FALSE(std::filesystem::exists(_path));
    }

    // The default game of the test binary is the first registered one, which this test does not choose: it holds the flag to what that game can do
    TEST_F(ReplayControlTest, InitializeReadsTheRecordFlagForTheDefaultGame) {
        GameManager::GetInstance().Shutdown();
        const String flag = "--record=" + PathText();
        CoreManager::GetCommandLine().Set(Vector<String>{flag});

        GameManager::GetInstance().Initialize();

        ASSERT_NE(GameManager::GetInstance().GetActive(), nullptr);
        EXPECT_EQ(SessionRecorder::GetInstance().IsRecording(), GameManager::GetInstance().GetActive()->GetName() == "ReplayTest"_intern);
    }

    TEST_F(ReplayControlTest, ShutdownClosesARecordingThatRuns) {
        ASSERT_TRUE(Succeeded(Call("record_start", PathParams())));
        ASSERT_TRUE(Succeeded(Call("reset", Params(R"({"seed":2})"))));

        GameManager::GetInstance().Shutdown();

        EXPECT_FALSE(SessionRecorder::GetInstance().IsRecording());
        EXPECT_EQ(ParseRecording().entries.size(), 1u);
    }

}  // namespace BECore::Tests
