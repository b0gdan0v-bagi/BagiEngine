#include <Common/Printers.h>
#include <Events/EventBase.h>
#include <Events/EventsQueueRegistry.h>

namespace BECore::Tests {

    // The dispatcher behind EventBase<T> is held to these facts: order of delivery, identity of a subscription, the
    // snapshot rules of a dispatch and of Update(). The events are local to the file because the dispatchers are
    // process-wide statics; a test leaves no subscriber behind.

    namespace SemanticsEvents {
        struct PingEvent : EventBase<PingEvent> {};
        struct OtherEvent : EventBase<OtherEvent> {};
        struct NestedEvent : EventBase<NestedEvent> {};

        struct SumEvent : EventBase<SumEvent> {
            int count = 0;
            double ratio = 0.0;
            String label;

            SumEvent(int c, double r, String l) : count(c), ratio(r), label(std::move(l)) {}
        };

        struct QueuedEvent : EventBase<QueuedEvent> {
            int value = 0;

            explicit QueuedEvent(int v) : value(v) {}
        };
    }  // namespace SemanticsEvents

    using namespace SemanticsEvents;

    // Every listener writes its id into the log of the test.
    class Listener : public SubscriptionHolder {
    public:
        // Subscribe() and UnsubscribeAll() are protected in SubscriptionHolder; the test drives them directly.
        using SubscriptionHolder::Subscribe;
        using SubscriptionHolder::UnsubscribeAll;

        Listener(int listenerId, Vector<int>& sharedLog) : id(listenerId), log(&sharedLog) {}

        int id;
        Vector<int>* log;
        int pings = 0;
        int lastCount = 0;
        double lastRatio = 0.0;
        String lastLabel;

        void OnPing(const PingEvent&) {
            ++pings;
            log->push_back(id);
        }

        void OnPingToo(const PingEvent&) {
            log->push_back(id + 100);
        }

        void OnPingWithoutArgument() {
            log->push_back(id + 200);
        }

        void OnOther(const OtherEvent&) {
            log->push_back(id + 300);
        }

        void OnSum(const SumEvent& e) {
            lastCount = e.count;
            lastRatio = e.ratio;
            lastLabel = e.label;
        }

        void OnQueued(const QueuedEvent& e) {
            log->push_back(e.value);
        }
    };

    // A free handler: the instance is the first argument, the shape of IComponent::InvokeInActiveScene.
    void FreeOnPing(Listener* listener, const PingEvent&) {
        listener->log->push_back(listener->id + 400);
    }

    static_assert(std::is_default_constructible_v<PingEvent>);
    static_assert(!std::is_default_constructible_v<SumEvent>);

    TEST(EventDispatchSemanticsTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts above are the check";
    }

    TEST(EventDispatchSemanticsTest, EmitDeliversInReverseOrderOfSubscription) {
        Vector<int> log;
        Listener a(1, log);
        Listener b(2, log);
        Listener c(3, log);
        a.Subscribe<PingEvent, &Listener::OnPing>(&a);
        b.Subscribe<PingEvent, &Listener::OnPing>(&b);
        c.Subscribe<PingEvent, &Listener::OnPing>(&c);

        PingEvent::Emit();

        EXPECT_EQ(log, (Vector<int>{3, 2, 1}));
    }

    TEST(EventDispatchSemanticsTest, SubscribingTheSamePairAgainCallsItOnceAndMovesItLast) {
        Vector<int> log;
        Listener a(1, log);
        Listener b(2, log);
        a.Subscribe<PingEvent, &Listener::OnPing>(&a);
        b.Subscribe<PingEvent, &Listener::OnPing>(&b);
        a.Subscribe<PingEvent, &Listener::OnPing>(&a);

        PingEvent::Emit();

        // The entry of a moved behind b, so it runs first and there is no second call.
        EXPECT_EQ(log, (Vector<int>{1, 2}));
    }

