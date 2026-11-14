#include <Common/Printers.h>
#include <Events/EventChannel.h>

namespace BECore::Tests {

    // The facts of EventDispatchSemanticsTest, driven against the channel without EventBase.

    namespace {
        struct Probe {
            int id = 0;
            Vector<int>* log = nullptr;
            EventChannel* channel = nullptr;
            Probe* other = nullptr;
            EventConnection* extra = nullptr;
            bool nested = false;
        };

        // Different bodies on purpose: identical functions may be folded by a linker, which would merge two pairs.
        void LogId(void* instance, const void*) {
            auto* probe = static_cast<Probe*>(instance);
            probe->log->push_back(probe->id);
        }

        void LogIdPlus100(void* instance, const void*) {
            auto* probe = static_cast<Probe*>(instance);
            probe->log->push_back(probe->id + 100);
        }

        void LogEventValue(void* instance, const void* event) {
            auto* probe = static_cast<Probe*>(instance);
            probe->log->push_back(*static_cast<const int*>(event));
        }

        void ConnectOtherThenLog(void* instance, const void*) {
            auto* probe = static_cast<Probe*>(instance);
            probe->log->push_back(probe->id);
            if (probe->extra != nullptr) {
                *probe->extra = probe->channel->Connect(probe->other, &LogId);
                probe->extra = nullptr;
            }
        }

        void DisconnectOtherThenLog(void* instance, const void*) {
            auto* probe = static_cast<Probe*>(instance);
            probe->log->push_back(probe->id);
            probe->channel->Disconnect(probe->other, &LogId);
        }

        void DisconnectSelfThenLog(void* instance, const void*) {
            auto* probe = static_cast<Probe*>(instance);
            probe->log->push_back(probe->id);
            probe->channel->Disconnect(probe, &DisconnectSelfThenLog);
        }

        void PublishNestedOnce(void* instance, const void* event) {
            auto* probe = static_cast<Probe*>(instance);
            probe->log->push_back(probe->id);
            if (probe->nested) {
                return;
            }
            probe->nested = true;
            probe->channel->Disconnect(probe->other, &LogId);
            probe->channel->Publish(event);
            probe->log->push_back(probe->id + 100);
        }

        // Probes with ids 1..3 on one channel, all connected with LogId in the order of the ids.
        struct ThreeProbes {
            Vector<int> log;
            EventChannel channel;
            Probe a{1, &log, &channel};
            Probe b{2, &log, &channel};
            Probe c{3, &log, &channel};
            EventConnection ca = channel.Connect(&a, &LogId);
            EventConnection cb = channel.Connect(&b, &LogId);
            EventConnection cc = channel.Connect(&c, &LogId);
        };
    }  // namespace

    static_assert(!std::is_copy_constructible_v<EventConnection>);
    static_assert(std::is_nothrow_move_constructible_v<EventConnection>);
    static_assert(!std::is_copy_constructible_v<EventChannel>);

    TEST(EventChannelTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts above are the check";
    }

    TEST(EventChannelTest, PublishCallsFromTheLastConnectedToTheFirst) {
        ThreeProbes t;

        t.channel.Publish(nullptr);

        EXPECT_EQ(t.log, (Vector<int>{3, 2, 1}));
    }

    TEST(EventChannelTest, PublishPassesTheEventAndTheInstanceThrough) {
        Vector<int> log;
        EventChannel channel;
        Probe probe;
        probe.log = &log;
        EventConnection connection = channel.Connect(&probe, &LogEventValue);

        int event = 42;
        channel.Publish(&event);

        EXPECT_EQ(log, (Vector<int>{42}));
    }

    TEST(EventChannelTest, ConnectingTheSamePairAgainCallsItOnceAndMovesItLast) {
        ThreeProbes t;

        EventConnection again = t.channel.Connect(&t.a, &LogId);
        t.channel.Publish(nullptr);

        // [a, b, c] -> a leaves, c takes its slot -> [c, b], a is appended -> [c, b, a]; delivered a, b, c.
        EXPECT_EQ(t.log, (Vector<int>{1, 2, 3}));
    }

    TEST(EventChannelTest, ADisconnectMovesTheLastEntryIntoTheFreedSlot) {
        ThreeProbes t;

        t.channel.Disconnect(&t.a, &LogId);
        t.channel.Publish(nullptr);

        // [a, b, c] -> [c, b]: delivered b, c, not c, b as a shifted tail would give.
        EXPECT_EQ(t.log, (Vector<int>{2, 3}));
    }

    TEST(EventChannelTest, DisconnectIsByThePairNotByTheInvokerOrTheInstanceAlone) {
        ThreeProbes t;
        EventConnection second = t.channel.Connect(&t.a, &LogIdPlus100);

        t.channel.Disconnect(&t.a, &LogId);
        t.channel.Publish(nullptr);

        EXPECT_EQ(t.log, (Vector<int>{3, 2, 101})) << "the other handler of a stays, the same handler of b and c too";
    }

    TEST(EventChannelTest, DisconnectingAnAbsentPairChangesNothing) {
        ThreeProbes t;
        Probe stranger;

        t.channel.Disconnect(&stranger, &LogId);
        t.channel.Disconnect(&t.a, &LogIdPlus100);
        t.channel.Publish(nullptr);

        EXPECT_EQ(t.log, (Vector<int>{3, 2, 1}));
    }

    TEST(EventChannelTest, DisconnectInstanceRemovesEveryHandlerOfThatInstanceOnly) {
        ThreeProbes t;
        EventConnection second = t.channel.Connect(&t.b, &LogIdPlus100);

        t.channel.DisconnectInstance(&t.b);
        t.channel.Publish(nullptr);

        EXPECT_EQ(t.log.size(), 2u);
        EXPECT_NE(eastl::find(t.log.begin(), t.log.end(), 1), t.log.end());
        EXPECT_NE(eastl::find(t.log.begin(), t.log.end(), 3), t.log.end());
    }

