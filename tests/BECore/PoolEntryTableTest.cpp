#include <BECore/PoolString/PoolEntryTable.h>
#include <BECore/PoolString/PoolStringEntry.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using Table = PoolEntryTable<Details::PoolStringEntry>;

        constexpr size_t kSmall = 8;

    }  // namespace

    TEST(PoolEntryTableTest, AReserveThatWouldWrapTheDoublingReturnsAndChangesNothing) {
        Table table(kSmall);
        const size_t counts[] = {SIZE_MAX, SIZE_MAX - 1, SIZE_MAX / 2 + 1, SIZE_MAX / 2, size_t{1} << 63, Table::kMaxReservedEntries + 1};
        for (const size_t entries : counts) {
            SCOPED_TRACE(entries);
            table.Reserve(entries);
            EXPECT_EQ(table.Capacity(), kSmall);
            EXPECT_EQ(table.Used(), 0u);
        }
    }

    TEST(PoolEntryTableTest, AReserveUnderTheLimitGrowsOnceToAPowerOfTwoThatHoldsIt) {
        Table table(kSmall);
        table.Reserve(100);
        EXPECT_EQ(table.Capacity(), 256u);
        table.Reserve(50);
        EXPECT_EQ(table.Capacity(), 256u);
        table.Reserve(0);
        EXPECT_EQ(table.Capacity(), 256u);
    }

}  // namespace BECore::Tests
