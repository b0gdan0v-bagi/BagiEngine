#include <BECore/UI/UIRegions.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        const PoolString kLeft = "table.left"_intern;
        const PoolString kRight = "table.right"_intern;

        void ExpectRect(const Rect* rect, float x, float y, float w, float h) {
            ASSERT_NE(rect, nullptr);
            EXPECT_FLOAT_EQ(rect->x, x);
            EXPECT_FLOAT_EQ(rect->y, y);
            EXPECT_FLOAT_EQ(rect->w, w);
            EXPECT_FLOAT_EQ(rect->h, h);
        }

    }  // namespace

    TEST(UIRegionSetTest, ASetStartsEmpty) {
        const UIRegionSet set;

        EXPECT_TRUE(set.IsEmpty());
        EXPECT_EQ(set.Size(), 0u);
        EXPECT_EQ(set.Find(kLeft), nullptr);
    }

    TEST(UIRegionSetTest, SetPublishesARegionAndFindReturnsItsRect) {
        UIRegionSet set;
        set.Set(kLeft, {1.0f, 2.0f, 30.0f, 40.0f});
        set.Set(kRight, {5.0f, 6.0f, 70.0f, 80.0f});

        EXPECT_FALSE(set.IsEmpty());
        EXPECT_EQ(set.Size(), 2u);
        ExpectRect(set.Find(kLeft), 1.0f, 2.0f, 30.0f, 40.0f);
        ExpectRect(set.Find(kRight), 5.0f, 6.0f, 70.0f, 80.0f);
    }

    TEST(UIRegionSetTest, SetOfAKnownNameReplacesTheRectAndKeepsTheCount) {
        UIRegionSet set;
        set.Set(kLeft, {1.0f, 2.0f, 30.0f, 40.0f});

        set.Set(kLeft, {9.0f, 9.0f, 9.0f, 9.0f});

        EXPECT_EQ(set.Size(), 1u);
        ExpectRect(set.Find(kLeft), 9.0f, 9.0f, 9.0f, 9.0f);
    }

    TEST(UIRegionSetTest, AnEmptyNameIsNeverPublishedOrFound) {
        UIRegionSet set;

        set.Set(PoolString{}, {0.0f, 0.0f, 10.0f, 10.0f});

        EXPECT_TRUE(set.IsEmpty());
        EXPECT_EQ(set.Find(PoolString{}), nullptr);
    }

    TEST(UIRegionSetTest, ARegionWithNoAreaIsStillARegion) {
        UIRegionSet set;
        set.Set(kLeft, {4.0f, 4.0f, 0.0f, 0.0f});

        ExpectRect(set.Find(kLeft), 4.0f, 4.0f, 0.0f, 0.0f);
    }

    TEST(UIRegionSetTest, RemoveDropsOneRegionAndSaysWhetherItWasThere) {
        UIRegionSet set;
        set.Set(kLeft, {0.0f, 0.0f, 1.0f, 1.0f});
        set.Set(kRight, {0.0f, 0.0f, 2.0f, 2.0f});

        EXPECT_TRUE(set.Remove(kLeft));
        EXPECT_FALSE(set.Remove(kLeft));

        EXPECT_EQ(set.Size(), 1u);
        EXPECT_EQ(set.Find(kLeft), nullptr);
        EXPECT_NE(set.Find(kRight), nullptr);
    }

    TEST(UIRegionSetTest, ClearDropsEveryRegion) {
        UIRegionSet set;
        set.Set(kLeft, {0.0f, 0.0f, 1.0f, 1.0f});
        set.Set(kRight, {0.0f, 0.0f, 2.0f, 2.0f});

        set.Clear();

        EXPECT_TRUE(set.IsEmpty());
        EXPECT_EQ(set.Find(kRight), nullptr);
    }

    TEST(UIRegionSetTest, IterationVisitsEveryRegionOnce) {
        UIRegionSet set;
        set.Set(kLeft, {0.0f, 0.0f, 1.0f, 1.0f});
        set.Set(kRight, {0.0f, 0.0f, 2.0f, 2.0f});

        size_t count = 0;
        float areaSum = 0.0f;
        for (const UIRegionSet::Entry& entry : set) {
            ++count;
            areaSum += entry.rect.w;
        }

        EXPECT_EQ(count, 2u);
        EXPECT_FLOAT_EQ(areaSum, 3.0f);
    }

    TEST(UIRegionSetTest, EqualityDoesNotDependOnTheOrderOfPublishing) {
        UIRegionSet a;
        a.Set(kLeft, {0.0f, 0.0f, 1.0f, 1.0f});
        a.Set(kRight, {0.0f, 0.0f, 2.0f, 2.0f});
        UIRegionSet b;
        b.Set(kRight, {0.0f, 0.0f, 2.0f, 2.0f});
        b.Set(kLeft, {0.0f, 0.0f, 1.0f, 1.0f});

        EXPECT_TRUE(a == b);
        EXPECT_TRUE(b == a);
    }

    TEST(UIRegionSetTest, SetsDifferByARectByANameAndByACount) {
        UIRegionSet a;
        a.Set(kLeft, {0.0f, 0.0f, 1.0f, 1.0f});
        UIRegionSet otherRect;
        otherRect.Set(kLeft, {0.0f, 0.0f, 1.0f, 2.0f});
        UIRegionSet otherName;
        otherName.Set(kRight, {0.0f, 0.0f, 1.0f, 1.0f});
        UIRegionSet bigger = a;
        bigger.Set(kRight, {0.0f, 0.0f, 1.0f, 1.0f});

        EXPECT_FALSE(a == otherRect);
        EXPECT_FALSE(a == otherName);
        EXPECT_FALSE(a == bigger);
        EXPECT_FALSE(bigger == a);
        EXPECT_TRUE(UIRegionSet{} == UIRegionSet{});
    }

}  // namespace BECore::Tests