    TEST(EventChannelTest, ADestroyedConnectionDisconnectsItsPair) {
        ThreeProbes t;
        {
            Probe extra;
            extra.id = 9;
            extra.log = &t.log;
            EventConnection scoped = t.channel.Connect(&extra, &LogId);
            t.channel.Publish(nullptr);
            EXPECT_EQ(t.log, (Vector<int>{9, 3, 2, 1}));
            t.log.clear();
        }

        t.channel.Publish(nullptr);

        EXPECT_EQ(t.log, (Vector<int>{3, 2, 1}));
    }

    TEST(EventChannelTest, DroppingTheOlderConnectionOfADoubledPairRemovesTheListener) {
        ThreeProbes t;
        EventConnection older = t.channel.Connect(&t.a, &LogIdPlus100);
        EventConnection newer = t.channel.Connect(&t.a, &LogIdPlus100);

        older.Reset();
        t.channel.Publish(nullptr);

        EXPECT_EQ(t.log.size(), 3u) << "the listener of the pair is gone although `newer` is alive";
        newer.Reset();
    }

    TEST(EventChannelTest, ResetAndTheDestructorOfAnAlreadyRemovedPairDoNothing) {
        ThreeProbes t;
        EventConnection connection = t.channel.Connect(&t.a, &LogIdPlus100);
        t.channel.Disconnect(&t.a, &LogIdPlus100);

        connection.Reset();
        connection.Reset();
        t.channel.Publish(nullptr);

        EXPECT_EQ(t.log, (Vector<int>{3, 2, 1}));
    }

    TEST(EventChannelTest, AMovedConnectionIsTheOnlyOneThatDisconnects) {
        ThreeProbes t;
        {
            EventConnection from = t.channel.Connect(&t.a, &LogIdPlus100);
            EventConnection to(std::move(from));
            from.Reset();
            t.channel.Publish(nullptr);
            EXPECT_EQ(t.log.size(), 4u) << "resetting the moved-from one leaves the listener";
            t.log.clear();
        }

        t.channel.Publish(nullptr);

        EXPECT_EQ(t.log, (Vector<int>{3, 2, 1})) << "the destruction of the moved-to one removes it";
    }

    TEST(EventChannelTest, MoveAssignmentDisconnectsWhatTheTargetHeld) {
        ThreeProbes t;
        EventConnection target = t.channel.Connect(&t.a, &LogIdPlus100);
        EventConnection source = t.channel.Connect(&t.b, &LogIdPlus100);

        target = std::move(source);
        t.channel.Publish(nullptr);

        EXPECT_EQ(t.log.size(), 4u) << "a's second handler is gone, b's stays";
        EXPECT_EQ(eastl::count(t.log.begin(), t.log.end(), 101), 0);
        EXPECT_EQ(eastl::count(t.log.begin(), t.log.end(), 102), 1);
    }

    TEST(EventChannelTest, AListenerConnectedByAHandlerIsNotCalledInThatPublish) {
        ThreeProbes t;
        Probe late;
        late.id = 9;
        late.log = &t.log;
        EventConnection lateConnection;
        EventConnection starter = t.channel.Connect(&t.b, &ConnectOtherThenLog);
        t.b.other = &late;
        t.b.extra = &lateConnection;

        t.channel.Publish(nullptr);
        EXPECT_EQ(eastl::count(t.log.begin(), t.log.end(), 9), 0);

        t.log.clear();
        t.channel.Publish(nullptr);
        EXPECT_EQ(eastl::count(t.log.begin(), t.log.end(), 9), 1);
    }

    TEST(EventChannelTest, ADisconnectInsideAPublishSkipsTheRemovedAndKeepsTheOthers) {
        ThreeProbes t;
        EventConnection remover = t.channel.Connect(&t.c, &DisconnectOtherThenLog);
        t.c.other = &t.a;

        t.channel.Publish(nullptr);

        // remover and c's LogId run first, b follows; a was removed before its turn.
        EXPECT_EQ(t.log, (Vector<int>{3, 3, 2}));

        t.log.clear();
        t.channel.Publish(nullptr);
        EXPECT_EQ(t.log, (Vector<int>{3, 3, 2})) << "the list is compacted: a is gone for good";
    }

    TEST(EventChannelTest, ADisconnectOfTheRunningListenerInsideAPublishIsHarmless) {
        ThreeProbes t;
        EventConnection self = t.channel.Connect(&t.b, &DisconnectSelfThenLog);

        t.channel.Publish(nullptr);
        EXPECT_EQ(t.log, (Vector<int>{2, 3, 2, 1}));

        t.log.clear();
        t.channel.Publish(nullptr);
        EXPECT_EQ(t.log, (Vector<int>{3, 2, 1}));
    }

    TEST(EventChannelTest, ANestedPublishDoesNotCompactTheListUnderTheOuterOne) {
        ThreeProbes t;
        Probe nester;
        nester.id = 7;
        nester.log = &t.log;
        nester.channel = &t.channel;
        nester.other = &t.a;
        EventConnection connection = t.channel.Connect(&nester, &PublishNestedOnce);

        t.channel.Publish(nullptr);

        // The outer pass runs the nester, which removes a and publishes again: the inner pass skips a and
        // leaves the list as it is, the outer one goes on over the same slots and skips a too.
        EXPECT_EQ(t.log, (Vector<int>{7, 7, 3, 2, 107, 3, 2}));

        t.log.clear();
        t.channel.Publish(nullptr);
        EXPECT_EQ(t.log, (Vector<int>{7, 3, 2}));
    }

}  // namespace BECore::Tests
