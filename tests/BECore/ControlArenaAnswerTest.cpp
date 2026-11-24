// An answer struct whose strings and lists are on the thread's scratch arena: the service gives the arena back with the reply, whatever made the struct
// (a handler, the `done` of a deferred call, a call inside `batch`, a push), and the second reply of the same shape allocates nothing.

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/ControlArenaAnswerTypes.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Memory/ScratchArena.h>
#include <BECore/Time/Time.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Generated/ControlArenaAnswerTypes.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using namespace ControlArenaTestData;

        constexpr StringView kText = "a text that is longer than the small string buffer";
        constexpr StringView kNote = "a second text that does not fit the buffer either";
        constexpr StringView kRowLabel = "the label of one row, also longer than the buffer";
        constexpr size_t kRowCount = 3;

        ArenaString ToArena(StringView text) {
            return ArenaString(text.data(), text.size());
        }

        ArenaAnswer MakeArenaAnswer() {
            ArenaAnswer answer;
            answer.text = ToArena(kText);
            answer.note = ToArena(kNote);
            answer.rows.reserve(kRowCount);
            for (size_t i = 0; i < kRowCount; ++i) {
                ArenaRow& row = answer.rows.emplace_back();
                row.label = ToArena(kRowLabel);
                row.rank = static_cast<int32_t>(i);
            }
            return answer;
        }

        HeapAnswer MakeHeapAnswer() {
            HeapAnswer answer;
            answer.text = String(kText);
            answer.note = String(kNote);
            for (size_t i = 0; i < kRowCount; ++i) {
                HeapRow& row = answer.rows.emplace_back();
                row.label = String(kRowLabel);
                row.rank = static_cast<int32_t>(i);
            }
            return answer;
        }

        size_t ScratchBytes() {
            return ScratchArena::Get().GetBytesUsed();
        }

        class ControlArenaAnswerTest : public ::testing::Test {
        protected:
            void SetUp() override {
                service.RegisterHandler("arena", [](ControlCall& call) {
                    const ArenaAnswer answer = MakeArenaAnswer();
                    call.ReplyObject(answer);
                });
                service.RegisterHandler("heap", [](ControlCall& call) {
                    const HeapAnswer answer = MakeHeapAnswer();
                    call.ReplyObject(answer);
                });
                service.RegisterHandler("arena_fail", [](ControlCall& call) {
                    const ArenaAnswer answer = MakeArenaAnswer();
                    call.Fail("boom", answer.text.c_str());
                });
                service.RegisterHandler("arena_deferred", [](ControlCall& call) {
                    call.DeferFrames(1, [](ControlCall& done) {
                        const ArenaAnswer answer = MakeArenaAnswer();
                        done.ReplyObject(answer);
                    });
                });
            }

            void TearDown() override {
                CoreManager::GetTime().Reset();
            }

            void Post(const String& text) {
                service.Post(text, [this](const String& reply) { replies.push_back(reply); });
            }

            /// The application loop of the manual clock: a frame runs while a step is due
            void RunDueFrames() {
                constexpr int kGuard = 1000;  // a loop that does not end would hang the test run
                for (int frames = 0; service.IsStepDue() && frames < kGuard; ++frames) {
                    CoreManager::GetTime().Advance(service.GetStepSeconds());
                    service.Drain();
                    service.EndFrame();
                }
            }

            ControlService service;
            Vector<String> replies;
        };

        TEST_F(ControlArenaAnswerTest, ReplyIsTheTextOfItsHeapTwin) {
            Post(MakeRequestText(1, "arena"));
            Post(MakeRequestText(1, "heap"));
            service.Drain();

            ASSERT_EQ(replies.size(), 2u);
            EXPECT_EQ(replies[0], replies[1]);
            EXPECT_NE(replies[0].find(kRowLabel.data()), String::npos);
        }

        TEST_F(ControlArenaAnswerTest, HandlerGivesItsArenaBackWithTheReply) {
            const size_t before = ScratchBytes();

            Post(MakeRequestText(1, "arena"));
            service.Drain();

            EXPECT_EQ(replies.size(), 1u);
            EXPECT_EQ(ScratchBytes(), before);
        }

        TEST_F(ControlArenaAnswerTest, FailedHandlerGivesItsArenaBack) {
            const size_t before = ScratchBytes();

            Post(MakeRequestText(1, "arena_fail"));
            Post(MakeRequestText(2, "no_such_method"));
            service.Drain();

            ASSERT_EQ(replies.size(), 2u);
            EXPECT_EQ(ControlReply(StringView(replies[0].c_str(), replies[0].size())).ErrorCode(), "boom");
            EXPECT_EQ(ScratchBytes(), before);
        }

        TEST_F(ControlArenaAnswerTest, DeferredDoneGivesItsArenaBack) {
            const size_t before = ScratchBytes();

            Post(MakeRequestText(1, "arena_deferred"));
            service.Drain();
            EXPECT_TRUE(replies.empty());
            service.EndFrame();

            ASSERT_EQ(replies.size(), 1u);
            EXPECT_EQ(ScratchBytes(), before);
            Post(MakeRequestText(2, "heap"));
            service.Drain();
            EXPECT_EQ(replies[0].substr(replies[0].find("result")), replies[1].substr(replies[1].find("result")));
        }

        TEST_F(ControlArenaAnswerTest, CallsOfABatchWithAStepInsideGiveTheirArenaBack) {
            service.SetManualClock(true);
            RegisterControlBuiltins(service);
            const size_t before = ScratchBytes();

            Post(MakeRequestText(1, "batch", R"({"calls":[{"method":"arena"},{"method":"step","params":{"frames":2}},{"method":"arena_deferred"},{"method":"arena"}]})"));
            service.Drain();
            RunDueFrames();

            ASSERT_EQ(replies.size(), 1u);
            EXPECT_EQ(ScratchBytes(), before);
            const ControlReply reply(StringView(replies[0].c_str(), replies[0].size()));
            ASSERT_TRUE(reply.View().IsValid());
            EXPECT_TRUE(reply.IsOk());
            EXPECT_NE(replies[0].find(kRowLabel.data()), String::npos);
        }

        TEST_F(ControlArenaAnswerTest, PushGivesItsArenaBack) {
            Vector<String> pushes;
            service.SetEventSink([&pushes](const String& text) { pushes.push_back(text); });
            const size_t before = ScratchBytes();

            service.Publish("changed", [](JsonWriter& writer) {
                const ArenaAnswer answer = MakeArenaAnswer();
                answer.WriteJson(writer);
            });

            ASSERT_EQ(pushes.size(), 1u);
            EXPECT_NE(pushes[0].find(kText.data()), String::npos);
            EXPECT_EQ(ScratchBytes(), before);
        }

        TEST_F(ControlArenaAnswerTest, ErrorAnsweredOnAPostingThreadLeavesItsScratchAsItWas) {
            if constexpr (!Thread::kAvailable) {
                GTEST_SKIP() << "no threads in this build";
            }
            service.Shutdown();  // after it Post answers on the posting thread
            size_t before = 0;
            size_t after = 0;
            String reply;

            Thread poster("ControlArenaPoster", [&] {
                before = ScratchBytes();
                service.Post("this is not JSON, and it is long enough to need the heap", [&reply](const String& text) { reply = text; });
                after = ScratchBytes();
            });
            poster.join();

            EXPECT_EQ(ControlReply(StringView(reply.c_str(), reply.size())).ErrorCode(), ControlErrors::kShuttingDown);
            EXPECT_EQ(after, before);
        }

        TEST_F(ControlArenaAnswerTest, SecondReplyOfTheSameShapeAllocatesNothing) {
            if constexpr (!AllocTracker::kEnabled) {
                GTEST_SKIP() << kNoAllocTracker;
            }
            Vector<AllocCount> counted;
            service.RegisterHandler("measured", [&counted](ControlCall& call) {
                counted.push_back(CountAllocations([&call] {
                    const ArenaAnswer answer = MakeArenaAnswer();
                    call.ReplyObject(answer);
                }));
            });
            counted.reserve(3);  // the vector is the test's, not the reply's

            size_t replyBytes = 0;  // the reply function of the measured calls keeps nothing, so only the service is counted
            for (int64_t id = 1; id <= 3; ++id) {
                service.Post(MakeRequestText(id, "measured"), [&replyBytes](const String& text) { replyBytes += text.size(); });
                service.Drain();
            }
            EXPECT_GT(replyBytes, 0u);

            ASSERT_EQ(counted.size(), 3u);
            EXPECT_EQ(counted[1].count, 0u);
            EXPECT_EQ(counted[2].count, 0u);
        }

    }  // namespace

}  // namespace BECore::Tests
