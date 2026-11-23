#include <BECore/RefCounted/New.h>
#include <BECore/Types/Types.h>
#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <type_traits>
#include <utility>

namespace BECore::Tests {

    namespace {

        constexpr int kContendingThreads = 4;
        constexpr int kCopiesPerThread = 20000;

        struct Counts {
            std::atomic<int> destroyed{0};
        };

        class Base : public RefCountedNonAtomic {
        public:
            explicit Base(Counts* counts = nullptr) : _counts(counts) {}
            ~Base() override {
                if (_counts != nullptr) {
                    ++_counts->destroyed;
                }
            }

        private:
            Counts* _counts;
        };

        class Derived : public Base {
        public:
            using Base::Base;
        };

        class Sibling : public Base {
        public:
            using Base::Base;
        };

        class Unrelated : public RefCountedNonAtomic {};

        class AtomicBase : public RefCountedAtomic {
        public:
            explicit AtomicBase(Counts* counts = nullptr) : _counts(counts) {}
            ~AtomicBase() override {
                if (_counts != nullptr) {
                    ++_counts->destroyed;
                }
            }

        private:
            Counts* _counts;
        };

        class AtomicDerived : public AtomicBase {
        public:
            using AtomicBase::AtomicBase;
        };

        /// Reaches back into the pointer that held it from its destructor: what it sees and whether Reset() there is harmless
        class Reentrant : public RefCountedNonAtomic {
        public:
            struct Report {
                int destroyed = 0;
                const Reentrant* seenInHolder = nullptr;
            };

            Reentrant(IntrusivePtr<Reentrant>* holder, Report* report) : _holder(holder), _report(report) {}
            ~Reentrant() override {
                ++_report->destroyed;
                _report->seenInHolder = _holder->Get();
                _holder->Reset();
            }

        private:
            IntrusivePtr<Reentrant>* _holder;
            Report* _report;
        };

        static_assert(std::is_convertible_v<IntrusivePtr<Derived>, IntrusivePtr<Base>>);
        static_assert(std::is_constructible_v<IntrusivePtr<Base>, IntrusivePtr<Derived>>);
        static_assert(std::is_convertible_v<IntrusivePtr<AtomicDerived>, IntrusivePtr<AtomicBase>>);
        static_assert(std::is_assignable_v<IntrusivePtr<Base>&, IntrusivePtr<Derived>>);
        static_assert(std::is_assignable_v<IntrusivePtr<Base>&, const IntrusivePtr<Derived>&>);

        static_assert(!std::is_constructible_v<IntrusivePtr<Derived>, IntrusivePtr<Base>>);
        static_assert(!std::is_constructible_v<IntrusivePtr<Derived>, const IntrusivePtr<Base>&>);
        static_assert(!std::is_assignable_v<IntrusivePtr<Derived>&, IntrusivePtr<Base>>);
        static_assert(!std::is_assignable_v<IntrusivePtr<Derived>&, const IntrusivePtr<Base>&>);
        static_assert(!std::is_constructible_v<IntrusivePtr<Base>, IntrusivePtr<Unrelated>>);
        static_assert(!std::is_assignable_v<IntrusivePtr<Base>&, IntrusivePtr<Unrelated>>);
        static_assert(std::is_constructible_v<IntrusivePtr<Base>, IntrusivePtr<Sibling>>);
        static_assert(!std::is_constructible_v<IntrusivePtr<Sibling>, IntrusivePtr<Derived>>);

        static_assert(std::is_nothrow_destructible_v<IntrusivePtr<Base>>);
        static_assert(std::is_nothrow_move_constructible_v<IntrusivePtr<Base>>);
        static_assert(std::is_nothrow_move_assignable_v<IntrusivePtr<Base>>);
        static_assert(std::is_nothrow_swappable_v<IntrusivePtr<Base>>);

        // The counter of the pointee decides the thread rule; the pointer type is one for both.
        static_assert(std::is_same_v<decltype(New<AtomicDerived>()), IntrusivePtr<AtomicDerived>>);
        static_assert(std::is_same_v<decltype(New<Derived>()), IntrusivePtr<Derived>>);

    }  // namespace

    TEST(IntrusivePtrTest, CompileTimeChecks) {
        // The contracts are the static_asserts above: a downcast, an unrelated type and a throwing move do not compile.
        SUCCEED();
    }

