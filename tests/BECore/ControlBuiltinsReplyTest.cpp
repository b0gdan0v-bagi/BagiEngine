// The text of the small built-in answers (`ping`, `status`, `step`, `pause`, `resume`): written with JsonWriter, and pinned to the bytes the `Json` DOM wrote
// before it (so a drift of a key or of a number's text fails here).

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Time/GamePause.h>
#include <BECore/Time/Time.h>
#include <BECore/UI/UIEnvironment.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr double kFrameSeconds = 0.125;

        /// The text `status` answers for a service with no main window (the test environment has none) on the clock the fixture sets (two frames of
        /// kFrameSeconds); pinned from what the DOM wrote before the writer
        String StatusText(bool manualClock, bool paused) {
            return Format(R"({{"deltaTime":0.125,"fixedDeltaTime":0.01666666753590107,"frame":2,"log":{{"errors":0,"warnings":0}},"manualClock":{},"paused":{},"totalTime":0.25,)"
                          R"("window":{{"density":1.0,"displayScale":1.0,"headless":false,"height":0,"outputHeight":0,"outputWidth":0,)"
                          R"("safeArea":{{"bottom":0.0,"left":0.0,"right":0.0,"top":0.0}},"userScale":1.0,"width":0}}}})",
                          manualClock, paused);
        }

        String OkText(StringView id, StringView resultText) {
            return Format(R"({{"id":{},"ok":true,"result":{}}})", id, resultText);
        }

        class ControlBuiltinsReplyTest : public ::testing::Test {
        protected:
            void SetUp() override {
                LogEvent::Flush();  // an error line that an earlier test of this process left in the ring buffer is not this service's
                CoreManager::GetTime().Reset();
                CoreManager::GetTime().Advance(kFrameSeconds);
                CoreManager::GetTime().Advance(kFrameSeconds);
                RegisterControlBuiltins(service);
                service.SetPause(&pause);
            }

            void TearDown() override {
                service.SetPause(nullptr);  // the pause is a member that dies before the service
                CoreManager::GetTime().Reset();
            }

            /// The raw response text of one request
            String Ask(int64_t id, StringView method, StringView paramsText = {}) {
                String text;
                service.Post(MakeRequestText(id, method, paramsText), [&text](const String& reply) { text = reply; });
                service.Drain();
                return text;
            }

            GamePause pause;
            ControlService service;
        };

    }  // namespace

    TEST_F(ControlBuiltinsReplyTest, PingIsPinnedToItsText) {
        EXPECT_EQ(Ask(1, "ping"), String(R"({"id":1,"ok":true,"result":{"pong":true,"protocol":1}})"));
    }

    TEST_F(ControlBuiltinsReplyTest, StatusIsX) {
        if (CoreManager::GetMainWindow()) {
            GTEST_SKIP() << "the reference describes a service with no main window";
        }

        EXPECT_EQ(Ask(1, "status"), OkText("1", StatusText(false, false)));

        pause.SetPaused(true);
        EXPECT_EQ(Ask(2, "status"), OkText("2", StatusText(false, true)));

        service.SetManualClock(true);
        EXPECT_EQ(Ask(3, "status"), OkText("3", StatusText(true, true)));
    }

    TEST_F(ControlBuiltinsReplyTest, StatusWithoutAPauseReportsNotPaused) {
        service.SetPause(nullptr);

        const ControlReply reply(Ask(1, "status"));

        ASSERT_TRUE(reply.View().IsValid());
        EXPECT_EQ(BoolOf(reply.Result(), "paused"), false);
    }

    TEST_F(ControlBuiltinsReplyTest, StepAnswersWithTheClockAfterItsLastFrame) {
        service.SetManualClock(true);
        String text;
        service.Post(MakeRequestText(5, "step", R"({"frames":3,"dt":0.25})"), [&text](const String& reply) { text = reply; });
        service.Drain();
        while (service.IsStepDue()) {
            CoreManager::GetTime().Advance(service.GetStepSeconds());
            service.Drain();
            service.EndFrame();
        }

        EXPECT_EQ(text, OkText("5", ClockText()));
        const ControlReply reply(text);
        EXPECT_EQ(IntOf(reply.Result(), "frame"), 5);
        EXPECT_DOUBLE_EQ(NumberOf(reply.Result(), "totalTime"), 2 * kFrameSeconds + 3 * 0.25);
    }

    TEST_F(ControlBuiltinsReplyTest, AStepSecondsPastADoubleIsInvalidParamsNotAParseError) {
        service.SetManualClock(true);
        String text;
        service.Post(R"({"id":6,"method":"step","params":{"dt":1e999}})", [&text](const String& reply) { text = reply; });
        service.Drain();

        const ControlReply reply(text);
        ASSERT_TRUE(reply.View().IsValid());
        EXPECT_EQ(reply.ErrorCode(), "invalid_params");
        EXPECT_FALSE(service.IsStepDue());
    }

    TEST_F(ControlBuiltinsReplyTest, PauseAndResumeAreWrittenAsTheirPinnedText) {
        EXPECT_EQ(Ask(1, "pause"), String(R"({"id":1,"ok":true,"result":{"paused":true}})"));
        EXPECT_TRUE(pause.IsPaused());
        EXPECT_EQ(Ask(2, "resume"), String(R"({"id":2,"ok":true,"result":{"paused":false}})"));
        EXPECT_FALSE(pause.IsPaused());
    }

    TEST_F(ControlBuiltinsReplyTest, ListMethodsWritesTheDescriptionsOfTheTableSortedByName) {
        ControlService small;
        small.RegisterHandler("zzz_last", [](ControlCall& call) { call.ReplyEmpty(); });
        small.RegisterHandler("aaa_first", [](ControlCall& call) { call.ReplyEmpty(); });
        small.DescribeMethod("aaa_first", {"First method", {{"count", "integer", true, "How many"}, {"flag", "boolean", false, "Whether"}}});
        small.DescribeMethod("zzz_last", {"Last method", {}});
        RegisterControlBuiltins(small);
        String text;
        small.Post(MakeRequestText(1, "list_methods"), [&text](const String& reply) { text = reply; });
        small.Drain();

        const ControlReply reply(text);
        const JsonView methods = MemberOf(reply.Result(), "methods");
        ASSERT_TRUE(methods.IsArray());
        ASSERT_GE(methods.Size(), 2u);
        constexpr StringView expectedFirst = R"({"name":"aaa_first","params":[{"description":"How many","name":"count","required":true,"type":"integer"},)"
                                             R"({"description":"Whether","name":"flag","required":false,"type":"boolean"}],"summary":"First method"})";
        constexpr StringView expectedLast = R"({"name":"zzz_last","params":[],"summary":"Last method"})";
        EXPECT_EQ(ItemAt(methods, 0).Raw(), expectedFirst);
        EXPECT_EQ(ItemAt(methods, methods.Size() - 1).Raw(), expectedLast);
    }

    TEST_F(ControlBuiltinsReplyTest, TheWritersAnswerInsideABatchAsTheirValues) {
        const ControlReply reply(Ask(1, "batch", R"({"calls":[{"method":"ping"}]})"));

        const JsonView results = MemberOf(reply.Result(), "results");
        ASSERT_TRUE(results.IsArray());
        ASSERT_EQ(results.Size(), 1u);
        const JsonView first = ItemAt(results, 0);
        EXPECT_EQ(BoolOf(first, "ok"), true);
        EXPECT_EQ(BoolOf(MemberOf(first, "result"), "pong"), true);
    }

    TEST_F(ControlBuiltinsReplyTest, WritingAStatusReplyAllocatesNothingBeyondTheCallItself) {
        service.RegisterHandler("empty", [](ControlCall& call) {
            call.Reply([](JsonWriter& writer) {
                writer.BeginObject();
                writer.EndObject();
            });
        });
        // Post() parses the request and Drain() makes the call object, and both allocate whatever the handler is: only the writing of the answer is under test. So the
        // Drain() of `status` is compared with the Drain() of a method that answers `{}`, after a first round that grew the reply buffer
        const auto drained = [this](StringView method) {
            service.Post(MakeRequestText(1, method), [](const String&) {});
            return CountAllocations([this] { service.Drain(); });
        };
        drained("status");
        drained("empty");

        const AllocCount status = drained("status");
        const AllocCount empty = drained("empty");

        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        EXPECT_EQ(status.count, empty.count);
        EXPECT_EQ(status.bytes, empty.bytes);
    }

    TEST_F(ControlBuiltinsReplyTest, ListMethodsCopiesTheDescriptionsToTheArenaNotTheHeap) {
        service.RegisterHandler("empty", [](ControlCall& call) {
            call.Reply([](JsonWriter& writer) {
                writer.BeginObject();
                writer.EndObject();
            });
        });
        service.DescribeMethod("empty", {"A summary that is longer than the small string buffer", {{"a_long_parameter_name", "integer", true, "A description that is longer than the buffer"}}});
        const auto drained = [this](StringView method) {
            service.Post(MakeRequestText(1, method), [](const String&) {});
            return CountAllocations([this] { service.Drain(); });
        };
        drained("list_methods");
        drained("empty");

        const AllocCount listing = drained("list_methods");
        const AllocCount empty = drained("empty");

        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        EXPECT_EQ(listing.count, empty.count);
        EXPECT_EQ(listing.bytes, empty.bytes);
    }

}  // namespace BECore::Tests