    TEST(EventDispatchSemanticsTest, DroppingTheOlderConnectionOfADoubledPairRemovesTheListener) {
        Vector<int> log;
        Listener target(1, log);
        Listener other(2, log);
        other.Subscribe<PingEvent, &Listener::OnPing>(&other);
        {
            Listener older(0, log);
            older.Subscribe<PingEvent, &Listener::OnPing>(&target);
            // The second connection of the pair replaces the entry; `newer` stays alive.
            Listener newer(0, log);
            newer.Subscribe<PingEvent, &Listener::OnPing>(&target);
            PingEvent::Emit();
            EXPECT_EQ(log, (Vector<int>{1, 2})) << "the pair is called once";

            // A connection disconnects by (handler, instance), not by a token: dropping `older` drops the pair.
            older.UnsubscribeAll();
            log.clear();
            PingEvent::Emit();
            EXPECT_EQ(log, (Vector<int>{2})) << "the listener is gone although `newer` still holds a connection";
        }
        log.clear();
        PingEvent::Emit();
        EXPECT_EQ(log, (Vector<int>{2})) << "an unrelated instance stays";
    }

    TEST(EventDispatchSemanticsTest, TwoInstancesWithTheSameHandlerAreIndependent) {
        Vector<int> log;
        Listener a(1, log);
        Listener b(2, log);
        a.Subscribe<PingEvent, &Listener::OnPing>(&a);
        b.Subscribe<PingEvent, &Listener::OnPing>(&b);

        a.UnsubscribeAll();
        PingEvent::Emit();

        EXPECT_EQ(log, (Vector<int>{2}));
        EXPECT_EQ(a.pings, 0);
        EXPECT_EQ(b.pings, 1);
    }

    TEST(EventDispatchSemanticsTest, DifferentHandlersOfOneInstanceAreDistinctEntries) {
        Vector<int> log;
        Listener a(1, log);
        a.Subscribe<PingEvent, &Listener::OnPing>(&a);
        a.Subscribe<PingEvent, &Listener::OnPingToo>(&a);
        a.Subscribe<PingEvent, &Listener::OnPingWithoutArgument>(&a);
        a.Subscribe<PingEvent, &FreeOnPing>(&a);

        PingEvent::Emit();

        EXPECT_EQ(log, (Vector<int>{401, 201, 101, 1})) << "the free function, the argument-less member and the members";
    }

    TEST(EventDispatchSemanticsTest, UnsubscribeRemovesOneHandlerOfOneInstance) {
        Vector<int> log;
        Listener a(1, log);
        Listener b(2, log);
        a.Subscribe<PingEvent, &Listener::OnPing>(&a);
        a.Subscribe<PingEvent, &Listener::OnPingToo>(&a);
        b.Subscribe<PingEvent, &Listener::OnPing>(&b);

        PingEvent::Unsubscribe<&Listener::OnPing>(&a);
        PingEvent::Emit();

        // The last entry takes the place of the removed one (see the next test), so b runs after a's other handler.
        EXPECT_EQ(log, (Vector<int>{101, 2}));
    }

    // A disconnect moves the last entry into the freed slot instead of shifting the tail: the order of the
    // others changes. The subscription order of a system after a re-subscribe depends on it.
    TEST(EventDispatchSemanticsTest, ADisconnectMovesTheLastEntryIntoTheFreedSlot) {
        Vector<int> log;
        Listener a(1, log);
        Listener b(2, log);
        Listener c(3, log);
        a.Subscribe<PingEvent, &Listener::OnPing>(&a);
        b.Subscribe<PingEvent, &Listener::OnPing>(&b);
        c.Subscribe<PingEvent, &Listener::OnPing>(&c);

        a.Subscribe<PingEvent, &Listener::OnPing>(&a);
        PingEvent::Emit();

        // Entries [a, b, c]: a leaves, c takes its slot, a is appended: [c, b, a], delivered a, b, c.
        EXPECT_EQ(log, (Vector<int>{1, 2, 3}));
    }

