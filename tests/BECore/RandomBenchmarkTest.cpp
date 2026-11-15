#include <BECore/Format/Format.h>
#include <BECore/Random/Rng.h>
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <numeric>
#include <random>  //-NoStd: the reference the benchmark compares with

namespace BECore::Tests {

    namespace {

        constexpr int kRepeats = 5;
#ifdef NDEBUG
        constexpr int kDraws = 10'000'000;
        constexpr int kShuffles = 200'000;
#else
        // A Debug build measures nothing worth quoting, so it runs a tenth of the work to keep `ctest` quick
        constexpr int kDraws = 1'000'000;
        constexpr int kShuffles = 20'000;
#endif
        constexpr size_t kDeckSize = 52;
        constexpr uint64_t kSeed = 12345;
        constexpr double kNanosecondsPerSecond = 1e9;

        using Clock = std::chrono::steady_clock;

        /// The best of `kRepeats` runs of `fn()` in nanoseconds per call, `calls` being the calls one run makes. `fn` returns a checksum of
        /// its results, which keeps the work from being optimized away; the checksum of the best run goes to `checksum`.
        template <typename Fn>
        double BestNsPerCall(int calls, uint64_t& checksum, Fn&& fn) {
            double best = 1e300;
            for (int i = 0; i < kRepeats; ++i) {
                const auto start = Clock::now();
                checksum = fn();
                const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
                best = std::min(best, seconds * kNanosecondsPerSecond / calls);
            }
            return best;
        }

        void Print(const char* name, double nsPerCall) {
            const String line = Format("{:<34} {:9.2f} ns/call\n", name, nsPerCall);
            std::cout << line.c_str();
        }

        uint64_t DrawNext64(int count) {
            Rng rng(kSeed);
            uint64_t sum = 0;
            for (int i = 0; i < count; ++i) {
                sum += rng.Next64();
            }
            return sum;
        }

        uint64_t DrawUniformInt(int count) {
            Rng rng(kSeed);
            uint64_t sum = 0;
            for (int i = 0; i < count; ++i) {
                sum += static_cast<uint64_t>(rng.UniformInt(0, 5));
            }
            return sum;
        }

        uint64_t ShuffleDecks(int count) {
            Rng rng(kSeed);
            Array<int, kDeckSize> deck{};
            std::iota(deck.begin(), deck.end(), 0);
            uint64_t sum = 0;
            for (int i = 0; i < count; ++i) {
                rng.Shuffle(Span<int>(deck));
                sum += static_cast<uint64_t>(deck[0]);
            }
            return sum;
        }

        uint64_t DrawMt19937(int count) {
            std::mt19937_64 gen(kSeed);  //-NoStd: the reference the benchmark compares with
            uint64_t sum = 0;
            for (int i = 0; i < count; ++i) {
                sum += gen();
            }
            return sum;
        }

        uint64_t DrawMtDistribution(int count) {
            std::mt19937_64 gen(kSeed);                             //-NoStd: the reference the benchmark compares with
            std::uniform_int_distribution<int> distribution(0, 5);  //-NoStd: the reference the benchmark compares with
            uint64_t sum = 0;
            for (int i = 0; i < count; ++i) {
                sum += static_cast<uint64_t>(distribution(gen));
            }
            return sum;
        }

        uint64_t ShuffleDecksMt(int count) {
            std::mt19937_64 gen(kSeed);  //-NoStd: the reference the benchmark compares with
            Array<int, kDeckSize> deck{};
            std::iota(deck.begin(), deck.end(), 0);
            uint64_t sum = 0;
            for (int i = 0; i < count; ++i) {
                std::shuffle(deck.begin(), deck.end(), gen);  //-NoStd: the reference the benchmark compares with
                sum += static_cast<uint64_t>(deck[0]);
            }
            return sum;
        }

    }  // namespace

    /// Prints what the generator costs beside `std::mt19937_64`; it asserts no time (a loaded machine would fail it), only that the
    /// work is the same in every run so that it cannot have been dropped. Run it alone: `BagiEngineTests --gtest_filter='RandomBenchmark*'`.
    TEST(RandomBenchmarkTest, RngBesideMt19937) {
        uint64_t checksum = 0;
        uint64_t previous = 0;
        uint64_t sink = 0;

        Print("Rng::Next64", BestNsPerCall(kDraws, checksum, [] { return DrawNext64(kDraws); }));
        EXPECT_EQ(checksum, DrawNext64(kDraws));
        sink += checksum;
        Print("mt19937_64 operator()", BestNsPerCall(kDraws, checksum, [] { return DrawMt19937(kDraws); }));
        sink += checksum;

        Print("Rng::UniformInt(0, 5)", BestNsPerCall(kDraws, checksum, [] { return DrawUniformInt(kDraws); }));
        previous = checksum;
        EXPECT_EQ(previous, DrawUniformInt(kDraws));
        sink += checksum;
        Print("uniform_int_distribution(0, 5)", BestNsPerCall(kDraws, checksum, [] { return DrawMtDistribution(kDraws); }));
        sink += checksum;

        Print("Rng::Shuffle of 52", BestNsPerCall(kShuffles, checksum, [] { return ShuffleDecks(kShuffles); }));
        previous = checksum;
        EXPECT_EQ(previous, ShuffleDecks(kShuffles));
        sink += checksum;
        Print("std::shuffle of 52", BestNsPerCall(kShuffles, checksum, [] { return ShuffleDecksMt(kShuffles); }));
        sink += checksum;

        EXPECT_NE(sink, 0u);
    }

}  // namespace BECore::Tests
