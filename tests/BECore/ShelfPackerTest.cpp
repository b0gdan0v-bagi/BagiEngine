#include <BECore/Text/ShelfPacker.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        struct Placed {
            uint32_t x, y, w, h;
        };

        bool Overlap(const Placed& a, const Placed& b) {
            return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
        }

    }  // namespace

    TEST(ShelfPackerTest, TheFirstRectGoesToTheCorner) {
        ShelfPacker packer(64, 64);

        const auto slot = packer.Pack(10, 12);

        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(slot->x, 0u);
        EXPECT_EQ(slot->y, 0u);
        EXPECT_EQ(packer.GetUsedHeight(), 12u);
    }

    TEST(ShelfPackerTest, RectsOfARowSitSideBySide) {
        ShelfPacker packer(64, 64);

        const auto first = packer.Pack(20, 10);
        const auto second = packer.Pack(20, 10);

        ASSERT_TRUE(first && second);
        EXPECT_EQ(second->x, 20u);
        EXPECT_EQ(second->y, first->y);
    }

    TEST(ShelfPackerTest, ARowThatIsFullOpensANewShelf) {
        ShelfPacker packer(40, 64);

        packer.Pack(30, 10);
        const auto next = packer.Pack(30, 10);

        ASSERT_TRUE(next.has_value());
        EXPECT_EQ(next->x, 0u);
        EXPECT_EQ(next->y, 10u);
    }

    TEST(ShelfPackerTest, ASmallRectFillsTheGapOfATallerShelf) {
        ShelfPacker packer(64, 64);
        packer.Pack(20, 30);

        const auto small = packer.Pack(10, 10);

        ASSERT_TRUE(small.has_value());
        EXPECT_EQ(small->y, 0u);  // on the first shelf, not below it
        EXPECT_EQ(small->x, 20u);
        EXPECT_EQ(packer.GetUsedHeight(), 30u);
    }

    TEST(ShelfPackerTest, ARectPrefersTheShelfThatWastesTheLeastHeight) {
        ShelfPacker packer(30, 100);
        packer.Pack(30, 40);  // shelf 0, full
        packer.Pack(10, 20);  // shelf 1, 20 high
        packer.Pack(30, 10);  // does not fit shelf 1 any more? it is 10 wide used: needs 30 -> shelf 2

        const auto slot = packer.Pack(10, 15);

        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(slot->y, 40u);  // the 20-high shelf, not the 10-high one that is too short
    }

    TEST(ShelfPackerTest, ARectThatDoesNotFitIsRefused) {
        ShelfPacker packer(32, 32);

        EXPECT_FALSE(packer.Pack(33, 4).has_value());
        EXPECT_FALSE(packer.Pack(4, 33).has_value());
        EXPECT_FALSE(packer.Pack(0, 4).has_value());
        EXPECT_FALSE(packer.Pack(4, 0).has_value());
    }

    TEST(ShelfPackerTest, WhenTheAreaIsFullTheNextRectIsRefused) {
        ShelfPacker packer(32, 32);
        ASSERT_TRUE(packer.Pack(32, 32).has_value());

        EXPECT_FALSE(packer.Pack(1, 1).has_value());
    }

    TEST(ShelfPackerTest, NothingOverlapsAndEverythingStaysInside) {
        ShelfPacker packer(128, 128);
        eastl::vector<Placed> placed;
        uint32_t seed = 12345;
        const auto next = [&seed](uint32_t limit) {
            seed = seed * 1664525u + 1013904223u;
            return 1 + (seed >> 16) % limit;
        };
        for (int i = 0; i < 200; ++i) {
            const uint32_t w = next(24);
            const uint32_t h = next(24);
            if (auto slot = packer.Pack(w, h)) {
                placed.push_back({slot->x, slot->y, w, h});
            }
        }

        ASSERT_GT(placed.size(), 20u);
        for (size_t i = 0; i < placed.size(); ++i) {
            EXPECT_LE(placed[i].x + placed[i].w, 128u);
            EXPECT_LE(placed[i].y + placed[i].h, 128u);
            for (size_t j = i + 1; j < placed.size(); ++j) {
                EXPECT_FALSE(Overlap(placed[i], placed[j])) << i << " and " << j;
            }
        }
    }

}  // namespace BECore::Tests
