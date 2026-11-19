#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlLog.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Logger/LogEvent.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        void Log(LogLevel level, StringView message) {
            LogEvent::Emit(level, message);
            LogEvent::Flush();
        }

        StringView MessageOf(const LogRecord& record) {
            return {record.message.data(), record.message.size()};
        }

        constexpr size_t kUnlimited = 100000;

        class ControlLogTest : public ::testing::Test {
        protected:
            void SetUp() override {
                LogEvent::Flush();  // lines that an earlier test of this process left in the ring buffer are not this test's
            }
        };

    }  // namespace

    TEST_F(ControlLogTest, DebugAndInfoAreNeitherKeptNorCounted) {
        ControlLog log;
        log.Start();

        Log(LogLevel::Debug, "ControlLogTest debug");
        Log(LogLevel::Info, "ControlLogTest info");

        EXPECT_EQ(log.GetWarnings(), 0U);
        EXPECT_EQ(log.GetErrors(), 0U);
        EXPECT_EQ(log.History().Size(), 0U);
    }

    TEST_F(ControlLogTest, WarningsAndErrorsAreCounted) {
        ControlLog log;
        log.Start();

        Log(LogLevel::Warning, "ControlLogTest warning");
        Log(LogLevel::Error, "ControlLogTest error");
        Log(LogLevel::Fatal, "ControlLogTest fatal");

        EXPECT_EQ(log.GetWarnings(), 1U);
        EXPECT_EQ(log.GetErrors(), 2U);
        EXPECT_EQ(log.History().Size(), 3U);
        EXPECT_EQ(log.GetNextSequence(), 4U);
    }

    TEST_F(ControlLogTest, AMarkSplitsTheLines) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest before");
        const uint64_t mark = log.GetNextSequence();
        Log(LogLevel::Error, "ControlLogTest after");

        const ControlLog::Since since = log.ReadSince(mark, LogLevel::Warning, kUnlimited);

        ASSERT_EQ(since.lines.size(), 1U);
        EXPECT_EQ(MessageOf(*since.lines[0]), "ControlLogTest after");
        EXPECT_EQ(since.lines[0]->level, LogLevel::Error);
        EXPECT_EQ(since.warnings, 0U);
        EXPECT_EQ(since.errors, 1U);
        EXPECT_EQ(since.dropped, 0U);
        EXPECT_EQ(since.next, log.GetNextSequence());
        EXPECT_FALSE(since.truncated);
    }

    TEST_F(ControlLogTest, MarkZeroAndOneBothMeanSinceTheStart) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest first");
        Log(LogLevel::Warning, "ControlLogTest second");

        EXPECT_EQ(log.ReadSince(0, LogLevel::Warning, kUnlimited).lines.size(), 2U);
        EXPECT_EQ(log.ReadSince(1, LogLevel::Warning, kUnlimited).lines.size(), 2U);
        EXPECT_EQ(log.ReadSince(2, LogLevel::Warning, kUnlimited).lines.size(), 1U);
    }

    TEST_F(ControlLogTest, AMarkInTheFutureFindsNothing) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest line");

        const ControlLog::Since since = log.ReadSince(1000, LogLevel::Warning, kUnlimited);

        EXPECT_TRUE(since.lines.empty());
        EXPECT_EQ(since.dropped, 0U);
        EXPECT_EQ(since.warnings, 0U);
    }

    TEST_F(ControlLogTest, TheErrorFilterShortensTheLinesNotTheCounts) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest warning");
        Log(LogLevel::Error, "ControlLogTest error");

        const ControlLog::Since since = log.ReadSince(0, LogLevel::Error, kUnlimited);

        ASSERT_EQ(since.lines.size(), 1U);
        EXPECT_EQ(MessageOf(*since.lines[0]), "ControlLogTest error");
        EXPECT_EQ(since.warnings, 1U);
        EXPECT_EQ(since.errors, 1U);
    }

    TEST_F(ControlLogTest, TheLineCapKeepsTheNewestAndSaysSo) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest one");
        Log(LogLevel::Warning, "ControlLogTest two");
        Log(LogLevel::Warning, "ControlLogTest three");

        const ControlLog::Since since = log.ReadSince(0, LogLevel::Warning, 2);

        ASSERT_EQ(since.lines.size(), 2U);
        EXPECT_EQ(MessageOf(*since.lines[0]), "ControlLogTest two");
        EXPECT_EQ(MessageOf(*since.lines[1]), "ControlLogTest three");
        EXPECT_TRUE(since.truncated);
        EXPECT_EQ(since.warnings, 3U);
    }

    TEST_F(ControlLogTest, DroppedCountsTheLinesTheRingHasLost) {
        constexpr size_t kCapacity = 4;
        ControlLog log(kCapacity);
        log.Start();
        const uint64_t mark = log.GetNextSequence();
        for (int i = 0; i < 6; ++i) {
            Log(LogLevel::Warning, Format("ControlLogTest line {}", i));
        }

        const ControlLog::Since since = log.ReadSince(mark, LogLevel::Warning, kUnlimited);

        EXPECT_EQ(since.dropped, 2U);
        ASSERT_EQ(since.lines.size(), kCapacity);
        EXPECT_EQ(MessageOf(*since.lines.front()), "ControlLogTest line 2");
        EXPECT_EQ(MessageOf(*since.lines.back()), "ControlLogTest line 5");
        EXPECT_EQ(since.warnings, kCapacity);
        EXPECT_EQ(log.GetWarnings(), 6U);

        EXPECT_EQ(log.ReadSince(log.GetNextSequence() - 1, LogLevel::Warning, kUnlimited).dropped, 0U);
    }

    TEST_F(ControlLogTest, TheLinesAreOldestFirstWithTheClockText) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest a");
        Log(LogLevel::Error, "ControlLogTest b");

        const ControlLog::Since since = log.ReadSince(0, LogLevel::Warning, kUnlimited);

        ASSERT_EQ(since.lines.size(), 2U);
        EXPECT_LT(since.lines[0]->sequence, since.lines[1]->sequence);
        EXPECT_EQ(MessageOf(*since.lines[0]), "ControlLogTest a");
        const StringView time(since.lines[0]->time);
        ASSERT_EQ(time.size(), kLogTimeLength - 1);
        EXPECT_EQ(time[2], ':');
        EXPECT_EQ(time[5], ':');
        EXPECT_EQ(time[8], '.');
    }

    TEST_F(ControlLogTest, ALineFromAWorkerThreadAppearsAfterFlush) {
        if constexpr (!Thread::kAvailable) {
            GTEST_SKIP() << "no threads in this build";
        }
        ControlLog log;
        log.Start();

        Thread thread("ControlLogWriter", [] { LogEvent::Emit(LogLevel::Error, "ControlLogTest worker line"); });
        thread.join();
        EXPECT_EQ(log.GetErrors(), 0U);
        LogEvent::Flush();

        EXPECT_EQ(log.GetErrors(), 1U);
    }

    TEST_F(ControlLogTest, StartingTwiceSubscribesOnce) {
        ControlLog log;
        log.Start();
        log.Start();

        Log(LogLevel::Warning, "ControlLogTest once");

        EXPECT_EQ(log.GetWarnings(), 1U);
        EXPECT_EQ(log.History().Size(), 1U);
    }

    TEST_F(ControlLogTest, ALineBeforeStartIsNotCounted) {
        ControlLog log;
        Log(LogLevel::Warning, "ControlLogTest early");

        log.Start();

        EXPECT_EQ(log.GetWarnings(), 0U);
        EXPECT_EQ(log.History().Size(), 0U);
    }

    TEST_F(ControlLogTest, AServiceOwnsALogThatListensAfterStart) {
        ControlService service;
        Log(LogLevel::Warning, "ControlLogTest before service start");
        EXPECT_EQ(service.Log().GetWarnings(), 0U);

        service.Log().Start();
        Log(LogLevel::Warning, "ControlLogTest after service start");

        EXPECT_EQ(service.Log().GetWarnings(), 1U);
    }

    namespace {

        /// A service with the builtins, whose answers and pushed events are collected
        class ControlLogMethodsTest : public ::testing::Test {
        protected:
            void SetUp() override {
                LogEvent::Flush();  // lines of an earlier test in this process are not this service's
                RegisterControlBuiltins(service);
                service.SetEventSink([this](const String& text) {
                    auto parsed = ParseJson(text);
                    ASSERT_TRUE(parsed.has_value());
                    events.push_back(std::move(*parsed));
                });
            }

            /// Runs one request and returns its response
            Json Call(StringView method, const Json& params = Json::object()) {
                Json request = Json::object();
                request["id"] = ++nextId;
                request["method"] = ToStdString(method);
                request["params"] = params;
                return Run(DumpJson(request));
            }

            Json Run(const String& text) {
                const size_t before = responses.size();
                service.Post(text, [this](String reply) {
                    auto parsed = ParseJson(reply);
                    responses.push_back(parsed ? std::move(*parsed) : Json());
                });
                service.Drain();
                EXPECT_EQ(responses.size(), before + 1);
                return responses.size() == before + 1 ? responses.back() : Json();
            }

            static const Json& ResultOf(const Json& response) {
                const Json* result = FindJsonMember(response, "result");
                EXPECT_NE(result, nullptr);
                static const Json kEmpty = Json::object();
                return result != nullptr ? *result : kEmpty;
            }

            static String ErrorCodeOf(const Json& response) {
                const Json* error = FindJsonMember(response, "error");
                if (error == nullptr) {
                    return {};
                }
                const auto code = GetJsonString(*error, "code");
                return code ? String(code->c_str(), code->size()) : String();
            }

            static uint64_t Number(const Json& object, StringView key) {
                return static_cast<uint64_t>(GetJsonInt(object, key).value_or(-1));
            }

            static void Warn(StringView message, LogLevel level = LogLevel::Warning) {
                LogEvent::Emit(level, message);
            }

            ControlService service;
            Vector<Json> responses;
            Vector<Json> events;
            int64_t nextId = 0;
        };

        constexpr int kSurplusLines = 5;

    }  // namespace

    TEST_F(ControlLogMethodsTest, StatusCarriesTheTotalsAndSeesALineNotFlushedYet) {
        Warn("ControlLogTest status warning");
        Warn("ControlLogTest status error", LogLevel::Error);

        const Json result = ResultOf(Call("status"));

        const Json* log = FindJsonMember(result, "log");
        ASSERT_NE(log, nullptr);
        EXPECT_EQ(Number(*log, "warnings"), 1U);
        EXPECT_EQ(Number(*log, "errors"), 1U);
    }

    TEST_F(ControlLogMethodsTest, ALineLoggedInAHandlerIsCountedByTheNextStatusOfTheFrame) {
        service.RegisterHandler("noisy", [](ControlCall& call) {
            LogEvent::Emit(LogLevel::Warning, "ControlLogTest from a handler");
            call.Reply(Json::object());
        });
        Vector<Json> answers;
        const auto collect = [&answers](String text) {
            auto parsed = ParseJson(text);
            answers.push_back(parsed ? std::move(*parsed) : Json());
        };

        service.Post(R"({"id": 1, "method": "noisy"})", collect);
        service.Post(R"({"id": 2, "method": "status"})", collect);
        service.Drain();

        ASSERT_EQ(answers.size(), 2U);
        const Json* log = FindJsonMember(ResultOf(answers[1]), "log");
        ASSERT_NE(log, nullptr);
        EXPECT_EQ(Number(*log, "warnings"), 1U);
    }

    TEST_F(ControlLogMethodsTest, LogMarkThenLogSinceGivesTheNewLine) {
        Warn("ControlLogTest old line");
        const Json mark = ResultOf(Call("log_mark"));
        EXPECT_EQ(Number(mark, "warnings"), 1U);
        EXPECT_EQ(Number(mark, "errors"), 0U);

        Warn("ControlLogTest new line");
        Json params = Json::object();
        params["mark"] = Number(mark, "mark");
        const Json since = ResultOf(Call("log_since", params));

        const Json* lines = FindJsonMember(since, "lines");
        ASSERT_NE(lines, nullptr);
        ASSERT_EQ(lines->size(), 1U);
        const Json& line = (*lines)[0];
        EXPECT_EQ(GetJsonString(line, "message").value_or(""), "ControlLogTest new line");
        EXPECT_EQ(GetJsonString(line, "level").value_or(""), "warning");
        EXPECT_EQ(GetJsonString(line, "time").value_or("").size(), kLogTimeLength - 1);
        EXPECT_EQ(Number(line, "sequence"), Number(mark, "mark"));
        EXPECT_EQ(Number(since, "warnings"), 1U);
        EXPECT_EQ(Number(since, "errors"), 0U);
        EXPECT_EQ(Number(since, "dropped"), 0U);
        EXPECT_EQ(FindJsonMember(since, "truncated"), nullptr);

        const Json after = ResultOf(Call("log_mark"));
        EXPECT_EQ(Number(since, "next"), Number(after, "mark"));
    }

    TEST_F(ControlLogMethodsTest, TheErrorFilterShortensTheLinesButNotTheCounts) {
        Warn("ControlLogTest filter warning");
        Warn("ControlLogTest filter error", LogLevel::Error);
        Warn("ControlLogTest filter fatal", LogLevel::Fatal);
        Json params = Json::object();
        params["mark"] = 0;

        const Json all = ResultOf(Call("log_since", params));
        params["level"] = "error";
        const Json errorsOnly = ResultOf(Call("log_since", params));

        EXPECT_EQ(Require(FindJsonMember(all, "lines")).size(), 3U);
        const Json* lines = FindJsonMember(errorsOnly, "lines");
        ASSERT_NE(lines, nullptr);
        ASSERT_EQ(lines->size(), 2U);
        EXPECT_EQ(GetJsonString((*lines)[0], "level").value_or(""), "error");
        EXPECT_EQ(GetJsonString((*lines)[1], "level").value_or(""), "fatal");
        EXPECT_EQ(Number(errorsOnly, "warnings"), 1U);
        EXPECT_EQ(Number(errorsOnly, "errors"), 2U);
    }

    TEST_F(ControlLogMethodsTest, LogSinceRefusesBadParameters) {
        EXPECT_EQ(ErrorCodeOf(Call("log_since")), "invalid_params");

        const auto since = [this](const Json& mark) {
            Json params = Json::object();
            params["mark"] = mark;
            return Call("log_since", params);
        };
        EXPECT_EQ(ErrorCodeOf(since(-1)), "invalid_params");
        EXPECT_EQ(ErrorCodeOf(since(1.5)), "invalid_params");
        EXPECT_EQ(ErrorCodeOf(since("3")), "invalid_params");
        EXPECT_EQ(ErrorCodeOf(since(Json())), "invalid_params");

        Json params = Json::object();
        params["mark"] = 0;
        params["level"] = "info";
        EXPECT_EQ(ErrorCodeOf(Call("log_since", params)), "invalid_params");
        params["level"] = 3;
        EXPECT_EQ(ErrorCodeOf(Call("log_since", params)), "invalid_params");

        EXPECT_EQ(ErrorCodeOf(Run(R"({"id": 99, "method": "log_since", "params": 5})")), "invalid_params");
        EXPECT_EQ(ErrorCodeOf(Run(R"({"id": 100, "method": "log_since", "params": [0]})")), "invalid_params");
    }

    TEST_F(ControlLogMethodsTest, TheLineCapSetsTruncated) {
        constexpr int kLines = static_cast<int>(ControlLog::kMaxLogSinceLines) + kSurplusLines;
        for (int i = 0; i < kLines; ++i) {
            Warn(Format("ControlLogTest cap {}", i));
        }
        Json params = Json::object();
        params["mark"] = 0;

        const Json since = ResultOf(Call("log_since", params));

        const Json* lines = FindJsonMember(since, "lines");
        ASSERT_NE(lines, nullptr);
        ASSERT_EQ(lines->size(), ControlLog::kMaxLogSinceLines);
        EXPECT_EQ(GetJsonString(lines->back(), "message").value_or(""), Format("ControlLogTest cap {}", kLines - 1));
        EXPECT_EQ(GetJsonBool(since, "truncated"), true);
        EXPECT_EQ(Number(since, "warnings"), static_cast<uint64_t>(kLines));
    }

    TEST_F(ControlLogMethodsTest, NothingIsPushedUntilSubscribed) {
        Warn("ControlLogTest unsubscribed");
        LogEvent::Flush();
        service.EndFrame();

        EXPECT_TRUE(events.empty());
    }

    TEST_F(ControlLogMethodsTest, SubscribedLinesArePushedOnePerEvent) {
        Json params = Json::object();
        params["enabled"] = true;
        const Json answer = ResultOf(Call("log_subscribe", params));
        EXPECT_EQ(GetJsonBool(answer, "enabled"), true);
        EXPECT_EQ(GetJsonString(answer, "level").value_or(""), "warning");

        Warn("ControlLogTest pushed warning");
        Warn("ControlLogTest pushed error", LogLevel::Error);
        LogEvent::Flush();
        service.EndFrame();

        ASSERT_EQ(events.size(), 2U);
        EXPECT_EQ(GetJsonString(events[0], "event").value_or(""), "log");
        const Json* data = FindJsonMember(events[0], "data");
        ASSERT_NE(data, nullptr);
        EXPECT_EQ(GetJsonString(*data, "message").value_or(""), "ControlLogTest pushed warning");
        EXPECT_EQ(GetJsonString(*data, "level").value_or(""), "warning");
        EXPECT_EQ(GetJsonString(*data, "time").value_or("").size(), kLogTimeLength - 1);
        EXPECT_NE(GetJsonInt(*data, "sequence").value_or(0), 0);
        EXPECT_EQ(FindJsonMember(*data, "skipped"), nullptr);
        EXPECT_EQ(GetJsonString(Require(FindJsonMember(events[1], "data")), "level").value_or(""), "error");

        service.EndFrame();
        EXPECT_EQ(events.size(), 2U);
    }

    TEST_F(ControlLogMethodsTest, SubscribingStartsAtTheNextLine) {
        Warn("ControlLogTest before subscribing");
        LogEvent::Flush();
        Json params = Json::object();
        params["enabled"] = true;
        Call("log_subscribe", params);

        service.EndFrame();

        EXPECT_TRUE(events.empty());
    }

    TEST_F(ControlLogMethodsTest, TheErrorLevelSkipsWarnings) {
        Json params = Json::object();
        params["enabled"] = true;
        params["level"] = "error";
        EXPECT_EQ(GetJsonString(ResultOf(Call("log_subscribe", params)), "level").value_or(""), "error");

        Warn("ControlLogTest quiet warning");
        Warn("ControlLogTest loud error", LogLevel::Error);
        LogEvent::Flush();
        service.EndFrame();

        ASSERT_EQ(events.size(), 1U);
        EXPECT_EQ(GetJsonString(Require(FindJsonMember(events[0], "data")), "message").value_or(""), "ControlLogTest loud error");
    }

    TEST_F(ControlLogMethodsTest, MoreThanTheCapInAFrameSaysHowManyWereSkipped) {
        Json params = Json::object();
        params["enabled"] = true;
        Call("log_subscribe", params);
        constexpr uint32_t kLines = ControlLog::kMaxLogPushPerFrame + kSurplusLines;
        for (uint32_t i = 0; i < kLines; ++i) {
            Warn(Format("ControlLogTest burst {}", i));
        }
        LogEvent::Flush();

        service.EndFrame();

        ASSERT_EQ(events.size(), ControlLog::kMaxLogPushPerFrame);
        for (size_t i = 0; i + 1 < events.size(); ++i) {
            EXPECT_EQ(FindJsonMember(Require(FindJsonMember(events[i], "data")), "skipped"), nullptr);
        }
        const Json& last = Require(FindJsonMember(events.back(), "data"));
        EXPECT_EQ(Number(last, "skipped"), static_cast<uint64_t>(kSurplusLines));
        EXPECT_EQ(GetJsonString(last, "message").value_or(""), Format("ControlLogTest burst {}", ControlLog::kMaxLogPushPerFrame - 1));

        service.EndFrame();
        EXPECT_EQ(events.size(), ControlLog::kMaxLogPushPerFrame);
    }

    TEST_F(ControlLogMethodsTest, TurningItOffStopsThePush) {
        Json params = Json::object();
        params["enabled"] = true;
        Call("log_subscribe", params);
        params["enabled"] = false;
        EXPECT_EQ(GetJsonBool(ResultOf(Call("log_subscribe", params)), "enabled"), false);

        Warn("ControlLogTest after unsubscribing");
        LogEvent::Flush();
        service.EndFrame();

        EXPECT_TRUE(events.empty());
    }

    TEST_F(ControlLogMethodsTest, LogSubscribeRefusesBadParameters) {
        EXPECT_EQ(ErrorCodeOf(Call("log_subscribe")), "invalid_params");
        Json params = Json::object();
        params["enabled"] = 1;
        EXPECT_EQ(ErrorCodeOf(Call("log_subscribe", params)), "invalid_params");
        params["enabled"] = true;
        params["level"] = "debug";
        EXPECT_EQ(ErrorCodeOf(Call("log_subscribe", params)), "invalid_params");
    }

    TEST_F(ControlLogMethodsTest, TheMethodsAreDescribed) {
        const Vector<ControlMethodListing> methods = service.GetMethods();
        for (const StringView name : {StringView("log_mark"), StringView("log_since"), StringView("log_subscribe")}) {
            const auto found = std::find_if(methods.begin(), methods.end(), [name](const ControlMethodListing& listing) { return listing.name == name; });
            ASSERT_NE(found, methods.end()) << name.data();
            EXPECT_NE(found->info, nullptr) << name.data();
        }
    }

}  // namespace BECore::Tests
