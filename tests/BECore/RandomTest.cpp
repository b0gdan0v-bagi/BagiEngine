#include <BECore/Random/Rng.h>
#include <gtest/gtest.h>
#include <numeric>

namespace BECore::Tests {

    // The expected numbers are the SplitMix64 stream of Vigna's reference (state += 0x9E3779B97F4A7C15, then the
    // xor-shift-multiply finalizer), worked out independently with 64-bit wrap-around in a script; the draws of
    // `Rng(seed)` are the outputs of that stream, one per step.

    static_assert(sizeof(Rng) == 16);
    static_assert(std::is_trivially_copyable_v<Rng>);
    static_assert(std::is_standard_layout_v<Rng>);
    static_assert(std::has_unique_object_representations_v<Rng>);
    static_assert(Rng(0).At(1) == 0xe220a8397b1dcdafULL);
    static_assert(Rng(7).At(10) == 0x69c3a27688795369ULL);

    TEST(RandomTest, CompileTimeChecks) {}

    TEST(RandomTest, Seed0MatchesPublishedSplitMix64) {
        Rng rng(0);
        EXPECT_EQ(rng.Next64(), 0xe220a8397b1dcdafULL);
        EXPECT_EQ(rng.Next64(), 0x6e789e6aa1b965f4ULL);
        EXPECT_EQ(rng.Next64(), 0x06c45d188009454fULL);
        EXPECT_EQ(rng.Next64(), 0xf88bb8a8724c81ecULL);
        EXPECT_EQ(rng.Next64(), 0x1b39896a51a8749bULL);
        EXPECT_EQ(rng.Counter(), 5u);
        EXPECT_EQ(rng.Seed(), 0u);
    }

    TEST(RandomTest, DefaultSeedIsZero) {
        Rng defaulted;
        Rng zero(0);
        EXPECT_EQ(defaulted, zero);
        EXPECT_EQ(defaulted.Next64(), 0xe220a8397b1dcdafULL);
    }

    TEST(RandomTest, Seed1) {
        Rng rng(1);
        EXPECT_EQ(rng.Next64(), 0x910a2dec89025cc1ULL);
        EXPECT_EQ(rng.Next64(), 0xbeeb8da1658eec67ULL);
        EXPECT_EQ(rng.Next64(), 0xf893a2eefb32555eULL);
        EXPECT_EQ(rng.Next64(), 0x71c18690ee42c90bULL);
        EXPECT_EQ(rng.Next64(), 0x71bb54d8d101b5b9ULL);
    }

    TEST(RandomTest, LargeSeed) {
        Rng rng(0xdeadbeefcafef00dULL);
        EXPECT_EQ(rng.Next64(), 0x901d4f652fb472cbULL);
        EXPECT_EQ(rng.Next64(), 0xa7ce246440f74527ULL);
        EXPECT_EQ(rng.Next64(), 0x19b40bbbb9380d34ULL);
        EXPECT_EQ(rng.Next64(), 0xe7a86dc5be618392ULL);
        EXPECT_EQ(rng.Next64(), 0x7366ce945d00e82cULL);
    }

    TEST(RandomTest, SkipThousandDraws) {
        Rng zero(0);
        zero.Skip(1000);
        EXPECT_EQ(zero.Counter(), 1000u);
        EXPECT_EQ(zero.Next64(), 0x2cfa2f23425329e1ULL);
        EXPECT_EQ(zero.Next64(), 0xdcad513c211f942dULL);
        EXPECT_EQ(zero.Next64(), 0x7b9b825c35c18b75ULL);
        EXPECT_EQ(zero.Next64(), 0xad1d5088c022a188ULL);
        EXPECT_EQ(zero.Next64(), 0xf804f37aa5267870ULL);

        Rng one(1);
        one.Skip(1000);
        EXPECT_EQ(one.Next64(), 0x7760003b54a685aeULL);
        EXPECT_EQ(one.Next64(), 0x22b93765ac22b99eULL);

        Rng large(0xdeadbeefcafef00dULL);
        large.Skip(1000);
        EXPECT_EQ(large.Next64(), 0x63dce22580fe6fb5ULL);
        EXPECT_EQ(large.Next64(), 0x365c83e42a540452ULL);
    }

