#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/UI/UILayout.h>
#include <BECore/UI/UITreeLayout.h>
#include <BECore/UI/UIVariant.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        void ExpectRect(const Rect& rect, float x, float y, float w, float h) {
            EXPECT_NEAR(rect.x, x, kTolerance);
            EXPECT_NEAR(rect.y, y, kTolerance);
            EXPECT_NEAR(rect.w, w, kTolerance);
            EXPECT_NEAR(rect.h, h, kTolerance);
        }

        const Rect kLandscape{0.0f, 0.0f, 1280.0f, 720.0f};
        const Rect kPortrait{0.0f, 0.0f, 720.0f, 1280.0f};

        /// The HUD of the plan's example: a 220 x 96 panel top left with a score label and a reset button inside
        IntrusivePtr<UIElement> MakeHudTree() {
            auto root = New<UIElement>();
            root->name = "Root"_intern;
            auto hud = root->AddChild("Hud"_intern);
            hud->style.rect.anchor = UIAnchor::TopLeft;
            hud->style.rect.x = UILength::Px(16.0f);
            hud->style.rect.y = UILength::Px(16.0f);
            hud->style.rect.width = UILength::Px(220.0f);
            hud->style.rect.height = UILength::Px(96.0f);
            auto score = hud->AddChild("Score"_intern);
            score->style.rect.x = UILength::Px(12.0f);
            score->style.rect.y = UILength::Px(10.0f);
            score->style.rect.width = UILength::Px(120.0f);
            score->style.rect.height = UILength::Px(28.0f);
            auto reset = hud->AddChild("Reset"_intern);
            reset->style.rect.anchor = UIAnchor::BottomRight;
            reset->style.rect.x = UILength::Px(-12.0f);
            reset->style.rect.y = UILength::Px(-10.0f);
            reset->style.rect.width = UILength::Px(96.0f);
            reset->style.rect.height = UILength::Px(32.0f);
            return root;
        }

        UIVariant PortraitHudVariant() {
            UIVariant variant;
            variant.when.orientation = UIOrientation::Portrait;
            UIElementOverride hud;
            hud.element = "Hud"_intern;
            hud.anchor = UIAnchor::Top;
            hud.x = UILength::Px(0.0f);
            hud.width = UILength::Percent(90.0f);
            variant.overrides.push_back(hud);
            return variant;
        }

    }  // namespace

    // ---- conditions -------------------------------------------------------------------------------------------------------

    TEST(UIVariantConditionTest, AnEmptyConditionMatchesAnyBounds) {
        const UIVariantCondition condition;

        EXPECT_TRUE(condition.Matches(kLandscape));
        EXPECT_TRUE(condition.Matches(kPortrait));
        EXPECT_TRUE(condition.Matches({0.0f, 0.0f, 0.0f, 0.0f}));
    }

    TEST(UIVariantConditionTest, OrientationFollowsTheShapeAndASquareIsPortrait) {
        UIVariantCondition portrait;
        portrait.orientation = UIOrientation::Portrait;
        UIVariantCondition landscape;
        landscape.orientation = UIOrientation::Landscape;

        EXPECT_TRUE(portrait.Matches(kPortrait));
        EXPECT_FALSE(portrait.Matches(kLandscape));
        EXPECT_TRUE(landscape.Matches(kLandscape));
        EXPECT_FALSE(landscape.Matches(kPortrait));
        EXPECT_TRUE(portrait.Matches({0.0f, 0.0f, 500.0f, 500.0f}));
        EXPECT_FALSE(landscape.Matches({0.0f, 0.0f, 500.0f, 500.0f}));
    }

    TEST(UIVariantConditionTest, AspectRangeIncludesItsEnds) {
        UIVariantCondition condition;
        condition.minAspect = 1.0f;
        condition.maxAspect = 2.0f;

        EXPECT_TRUE(condition.Matches({0.0f, 0.0f, 1000.0f, 1000.0f}));  // 1.0
        EXPECT_TRUE(condition.Matches({0.0f, 0.0f, 1000.0f, 500.0f}));   // 2.0
        EXPECT_TRUE(condition.Matches({0.0f, 0.0f, 1500.0f, 1000.0f}));
        EXPECT_FALSE(condition.Matches({0.0f, 0.0f, 900.0f, 1000.0f}));
        EXPECT_FALSE(condition.Matches({0.0f, 0.0f, 2100.0f, 1000.0f}));
        EXPECT_FALSE(condition.Matches({0.0f, 0.0f, 100.0f, 0.0f}));  // no height, no aspect
    }

    TEST(UIVariantConditionTest, AnOpenEndedRangeHasOnlyOneLimit) {
        UIVariantCondition wide;
        wide.minAspect = 2.0f;
        EXPECT_TRUE(wide.Matches({0.0f, 0.0f, 3200.0f, 900.0f}));
        EXPECT_FALSE(wide.Matches(kLandscape));  // 1.78

        UIVariantCondition notTall;
        notTall.maxAspect = 1.0f;
        EXPECT_TRUE(notTall.Matches(kPortrait));
        EXPECT_FALSE(notTall.Matches(kLandscape));
    }

    TEST(UIVariantConditionTest, WidthAndHeightRangesAreChecked) {
        UIVariantCondition condition;
        condition.maxWidth = 800.0f;
        condition.minHeight = 1000.0f;

        EXPECT_TRUE(condition.Matches(kPortrait));
        EXPECT_FALSE(condition.Matches(kLandscape));
        EXPECT_FALSE(condition.Matches({0.0f, 0.0f, 700.0f, 900.0f}));
    }

    TEST(UIVariantConditionTest, AllPartsMustHold) {
        UIVariantCondition condition;
        condition.orientation = UIOrientation::Portrait;
        condition.maxWidth = 600.0f;

        EXPECT_TRUE(condition.Matches({0.0f, 0.0f, 500.0f, 900.0f}));
        EXPECT_FALSE(condition.Matches(kPortrait));                     // portrait, but 720 wide
        EXPECT_FALSE(condition.Matches({0.0f, 0.0f, 500.0f, 300.0f}));  // narrow, but landscape
    }

    // ---- applying variants ------------------------------------------------------------------------------------------------

    TEST(UIVariantsTest, WithoutAMatchingVariantTheActiveStyleIsTheAuthoredOne) {
        auto root = MakeHudTree();
        const UIVariant variants[] = {PortraitHudVariant()};

        const size_t matched = UIVariants::Apply(*root, variants, kLandscape);

        EXPECT_EQ(matched, 0u);
        UIElement* hud = root->FindByName("Hud"_intern);
        ASSERT_NE(hud, nullptr);
        EXPECT_EQ(hud->active, hud->style);
    }

    TEST(UIVariantsTest, AMatchingVariantChangesOnlyTheNamedPropertiesOfTheNamedElement) {
        auto root = MakeHudTree();
        const UIVariant variants[] = {PortraitHudVariant()};

        const size_t matched = UIVariants::Apply(*root, variants, kPortrait);

        EXPECT_EQ(matched, 1u);
        UIElement* hud = root->FindByName("Hud"_intern);
        EXPECT_EQ(hud->active.rect.anchor, UIAnchor::Top);
        EXPECT_EQ(hud->active.rect.width, UILength::Percent(90.0f));
        EXPECT_FLOAT_EQ(hud->active.rect.y.value, 16.0f);  // not named: stays
        EXPECT_EQ(hud->active.rect.height, UILength::Px(96.0f));
        EXPECT_EQ(hud->style.rect.anchor, UIAnchor::TopLeft);  // the authored style is never touched
        UIElement* score = root->FindByName("Score"_intern);
        EXPECT_EQ(score->active, score->style);
    }

    TEST(UIVariantsTest, ALaterVariantWinsWhereTwoSetTheSamePropertyAndBothApplyWhereTheyDiffer) {
        auto root = MakeHudTree();
        UIVariant first;
        UIElementOverride a;
        a.element = "Hud"_intern;
        a.x = UILength::Px(100.0f);
        a.y = UILength::Px(200.0f);
        first.overrides.push_back(a);
        UIVariant second;
        UIElementOverride b;
        b.element = "Hud"_intern;
        b.x = UILength::Px(300.0f);
        second.overrides.push_back(b);
        const UIVariant variants[] = {first, second};

        EXPECT_EQ(UIVariants::Apply(*root, variants, kLandscape), 2u);

        UIElement* hud = root->FindByName("Hud"_intern);
        EXPECT_FLOAT_EQ(hud->active.rect.x.value, 300.0f);  // the second wins
        EXPECT_FLOAT_EQ(hud->active.rect.y.value, 200.0f);  // the first one's y stays
    }

    TEST(UIVariantsTest, TheOrderOfTheListNotThePriorityOfTheConditionDecides) {
        auto root = MakeHudTree();
        UIVariant broad;  // matches everything, listed last
        UIElementOverride wide;
        wide.element = "Hud"_intern;
        wide.x = UILength::Px(1.0f);
        broad.overrides.push_back(wide);
        UIVariant narrow = PortraitHudVariant();
        narrow.overrides[0].x = UILength::Px(50.0f);
        const UIVariant variants[] = {narrow, broad};

        UIVariants::Apply(*root, variants, kPortrait);

        EXPECT_FLOAT_EQ(root->FindByName("Hud"_intern)->active.rect.x.value, 1.0f);
    }

    TEST(UIVariantsTest, GoingBackToAShapeNoVariantMatchesRestoresTheAuthoredStyle) {
        auto root = MakeHudTree();
        const UIVariant variants[] = {PortraitHudVariant()};

        UIVariants::Apply(*root, variants, kPortrait);
        UIVariants::Apply(*root, variants, kLandscape);

        UIElement* hud = root->FindByName("Hud"_intern);
        EXPECT_EQ(hud->active, hud->style);
    }

    TEST(UIVariantsTest, AnOverrideOfAnUnknownElementIsIgnored) {
        auto root = MakeHudTree();
        UIVariant variant;
        UIElementOverride ghost;
        ghost.element = "Nobody"_intern;
        ghost.visible = false;
        variant.overrides.push_back(ghost);
        const UIVariant variants[] = {variant};

        EXPECT_EQ(UIVariants::Apply(*root, variants, kLandscape), 1u);

        EXPECT_TRUE(root->FindByName("Hud"_intern)->active.visible);
    }

    TEST(UIVariantsTest, EveryPropertyOfAnOverrideReachesTheStyle) {
        UIElementOverride all;
        all.visible = false;
        all.anchor = UIAnchor::BottomLeft;
        all.anchorMin = Vec2{0.1f, 0.2f};
        all.anchorMax = Vec2{0.9f, 0.8f};
        all.pivot = Vec2{0.0f, 1.0f};
        all.margin = UIEdges{1.0f, 2.0f, 3.0f, 4.0f};
        all.x = UILength::Px(1.0f);
        all.y = UILength::Px(2.0f);
        all.width = UILength::Percent(50.0f);
        all.height = UILength::Px(30.0f);
        all.region = "table.plate.1"_intern;
        all.direction = UIStackDirection::Horizontal;
        all.fontSize = 18.0f;
        UIElementStyle style;

        all.ApplyTo(style);

        EXPECT_FALSE(style.visible);
        EXPECT_EQ(style.rect.anchor, UIAnchor::BottomLeft);
        EXPECT_EQ(style.rect.anchorMin, Optional<Vec2>(Vec2{0.1f, 0.2f}));
        EXPECT_EQ(style.rect.anchorMax, Optional<Vec2>(Vec2{0.9f, 0.8f}));
        EXPECT_EQ(style.rect.pivot, Optional<Vec2>(Vec2{0.0f, 1.0f}));
        EXPECT_EQ(style.rect.margin, (UIEdges{1.0f, 2.0f, 3.0f, 4.0f}));
        EXPECT_FLOAT_EQ(style.rect.x.value, 1.0f);
        EXPECT_FLOAT_EQ(style.rect.y.value, 2.0f);
        EXPECT_EQ(style.rect.width, UILength::Percent(50.0f));
        EXPECT_EQ(style.rect.height, UILength::Px(30.0f));
        EXPECT_EQ(style.rect.region, "table.plate.1"_intern);
        EXPECT_EQ(style.stack.direction, UIStackDirection::Horizontal);
        EXPECT_FLOAT_EQ(style.fontSize, 18.0f);
    }

    TEST(UIVariantsTest, APercentOffsetOfAnOverrideSurvivesTheFileRoundTripAndReachesTheStyle) {
        UIVariant variant = PortraitHudVariant();
        variant.overrides[0].x = UILength::Percent(5.0f);
        variant.overrides[0].y = UILength::Px(0.0f);

        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("variant"));
        variant.Serialize(serializer);
        serializer.EndObject();
        const eastl::string xml = serializer.SaveToString();
        EXPECT_NE(xml.find("x=\"5%\""), eastl::string::npos);
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
        UIVariant loaded;
        ASSERT_TRUE(deserializer.BeginObject("variant"));
        loaded.Deserialize(deserializer);
        deserializer.EndObject();

        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_TRUE(loaded == variant);
        UIElementStyle style;
        loaded.overrides[0].ApplyTo(style);
        EXPECT_EQ(style.rect.x, UILength::Percent(5.0f));
        EXPECT_EQ(style.rect.y, UILength::Px(0.0f));
    }

    TEST(UIVariantsTest, EdgeAnchorsAndAMarginOfAnOverrideSurviveTheFileRoundTripEvenWhenTheyEqualADefault) {
        UIVariant variant = PortraitHudVariant();
        variant.overrides[0].anchorMin = Vec2{0.0f, 0.0f};  // the default of nothing: still the author's choice
        variant.overrides[0].anchorMax = Vec2{0.35f, 1.0f};
        variant.overrides[0].pivot = Vec2{0.5f, 0.0f};
        variant.overrides[0].margin = UIEdges{};  // an explicit zero margin

        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("variant"));
        variant.Serialize(serializer);
        serializer.EndObject();
        const eastl::string xml = serializer.SaveToString();
        EXPECT_NE(xml.find("anchorMin=\"0,0\""), eastl::string::npos) << xml.c_str();
        EXPECT_NE(xml.find("anchorMax=\"0.35,1\""), eastl::string::npos);
        EXPECT_NE(xml.find("pivot=\"0.5,0\""), eastl::string::npos);
        EXPECT_NE(xml.find("margin=\"0\""), eastl::string::npos);
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
        UIVariant loaded;
        ASSERT_TRUE(deserializer.BeginObject("variant"));
        loaded.Deserialize(deserializer);
        deserializer.EndObject();

        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_TRUE(loaded == variant);
        ASSERT_TRUE(loaded.overrides[0].margin.has_value());

        // An override that sets none of them writes none of them
        UIVariant bare = PortraitHudVariant();
        XmlSerializer bareSerializer;
        ASSERT_TRUE(bareSerializer.BeginObject("variant"));
        bare.Serialize(bareSerializer);
        bareSerializer.EndObject();
        const eastl::string bareXml = bareSerializer.SaveToString();
        EXPECT_EQ(bareXml.find("anchorM"), eastl::string::npos);
        EXPECT_EQ(bareXml.find("pivot"), eastl::string::npos);
        EXPECT_EQ(bareXml.find("margin"), eastl::string::npos);
    }

    TEST(UIVariantsTest, AMalformedEdgeAnchorOfAnOverrideIsReportedAndLeavesItUnset) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(
            "<root><variant when=\"orientation:portrait\"><overrides><set element=\"Hud\" anchorMin=\"1\" pivot=\"0.5,0.5\" margin=\"1,2\"/></overrides></variant></root>"));
        UIVariant loaded;
        ASSERT_TRUE(deserializer.BeginObject("variant"));
        loaded.Deserialize(deserializer);
        deserializer.EndObject();

        EXPECT_TRUE(deserializer.HasErrors());
        ASSERT_EQ(loaded.overrides.size(), 1u);
        EXPECT_FALSE(loaded.overrides[0].anchorMin);
        EXPECT_FALSE(loaded.overrides[0].margin);
        EXPECT_EQ(loaded.overrides[0].pivot, Optional<Vec2>(Vec2{0.5f, 0.5f}));
    }

    TEST(UIVariantsTest, ARegionOfAnOverrideSurvivesTheFileRoundTrip) {
        UIVariant variant = PortraitHudVariant();
        variant.overrides[0].region = "table.plate.2"_intern;
        UIElementOverride cleared;
        cleared.element = "Score"_intern;
        cleared.region = PoolString{};
        variant.overrides.push_back(cleared);

        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("variant"));
        variant.Serialize(serializer);
        serializer.EndObject();
        const eastl::string xml = serializer.SaveToString();
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
        UIVariant loaded;
        ASSERT_TRUE(deserializer.BeginObject("variant"));
        loaded.Deserialize(deserializer);
        deserializer.EndObject();

        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_TRUE(loaded == variant);
        ASSERT_EQ(loaded.overrides.size(), 2u);
        EXPECT_EQ(loaded.overrides[0].region, Optional<PoolString>{"table.plate.2"_intern});
        EXPECT_TRUE(loaded.overrides[1].region.has_value()) << "region=\"\" is an override that clears, not an absent one";
        EXPECT_TRUE(loaded.overrides[1].region->Empty());
    }

    TEST(UIVariantsTest, ARegionOverrideMovesTheElementAndTheAuthoredRegionComesBack) {
        auto root = MakeHudTree();
        auto hud = root->FindByName("Hud"_intern);
        hud->style.rect.region = "table.hud"_intern;
        UIVariant variant;
        variant.when.orientation = UIOrientation::Portrait;
        UIElementOverride toPlate;
        toPlate.element = "Hud"_intern;
        toPlate.region = "table.plate.2"_intern;
        UIElementOverride cleared;
        cleared.element = "Hud"_intern;
        cleared.region = PoolString{};
        variant.overrides.push_back(toPlate);
        const UIVariant plate[] = {variant};
        variant.overrides.push_back(cleared);
        const UIVariant clearing[] = {variant};

        EXPECT_EQ(UIVariants::Apply(*root, plate, kPortrait), 1u);
        EXPECT_EQ(hud->active.rect.region, "table.plate.2"_intern);
        EXPECT_EQ(hud->style.rect.region, "table.hud"_intern);

        EXPECT_EQ(UIVariants::Apply(*root, clearing, kPortrait), 1u);
        EXPECT_TRUE(hud->active.rect.region.Empty()) << "a later override with an empty name clears the region";

        EXPECT_EQ(UIVariants::Apply(*root, plate, kLandscape), 0u);
        EXPECT_EQ(hud->active.rect.region, "table.hud"_intern);
    }

    // ---- laying out a tree ------------------------------------------------------------------------------------------------

    TEST(UITreeLayoutTest, ChildrenAreLaidOutInTheRectOfTheirParentInCanvasCoordinates) {
        auto root = MakeHudTree();
        UIVariants::Apply(*root, {}, kLandscape);

        UITreeLayout::Run(*root, kLandscape, kLandscape);

        ExpectRect(root->rect, 0.0f, 0.0f, 1280.0f, 720.0f);
        ExpectRect(root->FindByName("Hud"_intern)->rect, 16.0f, 16.0f, 220.0f, 96.0f);
        ExpectRect(root->FindByName("Score"_intern)->rect, 28.0f, 26.0f, 120.0f, 28.0f);
        ExpectRect(root->FindByName("Reset"_intern)->rect, 16.0f + 220.0f - 12.0f - 96.0f, 16.0f + 96.0f - 10.0f - 32.0f, 96.0f, 32.0f);
    }

    TEST(UITreeLayoutTest, ThePortraitVariantMovesAndWidensTheHud) {
        auto root = MakeHudTree();
        const UIVariant variants[] = {PortraitHudVariant()};

        UIVariants::Apply(*root, variants, kPortrait);
        UITreeLayout::Run(*root, kPortrait, kPortrait);

        // Top center, 90% of 720 wide; the children follow their parent
        ExpectRect(root->FindByName("Hud"_intern)->rect, 36.0f, 16.0f, 648.0f, 96.0f);
        ExpectRect(root->FindByName("Score"_intern)->rect, 48.0f, 26.0f, 120.0f, 28.0f);
        const Rect reset = root->FindByName("Reset"_intern)->rect;
        EXPECT_NEAR(reset.x + reset.w, 36.0f + 648.0f - 12.0f, kTolerance);  // still pinned to the right edge of the wider panel
    }

    TEST(UITreeLayoutTest, APercentIsOfTheParentsRectNotTheCanvas) {
        auto root = New<UIElement>();
        auto panel = root->AddChild("Panel"_intern);
        panel->style.rect.width = UILength::Px(400.0f);
        panel->style.rect.height = UILength::Px(200.0f);
        auto half = panel->AddChild("Half"_intern);
        half->style.rect.width = UILength::Percent(50.0f);
        half->style.rect.height = UILength::Percent(50.0f);
        UIVariants::Apply(*root, {}, kLandscape);

        UITreeLayout::Run(*root, kLandscape, kLandscape);

        ExpectRect(half->rect, 0.0f, 0.0f, 200.0f, 100.0f);
    }

    TEST(UITreeLayoutTest, AStackContainerLaysItsChildrenOutAndAHiddenOneTakesNoRoom) {
        auto root = New<UIElement>();
        auto column = root->AddChild("Column"_intern);
        column->layout = UILayoutKind::Stack;
        column->style.rect.width = UILength::Px(200.0f);
        column->style.rect.height = UILength::Px(300.0f);
        column->style.stack.spacing = 10.0f;
        IntrusivePtr<UIElement> rows[3];
        for (int i = 0; i < 3; ++i) {
            rows[i] = column->AddChild(PoolString::Intern(Format("Row{}", i)));
            rows[i]->style.rect.width = UILength::Px(100.0f);
            rows[i]->style.rect.height = UILength::Px(40.0f);
        }
        auto inner = rows[1]->AddChild("Inner"_intern);
        inner->style.rect.width = UILength::Px(10.0f);
        inner->style.rect.height = UILength::Px(10.0f);
        rows[1]->style.visible = false;
        UIVariants::Apply(*root, {}, kLandscape);

        UITreeLayout::Run(*root, kLandscape, kLandscape);

        ExpectRect(rows[0]->rect, 0.0f, 0.0f, 100.0f, 40.0f);
        ExpectRect(rows[2]->rect, 0.0f, 50.0f, 100.0f, 40.0f);  // right under the first: the hidden row is not there
        ExpectRect(rows[1]->rect, 0.0f, 0.0f, 0.0f, 0.0f);
        ExpectRect(inner->rect, 0.0f, 0.0f, 0.0f, 0.0f);  // nor is what is inside it
    }

    TEST(UITreeLayoutTest, AVariantCanHideAnElement) {
        auto root = MakeHudTree();
        UIVariant variant;
        UIElementOverride hide;
        hide.element = "Reset"_intern;
        hide.visible = false;
        variant.overrides.push_back(hide);
        const UIVariant variants[] = {variant};

        UIVariants::Apply(*root, variants, kLandscape);
        UITreeLayout::Run(*root, kLandscape, kLandscape);

        EXPECT_TRUE(root->FindByName("Reset"_intern)->rect.IsEmpty());
        EXPECT_FALSE(root->FindByName("Score"_intern)->rect.IsEmpty());
    }

    TEST(UITreeLayoutTest, ABackgroundThatIgnoresTheSafeAreaUsesTheWholeCanvas) {
        auto root = New<UIElement>();
        auto background = root->AddChild("Background"_intern);
        background->style.rect.anchor = UIAnchor::Stretch;
        background->ignoreSafeArea = true;
        auto content = root->AddChild("Content"_intern);
        content->style.rect.anchor = UIAnchor::Stretch;
        auto nested = content->AddChild("Nested"_intern);
        nested->style.rect.anchor = UIAnchor::Stretch;
        nested->ignoreSafeArea = true;  // only a child of the root can opt out
        UIVariants::Apply(*root, {}, kLandscape);
        const Rect safe{40.0f, 30.0f, 1200.0f, 660.0f};

        UITreeLayout::Run(*root, safe, kLandscape);

        ExpectRect(root->rect, 40.0f, 30.0f, 1200.0f, 660.0f);
        ExpectRect(background->rect, 0.0f, 0.0f, 1280.0f, 720.0f);
        ExpectRect(content->rect, 40.0f, 30.0f, 1200.0f, 660.0f);
        ExpectRect(nested->rect, 40.0f, 30.0f, 1200.0f, 660.0f);
    }

    TEST(UITreeLayoutTest, TheRootCanBeAStackItself) {
        auto root = New<UIElement>();
        root->layout = UILayoutKind::Stack;
        root->style.stack.crossAlign = UIStackAlign::Center;
        for (int i = 0; i < 2; ++i) {
            auto button = root->AddChild(PoolString::Intern(Format("Button{}", i)));
            button->style.rect.width = UILength::Px(200.0f);
            button->style.rect.height = UILength::Px(60.0f);
        }
        UIVariants::Apply(*root, {}, kLandscape);

        UITreeLayout::Run(*root, kLandscape, kLandscape);

        ExpectRect(root->children[0]->rect, 540.0f, 0.0f, 200.0f, 60.0f);
        ExpectRect(root->children[1]->rect, 540.0f, 60.0f, 200.0f, 60.0f);
    }

    TEST(UIElementTest, FindByNameFindsTheFirstDepthFirstAndReturnsNullWhenThereIsNone) {
        auto root = MakeHudTree();

        EXPECT_EQ(root->FindByName("Root"_intern), root.Get());
        ASSERT_NE(root->FindByName("Reset"_intern), nullptr);
        EXPECT_EQ(root->FindByName("Reset"_intern)->name, "Reset"_intern);
        EXPECT_EQ(root->FindByName("Missing"_intern), nullptr);
    }

    TEST(UIElementTest, APlainElementHasNoContentToCheck) {
        UIElement element;

        const UIContentFit fit = element.CheckContent(10.0f, 10.0f, 1.0f);

        EXPECT_FALSE(fit.hasContent);
        EXPECT_TRUE(fit.fits);
    }

}  // namespace BECore::Tests
