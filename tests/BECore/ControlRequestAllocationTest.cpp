// The request path of ControlService (tape on the posting thread, recycled buffers, the reply into the service's scratch) allocates nothing once a message
// of its shape has been through: counted per thread, so only macos-profile checks it (kNoAllocTracker).

#include <BECore/Control/ControlActivity.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ControlTouches.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/Wait.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kRounds = 4;  // the first round grows the buffers and the free list, the rest are measured

        class ControlRequestAllocationTest : public ::testing::Test {
        protected:
            void SetUp() override {
                RegisterControlBuiltins(service);
            }

            /// A request of each shape: an id, a method, and `params` with a member the handler reads
            static String Ping() {
                return MakeRequestText(1, "ping");
            }

            static String LogSince() {
                return MakeRequestText(2, "log_since", R"({"level":"error","mark":0})");
            }

            ControlService service;
            size_t replyBytes = 0;  // what the reply function keeps: a number, so the function allocates nothing itself
        };

        TEST_F(ControlRequestAllocationTest, PostAndDrainOfAKnownShapeAllocateNothing) {
            if constexpr (!AllocTracker::kEnabled) {
                GTEST_SKIP() << kNoAllocTracker;
            }
            const IntrusivePtr<ControlClient> client = service.OpenConnectionClient();
            for (const IntrusivePtr<ControlClient>& asker : {IntrusivePtr<ControlClient>(), client}) {
                for (const String& request : {Ping(), LogSince()}) {
                    for (int round = 0; round < kRounds; ++round) {
                        const AllocCount counted = CountAllocations([&] {
                            service.Post(request, [this](const String& text) { replyBytes += text.size(); }, asker);
                            service.Drain();
                        });
                        if (round > 0) {
                            EXPECT_EQ(counted.count, 0u) << "round " << round << " of " << request.c_str() << (asker ? " with a client" : "");
                        }
                    }
                }
            }
            EXPECT_GT(replyBytes, 0u);
        }

        TEST_F(ControlRequestAllocationTest, ARequestThatReportsATouchAllocatesNothingOnceTheRecordsWentRound) {
            if constexpr (!AllocTracker::kEnabled) {
                GTEST_SKIP() << kNoAllocTracker;
            }
            service.RegisterHandler("touch", [this](ControlCall& call) {
                // Names longer than a string keeps inside itself, so every slot of the record has to have grown a buffer
                service.ReportTouch(1, "AComponentWithANameLongerThanTheInlineBuffer", "aFieldWithANameLongerThanTheInlineBuffer");
                call.ReplyEmpty();
            });
            const IntrusivePtr<ControlClient> client = service.OpenLocalClient("tool");
            const String request = MakeRequestText(3, "touch");
            const auto send = [&] {
                service.Post(request, [this](const String& text) { replyBytes += text.size(); }, client);
                service.Drain();
            };
            for (size_t i = 0; i < std::max(ControlTouches::kCapacity, ControlActivity::kCapacity) + 1; ++i) {
                send();
            }

            const AllocCount counted = CountAllocations(send);

            EXPECT_EQ(counted.count, 0u);
        }

        TEST_F(ControlRequestAllocationTest, PostingThreadAllocatesNothingWhileTheMainThreadGivesTheBuffersBack) {
            if constexpr (!AllocTracker::kEnabled) {
                GTEST_SKIP() << kNoAllocTracker;
            }
            if constexpr (!Thread::kAvailable) {
                GTEST_SKIP() << "no threads in this build";
            }
            const String request = LogSince();
            std::atomic<int> posted = 0;
            std::atomic<int> drained = 0;
            uint64_t postAllocations[kRounds] = {};

            Thread poster("ControlAllocPoster", [&] {
                for (int round = 0; round < kRounds; ++round) {
                    postAllocations[round] = CountAllocations([&] { service.Post(request, [this](const String& text) { replyBytes += text.size(); }); }).count;
                    posted.store(round + 1);
                    WaitUntil([&] { return drained.load() > round; });
                }
            });
            for (int round = 0; round < kRounds; ++round) {
                ASSERT_TRUE(WaitUntil([&] { return posted.load() > round; }));
                service.Drain();
                drained.store(round + 1);
            }
            poster.join();

            for (int round = 1; round < kRounds; ++round) {
                EXPECT_EQ(postAllocations[round], 0u) << "round " << round;
            }
        }

    }  // namespace

}  // namespace BECore::Tests