    TEST(RandomTest, SkipEqualsDrawing) {
        Rng drawn(5);
        for (int i = 0; i < 37; ++i) {
            (void)drawn.Next64();
        }
        Rng skipped(5);
        skipped.Skip(37);
        EXPECT_EQ(drawn, skipped);
        EXPECT_EQ(drawn.Next64(), skipped.Next64());
    }

    TEST(RandomTest, AtIsRandomAccessAndDoesNotMove) {
        const Rng rng(7);
        EXPECT_EQ(rng.At(1), 0x63cbe1e459320dd7ULL);
        EXPECT_EQ(rng.At(2), 0x044c3cd7f43c661cULL);
        EXPECT_EQ(rng.At(10), 0x69c3a27688795369ULL);
        EXPECT_EQ(rng.At(1000), 0x9793fb91046fffeeULL);
        EXPECT_EQ(rng.Counter(), 0u);

        Rng stream(7);
        for (uint64_t n = 1; n <= 20; ++n) {
            EXPECT_EQ(stream.Next64(), rng.At(n)) << "n=" << n;
        }
    }

    TEST(RandomTest, WrapAroundAtTheEdges) {
        const Rng rng(0xffffffffffffffffULL);
        EXPECT_EQ(rng.At(0xffffffffffffffffULL), 0xde0a564cbcd060c4ULL);
        EXPECT_EQ(rng.At(2), 0xe99ff867dbf682c9ULL);
    }

    TEST(RandomTest, MemcpyCopyContinuesTheSameStream) {
        Rng original(0xdeadbeefcafef00dULL);
        original.Skip(3);
        (void)original.Next64();

        Rng restored(0);
        std::memcpy(static_cast<void*>(&restored), static_cast<const void*>(&original), sizeof(Rng));
        EXPECT_EQ(restored, original);
        EXPECT_EQ(restored.Seed(), 0xdeadbeefcafef00dULL);
        EXPECT_EQ(restored.Counter(), 4u);
        for (int i = 0; i < 10; ++i) {
            EXPECT_EQ(restored.Next64(), original.Next64());
        }
    }

    TEST(RandomTest, EqualityComparesSeedAndCounter) {
        Rng a(3);
        Rng b(3);
        EXPECT_EQ(a, b);
        (void)a.Next64();
        EXPECT_NE(a, b);
        (void)b.Next64();
        EXPECT_EQ(a, b);
        EXPECT_NE(Rng(3), Rng(4));
    }

    TEST(RandomTest, Next32IsTheHighHalfOfOneDraw) {
        Rng rng(42);
        EXPECT_EQ(rng.Next32(), 0xbdd73226u);
        EXPECT_EQ(rng.Next32(), 0x28efe333u);
        EXPECT_EQ(rng.Next32(), 0x47526757u);
        EXPECT_EQ(rng.Next32(), 0x581ce1ffu);
        EXPECT_EQ(rng.Next32(), 0x09bc585au);
        EXPECT_EQ(rng.Next32(), 0xde4431fau);
        EXPECT_EQ(rng.Counter(), 6u);

        Rng wide(42);
        Rng narrow(42);
        for (int i = 0; i < 20; ++i) {
            EXPECT_EQ(narrow.Next32(), static_cast<uint32_t>(wide.Next64() >> 32));
        }
    }

    TEST(RandomTest, Uniform01ExactValues) {
        Rng rng(42);
        EXPECT_EQ(rng.Uniform01(), 0x1.7bae644c5fd6dp-1);
        EXPECT_EQ(rng.Uniform01(), 0x1.477f199d93378p-3);
        EXPECT_EQ(rng.Uniform01(), 0x1.1d499d5c4c3e6p-2);
        EXPECT_EQ(rng.Uniform01(), 0x1.607387fc392b8p-2);
        EXPECT_EQ(rng.Uniform01(), 0x1.378b0b4489040p-5);
        EXPECT_EQ(rng.Uniform01(), 0x1.bc8863f47901bp-1);
        EXPECT_EQ(rng.Counter(), 6u);

        Rng zero(0);
        EXPECT_EQ(zero.Uniform01(), 0x1.c4415072f63b9p-1);
        EXPECT_EQ(zero.Uniform01(), 0x1.b9e279aa86e58p-2);
        EXPECT_EQ(zero.Uniform01(), 0x1.b117462002500p-6);
    }