    TEST(IntrusivePtrTest, CopyAndMoveKeepTheCount) {
        Counts counts;
        IntrusivePtr<Base> first = New<Base>(&counts);
        EXPECT_EQ(first.GetRefCount(), 1);

        IntrusivePtr<Base> copy = first;
        EXPECT_EQ(first.GetRefCount(), 2);
        EXPECT_EQ(copy.Get(), first.Get());

        IntrusivePtr<Base> moved = std::move(copy);
        EXPECT_EQ(copy.Get(), nullptr);
        EXPECT_EQ(first.GetRefCount(), 2);

        IntrusivePtr<Base> assigned;
        assigned = first;
        EXPECT_EQ(first.GetRefCount(), 3);

        IntrusivePtr<Base> moveAssigned;
        moveAssigned = std::move(assigned);
        EXPECT_FALSE(assigned);
        EXPECT_EQ(first.GetRefCount(), 3);

        moved.Reset();
        moveAssigned.Reset();
        EXPECT_EQ(first.GetRefCount(), 1);
        EXPECT_EQ(counts.destroyed, 0);
        first.Reset();
        EXPECT_EQ(counts.destroyed, 1);
    }

    TEST(IntrusivePtrTest, SelfAssignmentChangesNothing) {
        Counts counts;
        IntrusivePtr<Base> ptr = New<Base>(&counts);
        IntrusivePtr<Base>& alias = ptr;

        ptr = alias;
        EXPECT_EQ(ptr.GetRefCount(), 1);
        ptr = std::move(alias);
        ASSERT_TRUE(ptr);
        EXPECT_EQ(ptr.GetRefCount(), 1);
        ptr = ptr.Get();
        EXPECT_EQ(ptr.GetRefCount(), 1);
        EXPECT_EQ(counts.destroyed, 0);
    }

    TEST(IntrusivePtrTest, AssignmentFromADerivedPointerSharesTheObject) {
        Counts counts;
        IntrusivePtr<Derived> derived = New<Derived>(&counts);
        IntrusivePtr<Base> base = derived;
        EXPECT_EQ(derived.GetRefCount(), 2);

        IntrusivePtr<Base> other;
        other = std::move(derived);
        EXPECT_FALSE(derived);
        EXPECT_EQ(other.GetRefCount(), 2);

        IntrusivePtr<Base> replaced = New<Base>(&counts);
        replaced = base;
        EXPECT_EQ(counts.destroyed, 1);
        EXPECT_EQ(replaced.GetRefCount(), 3);
    }

    TEST(IntrusivePtrTest, ResetAndTheBooleanForms) {
        Counts counts;
        IntrusivePtr<Base> ptr;
        EXPECT_FALSE(ptr);
        EXPECT_TRUE(!ptr);
        EXPECT_FALSE(ptr.IsValid());
        EXPECT_EQ(ptr.GetRefCount(), 0);

        Base* raw = new Base(&counts);
        ptr.Reset(raw);
        EXPECT_TRUE(ptr);
        EXPECT_TRUE(ptr.IsValid());
        EXPECT_EQ(raw->GetRefCount(), 1);

        ptr.Reset(New<Base>(&counts).Get());
        EXPECT_EQ(counts.destroyed, 1);

        ptr.Reset();
        EXPECT_FALSE(ptr);
        EXPECT_EQ(counts.destroyed, 2);
    }

    TEST(IntrusivePtrTest, ADestructorThatReachesBackIntoItsHolderSeesNoDyingObject) {
        Reentrant::Report report;

        IntrusivePtr<Reentrant> ptr;
        ptr = New<Reentrant>(&ptr, &report);
        ptr.Reset();
        EXPECT_EQ(report.destroyed, 1);
        EXPECT_EQ(report.seenInHolder, nullptr);

        ptr = New<Reentrant>(&ptr, &report);
        ptr = nullptr;
        EXPECT_EQ(report.destroyed, 2);
        EXPECT_EQ(report.seenInHolder, nullptr);

        Reentrant::Report assignedReport;
        {
            IntrusivePtr<Reentrant> holder;
            IntrusivePtr<Reentrant> survivor;
            survivor = New<Reentrant>(&survivor, &assignedReport);
            holder = New<Reentrant>(&holder, &assignedReport);
            const Reentrant* dying = holder.Get();
            holder = survivor;
            EXPECT_EQ(assignedReport.destroyed, 1);
            EXPECT_NE(assignedReport.seenInHolder, dying);
            // the dying object's own Reset() dropped the survivor from `holder`, and `survivor` still holds it
            EXPECT_TRUE(survivor);
        }
        EXPECT_EQ(assignedReport.destroyed, 2);
    }

