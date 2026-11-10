#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Text/TextComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/TextTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Output 800 x 600. "Plain" has no view size (one unit is one pixel); "Fit" shows 400 x 300 units, so its scale is 2.
        constexpr const char* kTextScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Plain"><nodes>
    <node name="Label"><components>
      <component type="TransformComponent" x="10" y="20" width="0" height="0"/>
      <component type="TextComponent" text="Hello" fontSize="24">
        <color r="255" g="128" b="0" a="200"/>
      </component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Fit" viewWidth="400" viewHeight="300"><nodes>
    <node name="Label"><components>
      <component type="TransformComponent" x="10" y="20" width="0" height="0"/>
      <component type="TextComponent" text="Hello" fontSize="24"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Styled"><nodes>
    <node name="Label"><components>
      <component type="TransformComponent" x="0" y="0" width="200" height="80"/>
      <component type="TextComponent" text="Привет, мир" fontSize="20" align="Center" overflow="Ellipsis" wrap="false" lineSpacing="1.5" minFontSize="9"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr size_t kInkedGlyphsInHello = 5;

    }  // namespace

    class TextComponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kTextScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        IntrusivePtr<TextComponent> Label(const char* sceneName) const {
            Scene& scene = *_manager->GetSceneByName(PoolString::Intern(sceneName));
            return scene.GetRootNode().GetChildren().front()->GetComponent<TextComponent>();
        }

        void Draw(const char* sceneName) {
            _manager->SetActiveScene(PoolString::Intern(sceneName));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(TextComponentTest, TheFieldsComeFromTheSceneConfig) {
        auto label = Label("Styled");

        ASSERT_TRUE(label);
        EXPECT_EQ(label->_text, "Привет, мир");
        EXPECT_FLOAT_EQ(label->_fontSize, 20.0f);
        EXPECT_EQ(label->_align, TextAlign::Center);
        EXPECT_EQ(label->_overflow, TextOverflow::Ellipsis);
        EXPECT_FALSE(label->_wrap);
        EXPECT_FLOAT_EQ(label->_lineSpacing, 1.5f);
        EXPECT_FLOAT_EQ(label->_minFontSize, 9.0f);
        EXPECT_EQ(Label("Plain")->_color.g, 128);
    }

    TEST_F(TextComponentTest, DrawsOneQuadPerGlyph) {
        Draw("Plain");

        EXPECT_EQ(_renderer->calls.size(), kInkedGlyphsInHello);
        for (const DrawCall& call : _renderer->calls) {
            EXPECT_EQ(call.kind, DrawKind::Texture);
        }
    }

    TEST_F(TextComponentTest, TheColorIsTheTint) {
        Draw("Plain");

        ASSERT_FALSE(_renderer->calls.empty());
        EXPECT_EQ(_renderer->calls[0].color.r, 255);
        EXPECT_EQ(_renderer->calls[0].color.g, 128);
        EXPECT_EQ(_renderer->calls[0].color.b, 0);
        EXPECT_EQ(_renderer->calls[0].color.a, 200);
    }

    TEST_F(TextComponentTest, TheTextStartsAtTheTransformOrigin) {
        Draw("Plain");

        ASSERT_FALSE(_renderer->calls.empty());
        const DrawCall& first = _renderer->calls[0];
        const TextLayoutResult& layout = Label("Plain")->GetLastLayout();
        // The pen of the first glyph is at the origin; the picture is where the glyph's offset puts it
        EXPECT_NEAR(first.rect.x, 10.0f, 4.0f);
        EXPECT_GT(first.rect.y, 20.0f - 1.0f);
        EXPECT_LT(first.rect.y, 20.0f + layout.lineHeight);
    }

    TEST_F(TextComponentTest, TheGlyphsGoLeftToRight) {
        Draw("Plain");

        ASSERT_EQ(_renderer->calls.size(), kInkedGlyphsInHello);
        for (size_t i = 1; i < _renderer->calls.size(); ++i) {
            EXPECT_GT(_renderer->calls[i].rect.x, _renderer->calls[i - 1].rect.x);
        }
    }

    TEST_F(TextComponentTest, AChangedFamilyOrWeightLoadsTheOtherFaceByItself) {
        const ScopedTextSettings settings(MakeTestFamilySettings());
        auto label = Label("Plain");
        label->_fontFamily = "Test"_intern;
        label->_fontWeight = 400;

        Draw("Plain");
        EXPECT_EQ(CoreManager::GetFontAtlasLibrary().GetAtlasCount(), 1u);
        label->_fontWeight = 700;
        Draw("Plain");
        EXPECT_EQ(CoreManager::GetFontAtlasLibrary().GetAtlasCount(), 2u);  // the bold face is its own font, so its own atlas
        EXPECT_EQ(_renderer->calls.size(), kInkedGlyphsInHello);
        label->_fontWeight = 600;  // the same face again: nothing new
        Draw("Plain");
        EXPECT_EQ(CoreManager::GetFontAtlasLibrary().GetAtlasCount(), 2u);
    }

    TEST_F(TextComponentTest, APathInFontPathWinsOverFamilyAndAnUnknownFamilyIsLoggedOnce) {
        const ScopedTextSettings settings(MakeTestFamilySettings());
        const WarningCounter warnings("font family");
        auto label = Label("Plain");
        label->_fontFamily = "Nope"_intern;
        for (int frame = 0; frame < 3; ++frame) {
            Draw("Plain");
        }
        EXPECT_EQ(warnings.Count(), 1u);
        EXPECT_EQ(_renderer->calls.size(), kInkedGlyphsInHello);  // drawn with the default font

        label->_fontPath = PoolString::Intern(kTestBoldPath);
        label->_fontFamily = "Other"_intern;
        Draw("Plain");
        EXPECT_EQ(warnings.Count(), 1u);
        EXPECT_EQ(CoreManager::GetFontAtlasLibrary().GetAtlasCount(), 2u);
    }

    TEST_F(TextComponentTest, TheAtlasMatchesTheSizeTimesTheViewScale) {
        Draw("Plain");
        const TextLayoutResult plain = Label("Plain")->GetLastLayout();
        Draw("Fit");
        const TextLayoutResult fitted = Label("Fit")->GetLastLayout();

        EXPECT_FLOAT_EQ(plain.fontSize, 24.0f);
        EXPECT_FLOAT_EQ(fitted.fontSize, 48.0f);  // 24 units at a scale of 2
        EXPECT_EQ(CoreManager::GetFontAtlasLibrary().GetAtlasCount(), 2u);
    }

    TEST_F(TextComponentTest, UnderAScaleTheQuadsAreStillOneToOneInOutputPixels) {
        Draw("Fit");

        ASSERT_EQ(_renderer->calls.size(), kInkedGlyphsInHello);
        const TextLayoutResult& layout = Label("Fit")->GetLastLayout();
        // The origin (10, 20) is at output (20, 40) under the scale of 2; glyphs of a 48 px atlas are at least a few px wide
        EXPECT_NEAR(_renderer->calls[0].rect.x, 20.0f, 8.0f);
        for (const DrawCall& call : _renderer->calls) {
            EXPECT_EQ(call.rect.w, call.source->w);
            EXPECT_EQ(call.rect.h, call.source->h);
        }
        EXPECT_GT(layout.width, 24.0f * 2.0f);  // wider than the same text at 24 px would be
    }

    TEST_F(TextComponentTest, AnEllipsisEndsTheTextThatDoesNotFit) {
        Label("Styled")->SetText("Очень длинная подпись, которая не помещается в коробку");
        Draw("Styled");

        const TextLayoutResult& layout = Label("Styled")->GetLastLayout();
        EXPECT_TRUE(layout.truncated);
        EXPECT_EQ(layout.glyphs.back().codePoint, 0x2026u);
        EXPECT_LE(layout.width, 200.0f + 0.1f);
    }

    TEST_F(TextComponentTest, CenterAlignmentCentersTheLineInTheBox) {
        Draw("Styled");

        const TextLayoutResult& layout = Label("Styled")->GetLastLayout();
        ASSERT_EQ(layout.lines.size(), 1u);
        EXPECT_NEAR(layout.lines[0].x + layout.lines[0].width * 0.5f, 100.0f, 0.1f);
    }

    TEST_F(TextComponentTest, ClipPushesTheBoxAroundATextThatOverflows) {
        auto label = Label("Styled");
        label->_overflow = TextOverflow::Clip;
        label->SetText("Очень длинная подпись, которая не помещается в коробку");
        Draw("Styled");

        ASSERT_FALSE(_renderer->calls.empty());
        for (const DrawCall& call : _renderer->calls) {
            ASSERT_TRUE(call.clip.has_value());
            EXPECT_FLOAT_EQ(call.clip->w, 200.0f);
            EXPECT_FLOAT_EQ(call.clip->h, 80.0f);
        }
        EXPECT_EQ(_renderer->GetClipDepth(), 0u);  // balanced after the draw
    }

    TEST_F(TextComponentTest, ATextThatFitsIsNotClipped) {
        Draw("Plain");

        for (const DrawCall& call : _renderer->calls) {
            EXPECT_FALSE(call.clip.has_value());
        }
    }

    TEST_F(TextComponentTest, EditingTheTextRelaysOutTheNextDraw) {
        Draw("Plain");
        const size_t before = _renderer->calls.size();

        Label("Plain")->_text = "Hello, world";
        Draw("Plain");

        EXPECT_GT(_renderer->calls.size(), before);
        EXPECT_EQ(Label("Plain")->GetLastLayout().glyphs.size(), 12u);
    }

    TEST_F(TextComponentTest, EditingTheSizeChangesTheLayout) {
        Draw("Plain");
        const float narrow = Label("Plain")->GetLastLayout().width;

        Label("Plain")->_fontSize = 48.0f;
        Draw("Plain");

        EXPECT_NEAR(Label("Plain")->GetLastLayout().width, narrow * 2.0f, narrow * 0.05f);
    }

    TEST_F(TextComponentTest, AnEmptyTextDrawsNothing) {
        Label("Plain")->SetText("");

        Draw("Plain");

        EXPECT_TRUE(_renderer->calls.empty());
    }

    TEST_F(TextComponentTest, AMissingFontDrawsNothingAndDoesNotCrash) {
        auto label = Label("Plain");
        label->_fontPath = "assets/fonts/does_not_exist.ttf"_intern;
        label->ReloadFont();

        Draw("Plain");

        EXPECT_TRUE(_renderer->calls.empty());
    }

    TEST_F(TextComponentTest, AnotherFontPathIsLoadedByReloadFont) {
        auto label = Label("Plain");
        label->_fontPath = "assets/fonts/PT_Sans-Web-Regular.ttf"_intern;

        label->ReloadFont();
        Draw("Plain");

        EXPECT_EQ(_renderer->calls.size(), kInkedGlyphsInHello);
    }

    TEST_F(TextComponentTest, AnOutlineAndAShadowAreDrawnInThreePassesInSceneUnitsTimesTheScale) {
        auto label = Label("Fit");  // a scale of 2
        label->_outlineWidth = 1.5f;
        label->_outlineColor = Color{1, 2, 3, 200};
        label->_shadowOffsetX = 2.0f;
        label->_shadowOffsetY = 1.0f;
        label->_shadowColor = Color{4, 5, 6, 100};
        Draw("Fit");

        ASSERT_EQ(_renderer->calls.size(), 3 * kInkedGlyphsInHello);
        constexpr float kOutlineOutputPixels = 3.0f;
        for (size_t i = 0; i < kInkedGlyphsInHello; ++i) {
            const DrawCall& shadow = _renderer->calls[i];
            const DrawCall& outline = _renderer->calls[kInkedGlyphsInHello + i];
            const DrawCall& fill = _renderer->calls[2 * kInkedGlyphsInHello + i];
            EXPECT_EQ(shadow.color.r, 4);
            EXPECT_EQ(outline.color.r, 1);
            EXPECT_EQ(fill.color.r, 255);
            EXPECT_FLOAT_EQ(outline.rect.w, fill.rect.w + 2.0f * kOutlineOutputPixels);
            EXPECT_FLOAT_EQ(outline.rect.x, fill.rect.x - kOutlineOutputPixels);
            EXPECT_FLOAT_EQ(shadow.rect.x, outline.rect.x + 4.0f);
            EXPECT_FLOAT_EQ(shadow.rect.y, outline.rect.y + 2.0f);
        }
        EXPECT_EQ(CoreManager::GetFontAtlasLibrary().GetAtlasCount(), 2u) << "the plain atlas and the outline atlas";
    }

    TEST_F(TextComponentTest, EffectsAreNotLayoutAndAnInvisibleOnePaintsNothing) {
        auto label = Label("Plain");
        Draw("Plain");
        const TextLayoutResult plain = label->GetLastLayout();

        label->_outlineWidth = 4.0f;                // no colour: no outline
        label->_shadowColor = Color{0, 0, 0, 255};  // no offset: a shadow right under the text is not drawn
        Draw("Plain");
        EXPECT_EQ(_renderer->calls.size(), kInkedGlyphsInHello);
        EXPECT_EQ(CoreManager::GetFontAtlasLibrary().GetAtlasCount(), 1u);

        label->_outlineColor = Color{0, 0, 0, 255};
        label->_shadowOffsetY = 3.0f;
        Draw("Plain");
        EXPECT_EQ(_renderer->calls.size(), 3 * kInkedGlyphsInHello);
        EXPECT_FLOAT_EQ(label->GetLastLayout().width, plain.width) << "the layout is the same with and without effects";
        EXPECT_FLOAT_EQ(label->GetLastLayout().height, plain.height);
    }

    TEST_F(TextComponentTest, TheTextOfAnInactiveSceneIsNotDrawn) {
        _manager->SetActiveScene("Plain"_intern);
        _renderer->ClearCalls();
        _manager->DrawAll();
        const size_t plainOnly = _renderer->calls.size();

        EXPECT_EQ(plainOnly, kInkedGlyphsInHello);
    }

}  // namespace BECore::Tests
