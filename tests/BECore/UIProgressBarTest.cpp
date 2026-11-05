#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIProgressBar.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A Screen canvas in window pixels (output 800 x 600, scale 1):
        //   Unit      (10, 10, 300 x 24)    bind unit, the defaults 0..1
        //   Percent   (10, 50, 200 x 20)    bind percent, 0..100
        //   Signed    (10, 90, 400 x 24)    bind signed, -50..50
        //   Custom    (10, 130, 200 x 20)   bind custom, explicit colors and an element fill
        //   Reversed  (10, 170, 200 x 20)   bind reversed, 10..0 (an empty range)
        //   Blocking  (10, 210, 200 x 20)   bind unit, blocksInput
        //   Unbound   (10, 250, 200 x 20)   no bind
        constexpr const char* kBarXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIProgressBar" name="Unit" bind="unit" x="10" y="10" width="300" height="24"/>
          <element type="UIProgressBar" name="Percent" bind="percent" minValue="0" maxValue="100" x="10" y="50" width="200" height="20"/>
          <element type="UIProgressBar" name="Signed" bind="signed" minValue="-50" maxValue="50" x="10" y="90" width="400" height="24"/>
          <element type="UIProgressBar" name="Custom" bind="custom" x="10" y="130" width="200" height="20" color="1,2,3,255"
                   trackColor="10,20,30,255" fillColor="40,50,60,255"/>
          <element type="UIProgressBar" name="Reversed" bind="reversed" minValue="10" maxValue="0" x="10" y="170" width="200" height="20"/>
          <element type="UIProgressBar" name="Blocking" bind="unit" x="10" y="210" width="200" height="20" blocksInput="true"/>
          <element type="UIProgressBar" name="Unbound" x="10" y="250" width="200" height="20"/>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        bool Same(const Color& a, const Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }

        bool Near(const Rect& a, const Rect& b, float tolerance = 1e-2f) {
            return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.w - b.w) <= tolerance && std::abs(a.h - b.h) <= tolerance;
        }

        eastl::string ElementToXml(const UIElement& element) {
            XmlSerializer serializer;
            EXPECT_TRUE(serializer.BeginObject("element"));
            element.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        }

        void SetVar(const char* name, const char* value) {
            CoreManager::GetUIVars().Set(PoolString::Intern(name), value);
        }

    }  // namespace

    // ---- the number helpers, shared with the slider -------------------------------------------------------------------------

    TEST(UIValueRangeTest, ParseFiniteFloatRefusesWhatIsNotAFiniteNumber) {
        EXPECT_FLOAT_EQ(*UIText::ParseFiniteFloat("12.5"), 12.5f);
        EXPECT_FLOAT_EQ(*UIText::ParseFiniteFloat("  -3 "), -3.0f);
        for (const char* text : {"", " ", "abc", "1x", "nan", "NaN", "inf", "-inf", "infinity", "1e99999"}) {
            EXPECT_FALSE(UIText::ParseFiniteFloat(text).has_value()) << '"' << text << '"';
        }
    }

    TEST(UIValueRangeTest, FractionInRangeClampsAndNeverDividesByZero) {
        EXPECT_FLOAT_EQ(UIText::FractionInRange(0.0f, 0.0f, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(0.5f, 0.0f, 1.0f), 0.5f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(1.0f, 0.0f, 1.0f), 1.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(25.0f, 0.0f, 100.0f), 0.25f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(-25.0f, -50.0f, 50.0f), 0.25f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(-7.0f, 0.0f, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(7.0f, 0.0f, 1.0f), 1.0f);

        EXPECT_FLOAT_EQ(UIText::FractionInRange(5.0f, 5.0f, 5.0f), 0.0f) << "an empty range";
        EXPECT_FLOAT_EQ(UIText::FractionInRange(5.0f, 10.0f, 0.0f), 0.0f) << "a reversed range";
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        EXPECT_FLOAT_EQ(UIText::FractionInRange(nan, 0.0f, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(inf, 0.0f, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(0.5f, nan, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::FractionInRange(0.5f, 0.0f, inf), 0.0f);
        const float big = std::numeric_limits<float>::max();
        EXPECT_FLOAT_EQ(UIText::FractionInRange(0.0f, -big, big), 0.5f) << "the span of two large floats does not overflow";
    }

    TEST(UIValueRangeTest, ValueInRangeIsTheClampedNumberOrTheMinimum) {
        EXPECT_FLOAT_EQ(UIText::ValueInRange("0.3", 0.0f, 1.0f), 0.3f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("-4", 0.0f, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("4", 0.0f, 1.0f), 1.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("", 2.0f, 8.0f), 2.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("abc", 2.0f, 8.0f), 2.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("nan", 2.0f, 8.0f), 2.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("inf", 2.0f, 8.0f), 2.0f);
        EXPECT_FLOAT_EQ(UIText::ValueInRange("5", 8.0f, 2.0f), 8.0f) << "a reversed range is its minimum";
        EXPECT_FLOAT_EQ(UIText::ValueInRange("5", 3.0f, 3.0f), 3.0f);
    }

    // ---- the control -------------------------------------------------------------------------------------------------------

    class UIProgressBarTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kBarXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();

            _canvas = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            _canvas.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void Draw() {
            _manager->SetActiveScene(PoolString::Intern("Main"));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        UIProgressBar& Bar(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            UIProgressBar* typed = element ? element->Cast<UIProgressBar>() : nullptr;
            EXPECT_NE(typed, nullptr) << name << " is not a progress bar";
            return *typed;
        }

        /// The rect calls of the last draw that lie inside @p area, in order
        eastl::vector<DrawCall> RectsIn(const Rect& area) const {
            eastl::vector<DrawCall> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && call.rect.x >= area.x - 1e-2f && call.rect.y >= area.y - 1e-2f && call.rect.x + call.rect.w <= area.x + area.w + 1e-2f &&
                    call.rect.y + call.rect.h <= area.y + area.h + 1e-2f) {
                    found.push_back(call);
                }
            }
            return found;
        }

        /// The width of the filled part drawn for the bar: 0 when only the track is there
        float FilledWidth(const char* name) {
            Draw();
            const Rect area = Bar(name).rect;
            const auto rects = RectsIn(area);
            EXPECT_FALSE(rects.empty()) << name << ": the track is always drawn";
            EXPECT_LE(rects.size(), 2u) << name;
            if (rects.size() < 2) {
                return 0.0f;
            }
            EXPECT_TRUE(Near(rects[0].rect, area)) << name << ": the track is the whole rect";
            EXPECT_NEAR(rects[1].rect.x, area.x, 1e-2f) << name << ": the fill starts at the left edge";
            EXPECT_NEAR(rects[1].rect.y, area.y, 1e-2f) << name;
            EXPECT_NEAR(rects[1].rect.h, area.h, 1e-2f) << name;
            return rects[1].rect.w;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    // ---- the fraction ------------------------------------------------------------------------------------------------------

    TEST_F(UIProgressBarTest, ABarFromAFileHasItsDefaults) {
        const auto& unit = Bar("Unit");
        EXPECT_FLOAT_EQ(unit.minValue, 0.0f);
        EXPECT_FLOAT_EQ(unit.maxValue, 1.0f);
        EXPECT_FALSE(unit.interactive);
        EXPECT_FALSE(unit.blocksInput);
        EXPECT_EQ(unit.bind, PoolString::Intern("unit"));
        EXPECT_FLOAT_EQ(Bar("Percent").maxValue, 100.0f);
        EXPECT_FLOAT_EQ(Bar("Signed").minValue, -50.0f);

        const auto made = AbstractFactory<UIElement>::GetInstance().Create("UIProgressBar");
        ASSERT_TRUE(made);
        ASSERT_NE(made->Cast<UIProgressBar>(), nullptr);
        EXPECT_FLOAT_EQ(made->Cast<UIProgressBar>()->maxValue, 1.0f);
    }

    TEST_F(UIProgressBarTest, TheFractionOfARangeZeroToOne) {
        auto& unit = Bar("Unit");
        SetVar("unit", "0");
        EXPECT_FLOAT_EQ(unit.GetFraction(), 0.0f);
        SetVar("unit", "0.5");
        EXPECT_FLOAT_EQ(unit.GetFraction(), 0.5f);
        SetVar("unit", "1");
        EXPECT_FLOAT_EQ(unit.GetFraction(), 1.0f);
    }

    TEST_F(UIProgressBarTest, TheFractionOfARangeZeroToOneHundred) {
        auto& percent = Bar("Percent");
        SetVar("percent", "0");
        EXPECT_FLOAT_EQ(percent.GetFraction(), 0.0f);
        SetVar("percent", "50");
        EXPECT_FLOAT_EQ(percent.GetFraction(), 0.5f);
        SetVar("percent", "100");
        EXPECT_FLOAT_EQ(percent.GetFraction(), 1.0f);
        SetVar("percent", "  25 ");
        EXPECT_FLOAT_EQ(percent.GetFraction(), 0.25f) << "white space around the number is allowed, as in every UI number";
    }

    TEST_F(UIProgressBarTest, TheFractionOfANegativeRange) {
        auto& range = Bar("Signed");
        SetVar("signed", "-50");
        EXPECT_FLOAT_EQ(range.GetFraction(), 0.0f);
        SetVar("signed", "0");
        EXPECT_FLOAT_EQ(range.GetFraction(), 0.5f);
        SetVar("signed", "50");
        EXPECT_FLOAT_EQ(range.GetFraction(), 1.0f);
        SetVar("signed", "-25");
        EXPECT_FLOAT_EQ(range.GetFraction(), 0.25f);
    }

    TEST_F(UIProgressBarTest, AValueOutsideTheRangeIsClamped) {
        SetVar("unit", "-3");
        EXPECT_FLOAT_EQ(Bar("Unit").GetFraction(), 0.0f);
        SetVar("unit", "3");
        EXPECT_FLOAT_EQ(Bar("Unit").GetFraction(), 1.0f);
        SetVar("signed", "-1000");
        EXPECT_FLOAT_EQ(Bar("Signed").GetFraction(), 0.0f);
        SetVar("signed", "1000");
        EXPECT_FLOAT_EQ(Bar("Signed").GetFraction(), 1.0f);
    }

    TEST_F(UIProgressBarTest, WhatIsNotANumberIsAnEmptyBar) {
        auto& unit = Bar("Unit");
        EXPECT_FLOAT_EQ(unit.GetFraction(), 0.0f) << "unset";
        for (const char* text : {"", "abc", "nan", "NaN", "inf", "-inf", "50%", "1,5", "0.5 0.6"}) {
            SetVar("unit", text);
            EXPECT_FLOAT_EQ(unit.GetFraction(), 0.0f) << '"' << text << '"';
        }
        EXPECT_FLOAT_EQ(Bar("Unbound").GetFraction(), 0.0f) << "no bind";
    }

    TEST_F(UIProgressBarTest, AnEmptyOrReversedRangeIsAnEmptyBar) {
        SetVar("reversed", "5");
        EXPECT_FLOAT_EQ(Bar("Reversed").GetFraction(), 0.0f);
        SetVar("reversed", "10");
        EXPECT_FLOAT_EQ(Bar("Reversed").GetFraction(), 0.0f);
        SetVar("reversed", "0");
        EXPECT_FLOAT_EQ(Bar("Reversed").GetFraction(), 0.0f);

        auto& unit = Bar("Unit");
        SetVar("unit", "0.5");
        unit.maxValue = unit.minValue;
        EXPECT_FLOAT_EQ(unit.GetFraction(), 0.0f);
        unit.maxValue = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FLOAT_EQ(unit.GetFraction(), 0.0f);
        unit.minValue = std::numeric_limits<float>::infinity();
        EXPECT_FLOAT_EQ(unit.GetFraction(), 0.0f);
    }

    // ---- drawing -----------------------------------------------------------------------------------------------------------

    TEST_F(UIProgressBarTest, TheTrackIsTheWholeRectAndTheFillIsAShareOfTheWidthFromTheLeft) {
        EXPECT_NEAR(FilledWidth("Unit"), 0.0f, 1e-3f) << "unset: the track only";

        SetVar("unit", "0.5");
        EXPECT_NEAR(FilledWidth("Unit"), 150.0f, 1e-2f);

        SetVar("unit", "0.25");
        EXPECT_NEAR(FilledWidth("Unit"), 75.0f, 1e-2f);

        SetVar("unit", "1");
        EXPECT_NEAR(FilledWidth("Unit"), 300.0f, 1e-2f);

        SetVar("percent", "40");
        EXPECT_NEAR(FilledWidth("Percent"), 80.0f, 1e-2f);

        SetVar("signed", "0");
        EXPECT_NEAR(FilledWidth("Signed"), 200.0f, 1e-2f);
    }

    TEST_F(UIProgressBarTest, TheFillNeverLeavesTheBar) {
        for (const char* text : {"-9", "0", "0.1", "0.999", "1", "9", "nan", "abc"}) {
            SetVar("unit", text);
            Draw();
            const Rect area = Bar("Unit").rect;
            for (const DrawCall& call : RectsIn(area)) {
                EXPECT_GE(call.rect.x, area.x - 1e-2f) << text;
                EXPECT_LE(call.rect.x + call.rect.w, area.x + area.w + 1e-2f) << text;
            }
            const float filled = FilledWidth("Unit");
            EXPECT_GE(filled, 0.0f) << text;
            EXPECT_LE(filled, area.w + 1e-2f) << text;
        }
        SetVar("reversed", "3");
        EXPECT_NEAR(FilledWidth("Reversed"), 0.0f, 1e-3f);
    }

    TEST_F(UIProgressBarTest, TheFillFollowsTheVariableAtTheNextDraw) {
        SetVar("unit", "0.2");
        EXPECT_NEAR(FilledWidth("Unit"), 60.0f, 1e-2f);
        SetVar("unit", "0.8");
        EXPECT_NEAR(FilledWidth("Unit"), 240.0f, 1e-2f);
        SetVar("unit", "oops");
        EXPECT_NEAR(FilledWidth("Unit"), 0.0f, 1e-3f);
    }

    TEST_F(UIProgressBarTest, TheElementFillComesFirstThenTheTrackThenTheFill) {
        SetVar("custom", "0.5");
        Draw();
        const auto rects = RectsIn(Bar("Custom").rect);
        ASSERT_EQ(rects.size(), 3u);
        EXPECT_EQ(rects[0].color.r, 1) << "the element fill";
        EXPECT_EQ(rects[1].color.r, 10) << "the track";
        EXPECT_EQ(rects[2].color.r, 40) << "the filled part";
        EXPECT_NEAR(rects[2].rect.w, 100.0f, 1e-2f);
    }

    TEST_F(UIProgressBarTest, TransparentColorsAreDerivedFromConstants) {
        SetVar("unit", "0.5");
        Draw();
        const auto rects = RectsIn(Bar("Unit").rect);
        ASSERT_EQ(rects.size(), 2u);
        EXPECT_TRUE(Same(rects[0].color, UIProgressBar::kTrackFallback));
        EXPECT_TRUE(Same(rects[1].color, kFillFallback));
        EXPECT_TRUE(Same(Bar("Unit").GetCurrentTrack(), UIProgressBar::kTrackFallback));
        EXPECT_EQ(Bar("Custom").GetCurrentFill().g, 50);
    }

    // ---- input -------------------------------------------------------------------------------------------------------------

    TEST_F(UIProgressBarTest, ABarTakesNoClick) {
        Draw();
        const Rect area = Bar("Unit").rect;
        const float x = area.x + area.w * 0.5f;
        const float y = area.y + area.h * 0.5f;
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << "the press goes to the game";
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false)));
        EXPECT_FALSE(Bar("Unit").pressed);
        EXPECT_FLOAT_EQ(Bar("Unit").GetFraction(), 0.0f) << "and nothing changed";
    }

    TEST_F(UIProgressBarTest, ABarThatBlocksInputTakesThePressButDoesNothingWithIt) {
        Draw();
        const Rect area = Bar("Blocking").rect;
        const float x = area.x + area.w * 0.5f;
        const float y = area.y + area.h * 0.5f;
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true)));
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false)));
        EXPECT_FALSE(Bar("Blocking").interactive);
    }

    TEST_F(UIProgressBarTest, ABarIsAContentFreeElementForTheValidator) {
        Draw();
        const UIContentFit fit = Bar("Unit").CheckContent(300.0f, 24.0f, 1.0f);
        EXPECT_FALSE(fit.hasContent);
        EXPECT_TRUE(fit.fits);
    }

    // ---- reflection --------------------------------------------------------------------------------------------------------

    TEST_F(UIProgressBarTest, TheFieldsSurviveAnXmlRoundTrip) {
        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("component"));
        _canvas->Serialize(serializer);
        serializer.EndObject();
        const eastl::string xml = serializer.SaveToString();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
        auto loaded = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_FALSE(deserializer.HasErrors());
        loaded->Initialize();

        const auto* signedBar = loaded->GetRoot().FindByName(PoolString::Intern("Signed"))->Cast<UIProgressBar>();
        ASSERT_NE(signedBar, nullptr);
        EXPECT_EQ(signedBar->bind, PoolString::Intern("signed"));
        EXPECT_FLOAT_EQ(signedBar->minValue, -50.0f);
        EXPECT_FLOAT_EQ(signedBar->maxValue, 50.0f);
        const auto* custom = loaded->GetRoot().FindByName(PoolString::Intern("Custom"))->Cast<UIProgressBar>();
        ASSERT_NE(custom, nullptr);
        EXPECT_EQ(custom->trackColor.g, 20);
        EXPECT_EQ(custom->fillColor.g, 50);
        const auto* unit = loaded->GetRoot().FindByName(PoolString::Intern("Unit"))->Cast<UIProgressBar>();
        ASSERT_NE(unit, nullptr);
        EXPECT_FLOAT_EQ(unit->maxValue, 1.0f) << "the defaults come back as the defaults";
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

    TEST_F(UIProgressBarTest, TheFieldsSurviveABinaryRoundTrip) {
        Bar("Unit").maxValue = 7.5f;
        BinarySerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("component"));
        _canvas->Serialize(serializer);
        serializer.EndObject();

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData().data(), serializer.GetData().size()));
        auto loaded = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_FALSE(deserializer.HasErrors());
        loaded->Initialize();

        const auto* custom = loaded->GetRoot().FindByName(PoolString::Intern("Custom"))->Cast<UIProgressBar>();
        ASSERT_NE(custom, nullptr);
        EXPECT_EQ(custom->fillColor.b, 60);
        EXPECT_EQ(loaded->GetRoot().FindByName(PoolString::Intern("Unit"))->Cast<UIProgressBar>()->maxValue, 7.5f);
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

}  // namespace BECore::Tests
