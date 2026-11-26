// The journal of the control API (ControlService::Activity()): what is listed, in which order, and what it keeps of an entry.

#include <BECore/Control/ControlActivity.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        class ControlActivityTest : public ::testing::Test {
        protected:
            void SetUp() override {
                RegisterControlBuiltins(service);
                service.RegisterHandler("act", [](ControlCall& call) { call.ReplyEmpty(); });
                service.DescribeMethod("act", {"Acts", {}});
                service.RegisterHandler("refused", [](ControlCall& call) { call.Fail("not_now", "no"); });
                service.RegisterHandler("later", [](ControlCall& call) { call.DeferFrames(2, [](ControlCall& done) { done.ReplyEmpty(); }); });
                service.RegisterHandler("look", [](ControlCall& call) { call.ReplyEmpty(); });
                service.DescribeMethod("look", {"Reads", {}, /*quiet=*/true});
            }

            void Send(StringView method, StringView params = {}, const IntrusivePtr<ControlClient>& client = {}) {
                service.Post(MakeRequestText(1, method, params), [](const String&) {}, client);
                service.Drain();
            }

            const ControlActivityEntry& Entry(size_t index) const {
                return Require(index < service.Activity().Size() ? &service.Activity().At(index) : nullptr);
            }

            /// The methods of the calls listed, oldest first
            Vector<String> ListedMethods() const {
                Vector<String> methods;
                for (size_t i = 0; i < service.Activity().Size(); ++i) {
                    methods.push_back(service.Activity().At(i).method);
                }
                return methods;
            }

            ControlService service;
        };

    }  // namespace

    TEST_F(ControlActivityTest, AConnectionItsNameAndItsEndAreListedInOrder) {
        const IntrusivePtr<ControlClient> client = service.OpenConnectionClient();
        Send("client_hello", R"({"name":"first"})", client);
        Send("client_hello", R"({"name":"second"})", client);
        service.CloseConnectionClient(client);
        service.Drain();

        ASSERT_EQ(service.Activity().Size(), 4u);
        EXPECT_EQ(Entry(0).kind, ControlActivityKind::Connected);
        EXPECT_EQ(Entry(1).kind, ControlActivityKind::Named);
        EXPECT_EQ(Entry(1).text, "first");
        EXPECT_EQ(Entry(2).kind, ControlActivityKind::Named);
        EXPECT_EQ(Entry(2).text, "second");
        EXPECT_EQ(Entry(3).kind, ControlActivityKind::Disconnected);
        for (size_t i = 0; i < service.Activity().Size(); ++i) {
            EXPECT_EQ(Entry(i).client.Get(), client.Get()) << i;
            EXPECT_EQ(Entry(i).sequence, i + 1) << i;
        }
        EXPECT_EQ(service.Activity().GetNextSequence(), 5u);
    }

    TEST_F(ControlActivityTest, ACallIsListedWithWhoAskedWhatAndHowItEnded) {
        const IntrusivePtr<ControlClient> client = service.OpenLocalClient("tool");
        const uint64_t before = service.Activity().GetVersion();

        Send("act", R"({"x":1})", client);
        const uint64_t afterFirst = service.Activity().GetVersion();
        Send("refused");

        EXPECT_NE(afterFirst, before);
        EXPECT_NE(service.Activity().GetVersion(), afterFirst);
        ASSERT_EQ(service.Activity().Size(), 2u);
        EXPECT_EQ(Entry(0).kind, ControlActivityKind::Call);
        EXPECT_EQ(Entry(0).client.Get(), client.Get());
        EXPECT_EQ(Entry(0).method, "act");
        EXPECT_EQ(Entry(0).text, R"({"x":1})");
        EXPECT_EQ(Entry(0).error, "");
        EXPECT_FALSE(StringView(Entry(0).time).empty());
        EXPECT_FALSE(Entry(1).client);
        EXPECT_EQ(Entry(1).method, "refused");  // a method nobody described is listed
        EXPECT_EQ(Entry(1).error, "not_now");
    }

    TEST_F(ControlActivityTest, AReplyThatCannotBeWrittenIsListedAsAnError) {
        service.RegisterHandler("broken",
                                [](ControlCall& call) { call.TryReply([](JsonWriter&) -> std::expected<void, ControlError> { return std::unexpected(MakeControlError("half_way", "no")); }); });

        Send("broken");
        Send("batch", R"({"calls":[{"method":"broken"}]})");

        ASSERT_EQ(service.Activity().Size(), 2u);
        EXPECT_EQ(Entry(0).error, "half_way");
        EXPECT_EQ(Entry(1).error, "half_way");
    }

    TEST_F(ControlActivityTest, ReadsAndRequestsThatRunNoMethodAreNotListed) {
        const uint64_t before = service.Activity().GetVersion();

        for (const StringView method : {"look", "ping", "status", "list_methods", "log_mark", "no_such_method"}) {
            Send(method);
        }
        service.Post("not json", [](const String&) {});
        service.Drain();

        EXPECT_EQ(service.Activity().Size(), 0u);
        EXPECT_EQ(service.Activity().GetVersion(), before);
    }

    TEST_F(ControlActivityTest, TheCallsOfABatchAreListedUnderItsClientAndTheBatchIsNot) {
        const IntrusivePtr<ControlClient> client = service.OpenLocalClient("batcher");
        service.SetManualClock(true);

        Send("batch", R"({"calls":[{"method":"act","params":{"n":1}},{"method":"look"},{"method":"later"},{"method":"refused"}]})", client);
        EXPECT_EQ(ListedMethods(), Vector<String>{"act"});
        service.EndFrame();
        service.EndFrame();

        EXPECT_EQ(ListedMethods(), (Vector<String>{"act", "later", "refused"}));
        for (size_t i = 0; i < service.Activity().Size(); ++i) {
            EXPECT_EQ(Entry(i).client.Get(), client.Get()) << i;
        }
        EXPECT_EQ(Entry(0).text, R"({"n":1})");
        EXPECT_EQ(Entry(2).error, "not_now");
    }

    TEST_F(ControlActivityTest, ADeferredCallIsListedWhenItIsAnswered) {
        Send("later", R"({"n":2})");
        service.EndFrame();
        EXPECT_EQ(service.Activity().Size(), 0u);

        service.EndFrame();

        ASSERT_EQ(service.Activity().Size(), 1u);
        EXPECT_EQ(Entry(0).method, "later");
        EXPECT_EQ(Entry(0).text, R"({"n":2})");
    }

    TEST_F(ControlActivityTest, WhatShutdownAnswersIsNotListed) {
        Send("later");
        Send("act");  // held by the barrier

        service.Shutdown();

        EXPECT_EQ(service.Activity().Size(), 0u);
    }

    TEST_F(ControlActivityTest, LongParamsAreCutBetweenCharactersAndKeptOnOneLine) {
        String params = "{\n\t\"tt\":\"";  // an odd number of bytes before characters of two, so the limit falls inside one
        const size_t head = params.size();
        while (params.size() < 2 * ControlActivity::kMaxTextBytes) {
            params += "\xD0\x90";  // U+0410
        }
        params += "\"}";
        const String request = Format("{{\"id\":1,\"method\":\"act\",\"params\":{}}}", params);

        service.Post(request, [](const String&) {});
        service.Drain();

        ASSERT_EQ(service.Activity().Size(), 1u);
        const String& text = Entry(0).text;
        ASSERT_LE(text.size(), ControlActivity::kMaxTextBytes);
        ASSERT_GE(text.size(), ControlActivity::kMaxTextBytes - 1);
        EXPECT_EQ((text.size() - head) % 2, 0u) << "a character was cut in half";
        String expected = params.substr(0, text.size());
        expected[1] = ' ';
        expected[2] = ' ';
        EXPECT_EQ(text, expected);
    }

    TEST_F(ControlActivityTest, AFullJournalDropsItsOldestEntries) {
        constexpr size_t kExtra = 3;
        for (size_t i = 0; i < ControlActivity::kCapacity + kExtra; ++i) {
            Send("act", Format("{{\"n\":{}}}", i));
        }

        ASSERT_EQ(service.Activity().Size(), ControlActivity::kCapacity);
        for (size_t i = 0; i < ControlActivity::kCapacity; ++i) {
            ASSERT_EQ(Entry(i).sequence, i + kExtra + 1) << i;
            ASSERT_EQ(Entry(i).text, Format("{{\"n\":{}}}", i + kExtra)) << i;
        }
    }

    TEST_F(ControlActivityTest, AnEntryThatTakesAnOldSlotKeepsNoNodeOfIt) {
        service.RegisterHandler("touch", [this](ControlCall& call) {
            service.ReportTouch(5);
            call.ReplyEmpty();
        });
        const IntrusivePtr<ControlClient> client = service.OpenLocalClient("tool");
        for (size_t i = 0; i < ControlActivity::kCapacity; ++i) {
            Send("touch", {}, client);
        }

        Send("act", {}, client);

        EXPECT_EQ(Entry(ControlActivity::kCapacity - 2).node, 5u);
        EXPECT_EQ(Entry(ControlActivity::kCapacity - 1).node, 0u);
    }

    TEST_F(ControlActivityTest, ClearGivesTheClientsBackAndKeepsTheSequence) {
        const IntrusivePtr<ControlClient> client = service.OpenLocalClient("tool");
        Send("act", {}, client);
        const uint64_t version = service.Activity().GetVersion();

        service.Activity().Clear();

        EXPECT_EQ(service.Activity().Size(), 0u);
        EXPECT_NE(service.Activity().GetVersion(), version);
        EXPECT_EQ(client->GetRefCount(), 1);

        Send("refused");
        ASSERT_EQ(service.Activity().Size(), 1u);
        EXPECT_EQ(Entry(0).sequence, 2u);
        EXPECT_EQ(Entry(0).method, "refused");
    }

    TEST_F(ControlActivityTest, RecordingIntoAJournalThatHasGoneRoundAllocatesNothing) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const IntrusivePtr<ControlClient> client = service.OpenLocalClient("tool");
        // Longer than a string keeps inside itself, so every slot has to have grown a buffer
        const String request = MakeRequestText(1, "act", R"({"name":"a value long enough to need a buffer of its own"})");
        const auto send = [&] {
            service.Post(request, [](const String&) {}, client);
            service.Drain();
        };
        for (size_t i = 0; i < ControlActivity::kCapacity + 1; ++i) {
            send();
        }

        const AllocCount counted = CountAllocations(send);

        EXPECT_EQ(counted.count, 0u);
    }

}  // namespace BECore::Tests
