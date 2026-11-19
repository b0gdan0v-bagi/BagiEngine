#include <BECore/FontAtlasTestSupport.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Text/FontAtlas.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Text/GlyphOutline.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/TextTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kPixelSize = 24;
        constexpr uint32_t kPageSize = 512;

        const RecordedTexture* AsRecorded(ITexture* texture) {
            return static_cast<const RecordedTexture*>(texture);
        }

        uint8_t AlphaAt(const RecordedTexture& page, uint32_t x, uint32_t y) {
            const auto width = static_cast<uint32_t>(page.GetWidth());
            return page.GetPixels()[(static_cast<size_t>(y) * width + x) * 4 + 3];
        }

    }  // namespace

    class FontAtlasTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            font = LoadTestFont();
            ASSERT_TRUE(font);
            renderer = New<RecordingRenderer>();
            atlas = New<FontAtlas>(font, kPixelSize, kPageSize);
        }

        uint32_t Glyph(char32_t codePoint) const {
            return font->FindGlyph(codePoint);
        }

        IntrusivePtr<Font> font;
        IntrusivePtr<RecordingRenderer> renderer;
        IntrusivePtr<FontAtlas> atlas;
    };

    TEST_F(FontAtlasTest, ANewAtlasIsEmpty) {
        EXPECT_EQ(atlas->GetPageCount(), 0u);
        EXPECT_EQ(atlas->GetGlyphCount(), 0u);
        EXPECT_EQ(atlas->Find(Glyph(U'A')), nullptr);
        EXPECT_EQ(atlas->GetPixelSize(), kPixelSize);
    }

    TEST_F(FontAtlasTest, PrepareMakesAPageAndFindsTheGlyphs) {
        const uint32_t glyphs[] = {Glyph(U'A'), Glyph(U'B'), Glyph(0x0416)};

        EXPECT_TRUE(atlas->Prepare(*renderer, glyphs));

        EXPECT_EQ(atlas->GetPageCount(), 1u);
        EXPECT_EQ(renderer->texturesCreated, 1u);
        for (const uint32_t glyph : glyphs) {
            const AtlasGlyph* found = atlas->Find(glyph);
            ASSERT_NE(found, nullptr);
            EXPECT_EQ(found->page, 0);
            EXPECT_GT(found->source.w, 0.0f);
            EXPECT_GT(found->source.h, 0.0f);
        }
    }

    TEST_F(FontAtlasTest, AGlyphPictureHasTheCoverageOfTheGlyph) {
        const uint32_t glyphs[] = {Glyph(U'H')};
        atlas->Prepare(*renderer, glyphs);

        const AtlasGlyph* found = atlas->Find(glyphs[0]);
        ASSERT_NE(found, nullptr);
        const RecordedTexture* page = AsRecorded(atlas->GetPage(found->page));
        ASSERT_NE(page, nullptr);
        const GlyphBitmap expected = font->RasterizeGlyph(glyphs[0], static_cast<float>(kPixelSize));
        const auto left = static_cast<uint32_t>(found->source.x);
        const auto top = static_cast<uint32_t>(found->source.y);
        ASSERT_EQ(static_cast<uint32_t>(found->source.w), expected.width);
        ASSERT_EQ(static_cast<uint32_t>(found->source.h), expected.height);
        for (uint32_t y = 0; y < expected.height; ++y) {
            for (uint32_t x = 0; x < expected.width; ++x) {
                ASSERT_EQ(AlphaAt(*page, left + x, top + y), expected.coverage[static_cast<size_t>(y) * expected.width + x]) << x << "," << y;
            }
        }
        EXPECT_FLOAT_EQ(found->offsetX, static_cast<float>(expected.offsetX));
        EXPECT_FLOAT_EQ(found->offsetY, static_cast<float>(expected.offsetY));
    }

    TEST_F(FontAtlasTest, GlyphPixelsAreWhiteAndTheBorderIsTransparent) {
        const uint32_t glyphs[] = {Glyph(U'H')};
        atlas->Prepare(*renderer, glyphs);

        const AtlasGlyph* found = atlas->Find(glyphs[0]);
        ASSERT_NE(found, nullptr);
        const RecordedTexture* page = AsRecorded(atlas->GetPage(found->page));
        ASSERT_NE(page, nullptr);
        const auto& pixels = page->GetPixels();
        for (size_t i = 0; i < pixels.size(); i += 4) {
            ASSERT_EQ(pixels[i], 255);
            ASSERT_EQ(pixels[i + 1], 255);
            ASSERT_EQ(pixels[i + 2], 255);
        }
        // The padding around the glyph picture has no coverage
        const auto left = static_cast<uint32_t>(found->source.x);
        const auto top = static_cast<uint32_t>(found->source.y);
        EXPECT_EQ(AlphaAt(*page, left - 1, top), 0);
        EXPECT_EQ(AlphaAt(*page, left, top - 1), 0);
        EXPECT_EQ(AlphaAt(*page, left + static_cast<uint32_t>(found->source.w), top), 0);
    }

    TEST_F(FontAtlasTest, GlyphPicturesDoNotOverlapAndKeepAGapBetweenThem) {
        Vector<uint32_t> glyphs;
        for (char32_t codePoint = U'A'; codePoint <= U'Z'; ++codePoint) {
            glyphs.push_back(Glyph(codePoint));
        }
        atlas->Prepare(*renderer, glyphs);

        for (size_t i = 0; i < glyphs.size(); ++i) {
            for (size_t j = i + 1; j < glyphs.size(); ++j) {
                const AtlasGlyph* a = atlas->Find(glyphs[i]);
                const AtlasGlyph* b = atlas->Find(glyphs[j]);
                ASSERT_NE(a, nullptr);
                ASSERT_NE(b, nullptr);
                if (a->page != b->page) {
                    continue;
                }
                const float gap = static_cast<float>(FontAtlas::kPadding);
                const bool apart = a->source.x + a->source.w + gap <= b->source.x || b->source.x + b->source.w + gap <= a->source.x || a->source.y + a->source.h + gap <= b->source.y ||
                                   b->source.y + b->source.h + gap <= a->source.y;
                EXPECT_TRUE(apart) << i << " and " << j;
            }
        }
    }

    TEST_F(FontAtlasTest, ASpaceIsRememberedAsAGlyphWithNoInk) {
        const uint32_t glyphs[] = {Glyph(U' ')};

        EXPECT_TRUE(atlas->Prepare(*renderer, glyphs));

        const AtlasGlyph* found = atlas->Find(glyphs[0]);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->page, -1);
        EXPECT_EQ(atlas->GetPageCount(), 0u);  // nothing to upload
        EXPECT_EQ(renderer->texturesCreated, 0u);
    }

    TEST_F(FontAtlasTest, PreparingTheSameGlyphsAgainMakesNothing) {
        const uint32_t glyphs[] = {Glyph(U'A'), Glyph(U'B')};
        atlas->Prepare(*renderer, glyphs);

        atlas->Prepare(*renderer, glyphs);

        EXPECT_EQ(atlas->GetPageCount(), 1u);
        EXPECT_EQ(renderer->texturesCreated, 1u);
    }

    TEST_F(FontAtlasTest, NewGlyphsGoToANewPageAndTheOldPageStaysAsItWas) {
        const uint32_t first[] = {Glyph(U'A')};
        atlas->Prepare(*renderer, first);
        const AtlasGlyph before = Require(atlas->Find(first[0]));
        ITexture* firstPage = atlas->GetPage(0);
        const RecordedTexture* firstRecorded = AsRecorded(firstPage);
        ASSERT_NE(firstRecorded, nullptr);
        const Vector<uint8_t> firstPixels = firstRecorded->GetPixels();

        const uint32_t second[] = {Glyph(U'B')};
        atlas->Prepare(*renderer, second);

        EXPECT_EQ(atlas->GetPageCount(), 2u);
        EXPECT_EQ(atlas->GetPage(0), firstPage);  // the same texture object, never replaced
        EXPECT_EQ(firstRecorded->GetPixels(), firstPixels);
        EXPECT_EQ(Require(atlas->Find(first[0])).page, before.page);
        EXPECT_FLOAT_EQ(Require(atlas->Find(first[0])).source.x, before.source.x);
        EXPECT_EQ(Require(atlas->Find(second[0])).page, 1);
    }

    TEST_F(FontAtlasTest, DuplicatesInOnePrepareAreDrawnOnce) {
        const uint32_t glyphs[] = {Glyph(U'A'), Glyph(U'A'), Glyph(U'A')};

        atlas->Prepare(*renderer, glyphs);

        EXPECT_EQ(atlas->GetGlyphCount(), 1u);
    }

    TEST_F(FontAtlasTest, ASmallBatchGetsASmallPage) {
        const uint32_t glyphs[] = {Glyph(U'A')};
        atlas->Prepare(*renderer, glyphs);

        const RecordedTexture* page = AsRecorded(atlas->GetPage(0));
        ASSERT_NE(page, nullptr);
        EXPECT_LT(page->GetWidth(), static_cast<float>(kPageSize));
    }

    TEST_F(FontAtlasTest, ABatchThatDoesNotFitOnePageSpillsOverToMore) {
        // 64 px glyphs on a 128 px page: only a few fit on a page
        auto big = New<FontAtlas>(font, 64, 128);
        Vector<uint32_t> glyphs;
        for (char32_t codePoint = U'A'; codePoint <= U'Z'; ++codePoint) {
            glyphs.push_back(Glyph(codePoint));
        }

        EXPECT_TRUE(big->Prepare(*renderer, glyphs));

        EXPECT_GT(big->GetPageCount(), 1u);
        for (const uint32_t glyph : glyphs) {
            const AtlasGlyph* found = big->Find(glyph);
            ASSERT_NE(found, nullptr);
            EXPECT_GE(found->page, 0);
            EXPECT_LE(found->source.x + found->source.w, 128.0f);
            EXPECT_LE(found->source.y + found->source.h, 128.0f);
        }
    }

    TEST_F(FontAtlasTest, AGlyphLargerThanAPageIsRefusedAndNotRetried) {
        auto tiny = New<FontAtlas>(font, 200, 64);
        const uint32_t glyphs[] = {Glyph(U'W')};

        EXPECT_FALSE(tiny->Prepare(*renderer, glyphs));

        ASSERT_NE(tiny->Find(glyphs[0]), nullptr);
        EXPECT_EQ(tiny->Find(glyphs[0])->page, -1);
        EXPECT_TRUE(tiny->Prepare(*renderer, glyphs));  // remembered: no second attempt
    }

    TEST_F(FontAtlasTest, APreloadRangeDrawsTheGlyphsTheFontHas) {
        const GlyphRange ranges[] = {{U'0', U'9'}, {0x4E2D, 0x4E2E}};  // digits, and two code points the font lacks

        atlas->PrepareCodePoints(*renderer, ranges);

        EXPECT_EQ(atlas->GetGlyphCount(), 10u);
        EXPECT_NE(atlas->Find(Glyph(U'5')), nullptr);
    }

    TEST_F(FontAtlasTest, AFailedUploadLeavesTheGlyphsBlankAndSaysSo) {
        class FailingRenderer : public RecordingRenderer {
        public:
            IntrusivePtr<ITexture> CreateTexture(const uint8_t*, uint32_t, uint32_t) override {
                return {};
            }
        };
        auto failing = New<FailingRenderer>();
        const uint32_t glyphs[] = {Glyph(U'A')};

        EXPECT_FALSE(atlas->Prepare(*failing, glyphs));

        ASSERT_NE(atlas->Find(glyphs[0]), nullptr);
        EXPECT_EQ(atlas->Find(glyphs[0])->page, -1);
        EXPECT_EQ(atlas->GetPageCount(), 0u);
    }

    TEST_F(FontAtlasTest, TheDefaultPreloadMakesTheTextOfTheProjectDrawableAtOnce) {
        CoreManager::GetFontAtlasLibrary().Clear();
        auto library = CoreManager::GetFontAtlasLibrary().Get(*renderer, font, kPixelSize);
        ASSERT_TRUE(library);
        const uint32_t pagesAfterPreload = static_cast<uint32_t>(library->GetPageCount());

        Vector<uint32_t> glyphs;
        for (const char32_t codePoint : {U'S', U'c', U'o', U'r', U'e', U':', U' ', U'0', U'9', char32_t{0x0421}, char32_t{0x0447}, char32_t{0x0451}, char32_t{0x00E9}}) {
            glyphs.push_back(font->ResolveGlyph(codePoint));
        }
        library->Prepare(*renderer, glyphs);

        EXPECT_GE(pagesAfterPreload, 1u);
        EXPECT_EQ(library->GetPageCount(), pagesAfterPreload);  // everything was there already
        CoreManager::GetFontAtlasLibrary().Clear();
    }

    TEST_F(FontAtlasLibraryTest, QuantizeSizeRoundsToWholePixels) {
        EXPECT_EQ(FontAtlasLibrary::QuantizeSize(24.0f), 24);
        EXPECT_EQ(FontAtlasLibrary::QuantizeSize(24.4f), 24);
        EXPECT_EQ(FontAtlasLibrary::QuantizeSize(24.5f), 25);
        EXPECT_EQ(FontAtlasLibrary::QuantizeSize(0.2f), 1);
        EXPECT_EQ(FontAtlasLibrary::QuantizeSize(-5.0f), 1);
        EXPECT_EQ(FontAtlasLibrary::QuantizeSize(std::numeric_limits<float>::quiet_NaN()), 1);
        EXPECT_EQ(FontAtlasLibrary::QuantizeSize(100000.0f), FontAtlasLibrary::kMaxPixelSize);
    }

    TEST_F(FontAtlasLibraryTest, TheSameFontAndSizeGiveTheSameAtlas) {
        auto first = Library().Get(*renderer, font, 20);
        auto second = Library().Get(*renderer, font, 20);

        ASSERT_TRUE(first);
        EXPECT_EQ(first.Get(), second.Get());
        EXPECT_EQ(Library().GetAtlasCount(), 1u);
    }

    TEST_F(FontAtlasLibraryTest, ADifferentSizeGivesAnotherAtlas) {
        auto small = Library().Get(*renderer, font, 16);
        auto large = Library().Get(*renderer, font, 32);

        EXPECT_NE(small.Get(), large.Get());
        EXPECT_EQ(small->GetPixelSize(), 16);
        EXPECT_EQ(large->GetPixelSize(), 32);
        EXPECT_EQ(Library().GetAtlasCount(), 2u);
    }

    TEST_F(FontAtlasLibraryTest, ANewAtlasIsPreloaded) {
        auto atlas = Library().Get(*renderer, font, 20);

        EXPECT_GE(atlas->GetPageCount(), 1u);
        EXPECT_NE(atlas->Find(font->FindGlyph(0x0416)), nullptr);  // Ж
        EXPECT_NE(atlas->Find(font->FindGlyph(U'z')), nullptr);
    }

    TEST_F(FontAtlasLibraryTest, ANewRendererDropsTheAtlasesOfTheOldOne) {
        auto first = Library().Get(*renderer, font, 20);
        auto otherRenderer = New<RecordingRenderer>();

        auto second = Library().Get(*otherRenderer, font, 20);

        EXPECT_NE(first.Get(), second.Get());
        EXPECT_EQ(Library().GetAtlasCount(), 1u);
        EXPECT_GE(otherRenderer->texturesCreated, 1u);
    }

    TEST_F(FontAtlasLibraryTest, AFontThatDidNotLoadGivesNoAtlas) {
        auto broken = Font::CreateFromMemory("broken"_intern, Vector<uint8_t>(32, 0x41));

        EXPECT_FALSE(Library().Get(*renderer, broken, 20));
        EXPECT_FALSE(Library().Get(*renderer, IntrusivePtr<Font>(), 20));
    }

    TEST_F(FontAtlasLibraryTest, ClearDropsEveryAtlas) {
        Library().Get(*renderer, font, 20);

        Library().Clear();

        EXPECT_EQ(Library().GetAtlasCount(), 0u);
    }

    TEST_F(FontAtlasLibraryTest, TheSizeIsKeptInRange) {
        auto huge = Library().Get(*renderer, font, 1000000);
        auto zero = Library().Get(*renderer, font, 0);

        EXPECT_EQ(huge->GetPixelSize(), FontAtlasLibrary::kMaxPixelSize);
        EXPECT_EQ(zero->GetPixelSize(), 1);
    }

    TEST_F(FontAtlasLibraryTest, QuantizeOutlineKeepsAnOutlineAtLeastOnePixel) {
        EXPECT_EQ(FontAtlasLibrary::QuantizeOutline(0.0f), 0);
        EXPECT_EQ(FontAtlasLibrary::QuantizeOutline(-2.0f), 0);
        EXPECT_EQ(FontAtlasLibrary::QuantizeOutline(std::numeric_limits<float>::quiet_NaN()), 0);
        EXPECT_EQ(FontAtlasLibrary::QuantizeOutline(0.1f), 1);
        EXPECT_EQ(FontAtlasLibrary::QuantizeOutline(2.4f), 2);
        EXPECT_EQ(FontAtlasLibrary::QuantizeOutline(2.5f), 3);
        EXPECT_EQ(FontAtlasLibrary::QuantizeOutline(1000.0f), kMaxOutlinePixels);
    }

    TEST_F(FontAtlasLibraryTest, AnOutlineAtlasHasLargerPicturesAndNoPreload) {
        auto plain = Library().Get(*renderer, font, 24);
        auto outlined = Library().Get(*renderer, font, 24, 2);
        const uint32_t glyphs[] = {font->FindGlyph(U'H')};

        EXPECT_EQ(outlined->GetOutlinePixels(), 2);
        EXPECT_EQ(outlined->GetGlyphCount(), 0u);
        EXPECT_EQ(outlined->GetPageCount(), 0u);
        EXPECT_TRUE(outlined->Prepare(*renderer, glyphs));

        const AtlasGlyph* fill = plain->Find(glyphs[0]);
        const AtlasGlyph* grown = outlined->Find(glyphs[0]);
        ASSERT_NE(fill, nullptr);
        ASSERT_NE(grown, nullptr);
        EXPECT_FLOAT_EQ(grown->source.w, fill->source.w + 4.0f);
        EXPECT_FLOAT_EQ(grown->source.h, fill->source.h + 4.0f);
        EXPECT_FLOAT_EQ(grown->offsetX, fill->offsetX - 2.0f);
        EXPECT_FLOAT_EQ(grown->offsetY, fill->offsetY - 2.0f);
    }

    TEST_F(FontAtlasLibraryTest, TheOutlineWidthIsPartOfTheKey) {
        auto plain = Library().Get(*renderer, font, 24);
        auto thin = Library().Get(*renderer, font, 24, 1);
        auto thick = Library().Get(*renderer, font, 24, 2);

        EXPECT_NE(thin.Get(), thick.Get());
        EXPECT_NE(plain.Get(), thin.Get());
        EXPECT_EQ(Library().Get(*renderer, font, 24, 2).Get(), thick.Get());
        EXPECT_EQ(Library().GetAtlasCount(), 3u);
    }

    TEST_F(FontAtlasLibraryTest, TheOutlineIsKeptInRange) {
        EXPECT_EQ(Library().Get(*renderer, font, 12, 1000)->GetOutlinePixels(), kMaxOutlinePixels);
        EXPECT_EQ(Library().Get(*renderer, font, 12, -3)->GetOutlinePixels(), 0);
    }

}  // namespace BECore::Tests
