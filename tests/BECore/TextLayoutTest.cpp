#include <BECore/GameManager/CoreManager.h>
#include <BECore/Text/Font.h>
#include <BECore/Text/TextLayout.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kDefaultFontPath = "assets/fonts/PT_Sans-Web-Regular.ttf";
        constexpr float kSize = 20.0f;
        constexpr float kEllipsisCodePoint = 0x2026;

        char32_t LastCodePoint(const TextLayoutResult& result) {
            return result.glyphs.empty() ? 0 : result.glyphs.back().codePoint;
        }

        /// The code points of one line, in order
        eastl::vector<char32_t> LineText(const TextLayoutResult& result, size_t line) {
            eastl::vector<char32_t> text;
            const TextLine& textLine = result.lines[line];
            for (uint32_t i = 0; i < textLine.glyphCount; ++i) {
                text.push_back(result.glyphs[textLine.firstGlyph + i].codePoint);
            }
            return text;
        }

        eastl::vector<char32_t> Codes(const char32_t* text) {
            eastl::vector<char32_t> codes;
            for (; *text != 0; ++text) {
                codes.push_back(*text);
            }
            return codes;
        }

    }  // namespace

    class TextLayoutTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            auto handle = CoreManager::GetResourceManager().Load<Font>(kDefaultFontPath);
            font = IntrusivePtr<Font>(handle ? &*handle : nullptr);
            ASSERT_TRUE(font);
            ASSERT_EQ(font->GetState(), ResourceState::Loaded);
            options.fontSize = kSize;
        }

        TextLayoutResult Layout(eastl::string_view text) const {
            return TextLayout::Layout(*font, text, options);
        }

        /// The width of the text on one line without any limit
        float WidthOf(eastl::string_view text) const {
            TextLayoutOptions unlimited;
            unlimited.fontSize = options.fontSize;
            return TextLayout::Layout(*font, text, unlimited).width;
        }

        IntrusivePtr<Font> font;
        TextLayoutOptions options;
    };

    TEST_F(TextLayoutTest, AnEmptyTextHasNoLines) {
        const TextLayoutResult result = Layout("");

        EXPECT_TRUE(result.lines.empty());
        EXPECT_TRUE(result.glyphs.empty());
        EXPECT_FLOAT_EQ(result.width, 0.0f);
        EXPECT_FLOAT_EQ(result.height, 0.0f);
    }

    TEST_F(TextLayoutTest, OneLineIsAsWideAsItsAdvances) {
        const TextLayoutResult result = Layout("Hello");

        ASSERT_EQ(result.lines.size(), 1u);
        ASSERT_EQ(result.glyphs.size(), 5u);
        float expected = 0.0f;
        for (const char32_t codePoint : Codes(U"Hello")) {
            expected += font->GetAdvance(font->FindGlyph(codePoint), kSize);
        }
        EXPECT_NEAR(result.width, expected, 0.01f);
        EXPECT_FLOAT_EQ(result.glyphs[0].x, 0.0f);
        for (size_t i = 1; i < result.glyphs.size(); ++i) {
            EXPECT_GT(result.glyphs[i].x, result.glyphs[i - 1].x);
        }
    }

    TEST_F(TextLayoutTest, TheFirstBaselineIsTheAscentAndTheBoxStartsAtTheTop) {
        const TextLayoutResult result = Layout("Hello");
        const FontVerticalMetrics metrics = font->GetVerticalMetrics(kSize);

        EXPECT_NEAR(result.glyphs[0].y, metrics.ascent, 0.01f);
        EXPECT_NEAR(result.height, metrics.LineHeight(), 0.01f);
        EXPECT_FLOAT_EQ(result.fontSize, kSize);
    }

    TEST_F(TextLayoutTest, ANewlineStartsALine) {
        const TextLayoutResult result = Layout("ab\ncd");

        ASSERT_EQ(result.lines.size(), 2u);
        EXPECT_EQ(LineText(result, 0), Codes(U"ab"));
        EXPECT_EQ(LineText(result, 1), Codes(U"cd"));
        EXPECT_NEAR(result.lines[1].baseline - result.lines[0].baseline, result.lineHeight, 0.01f);
        EXPECT_NEAR(result.height, result.lineHeight * 2.0f, 0.01f);
    }

    TEST_F(TextLayoutTest, ACarriageReturnIsIgnored) {
        const TextLayoutResult unix = Layout("ab\ncd");
        const TextLayoutResult windows = Layout("ab\r\ncd");

        EXPECT_EQ(windows.lines.size(), unix.lines.size());
        EXPECT_EQ(windows.glyphs.size(), unix.glyphs.size());
    }

    TEST_F(TextLayoutTest, ATrailingNewlineLeavesAnEmptyLastLine) {
        const TextLayoutResult result = Layout("ab\n");

        ASSERT_EQ(result.lines.size(), 2u);
        EXPECT_EQ(result.lines[1].glyphCount, 0u);
    }

    TEST_F(TextLayoutTest, BlankLinesInTheMiddleAreKept) {
        const TextLayoutResult result = Layout("a\n\nb");

        ASSERT_EQ(result.lines.size(), 3u);
        EXPECT_EQ(result.lines[1].glyphCount, 0u);
    }

    TEST_F(TextLayoutTest, ATabIsFourSpaces) {
        const float tab = WidthOf("a\tb");
        const float spaces = WidthOf("a    b");

        EXPECT_NEAR(tab, spaces, 0.01f);
    }

    TEST_F(TextLayoutTest, WrapBreaksAtASpaceAndDropsIt) {
        options.maxWidth = WidthOf("aaaa bbbb") * 0.8f;  // "aaaa bbbb" does not fit, "aaaa" does

        const TextLayoutResult result = Layout("aaaa bbbb cccc");

        ASSERT_GE(result.lines.size(), 2u);
        EXPECT_EQ(LineText(result, 0), Codes(U"aaaa"));
        EXPECT_EQ(result.glyphs[result.lines[1].firstGlyph].codePoint, U'b');
        EXPECT_FLOAT_EQ(result.glyphs[result.lines[1].firstGlyph].x, 0.0f);  // no space hangs at the start of the line
        for (const TextLine& line : result.lines) {
            EXPECT_LE(line.width, options.maxWidth + 0.01f);
        }
        EXPECT_FALSE(result.overflowed);
    }

    TEST_F(TextLayoutTest, SeveralSpacesAtABreakAreAllDropped) {
        options.maxWidth = WidthOf("aaaa") * 1.2f;

        const TextLayoutResult result = Layout("aaaa     bbbb");

        ASSERT_EQ(result.lines.size(), 2u);
        EXPECT_EQ(LineText(result, 0), Codes(U"aaaa"));
        EXPECT_EQ(LineText(result, 1), Codes(U"bbbb"));
    }

    TEST_F(TextLayoutTest, AWordWiderThanTheBoxBreaksBetweenCharacters) {
        options.maxWidth = WidthOf("WWW");

        const TextLayoutResult result = Layout("WWWWWWWW");

        ASSERT_GE(result.lines.size(), 3u);
        for (const TextLine& line : result.lines) {
            EXPECT_LE(line.width, options.maxWidth + 0.01f);
            EXPECT_GT(line.glyphCount, 0u);
        }
        EXPECT_EQ(result.glyphs.size(), 8u);
    }

    TEST_F(TextLayoutTest, ABoxNarrowerThanOneGlyphStillMakesProgress) {
        options.maxWidth = 1.0f;

        const TextLayoutResult result = Layout("abc");

        EXPECT_EQ(result.lines.size(), 3u);
        EXPECT_EQ(result.glyphs.size(), 3u);
        EXPECT_TRUE(result.overflowed);
    }

    TEST_F(TextLayoutTest, WithoutWrapALongLineStaysOneLineAndOverflows) {
        options.maxWidth = WidthOf("aaaa");
        options.wrap = false;

        const TextLayoutResult result = Layout("aaaa bbbb cccc");

        ASSERT_EQ(result.lines.size(), 1u);
        EXPECT_GT(result.width, options.maxWidth);
        EXPECT_TRUE(result.overflowed);
        EXPECT_FALSE(result.truncated);
    }

    TEST_F(TextLayoutTest, WithoutAMaxWidthNothingWraps) {
        const TextLayoutResult result = Layout("aaaa bbbb cccc dddd eeee");

        EXPECT_EQ(result.lines.size(), 1u);
        EXPECT_FALSE(result.overflowed);
    }

    TEST_F(TextLayoutTest, AlignmentIsInsideTheBox) {
        options.maxWidth = 200.0f;
        options.align = TextAlign::Left;
        const TextLayoutResult left = Layout("Hi");
        options.align = TextAlign::Center;
        const TextLayoutResult center = Layout("Hi");
        options.align = TextAlign::Right;
        const TextLayoutResult right = Layout("Hi");

        const float width = left.width;
        EXPECT_FLOAT_EQ(left.lines[0].x, 0.0f);
        EXPECT_NEAR(center.lines[0].x, (200.0f - width) * 0.5f, 0.01f);
        EXPECT_NEAR(right.lines[0].x, 200.0f - width, 0.01f);
        EXPECT_NEAR(right.glyphs[0].x, 200.0f - width, 0.01f);
    }

    TEST_F(TextLayoutTest, WithoutABoxAlignmentIsInsideTheWidestLine) {
        options.align = TextAlign::Right;

        const TextLayoutResult result = Layout("a\nabcdef");

        EXPECT_NEAR(result.lines[1].x, 0.0f, 0.01f);
        EXPECT_NEAR(result.lines[0].x, result.width - result.lines[0].width, 0.01f);
    }

    TEST_F(TextLayoutTest, EachLineIsAlignedOnItsOwn) {
        options.maxWidth = 300.0f;
        options.align = TextAlign::Center;

        const TextLayoutResult result = Layout("a\nabcdef");

        EXPECT_GT(result.lines[0].x, result.lines[1].x);
        EXPECT_NEAR(result.lines[0].x + result.lines[0].width * 0.5f, 150.0f, 0.01f);
        EXPECT_NEAR(result.lines[1].x + result.lines[1].width * 0.5f, 150.0f, 0.01f);
    }

    TEST_F(TextLayoutTest, LineSpacingScalesTheLineHeight) {
        const TextLayoutResult normal = Layout("a\nb");
        options.lineSpacing = 1.5f;
        const TextLayoutResult loose = Layout("a\nb");

        EXPECT_NEAR(loose.lineHeight, normal.lineHeight * 1.5f, 0.01f);
        EXPECT_NEAR(loose.height, normal.height * 1.5f, 0.01f);
    }

    TEST_F(TextLayoutTest, CyrillicIsLaidOutWithItsOwnGlyphs) {
        const TextLayoutResult result = Layout("Привет, мир! Ёж");

        ASSERT_EQ(result.lines.size(), 1u);
        for (const PositionedGlyph& glyph : result.glyphs) {
            EXPECT_EQ(glyph.glyph, font->FindGlyph(glyph.codePoint));
            EXPECT_NE(glyph.glyph, 0u);
        }
    }

    TEST_F(TextLayoutTest, ACodePointTheFontLacksUsesTheFallbackGlyph) {
        const TextLayoutResult result = Layout("a\xE4\xB8\xAD"
                                               "b");  // a, U+4E2D, b

        ASSERT_EQ(result.glyphs.size(), 3u);
        EXPECT_EQ(result.glyphs[1].codePoint, 0x4E2Du);
        EXPECT_EQ(result.glyphs[1].glyph, font->ResolveGlyph(0x4E2D));
        EXPECT_NE(result.glyphs[1].glyph, 0u);
    }

    TEST_F(TextLayoutTest, DamagedUtf8DoesNotStopTheLayout) {
        const TextLayoutResult result = Layout(eastl::string_view("a\xD0z", 3));

        EXPECT_EQ(result.glyphs.size(), 3u);
    }

    TEST_F(TextLayoutTest, ClipKeepsAllTheTextAndSaysItOverflows) {
        options.maxWidth = WidthOf("aaaa");
        options.wrap = false;
        options.overflow = TextOverflow::Clip;

        const TextLayoutResult result = Layout("aaaa bbbb");

        EXPECT_EQ(result.glyphs.size(), 9u);
        EXPECT_TRUE(result.overflowed);
        EXPECT_FALSE(result.truncated);
    }

    TEST_F(TextLayoutTest, EllipsisCutsALineThatIsTooWide) {
        options.maxWidth = WidthOf("aaaa bb");
        options.wrap = false;
        options.overflow = TextOverflow::Ellipsis;

        const TextLayoutResult result = Layout("aaaa bbbb cccc dddd");

        ASSERT_EQ(result.lines.size(), 1u);
        EXPECT_TRUE(result.truncated);
        EXPECT_EQ(LastCodePoint(result), static_cast<char32_t>(kEllipsisCodePoint));
        EXPECT_LE(result.width, options.maxWidth + 0.01f);
        EXPECT_FALSE(result.overflowed);
    }

    TEST_F(TextLayoutTest, EllipsisLeavesATextThatFitsAlone) {
        options.maxWidth = 500.0f;
        options.overflow = TextOverflow::Ellipsis;

        const TextLayoutResult result = Layout("short");

        EXPECT_FALSE(result.truncated);
        EXPECT_EQ(result.glyphs.size(), 5u);
    }

    TEST_F(TextLayoutTest, EllipsisDoesNotLeaveASpaceBeforeIt) {
        options.maxWidth = WidthOf("aaaa b");
        options.wrap = false;
        options.overflow = TextOverflow::Ellipsis;

        const TextLayoutResult result = Layout("aaaa bbbb");

        ASSERT_GE(result.glyphs.size(), 2u);
        EXPECT_NE(result.glyphs[result.glyphs.size() - 2].codePoint, U' ');
    }

    TEST_F(TextLayoutTest, EllipsisKeepsTheLinesThatFitTheHeightAndEndsTheLastOne) {
        options.maxWidth = WidthOf("aaaa bbbb");
        options.overflow = TextOverflow::Ellipsis;
        options.maxHeight = font->GetVerticalMetrics(kSize).LineHeight() * 2.2f;  // two lines

        const TextLayoutResult result = Layout("aaaa bbbb cccc dddd eeee ffff");

        ASSERT_EQ(result.lines.size(), 2u);
        EXPECT_TRUE(result.truncated);
        EXPECT_EQ(LastCodePoint(result), static_cast<char32_t>(kEllipsisCodePoint));
        EXPECT_LE(result.height, options.maxHeight + 0.01f);
        EXPECT_LE(result.lines[1].width, options.maxWidth + 0.01f);
    }

    TEST_F(TextLayoutTest, EllipsisKeepsAtLeastOneLineInAShortBox) {
        options.maxWidth = WidthOf("aaaa");
        options.overflow = TextOverflow::Ellipsis;
        options.maxHeight = 1.0f;

        const TextLayoutResult result = Layout("aaaa bbbb");

        EXPECT_EQ(result.lines.size(), 1u);
        EXPECT_TRUE(result.truncated);
    }

    TEST_F(TextLayoutTest, AutoFitShrinksUntilTheTextFits) {
        options.fontSize = 40.0f;
        options.maxWidth = WidthOf("Hello, world") * 0.5f;  // at size 40 the text is twice too wide
        options.wrap = false;
        options.overflow = TextOverflow::AutoFit;
        options.minFontSize = 6.0f;

        const TextLayoutResult result = Layout("Hello, world");

        EXPECT_LT(result.fontSize, 40.0f);
        EXPECT_GE(result.fontSize, 6.0f);
        EXPECT_LE(result.width, options.maxWidth + 0.01f);
        EXPECT_FALSE(result.truncated);
        EXPECT_FALSE(result.overflowed);
    }

    TEST_F(TextLayoutTest, AutoFitPicksTheLargestWholeSizeThatFits) {
        options.fontSize = 40.0f;
        options.wrap = false;
        options.overflow = TextOverflow::AutoFit;
        options.minFontSize = 6.0f;
        options.maxWidth = WidthOf("Hello, world") * 0.5f;

        const TextLayoutResult result = Layout("Hello, world");

        // One pixel more would not fit
        TextLayoutOptions bigger = options;
        bigger.fontSize = result.fontSize + 1.0f;
        bigger.overflow = TextOverflow::Clip;
        EXPECT_TRUE(TextLayout::Layout(*font, "Hello, world", bigger).overflowed);
        EXPECT_FLOAT_EQ(result.fontSize, std::floor(result.fontSize));
    }

    TEST_F(TextLayoutTest, AutoFitLeavesATextThatFitsAtItsSize) {
        options.fontSize = 24.0f;
        options.maxWidth = 800.0f;
        options.overflow = TextOverflow::AutoFit;

        const TextLayoutResult result = Layout("fits");

        EXPECT_FLOAT_EQ(result.fontSize, 24.0f);
    }

    TEST_F(TextLayoutTest, AutoFitAlsoShrinksForTheHeight) {
        options.fontSize = 40.0f;
        options.maxWidth = 120.0f;
        options.maxHeight = 60.0f;
        options.overflow = TextOverflow::AutoFit;
        options.minFontSize = 6.0f;

        const TextLayoutResult result = Layout("one two three four five six seven");

        EXPECT_LT(result.fontSize, 40.0f);
        EXPECT_LE(result.height, 60.0f + 0.01f);
        EXPECT_LE(result.width, 120.0f + 0.01f);
    }

    TEST_F(TextLayoutTest, AutoFitCutsWithAnEllipsisWhenEvenTheSmallestSizeIsTooBig) {
        options.fontSize = 40.0f;
        options.maxWidth = 30.0f;
        options.wrap = false;
        options.overflow = TextOverflow::AutoFit;
        options.minFontSize = 20.0f;

        const TextLayoutResult result = Layout("a very long caption indeed");

        EXPECT_FLOAT_EQ(result.fontSize, 20.0f);
        EXPECT_TRUE(result.truncated);
        EXPECT_EQ(LastCodePoint(result), static_cast<char32_t>(kEllipsisCodePoint));
    }

    TEST_F(TextLayoutTest, AutoFitWithoutABoxChangesNothing) {
        options.fontSize = 30.0f;
        options.overflow = TextOverflow::AutoFit;

        const TextLayoutResult result = Layout("no box");

        EXPECT_FLOAT_EQ(result.fontSize, 30.0f);
    }

    TEST_F(TextLayoutTest, AFontThatFailedToLoadLaysOutNothing) {
        auto broken = Font::CreateFromMemory("broken"_intern, eastl::vector<uint8_t>(64, 0x41));

        const TextLayoutResult result = TextLayout::Layout(*broken, "text", options);

        EXPECT_TRUE(result.lines.empty());
        EXPECT_TRUE(result.glyphs.empty());
    }

    TEST_F(TextLayoutTest, ASizeOfZeroLaysOutNothing) {
        options.fontSize = 0.0f;

        EXPECT_TRUE(Layout("text").glyphs.empty());
    }

    TEST_F(TextLayoutTest, TheSameInputGivesTheSameLayout) {
        options.maxWidth = 150.0f;
        const TextLayoutResult first = Layout("Съешь ещё этих мягких французских булок, да выпей чаю");
        const TextLayoutResult second = Layout("Съешь ещё этих мягких французских булок, да выпей чаю");

        ASSERT_EQ(first.glyphs.size(), second.glyphs.size());
        for (size_t i = 0; i < first.glyphs.size(); ++i) {
            EXPECT_FLOAT_EQ(first.glyphs[i].x, second.glyphs[i].x);
            EXPECT_FLOAT_EQ(first.glyphs[i].y, second.glyphs[i].y);
        }
    }

}  // namespace BECore::Tests
