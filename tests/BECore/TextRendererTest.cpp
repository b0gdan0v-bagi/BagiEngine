#include <BECore/FontAtlasTestSupport.h>
#include <BECore/Text/FontAtlas.h>
#include <BECore/Text/TextLayout.h>
#include <BECore/Text/TextRenderer.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    // -- TextRenderer --------------------------------------------------------------------------------------------------

    class TextRendererTest : public FontAtlasLibraryTest {
    protected:
        TextLayoutResult Layout(StringView text, int size = 24, float maxWidth = 0.0f) const {
            TextLayoutOptions options;
            options.fontSize = static_cast<float>(size);
            options.maxWidth = maxWidth;
            return TextLayout::Layout(*font, text, options);
        }

        IntrusivePtr<FontAtlas> Atlas(int size = 24) {
            return Library().Get(*renderer, font, size);
        }
    };

    TEST_F(TextRendererTest, DrawsOneQuadForEveryGlyphThatHasInk) {
        const TextLayoutResult layout = Layout("Hi, мир");
        auto atlas = Atlas();

        const size_t drawn = TextRenderer::Draw(*renderer, *atlas, layout, 0.0f, 0.0f, Color{255, 255, 255, 255});

        EXPECT_EQ(drawn, 6u);  // seven glyphs, one of them the space
        EXPECT_EQ(renderer->calls.size(), 6u);
        for (const DrawCall& call : renderer->calls) {
            EXPECT_EQ(call.kind, DrawKind::Texture);
        }
    }

    TEST_F(TextRendererTest, ADrawOfGlyphsTheAtlasHasAllocatesNothing) {
        const TextLayoutResult layout = Layout("Hi, мир");
        auto atlas = Atlas();
        TextRenderer::Draw(*renderer, *atlas, layout, 0.0f, 0.0f, Color{255, 255, 255, 255});  // the atlas has every glyph from here on
        renderer->calls.clear();
        renderer->calls.reserve(layout.glyphs.size());  // the recording itself must not count

        size_t drawn = 0;
        ExpectNoAllocations([&] { drawn = TextRenderer::Draw(*renderer, *atlas, layout, 0.0f, 0.0f, Color{255, 255, 255, 255}); });
        EXPECT_EQ(drawn, 6u);
    }

    TEST_F(TextRendererTest, AGlyphTheAtlasLacksIsPreparedByTheDraw) {
        const TextLayoutResult layout = Layout("Hi", 24);
        auto outline = Library().Get(*renderer, font, 24, 2);  // an outline atlas is not preloaded
        ASSERT_EQ(outline->Find(layout.glyphs[0].glyph), nullptr);

        TextEffects effects;
        effects.outlineWidth = 2.0f;
        effects.outlineColor = Color{0, 0, 0, 255};
        TextRenderer::Draw(*renderer, *Atlas(), outline.Get(), layout, 0.0f, 0.0f, Color{255, 255, 255, 255}, effects);

        EXPECT_NE(outline->Find(layout.glyphs[0].glyph), nullptr);
        EXPECT_NE(outline->Find(layout.glyphs[1].glyph), nullptr);
    }

    TEST_F(TextRendererTest, TheTintIsTheTextColor) {
        const TextLayoutResult layout = Layout("A");
        auto atlas = Atlas();

        TextRenderer::Draw(*renderer, *atlas, layout, 0.0f, 0.0f, Color{10, 20, 30, 40});

        ASSERT_EQ(renderer->calls.size(), 1u);
        EXPECT_EQ(At(renderer->calls, 0).color.r, 10);
        EXPECT_EQ(At(renderer->calls, 0).color.a, 40);
    }

    TEST_F(TextRendererTest, AGlyphIsDrawnFromItsAtlasRectAtItsOwnSize) {
        const TextLayoutResult layout = Layout("A");
        auto atlas = Atlas();

        TextRenderer::Draw(*renderer, *atlas, layout, 0.0f, 0.0f, Color{255, 255, 255, 255});

        ASSERT_EQ(renderer->calls.size(), 1u);
        const AtlasGlyph& glyph = *atlas->Find(layout.glyphs[0].glyph);
        const DrawCall& call = At(renderer->calls, 0);
        ASSERT_TRUE(call.source.has_value());
        EXPECT_FLOAT_EQ(call.source->x, glyph.source.x);
        EXPECT_FLOAT_EQ(call.source->w, glyph.source.w);
        EXPECT_FLOAT_EQ(call.rect.w, glyph.source.w);  // 1:1, no stretching
        EXPECT_FLOAT_EQ(call.rect.h, glyph.source.h);
        EXPECT_EQ(call.texture, atlas->GetPage(glyph.page));
    }

    TEST_F(TextRendererTest, GlyphsAreSnappedToWholeOutputPixels) {
        const TextLayoutResult layout = Layout("Hello");
        auto atlas = Atlas();

        TextRenderer::Draw(*renderer, *atlas, layout, 10.3f, 20.7f, Color{255, 255, 255, 255});

        ASSERT_FALSE(renderer->calls.empty());
        for (const DrawCall& call : renderer->calls) {
            EXPECT_FLOAT_EQ(call.rect.x, std::round(call.rect.x));
            EXPECT_FLOAT_EQ(call.rect.y, std::round(call.rect.y));
        }
    }

    TEST_F(TextRendererTest, TheOriginMovesTheText) {
        const TextLayoutResult layout = Layout("A");
        auto atlas = Atlas();

        TextRenderer::Draw(*renderer, *atlas, layout, 0.0f, 0.0f, Color{255, 255, 255, 255});
        TextRenderer::Draw(*renderer, *atlas, layout, 100.0f, 50.0f, Color{255, 255, 255, 255});

        ASSERT_EQ(renderer->calls.size(), 2u);
        EXPECT_FLOAT_EQ(At(renderer->calls, 1).rect.x - At(renderer->calls, 0).rect.x, 100.0f);
        EXPECT_FLOAT_EQ(At(renderer->calls, 1).rect.y - At(renderer->calls, 0).rect.y, 50.0f);
    }

    TEST_F(TextRendererTest, TheViewTransformMapsTheOriginButNotTheGlyphSize) {
        // The layout is in output pixels, so under a scale of 2 the quads are asked for in half units and come out 1:1
        renderer->SetViewTransform({2.0f, 40.0f, 0.0f});
        const TextLayoutResult layout = Layout("A");
        auto atlas = Atlas();

        TextRenderer::Draw(*renderer, *atlas, layout, 10.0f, 0.0f, Color{255, 255, 255, 255});

        ASSERT_EQ(renderer->calls.size(), 1u);
        const AtlasGlyph& glyph = *atlas->Find(layout.glyphs[0].glyph);
        EXPECT_FLOAT_EQ(At(renderer->calls, 0).rect.w, glyph.source.w);
        // The origin (10, 0) is at output x = 10 * 2 + 40 = 60
        EXPECT_FLOAT_EQ(At(renderer->calls, 0).rect.x, std::round(60.0f + layout.glyphs[0].x + glyph.offsetX));
    }

    TEST_F(TextRendererTest, MissingGlyphsAreDrawnIntoNewPagesBeforeTheDraw) {
        const TextLayoutResult layout = Layout("a\xE4\xB8\xAD"
                                               "b");  // U+4E2D is not in the font: the fallback glyph is drawn
        auto atlas = Atlas();

        const size_t drawn = TextRenderer::Draw(*renderer, *atlas, layout, 0.0f, 0.0f, Color{255, 255, 255, 255});

        EXPECT_EQ(drawn, 3u);
    }

    TEST_F(TextRendererTest, AnEmptyLayoutDrawsNothing) {
        auto atlas = Atlas();

        EXPECT_EQ(TextRenderer::Draw(*renderer, *atlas, TextLayoutResult{}, 0.0f, 0.0f, Color{255, 255, 255, 255}), 0u);
        EXPECT_TRUE(renderer->calls.empty());
    }

    // -- TextRenderer with effects -------------------------------------------------------------------------------------

    namespace {

        constexpr int kOutlineRadius = 2;
        const Color kFill{10, 20, 30, 255};
        const Color kOutlineColor{200, 0, 0, 255};
        const Color kShadowColor{0, 0, 0, 160};

        TextEffects BothEffects() {
            TextEffects effects;
            effects.outlineColor = kOutlineColor;
            effects.outlineWidth = static_cast<float>(kOutlineRadius);
            effects.shadowColor = kShadowColor;
            effects.shadowOffsetX = 3.0f;
            effects.shadowOffsetY = 4.0f;
            return effects;
        }

    }  // namespace

    TEST(TextEffectsTest, AnEffectNeedsSomethingToShow) {
        TextEffects effects;
        EXPECT_FALSE(effects.HasOutline());
        EXPECT_FALSE(effects.HasShadow());

        effects.outlineWidth = 2.0f;  // a width with a transparent colour
        effects.shadowOffsetX = 2.0f;
        EXPECT_FALSE(effects.HasOutline());
        EXPECT_FALSE(effects.HasShadow());

        effects.outlineColor = kOutlineColor;
        effects.shadowColor = kShadowColor;
        EXPECT_TRUE(effects.HasOutline());
        EXPECT_TRUE(effects.HasShadow());

        effects.outlineWidth = 0.0f;   // a colour with no width
        effects.shadowOffsetX = 0.0f;  // a shadow right under the text
        EXPECT_FALSE(effects.HasOutline());
        EXPECT_FALSE(effects.HasShadow());
        effects.shadowOffsetY = -1.0f;
        EXPECT_TRUE(effects.HasShadow());
    }

    TEST_F(TextRendererTest, ShadowOutlineAndFillAreThreePassesOverTheWholeText) {
        const TextLayoutResult layout = Layout("Hi");
        auto fill = Atlas();
        auto outline = Library().Get(*renderer, font, 24, kOutlineRadius);

        const size_t drawn = TextRenderer::Draw(*renderer, *fill, outline.Get(), layout, 0.0f, 0.0f, kFill, BothEffects());

        EXPECT_EQ(drawn, 6u);
        ASSERT_EQ(renderer->calls.size(), 6u);
        const Color expected[] = {kShadowColor, kShadowColor, kOutlineColor, kOutlineColor, kFill, kFill};
        for (size_t i = 0; i < 6; ++i) {
            EXPECT_EQ(At(renderer->calls, i).color.r, expected[i].r) << i;
            EXPECT_EQ(At(renderer->calls, i).color.a, expected[i].a) << i;
            EXPECT_EQ(At(renderer->calls, i).texture, i < 4 ? outline->GetPage(0) : fill->GetPage(0)) << i;
        }
    }

    TEST_F(TextRendererTest, TheOutlinePictureIsLargerAroundTheFillAndTheShadowIsTheOutlineMoved) {
        const TextLayoutResult layout = Layout("Hi");
        auto fill = Atlas();
        auto outline = Library().Get(*renderer, font, 24, kOutlineRadius);

        TextRenderer::Draw(*renderer, *fill, outline.Get(), layout, 10.0f, 20.0f, kFill, BothEffects());

        for (size_t glyph = 0; glyph < 2; ++glyph) {
            const DrawCall& shadow = At(renderer->calls, glyph);
            const DrawCall& ring = renderer->calls[2 + glyph];
            const DrawCall& body = renderer->calls[4 + glyph];
            EXPECT_FLOAT_EQ(ring.rect.x, body.rect.x - kOutlineRadius);
            EXPECT_FLOAT_EQ(ring.rect.y, body.rect.y - kOutlineRadius);
            EXPECT_FLOAT_EQ(ring.rect.w, body.rect.w + 2 * kOutlineRadius);
            EXPECT_FLOAT_EQ(ring.rect.h, body.rect.h + 2 * kOutlineRadius);
            EXPECT_FLOAT_EQ(shadow.rect.x, ring.rect.x + 3.0f);
            EXPECT_FLOAT_EQ(shadow.rect.y, ring.rect.y + 4.0f);
            EXPECT_FLOAT_EQ(shadow.rect.w, ring.rect.w);
        }
    }

    TEST_F(TextRendererTest, AShadowWithoutAnOutlineIsDrawnFromTheFillPictures) {
        const TextLayoutResult layout = Layout("Hi");
        auto fill = Atlas();
        TextEffects effects = BothEffects();
        effects.outlineWidth = 0.0f;

        const size_t drawn = TextRenderer::Draw(*renderer, *fill, nullptr, layout, 0.0f, 0.0f, kFill, effects);

        EXPECT_EQ(drawn, 4u);
        ASSERT_EQ(renderer->calls.size(), 4u);
        EXPECT_EQ(At(renderer->calls, 0).color.a, kShadowColor.a);
        EXPECT_EQ(At(renderer->calls, 2).color.a, kFill.a);
        EXPECT_FLOAT_EQ(At(renderer->calls, 0).rect.x, At(renderer->calls, 2).rect.x + 3.0f);
        EXPECT_FLOAT_EQ(At(renderer->calls, 0).rect.w, At(renderer->calls, 2).rect.w);
        EXPECT_EQ(Library().GetAtlasCount(), 1u);  // a text with no outline makes one atlas
    }

    TEST_F(TextRendererTest, AnOutlineWithoutAnOutlineAtlasIsNotDrawn) {
        const TextLayoutResult layout = Layout("Hi");
        auto fill = Atlas();
        TextEffects effects = BothEffects();
        effects.shadowColor = Color{};

        EXPECT_EQ(TextRenderer::Draw(*renderer, *fill, nullptr, layout, 0.0f, 0.0f, kFill, effects), 2u);
    }

    TEST_F(TextRendererTest, NoEffectIsExactlyThePlainDraw) {
        const TextLayoutResult layout = Layout("Hi");
        auto fill = Atlas();
        TextRenderer::Draw(*renderer, *fill, layout, 5.0f, 6.0f, kFill);
        const Vector<DrawCall> plain = renderer->calls;
        renderer->calls.clear();
        auto outline = Library().Get(*renderer, font, 24, kOutlineRadius);

        TextRenderer::Draw(*renderer, *fill, outline.Get(), layout, 5.0f, 6.0f, kFill, TextEffects{});

        ASSERT_EQ(renderer->calls.size(), plain.size());
        for (size_t i = 0; i < plain.size(); ++i) {
            EXPECT_FLOAT_EQ(At(renderer->calls, i).rect.x, plain[i].rect.x);
            EXPECT_FLOAT_EQ(At(renderer->calls, i).rect.y, plain[i].rect.y);
            EXPECT_EQ(At(renderer->calls, i).color.r, plain[i].color.r);
        }
        EXPECT_EQ(outline->GetGlyphCount(), 0u);  // not even prepared
    }

    TEST_F(TextRendererTest, ASpaceDrawsNothingInAnyPass) {
        const TextLayoutResult layout = Layout(" ");
        auto fill = Atlas();
        auto outline = Library().Get(*renderer, font, 24, kOutlineRadius);

        EXPECT_EQ(TextRenderer::Draw(*renderer, *fill, outline.Get(), layout, 0.0f, 0.0f, kFill, BothEffects()), 0u);
        EXPECT_TRUE(renderer->calls.empty());
    }

    TEST_F(TextRendererTest, TheOutlineAtlasIsPreparedByTheDraw) {
        const TextLayoutResult layout = Layout("A");
        auto fill = Atlas();
        auto outline = Library().Get(*renderer, font, 24, kOutlineRadius);
        ASSERT_EQ(outline->GetGlyphCount(), 0u);

        TextRenderer::Draw(*renderer, *fill, outline.Get(), layout, 0.0f, 0.0f, kFill, BothEffects());

        EXPECT_NE(outline->Find(layout.glyphs[0].glyph), nullptr);
    }

    TEST_F(TextRendererTest, TheShadowOffsetIsInOutputPixelsAndSnapped) {
        renderer->SetViewTransform({2.0f, 0.0f, 0.0f});
        const TextLayoutResult layout = Layout("A");
        auto fill = Atlas();
        TextEffects effects;
        effects.shadowColor = kShadowColor;
        effects.shadowOffsetX = 2.6f;
        effects.shadowOffsetY = 0.0f;

        TextRenderer::Draw(*renderer, *fill, nullptr, layout, 0.0f, 0.0f, kFill, effects);

        ASSERT_EQ(renderer->calls.size(), 2u);
        EXPECT_FLOAT_EQ(At(renderer->calls, 0).rect.x, std::round(layout.glyphs[0].x + fill->Find(layout.glyphs[0].glyph)->offsetX + 2.6f));
        EXPECT_FLOAT_EQ(At(renderer->calls, 0).rect.x, std::round(At(renderer->calls, 0).rect.x));
    }

}  // namespace BECore::Tests