    TEST(EventDispatchSemanticsTest, UnsubscribeAllRemovesOneInstanceForOneEventOnly) {
        Vector<int> log;
        Listener a(1, log);
        Listener b(2, log);
        a.Subscribe<PingEvent, &Listener::OnPing>(&a);
        a.Subscribe<PingEvent, &Listener::OnPingToo>(&a);
        a.Subscribe<OtherEvent, &Listener::OnOther>(&a);
        b.Subscribe<PingEvent, &Listener::OnPing>(&b);

        PingEvent::UnsubscribeAll(&a);
        PingEvent::Emit();
        OtherEvent::Emit();

        EXPECT_EQ(log, (Vector<int>{2, 301}));
    }

    TEST(EventDispatchSemanticsTest, ASubscriptionWithoutTheEventArgumentIsCalled) {
        Vector<int> log;
        Listener a(1, log);
        a.Subscribe<PingEvent, &Listener::OnPingWithoutArgument>(&a);

        PingEvent::Emit();
        PingEvent::Emit();

        EXPECT_EQ(log, (Vector<int>{201, 201}));
    }

    TEST(EventDispatchSemanticsTest, EmitWithSeveralArgumentsDeliversTheFields) {
        Vector<int> log;
        Listener a(1, log);
        a.Subscribe<SumEvent, &Listener::OnSum>(&a);

        SumEvent::Emit(7, 2.5, String("seven"));

        EXPECT_EQ(a.lastCount, 7);
        EXPECT_EQ(a.lastRatio, 2.5);
        EXPECT_EQ(a.lastLabel, String("seven"));
    }

    TEST(EventDispatchSemanticsTest, AnEmitInsideAHandlerIsDeliveredAtOnceAndCompletesBeforeTheOuterGoesOn) {
        struct Nester : SubscriptionHolder {
            using SubscriptionHolder::Subscribe;
            Vector<int>* log = nullptr;

            void OnPing(const PingEvent&) {
                log->push_back(1);
                NestedEvent::Emit();
                log->push_back(3);
            }

            void OnNested(const NestedEvent&) {
                log->push_back(2);
            }
        };

        Vector<int> log;
        Nester nester;
        nester.log = &log;
        nester.Subscribe<PingEvent, &Nester::OnPing>(&nester);
        nester.Subscribe<NestedEvent, &Nester::OnNested>(&nester);

        PingEvent::Emit();

        EXPECT_EQ(log, (Vector<int>{1, 2, 3}));
    }

    TEST(EventDispatchSemanticsTest, EnqueueDeliversNothingBeforeUpdateThenInEnqueueOrderThenTheQueueIsEmpty) {
        Vector<int> log;
        Listener a(1, log);
        a.Subscribe<QueuedEvent, &Listener::OnQueued>(&a);

        QueuedEvent::Enqueue(10);
        QueuedEvent::Enqueue(20);
        QueuedEvent::Enqueue(30);
        EXPECT_TRUE(log.empty());

        QueuedEvent::Update();
        EXPECT_EQ(log, (Vector<int>{10, 20, 30}));

        QueuedEvent::Update();
        EXPECT_EQ(log.size(), 3u) << "the delivered events are removed";
    }

    TEST(EventDispatchSemanticsTest, AnEventEnqueuedByAHandlerDuringUpdateWaitsForTheNextUpdate) {
        struct Chain : SubscriptionHolder {
            using SubscriptionHolder::Subscribe;
            Vector<int>* log = nullptr;

            void OnQueued(const QueuedEvent& e) {
                log->push_back(e.value);
                if (e.value == 1) {
                    QueuedEvent::Enqueue(2);
                }
            }
        };

        Vector<int> log;
        Chain chain;
        chain.log = &log;
        chain.Subscribe<QueuedEvent, &Chain::OnQueued>(&chain);

        QueuedEvent::Enqueue(1);
        QueuedEvent::Update();
        EXPECT_EQ(log, (Vector<int>{1}));

        QueuedEvent::Update();
        EXPECT_EQ(log, (Vector<int>{1, 2}));

        QueuedEvent::Update();
        EXPECT_EQ(log, (Vector<int>{1, 2}));
    }