    TEST(RandomTest, Uniform01StaysInHalfOpenUnitInterval) {
        Rng rng(2024);
        for (int i = 0; i < 100000; ++i) {
            const double v = rng.Uniform01();
            ASSERT_GE(v, 0.0);
            ASSERT_LT(v, 1.0);
        }
        EXPECT_EQ(rng.Counter(), 100000u);
    }

    // UniformInt: `lo + x % range` of the first draw `x` that is not below `2^64 mod range`; the sequences below are from an
    // independent implementation of that rule.

    TEST(RandomTest, UniformIntZeroToFive) {
        constexpr Array<int, 20> expected{5, 1, 0, 5, 3, 2, 3, 3, 0, 4, 3, 4, 2, 4, 4, 5, 3, 5, 2, 0};
        Rng rng(1);
        for (const int value : expected) {
            EXPECT_EQ(rng.UniformInt(0, 5), value);
        }
        EXPECT_EQ(rng.Counter(), 20u);
    }

    TEST(RandomTest, UniformIntNegativeRange) {
        constexpr Array<int, 20> expected{1, -3, -3, -2, 0, -1, -3, 2, -1, -2, -3, -2, -3, 2, 3, 2, 3, 0, -1, -1};
        Rng rng(2);
        for (const int value : expected) {
            EXPECT_EQ(rng.UniformInt(-3, 3), value);
        }
        EXPECT_EQ(rng.Counter(), 20u);
    }

    TEST(RandomTest, UniformIntOfOneValueDrawsOnceEach) {
        Rng rng(3);
        for (int i = 0; i < 10; ++i) {
            EXPECT_EQ(rng.UniformInt(5, 5), 5);
        }
        EXPECT_EQ(rng.Counter(), 10u);
        EXPECT_EQ(rng.UniformInt<uint8_t>(7, 7), 7);
        EXPECT_EQ(rng.UniformInt<int64_t>(-9, -9), -9);
        EXPECT_EQ(rng.Counter(), 12u);
    }

    TEST(RandomTest, UniformIntFullSignedRangeIsTheDrawAsIs) {
        constexpr Array<int64_t, 5> expected{7958955049054603978LL, -1984743375926415312LL, -2598829887456574369LL, 9071633986856679582LL, 7278725300257082041LL};
        Rng rng(4);
        for (const int64_t value : expected) {
            EXPECT_EQ(rng.UniformInt<int64_t>(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()), value);
        }
        EXPECT_EQ(rng.Counter(), 5u);
    }

    TEST(RandomTest, UniformIntFullUnsignedRangeIsTheDrawAsIs) {
        constexpr Array<uint64_t, 5> expected{0x6e73e372e2338acaULL, 0xe474c66a4b98b030ULL, 0xdbef19fc8e7b845fULL, 0x7de4eb0c26f3f89eULL, 0x65033a18a378cab9ULL};
        Rng rng(4);
        for (const uint64_t value : expected) {
            EXPECT_EQ(rng.UniformInt<uint64_t>(0, std::numeric_limits<uint64_t>::max()), value);
        }
        EXPECT_EQ(rng.Counter(), 5u);
    }

    TEST(RandomTest, UniformIntRangeNear2Pow63RejectsOften) {
        // range 2^63 + 1: a draw is rejected with probability close to one half (20 results took 46 draws)
        constexpr Array<uint64_t, 20> expected{4654242949169100535ULL, 8957066056171264800ULL, 204786321411665706ULL,  1908141438795372386ULL, 6903704994719435514ULL,
                                               8387343232142502422ULL, 8097663149109484117ULL, 6107755222391796074ULL, 8992142949797167562ULL, 6929563570622903022ULL,
                                               3922199910016846850ULL, 8049559783456564693ULL, 6350574005908689845ULL, 8807188270585211006ULL, 8683844339055248014ULL,
                                               3850321435793303524ULL, 535584032798730017ULL,  2377898451075746561ULL, 2794872976919844752ULL, 4833303329173688674ULL};
        Rng rng(5);
        for (const uint64_t value : expected) {
            EXPECT_EQ(rng.UniformInt<uint64_t>(0, uint64_t{1} << 63), value);
        }
        EXPECT_EQ(rng.Counter(), 46u);
    }

