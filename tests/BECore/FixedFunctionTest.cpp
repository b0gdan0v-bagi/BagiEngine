#include <Common/AllocCheck.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kBuffer = 32;

        using Fixed = FixedFunction<kBuffer, int(int)>;

        /// Counts the live objects: a callable destroyed twice or never leaves a balance other than zero
        struct LifeCounter {
            static inline int alive = 0;

            LifeCounter() {
                ++alive;
            }
            LifeCounter(LifeCounter&&) noexcept {
                ++alive;
            }
            LifeCounter(const LifeCounter&) = delete;
            ~LifeCounter() {
                --alive;
            }
        };

        struct Tracked {
            LifeCounter life;
            int addend = 0;

            int operator()(int value) const {
                return value + addend;
            }
        };

        struct TooBig {
            char bytes[kBuffer * 2] = {};

            int operator()(int value) const {
                return value;
            }
        };

        struct ThrowingMove {
            ThrowingMove() = default;
            ThrowingMove(ThrowingMove&&) noexcept(false) {}
            int operator()(int value) const {
                return value;
            }
        };

        static_assert(std::is_constructible_v<Fixed, Tracked>);
        static_assert(!std::is_constructible_v<Fixed, TooBig>);
        static_assert(!std::is_constructible_v<Fixed, ThrowingMove>);
        static_assert(!std::is_assignable_v<Fixed&, TooBig>);
        static_assert(std::is_constructible_v<FixedFunction<sizeof(TooBig), int(int)>, TooBig>);
        static_assert(std::is_nothrow_move_constructible_v<Fixed>);
        static_assert(std::is_nothrow_move_assignable_v<Fixed>);
        static_assert(!std::is_copy_constructible_v<Fixed>);
        static_assert(sizeof(Fixed) == kBuffer + 2 * sizeof(void*));

        class FixedFunctionTest : public ::testing::Test {
        protected:
            void SetUp() override {
                LifeCounter::alive = 0;
            }
            void TearDown() override {
                EXPECT_EQ(LifeCounter::alive, 0);
            }
        };

    }  // namespace

    TEST_F(FixedFunctionTest, CompileTimeChecks) {
        SUCCEED();
    }

    TEST_F(FixedFunctionTest, AMoveOnlyCaptureIsCalled) {
        Fixed function = [owned = MakeUnique<int>(40)](int value) { return *owned + value; };
        EXPECT_EQ(function(2), 42);
    }

    TEST_F(FixedFunctionTest, AMutableLambdaKeepsItsStateOverTwoCalls) {
        const Fixed function = [sum = 0](int value) mutable { return sum += value; };
        EXPECT_EQ(function(1), 1);
        EXPECT_EQ(function(2), 3);
    }

    TEST_F(FixedFunctionTest, TheEmptyState) {
        Fixed empty;
        EXPECT_FALSE(empty);
        EXPECT_TRUE(empty == nullptr);
        Fixed fromNull = nullptr;
        EXPECT_FALSE(fromNull);
        Fixed filled = [](int value) { return value; };
        EXPECT_TRUE(filled);
        filled = nullptr;
        EXPECT_FALSE(filled);
        void (*nullFunction)() = nullptr;
        EXPECT_FALSE((FixedFunction<kBuffer, void()>{nullFunction}));
    }

    TEST_F(FixedFunctionTest, MovingLeavesTheSourceEmptyAndDestroysTheCallableOnce) {
        {
            Fixed source{Tracked{{}, 5}};
            EXPECT_EQ(LifeCounter::alive, 1);
            Fixed target = std::move(source);
            EXPECT_FALSE(source);
            EXPECT_EQ(LifeCounter::alive, 1);
            EXPECT_EQ(target(1), 6);
        }
        EXPECT_EQ(LifeCounter::alive, 0);
    }

    TEST_F(FixedFunctionTest, ResetAndAssignmentDestroyTheOldCallableOnce) {
        Fixed function{Tracked{{}, 1}};
        function = Tracked{{}, 2};
        EXPECT_EQ(LifeCounter::alive, 1);
        EXPECT_EQ(function(0), 2);
        function = Fixed{Tracked{{}, 3}};
        EXPECT_EQ(LifeCounter::alive, 1);
        EXPECT_EQ(function(0), 3);
        function.Reset();
        EXPECT_EQ(LifeCounter::alive, 0);
        function.Reset();
        EXPECT_EQ(LifeCounter::alive, 0);
    }

    TEST_F(FixedFunctionTest, SelfMoveKeepsTheCallable) {
        Fixed function{Tracked{{}, 4}};
        Fixed& alias = function;
        function = std::move(alias);
        EXPECT_EQ(LifeCounter::alive, 1);
        EXPECT_EQ(function(1), 5);
    }

    TEST_F(FixedFunctionTest, ItNeverAllocates) {
        ExpectNoAllocations([] {
            Fixed function{Tracked{{}, 1}};
            Fixed moved = std::move(function);
            Fixed other = [](int value) { return value; };
            moved = std::move(other);
            EXPECT_EQ(moved(7), 7);
        });
    }

    TEST_F(FixedFunctionTest, ItIsStoredInAVectorAndDestroyedOnce) {
        {
            Vector<Fixed> functions;
            for (int index = 0; index < 10; ++index) {
                functions.push_back(Fixed{Tracked{{}, index}});
            }
            EXPECT_EQ(LifeCounter::alive, 10);
            EXPECT_EQ(functions[9](1), 10);
        }
        EXPECT_EQ(LifeCounter::alive, 0);
    }

}  // namespace BECore::Tests