    TEST(EventDispatchSemanticsTest, ASubscriberAddedByAHandlerIsNotCalledInThatDispatch) {
        Vector<int> log;
        Listener late(9, log);

        struct Adder : SubscriptionHolder {
            using SubscriptionHolder::Subscribe;
            using SubscriptionHolder::UnsubscribeAll;
            Listener* late = nullptr;
            bool added = false;

            void OnPing(const PingEvent&) {
                if (!added) {
                    added = true;
                    Subscribe<PingEvent, &Listener::OnPing>(late);
                }
            }
        };

        Adder adder;
        adder.late = &late;
        adder.Subscribe<PingEvent, &Adder::OnPing>(&adder);

        PingEvent::Emit();
        EXPECT_EQ(late.pings, 0);

        PingEvent::Emit();
        EXPECT_EQ(late.pings, 1);

        adder.UnsubscribeAll();
    }

    TEST(EventDispatchSemanticsTest, TheFirstEnqueueRegistersUpdateInTheQueueRegistryOnce) {
        struct Chain : SubscriptionHolder {
            using SubscriptionHolder::Subscribe;
            Vector<int>* log = nullptr;

            void OnQueued(const QueuedEvent& e) {
                log->push_back(e.value);
                if (e.value == 1) {
                    QueuedEvent::Enqueue(2);
                }
            }
        };

        Vector<int> log;
        Chain chain;
        chain.log = &log;
        chain.Subscribe<QueuedEvent, &Chain::OnQueued>(&chain);

        QueuedEvent::Enqueue(1);
        QueuedEvent::Enqueue(1);
        QueuedEvent::Enqueue(1);
        EXPECT_TRUE(log.empty());

        // Registered more than once, one UpdateAll() would also deliver the events the handlers enqueue meanwhile.
        EventsQueueRegistry::UpdateAll();
        EXPECT_EQ(log, (Vector<int>{1, 1, 1}));

        EventsQueueRegistry::UpdateAll();
        EXPECT_EQ(log, (Vector<int>{1, 1, 1, 2, 2, 2}));

        EventsQueueRegistry::UpdateAll();
        EXPECT_EQ(log.size(), 6u);
    }

    TEST(EventDispatchSemanticsTest, AHolderDestroyedBeforeTheEventStopsReceiving) {
        Vector<int> log;
        Listener survivor(1, log);
        survivor.Subscribe<QueuedEvent, &Listener::OnQueued>(&survivor);
        {
            Listener gone(2, log);
            gone.Subscribe<PingEvent, &Listener::OnPing>(&gone);
            gone.Subscribe<QueuedEvent, &Listener::OnQueued>(&gone);
            QueuedEvent::Enqueue(5);
        }

        PingEvent::Emit();
        QueuedEvent::Update();

        EXPECT_EQ(log, (Vector<int>{5})) << "only the survivor gets the queued event, nobody gets the ping";
    }

    TEST(EventDispatchSemanticsTest, UnsubscribeAllOfTheHolderDropsEverythingAndTheHolderCanSubscribeAgain) {
        Vector<int> log;
        Listener a(1, log);
        a.Subscribe<PingEvent, &Listener::OnPing>(&a);
        a.Subscribe<OtherEvent, &Listener::OnOther>(&a);

        a.UnsubscribeAll();
        PingEvent::Emit();
        OtherEvent::Emit();
        EXPECT_TRUE(log.empty());

        a.Subscribe<PingEvent, &Listener::OnPing>(&a);
        PingEvent::Emit();
        EXPECT_EQ(log, (Vector<int>{1}));
    }

    // The defaulted move of SubscriptionHolder moves the connections and leaves them bound to the address that
    // subscribed, so the moved-to object receives nothing.
    TEST(EventDispatchSemanticsTest, AMovedHolderKeepsTheConnectionsBoundToTheOriginalInstance) {
        Vector<int> log;
        Listener moved(1, log);
        moved.Subscribe<PingEvent, &Listener::OnPing>(&moved);
        {
            Listener target(std::move(moved));
            PingEvent::Emit();
            EXPECT_EQ(moved.pings, 1) << "the connection still calls the instance that subscribed";
            EXPECT_EQ(target.pings, 0);
        }

        // `target` held the connection and has gone out of scope.
        PingEvent::Emit();
        EXPECT_EQ(moved.pings, 1);
    }

}  // namespace BECore::Tests
