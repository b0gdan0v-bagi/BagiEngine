#include <BECore/Utils/FunctionRef.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        int gFreeCalls = 0;

        int Twice(int value) {
            ++gFreeCalls;
            return value * 2;
        }

        struct Counter {
            int total = 0;

            void Add(int value) {
                total += value;
            }
            int operator()(int value) const {
                return value + 1;
            }
        };

        /// The shape of a `ForEach(n, callback)` parameter
        int SumOver(int count, FunctionRef<int(int)> term) {
            int sum = 0;
            for (int index = 0; index < count; ++index) {
                sum += term(index);
            }
            return sum;
        }

        static_assert(sizeof(FunctionRef<void()>) == 2 * sizeof(void*));
        static_assert(std::is_trivially_copyable_v<FunctionRef<void(int)>>);
        static_assert(std::is_trivially_destructible_v<FunctionRef<void(int)>>);
        static_assert(!std::is_default_constructible_v<FunctionRef<void()>>);
        static_assert(std::is_constructible_v<FunctionRef<int(int)>, int (*)(int)>);
        static_assert(!std::is_constructible_v<FunctionRef<int(int)>, int (*)(String)>);
        static_assert(!std::is_constructible_v<FunctionRef<int(int)>, int>);

    }  // namespace

    TEST(FunctionRefTest, CompileTimeChecks) {
        SUCCEED();
    }

    TEST(FunctionRefTest, ItCallsACapturingLambdaLvalue) {
        int calls = 0;
        auto lambda = [&calls](int value) {
            ++calls;
            return value + 10;
        };
        FunctionRef<int(int)> ref = lambda;
        EXPECT_EQ(ref(1), 11);
        EXPECT_EQ(ref(2), 12);
        EXPECT_EQ(calls, 2);
    }

    TEST(FunctionRefTest, AMutableLambdaLvalueKeepsItsState) {
        auto lambda = [sum = 0](int value) mutable { return sum += value; };
        FunctionRef<int(int)> ref = lambda;
        EXPECT_EQ(ref(5), 5);
        EXPECT_EQ(ref(5), 10);
        EXPECT_EQ(lambda(1), 11);
    }

    TEST(FunctionRefTest, ItCallsAFreeFunctionAndAFunctionPointer) {
        gFreeCalls = 0;
        FunctionRef<int(int)> fromFunction = Twice;
        int (*pointer)(int) = &Twice;
        FunctionRef<int(int)> fromPointer = pointer;
        EXPECT_EQ(fromFunction(4), 8);
        EXPECT_EQ(fromPointer(5), 10);
        EXPECT_EQ(gFreeCalls, 2);
    }

    TEST(FunctionRefTest, AFunctionPointerIsCopiedSoATemporaryPointerIsEnough) {
        gFreeCalls = 0;
        FunctionRef<int(int)> ref = &Twice;
        EXPECT_EQ(ref(7), 14);
        EXPECT_EQ(gFreeCalls, 1);
    }

    TEST(FunctionRefTest, ItCallsALambdaThatCallsAMember) {
        Counter counter;
        auto lambda = [&counter](int value) { counter.Add(value); };
        FunctionRef<void(int)> ref = lambda;
        ref(3);
        ref(4);
        EXPECT_EQ(counter.total, 7);
    }

    TEST(FunctionRefTest, ItCallsAConstObject) {
        const Counter counter;
        FunctionRef<int(int)> ref = counter;
        EXPECT_EQ(ref(1), 2);
    }

    TEST(FunctionRefTest, AConstFunctionRefIsCalled) {
        int calls = 0;
        auto lambda = [&calls]() { ++calls; };
        const FunctionRef<void()> ref = lambda;
        ref();
        EXPECT_EQ(calls, 1);
    }

    TEST(FunctionRefTest, AFunctionTakingAFunctionRefIsCalledWithATemporaryLambda) {
        int calls = 0;
        EXPECT_EQ(SumOver(4,
                          [&calls](int index) {
                              ++calls;
                              return index * 10;
                          }),
                  0 + 10 + 20 + 30);
        EXPECT_EQ(calls, 4);
    }

    TEST(FunctionRefTest, ACopyIsACopyNotAViewOfTheView) {
        int calls = 0;
        auto lambda = [&calls]() { ++calls; };
        FunctionRef<void()> first = lambda;
        FunctionRef<void()> second = first;
        first = second;
        second();
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(std::memcmp(&first, &second, sizeof(first)), 0);
    }

    TEST(FunctionRefTest, ArgumentsAndResultsPassThrough) {
        auto lambda = [](int& counter, const String& left, String&& right) {
            ++counter;
            return Format("{}{}", left, right);
        };
        FunctionRef<String(int&, const String&, String&&)> ref = lambda;
        int counter = 0;
        const String left = "ab";
        EXPECT_EQ(ref(counter, left, String("cd")), "abcd");
        EXPECT_EQ(counter, 1);
    }

}  // namespace BECore::Tests
