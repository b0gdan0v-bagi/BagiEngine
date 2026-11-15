#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Utils/FixedFunction.h>
#include <BECore/Utils/FunctionRef.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <EASTL/functional.h>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// BE_CALLABLES_BENCH=<iterations> runs that many and prints a line per case; without it a case is a smoke run that checks results
        constexpr const char* kBenchVariable = "BE_CALLABLES_BENCH";
        constexpr uint64_t kSmokeIterations = 4000;
        constexpr uint64_t kSeed = 3;
        constexpr size_t kWordBytes = sizeof(uint64_t);
        /// The inline bytes `eastl::function` keeps (one pointer and a word more), and the buffer of the default MoveOnlyFunction
        constexpr size_t kSmallCapture = 16;
        constexpr size_t kLargeCapture = 48;
        constexpr size_t kElements = 8;
        constexpr double kNanosPerSecond = 1e9;

        uint64_t Iterations(bool& printing) {
            const char* text = std::getenv(kBenchVariable);
            printing = false;
            if (text == nullptr || *text == '\0') {
                return kSmokeIterations;
            }
            uint64_t value = 0;
            const char* end = text + std::strlen(text);
            const auto result = std::from_chars(text, end, value);
            if (result.ec != std::errc() || result.ptr != end || value == 0) {
                return kSmokeIterations;
            }
            printing = true;
            return value;
        }

        /// Makes the object escape so that an optimizer cannot drop the construction or the allocation it holds
        void Escape(const void* object) {
            static std::atomic<const void*> sink;
            sink.store(object, std::memory_order_relaxed);
        }

        /// A callable of @p Bytes of capture, trivially copyable so that `eastl::function` takes it too
        template <size_t Bytes>
        struct Payload {
            uint64_t words[Bytes / kWordBytes];

            explicit Payload(uint64_t seed) {
                for (uint64_t& word : words) {
                    word = seed;
                }
            }
            uint64_t operator()(uint64_t value) const {
                return value + words[0];
            }
        };

        template <>
        struct Payload<0> {
            explicit Payload(uint64_t) {}
            uint64_t operator()(uint64_t value) const {
                return value + kSeed;
            }
        };

        struct Measure {
            double nanos = 0;
            double allocs = 0;
        };

        class Stopwatch {
        public:
            Stopwatch() : _start(std::chrono::steady_clock::now()), _allocs(AllocTracker::GetThreadCounters().count) {}

            Measure PerOperation(uint64_t operations) const {
                const auto elapsed = std::chrono::steady_clock::now() - _start;
                const uint64_t allocs = AllocTracker::GetThreadCounters().count - _allocs;
                const double count = static_cast<double>(operations);
                return {std::chrono::duration<double>(elapsed).count() * kNanosPerSecond / count, static_cast<double>(allocs) / count};
            }

        private:
            std::chrono::steady_clock::time_point _start;
            uint64_t _allocs;
        };

        void Report(const char* name, const char* what, size_t capture, const Measure& measure, bool printing) {
            if (!printing) {
                return;
            }
            if (AllocTracker::kEnabled) {
                std::printf("%-18s %-9s %2zu bytes  %8.2f ns/call  %5.2f allocs/call\n", name, what, capture, measure.nanos, measure.allocs);
            } else {
                std::printf("%-18s %-9s %2zu bytes  %8.2f ns/call  allocs n/a\n", name, what, capture, measure.nanos);
            }
        }

        /// Times the call of one function object and the construct-and-destroy of a function from a callable of @p Bytes
        template <typename TFunction, size_t Bytes>
        void BenchCallable(const char* name, uint64_t iterations, bool printing) {
            Payload<Bytes> payload(kSeed);
            TFunction function(payload);

            uint64_t sum = 0;
            const Stopwatch callWatch;
            for (uint64_t index = 0; index < iterations; ++index) {
                sum += function(index);
            }
            const Measure call = callWatch.PerOperation(iterations);
            EXPECT_EQ(sum, iterations * (iterations - 1) / 2 + kSeed * iterations) << name;
            Report(name, "call", Bytes, call, printing);

            uint64_t made = 0;
            const Stopwatch makeWatch;
            for (uint64_t index = 0; index < iterations; ++index) {
                const Payload<Bytes> value(index);
                TFunction local(value);
                Escape(&local);
                ++made;
            }
            const Measure construct = makeWatch.PerOperation(iterations);
            EXPECT_EQ(made, iterations) << name;
            Report(name, "construct", Bytes, construct, printing);
        }

        template <size_t Bytes>
        void BenchAllTypes(uint64_t iterations, bool printing) {
            BenchCallable<MoveOnlyFunction<uint64_t(uint64_t)>, Bytes>("MoveOnlyFunction", iterations, printing);
            BenchCallable<FixedFunction<kLargeCapture, uint64_t(uint64_t)>, Bytes>("FixedFunction", iterations, printing);
            BenchCallable<eastl::function<uint64_t(uint64_t)>, Bytes>("eastl::function", iterations, printing);  //-NoStd: the baseline of the comparison
            BenchCallable<FunctionRef<uint64_t(uint64_t)>, Bytes>("FunctionRef", iterations, printing);
        }

        /// The shape of a `ForEach` helper: the parameter is called once per element of one call
        EA_NO_INLINE void ForEachRef(size_t count, FunctionRef<void(uint64_t)> body) {
            for (size_t index = 0; index < count; ++index) {
                body(index);
            }
        }

        EA_NO_INLINE void ForEachFunction(size_t count, const eastl::function<void(uint64_t)>& body) {  //-NoStd: the baseline of the comparison
            for (size_t index = 0; index < count; ++index) {
                body(index);
            }
        }

        /// The caller builds the parameter from a lambda that captures @p References references, in every round
        template <size_t References>
        void BenchForEach(uint64_t rounds, bool printing) {
            static_assert(References == 1 || References == 3, "one reference fits eastl::function inline, three do not");
            const uint64_t expected = rounds * (kElements * (kElements - 1) / 2);
            const size_t capture = References * sizeof(void*);

            uint64_t sum = 0;
            uint64_t other = 0;
            uint64_t third = 0;
            const Stopwatch refWatch;
            for (uint64_t round = 0; round < rounds; ++round) {
                if constexpr (References == 1) {
                    ForEachRef(kElements, [&sum](uint64_t index) { sum += index; });
                } else {
                    ForEachRef(kElements, [&sum, &other, &third](uint64_t index) {
                        sum += index;
                        other += 0 * index;
                        third += 0 * index;
                    });
                }
            }
            const Measure ref = refWatch.PerOperation(rounds * kElements);
            EXPECT_EQ(sum, expected);
            Report("ForEach FunctionRef", "per element", capture, ref, printing);

            sum = 0;
            const Stopwatch functionWatch;
            for (uint64_t round = 0; round < rounds; ++round) {
                if constexpr (References == 1) {
                    ForEachFunction(kElements, [&sum](uint64_t index) { sum += index; });
                } else {
                    ForEachFunction(kElements, [&sum, &other, &third](uint64_t index) {
                        sum += index;
                        other += 0 * index;
                        third += 0 * index;
                    });
                }
            }
            const Measure function = functionWatch.PerOperation(rounds * kElements);
            EXPECT_EQ(sum, expected);
            EXPECT_EQ(other + third, 0u);
            Report("ForEach eastl::fn", "per element", capture, function, printing);
        }

    }  // namespace

    /// Prints numbers on request and asserts nothing about time: tests run side by side and a machine drifts, so it is not a perf-set member
    TEST(CallablesBenchTest, EmptyCapture) {
        bool printing = false;
        BenchAllTypes<0>(Iterations(printing), printing);
    }

    TEST(CallablesBenchTest, CaptureOfWhatEastlKeepsInside) {
        bool printing = false;
        BenchAllTypes<kSmallCapture>(Iterations(printing), printing);
    }

    TEST(CallablesBenchTest, CaptureOfTheDefaultBuffer) {
        bool printing = false;
        BenchAllTypes<kLargeCapture>(Iterations(printing), printing);
    }

    TEST(CallablesBenchTest, ForEachParameter) {
        bool printing = false;
        const uint64_t iterations = Iterations(printing);
        BenchForEach<1>(iterations, printing);
        BenchForEach<3>(iterations, printing);
    }

}  // namespace BECore::Tests
