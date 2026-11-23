#include <BECore/Memory/ScratchArena.h>
#include <BECore/Text/GlyphOutline.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint8_t kFull = 255;

        GlyphBitmap Block(uint32_t width, uint32_t height, uint8_t coverage = kFull) {
            GlyphBitmap glyph;
            glyph.width = width;
            glyph.height = height;
            glyph.offsetX = 3;
            glyph.offsetY = -7;
            glyph.coverage.assign(static_cast<size_t>(width) * height, coverage);
            return glyph;
        }

        uint8_t At(const GlyphBitmap& glyph, uint32_t x, uint32_t y) {
            return glyph.coverage[static_cast<size_t>(y) * glyph.width + x];
        }

    }  // namespace

    TEST(GlyphOutlineTest, TheResultGrowsOnEverySideAndItsOffsetsFollow) {
        const GlyphBitmap grown = GlyphOutline::Dilate(Block(3, 2), 2);

        EXPECT_EQ(grown.width, 7u);
        EXPECT_EQ(grown.height, 6u);
        EXPECT_EQ(grown.offsetX, 1);
        EXPECT_EQ(grown.offsetY, -9);
        EXPECT_EQ(grown.coverage.size(), 42u);
    }

    TEST(GlyphOutlineTest, ABlockGrownByOneHasTheExactCoverage) {
        const GlyphBitmap grown = GlyphOutline::Dilate(Block(3, 3), 1);

        ASSERT_EQ(grown.width, 5u);
        EXPECT_EQ(At(grown, 2, 2), kFull);  // the centre of the block
        EXPECT_EQ(At(grown, 1, 1), kFull);  // the corner of the block: it is covered itself
        EXPECT_EQ(At(grown, 0, 2), 128);    // one pixel off an edge: 255 * 0.5
        EXPECT_EQ(At(grown, 2, 0), 128);
        EXPECT_EQ(At(grown, 0, 0), 22);  // one pixel off a corner on the diagonal: 255 * (1.5 - sqrt(2))
        EXPECT_EQ(At(grown, 4, 4), 22);
    }

    TEST(GlyphOutlineTest, ABlockGrownByTwoHasTheExactCoverage) {
        const GlyphBitmap grown = GlyphOutline::Dilate(Block(3, 3), 2);

        ASSERT_EQ(grown.width, 7u);
        EXPECT_EQ(At(grown, 3, 3), kFull);
        EXPECT_EQ(At(grown, 2, 2), kFull);  // the corner of the block
        EXPECT_EQ(At(grown, 1, 1), kFull);  // sqrt(2) from the corner: inside the radius
        EXPECT_EQ(At(grown, 0, 3), 128);    // two pixels off an edge
        EXPECT_EQ(At(grown, 0, 1), 67);     // sqrt(5) from the corner: 255 * (2.5 - sqrt(5))
        EXPECT_EQ(At(grown, 0, 0), 0);      // 2 * sqrt(2) from the corner: out of reach
    }

    // The target of a buffer form is the caller's and may hold anything: every byte of it is written
    TEST(GlyphOutlineTest, TheBufferFormOverwritesAnyTargetWithTheBitmapOfTheHeapForm) {
        GlyphBitmap glyph = Block(4, 3);
        glyph.coverage[1] = 90;
        glyph.coverage[6] = 0;
        const GlyphBitmap expected = GlyphOutline::Dilate(glyph, 3);
        Vector<uint8_t> target(expected.coverage.size(), 0xEE);

        const ScratchScope scratch;
        const GlyphOutline::Kernel kernel(3);
        GlyphOutline::Dilate(Span<const uint8_t>(glyph.coverage.data(), glyph.coverage.size()), glyph.width, glyph.height, kernel, Span<uint8_t>(target.data(), target.size()));

        EXPECT_EQ(target, expected.coverage);
    }

    TEST(GlyphOutlineTest, TheBufferFormOfRadiusZeroCopiesTheSource) {
        const GlyphBitmap glyph = Block(3, 2, 77);
        Vector<uint8_t> target(glyph.coverage.size(), 0);

        const ScratchScope scratch;
        const GlyphOutline::Kernel kernel(0);
        GlyphOutline::Dilate(Span<const uint8_t>(glyph.coverage.data(), glyph.coverage.size()), glyph.width, glyph.height, kernel, Span<uint8_t>(target.data(), target.size()));

        EXPECT_EQ(target, glyph.coverage);
    }

    TEST(GlyphOutlineTest, ASoftPixelKeepsItsCoverageUnderTheWeight) {
        const GlyphBitmap grown = GlyphOutline::Dilate(Block(1, 1, 100), 1);

        EXPECT_EQ(At(grown, 1, 1), 100);  // the pixel itself, at the full weight
        EXPECT_EQ(At(grown, 0, 1), 50);
    }

    TEST(GlyphOutlineTest, TheMaximumWinsWhereTwoPixelsReach) {
        GlyphBitmap glyph = Block(3, 1);
        glyph.coverage = {kFull, 0, kFull};

        const GlyphBitmap grown = GlyphOutline::Dilate(glyph, 1);

        EXPECT_EQ(At(grown, 2, 1), 128);  // between the two pixels, one pixel from each: the maximum, not the sum
        EXPECT_EQ(At(grown, 1, 1), kFull);
    }

    TEST(GlyphOutlineTest, RadiusZeroReturnsTheGlyphUnchanged) {
        const GlyphBitmap glyph = Block(3, 3);

        const GlyphBitmap same = GlyphOutline::Dilate(glyph, 0);

        EXPECT_EQ(same.width, glyph.width);
        EXPECT_EQ(same.offsetX, glyph.offsetX);
        EXPECT_EQ(same.coverage, glyph.coverage);
        EXPECT_EQ(GlyphOutline::Dilate(glyph, -3).coverage, glyph.coverage);
    }

    TEST(GlyphOutlineTest, AGlyphWithNoInkStaysEmpty) {
        GlyphBitmap space;
        space.offsetX = 1;

        const GlyphBitmap grown = GlyphOutline::Dilate(space, 3);

        EXPECT_EQ(grown.width, 0u);
        EXPECT_EQ(grown.height, 0u);
        EXPECT_TRUE(grown.coverage.empty());
    }

    TEST(GlyphOutlineTest, TheRadiusIsCutToTheMaximum) {
        const GlyphBitmap grown = GlyphOutline::Dilate(Block(1, 1), kMaxOutlinePixels + 10);

        EXPECT_EQ(grown.width, 1u + 2u * kMaxOutlinePixels);
        EXPECT_EQ(grown.offsetX, 3 - kMaxOutlinePixels);
    }

}  // namespace BECore::Tests
