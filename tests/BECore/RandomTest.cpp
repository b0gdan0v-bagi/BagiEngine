#include <BECore/Random/Rng.h>
#include <cstring>
#include <gtest/gtest.h>
#include <type_traits>

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

}  // namespace BECore::Tests
