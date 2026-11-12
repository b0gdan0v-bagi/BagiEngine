#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UITextComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The draw and the content check of an icon button in its composed form (`ui-controls.md`, "Components": an image child, and a caption child in a horizontal stack when there is text).
     * The class `UIButton` had these cases with `GetParts`; the geometry is now the layout of the children, and the numbers (the shares and the gaps that the HTML compiler writes, which
     * `UIWebTest` holds it to) are given below as numbers. What the class tests held on the colour of an icon by state (hover, press, disabled, group-disabled, selected) is
     * `UIStateLookTest` (`ResolveTint`), `UIInputClickerTest` and `UIStateInheritTest` (a child follows its button). An icon above the caption has no composed form, so its cases are gone.
     */

    namespace {

        constexpr float kEpsilon = 1e-3f;
        constexpr float kTolerance = 1e-2f;
        constexpr uint32_t kIconPixels = 32;
        constexpr float kIconOnlyShare = UIImageComponent::kIconOnlyShare;          // width and height of the icon of a button with no caption, of the shorter side
        constexpr float kIconWithTextShare = UIImageComponent::kIconWithTextShare;  // height of the icon of a button with a caption
        constexpr float kIconPadding = UIImageComponent::kIconPadding;              // left of the icon of a button with a caption
        constexpr float kIconGap = UIImageComponent::kIconGap;                      // between the icon and the caption

        // A Screen canvas in window pixels (output 800 x 600, scale 1); every icon is the sprite "TestIcon", whose texture the fixture hands over:
        //   Only      (10, 10, 48 x 48)     no caption: the icon alone, in the middle
        //   Left      (10, 100, 200 x 48)   a short caption, the icon at the left
        //   Plain     (10, 320, 200 x 48)   a caption and no icon
        //   Long      (10, 400, 120 x 48)   a caption that fits the whole rect but not the room the icon leaves
        //   LongPlain (10, 480, 120 x 48)   the same caption with no icon
        constexpr const char* kIconXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Only" x="10" y="10" width="48" height="48" color="30,30,30,255">
            <components><component type="UIInputClicker" action="IconOnly"/></components>
            <children>
              <element type="UIElement" name="OnlyIcon" anchor="Center" width="60%" height="60%" aspectRatio="1">
                <components><component type="UIImageComponent" sprite="TestIcon"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Left" x="10" y="100" width="200" height="48" color="30,30,30,255" layout="Stack" direction="Horizontal" padding="8,0,0,0" spacing="6" crossAlign="Center">
            <components><component type="UIInputClicker" action="IconLeft"/></components>
            <children>
              <element type="UIElement" name="LeftIcon" height="50%" aspectRatio="1">
                <components><component type="UIImageComponent" sprite="TestIcon"/></components>
              </element>
              <element type="UIElement" name="LeftCaption" flex="1" height="100%" fontSize="24">
                <components><component type="UITextComponent" text="Go" align="Center" verticalAlign="Center" wrap="false"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Plain" x="10" y="320" width="200" height="48" color="30,30,30,255" fontSize="24">
            <components>
              <component type="UITextComponent" text="Go" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="IconPlain"/>
            </components>
          </element>
          <element type="UIElement" name="Long" x="10" y="400" width="120" height="48" color="30,30,30,255" layout="Stack" direction="Horizontal" padding="8,0,0,0" spacing="6" crossAlign="Center">
            <components><component type="UIInputClicker" action="IconLong"/></components>
            <children>
              <element type="UIElement" name="LongIcon" height="50%" aspectRatio="1">
                <components><component type="UIImageComponent" sprite="TestIcon"/></components>
              </element>
              <element type="UIElement" name="LongCaption" flex="1" height="100%" fontSize="24">
                <components><component type="UITextComponent" text="Settings" align="Center" verticalAlign="Center" wrap="false"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="LongPlain" x="10" y="480" width="120" height="48" color="30,30,30,255" fontSize="24">
            <components>
              <component type="UITextComponent" text="Settings" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="IconLongPlain"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        bool Near(const Rect& a, const Rect& b, float tolerance = kTolerance) {
            return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.w - b.w) <= tolerance && std::abs(a.h - b.h) <= tolerance;
        }

        bool Inside(const Rect& inner, const Rect& outer, float tolerance = kEpsilon) {
            return inner.x >= outer.x - tolerance && inner.y >= outer.y - tolerance && inner.x + inner.w <= outer.x + outer.w + tolerance && inner.y + inner.h <= outer.y + outer.h + tolerance;
        }

    }  // namespace

    class UIButtonIconDrawTest : public ::testing::Test {
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
            ASSERT_TRUE(deserializer.LoadFromString(kIconXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _canvas = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);

            const eastl::vector<uint8_t> pixels(static_cast<size_t>(kIconPixels) * kIconPixels * 4, 200);
            _icon = _renderer->CreateTexture(pixels.data(), kIconPixels, kIconPixels);
            for (const char* name : {"OnlyIcon", "LeftIcon", "LongIcon"}) {
                Get(name).GetComponent<UIImageComponent>()->GetSprite().SetTexture(_icon);
            }
        }

        void TearDown() override {
            _icon.Reset();
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

        /// The draws of the icon texture (there is one icon texture for all the buttons)
        eastl::vector<DrawCall> IconCalls() const {
            eastl::vector<DrawCall> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.texture == _icon.Get()) {
                    found.push_back(call);
                }
            }
            return found;
        }

        /// The union of the glyph draws (every other texture draw) that start inside @p within
        std::optional<Rect> GlyphBounds(const Rect& within) const {
            std::optional<Rect> bounds;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind != DrawKind::Texture || call.texture == _icon.Get() || !Inside({call.rect.x, call.rect.y, 0.0f, 0.0f}, within)) {
                    continue;
                }
                if (!bounds) {
                    bounds = call.rect;
                    continue;
                }
                const float right = std::max(bounds->x + bounds->w, call.rect.x + call.rect.w);
                const float bottom = std::max(bounds->y + bounds->h, call.rect.y + call.rect.h);
                bounds->x = std::min(bounds->x, call.rect.x);
                bounds->y = std::min(bounds->y, call.rect.y);
                bounds->w = right - bounds->x;
                bounds->h = bottom - bounds->y;
            }
            return bounds;
        }

        bool IconDrawnAt(const Rect& expected) const {
            for (const DrawCall& call : IconCalls()) {
                if (Near(call.rect, expected)) {
                    return true;
                }
            }
            return false;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
        IntrusivePtr<ITexture> _icon;
    };

    TEST_F(UIButtonIconDrawTest, AnIconOnlyButtonDrawsTheSpriteAloneInTheMiddleOfItsRect) {
        Draw();
        const UIElement& only = Get("Only");
        ASSERT_EQ(IconCalls().size(), 3u) << "one icon per icon button, the plain one has none";

        const float side = only.rect.h * kIconOnlyShare;
        const Rect expected{only.rect.x + (only.rect.w - side) * 0.5f, only.rect.y + (only.rect.h - side) * 0.5f, side, side};
        bool found = false;
        for (const DrawCall& call : IconCalls()) {
            if (Near(call.rect, expected)) {
                found = true;
                ASSERT_TRUE(call.source.has_value());
                EXPECT_TRUE(Near(*call.source, {0.0f, 0.0f, static_cast<float>(kIconPixels), static_cast<float>(kIconPixels)}));
            }
        }
        EXPECT_TRUE(found) << "the icon of the Only button is the square share of its rect, in the middle";
        EXPECT_FALSE(GlyphBounds(only.rect)) << "no caption, no glyph";
    }

    TEST_F(UIButtonIconDrawTest, TheIconAndTheCaptionOfALeftButtonShareTheRect) {
        Draw();
        const UIElement& left = Get("Left");
        const float side = left.rect.h * kIconWithTextShare;
        const Rect icon{left.rect.x + kIconPadding, left.rect.y + (left.rect.h - side) * 0.5f, side, side};
        EXPECT_TRUE(IconDrawnAt(icon));

        const Rect captionArea{icon.x + side + kIconGap, left.rect.y, left.rect.x + left.rect.w - (icon.x + side + kIconGap), left.rect.h};
        EXPECT_TRUE(Near(Get("LeftCaption").rect, captionArea));
        const auto glyphs = GlyphBounds(left.rect);
        ASSERT_TRUE(glyphs.has_value()) << "the caption is drawn";
        EXPECT_TRUE(Inside(*glyphs, captionArea, 1.0f)) << "in the area the icon leaves";
    }

    TEST_F(UIButtonIconDrawTest, ASpriteThatIsNotInTheLibraryDrawsNoIconButStillTheCaption) {
        Get("LeftIcon").GetComponent<UIImageComponent>()->GetSprite() = UISprite{};  // no direct texture any more: the name "TestIcon" is not in the TextureLibrary
        Draw();
        EXPECT_EQ(IconCalls().size(), 2u);
        EXPECT_TRUE(GlyphBounds(Get("Left").rect).has_value());
    }

    TEST_F(UIButtonIconDrawTest, AButtonWithoutAnIconCentresItsCaptionInTheWholeRect) {
        Draw();
        const UIElement& plain = Get("Plain");
        const auto glyphs = GlyphBounds(plain.rect);
        ASSERT_TRUE(glyphs.has_value());
        EXPECT_NEAR(glyphs->x + glyphs->w * 0.5f, plain.rect.x + plain.rect.w * 0.5f, 3.0f);

        size_t rects = 0;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Rect && Near(call.rect, plain.rect)) {
                ++rects;
            }
        }
        EXPECT_EQ(rects, 1u) << "the plate and nothing else";
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Texture && Inside({call.rect.x, call.rect.y, 0.0f, 0.0f}, plain.rect)) {
                EXPECT_NE(call.texture, _icon.Get());
            }
        }
    }

    // ---- the content check ---------------------------------------------------------------------------------------------------

    TEST_F(UIButtonIconDrawTest, ACaptionThatFitsTheRectButNotTheRoomTheIconLeavesIsCaught) {
        Draw();
        const UIElement& caption = Get("LongCaption");
        const UIContentFit withIcon = caption.CheckContent(caption.rect.w, caption.rect.h, 1.0f);
        EXPECT_TRUE(withIcon.hasContent);
        EXPECT_FALSE(withIcon.fits) << "the icon takes room the caption needs";

        const UIElement& whole = Get("LongPlain");
        const UIContentFit without = whole.CheckContent(whole.rect.w, whole.rect.h, 1.0f);
        EXPECT_TRUE(without.hasContent);
        EXPECT_TRUE(without.fits) << "the same caption fits the whole rect";
    }

    TEST_F(UIButtonIconDrawTest, ACaptionNextToAnIconThatFitsIsNotReported) {
        Draw();
        const UIElement& caption = Get("LeftCaption");
        const UIContentFit fit = caption.CheckContent(caption.rect.w, caption.rect.h, 1.0f);
        EXPECT_TRUE(fit.hasContent);
        EXPECT_TRUE(fit.fits);
    }

    TEST_F(UIButtonIconDrawTest, AnIconOnlyButtonHasNoTextToCheck) {
        Draw();
        const UIElement& only = Get("Only");
        EXPECT_FALSE(only.CheckContent(only.rect.w, only.rect.h, 1.0f).hasContent);
        const UIElement& icon = Get("OnlyIcon");
        EXPECT_FALSE(icon.CheckContent(icon.rect.w, icon.rect.h, 1.0f).hasContent);
    }

}  // namespace BECore::Tests
