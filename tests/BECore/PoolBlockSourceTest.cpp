#include <BECore/PoolString/PoolBlockSource.h>
#include <BECore/PoolString/PoolString.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kChunk = size_t{64} * 1024;
        constexpr size_t kSmallChunks = 4;

        uint8_t PatternByte(size_t seed, size_t index) {
            return static_cast<uint8_t>(seed * 31 + index * 7 + (index >> 8));
        }

        void FillPattern(const PoolMemory& memory, size_t seed) {
            auto* bytes = static_cast<uint8_t*>(memory.data);
            for (size_t i = 0; i < memory.bytes; ++i) {
                bytes[i] = PatternByte(seed, i);
            }
        }

        /// Counts the bytes of the range that differ from the pattern of `seed`
        size_t PatternMismatches(const PoolMemory& memory, size_t seed) {
            const auto* bytes = static_cast<const uint8_t*>(memory.data);
            size_t mismatches = 0;
            for (size_t i = 0; i < memory.bytes; ++i) {
                mismatches += bytes[i] != PatternByte(seed, i) ? 1 : 0;
            }
            return mismatches;
        }

        /// Allocates ranges of the given sizes; each must be aligned to 8, writable end to end and apart from the others
        void ExpectUsableRanges(IPoolBlockSource& source, std::initializer_list<size_t> sizes) {
            Vector<PoolMemory> ranges;
            size_t seed = 1;
            for (const size_t size : sizes) {
                const PoolMemory memory = source.Allocate(size);
                ASSERT_NE(memory.data, nullptr) << size;
                ASSERT_GE(memory.bytes, size);
                EXPECT_EQ(reinterpret_cast<uintptr_t>(memory.data) % 8, 0u) << size;
                FillPattern(memory, seed++);
                ranges.push_back(memory);
            }
            seed = 1;
            for (const PoolMemory& memory : ranges) {
                EXPECT_EQ(PatternMismatches(memory, seed++), 0u) << memory.bytes;
            }
            for (size_t a = 0; a < ranges.size(); ++a) {
                for (size_t b = a + 1; b < ranges.size(); ++b) {
                    const auto beginA = reinterpret_cast<uintptr_t>(ranges[a].data);
                    const auto beginB = reinterpret_cast<uintptr_t>(ranges[b].data);
                    EXPECT_TRUE(beginA + ranges[a].bytes <= beginB || beginB + ranges[b].bytes <= beginA) << a << " overlaps " << b;
                }
            }
        }

        constexpr std::initializer_list<size_t> kMixedSizes = {8, 16, 24, 4096, 1000 * 8, kChunk, kChunk + 8, 3 * kChunk + 64, 8, 128 * 1024};

    }  // namespace

    TEST(PoolBlockSourceTest, HeapRangesAreAlignedWritableAndApart) {
        UniquePtr<IPoolBlockSource> source = MakeHeapBlockSource();
        ASSERT_NE(source, nullptr);
        ExpectUsableRanges(*source, kMixedSizes);
    }

    TEST(PoolBlockSourceTest, VirtualRangesAreAlignedWritableAndApart) {
        UniquePtr<IPoolBlockSource> source = MakeVirtualBlockSource(size_t{64} * 1024 * 1024, kChunk);
        ASSERT_NE(source, nullptr);
        ExpectUsableRanges(*source, kMixedSizes);
    }

    TEST(PoolBlockSourceTest, VirtualRangesAreAdjacent) {
        UniquePtr<IPoolBlockSource> source = MakeVirtualBlockSource(size_t{16} * kChunk, kChunk);
        ASSERT_NE(source, nullptr);
        const PoolMemory first = source->Allocate(24);
        const PoolMemory second = source->Allocate(4096);
        const PoolMemory third = source->Allocate(8);
        ASSERT_NE(first.data, nullptr);
        EXPECT_EQ(static_cast<char*>(first.data) + first.bytes, static_cast<char*>(second.data));
        EXPECT_EQ(static_cast<char*>(second.data) + second.bytes, static_cast<char*>(third.data));
    }

    TEST(PoolBlockSourceTest, VirtualAllocationAcrossACommitBoundaryIsWritable) {
        UniquePtr<IPoolBlockSource> source = MakeVirtualBlockSource(size_t{16} * kChunk, kChunk);
        ASSERT_NE(source, nullptr);
        // The first range ends 8 bytes before the end of the committed chunk, the second one starts inside it
        const PoolMemory first = source->Allocate(kChunk - 8);
        const PoolMemory crossing = source->Allocate(64);
        const PoolMemory larger = source->Allocate(3 * kChunk + 8);
        ASSERT_NE(first.data, nullptr);
        ASSERT_NE(crossing.data, nullptr);
        ASSERT_NE(larger.data, nullptr);
        FillPattern(first, 1);
        FillPattern(crossing, 2);
        FillPattern(larger, 3);
        EXPECT_EQ(PatternMismatches(first, 1), 0u);
        EXPECT_EQ(PatternMismatches(crossing, 2), 0u);
        EXPECT_EQ(PatternMismatches(larger, 3), 0u);
    }

    TEST(PoolBlockSourceTest, VirtualSourceIsExhaustedWithAnEmptyResultAndKeepsEarlierRanges) {
        UniquePtr<IPoolBlockSource> source = MakeVirtualBlockSource(kSmallChunks * kChunk, kChunk);
        ASSERT_NE(source, nullptr);
        const PoolMemory first = source->Allocate(2 * kChunk);
        const PoolMemory second = source->Allocate(kChunk + kChunk / 2);
        ASSERT_NE(first.data, nullptr);
        ASSERT_NE(second.data, nullptr);
        FillPattern(first, 1);
        FillPattern(second, 2);

        // Half a chunk is left: a request that does not fit it is empty, one that does fit is served
        const PoolMemory tooMuch = source->Allocate(kChunk / 2 + 8);
        EXPECT_EQ(tooMuch.data, nullptr);
        EXPECT_EQ(tooMuch.bytes, 0u);
        const PoolMemory last = source->Allocate(kChunk / 2);
        ASSERT_NE(last.data, nullptr);
        FillPattern(last, 3);
        EXPECT_EQ(source->Allocate(8).data, nullptr);
        EXPECT_EQ(source->Allocate(8).data, nullptr);

        EXPECT_EQ(PatternMismatches(first, 1), 0u);
        EXPECT_EQ(PatternMismatches(second, 2), 0u);
        EXPECT_EQ(PatternMismatches(last, 3), 0u);
    }

    TEST(PoolBlockSourceTest, VirtualRequestLargerThanTheWholeReservationIsEmptyAtOnce) {
        UniquePtr<IPoolBlockSource> source = MakeVirtualBlockSource(kSmallChunks * kChunk, kChunk);
        ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->Allocate(kSmallChunks * kChunk + 8).data, nullptr);
        EXPECT_EQ(source->Allocate(SIZE_MAX / 8 * 8).data, nullptr);
        // The refused requests took nothing: the whole reservation is still there
        const PoolMemory whole = source->Allocate(kSmallChunks * kChunk);
        ASSERT_NE(whole.data, nullptr);
        FillPattern(whole, 1);
        EXPECT_EQ(PatternMismatches(whole, 1), 0u);
    }

    TEST(PoolBlockSourceTest, VirtualReservationThatIsNotAWholeNumberOfChunksStopsAtTheRequestedSize) {
        UniquePtr<IPoolBlockSource> source = MakeVirtualBlockSource(kChunk + 4096, kChunk);
        ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->Allocate(kChunk + 4096 + 8).data, nullptr);
        const PoolMemory all = source->Allocate(kChunk + 4096);
        ASSERT_NE(all.data, nullptr);
        FillPattern(all, 1);
        EXPECT_EQ(PatternMismatches(all, 1), 0u);
        EXPECT_EQ(source->Allocate(8).data, nullptr);
    }

    TEST(PoolBlockSourceTest, DefaultSourceServesAStandardBlock) {
        UniquePtr<IPoolBlockSource> source = MakeDefaultBlockSource();
        ASSERT_NE(source, nullptr);
        const PoolMemory block = source->Allocate(PoolString::kBlockBytes);
        ASSERT_NE(block.data, nullptr);
        ASSERT_GE(block.bytes, PoolString::kBlockBytes);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(block.data) % 8, 0u);
        FillPattern(block, 1);
        EXPECT_EQ(PatternMismatches(block, 1), 0u);
    }

}  // namespace BECore::Tests
