#include <BECore/Scene/SceneUpdateList.h>
#include <Common/AllocCheck.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kSlots = 8;
        constexpr size_t kBulkEntries = 100;
        constexpr int kBulkRuns = 10;

        /// What a run hands to the invokers as its event: the log of the calls and an optional action a call performs
        struct RunContext {
            SceneUpdateList* list = nullptr;
            char* slots = nullptr;
            Vector<size_t> calls;
            void (*action)(RunContext& context, size_t index) = nullptr;
        };

        IComponent& SlotOf(RunContext& context, size_t index) {
            return *reinterpret_cast<IComponent*>(context.slots + index);
        }

        /// The list never dereferences a component, so the addresses of a local array stand for components
        class UpdateListFixture : public ::testing::Test {
        protected:
            IComponent& Slot(size_t index) {
                return *reinterpret_cast<IComponent*>(&_slots[index]);
            }

            void Add(size_t index) {
                _list.Add(Slot(index), &Invoke);
            }

            Vector<size_t> Run() {
                RunContext context;
                context.list = &_list;
                context.slots = _slots;
                _list.Run(&context);
                return context.calls;
            }

            Vector<size_t> RunWith(void (*action)(RunContext&, size_t)) {
                RunContext context;
                context.list = &_list;
                context.slots = _slots;
                context.action = action;
                _list.Run(&context);
                return context.calls;
            }

            static void Invoke(IComponent* component, const void* event) {
                auto& context = *const_cast<RunContext*>(static_cast<const RunContext*>(event));
                const size_t index = static_cast<size_t>(reinterpret_cast<const char*>(component) - context.slots);
                context.calls.push_back(index);
                if (context.action != nullptr) {
                    context.action(context, index);
                }
            }

            SceneUpdateList _list;
            char _slots[kSlots] = {};
        };

        using Calls = Vector<size_t>;

    }  // namespace

    using SceneUpdateListTest = UpdateListFixture;

    TEST_F(SceneUpdateListTest, LastAddedIsCalledFirst) {
        Add(0);
        Add(1);
        Add(2);
        EXPECT_EQ(Run(), (Calls{2, 1, 0}));
    }

    TEST_F(SceneUpdateListTest, AnEntryAddedByACallRunsInTheNextRunOnly) {
        Add(0);
        const Calls first = RunWith([](RunContext& context, size_t index) {
            if (index == 0) {
                context.list->Add(SlotOf(context, 1), &Invoke);
            }
        });
        EXPECT_EQ(first, (Calls{0}));
        EXPECT_EQ(_list.GetCount(), 2u);
        EXPECT_EQ(Run(), (Calls{1, 0}));
    }

    TEST_F(SceneUpdateListTest, RemoveOutsideARunKeepsTheOrderOfTheOthers) {
        Add(0);
        Add(1);
        Add(2);
        Add(3);
        _list.Remove(Slot(1));
        EXPECT_EQ(_list.GetCount(), 3u);
        EXPECT_EQ(Run(), (Calls{3, 2, 0}));
    }

    TEST_F(SceneUpdateListTest, RemoveDropsEveryEntryOfTheComponent) {
        Add(0);
        Add(1);
        Add(0);
        _list.Remove(Slot(0));
        EXPECT_EQ(_list.GetCount(), 1u);
        EXPECT_EQ(Run(), (Calls{1}));
    }

    TEST_F(SceneUpdateListTest, RemovingALaterEntryInARunSkipsItAndAnEarlierOneChangesNothingNow) {
        Add(0);
        Add(1);
        Add(2);
        // 2 runs first and removes 0 (not yet called: skipped) and 2 itself (already called: no effect on this run)
        const Calls calls = RunWith([](RunContext& context, size_t index) {
            if (index == 2) {
                context.list->Remove(SlotOf(context, 0));
                context.list->Remove(SlotOf(context, 2));
                EXPECT_EQ(context.list->GetCount(), 1u);
            }
        });
        EXPECT_EQ(calls, (Calls{2, 1}));
        EXPECT_EQ(_list.GetCount(), 1u);
        EXPECT_EQ(Run(), (Calls{1}));
    }

    TEST_F(SceneUpdateListTest, ARemovedAndAddedComponentRunsOnceInTheNextRun) {
        Add(0);
        Add(1);
        const Calls first = RunWith([](RunContext& context, size_t index) {
            if (index == 1) {
                context.list->Remove(SlotOf(context, 0));
                context.list->Add(SlotOf(context, 0), &Invoke);
            }
        });
        EXPECT_EQ(first, (Calls{1}));
        EXPECT_EQ(_list.GetCount(), 2u);
        EXPECT_EQ(Run(), (Calls{0, 1}));
    }

    TEST_F(SceneUpdateListTest, ANestedRunDoesNotCompactBeforeTheOuterOneEnds) {
        Add(0);
        Add(1);
        Add(2);
        Add(3);
        // 3 removes 2 (later in the outer run), then runs the list again inside: 2 is skipped there too, and the outer run still skips it
        const Calls calls = RunWith([](RunContext& context, size_t index) {
            if (index == 3) {
                context.list->Remove(SlotOf(context, 2));
                RunContext inner;
                inner.list = context.list;
                inner.slots = context.slots;
                context.list->Run(&inner);
                EXPECT_EQ(inner.calls, (Calls{3, 1, 0}));
            }
        });
        EXPECT_EQ(calls, (Calls{3, 1, 0}));
        EXPECT_EQ(Run(), (Calls{3, 1, 0}));
    }

    TEST_F(SceneUpdateListTest, TheEventPointerReachesTheInvokerUnchanged) {
        struct Probe {
            const void* seen = nullptr;
        } probe;
        SceneUpdateList list;
        list.Add(Slot(0), [](IComponent*, const void* event) { const_cast<Probe*>(static_cast<const Probe*>(event))->seen = event; });
        list.Run(&probe);
        EXPECT_EQ(probe.seen, &probe);
    }

    TEST_F(SceneUpdateListTest, ASteadyRunAllocatesNothing) {
        Vector<char> components(kBulkEntries);
        SceneUpdateList list;
        int counter = 0;
        for (char& component : components) {
            list.Add(*reinterpret_cast<IComponent*>(&component), [](IComponent*, const void* event) { ++*const_cast<int*>(static_cast<const int*>(event)); });
        }
        list.Run(&counter);
        ExpectNoAllocations([&] {
            for (int run = 0; run < kBulkRuns; ++run) {
                list.Run(&counter);
            }
        });
        EXPECT_EQ(counter, static_cast<int>(kBulkEntries) * (kBulkRuns + 1));
    }

}  // namespace BECore::Tests