    TEST(RandomTest, UniformIntRejectedDrawMovesTheCounterByTwo) {
        // Seed 3: draw 1 is 0x1d0b14e4db018fed (below the limit 2^63 - 1 of range 2^63 + 1, rejected), draw 2 is
        // 0xb3466f8a7b81a989 (taken).
        Rng rng(3);
        EXPECT_EQ(rng.UniformInt<uint64_t>(0, uint64_t{1} << 63), 3694763184872335752ULL);
        EXPECT_EQ(rng.Counter(), 2u);
    }

    TEST(RandomTest, UniformIntIsInsideTheBoundsForNarrowTypes) {
        Rng rng(77);
        for (int i = 0; i < 5000; ++i) {
            const int8_t narrow = rng.UniformInt<int8_t>(-100, 100);
            ASSERT_GE(narrow, -100);
            ASSERT_LE(narrow, 100);
            const int64_t wide = rng.UniformInt<int64_t>(std::numeric_limits<int64_t>::min() + 1, std::numeric_limits<int64_t>::min() + 3);
            ASSERT_GE(wide, std::numeric_limits<int64_t>::min() + 1);
            ASSERT_LE(wide, std::numeric_limits<int64_t>::min() + 3);
        }
    }

    TEST(RandomTest, UniformIntHasNoModuloBiasSmoke) {
        // 600000 draws over 6 faces (the exact counts are from the independent implementation; the band is the smoke)
        constexpr Array<int, 6> expected{100066, 100300, 99840, 99745, 100138, 99911};
        Rng rng(2024);
        Array<int, 6> counts{};
        for (int i = 0; i < 600000; ++i) {
            ++counts[static_cast<size_t>(rng.UniformInt(0, 5))];
        }
        for (size_t face = 0; face < counts.size(); ++face) {
            EXPECT_EQ(counts[face], expected[face]) << "face " << face;
            EXPECT_NEAR(counts[face], 100000, 2000) << "face " << face;
        }
        EXPECT_EQ(rng.Counter(), 600000u);
    }

    TEST(RandomTest, ChanceBeyondTheBoundsDrawsNothing) {
        Rng rng(5);
        EXPECT_FALSE(rng.Chance(0.0));
        EXPECT_FALSE(rng.Chance(-1.0));
        EXPECT_FALSE(rng.Chance(std::numeric_limits<double>::quiet_NaN()));
        EXPECT_TRUE(rng.Chance(1.0));
        EXPECT_TRUE(rng.Chance(2.5));
        EXPECT_TRUE(rng.Chance(std::numeric_limits<double>::infinity()));
        EXPECT_EQ(rng.Counter(), 0u);
    }

    TEST(RandomTest, ChanceHalfAndTenth) {
        constexpr Array<bool, 20> half{true, false, true, true, true, true, false, false, true, false, true, true, false, true, false, false, false, true, true, true};
        Rng halfRng(5);
        for (const bool value : half) {
            EXPECT_EQ(halfRng.Chance(0.5), value);
        }
        EXPECT_EQ(halfRng.Counter(), 20u);

        Rng tenth(5);
        for (int i = 0; i < 20; ++i) {
            EXPECT_EQ(tenth.Chance(0.1), i == 3) << "draw " << i;
        }
        EXPECT_EQ(tenth.Counter(), 20u);
    }

    TEST(RandomTest, WeightedPickNeverTakesAZeroWeight) {
        constexpr Array<uint32_t, 4> weights{3, 0, 5, 2};
        constexpr Array<size_t, 20> expected{0, 2, 2, 3, 2, 0, 0, 0, 2, 0, 2, 2, 0, 2, 2, 0, 3, 0, 3, 2};
        Rng rng(6);
        for (const size_t index : expected) {
            const Optional<size_t> picked = rng.WeightedPick(Span<const uint32_t>(weights));
            ASSERT_TRUE(picked.has_value());
            EXPECT_EQ(*picked, index);
        }
        EXPECT_EQ(rng.Counter(), 20u);
    }

    TEST(RandomTest, WeightedPickOfNothingDrawsNothing) {
        Rng rng(6);
        const Vector<uint32_t> empty;
        const Vector<uint32_t> zeros{0, 0, 0};
        EXPECT_FALSE(rng.WeightedPick(Span<const uint32_t>(empty)).has_value());
        EXPECT_FALSE(rng.WeightedPick(Span<const uint32_t>(zeros)).has_value());
        EXPECT_EQ(rng.Counter(), 0u);
    }

