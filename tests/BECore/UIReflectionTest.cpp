#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIValueText.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/UITreeCompare.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        // A canvas with a nested tree, a stack, a hidden element and a portrait variant, written the way a model would write it
        constexpr const char* kScreenXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ScaleWithScreen" referenceWidth="1280" referenceHeight="720" match="0.5" minScale="0.7" useSafeArea="true">
        <elements>
          <element type="UIElement" name="Panel" anchor="TopLeft" x="16" y="16" width="220" height="96" color="0,0,0,160" blocksInput="true">
            <children>
              <element type="UIElement" name="Score" x="12" y="10" width="40%" height="28" minWidth="20"/>
              <element type="UIElement" name="Row" layout="Stack" direction="Horizontal" spacing="8" padding="4,2,4,2" anchor="BottomRight" x="-12" y="-10" width="96" height="32" visible="false"/>
            </children>
          </element>
        </elements>
        <variants>
          <variant when="orientation:portrait">
            <overrides><set element="Panel" anchor="Top" x="0" y="16" width="90%"/></overrides>
          </variant>
        </variants>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        IntrusivePtr<UIElement> MakeElement(const char* name) {
            auto element = New<UIElement>();
            element->name = PoolString::Intern(name);
            return element;
        }

        /// A tree that sets every kind of property at least once, with values that equal the defaults of their type (a false `visible`, a zero offset)
        IntrusivePtr<UICanvasComponent> MakeCanvas() {
            auto canvas = New<UICanvasComponent>();
            canvas->_space = CanvasSpace::SceneView;
            canvas->_minScale = 0.7f;

            auto panel = MakeElement("Panel");
            panel->style.rect.anchor = UIAnchor::BottomRight;
            panel->style.rect.x = -12.0f;
            panel->style.rect.width = UILength::Percent(35.5f);
            panel->style.rect.height = UILength::Px(96.0f);
            panel->style.rect.minWidth = UILength::Px(0.0f);
            panel->style.rect.maxHeight = UILength::Percent(50.0f);
            panel->style.rect.aspectRatio = 2.0f;
            panel->style.rect.aspectMode = UIAspectMode::Fill;
            panel->style.rect.margin = {1.0f, 2.0f, 3.0f, 4.0f};
            panel->color = Color{0, 0, 0, 160};
            panel->blocksInput = true;

            auto row = MakeElement("Row");
            row->layout = UILayoutKind::Stack;
            row->style.visible = false;
            row->style.stack.direction = UIStackDirection::Horizontal;
            row->style.stack.spacing = 8.0f;
            row->style.stack.padding = {5.0f, 5.0f, 5.0f, 5.0f};
            row->style.stack.wrap = true;
            row->style.stack.crossAlign = UIStackAlign::Center;
            row->style.fontSize = 18.0f;
            row->interactive = true;
            row->ignoreSafeArea = true;
            row->style.rect.flex = 1.5f;

            panel->children.push_back(row);
            canvas->_elements.push_back(panel);

            UIVariant portrait;
            portrait.when.orientation = UIOrientation::Portrait;
            portrait.when.minAspect = 0.5f;
            portrait.when.maxHeight = 2000.0f;
            UIElementOverride move;
            move.element = PoolString::Intern("Panel");
            move.x = 0.0f;  // an explicit zero must survive
            move.visible = false;
            move.width = UILength::Percent(90.0f);
            move.direction = UIStackDirection::Vertical;
            portrait.overrides.push_back(move);
            canvas->_variants.push_back(portrait);
            return canvas;
        }

        void ExpectSameCanvas(const UICanvasComponent& expected, const UICanvasComponent& actual) {
            EXPECT_EQ(expected._space, actual._space);
            EXPECT_FLOAT_EQ(expected._minScale, actual._minScale);
            ExpectSameElements(expected._elements, actual._elements);
            EXPECT_TRUE(expected._variants == actual._variants);
        }

        eastl::string ToXml(const UICanvasComponent& canvas, bool skipDefaults) {
            XmlSerializer serializer;
            serializer.SetSkipDefaults(skipDefaults);
            EXPECT_TRUE(serializer.BeginObject("component"));
            canvas.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        }

        IntrusivePtr<UICanvasComponent> FromXml(const eastl::string& xml, bool expectClean = true) {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
            auto canvas = New<UICanvasComponent>();
            EXPECT_TRUE(deserializer.BeginObject("component"));
            canvas->Deserialize(deserializer);
            deserializer.EndObject();
            if (expectClean) {
                EXPECT_FALSE(deserializer.HasErrors());
            }
            canvas->Initialize();
            return canvas;
        }

    }  // namespace

    // ---- text forms -------------------------------------------------------------------------------------------------------

    TEST(UIReflectionTest, LengthsAreUnitsOrPercent) {
        EXPECT_EQ(UIText::FormatLength(UILength::Px(220.0f)), "220");
        EXPECT_EQ(UIText::FormatLength(UILength::Percent(90.0f)), "90%");
        EXPECT_EQ(UIText::FormatLength(UILength::Px(0.5f)), "0.5");

        const auto plain = UIText::ParseLength("220");
        const auto suffixed = UIText::ParseLength(" 220px ");
        const auto percent = UIText::ParseLength("37.5%");
        ASSERT_TRUE(plain && suffixed && percent);
        EXPECT_EQ(*plain, UILength::Px(220.0f));
        EXPECT_EQ(*suffixed, UILength::Px(220.0f));
        EXPECT_EQ(*percent, UILength::Percent(37.5f));

        for (const char* bad : {"", "px", "%", "abc", "12 3", "1e", "10em"}) {
            EXPECT_FALSE(UIText::ParseLength(bad)) << bad;
        }
    }

    TEST(UIReflectionTest, EdgesAreOneNumberOrFour) {
        EXPECT_EQ(UIText::FormatEdges({8.0f, 8.0f, 8.0f, 8.0f}), "8");
        EXPECT_EQ(UIText::FormatEdges({1.0f, 2.0f, 3.0f, 4.5f}), "1,2,3,4.5");

        const auto all = UIText::ParseEdges("8");
        const auto four = UIText::ParseEdges("1, 2, 3, 4.5");
        ASSERT_TRUE(all && four);
        EXPECT_EQ(*all, (UIEdges{8.0f, 8.0f, 8.0f, 8.0f}));
        EXPECT_EQ(*four, (UIEdges{1.0f, 2.0f, 3.0f, 4.5f}));

        for (const char* bad : {"", "1,2", "1,2,3", "1,2,3,4,5", "a", "1,,3,4"}) {
            EXPECT_FALSE(UIText::ParseEdges(bad)) << bad;
        }
    }

    TEST(UIReflectionTest, ColorsAreRgbaOrHex) {
        EXPECT_EQ(UIText::FormatColor(Color{0, 0, 0, 160}), "0,0,0,160");

        const auto rgba = UIText::ParseColor("10, 20, 30, 255");
        const auto rgb = UIText::ParseColor("#102030");
        const auto withAlpha = UIText::ParseColor("#102030A0");
        ASSERT_TRUE(rgba && rgb && withAlpha);
        EXPECT_EQ(rgba->g, 20);
        EXPECT_EQ(rgb->r, 0x10);
        EXPECT_EQ(rgb->a, 255);
        EXPECT_EQ(withAlpha->a, 0xA0);

        for (const char* bad : {"", "1,2,3", "1,2,3,256", "1,2,3,-1", "#12345", "#GGGGGG", "red"}) {
            EXPECT_FALSE(UIText::ParseColor(bad)) << bad;
        }
    }

    TEST(UIReflectionTest, ConditionsAreNameValueParts) {
        UIVariantCondition condition;
        condition.orientation = UIOrientation::Portrait;
        condition.minAspect = 0.5f;
        condition.maxHeight = 2000.0f;
        EXPECT_EQ(condition.ToText(), "orientation:portrait;minAspect:0.5;maxHeight:2000");
        EXPECT_EQ(UIVariantCondition{}.ToText(), "");

        const auto parsed = UIVariantCondition::FromText(" Orientation : Landscape ; minWidth:640 ");
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(parsed->orientation, UIOrientation::Landscape);
        EXPECT_FLOAT_EQ(parsed->minWidth, 640.0f);
        EXPECT_TRUE(UIVariantCondition::FromText("").has_value());

        for (const char* bad : {"orientation", "orientation:sideways", "colour:red", "minWidth:wide"}) {
            EXPECT_FALSE(UIVariantCondition::FromText(bad).has_value()) << bad;
        }
    }

    // ---- the tree round-trips -------------------------------------------------------------------------------------------

    TEST(UIReflectionTest, ANestedTreeSavesAndLoadsBack) {
        for (const bool skipDefaults : {false, true}) {
            const auto canvas = MakeCanvas();
            const eastl::string xml = ToXml(*canvas, skipDefaults);
            const auto loaded = FromXml(xml);

            ASSERT_NO_FATAL_FAILURE(ExpectSameCanvas(*canvas, *loaded)) << "skipDefaults " << skipDefaults;
            EXPECT_EQ(ToXml(*loaded, skipDefaults), xml) << "the second save differs, skipDefaults " << skipDefaults;
        }
    }

    TEST(UIReflectionTest, TheFileIsFlatAndReadable) {
        const eastl::string xml = ToXml(*MakeCanvas(), true);

        // Style properties are attributes of the element itself, lengths and colors are one attribute each
        EXPECT_NE(xml.find("<element"), eastl::string::npos) << xml.c_str();
        EXPECT_NE(xml.find("type=\"UIElement\""), eastl::string::npos);
        EXPECT_NE(xml.find("width=\"35.5%\""), eastl::string::npos);
        EXPECT_NE(xml.find("color=\"0,0,0,160\""), eastl::string::npos);
        EXPECT_NE(xml.find("margin=\"1,2,3,4\""), eastl::string::npos);
        EXPECT_NE(xml.find("padding=\"5\""), eastl::string::npos);
        EXPECT_NE(xml.find("when=\"orientation:portrait;minAspect:0.5;maxHeight:2000\""), eastl::string::npos);
        EXPECT_NE(xml.find("<children"), eastl::string::npos);
        EXPECT_EQ(xml.find("<style"), eastl::string::npos);
        EXPECT_EQ(xml.find("<color"), eastl::string::npos);
        // Defaults stay out
        EXPECT_EQ(xml.find("spacing=\"0\""), eastl::string::npos);
        EXPECT_EQ(xml.find("aspectMode=\"Fit\""), eastl::string::npos);
    }

    TEST(UIReflectionTest, ABinaryStreamCarriesTheSameTree) {
        const auto canvas = MakeCanvas();

        BinarySerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("component"));
        canvas->Serialize(serializer);
        serializer.EndObject();

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData().data(), serializer.GetData().size()));
        auto loaded = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_FALSE(deserializer.HasErrors());
        loaded->Initialize();

        ExpectSameCanvas(*canvas, *loaded);
    }

    TEST(UIReflectionTest, AMalformedValueIsReportedAndLeavesTheDefault) {
        const eastl::string xml = "<root><component><elements><element type=\"UIElement\" name=\"A\" width=\"wide\" color=\"1,2\" margin=\"1,2\" x=\"5\"/></elements>"
                                  "<variants><variant when=\"orientation:sideways\"/></variants></component></root>";
        const auto canvas = FromXml(xml, false);

        ASSERT_EQ(canvas->_elements.size(), 1u);
        const UIElement& element = *canvas->_elements[0];
        EXPECT_EQ(element.style.rect.width, UILength{});
        EXPECT_EQ(element.color.a, 0);
        EXPECT_EQ(element.style.rect.margin, UIEdges{});
        EXPECT_FLOAT_EQ(element.style.rect.x, 5.0f);  // the well-formed attributes still load

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
        auto again = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        again->Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_TRUE(deserializer.HasErrors());
    }

    TEST(UIReflectionTest, AnUnknownElementTypeIsReported) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString("<root><component><elements><element type=\"NoSuchControl\" name=\"A\"/></elements></component></root>"));
        auto canvas = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        canvas->Deserialize(deserializer);
        deserializer.EndObject();

        ASSERT_TRUE(deserializer.HasErrors());
        EXPECT_NE(deserializer.GetErrors()[0].errorMessage.ToStringView().find("NoSuchControl"), eastl::string_view::npos);
    }

    // ---- from a scene file ------------------------------------------------------------------------------------------------

    class UIReflectionSceneTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kScreenXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
        }

        [[nodiscard]] IntrusivePtr<UICanvasComponent> Canvas() const {
            for (const auto& node : _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern("Hud")) {
                    return node->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(UIReflectionSceneTest, TheElementsOfTheFileAreTheTreeOfTheCanvas) {
        const auto canvas = Canvas();
        ASSERT_TRUE(canvas);
        EXPECT_TRUE(canvas->_useSafeArea);
        EXPECT_FLOAT_EQ(canvas->_minScale, 0.7f);

        const UIElement& root = canvas->GetRoot();
        ASSERT_EQ(root.children.size(), 1u);
        const UIElement& panel = *root.children[0];
        EXPECT_EQ(panel.name, PoolString::Intern("Panel"));
        EXPECT_FLOAT_EQ(panel.style.rect.x, 16.0f);
        EXPECT_EQ(panel.style.rect.width, UILength::Px(220.0f));
        EXPECT_EQ(panel.color.a, 160);
        EXPECT_TRUE(panel.blocksInput);

        ASSERT_EQ(panel.children.size(), 2u);
        EXPECT_EQ(panel.children[0]->style.rect.width, UILength::Percent(40.0f));
        EXPECT_EQ(panel.children[0]->style.rect.minWidth, UILength::Px(20.0f));
        const UIElement& row = *panel.children[1];
        EXPECT_EQ(row.layout, UILayoutKind::Stack);
        EXPECT_FALSE(row.style.visible);
        EXPECT_EQ(row.style.stack.direction, UIStackDirection::Horizontal);
        EXPECT_EQ(row.style.stack.padding, (UIEdges{4.0f, 2.0f, 4.0f, 2.0f}));
        EXPECT_EQ(row.style.rect.anchor, UIAnchor::BottomRight);

        ASSERT_EQ(canvas->GetLayout().variants.size(), 1u);
        EXPECT_EQ(canvas->GetLayout().variants[0].when.orientation, UIOrientation::Portrait);
    }

    TEST_F(UIReflectionSceneTest, TheFileLaysOutLikeTheSameTreeBuiltInCode) {
        const auto canvas = Canvas();
        ASSERT_TRUE(canvas);
        const UIElement& panel = *canvas->GetRoot().children[0];

        // 1280 x 720 is the reference size: scale 1, the panel is where it is written
        canvas->UpdateLayout(1280.0f, 720.0f);
        EXPECT_NEAR(panel.rect.x, 16.0f, kTolerance);
        EXPECT_NEAR(panel.rect.y, 16.0f, kTolerance);
        EXPECT_NEAR(panel.rect.w, 220.0f, kTolerance);
        EXPECT_NEAR(panel.rect.h, 96.0f, kTolerance);

        // 720 x 1280 is portrait at scale 1 as well: the variant of the file puts the panel at the top, 90% wide
        canvas->UpdateLayout(720.0f, 1280.0f);
        EXPECT_NEAR(panel.rect.w, 648.0f, kTolerance);
        EXPECT_NEAR(panel.rect.x, 36.0f, kTolerance);
        EXPECT_NEAR(panel.rect.y, 16.0f, kTolerance);
    }

    TEST_F(UIReflectionSceneTest, ASceneSavesItsCanvasBack) {
        const auto canvas = Canvas();
        ASSERT_TRUE(canvas);
        const eastl::string xml = ToXml(*canvas, true);
        const auto loaded = FromXml(xml);
        ExpectSameCanvas(*canvas, *loaded);
        EXPECT_EQ(ToXml(*loaded, true), xml);
    }

    TEST_F(UIReflectionSceneTest, ElementsAddedInCodeAreNotSavedBack) {
        const auto canvas = Canvas();
        ASSERT_TRUE(canvas);
        canvas->GetRoot().AddChild(PoolString::Intern("CodeOnly"));
        canvas->Invalidate();

        const auto loaded = FromXml(ToXml(*canvas, true));
        EXPECT_EQ(loaded->_elements.size(), 1u);
        EXPECT_EQ(loaded->GetRoot().FindByName(PoolString::Intern("CodeOnly")), nullptr);
    }

    TEST(UIReflectionTest, ExtendToOutputSavesAndLoadsBackAndIsOffByDefault) {
        auto canvas = MakeCanvas();
        EXPECT_FALSE(canvas->_extendToOutput);
        canvas->_extendToOutput = true;

        for (const bool skipDefaults : {false, true}) {
            const eastl::string xml = ToXml(*canvas, skipDefaults);
            EXPECT_NE(xml.find("extendToOutput=\"true\""), eastl::string::npos);
            EXPECT_TRUE(FromXml(xml)->_extendToOutput);
        }

        canvas->_extendToOutput = false;
        EXPECT_FALSE(FromXml(ToXml(*canvas, false))->_extendToOutput);
    }

    TEST(UIReflectionTest, CompileTimeChecks) {
        static_assert(std::is_base_of_v<Color, UIColor>);
        static_assert(std::is_convertible_v<Color, UIColor>);
        static_assert(std::is_same_v<decltype(UIElement::children)::value_type, IntrusivePtr<UIElement>>);
    }

}  // namespace BECore::Tests
