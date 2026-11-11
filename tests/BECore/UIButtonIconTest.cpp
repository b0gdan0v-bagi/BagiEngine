#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kEpsilon = 1e-3f;
        constexpr uint32_t kIconPixels = 32;

        // A Screen canvas in window pixels (output 800 x 600, scale 1); every button names the sprite "TestIcon", whose texture the fixture hands over:
        //   Only   (10, 10, 48 x 48)     no caption
        //   Left   (10, 100, 200 x 48)   a short caption, the icon at the left
        //   Above  (10, 200, 120 x 96)   a short caption, the icon above it
        //   Plain  (10, 320, 200 x 48)   a caption and no icon
        //   Long   (10, 400, 120 x 48)   a caption that fits the whole rect but not the room the icon leaves
        constexpr const char* kIconXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIButton" name="Only" icon="TestIcon" x="10" y="10" width="48" height="48" color="30,30,30,255"/>
          <element type="UIButton" name="Left" icon="TestIcon" text="Go" x="10" y="100" width="200" height="48" color="30,30,30,255"/>
          <element type="UIButton" name="Above" icon="TestIcon" iconPlacement="Above" text="Go" x="10" y="200" width="120" height="96" color="30,30,30,255"/>
          <element type="UIButton" name="Plain" text="Go" x="10" y="320" width="200" height="48" color="30,30,30,255"/>
          <element type="UIButton" name="Long" icon="TestIcon" text="Settings" x="10" y="400" width="120" height="48" color="30,30,30,255"/>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        bool Near(const Rect& a, const Rect& b, float tolerance = 1e-2f) {
            return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.w - b.w) <= tolerance && std::abs(a.h - b.h) <= tolerance;
        }

        bool Inside(const Rect& inner, const Rect& outer, float tolerance = kEpsilon) {
            return inner.x >= outer.x - tolerance && inner.y >= outer.y - tolerance && inner.x + inner.w <= outer.x + outer.w + tolerance && inner.y + inner.h <= outer.y + outer.h + tolerance;
        }

        bool Disjoint(const Rect& a, const Rect& b) {
            return a.x + a.w <= b.x + kEpsilon || b.x + b.w <= a.x + kEpsilon || a.y + a.h <= b.y + kEpsilon || b.y + b.h <= a.y + kEpsilon;
        }

        bool SameColor(const Color& a, const Color& b) {
            return ColorsEqual(a, b);
        }

        constexpr Color kText{200, 180, 160, 255};
        constexpr Color kTint{100, 110, 120, 200};

        IntrusivePtr<UIButton> MakeButton() {
            auto button = New<UIButton>();
            button->icon = PoolString::Intern("TestIcon");
            button->textColor = kText;
            return button;
        }

    }  // namespace

    // ---- the geometry --------------------------------------------------------------------------------------------------------

    TEST(UIButtonIconTest, WithoutAnIconTheTextTakesTheWholeArea) {
        const Rect area{10.0f, 20.0f, 100.0f, 40.0f};
        const auto parts = UIButton::GetParts(area, false, true, UIIconPlacement::Left, 0.0f);
        EXPECT_TRUE(Near(parts.text, area));
        EXPECT_TRUE(parts.icon.IsEmpty());
    }

    TEST(UIButtonIconTest, AnIconAloneIsCentredAndTakesAShareOfTheShorterSide) {
        const Rect area{10.0f, 20.0f, 100.0f, 40.0f};
        const auto parts = UIButton::GetParts(area, true, false, UIIconPlacement::Left, 0.0f);
        const float side = 40.0f * UIButton::kIconOnlyShare;
        EXPECT_TRUE(Near(parts.icon, {10.0f + (100.0f - side) * 0.5f, 20.0f + (40.0f - side) * 0.5f, side, side}));
        EXPECT_TRUE(parts.text.IsEmpty());

        const auto sized = UIButton::GetParts(area, true, false, UIIconPlacement::Above, 16.0f);
        EXPECT_TRUE(Near(sized.icon, {10.0f + 42.0f, 20.0f + 12.0f, 16.0f, 16.0f})) << "iconSize wins, and the placement means nothing without a caption";
    }

    TEST(UIButtonIconTest, AnIconAtTheLeftLeavesTheRestOfTheWidthToTheText) {
        const Rect area{10.0f, 20.0f, 200.0f, 48.0f};
        const auto parts = UIButton::GetParts(area, true, true, UIIconPlacement::Left, 0.0f);
        const float side = 48.0f * UIButton::kIconWithTextShare;
        EXPECT_TRUE(Near(parts.icon, {10.0f + UIButton::kIconPadding, 20.0f + (48.0f - side) * 0.5f, side, side}));
        EXPECT_NEAR(parts.text.x, parts.icon.x + side + UIButton::kIconGap, kEpsilon);
        EXPECT_NEAR(parts.text.x + parts.text.w, 210.0f, kEpsilon);
        EXPECT_NEAR(parts.text.y, 20.0f, kEpsilon);
        EXPECT_NEAR(parts.text.h, 48.0f, kEpsilon);
        EXPECT_TRUE(Disjoint(parts.icon, parts.text));
    }

    TEST(UIButtonIconTest, AnIconAboveLeavesTheRestOfTheHeightToTheText) {
        const Rect area{10.0f, 20.0f, 120.0f, 96.0f};
        const auto parts = UIButton::GetParts(area, true, true, UIIconPlacement::Above, 0.0f);
        const float side = 96.0f * UIButton::kIconWithTextShare;
        EXPECT_TRUE(Near(parts.icon, {10.0f + (120.0f - side) * 0.5f, 20.0f + UIButton::kIconPadding, side, side}));
        EXPECT_NEAR(parts.text.y, parts.icon.y + side + UIButton::kIconGap, kEpsilon);
        EXPECT_NEAR(parts.text.y + parts.text.h, 116.0f, kEpsilon);
        EXPECT_NEAR(parts.text.x, 10.0f, kEpsilon);
        EXPECT_NEAR(parts.text.w, 120.0f, kEpsilon);
        EXPECT_TRUE(Disjoint(parts.icon, parts.text));
    }

    TEST(UIButtonIconTest, AnExplicitIconSizeIsKeptWhereItFitsAndClippedWhereItDoesNot) {
        const Rect area{0.0f, 0.0f, 200.0f, 48.0f};
        EXPECT_NEAR(UIButton::GetParts(area, true, true, UIIconPlacement::Left, 20.0f).icon.w, 20.0f, kEpsilon);
        EXPECT_NEAR(UIButton::GetParts(area, true, true, UIIconPlacement::Left, 500.0f).icon.h, 48.0f, kEpsilon) << "no taller than the rect";
        EXPECT_NEAR(UIButton::GetParts(area, true, false, UIIconPlacement::Left, 500.0f).icon.w, 48.0f, kEpsilon);
    }

    TEST(UIButtonIconTest, APartNeverLeavesAnAreaThatIsTooSmall) {
        for (const UIIconPlacement placement : {UIIconPlacement::Left, UIIconPlacement::Above}) {
            for (const bool hasText : {false, true}) {
                for (const float iconSize : {0.0f, 12.0f, 300.0f}) {
                    for (const float w : {0.0f, 1.0f, 5.0f, 9.0f, 30.0f}) {
                        for (const float h : {0.0f, 1.0f, 5.0f, 9.0f, 30.0f}) {
                            const Rect area{5.0f, 7.0f, w, h};
                            const auto parts = UIButton::GetParts(area, true, hasText, placement, iconSize);
                            const auto note = [&] {
                                return ::testing::Message() << "placement " << static_cast<int>(placement) << " text " << hasText << " size " << iconSize << " area " << w << "x" << h;
                            };
                            EXPECT_GE(parts.icon.w, 0.0f) << note();
                            EXPECT_GE(parts.icon.h, 0.0f) << note();
                            EXPECT_GE(parts.text.w, 0.0f) << note();
                            EXPECT_GE(parts.text.h, 0.0f) << note();
                            if (!parts.icon.IsEmpty()) {
                                EXPECT_TRUE(Inside(parts.icon, area)) << note();
                            }
                            if (!parts.text.IsEmpty()) {
                                EXPECT_TRUE(Inside(parts.text, area)) << note();
                            }
                            if (!parts.icon.IsEmpty() && !parts.text.IsEmpty()) {
                                EXPECT_TRUE(Disjoint(parts.icon, parts.text)) << note();
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- the colour of the icon -----------------------------------------------------------------------------------------------

    TEST(UIButtonIconTest, TheIconTakesTheTextColourUnlessItHasATint) {
        auto button = MakeButton();
        EXPECT_TRUE(SameColor(button->GetCurrentIconColor(), kText));
        button->iconTint = kTint;
        EXPECT_TRUE(SameColor(button->GetCurrentIconColor(), kTint));
    }

    TEST(UIButtonIconTest, HoverBrightensAndAHeldPressDarkensTheIcon) {
        auto button = MakeButton();
        button->iconTint = kTint;

        button->hovered = true;
        EXPECT_TRUE(SameColor(button->GetCurrentIconColor(), ShiftColor(kTint, kHoverBrighten)));

        button->pressed = true;
        EXPECT_TRUE(SameColor(button->GetCurrentIconColor(), ShiftColor(kTint, -kPressDarken)));

        button->hovered = false;  // the press moved off the button: not shown as pressed
        EXPECT_TRUE(SameColor(button->GetCurrentIconColor(), kTint));
    }

    TEST(UIButtonIconTest, ADisabledButtonKeepsHalfTheAlphaOfItsIcon) {
        auto button = MakeButton();
        button->iconTint = kTint;
        button->disabled = true;
        EXPECT_TRUE(SameColor(button->GetCurrentIconColor(), WithAlpha(kTint, kDisabledAlpha)));

        button->hovered = true;
        EXPECT_TRUE(SameColor(button->GetCurrentIconColor(), WithAlpha(kTint, kDisabledAlpha))) << "no hover shift on a disabled button";

        button->disabled = false;
        button->groupDisabled = true;
        EXPECT_TRUE(SameColor(button->GetCurrentIconColor(), WithAlpha(kTint, kDisabledAlpha)));
    }

    TEST(UIButtonIconTest, SelectedLeavesTheIconColourAlone) {
        auto button = MakeButton();
        button->selected = true;
        EXPECT_TRUE(SameColor(button->GetCurrentIconColor(), kText));
    }

    // ---- the draw and the check, in a scene -------------------------------------------------------------------------------------

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
            for (const char* name : {"Only", "Left", "Above", "Long"}) {
                Button(name).GetIconSprite().SetTexture(_icon);
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

        UIButton& Button(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            UIButton* typed = element ? element->Cast<UIButton>() : nullptr;
            EXPECT_NE(typed, nullptr) << name << " is not a button";
            return *typed;
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

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
        IntrusivePtr<ITexture> _icon;
    };

    TEST_F(UIButtonIconDrawTest, AnIconOnlyButtonDrawsTheSpriteAloneInTheMiddleOfItsRect) {
        Draw();
        const auto& only = Button("Only");
        const auto calls = IconCalls();
        ASSERT_EQ(calls.size(), 4u) << "one icon per icon button, the plain one has none";

        const Rect expected = UIButton::GetParts(only.rect, true, false, UIIconPlacement::Left, 0.0f).icon;
        bool found = false;
        for (const DrawCall& call : calls) {
            if (Near(call.rect, expected)) {
                found = true;
                EXPECT_TRUE(SameColor(call.color, only.GetCurrentIconColor()));
                ASSERT_TRUE(call.source.has_value());
                EXPECT_TRUE(Near(*call.source, {0.0f, 0.0f, static_cast<float>(kIconPixels), static_cast<float>(kIconPixels)}));
            }
        }
        EXPECT_TRUE(found) << "the icon of the Only button is where GetParts puts it";
        EXPECT_FALSE(GlyphBounds(only.rect)) << "no caption, no glyph";
    }

    TEST_F(UIButtonIconDrawTest, TheIconAndTheCaptionOfALeftButtonShareTheRect) {
        Draw();
        const auto& left = Button("Left");
        const auto parts = UIButton::GetParts(left.rect, true, true, UIIconPlacement::Left, 0.0f);

        bool iconDrawn = false;
        for (const DrawCall& call : IconCalls()) {
            iconDrawn = iconDrawn || Near(call.rect, parts.icon);
        }
        EXPECT_TRUE(iconDrawn);

        const auto glyphs = GlyphBounds(left.rect);
        ASSERT_TRUE(glyphs.has_value()) << "the caption is drawn";
        EXPECT_TRUE(Inside(*glyphs, parts.text, 1.0f)) << "in the area the icon leaves";
        EXPECT_GE(glyphs->x, parts.text.x - 1.0f);
    }

    TEST_F(UIButtonIconDrawTest, TheCaptionOfAnAboveButtonIsUnderTheIcon) {
        Draw();
        const auto& above = Button("Above");
        const auto parts = UIButton::GetParts(above.rect, true, true, UIIconPlacement::Above, 0.0f);

        bool iconDrawn = false;
        for (const DrawCall& call : IconCalls()) {
            iconDrawn = iconDrawn || Near(call.rect, parts.icon);
        }
        EXPECT_TRUE(iconDrawn);

        const auto glyphs = GlyphBounds(above.rect);
        ASSERT_TRUE(glyphs.has_value());
        EXPECT_GE(glyphs->y, parts.icon.y + parts.icon.h - 1.0f) << "below the icon";
    }

    TEST_F(UIButtonIconDrawTest, TheIconIsDrawnInTheColourOfTheStateOfTheButton) {
        auto& left = Button("Left");
        left.iconTint = kTint;
        left.hovered = true;
        Draw();
        const Rect iconRect = UIButton::GetParts(left.rect, true, true, UIIconPlacement::Left, 0.0f).icon;
        for (const DrawCall& call : IconCalls()) {
            if (Near(call.rect, iconRect)) {
                EXPECT_TRUE(SameColor(call.color, ShiftColor(kTint, kHoverBrighten)));
                return;
            }
        }
        FAIL() << "the icon was not drawn";
    }

    TEST_F(UIButtonIconDrawTest, ASpriteThatIsNotInTheLibraryDrawsNoIconButStillTheCaption) {
        auto& left = Button("Left");
        left.GetIconSprite() = UISprite{};  // no direct texture any more: the name "TestIcon" is not in the TextureLibrary
        Draw();
        EXPECT_EQ(IconCalls().size(), 3u);
        EXPECT_TRUE(GlyphBounds(left.rect).has_value());
    }

    TEST_F(UIButtonIconDrawTest, AButtonWithoutAnIconDrawsAsItDidBefore) {
        Draw();
        const auto& plain = Button("Plain");
        const auto glyphs = GlyphBounds(plain.rect);
        ASSERT_TRUE(glyphs.has_value());

        // The caption is centred in the whole rect, as a button with no icon always did
        const float middle = glyphs->x + glyphs->w * 0.5f;
        EXPECT_NEAR(middle, plain.rect.x + plain.rect.w * 0.5f, 3.0f);

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
        auto& longOne = Button("Long");
        const UIContentFit withIcon = longOne.CheckContent(longOne.rect.w, longOne.rect.h, 1.0f);
        EXPECT_TRUE(withIcon.hasContent);
        EXPECT_FALSE(withIcon.fits) << "the icon takes room the caption needs";

        longOne.icon = PoolString{};
        const UIContentFit without = longOne.CheckContent(longOne.rect.w, longOne.rect.h, 1.0f);
        EXPECT_TRUE(without.hasContent);
        EXPECT_TRUE(without.fits) << "the same caption fits the whole rect";
    }

    TEST_F(UIButtonIconDrawTest, ACaptionNextToAnIconThatFitsIsNotReported) {
        Draw();
        const auto& left = Button("Left");
        const UIContentFit fit = left.CheckContent(left.rect.w, left.rect.h, 1.0f);
        EXPECT_TRUE(fit.hasContent);
        EXPECT_TRUE(fit.fits);
    }

    TEST_F(UIButtonIconDrawTest, AnIconOnlyButtonHasNoTextToCheck) {
        Draw();
        const auto& only = Button("Only");
        EXPECT_FALSE(only.CheckContent(only.rect.w, only.rect.h, 1.0f).hasContent);
    }

}  // namespace BECore::Tests