    TEST(RandomTest, WeightedPickOfOneWeightDrawsOnce) {
        constexpr Array<uint32_t, 1> single{7};
        Rng rng(7);
        for (int i = 1; i <= 3; ++i) {
            EXPECT_EQ(rng.WeightedPick(Span<const uint32_t>(single)), Optional<size_t>{0});
            EXPECT_EQ(rng.Counter(), static_cast<uint64_t>(i));
        }

        constexpr Array<uint32_t, 5> oneNonZero{0, 0, 0, 1, 0};
        Rng other(9);
        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(other.WeightedPick(Span<const uint32_t>(oneNonZero)), Optional<size_t>{3});
        }
    }

    TEST(RandomTest, WeightedPickSumsInSixtyFourBits) {
        // 3 * (2^32 - 1) does not fit in 32 bits
        constexpr Array<uint32_t, 3> weights{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        constexpr Array<size_t, 20> expected{1, 0, 1, 1, 1, 0, 0, 1, 0, 0, 2, 1, 0, 0, 2, 0, 0, 0, 0, 0};
        Rng rng(8);
        for (const size_t index : expected) {
            EXPECT_EQ(rng.WeightedPick(Span<const uint32_t>(weights)), Optional<size_t>{index});
        }
        EXPECT_EQ(rng.Counter(), 20u);
    }

    TEST(RandomTest, ShuffleOfFewElementsDrawsAsExpected) {
        Rng rng(1);
        Array<int, 0> none{};
        rng.Shuffle(Span<int>(none));
        Array<int, 1> one{42};
        rng.Shuffle(Span<int>(one));
        EXPECT_EQ(one[0], 42);
        EXPECT_EQ(rng.Counter(), 0u);

        Array<int, 2> swapped{0, 1};
        Rng first(10);
        first.Shuffle(Span<int>(swapped));
        EXPECT_EQ(swapped, (Array<int, 2>{1, 0}));
        EXPECT_EQ(first.Counter(), 1u);

        Array<int, 2> kept{0, 1};
        Rng second(11);
        second.Shuffle(Span<int>(kept));
        EXPECT_EQ(kept, (Array<int, 2>{0, 1}));
        EXPECT_EQ(second.Counter(), 1u);
    }

    TEST(RandomTest, ShuffleOfTenElements) {
        Array<int, 10> first{};
        std::iota(first.begin(), first.end(), 0);
        Rng rngFirst(13);
        rngFirst.Shuffle(Span<int>(first));
        EXPECT_EQ(first, (Array<int, 10>{8, 1, 2, 4, 9, 3, 7, 0, 6, 5}));
        EXPECT_EQ(rngFirst.Counter(), 9u);

        Array<int, 10> second{};
        std::iota(second.begin(), second.end(), 0);
        Rng rngSecond(14);
        rngSecond.Shuffle(Span<int>(second));
        EXPECT_EQ(second, (Array<int, 10>{4, 1, 7, 3, 9, 5, 0, 2, 6, 8}));
        EXPECT_EQ(rngSecond.Counter(), 9u);
    }

    TEST(RandomTest, ShuffleOfStringsThroughSpan) {
        Vector<String> items{"a", "bb", "ccc", "dddd", "e"};
        Rng rng(15);
        rng.Shuffle(Span<String>(items));
        EXPECT_EQ(items, (Vector<String>{"dddd", "e", "ccc", "a", "bb"}));
        EXPECT_EQ(rng.Counter(), 4u);
    }

    TEST(RandomTest, ShuffleOfArrayThroughSpan) {
        Array<int, 7> items{10, 20, 30, 40, 50, 60, 70};
        Rng rng(16);
        rng.Shuffle(Span<int>(items));
        EXPECT_EQ(items, (Array<int, 7>{50, 10, 60, 70, 40, 20, 30}));
        EXPECT_EQ(rng.Counter(), 6u);
    }

    TEST(RandomTest, ShuffleKeepsAPermutation) {
        Array<int, 52> deck{};
        std::iota(deck.begin(), deck.end(), 0);
        Rng rng(99);
        for (int round = 0; round < 20; ++round) {
            rng.Shuffle(Span<int>(deck));
            Array<int, 52> sorted = deck;
            std::sort(sorted.begin(), sorted.end());
            for (int i = 0; i < 52; ++i) {
                ASSERT_EQ(sorted[static_cast<size_t>(i)], i) << "round " << round;
            }
        }
    }

}  // namespace BECore::Tests
