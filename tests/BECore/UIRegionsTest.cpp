#include <BECore/UI/UIRegions.h>
#include <BECore/UI/UITreeLayout.h>
#include <BECore/UI/UIVariant.h>
#include <Common/Printers.h>
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

        void ExpectRect(const Rect& rect, float x, float y, float w, float h) {
            ExpectRect(&rect, x, y, w, h);
        }

        const Rect kCanvas{0.0f, 0.0f, 1280.0f, 720.0f};
        const Rect kPlate{100.0f, 200.0f, 300.0f, 100.0f};

        /// Lays a tree out as a canvas does: the active styles from the authored ones, then the rects
        void Layout(UIElement& root, const UIRegionSet& regions, const Rect& content = kCanvas, const Rect& full = kCanvas) {
            UIVariants::Apply(root, {}, full);
            UITreeLayout::Run(root, content, full, regions);
        }

        /// A child of @p parent of the given size that names a region
        IntrusivePtr<UIElement> AddInRegion(UIElement& parent, const char* name, PoolString region, float width, float height) {
            auto child = parent.AddChild(PoolString::Intern(name));
            child->style.rect.width = UILength::Px(width);
            child->style.rect.height = UILength::Px(height);
            child->style.rect.region = region;
            return child;
        }

        UIRegionSet PlateRegions() {
            UIRegionSet regions;
            regions.Set(kLeft, kPlate);
            return regions;
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

    // ---- the layout inside a region ---------------------------------------------------------------------------------------

    TEST(UIRegionLayoutTest, AnAnchorIsPinnedToThePointOfTheRegionNotOfTheParent) {
        auto root = New<UIElement>();
        auto element = AddInRegion(*root, "Plate", kLeft, 40.0f, 20.0f);
        element->style.rect.anchor = UIAnchor::BottomRight;
        element->style.rect.x = -12.0f;
        element->style.rect.y = -5.0f;

        Layout(*root, PlateRegions());

        ExpectRect(element->rect, 100.0f + 300.0f - 12.0f - 40.0f, 200.0f + 100.0f - 5.0f - 20.0f, 40.0f, 20.0f);
    }

    TEST(UIRegionLayoutTest, PercentSizesAreOfTheRegion) {
        auto root = New<UIElement>();
        auto element = AddInRegion(*root, "Plate", kLeft, 0.0f, 0.0f);
        element->style.rect.width = UILength::Percent(50.0f);
        element->style.rect.height = UILength::Percent(25.0f);

        Layout(*root, PlateRegions());

        ExpectRect(element->rect, 100.0f, 200.0f, 150.0f, 25.0f);
    }

    TEST(UIRegionLayoutTest, StretchFillsTheRegionMinusTheMargin) {
        auto root = New<UIElement>();
        auto element = AddInRegion(*root, "Plate", kLeft, 0.0f, 0.0f);
        element->style.rect.anchor = UIAnchor::Stretch;
        element->style.rect.margin = {10.0f, 5.0f, 20.0f, 15.0f};

        Layout(*root, PlateRegions());

        ExpectRect(element->rect, 110.0f, 205.0f, 270.0f, 80.0f);
    }

    TEST(UIRegionLayoutTest, AnUnpublishedRegionLeavesTheElementInItsParentAsIfItNamedNone) {
        auto root = New<UIElement>();
        auto named = AddInRegion(*root, "Named", kRight, 40.0f, 20.0f);
        auto none = AddInRegion(*root, "None", PoolString{}, 40.0f, 20.0f);
        for (const auto& element : {named, none}) {
            element->style.rect.anchor = UIAnchor::BottomRight;
            element->style.rect.x = -12.0f;
        }

        Layout(*root, PlateRegions());

        ExpectRect(named->rect, 1280.0f - 12.0f - 40.0f, 720.0f - 20.0f, 40.0f, 20.0f);
        ExpectRect(named->rect, none->rect.x, none->rect.y, none->rect.w, none->rect.h);
    }

    TEST(UIRegionLayoutTest, ARegionWithNoAreaGivesAZeroRect) {
        auto root = New<UIElement>();
        auto element = AddInRegion(*root, "Plate", kLeft, 0.0f, 0.0f);
        element->style.rect.anchor = UIAnchor::Stretch;
        UIRegionSet regions;
        regions.Set(kLeft, {50.0f, 60.0f, 0.0f, 0.0f});

        Layout(*root, regions);

        ExpectRect(element->rect, 50.0f, 60.0f, 0.0f, 0.0f);
    }

    TEST(UIRegionLayoutTest, ARegionWinsOverIgnoreSafeArea) {
        const Rect safe{0.0f, 40.0f, 1280.0f, 640.0f};
        auto root = New<UIElement>();
        auto inRegion = AddInRegion(*root, "InRegion", kLeft, 0.0f, 0.0f);
        inRegion->style.rect.anchor = UIAnchor::Stretch;
        inRegion->ignoreSafeArea = true;
        auto background = root->AddChild("Background"_intern);
        background->style.rect.anchor = UIAnchor::Stretch;
        background->ignoreSafeArea = true;

        Layout(*root, PlateRegions(), safe, kCanvas);

        ExpectRect(inRegion->rect, 100.0f, 200.0f, 300.0f, 100.0f);
        ExpectRect(background->rect, 0.0f, 0.0f, 1280.0f, 720.0f);
    }

    TEST(UIRegionLayoutTest, AChildOfAStackIsPlacedByTheStackAndItsRegionIsIgnored) {
        auto root = New<UIElement>();
        auto stack = root->AddChild("Stack"_intern);
        stack->layout = UILayoutKind::Stack;
        stack->style.stack.direction = UIStackDirection::Vertical;
        stack->style.rect.width = UILength::Px(200.0f);
        stack->style.rect.height = UILength::Px(200.0f);
        auto plain = stack->AddChild("Plain"_intern);
        plain->style.rect.width = UILength::Px(50.0f);
        plain->style.rect.height = UILength::Px(30.0f);
        auto named = AddInRegion(*stack, "Named", kLeft, 50.0f, 30.0f);

        Layout(*root, PlateRegions());

        ExpectRect(plain->rect, 0.0f, 0.0f, 50.0f, 30.0f);
        ExpectRect(named->rect, 0.0f, 30.0f, 50.0f, 30.0f);
    }

    TEST(UIRegionLayoutTest, AStackThatNamesARegionLaysItsChildrenOutInIt) {
        auto root = New<UIElement>();
        auto stack = AddInRegion(*root, "Stack", kLeft, 0.0f, 0.0f);
        stack->layout = UILayoutKind::Stack;
        stack->style.rect.anchor = UIAnchor::Stretch;
        stack->style.stack.direction = UIStackDirection::Horizontal;
        auto first = stack->AddChild("First"_intern);
        first->style.rect.width = UILength::Px(40.0f);
        first->style.rect.height = UILength::Px(30.0f);
        auto second = stack->AddChild("Second"_intern);
        second->style.rect.width = UILength::Px(40.0f);
        second->style.rect.height = UILength::Px(30.0f);

        Layout(*root, PlateRegions());

        ExpectRect(stack->rect, 100.0f, 200.0f, 300.0f, 100.0f);
        ExpectRect(first->rect, 100.0f, 200.0f, 40.0f, 30.0f);
        ExpectRect(second->rect, 140.0f, 200.0f, 40.0f, 30.0f);
    }

    TEST(UIRegionLayoutTest, AnElementUnderAnElementWithARegionFollowsItsParentNotTheRegion) {
        auto root = New<UIElement>();
        auto outer = AddInRegion(*root, "Outer", kLeft, 100.0f, 50.0f);
        auto inner = outer->AddChild("Inner"_intern);
        inner->style.rect.anchor = UIAnchor::BottomRight;
        inner->style.rect.width = UILength::Percent(50.0f);
        inner->style.rect.height = UILength::Px(10.0f);

        Layout(*root, PlateRegions());

        ExpectRect(outer->rect, 100.0f, 200.0f, 100.0f, 50.0f);
        ExpectRect(inner->rect, 150.0f, 240.0f, 50.0f, 10.0f);
    }

    TEST(UIRegionLayoutTest, WithoutARegionSetTheThreeArgumentRunLaysOutAsBefore) {
        auto root = New<UIElement>();
        auto element = AddInRegion(*root, "Plate", kLeft, 40.0f, 20.0f);
        UIVariants::Apply(*root, {}, kCanvas);

        UITreeLayout::Run(*root, kCanvas, kCanvas);

        ExpectRect(element->rect, 0.0f, 0.0f, 40.0f, 20.0f);
    }

}  // namespace BECore::Tests
