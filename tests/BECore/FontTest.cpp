#include <BECore/GameManager/CoreManager.h>
#include <BECore/Text/Font.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kDefaultFontPath = "assets/fonts/PT_Sans-Web-Regular.ttf";
        constexpr float kSize = 32.0f;

        /// The font that ships with the engine, loaded through the resource manager like a game would
        IntrusivePtr<Font> LoadDefaultFont() {
            auto handle = CoreManager::GetResourceManager().Load<Font>(kDefaultFontPath);
            return IntrusivePtr<Font>(handle ? &*handle : nullptr);
        }

    }  // namespace

    class FontTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            font = LoadDefaultFont();
            ASSERT_TRUE(font);
            ASSERT_EQ(font->GetState(), ResourceState::Loaded);
        }

        IntrusivePtr<Font> font;
    };

    TEST_F(FontTest, TheDefaultFontLoadsFromTheEngineAssets) {
        EXPECT_EQ(font->GetTypeName(), "Font"_intern);
        EXPECT_GT(font->GetMemoryUsage(), 0u);
        EXPECT_EQ(font->GetPath(), PoolString::Intern(kDefaultFontPath));
    }

    // The name a CSS font-family says: RmlUi's documents name the default font by it
    TEST_F(FontTest, TheFamilyNameComesFromTheNameTable) {
        EXPECT_EQ(font->GetFamilyName(), "PT Sans");
    }

    TEST_F(FontTest, TheSameFileIsLoadedOnce) {
        auto again = LoadDefaultFont();

        EXPECT_EQ(again.Get(), font.Get());
    }

    // The font is committed to the repository because it covers these ranges: Basic Latin, Latin-1, Cyrillic, punctuation
    TEST_F(FontTest, CoversLatinLatin1AndCyrillic) {
        for (char32_t codePoint = 0x20; codePoint <= 0x7E; ++codePoint) {
            EXPECT_NE(font->FindGlyph(codePoint), 0u) << "U+" << std::hex << static_cast<uint32_t>(codePoint);
        }
        for (char32_t codePoint = 0xA1; codePoint <= 0xFF; ++codePoint) {
            EXPECT_NE(font->FindGlyph(codePoint), 0u) << "U+" << std::hex << static_cast<uint32_t>(codePoint);
        }
        for (char32_t codePoint = 0x0410; codePoint <= 0x044F; ++codePoint) {  // А-я
            EXPECT_NE(font->FindGlyph(codePoint), 0u) << "U+" << std::hex << static_cast<uint32_t>(codePoint);
        }
        EXPECT_NE(font->FindGlyph(0x0401), 0u);  // Ё
        EXPECT_NE(font->FindGlyph(0x0451), 0u);  // ё
    }

    TEST_F(FontTest, CoversTheCommonPunctuation) {
        for (const char32_t codePoint :
             {char32_t{0x2013}, char32_t{0x2014}, char32_t{0x2018}, char32_t{0x2019}, char32_t{0x201C}, char32_t{0x201D}, char32_t{0x2022}, char32_t{0x2026}, char32_t{0x2116}}) {
            EXPECT_NE(font->FindGlyph(codePoint), 0u) << "U+" << std::hex << static_cast<uint32_t>(codePoint);
        }
    }

    TEST_F(FontTest, AMissingCodePointFindsNoGlyphAndResolvesToAFallback) {
        const char32_t chineseCharacter = 0x4E2D;

        EXPECT_EQ(font->FindGlyph(chineseCharacter), 0u);
        EXPECT_NE(font->ResolveGlyph(chineseCharacter), 0u);  // U+FFFD or '?'
        EXPECT_EQ(font->ResolveGlyph(U'A'), font->FindGlyph(U'A'));
    }

    TEST_F(FontTest, VerticalMetricsScaleWithTheSize) {
        const FontVerticalMetrics small = font->GetVerticalMetrics(16.0f);
        const FontVerticalMetrics large = font->GetVerticalMetrics(32.0f);

        EXPECT_GT(small.ascent, 0.0f);
        EXPECT_LT(small.descent, 0.0f);
        EXPECT_GT(small.LineHeight(), small.ascent);
        EXPECT_NEAR(large.ascent, small.ascent * 2.0f, 0.01f);
        EXPECT_NEAR(large.LineHeight(), small.LineHeight() * 2.0f, 0.01f);
    }

    TEST_F(FontTest, TheEmSquareMapsToTheSize) {
        // CSS-like sizes: the line is a bit taller than the size, and the capital letters are shorter than it
        const FontVerticalMetrics metrics = font->GetVerticalMetrics(kSize);
        const GlyphBitmap capital = font->RasterizeGlyph(font->FindGlyph(U'H'), kSize);

        EXPECT_GT(metrics.LineHeight(), kSize);
        EXPECT_LT(static_cast<float>(capital.height), kSize);
        EXPECT_GT(static_cast<float>(capital.height), kSize * 0.5f);
    }

    TEST_F(FontTest, AdvancesAreProportional) {
        const float narrow = font->GetAdvance(font->FindGlyph(U'i'), kSize);
        const float wide = font->GetAdvance(font->FindGlyph(U'W'), kSize);
        const float space = font->GetAdvance(font->FindGlyph(U' '), kSize);

        EXPECT_GT(narrow, 0.0f);
        EXPECT_GT(wide, narrow);
        EXPECT_GT(space, 0.0f);
        EXPECT_NEAR(font->GetAdvance(font->FindGlyph(U'W'), kSize * 2.0f), wide * 2.0f, 0.01f);
    }

    TEST_F(FontTest, AGlyphWithInkHasABitmapAboveTheBaseline) {
        const GlyphBitmap bitmap = font->RasterizeGlyph(font->FindGlyph(U'H'), kSize);

        ASSERT_GT(bitmap.width, 0u);
        ASSERT_GT(bitmap.height, 0u);
        EXPECT_EQ(bitmap.coverage.size(), static_cast<size_t>(bitmap.width) * bitmap.height);
        EXPECT_LT(bitmap.offsetY, 0);                                                       // the capital rises above the baseline
        EXPECT_GT(*std::max_element(bitmap.coverage.begin(), bitmap.coverage.end()), 200);  // solid ink somewhere
    }

    TEST_F(FontTest, ASpaceHasNoInk) {
        const GlyphBitmap bitmap = font->RasterizeGlyph(font->FindGlyph(U' '), kSize);

        EXPECT_EQ(bitmap.width, 0u);
        EXPECT_EQ(bitmap.height, 0u);
        EXPECT_TRUE(bitmap.coverage.empty());
    }

    TEST_F(FontTest, ADescenderGoesBelowTheBaseline) {
        const GlyphBitmap bitmap = font->RasterizeGlyph(font->FindGlyph(U'p'), kSize);

        ASSERT_GT(bitmap.height, 0u);
        EXPECT_GT(bitmap.offsetY + static_cast<int32_t>(bitmap.height), 0);
    }

    TEST(FontLoadTest, DataThatIsNotAFontFails) {
        const eastl::vector<uint8_t> garbage(256, 0x41);

        auto font = Font::CreateFromMemory("garbage"_intern, garbage);

        ASSERT_TRUE(font);
        EXPECT_EQ(font->GetState(), ResourceState::Failed);
        EXPECT_EQ(font->FindGlyph(U'A'), 0u);
        EXPECT_FLOAT_EQ(font->GetAdvance(1, 16.0f), 0.0f);
        EXPECT_TRUE(font->RasterizeGlyph(1, 16.0f).coverage.empty());
    }

    TEST(FontLoadTest, EmptyDataFails) {
        auto font = Font::CreateFromMemory("empty"_intern, {});

        EXPECT_EQ(font->GetState(), ResourceState::Failed);
    }

    TEST(FontLoadTest, AMissingFileFailsAndSaysSo) {
        EngineEnvironment::EnsureCoreServices();

        auto handle = CoreManager::GetResourceManager().Load<Font>("assets/fonts/does_not_exist.ttf");

        EXPECT_TRUE(!handle || handle->GetState() == ResourceState::Failed);
    }

}  // namespace BECore::Tests
