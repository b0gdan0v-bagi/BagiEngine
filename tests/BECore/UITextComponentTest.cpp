#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIColor.h>
#include <BECore/UI/UILayoutValidator.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UITooltip.h>
#include <BECore/UI/UIVars.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/TextTestSupport.h>
#include <Common/UITestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Cap (10, 10, 200 x 30) is an element with a text component,
        // Narrow (10, 100, 40 x 40) holds a long text that is clipped, Shrinking the same text that shrinks to fit
        constexpr const char* kTextXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Cap" x="10" y="10" width="200" height="30" fontSize="24">
            <components>
              <component type="UITextComponent" text="Score" textColor="200,100,50,255" hoverTextColor="10,20,30,255" pressedTextColor="40,50,60,255"
                         disabledTextColor="70,80,90,100" selectedTextColor="110,120,130,255" textOutline="2,0,0,0,255" align="Center" verticalAlign="Center"/>
            </components>
          </element>
          <element type="UIElement" name="Narrow" x="10" y="100" width="40" height="40">
            <components>
              <component type="UITextComponent" text="A text that is much too long for this box" wrap="false" overflow="Clip"/>
            </components>
          </element>
          <element type="UIElement" name="Shrinking" x="10" y="200" width="40" height="40">
            <components>
              <component type="UITextComponent" text="A text that is much too long for this box" wrap="false" overflow="AutoFit"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        const Color kTextColor{200, 100, 50, 255};

        bool SameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

        size_t CountTextIssues(UICanvasComponent& canvas, const char* element) {
            size_t count = 0;
            for (const UILayoutIssue& issue : canvas.GetLayout().Validate({})) {
                count += issue.kind == UILayoutIssueKind::TextDoesNotFit && issue.element == PoolString::Intern(element) ? 1u : 0u;
            }
            return count;
        }

    }  // namespace

    class UITextComponentTest : public UISceneFixture {
    protected:
        void SetUp() override {
            UISceneFixture::SetUp();
            ASSERT_NO_FATAL_FAILURE(LoadScene(kTextXml));
            ASSERT_TRUE(_canvas);
        }

        UITextComponent& CaptionOf(const char* name) const {
            UITextComponent* caption = El(name).GetComponent<UITextComponent>();
            EXPECT_NE(caption, nullptr) << name;
            return Require(caption);
        }

        /// The textured draws of the row of Cap in the last frame (the glyphs; the outline is drawn first, in its own colour). Narrow and Shrinking start at y 100
        Vector<DrawCall> Glyphs() const {
            constexpr float kCapRowBottom = 95.0f;
            Vector<DrawCall> glyphs;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.rect.y < kCapRowBottom) {
                    glyphs.push_back(call);
                }
            }
            return glyphs;
        }

        /// The colour of the last glyph: the text itself comes after its outline and its shadow; all zero when nothing was drawn
        Color TextColorDrawn() const {
            const auto glyphs = Glyphs();
            return glyphs.empty() ? Color{} : glyphs.back().color;
        }
    };

    TEST_F(UITextComponentTest, TheFieldsComeFromTheFile) {
        const UITextComponent& caption = CaptionOf("Cap");
        EXPECT_EQ(caption.text, "Score");
        EXPECT_PRED2(ColorsEqual, caption.textColor, kTextColor);
        EXPECT_PRED2(ColorsEqual, caption.hoverTextColor, (Color{10, 20, 30, 255}));
        EXPECT_PRED2(ColorsEqual, caption.disabledTextColor, (Color{70, 80, 90, 100}));
        EXPECT_EQ(caption.align, TextAlign::Center);
        EXPECT_EQ(caption.verticalAlign, UIVerticalAlign::Center);
        EXPECT_EQ(caption.textEffects.outlineWidth, 2.0f);
        EXPECT_EQ(caption.overflow, TextOverflow::Ellipsis);
        EXPECT_TRUE(caption.wrap);
    }

    TEST_F(UITextComponentTest, TheTextComponentSavesAndLoadsBack) {
        const auto toXml = [](const UIElement& element) {
            XmlSerializer serializer;
            EXPECT_TRUE(serializer.BeginObject("element"));
            element.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        };
        const String xml = toXml(El("Cap"));
        EXPECT_NE(xml.find("hoverTextColor"), String::npos) << xml.c_str();
        EXPECT_NE(xml.find("textOutline"), String::npos) << xml.c_str();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(StringView(xml)));
        auto loaded = New<UIElement>();
        ASSERT_TRUE(deserializer.BeginObject("element"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_STREQ(toXml(*loaded).c_str(), xml.c_str());
    }

    TEST_F(UITextComponentTest, TheTextIsDrawnInTheRectOfTheOwner) {
        Draw();
        const UIElement& cap = El("Cap");
        const auto glyphs = Glyphs();
        ASSERT_FALSE(glyphs.empty());
        for (const DrawCall& glyph : glyphs) {
            EXPECT_GE(glyph.rect.x, cap.rect.x - 4.0f);
            EXPECT_LE(glyph.rect.x + glyph.rect.w, cap.rect.x + cap.rect.w + 4.0f);
            EXPECT_GE(glyph.rect.y, cap.rect.y - 4.0f);
            EXPECT_LE(glyph.rect.y + glyph.rect.h, cap.rect.y + cap.rect.h + 4.0f);
        }
    }

    TEST_F(UITextComponentTest, TheSizeIsTheFontSizeOfTheOwner) {
        Draw();
        const float small = Back(Glyphs()).rect.h;
        El("Cap").style.fontSize = 48.0f;
        El("Cap").style.rect.height = UILength::Px(80.0f);
        _canvas->Invalidate();
        Draw();
        EXPECT_GT(Back(Glyphs()).rect.h, small * 1.5f);
    }

    // ---- the strings and the variables ------------------------------------------------------------------------------------

    TEST_F(UITextComponentTest, ItShowsTheStringOfTheCurrentLanguageAndFollowsASwitch) {
        UIStrings& strings = CoreManager::GetUIStrings();
        strings.Set("en"_intern, "menu.start", "Start");
        strings.Set("ru"_intern, "menu.start", "Начать");
        strings.SetDefaultLanguage("en"_intern);
        strings.SetLanguage("en"_intern);
        UITextComponent& caption = CaptionOf("Cap");
        caption.text = "{#menu.start}";

        EXPECT_EQ(caption.GetDisplayText(), "Start");
        strings.SetLanguage("ru"_intern);
        EXPECT_EQ(caption.GetDisplayText(), "Начать");
        strings.SetLanguage("en"_intern);
        EXPECT_EQ(caption.GetDisplayText(), "Start");
    }

    TEST_F(UITextComponentTest, AKeyMissingEverywhereShowsTheKeyAndIsLoggedOnce) {
        UITextComponent& caption = CaptionOf("Cap");
        caption.text = "{#menu.nowhere}";
        const LogRecorder warnings;

        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(caption.GetDisplayText(), "menu.nowhere");
        }
        LogEvent::Flush();
        EXPECT_EQ(warnings.Count(LogLevel::Warning, {"menu.nowhere"}), 1u);
    }

    TEST_F(UITextComponentTest, ItFollowsAVariable) {
        UITextComponent& caption = CaptionOf("Cap");
        caption.text = "Score: {score}";
        UIVars& vars = CoreManager::GetUIVars();

        EXPECT_EQ(caption.GetDisplayText(), "Score: ");
        vars.Set("score"_intern, 5);
        EXPECT_EQ(caption.GetDisplayText(), "Score: 5");
        vars.Set("score"_intern, 100);
        Draw();
        EXPECT_FALSE(Glyphs().empty());
        EXPECT_EQ(caption.GetDisplayText(), "Score: 100");
    }

    TEST_F(UITextComponentTest, ADisplayTextIsExpandedAgainOnlyWhenWhatItReadsMoved) {
        UIStrings& strings = CoreManager::GetUIStrings();
        strings.Set("en"_intern, "k", "Hello");
        strings.Set("ru"_intern, "k", "Privet");
        strings.SetLanguage("en"_intern);
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("score"_intern, 1);
        UITextComponent& caption = CaptionOf("Cap");
        caption.text = "{#k} {score}";

        EXPECT_EQ(caption.GetDisplayText(), "Hello 1");
        const uint32_t first = caption.GetExpandCount();
        EXPECT_EQ(caption.GetDisplayText(), "Hello 1");
        EXPECT_EQ(caption.GetExpandCount(), first);

        strings.SetLanguage("ru"_intern);
        EXPECT_EQ(caption.GetDisplayText(), "Privet 1");
        EXPECT_EQ(caption.GetExpandCount(), first + 1);

        vars.Set("score"_intern, 2);
        EXPECT_EQ(caption.GetDisplayText(), "Privet 2");
        EXPECT_EQ(caption.GetExpandCount(), first + 2);

        EXPECT_EQ(caption.GetDisplayText(), "Privet 2");
        EXPECT_EQ(caption.GetExpandCount(), first + 2);
    }

    TEST_F(UITextComponentTest, ATextWithNoStringIsLeftAloneByALanguageSwitch) {
        UIStrings& strings = CoreManager::GetUIStrings();
        strings.Set("en"_intern, "k", "Hello");
        strings.Set("ru"_intern, "k", "Privet");
        strings.SetLanguage("en"_intern);
        CoreManager::GetUIVars().Set("score"_intern, 7);
        UITextComponent& caption = CaptionOf("Cap");
        caption.text = "Score {score}";

        EXPECT_EQ(caption.GetDisplayText(), "Score 7");
        const uint32_t first = caption.GetExpandCount();
        strings.SetLanguage("ru"_intern);
        strings.Set("ru"_intern, "k", "Else");
        EXPECT_EQ(caption.GetDisplayText(), "Score 7");
        EXPECT_EQ(caption.GetExpandCount(), first);
    }

    TEST_F(UITextComponentTest, ASteadyDisplayTextAllocatesNothing) {
        CoreManager::GetUIStrings().Set("en"_intern, "k", "Hello");
        CoreManager::GetUIVars().Set("score"_intern, 7);
        UITextComponent& caption = CaptionOf("Cap");
        caption.text = "{#k}: {score}";
        ASSERT_EQ(caption.GetDisplayText(), "Hello: 7");

        ExpectNoAllocations([&] {
            for (int frame = 0; frame < 100; ++frame) {
                static_cast<void>(caption.GetDisplayText());
            }
        });
    }

    TEST_F(UITextComponentTest, AnUnchangedTextIsLaidOutOnce) {
        UITextComponent& caption = CaptionOf("Cap");
        caption.text = "Score: {score}";
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("score"_intern, 5);

        Draw();
        const uint32_t afterFirst = caption.GetLayoutCount();
        EXPECT_GT(afterFirst, 0u);
        Draw();
        Draw();
        EXPECT_EQ(caption.GetLayoutCount(), afterFirst);

        vars.Set("score"_intern, 6);
        Draw();
        EXPECT_EQ(caption.GetLayoutCount(), afterFirst + 1);
    }

    TEST_F(UITextComponentTest, AnUnknownFamilyIsLoggedOnceAndTheFaceOfAFamilyIsChosenByItsWeight) {
        const ScopedTextSettings settings(MakeTestFamilySettings());
        UITextComponent& caption = CaptionOf("Cap");
        caption.fontFamily = "Test"_intern;
        caption.fontWeight = 700;
        EXPECT_EQ(caption.GetResolvedFontPath(), PoolString::Intern(kTestBoldPath));

        caption.fontFamily = "Nope"_intern;
        const LogRecorder warnings;
        for (int frame = 0; frame < 3; ++frame) {
            Draw();
        }
        LogEvent::Flush();
        EXPECT_EQ(warnings.Count(LogLevel::Warning, {"font family 'Nope'"}), 1u);
        EXPECT_FALSE(Glyphs().empty());
    }

    // ---- the validator ---------------------------------------------------------------------------------------------------------

    TEST_F(UITextComponentTest, ATextThatDoesNotFitAndCannotShrinkIsReported) {
        Draw();
        EXPECT_EQ(CountTextIssues(*_canvas, "Narrow"), 1u);
        EXPECT_EQ(CountTextIssues(*_canvas, "Cap"), 0u);
        EXPECT_EQ(CountTextIssues(*_canvas, "Shrinking"), 0u) << "a text that shrinks is no defect";

        const UIElement& narrow = El("Narrow");
        EXPECT_FALSE(narrow.CheckContent(40.0f, 40.0f, 1.0f).fits);
        EXPECT_TRUE(narrow.CheckContent(800.0f, 40.0f, 1.0f).fits);
    }

    TEST_F(UITextComponentTest, TheCheckMeasuresTheStringOfTheCurrentLanguage) {
        UIStrings& strings = CoreManager::GetUIStrings();
        strings.Set("en"_intern, "line", "Short");
        strings.Set("de"_intern, "line", "A value that is much too long for a box of this width");
        strings.SetDefaultLanguage("en"_intern);
        UITextComponent& caption = CaptionOf("Narrow");
        caption.text = "{#line}";
        El("Narrow").style.rect.width = UILength::Px(120.0f);
        _canvas->Invalidate();

        Draw();
        EXPECT_EQ(CountTextIssues(*_canvas, "Narrow"), 0u);
        strings.SetLanguage("de"_intern);
        Draw();
        EXPECT_EQ(CountTextIssues(*_canvas, "Narrow"), 1u);
    }

    TEST_F(UITextComponentTest, MeasureGivesTheSizeOfTheShownText) {
        Draw();
        const UIElement& cap = El("Cap");
        const Vec2 size = CaptionOf("Cap").MeasureText(cap, 200.0f, 1.0f);
        EXPECT_GT(size.x, 0.0f);
        EXPECT_LT(size.x, 200.0f);
        EXPECT_GT(size.y, 0.0f);
        CaptionOf("Cap").text = "";
        EXPECT_EQ(CaptionOf("Cap").MeasureText(cap, 200.0f, 1.0f).x, 0.0f);
    }

    // ---- the state -------------------------------------------------------------------------------------------------------------

    TEST_F(UITextComponentTest, TheTextTakesTheColourOfTheStateOfItsOwner) {
        UIElement& cap = El("Cap");
        const UITextComponent& caption = CaptionOf("Cap");
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), kTextColor);

        cap.hovered = true;
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), caption.hoverTextColor);

        cap.pressed = true;
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), caption.pressedTextColor);

        cap.pressed = false;
        cap.hovered = false;
        cap.selected = true;
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), caption.selectedTextColor);

        cap.groupDisabled = true;
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), caption.disabledTextColor) << "disabled wins over selected";
    }

    TEST_F(UITextComponentTest, APressThatLeftTheOwnerIsNotShownAsPressed) {
        UIElement& cap = El("Cap");
        cap.pressed = true;
        cap.hovered = false;
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), kTextColor);
    }

    TEST_F(UITextComponentTest, AStateWithNoColourLeavesTheTextColourAlone) {
        UITextComponent& caption = CaptionOf("Cap");
        caption.hoverTextColor = UIColor{};
        caption.disabledTextColor = UIColor{};
        UIElement& cap = El("Cap");
        cap.hovered = true;
        cap.groupDisabled = true;
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), kTextColor);
    }

    TEST_F(UITextComponentTest, FadeWhenDisabledDrawsTheTextAtHalfAlphaUnlessTheStateHasAColourOfItsOwn) {
        UITextComponent& caption = CaptionOf("Cap");
        caption.disabledTextColor = UIColor{};
        UIElement& cap = El("Cap");
        cap.groupDisabled = true;
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), kTextColor) << "off by default: a button does not fade its caption";

        caption.fadeWhenDisabled = true;
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), WithAlpha(caption.textColor, kDisabledAlpha));

        cap.groupDisabled = false;
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), kTextColor) << "only while disabled";

        caption.disabledTextColor = UIColor{70, 80, 90, 255};
        cap.groupDisabled = true;
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), caption.disabledTextColor) << "a colour of the state wins";
    }

    TEST_F(UITextComponentTest, ADisabledTextFadesItsOutlineByTheSameShare) {
        Draw();
        const auto outlineAlpha = [this]() {
            const auto glyphs = Glyphs();
            return glyphs.empty() ? -1 : static_cast<int>(glyphs.front().color.a);
        };
        const int plain = outlineAlpha();
        EXPECT_EQ(plain, 255);

        El("Cap").groupDisabled = true;
        Draw();
        EXPECT_LT(outlineAlpha(), plain) << "the outline fades with the text";
    }

    TEST_F(UITextComponentTest, TheHooksOfAPlainOwnerLeaveItsTextAndItsAreaAlone) {
        Draw();
        UIElement& cap = El("Cap");
        EXPECT_TRUE(SameRect(cap.GetComponentArea(), cap.rect)) << "the area is the rect";
        EXPECT_EQ(cap.GetTextOverride(), nullptr);
        EXPECT_EQ(CaptionOf("Cap").GetDisplayText(cap), CaptionOf("Cap").GetDisplayText());
        EXPECT_EQ(CaptionOf("Cap").GetDisplayText(cap), "Score");
        for (const DrawCall& glyph : Glyphs()) {
            EXPECT_GE(glyph.rect.x, cap.rect.x - 1e-2f);
            EXPECT_LE(glyph.rect.x + glyph.rect.w, cap.rect.x + cap.rect.w + 1e-2f);
        }
    }

    TEST_F(UITextComponentTest, TheStateAccentOfAnElementWithATextComponentIsItsTextColour) {
        EXPECT_PRED2(ColorsEqual, El("Cap").GetStateAccent(), kTextColor);
        EXPECT_PRED2(ColorsEqual, El("Narrow").GetStateAccent(), (Color{255, 255, 255, 255})) << "the text component of Narrow keeps the default white";
    }

    // ---- MeasureText ---------------------------------------------------------------------------------------------------------

    constexpr float kMeasureEpsilon = 1e-3f;

    class UITextMeasureTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        /// A text element at the tooltip's font size
        struct Text {
            IntrusivePtr<UIElement> element;

            [[nodiscard]] Vec2 MeasureText(float maxWidth, float scale) const {
                return Require(element->GetComponent<UITextComponent>()).MeasureText(*element, maxWidth, scale);
            }
        };

        static Text MakeLabel(const char* text) {
            auto element = New<UIElement>();
            element->AddComponent<UITextComponent>().text = text;
            element->active.fontSize = UITooltip::kFontSize;
            return {element};
        }
    };

    TEST_F(UITextMeasureTest, AShortTextIsOneLineNarrowerThanTheLimit) {
        const auto label = MakeLabel("Settings");
        const Vec2 size = label.MeasureText(UITooltip::kMaxWidth, 1.0f);

        EXPECT_GT(size.x, 0.0f);
        EXPECT_LT(size.x, UITooltip::kMaxWidth);
        EXPECT_GE(size.y, UITooltip::kFontSize) << "a line is at least as high as the font is";
        EXPECT_LT(size.y, UITooltip::kFontSize * 2.0f) << "one line";
    }

    TEST_F(UITextMeasureTest, ALongTextWrapsAtTheLimitAndGrowsTaller) {
        const auto shortLabel = MakeLabel("Settings");
        const auto longLabel = MakeLabel("Settings of the game: the sound, the language and the size of everything on the screen");
        const Vec2 shortSize = shortLabel.MeasureText(UITooltip::kMaxWidth, 1.0f);
        const Vec2 longSize = longLabel.MeasureText(UITooltip::kMaxWidth, 1.0f);

        EXPECT_LE(longSize.x, UITooltip::kMaxWidth + kMeasureEpsilon);
        EXPECT_GT(longSize.x, shortSize.x);
        EXPECT_GT(longSize.y, shortSize.y * 1.5f) << "several lines";
    }

    TEST_F(UITextMeasureTest, ANarrowerLimitMakesItTaller) {
        const auto label = MakeLabel("Settings of the game and the sound");
        const Vec2 wide = label.MeasureText(400.0f, 1.0f);
        const Vec2 narrow = label.MeasureText(100.0f, 1.0f);

        EXPECT_LE(narrow.x, 100.0f + kMeasureEpsilon);
        EXPECT_GT(narrow.y, wide.y);
    }

    TEST_F(UITextMeasureTest, TheSizeIsInCanvasUnitsWhateverTheScale) {
        const auto label = MakeLabel("Settings");
        const Vec2 one = label.MeasureText(UITooltip::kMaxWidth, 1.0f);
        const Vec2 two = label.MeasureText(UITooltip::kMaxWidth, 2.0f);

        EXPECT_NEAR(two.x, one.x, one.x * 0.1f) << "twice the pixels, twice the divisor";
        EXPECT_NEAR(two.y, one.y, one.y * 0.1f);
    }

    TEST_F(UITextMeasureTest, AChangedVariableInTheTextIsMeasuredAgain) {
        UIVars& vars = CoreManager::GetUIVars();
        const PoolString name = "measure_var"_intern;
        const auto label = MakeLabel("{measure_var}");
        vars.Set(name, "Hi");
        const Vec2 shortSize = label.MeasureText(UITooltip::kMaxWidth, 1.0f);
        EXPECT_EQ(label.MeasureText(UITooltip::kMaxWidth, 1.0f), shortSize) << "the same question, the same answer";

        vars.Set(name, "A much longer value of the variable");
        EXPECT_GT(label.MeasureText(UITooltip::kMaxWidth, 1.0f).x, shortSize.x);
        EXPECT_EQ(label.MeasureText(UITooltip::kMaxWidth, 1.0f).x, label.MeasureText(UITooltip::kMaxWidth, 1.0f).x);
        vars.Clear();
    }

    TEST_F(UITextMeasureTest, AnEmptyTextOrNoRoomIsZero) {
        const auto empty = MakeLabel("");
        EXPECT_EQ(empty.MeasureText(UITooltip::kMaxWidth, 1.0f), Vec2{});

        const auto label = MakeLabel("Settings");
        EXPECT_EQ(label.MeasureText(0.0f, 1.0f), Vec2{});
        EXPECT_EQ(label.MeasureText(UITooltip::kMaxWidth, 0.0f), Vec2{});
    }

}  // namespace BECore::Tests