    TEST(IntrusivePtrTest, ADestructorThatReachesBackIntoAContainerElementSeesNull) {
        Reentrant::Report report;
        Vector<IntrusivePtr<Reentrant>> items;
        items.resize(1);
        items[0] = New<Reentrant>(&items[0], &report);

        items.clear();
        EXPECT_EQ(report.destroyed, 1);
        EXPECT_EQ(report.seenInHolder, nullptr);
    }

    TEST(IntrusivePtrTest, StaticPointerCastKeepsOrMovesTheCount) {
        Counts counts;
        IntrusivePtr<Base> base = New<Derived>(&counts);

        IntrusivePtr<Derived> down = StaticPointerCast<Derived>(base);
        EXPECT_EQ(down.Get(), static_cast<Derived*>(base.Get()));
        EXPECT_EQ(base.GetRefCount(), 2);

        IntrusivePtr<Base> up = StaticPointerCast<Base>(down);
        EXPECT_EQ(base.GetRefCount(), 3);

        IntrusivePtr<Derived> moved = StaticPointerCast<Derived>(std::move(up));
        EXPECT_FALSE(up);
        EXPECT_EQ(base.GetRefCount(), 3);

        IntrusivePtr<Base> empty;
        EXPECT_FALSE(StaticPointerCast<Derived>(empty));
    }

    TEST(IntrusivePtrTest, DynamicPointerCastIsEmptyWhenTheTypeIsWrong) {
        Counts counts;
        IntrusivePtr<Base> base = New<Derived>(&counts);

        IntrusivePtr<Derived> right = DynamicPointerCast<Derived>(base);
        EXPECT_EQ(right.Get(), base.Get());
        EXPECT_EQ(base.GetRefCount(), 2);

        EXPECT_FALSE(DynamicPointerCast<Sibling>(base));
        EXPECT_EQ(base.GetRefCount(), 2);

        // a failed cast of an rvalue leaves the source alone; a successful one takes its count
        IntrusivePtr<Base> source = New<Derived>(&counts);
        EXPECT_FALSE(DynamicPointerCast<Sibling>(std::move(source)));
        ASSERT_TRUE(source);
        EXPECT_EQ(source.GetRefCount(), 1);

        IntrusivePtr<Derived> taken = DynamicPointerCast<Derived>(std::move(source));
        EXPECT_FALSE(source);
        ASSERT_TRUE(taken);
        EXPECT_EQ(taken.GetRefCount(), 1);
    }

    TEST(IntrusivePtrTest, DetachThenAdoptKeepsOneCount) {
        Counts counts;
        IntrusivePtr<Base> ptr = New<Base>(&counts);

        Base* raw = ptr.Detach();
        EXPECT_FALSE(ptr);
        ASSERT_NE(raw, nullptr);
        EXPECT_EQ(raw->GetRefCount(), 1);
        EXPECT_EQ(counts.destroyed, 0);

        IntrusivePtr<Base> adopted(raw, kAdoptRef);
        EXPECT_EQ(adopted.Get(), raw);
        EXPECT_EQ(adopted.GetRefCount(), 1);
        adopted.Reset();
        EXPECT_EQ(counts.destroyed, 1);
    }

    TEST(IntrusivePtrTest, SwapExchangesWithoutTouchingTheCounts) {
        Counts counts;
        IntrusivePtr<Base> first = New<Base>(&counts);
        IntrusivePtr<Base> second = New<Base>(&counts);
        const Base* firstRaw = first.Get();
        const Base* secondRaw = second.Get();

        first.swap(second);
        EXPECT_EQ(first.Get(), secondRaw);
        EXPECT_EQ(second.Get(), firstRaw);

        swap(first, second);
        EXPECT_EQ(first.Get(), firstRaw);
        EXPECT_EQ(first.GetRefCount(), 1);
        EXPECT_EQ(second.GetRefCount(), 1);
        EXPECT_EQ(counts.destroyed, 0);
    }

    TEST(IntrusivePtrTest, ComparisonsLookAtTheObjectNotTheStaticType) {
        Counts counts;
        IntrusivePtr<Derived> derived = New<Derived>(&counts);
        IntrusivePtr<Base> base = derived;
        IntrusivePtr<Base> other = New<Base>(&counts);
        IntrusivePtr<Base> empty;

        EXPECT_TRUE(base == derived);
        EXPECT_TRUE(derived == base);
        EXPECT_FALSE(base != derived);
        EXPECT_TRUE(base != other);

        EXPECT_TRUE(derived == derived.Get());
        EXPECT_TRUE(derived.Get() == derived);
        EXPECT_TRUE(base == derived.Get());
        EXPECT_TRUE(base.Get() != other);
        EXPECT_TRUE(other != derived.Get());

        EXPECT_TRUE(empty == nullptr);
        EXPECT_TRUE(nullptr == empty);
        EXPECT_TRUE(base != nullptr);
        EXPECT_TRUE(nullptr != base);
    }

