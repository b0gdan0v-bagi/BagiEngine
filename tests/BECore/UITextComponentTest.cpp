#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UILayoutValidator.h>
#include <BECore/UI/UITextComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/TextTestSupport.h>
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

        /// A colour as one number: Color has no comparison, and a failed EXPECT_EQ then prints the four channels in one
        uint32_t Pack(const Color& color) {
            return (static_cast<uint32_t>(color.r) << 24) | (static_cast<uint32_t>(color.g) << 16) | (static_cast<uint32_t>(color.b) << 8) | color.a;
        }

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

    class UITextComponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kTextXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _canvas = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
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

        UIElement& Get(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        UITextComponent& CaptionOf(const char* name) const {
            UITextComponent* caption = Get(name).GetComponent<UITextComponent>();
            EXPECT_NE(caption, nullptr) << name;
            return *caption;
        }

        /// The textured draws of the row of Cap in the last frame (the glyphs; the outline is drawn first, in its own colour). Narrow and Shrinking start at y 100
        eastl::vector<DrawCall> Glyphs() const {
            constexpr float kCapRowBottom = 95.0f;
            eastl::vector<DrawCall> glyphs;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.rect.y < kCapRowBottom) {
                    glyphs.push_back(call);
                }
            }
            return glyphs;
        }

        /// The colour of the last glyph (packed): the text itself comes after its outline and its shadow; 0 when nothing was drawn
        uint32_t TextColorDrawn() const {
            const auto glyphs = Glyphs();
            return glyphs.empty() ? 0u : Pack(glyphs.back().color);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    TEST_F(UITextComponentTest, TheFieldsComeFromTheFileUnderTheNamesOfALabel) {
        const UITextComponent& caption = CaptionOf("Cap");
        EXPECT_EQ(caption.text, "Score");
        EXPECT_EQ(Pack(caption.textColor), Pack(kTextColor));
        EXPECT_EQ(Pack(caption.hoverTextColor), Pack({10, 20, 30, 255}));
        EXPECT_EQ(Pack(caption.disabledTextColor), Pack({70, 80, 90, 100}));
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
        const eastl::string xml = toXml(Get("Cap"));
        EXPECT_NE(xml.find("hoverTextColor"), eastl::string::npos) << xml.c_str();
        EXPECT_NE(xml.find("textOutline"), eastl::string::npos) << xml.c_str();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
        auto loaded = New<UIElement>();
        ASSERT_TRUE(deserializer.BeginObject("element"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_STREQ(toXml(*loaded).c_str(), xml.c_str());
    }

    TEST_F(UITextComponentTest, TheTextIsDrawnInTheRectOfTheOwner) {
        Draw();
        const UIElement& cap = Get("Cap");
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
        const float small = Glyphs().back().rect.h;
        Get("Cap").style.fontSize = 48.0f;
        Get("Cap").style.rect.height = UILength::Px(80.0f);
        _canvas->Invalidate();
        Draw();
        EXPECT_GT(Glyphs().back().rect.h, small * 1.5f);
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
        const WarningCounter warnings("menu.nowhere");

        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(caption.GetDisplayText(), "menu.nowhere");
        }
        EXPECT_EQ(warnings.Count(), 1u);
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
        const WarningCounter warnings("font family 'Nope'");
        for (int frame = 0; frame < 3; ++frame) {
            Draw();
        }
        EXPECT_EQ(warnings.Count(), 1u);
        EXPECT_FALSE(Glyphs().empty());
    }

    // ---- the validator ---------------------------------------------------------------------------------------------------------

    TEST_F(UITextComponentTest, ATextThatDoesNotFitAndCannotShrinkIsReported) {
        Draw();
        EXPECT_EQ(CountTextIssues(*_canvas, "Narrow"), 1u);
        EXPECT_EQ(CountTextIssues(*_canvas, "Cap"), 0u);
        EXPECT_EQ(CountTextIssues(*_canvas, "Shrinking"), 0u) << "a text that shrinks is no defect";

        const UIElement& narrow = Get("Narrow");
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
        Get("Narrow").style.rect.width = UILength::Px(120.0f);
        _canvas->Invalidate();

        Draw();
        EXPECT_EQ(CountTextIssues(*_canvas, "Narrow"), 0u);
        strings.SetLanguage("de"_intern);
        Draw();
        EXPECT_EQ(CountTextIssues(*_canvas, "Narrow"), 1u);
    }

    TEST_F(UITextComponentTest, MeasureGivesTheSizeOfTheShownText) {
        Draw();
        const UIElement& cap = Get("Cap");
        const Vec2 size = CaptionOf("Cap").MeasureText(cap, 200.0f, 1.0f);
        EXPECT_GT(size.x, 0.0f);
        EXPECT_LT(size.x, 200.0f);
        EXPECT_GT(size.y, 0.0f);
        CaptionOf("Cap").text = "";
        EXPECT_EQ(CaptionOf("Cap").MeasureText(cap, 200.0f, 1.0f).x, 0.0f);
    }

    // ---- the state -------------------------------------------------------------------------------------------------------------

    TEST_F(UITextComponentTest, TheTextTakesTheColourOfTheStateOfItsOwner) {
        UIElement& cap = Get("Cap");
        const UITextComponent& caption = CaptionOf("Cap");
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(kTextColor));

        cap.hovered = true;
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(caption.hoverTextColor));

        cap.pressed = true;
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(caption.pressedTextColor));

        cap.pressed = false;
        cap.hovered = false;
        cap.selected = true;
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(caption.selectedTextColor));

        cap.groupDisabled = true;
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(caption.disabledTextColor)) << "disabled wins over selected";
    }

    TEST_F(UITextComponentTest, APressThatLeftTheOwnerIsNotShownAsPressed) {
        UIElement& cap = Get("Cap");
        cap.pressed = true;
        cap.hovered = false;
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(kTextColor));
    }

    TEST_F(UITextComponentTest, AStateWithNoColourLeavesTheTextColourAlone) {
        UITextComponent& caption = CaptionOf("Cap");
        caption.hoverTextColor = UIColor{};
        caption.disabledTextColor = UIColor{};
        UIElement& cap = Get("Cap");
        cap.hovered = true;
        cap.groupDisabled = true;
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(kTextColor));
    }

    TEST_F(UITextComponentTest, FadeWhenDisabledDrawsTheTextAtHalfAlphaUnlessTheStateHasAColourOfItsOwn) {
        UITextComponent& caption = CaptionOf("Cap");
        caption.disabledTextColor = UIColor{};
        UIElement& cap = Get("Cap");
        cap.groupDisabled = true;
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(kTextColor)) << "off by default: a button does not fade its caption";

        caption.fadeWhenDisabled = true;
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(WithAlpha(caption.textColor, kDisabledAlpha)));

        cap.groupDisabled = false;
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(kTextColor)) << "only while disabled";

        caption.disabledTextColor = UIColor{70, 80, 90, 255};
        cap.groupDisabled = true;
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(caption.disabledTextColor)) << "a colour of the state wins";
    }

    TEST_F(UITextComponentTest, ADisabledTextFadesItsOutlineByTheSameShare) {
        Draw();
        const auto outlineAlpha = [this]() {
            const auto glyphs = Glyphs();
            return glyphs.empty() ? -1 : static_cast<int>(glyphs.front().color.a);
        };
        const int plain = outlineAlpha();
        EXPECT_EQ(plain, 255);

        Get("Cap").groupDisabled = true;
        Draw();
        EXPECT_LT(outlineAlpha(), plain) << "the outline fades with the text";
    }

    TEST_F(UITextComponentTest, TheHooksOfAPlainOwnerLeaveItsTextAndItsAreaAlone) {
        Draw();
        UIElement& cap = Get("Cap");
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
        EXPECT_EQ(Pack(Get("Cap").GetStateAccent()), Pack(kTextColor));
        EXPECT_EQ(Pack(Get("Narrow").GetStateAccent()), Pack({255, 255, 255, 255})) << "the text component of Narrow keeps the default white";
    }

}  // namespace BECore::Tests
