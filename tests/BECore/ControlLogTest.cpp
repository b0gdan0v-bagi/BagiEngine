#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlLog.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Logger/LogEvent.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
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
                service.SetEventSink([this](const String& text) { events.emplace_back(StringView(text.c_str(), text.size())); });
            }

            /// Runs one request and returns its response (a view of the reply kept in `responses`, so it stays valid); not valid when nothing was answered
            JsonView Call(StringView method, StringView paramsText = "{}") {
                return Run(MakeRequestText(++nextId, method, paramsText));
            }

            JsonView Run(const String& text) {
                const size_t before = responses.size();
                service.Post(text, [this](String reply) { responses.emplace_back(StringView(reply.c_str(), reply.size())); });
                service.Drain();
                EXPECT_EQ(responses.size(), before + 1);
                return responses.size() == before + 1 ? responses.back().View() : JsonView();
            }

            static JsonView ResultOf(JsonView response) {
                const JsonView result = MemberOf(response, "result");
                EXPECT_TRUE(result.IsValid());
                return result;
            }

            static String ErrorCodeOf(JsonView response) {
                return StringOf(MemberOf(response, "error"), "code");
            }

            static uint64_t Number(JsonView object, StringView key) {
                return static_cast<uint64_t>(IntOf(object, key).value_or(-1));
            }

            /// The text of the answer as it was sent, with the clock text of every line replaced by `T`
            String Raw(StringView method, StringView paramsText = "{}") {
                String text;
                service.Post(MakeRequestText(++nextId, method, paramsText), [&text](const String& reply) { text = reply; });
                service.Drain();
                return MaskTimes(text);
            }

            static String MaskTimes(const String& text) {
                constexpr StringView kTimeKey = "\"time\":\"";
                String masked;
                size_t from = 0;
                for (size_t at = text.find(kTimeKey.data(), 0, kTimeKey.size()); at != String::npos; at = text.find(kTimeKey.data(), from, kTimeKey.size())) {
                    const size_t begin = at + kTimeKey.size();
                    masked.append(text.data() + from, begin - from);
                    masked.push_back('T');
                    from = text.find('"', begin);
                }
                masked.append(text.data() + from, text.size() - from);
                return masked;
            }

            static void Warn(StringView message, LogLevel level = LogLevel::Warning) {
                LogEvent::Emit(level, message);
            }

            ControlService service;
            Vector<ControlReply> responses;
            Vector<ControlReply> events;
            int64_t nextId = 0;
        };

        constexpr int kSurplusLines = 5;

    }  // namespace

    TEST_F(ControlLogMethodsTest, StatusCarriesTheTotalsAndSeesALineNotFlushedYet) {
        Warn("ControlLogTest status warning");
        Warn("ControlLogTest status error", LogLevel::Error);

        const JsonView result = ResultOf(Call("status"));

        const JsonView log = MemberOf(result, "log");
        ASSERT_TRUE(log.IsObject());
        EXPECT_EQ(Number(log, "warnings"), 1U);
        EXPECT_EQ(Number(log, "errors"), 1U);
    }

    TEST_F(ControlLogMethodsTest, ALineLoggedInAHandlerIsCountedByTheNextStatusOfTheFrame) {
        service.RegisterHandler("noisy", [](ControlCall& call) {
            LogEvent::Emit(LogLevel::Warning, "ControlLogTest from a handler");
            call.ReplyEmpty();
        });
        Vector<ControlReply> answers;
        const auto collect = [&answers](String text) { answers.emplace_back(StringView(text.c_str(), text.size())); };

        service.Post(R"({"id": 1, "method": "noisy"})", collect);
        service.Post(R"({"id": 2, "method": "status"})", collect);
        service.Drain();

        ASSERT_EQ(answers.size(), 2U);
        const JsonView log = MemberOf(ResultOf(answers[1].View()), "log");
        ASSERT_TRUE(log.IsObject());
        EXPECT_EQ(Number(log, "warnings"), 1U);
    }

    TEST_F(ControlLogMethodsTest, LogMarkThenLogSinceGivesTheNewLine) {
        Warn("ControlLogTest old line");
        const JsonView mark = ResultOf(Call("log_mark"));
        EXPECT_EQ(Number(mark, "warnings"), 1U);
        EXPECT_EQ(Number(mark, "errors"), 0U);

        Warn("ControlLogTest new line");
        const JsonView since = ResultOf(Call("log_since", Format(R"({{"mark":{}}})", Number(mark, "mark"))));

        const JsonView lines = MemberOf(since, "lines");
        ASSERT_TRUE(lines.IsArray());
        ASSERT_EQ(lines.Size(), 1U);
        const JsonView line = ItemAt(lines, 0);
        EXPECT_EQ(StringOf(line, "message"), "ControlLogTest new line");
        EXPECT_EQ(StringOf(line, "level"), "warning");
        EXPECT_EQ(StringOf(line, "time").size(), kLogTimeLength - 1);
        EXPECT_EQ(Number(line, "sequence"), Number(mark, "mark"));
        EXPECT_EQ(Number(since, "warnings"), 1U);
        EXPECT_EQ(Number(since, "errors"), 0U);
        EXPECT_EQ(Number(since, "dropped"), 0U);
        EXPECT_FALSE(MemberOf(since, "truncated").IsValid());

        const JsonView after = ResultOf(Call("log_mark"));
        EXPECT_EQ(Number(since, "next"), Number(after, "mark"));
    }

    TEST_F(ControlLogMethodsTest, TheErrorFilterShortensTheLinesButNotTheCounts) {
        Warn("ControlLogTest filter warning");
        Warn("ControlLogTest filter error", LogLevel::Error);
        Warn("ControlLogTest filter fatal", LogLevel::Fatal);

        const JsonView all = ResultOf(Call("log_since", R"({"mark":0})"));
        const JsonView errorsOnly = ResultOf(Call("log_since", R"({"level":"error","mark":0})"));

        EXPECT_EQ(MemberOf(all, "lines").Size(), 3U);
        const JsonView lines = MemberOf(errorsOnly, "lines");
        ASSERT_TRUE(lines.IsArray());
        ASSERT_EQ(lines.Size(), 2U);
        EXPECT_EQ(StringOf(ItemAt(lines, 0), "level"), "error");
        EXPECT_EQ(StringOf(ItemAt(lines, 1), "level"), "fatal");
        EXPECT_EQ(Number(errorsOnly, "warnings"), 1U);
        EXPECT_EQ(Number(errorsOnly, "errors"), 2U);
    }

    TEST_F(ControlLogMethodsTest, LogSinceRefusesBadParameters) {
        EXPECT_EQ(ErrorCodeOf(Call("log_since")), "invalid_params");

        const auto since = [this](StringView markText) { return Call("log_since", Format(R"({{"mark":{}}})", markText)); };
        EXPECT_EQ(ErrorCodeOf(since("-1")), "invalid_params");
        EXPECT_EQ(ErrorCodeOf(since("1.5")), "invalid_params");
        EXPECT_EQ(ErrorCodeOf(since(R"("3")")), "invalid_params");
        EXPECT_EQ(ErrorCodeOf(since("null")), "invalid_params");

        EXPECT_EQ(ErrorCodeOf(Call("log_since", R"({"level":"info","mark":0})")), "invalid_params");
        EXPECT_EQ(ErrorCodeOf(Call("log_since", R"({"level":3,"mark":0})")), "invalid_params");

        EXPECT_EQ(ErrorCodeOf(Run(R"({"id": 99, "method": "log_since", "params": 5})")), "invalid_params");
        EXPECT_EQ(ErrorCodeOf(Run(R"({"id": 100, "method": "log_since", "params": [0]})")), "invalid_params");
    }

    TEST_F(ControlLogMethodsTest, TheLineCapSetsTruncated) {
        constexpr int kLines = static_cast<int>(ControlLog::kMaxLogSinceLines) + kSurplusLines;
        for (int i = 0; i < kLines; ++i) {
            Warn(Format("ControlLogTest cap {}", i));
        }

        const JsonView since = ResultOf(Call("log_since", R"({"mark":0})"));

        const JsonView lines = MemberOf(since, "lines");
        ASSERT_TRUE(lines.IsArray());
        ASSERT_EQ(lines.Size(), ControlLog::kMaxLogSinceLines);
        EXPECT_EQ(StringOf(ItemAt(lines, lines.Size() - 1), "message"), Format("ControlLogTest cap {}", kLines - 1));
        EXPECT_EQ(BoolOf(since, "truncated"), true);
        EXPECT_EQ(Number(since, "warnings"), static_cast<uint64_t>(kLines));
    }

    TEST_F(ControlLogMethodsTest, TheLogMethodsAnswerTheSameBytes) {
        constexpr LogSite kSite{"src/Modules/GoldenCategory/Golden.cpp", 3};
        Vector<String> replies;
        replies.push_back(Raw("log_mark"));
        LogEvent::Emit(LogLevel::Warning, "ControlLogTest golden warning", kSite);
        LogEvent::Emit(LogLevel::Error, "quote \" slash \\ newline \n caf\xC3\xA9 bad \xFF end");
        LogEvent::Emit(LogLevel::Fatal, "ControlLogTest golden fatal");
        replies.push_back(Raw("log_since", R"({"mark":0})"));
        replies.push_back(Raw("log_since", R"({"level":"error","mark":0})"));
        replies.push_back(Raw("log_mark"));
        replies.push_back(Raw("log_subscribe", R"({"enabled":true})"));
        replies.push_back(Raw("log_subscribe", R"({"enabled":true,"level":"error"})"));
        replies.push_back(Raw("log_subscribe", R"({"enabled":false,"level":"error"})"));

        ExpectTexts(
            replies,
            {R"json({"id":1,"ok":true,"result":{"errors":0,"mark":1,"warnings":0}})json",
             R"json({"id":2,"ok":true,"result":{"dropped":0,"errors":2,"lines":[{"category":"GoldenCategory","level":"warning","message":"ControlLogTest golden warning","sequence":1,"time":"T"},{"level":"error","message":"quote \" slash \\ newline \n café bad � end","sequence":2,"time":"T"},{"level":"fatal","message":"ControlLogTest golden fatal","sequence":3,"time":"T"}],"next":4,"warnings":1}})json",
             R"json({"id":3,"ok":true,"result":{"dropped":0,"errors":2,"lines":[{"level":"error","message":"quote \" slash \\ newline \n café bad � end","sequence":2,"time":"T"},{"level":"fatal","message":"ControlLogTest golden fatal","sequence":3,"time":"T"}],"next":4,"warnings":1}})json",
             R"json({"id":4,"ok":true,"result":{"errors":2,"mark":4,"warnings":1}})json", R"json({"id":5,"ok":true,"result":{"enabled":true,"level":"warning"}})json",
             R"json({"id":6,"ok":true,"result":{"enabled":true,"level":"error"}})json", R"json({"id":7,"ok":true,"result":{"enabled":false,"level":"error"}})json"});
    }

    TEST_F(ControlLogMethodsTest, ATruncatedLogSinceAnswersTheSameTail) {
        for (size_t i = 0; i < ControlLog::kMaxLogSinceLines + kSurplusLines; ++i) {
            Warn(Format("ControlLogTest golden {}", i));
        }

        const String text = Raw("log_since", R"({"mark":0})");

        const size_t next = text.find("\"next\":");
        ASSERT_NE(next, String::npos);
        EXPECT_EQ(String(text.substr(next)), R"json("next":206,"truncated":true,"warnings":205}})json");
    }

    TEST_F(ControlLogMethodsTest, ThePushedLinesAreTheSameBytesAndTheLastOneCountsTheSkipped) {
        Vector<String> pushed;
        service.SetEventSink([&pushed](const String& text) { pushed.push_back(MaskTimes(text)); });
        Raw("log_subscribe", R"({"enabled":true})");
        constexpr LogSite kSite{"src/Modules/GoldenCategory/Golden.cpp", 3};
        LogEvent::Emit(LogLevel::Warning, "ControlLogTest golden push \"quoted\" caf\xC3\xA9", kSite);
        for (size_t i = 0; i < ControlLog::kMaxLogPushPerFrame + kSurplusLines; ++i) {
            Warn(Format("ControlLogTest golden push {}", i));
        }
        LogEvent::Flush();
        service.EndFrame();

        ASSERT_EQ(pushed.size(), ControlLog::kMaxLogPushPerFrame);
        ExpectTexts({pushed.front(), pushed[1], pushed.back()},
                    {R"json({"data":{"category":"GoldenCategory","level":"warning","message":"ControlLogTest golden push \"quoted\" café","sequence":1,"time":"T"},"event":"log"})json",
                     R"json({"data":{"level":"warning","message":"ControlLogTest golden push 0","sequence":2,"time":"T"},"event":"log"})json",
                     R"json({"data":{"level":"warning","message":"ControlLogTest golden push 14","sequence":16,"skipped":6,"time":"T"},"event":"log"})json"});
    }

    TEST_F(ControlLogMethodsTest, ASecondLogSinceWithLinesAllocatesNothingBeyondWhatItReads) {
        // ReadSince() lists the lines in a Vector, which allocates whoever asks: a method that reads the same lines and answers `{}` is the measure of the rest
        service.RegisterHandler("reads_only", [this](ControlCall& call) {
            const ControlLog::Since since = service.Log().ReadSince(0, LogLevel::Warning, ControlLog::kMaxLogSinceLines);
            call.Reply([](JsonWriter& writer) {
                writer.BeginObject();
                writer.EndObject();
            });
        });
        for (size_t i = 0; i < kSurplusLines; ++i) {
            Warn(Format("ControlLogTest alloc {}", i));
        }
        LogEvent::Flush();
        const auto drained = [&](StringView method) {
            service.Post(MakeRequestText(++nextId, method, R"({"mark":0})"), [](const String&) {});
            return CountAllocations([this] { service.Drain(); });
        };
        drained("log_since");
        drained("reads_only");

        const AllocCount since = drained("log_since");
        const AllocCount reads = drained("reads_only");

        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        EXPECT_EQ(since.count, reads.count);
        EXPECT_EQ(since.bytes, reads.bytes);
    }

    TEST_F(ControlLogMethodsTest, NothingIsPushedUntilSubscribed) {
        Warn("ControlLogTest unsubscribed");
        LogEvent::Flush();
        service.EndFrame();

        EXPECT_TRUE(events.empty());
    }

    TEST_F(ControlLogMethodsTest, SubscribedLinesArePushedOnePerEvent) {
        const JsonView answer = ResultOf(Call("log_subscribe", R"({"enabled":true})"));
        EXPECT_EQ(BoolOf(answer, "enabled"), true);
        EXPECT_EQ(StringOf(answer, "level"), "warning");

        Warn("ControlLogTest pushed warning");
        Warn("ControlLogTest pushed error", LogLevel::Error);
        LogEvent::Flush();
        service.EndFrame();

        ASSERT_EQ(events.size(), 2U);
        EXPECT_EQ(StringOf(events[0].View(), "event"), "log");
        const JsonView data = MemberOf(events[0].View(), "data");
        ASSERT_TRUE(data.IsObject());
        EXPECT_EQ(StringOf(data, "message"), "ControlLogTest pushed warning");
        EXPECT_EQ(StringOf(data, "level"), "warning");
        EXPECT_EQ(StringOf(data, "time").size(), kLogTimeLength - 1);
        EXPECT_NE(IntOf(data, "sequence").value_or(0), 0);
        EXPECT_FALSE(MemberOf(data, "skipped").IsValid());
        EXPECT_EQ(StringOf(MemberOf(events[1].View(), "data"), "level"), "error");

        service.EndFrame();
        EXPECT_EQ(events.size(), 2U);
    }

    TEST_F(ControlLogMethodsTest, SubscribingStartsAtTheNextLine) {
        Warn("ControlLogTest before subscribing");
        LogEvent::Flush();
        Call("log_subscribe", R"({"enabled":true})");

        service.EndFrame();

        EXPECT_TRUE(events.empty());
    }

    TEST_F(ControlLogMethodsTest, TheErrorLevelSkipsWarnings) {
        EXPECT_EQ(StringOf(ResultOf(Call("log_subscribe", R"({"enabled":true,"level":"error"})")), "level"), "error");

        Warn("ControlLogTest quiet warning");
        Warn("ControlLogTest loud error", LogLevel::Error);
        LogEvent::Flush();
        service.EndFrame();

        ASSERT_EQ(events.size(), 1U);
        EXPECT_EQ(StringOf(MemberOf(events[0].View(), "data"), "message"), "ControlLogTest loud error");
    }

    TEST_F(ControlLogMethodsTest, MoreThanTheCapInAFrameSaysHowManyWereSkipped) {
        Call("log_subscribe", R"({"enabled":true})");
        constexpr uint32_t kLines = ControlLog::kMaxLogPushPerFrame + kSurplusLines;
        for (uint32_t i = 0; i < kLines; ++i) {
            Warn(Format("ControlLogTest burst {}", i));
        }
        LogEvent::Flush();

        service.EndFrame();

        ASSERT_EQ(events.size(), ControlLog::kMaxLogPushPerFrame);
        for (size_t i = 0; i + 1 < events.size(); ++i) {
            EXPECT_FALSE(MemberOf(MemberOf(events[i].View(), "data"), "skipped").IsValid());
        }
        const JsonView last = MemberOf(events.back().View(), "data");
        EXPECT_EQ(Number(last, "skipped"), static_cast<uint64_t>(kSurplusLines));
        EXPECT_EQ(StringOf(last, "message"), Format("ControlLogTest burst {}", ControlLog::kMaxLogPushPerFrame - 1));

        service.EndFrame();
        EXPECT_EQ(events.size(), ControlLog::kMaxLogPushPerFrame);
    }

    TEST_F(ControlLogMethodsTest, TurningItOffStopsThePush) {
        Call("log_subscribe", R"({"enabled":true})");
        EXPECT_EQ(BoolOf(ResultOf(Call("log_subscribe", R"({"enabled":false})")), "enabled"), false);

        Warn("ControlLogTest after unsubscribing");
        LogEvent::Flush();
        service.EndFrame();

        EXPECT_TRUE(events.empty());
    }

    TEST_F(ControlLogMethodsTest, TextThatTheDomCouldNotHoldIsReadByTheTape) {
        EXPECT_EQ(ErrorCodeOf(Run(R"({"id": 1, "method": "log_since", "params": {"mark": 1e999}})")), "invalid_params") << "a number past a double is refused by the reader, not at parse";
        EXPECT_EQ(BoolOf(Run(R"({"id": 2, "method": "log_since", "params": {"mark": 0, "level": "err\u006fr"}})"), "ok"), true) << "an escape in `level` is decoded";
        EXPECT_EQ(ErrorCodeOf(Run(R"({"id": 3, "method": "log_subscribe", "params": {"enabled": true, "level": "warn\u0069ng!"}})")), "invalid_params");
    }

    TEST_F(ControlLogMethodsTest, LogSubscribeRefusesBadParameters) {
        EXPECT_EQ(ErrorCodeOf(Call("log_subscribe")), "invalid_params");
        EXPECT_EQ(ErrorCodeOf(Call("log_subscribe", R"({"enabled":1})")), "invalid_params");
        EXPECT_EQ(ErrorCodeOf(Call("log_subscribe", R"({"enabled":true,"level":"debug"})")), "invalid_params");
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