    TEST(IntrusivePtrTest, OrderingIsTheOrderOfTheAddresses) {
        Counts counts;
        IntrusivePtr<Base> first = New<Base>(&counts);
        IntrusivePtr<Base> second = New<Base>(&counts);

        EXPECT_EQ(first < second, first.Get() < second.Get());
        EXPECT_EQ(first > second, first.Get() > second.Get());
        EXPECT_EQ(first <= second, first.Get() <= second.Get());
        // clang-format off: the formatter of this repository is set to C++17 and splits `<=>`
        EXPECT_TRUE((first <=> first) == std::strong_ordering::equal);
        EXPECT_TRUE((first <=> second) == (first.Get() <=> second.Get()));
        EXPECT_TRUE((first <=> nullptr) == std::strong_ordering::greater);
        EXPECT_TRUE((first <=> second.Get()) == (first.Get() <=> second.Get()));
        // clang-format on
    }

    TEST(IntrusivePtrTest, EqualPointersHashEqualAndWorkAsKeys) {
        Counts counts;
        IntrusivePtr<Base> first = New<Base>(&counts);
        IntrusivePtr<Base> sameObject = first;
        IntrusivePtr<Base> second = New<Base>(&counts);

        EXPECT_EQ(eastl::hash<IntrusivePtr<Base>>{}(first), eastl::hash<IntrusivePtr<Base>>{}(sameObject));
        EXPECT_EQ(std::hash<IntrusivePtr<Base>>{}(first), std::hash<IntrusivePtr<Base>>{}(sameObject));

        HashMap<IntrusivePtr<Base>, int> map;
        map[first] = 1;
        map[second] = 2;
        EXPECT_EQ(map.size(), 2u);
        const auto found = map.find(sameObject);
        ASSERT_NE(found, map.end());
        EXPECT_EQ(found->second, 1);
    }

    TEST(IntrusivePtrTest, BothCountersReportTheirCountThroughThePointer) {
        Counts counts;
        IntrusivePtr<Base> nonAtomic = New<Base>(&counts);
        IntrusivePtr<AtomicBase> atomic = New<AtomicDerived>(&counts);
        IntrusivePtr<Base> nonAtomicCopy = nonAtomic;
        IntrusivePtr<AtomicBase> atomicCopy = atomic;

        EXPECT_EQ(nonAtomic.GetRefCount(), 2);
        EXPECT_EQ(atomic.GetRefCount(), 2);
        atomicCopy.Reset();
        EXPECT_EQ(atomic.GetRefCount(), 1);
    }

    TEST(IntrusivePtrTest, ConcurrentCopiesOfAnAtomicObjectKeepTheCount) {
        Counts counts;
        IntrusivePtr<AtomicBase> shared = New<AtomicDerived>(&counts);

        std::atomic<bool> go{false};
        Vector<std::thread> threads;
        for (int i = 0; i < kContendingThreads; ++i) {
            threads.emplace_back([&] {
                while (!go.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                for (int n = 0; n < kCopiesPerThread; ++n) {
                    IntrusivePtr<AtomicBase> local = shared;
                    EXPECT_TRUE(local);
                }
            });
        }
        go.store(true, std::memory_order_release);
        for (std::thread& thread : threads) {
            thread.join();
        }

        EXPECT_EQ(shared.GetRefCount(), 1);
        EXPECT_EQ(counts.destroyed, 0);
        shared.Reset();
        EXPECT_EQ(counts.destroyed, 1);
    }

    TEST(IntrusivePtrTest, TheLastHolderAmongThreadsDestroysTheAtomicObjectOnce) {
        Counts counts;
        IntrusivePtr<AtomicBase> shared = New<AtomicDerived>(&counts);

        std::atomic<bool> go{false};
        Vector<std::thread> threads;
        for (int i = 0; i < kContendingThreads; ++i) {
            threads.emplace_back([&go, own = shared] {
                while (!go.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                for (int n = 0; n < kCopiesPerThread; ++n) {
                    IntrusivePtr<AtomicBase> local = own;
                    EXPECT_TRUE(local);
                }
            });
        }
        shared.Reset();
        go.store(true, std::memory_order_release);
        for (std::thread& thread : threads) {
            thread.join();
        }

        EXPECT_EQ(counts.destroyed, 1);
    }

}  // namespace BECore::Tests
