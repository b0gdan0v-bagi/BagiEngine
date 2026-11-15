#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <EASTL/deque.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Makes the object escape so that an optimizer cannot drop the allocation it holds
        void Escape(const void* object) {
            static std::atomic<const void*> sink;
            sink.store(object, std::memory_order_relaxed);
        }

        /// Counts how many objects of the type are alive: a callable that is destroyed twice or never shows as a non-zero balance
        struct LifeCounter {
            static inline int alive = 0;
            static inline int destroyed = 0;

            LifeCounter() {
                ++alive;
            }
            LifeCounter(LifeCounter&&) noexcept {
                ++alive;
            }
            LifeCounter(const LifeCounter&) = delete;
            ~LifeCounter() {
                --alive;
                ++destroyed;
            }
        };

        /// Callable of @p Padding bytes after the counter; a callable above the buffer takes the heap path
        template <size_t Padding>
        struct Counted {
            LifeCounter life;
            char padding[Padding] = {};
            int* calls;

            explicit Counted(int* callsCounter) : calls(callsCounter) {}
            Counted(Counted&&) noexcept = default;

            void operator()() {
                ++*calls;
            }
        };

        using Small = Counted<8>;
        using Large = Counted<kDefaultFunctionInlineBytes * 2>;

        /// Moving may throw: such a callable never lives inside the buffer
        struct ThrowingMove {
            ThrowingMove() = default;
            ThrowingMove(ThrowingMove&&) noexcept(false) {}
            void operator()() const {}
        };

        struct alignas(32) OverAligned {
            void operator()() const {}
        };

        class MoveOnlyFunctionTest : public ::testing::Test {
        protected:
            void SetUp() override {
                LifeCounter::alive = 0;
                LifeCounter::destroyed = 0;
            }
            void TearDown() override {
                EXPECT_EQ(LifeCounter::alive, 0);
            }
        };

        using Task = MoveOnlyFunction<void()>;

        static_assert(kFunctionFitsInline<Small>);
        static_assert(!kFunctionFitsInline<Large>);
        static_assert(!kFunctionFitsInline<ThrowingMove>);
        static_assert(!kFunctionFitsInline<OverAligned>);
        static_assert(kFunctionFitsInline<void (*)()>);
        static_assert(kFunctionFitsInline<UniquePtr<int>>);
        static_assert(kFunctionFitsInline<Large, 128>);
        static_assert(std::is_nothrow_move_constructible_v<Task>);
        static_assert(std::is_nothrow_move_assignable_v<Task>);
        static_assert(!std::is_copy_constructible_v<Task>);
        static_assert(!std::is_copy_assignable_v<Task>);
        static_assert(std::is_nothrow_default_constructible_v<Task>);
        static_assert(sizeof(Task) == kDefaultFunctionInlineBytes + 2 * sizeof(void*));

    }  // namespace

    TEST_F(MoveOnlyFunctionTest, CompileTimeChecks) {
        SUCCEED();
    }

    TEST_F(MoveOnlyFunctionTest, AMoveOnlyCaptureIsCalled) {
        auto owned = MakeUnique<int>(41);
        MoveOnlyFunction<int()> function = [owned = std::move(owned)]() { return ++*owned; };
        EXPECT_EQ(function(), 42);
        EXPECT_EQ(function(), 43);
    }

    TEST_F(MoveOnlyFunctionTest, ArgumentsAndResultPassThrough) {
        MoveOnlyFunction<String(int&, const String&, String&&)> function = [](int& counter, const String& left, String&& right) {
            ++counter;
            return Format("{}{}", left, right);
        };
        int counter = 0;
        const String left = "ab";
        EXPECT_EQ(function(counter, left, String("cd")), "abcd");
        EXPECT_EQ(counter, 1);
    }

    TEST_F(MoveOnlyFunctionTest, AVoidSignatureDiscardsTheResultOfTheCallable) {
        int calls = 0;
        MoveOnlyFunction<void()> function = [&calls]() { return ++calls; };
        function();
        EXPECT_EQ(calls, 1);
    }

    TEST_F(MoveOnlyFunctionTest, ACaptureThatFitsLivesInsideAndOneAboveTheBufferWorks) {
        int calls = 0;
        {
            Task small(Small{&calls});
            Task large(Large{&calls});
            const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
            small();
            large();
            const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();
            EXPECT_EQ(calls, 2);
            EXPECT_EQ(after.count, before.count);
        }
        EXPECT_EQ(LifeCounter::alive, 0);
    }

    TEST_F(MoveOnlyFunctionTest, ConstructionAllocatesOnlyForACaptureAboveTheBuffer) {
        int calls = 0;
        const AllocTracker::ThreadCounters start = AllocTracker::GetThreadCounters();
        Task small(Small{&calls});
        Escape(&small);
        const AllocTracker::ThreadCounters afterSmall = AllocTracker::GetThreadCounters();
        Task large(Large{&calls});
        Escape(&large);
        const AllocTracker::ThreadCounters afterLarge = AllocTracker::GetThreadCounters();
        Task moved = std::move(large);
        const AllocTracker::ThreadCounters afterMove = AllocTracker::GetThreadCounters();
        if (AllocTracker::kEnabled) {
            EXPECT_EQ(afterSmall.count, start.count);
            EXPECT_EQ(afterLarge.count, afterSmall.count + 1);
            EXPECT_EQ(afterMove.count, afterLarge.count);
        } else {
            EXPECT_EQ(afterLarge.count, start.count);
        }
    }

    TEST_F(MoveOnlyFunctionTest, ACallableThatMayThrowWhileMovingOrNeedsMoreAlignmentWorks) {
        Task throwing{ThrowingMove{}};
        Task aligned{OverAligned{}};
        Task movedThrowing = std::move(throwing);
        Task movedAligned = std::move(aligned);
        movedThrowing();
        movedAligned();
        EXPECT_FALSE(throwing);
        EXPECT_TRUE(movedThrowing);
    }

    TEST_F(MoveOnlyFunctionTest, MovingLeavesTheSourceEmptyAndDestroysTheCallableOnce) {
        int calls = 0;
        {
            Task source(Small{&calls});
            EXPECT_EQ(LifeCounter::alive, 1);
            Task target(std::move(source));
            EXPECT_FALSE(source);
            EXPECT_TRUE(target);
            EXPECT_EQ(LifeCounter::alive, 1);
            target();
            EXPECT_EQ(calls, 1);
        }
        EXPECT_EQ(LifeCounter::alive, 0);
    }

    TEST_F(MoveOnlyFunctionTest, MovingTheHeapPathLeavesTheSourceEmptyAndDestroysTheCallableOnce) {
        int calls = 0;
        {
            Task source(Large{&calls});
            Task target(std::move(source));
            EXPECT_FALSE(source);
            EXPECT_EQ(LifeCounter::alive, 1);
            target();
            EXPECT_EQ(calls, 1);
        }
        EXPECT_EQ(LifeCounter::alive, 0);
    }

    template <typename TCallable>
    class MoveOnlyFunctionLifetimeTest : public MoveOnlyFunctionTest {};

    using Callables = ::testing::Types<Small, Large>;
    TYPED_TEST_SUITE(MoveOnlyFunctionLifetimeTest, Callables);

    TYPED_TEST(MoveOnlyFunctionLifetimeTest, ResetDestroysTheCallableOnce) {
        int calls = 0;
        Task function{TypeParam{&calls}};
        EXPECT_EQ(LifeCounter::alive, 1);
        function.Reset();
        EXPECT_EQ(LifeCounter::alive, 0);
        EXPECT_FALSE(function);
        const int destroyedAfterFirst = LifeCounter::destroyed;
        function.Reset();
        EXPECT_EQ(LifeCounter::destroyed, destroyedAfterFirst);
    }

    TYPED_TEST(MoveOnlyFunctionLifetimeTest, AssignmentOverwritesAndDestroysTheOldCallable) {
        int firstCalls = 0;
        int secondCalls = 0;
        Task function{TypeParam{&firstCalls}};
        function = Task{TypeParam{&secondCalls}};
        EXPECT_EQ(LifeCounter::alive, 1);
        function();
        EXPECT_EQ(firstCalls, 0);
        EXPECT_EQ(secondCalls, 1);
        function = TypeParam{&firstCalls};
        EXPECT_EQ(LifeCounter::alive, 1);
        function();
        EXPECT_EQ(firstCalls, 1);
        function = nullptr;
        EXPECT_EQ(LifeCounter::alive, 0);
    }

    TYPED_TEST(MoveOnlyFunctionLifetimeTest, SelfMoveKeepsTheCallable) {
        int calls = 0;
        Task function{TypeParam{&calls}};
        Task& alias = function;
        function = std::move(alias);
        EXPECT_EQ(LifeCounter::alive, 1);
        ASSERT_TRUE(function);
        function();
        EXPECT_EQ(calls, 1);
    }

    TYPED_TEST(MoveOnlyFunctionLifetimeTest, AnUnrunOneIsDestroyedWithItsHolder) {
        int calls = 0;
        {
            Task function{TypeParam{&calls}};
            EXPECT_EQ(LifeCounter::alive, 1);
        }
        EXPECT_EQ(LifeCounter::alive, 0);
        EXPECT_EQ(calls, 0);
    }

    TYPED_TEST(MoveOnlyFunctionLifetimeTest, UnrunOnesInContainersAreDestroyedOnce) {
        int calls = 0;
        {
            Vector<Task> tasks;
            for (int index = 0; index < 20; ++index) {
                tasks.push_back(Task{TypeParam{&calls}});
            }
            EXPECT_EQ(LifeCounter::alive, 20);
            tasks.erase(tasks.begin() + 3);
            EXPECT_EQ(LifeCounter::alive, 19);

            eastl::deque<Task> queue;  //-NoStd: the container the task queue uses
            for (int index = 0; index < 20; ++index) {
                queue.push_back(Task{TypeParam{&calls}});
            }
            queue.pop_front();
            EXPECT_EQ(LifeCounter::alive, 19 + 19);
        }
        EXPECT_EQ(LifeCounter::alive, 0);
        EXPECT_EQ(calls, 0);
    }

    /// The outer callable of the overwrite tests: @p Padding picks the inline path (small) or the heap path (above the buffer)
    template <size_t Padding>
    struct OwnsInner {
        LifeCounter life;
        char padding[Padding + 1] = {};
        UniquePtr<Task> task;
        UniquePtr<Small> small;

        OwnsInner(UniquePtr<Task> ownedTask, UniquePtr<Small> ownedSmall) : task(std::move(ownedTask)), small(std::move(ownedSmall)) {}
        OwnsInner(OwnsInner&&) noexcept = default;

        void operator()() const {}
    };

    /// Reports what its destructor sees of the function that holds it; the moved-from objects of the relocations report nothing
    template <size_t Padding>
    struct WatchesHolder {
        using OnDestroy = void (*)(Task& holder, int* counter);

        LifeCounter life;
        char padding[Padding + 1] = {};
        Task* holder;
        OnDestroy onDestroy;
        int* counter;

        WatchesHolder(Task* heldBy, OnDestroy callback, int* callbackCounter) : holder(heldBy), onDestroy(callback), counter(callbackCounter) {}
        WatchesHolder(WatchesHolder&& other) noexcept : life(std::move(other.life)), holder(std::exchange(other.holder, nullptr)), onDestroy(other.onDestroy), counter(other.counter) {}
        ~WatchesHolder() {
            if (holder != nullptr) {
                onDestroy(*holder, counter);
            }
        }

        void operator()() const {}
    };

    using InlinePadding = std::integral_constant<size_t, 8>;
    using HeapPadding = std::integral_constant<size_t, kDefaultFunctionInlineBytes * 2>;
    static_assert(kFunctionFitsInline<OwnsInner<InlinePadding::value>>);
    static_assert(!kFunctionFitsInline<OwnsInner<HeapPadding::value>>);
    static_assert(kFunctionFitsInline<WatchesHolder<InlinePadding::value>>);
    static_assert(!kFunctionFitsInline<WatchesHolder<HeapPadding::value>>);

    template <typename TPadding>
    class MoveOnlyFunctionOverwriteTest : public MoveOnlyFunctionTest {};

    using Paddings = ::testing::Types<InlinePadding, HeapPadding>;
    TYPED_TEST_SUITE(MoveOnlyFunctionOverwriteTest, Paddings);

    TYPED_TEST(MoveOnlyFunctionOverwriteTest, AssigningAFunctionTheOldCaptureOwnsKeepsIt) {
        int calls = 0;
        auto inner = MakeUnique<Task>(Small{&calls});
        Task* innerPointer = inner.get();
        Task function{OwnsInner<TypeParam::value>{std::move(inner), nullptr}};
        EXPECT_EQ(LifeCounter::alive, 2);

        function = std::move(*innerPointer);

        EXPECT_EQ(LifeCounter::alive, 1);
        ASSERT_TRUE(function);
        function();
        EXPECT_EQ(calls, 1);
    }

    TYPED_TEST(MoveOnlyFunctionOverwriteTest, AssigningACallableTheOldCaptureOwnsKeepsIt) {
        int calls = 0;
        auto inner = MakeUnique<Small>(&calls);
        Small* innerPointer = inner.get();
        Task function{OwnsInner<TypeParam::value>{nullptr, std::move(inner)}};
        EXPECT_EQ(LifeCounter::alive, 2);

        function = std::move(*innerPointer);

        EXPECT_EQ(LifeCounter::alive, 1);
        ASSERT_TRUE(function);
        function();
        EXPECT_EQ(calls, 1);
    }

    TYPED_TEST(MoveOnlyFunctionOverwriteTest, TheOldCaptureSeesTheNewCallableWhileItIsDestroyed) {
        int calls = 0;
        int seenCallable = 0;
        Task function{WatchesHolder<TypeParam::value>{&function, [](Task& holder, int* counter) { *counter += holder ? 1 : 0; }, &seenCallable}};
        Task replacement{[&calls] { ++calls; }};

        function = std::move(replacement);

        EXPECT_EQ(seenCallable, 1);  // the destructor found the new callable in the function, not the half-destroyed old one
        function();
        EXPECT_EQ(calls, 1);
    }

    TYPED_TEST(MoveOnlyFunctionOverwriteTest, TheOldCaptureSeesAnEmptyFunctionWhenItIsResetOrCleared) {
        const auto record = [](Task& holder, int* counter) { *counter = holder ? 1 : 0; };
        int observed = -1;
        Task function{WatchesHolder<TypeParam::value>{&function, record, &observed}};
        function.Reset();
        EXPECT_EQ(observed, 0);
        EXPECT_FALSE(function);

        observed = -1;
        function = WatchesHolder<TypeParam::value>{&function, record, &observed};
        function = nullptr;
        EXPECT_EQ(observed, 0);
        EXPECT_FALSE(function);
    }

    TYPED_TEST(MoveOnlyFunctionOverwriteTest, ADestructorThatAssignsTheFunctionLeavesAValidOne) {
        int calls = 0;
        const auto assign = [](Task& holder, int* counter) { holder = Task{[counter] { ++*counter; }}; };
        Task function{WatchesHolder<TypeParam::value>{&function, assign, &calls}};

        function = Task{[] {}};  // the destructor of the old capture then stores its own callable
        ASSERT_TRUE(function);
        function();
        EXPECT_EQ(calls, 1);

        function.Reset();
        EXPECT_FALSE(function);
        EXPECT_EQ(LifeCounter::alive, 0);
    }

    TEST_F(MoveOnlyFunctionTest, TheEmptyState) {
        Task empty;
        EXPECT_FALSE(empty);
        EXPECT_TRUE(empty == nullptr);
        Task fromNull = nullptr;
        EXPECT_FALSE(fromNull);
        int calls = 0;
        Task filled{Small{&calls}};
        EXPECT_TRUE(filled);
        EXPECT_FALSE(filled == nullptr);
        filled = nullptr;
        EXPECT_FALSE(filled);
    }

    TEST_F(MoveOnlyFunctionTest, EmptySourcesGiveAnEmptyFunction) {
        void (*nullFunction)() = nullptr;
        EXPECT_FALSE(Task{nullFunction});
        EXPECT_FALSE(Task{std::function<void()>{}});  //-NoStd: the conversion source under test
        EXPECT_FALSE((Task{MoveOnlyFunction<void(), 16>{}}));
    }

    TEST_F(MoveOnlyFunctionTest, AConstObjectIsCalledAndAMutableLambdaKeepsItsState) {
        int seen = 0;
        const MoveOnlyFunction<int()> function = [count = 0]() mutable { return ++count; };
        seen += function();
        seen += function();
        EXPECT_EQ(seen, 1 + 2);
    }

    TEST_F(MoveOnlyFunctionTest, ItIsBuiltFromAStdFunctionAFunctionPointerAndALambda) {
        int calls = 0;
        MoveOnlyFunction<void()> fromStd = std::function<void()>([&calls] { ++calls; });  //-NoStd: the conversion source under test
        fromStd();
        EXPECT_EQ(calls, 1);

        static int pointerCalls = 0;
        MoveOnlyFunction<void()> fromPointer = +[]() { ++pointerCalls; };
        fromPointer();
        EXPECT_EQ(pointerCalls, 1);

        MoveOnlyFunction<int(int)> fromFunction = [](int value) { return value * 2; };
        EXPECT_EQ(fromFunction(21), 42);
    }

    TEST_F(MoveOnlyFunctionTest, AFunctionOfAnotherBufferSizeIsMovedIn) {
        int calls = 0;
        MoveOnlyFunction<void(), 16> small = Small{&calls};
        EXPECT_EQ(LifeCounter::alive, 1);
        Task wide = std::move(small);
        EXPECT_FALSE(small);
        wide();
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(LifeCounter::alive, 1);
    }

    TEST_F(MoveOnlyFunctionTest, ANestedFunctionTakesTheHeapPathOfTheOuterOne) {
        int calls = 0;
        Task inner{Small{&calls}};
        static_assert(!kFunctionFitsInline<decltype([inner = std::move(inner)]() {})>);
        Task outer{[inner = std::move(inner)]() mutable { inner(); }};
        outer();
        EXPECT_EQ(calls, 1);
    }

}  // namespace BECore::Tests
