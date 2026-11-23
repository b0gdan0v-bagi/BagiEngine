// The text of the small built-in answers (`ping`, `status`, `step`, `pause`, `resume`): written with JsonWriter, and the bytes the `Json` DOM wrote before it
// (the references below are built the old way, so a drift of a key or of a number's text fails here).

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

        /// The `window` object of `status` for a service with no main window (the test environment has none)
        Json DomWindowWithoutWindow() {
            Json insets = Json::object();
            insets["top"] = 0.0;
            insets["right"] = 0.0;
            insets["bottom"] = 0.0;
            insets["left"] = 0.0;
            Json window = Json::object();
            window["width"] = 0;
            window["height"] = 0;
            window["density"] = 1.0;
            window["displayScale"] = 1.0;
            window["safeArea"] = std::move(insets);
            window["outputWidth"] = 0;
            window["outputHeight"] = 0;
            window["userScale"] = CoreManager::GetUIEnvironment().GetSettings().userScale;
            window["headless"] = false;
            return window;
        }

        Json DomStatus(bool manualClock, bool paused) {
            Json totals = Json::object();
            totals["warnings"] = 0;
            totals["errors"] = 0;
            Json result = DomClock();
            result["log"] = std::move(totals);
            result["manualClock"] = manualClock;
            result["paused"] = paused;
            result["window"] = DomWindowWithoutWindow();
            return result;
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
            String Ask(int64_t id, StringView method, const Json& params = Json()) {
                String text;
                service.Post(MakeRequest(id, method, params), [&text](const String& reply) { text = reply; });
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

    TEST_F(ControlBuiltinsReplyTest, StatusIsTheDomPathsTextOnTheRealClockAndOnTheManualOne) {
        if (CoreManager::GetMainWindow()) {
            GTEST_SKIP() << "the reference describes a service with no main window";
        }

        EXPECT_EQ(Ask(1, "status"), MakeOkResponse(Json(1), DomStatus(false, false)));

        pause.SetPaused(true);
        EXPECT_EQ(Ask(2, "status"), MakeOkResponse(Json(2), DomStatus(false, true)));

        service.SetManualClock(true);
        EXPECT_EQ(Ask(3, "status"), MakeOkResponse(Json(3), DomStatus(true, true)));
    }

    TEST_F(ControlBuiltinsReplyTest, StatusWithoutAPauseReportsNotPaused) {
        service.SetPause(nullptr);

        const auto parsed = ParseJson(Ask(1, "status"));

        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(BoolOf(FindJsonMember(*parsed, "result"), "paused"), false);
    }

    TEST_F(ControlBuiltinsReplyTest, StepAnswersWithTheClockAfterItsLastFrame) {
        service.SetManualClock(true);
        Json params = Json::object();
        params["frames"] = 3;
        params["dt"] = 0.25;
        String text;
        service.Post(MakeRequest(5, "step", params), [&text](const String& reply) { text = reply; });
        service.Drain();
        while (service.IsStepDue()) {
            CoreManager::GetTime().Advance(service.GetStepSeconds());
            service.Drain();
            service.EndFrame();
        }

        EXPECT_EQ(text, MakeOkResponse(Json(5), DomClock()));
        const auto parsed = ParseJson(text);
        ASSERT_TRUE(parsed.has_value());
        const Json* result = FindJsonMember(*parsed, "result");
        EXPECT_EQ(GetJsonInt(Require(result), "frame").value_or(0), 5);
        EXPECT_DOUBLE_EQ(NumberOf(result, "totalTime"), 2 * kFrameSeconds + 3 * 0.25);
    }

    TEST_F(ControlBuiltinsReplyTest, PauseAndResumeAreWrittenAsTheirPinnedText) {
        EXPECT_EQ(Ask(1, "pause"), String(R"({"id":1,"ok":true,"result":{"paused":true}})"));
        EXPECT_TRUE(pause.IsPaused());
        EXPECT_EQ(Ask(2, "resume"), String(R"({"id":2,"ok":true,"result":{"paused":false}})"));
        EXPECT_FALSE(pause.IsPaused());
    }

    TEST_F(ControlBuiltinsReplyTest, ListMethodsWritesTheDescriptionsOfTheTableSortedByName) {
        ControlService small;
        small.RegisterHandler("zzz_last", [](ControlCall& call) { call.Reply(Json::object()); });
        small.RegisterHandler("aaa_first", [](ControlCall& call) { call.Reply(Json::object()); });
        small.DescribeMethod("aaa_first", {"First method", {{"count", "integer", true, "How many"}, {"flag", "boolean", false, "Whether"}}});
        small.DescribeMethod("zzz_last", {"Last method", {}});
        RegisterControlBuiltins(small);
        String text;
        small.Post(MakeRequest(1, "list_methods"), [&text](const String& reply) { text = reply; });
        small.Drain();

        const auto parsed = ParseJson(text);
        ASSERT_TRUE(parsed.has_value());
        const Json* methods = FindJsonMember(Require(FindJsonMember(*parsed, "result")), "methods");
        ASSERT_NE(methods, nullptr);
        Json expectedFirst = Json::object();
        expectedFirst["name"] = "aaa_first";
        expectedFirst["summary"] = "First method";
        Json count = Json::object();
        count["name"] = "count";
        count["type"] = "integer";
        count["required"] = true;
        count["description"] = "How many";
        Json flag = Json::object();
        flag["name"] = "flag";
        flag["type"] = "boolean";
        flag["required"] = false;
        flag["description"] = "Whether";
        expectedFirst["params"] = Json::array({count, flag});
        Json expectedLast = Json::object();
        expectedLast["name"] = "zzz_last";
        expectedLast["summary"] = "Last method";
        expectedLast["params"] = Json::array();
        ASSERT_GE(methods->size(), 2u);
        EXPECT_EQ(DumpJson((*methods)[0]), DumpJson(expectedFirst));
        EXPECT_EQ(DumpJson(methods->back()), DumpJson(expectedLast));
    }

    TEST_F(ControlBuiltinsReplyTest, TheWritersAnswerInsideABatchAsTheirValues) {
        Json call = Json::object();
        call["method"] = "ping";
        Json params = Json::object();
        params["calls"] = Json::array({call});

        const auto parsed = ParseJson(Ask(1, "batch", params));

        ASSERT_TRUE(parsed.has_value());
        const Json* results = FindJsonMember(Require(FindJsonMember(*parsed, "result")), "results");
        ASSERT_NE(results, nullptr);
        ASSERT_EQ(results->size(), 1u);
        EXPECT_EQ(BoolOf(&(*results)[0], "ok"), true);
        EXPECT_EQ(BoolOf(FindJsonMember((*results)[0], "result"), "pong"), true);
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
            service.Post(MakeRequest(1, method), [](const String&) {});
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

}  // namespace BECore::Tests
