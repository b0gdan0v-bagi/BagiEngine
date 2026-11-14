#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/SceneView.h>
#include <BECore/UI/UIPreview.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kDefaultWidth = 1280.0f;
        constexpr float kDefaultHeight = 720.0f;

        SceneViewVariant MakeVariant(const char* name, const char* when, float width, float height) {
            SceneViewVariant variant;
            variant.name = PoolString::Intern(name);
            const auto condition = UIVariantCondition::FromText(when);
            EXPECT_TRUE(condition.has_value()) << when;
            if (condition) {
                variant.when = *condition;
            }
            variant.width = width;
            variant.height = height;
            return variant;
        }

        ActiveSceneView Pick(const eastl::vector<SceneViewVariant>& variants, float outputWidth, float outputHeight) {
            return SceneViewSelector::Pick(kDefaultWidth, kDefaultHeight, variants, outputWidth, outputHeight);
        }

        eastl::vector<SceneViewVariant> PortraitAndWide() {
            return {MakeVariant("Portrait", "orientation:portrait", 720.0f, 1280.0f), MakeVariant("Wide", "minAspect:2", 1600.0f, 600.0f)};
        }

    }  // namespace

    TEST(SceneViewSelectorTest, NoVariantsIsTheDefaultView) {
        const ActiveSceneView view = Pick({}, 600.0f, 900.0f);
        EXPECT_EQ(view.index, -1);
        EXPECT_TRUE(view.name.Empty());
        EXPECT_FLOAT_EQ(view.width, kDefaultWidth);
        EXPECT_FLOAT_EQ(view.height, kDefaultHeight);
    }

    TEST(SceneViewSelectorTest, TheFirstVariantThatHoldsWins) {
        const auto variants = PortraitAndWide();

        const ActiveSceneView portrait = Pick(variants, 600.0f, 900.0f);
        EXPECT_EQ(portrait.index, 0);
        EXPECT_EQ(portrait.name, "Portrait"_intern);
        EXPECT_FLOAT_EQ(portrait.width, 720.0f);
        EXPECT_FLOAT_EQ(portrait.height, 1280.0f);

        const ActiveSceneView wide = Pick(variants, 1200.0f, 400.0f);
        EXPECT_EQ(wide.index, 1);
        EXPECT_FLOAT_EQ(wide.width, 1600.0f);

        // Landscape but not wide: nothing holds
        EXPECT_EQ(Pick(variants, 1000.0f, 800.0f).index, -1);

        // The order of the list decides when two hold: both of these match a 300 x 100 output
        const eastl::vector<SceneViewVariant> reversed = {MakeVariant("Wide", "minAspect:2", 1600.0f, 600.0f), MakeVariant("Any", "", 10.0f, 10.0f)};
        EXPECT_EQ(Pick(reversed, 300.0f, 100.0f).index, 0);
        EXPECT_EQ(Pick(reversed, 100.0f, 300.0f).index, 1);
    }

    TEST(SceneViewSelectorTest, ASquareOutputIsPortrait) {
        const auto variants = PortraitAndWide();
        EXPECT_EQ(Pick(variants, 800.0f, 800.0f).index, 0);
    }

    TEST(SceneViewSelectorTest, AspectAndSizeRangesAreTestedAgainstTheOutputInPixels) {
        const eastl::vector<SceneViewVariant> variants = {
            MakeVariant("Small", "maxWidth:500", 400.0f, 400.0f),
            MakeVariant("Tall", "maxAspect:0.5;minHeight:1000", 500.0f, 1000.0f),
        };
        EXPECT_EQ(Pick(variants, 500.0f, 300.0f).index, 0);
        EXPECT_EQ(Pick(variants, 501.0f, 300.0f).index, -1);
        EXPECT_EQ(Pick(variants, 600.0f, 1200.0f).index, 1);
        EXPECT_EQ(Pick(variants, 600.0f, 900.0f).index, -1);
    }

    TEST(SceneViewSelectorTest, AVariantWithAnUnusableSizeIsSkipped) {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        const eastl::vector<SceneViewVariant> variants = {
            MakeVariant("Zero", "", 0.0f, 100.0f), MakeVariant("Negative", "", 100.0f, -1.0f), MakeVariant("Nan", "", nan, 100.0f),
            MakeVariant("Inf", "", 100.0f, inf),   MakeVariant("Good", "", 300.0f, 200.0f),
        };
        const ActiveSceneView view = Pick(variants, 400.0f, 400.0f);
        EXPECT_EQ(view.index, 4);
        EXPECT_EQ(view.name, "Good"_intern);
    }

    TEST(SceneViewSelectorTest, AnOutputWithoutAShapeIsTheDefault) {
        const auto variants = PortraitAndWide();
        EXPECT_EQ(Pick(variants, 0.0f, 0.0f).index, -1);
        EXPECT_EQ(Pick(variants, 0.0f, 900.0f).index, -1);
        EXPECT_EQ(Pick(variants, 600.0f, -1.0f).index, -1);
        EXPECT_EQ(Pick(variants, std::numeric_limits<float>::quiet_NaN(), 900.0f).index, -1);
    }

    TEST(SceneViewSelectorTest, ADefaultWithoutASizeStaysWithoutOne) {
        const ActiveSceneView view = SceneViewSelector::Pick(0.0f, 0.0f, {}, 600.0f, 900.0f);
        EXPECT_EQ(view.index, -1);
        EXPECT_FLOAT_EQ(view.width, 0.0f);
        EXPECT_FLOAT_EQ(view.height, 0.0f);
    }

    TEST(SceneViewSelectorTest, VariantsRoundTripThroughTheFileForm) {
        eastl::vector<SceneViewVariant> variants = PortraitAndWide();
        variants.push_back(MakeVariant("", "", 10.0f, 20.0f));

        XmlSerializer serializer;
        Ser::DataAccessor<eastl::vector<SceneViewVariant>>::Save(serializer, variants, "viewVariants");
        const pugi::xml_node list = serializer.GetRootNode().child("viewVariants");
        ASSERT_TRUE(list);
        const pugi::xml_node first = list.child("view");
        ASSERT_TRUE(first);
        EXPECT_STREQ(first.attribute("name").as_string(), "Portrait");
        EXPECT_STREQ(first.attribute("when").as_string(), "orientation:portrait");
        EXPECT_FLOAT_EQ(first.attribute("width").as_float(), 720.0f);
        EXPECT_FLOAT_EQ(first.attribute("height").as_float(), 1280.0f);
        EXPECT_FALSE(list.last_child().attribute("name"));

        String text;
        {
            std::ostringstream stream;
            serializer.GetRootNode().print(stream);
            text = stream.str().c_str();
        }
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(text.c_str()));
        eastl::vector<SceneViewVariant> loaded;
        ASSERT_TRUE(Ser::DataAccessor<eastl::vector<SceneViewVariant>>::Load(deserializer, loaded, "viewVariants"));
        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_EQ(loaded, variants);
    }

    TEST(SceneViewSelectorTest, AnEmptyListWritesNothing) {
        XmlSerializer serializer;
        Ser::DataAccessor<eastl::vector<SceneViewVariant>>::Save(serializer, {}, "viewVariants");
        EXPECT_FALSE(serializer.GetRootNode().child("viewVariants"));
    }

    TEST(SceneViewSelectorTest, AMalformedConditionIsOneErrorAndTheVariantIsDropped) {
        constexpr const char* xml = R"(
<Scene><viewVariants>
  <view name="Bad" when="orientation:sideways" width="100" height="100"/>
  <view name="Good" when="orientation:portrait" width="200" height="300"/>
</viewVariants></Scene>)";
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        eastl::vector<SceneViewVariant> loaded;
        ASSERT_TRUE(Ser::DataAccessor<eastl::vector<SceneViewVariant>>::Load(deserializer, loaded, "viewVariants"));

        EXPECT_EQ(deserializer.GetErrors().size(), 1u);
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_EQ(loaded[0].name, "Good"_intern);
    }

    // The editor viewport's offscreen pass is as big as UIPreview::FitSize says, and that size picks the view
    TEST(SceneViewSelectorTest, TheViewportPresetsPickTheirViewBySize) {
        const auto variants = PortraitAndWide();
        struct Case {
            ViewportAspect aspect;
            float availableWidth;
            float availableHeight;
            int32_t index;
        };
        const Case cases[] = {
            {ViewportAspect::Wide16x9, 1000.0f, 800.0f, -1},    {ViewportAspect::Standard4x3, 1000.0f, 800.0f, -1}, {ViewportAspect::UltraWide21x9, 1400.0f, 800.0f, 1},
            {ViewportAspect::Portrait9x16, 1000.0f, 800.0f, 0}, {ViewportAspect::Free, 400.0f, 900.0f, 0},          {ViewportAspect::Free, 1000.0f, 800.0f, -1},
        };
        for (const Case& c : cases) {
            const auto [w, h] = UIPreview::FitSize(c.aspect, c.availableWidth, c.availableHeight);
            SCOPED_TRACE(Format("{} in {}x{} is {}x{}", EnumUtils<ViewportAspect>::ToString(c.aspect), c.availableWidth, c.availableHeight, w, h).c_str());
            EXPECT_EQ(Pick(variants, static_cast<float>(w), static_cast<float>(h)).index, c.index);
        }
    }

}  // namespace BECore::Tests
