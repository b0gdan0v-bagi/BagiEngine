#include <Common/Printers.h>
#include <Widgets/TextureEditor/SpriteSheetGrid.h>
#include <Widgets/TextureEditor/SpriteSheetIslands.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kBytesPerPixel = 4;
        constexpr uint8_t kSolid = 255;

        RgbaImage Blank(uint32_t width, uint32_t height) {
            RgbaImage image;
            image.width = width;
            image.height = height;
            image.pixels.assign(static_cast<size_t>(width) * height * kBytesPerPixel, 0);
            return image;
        }

        /// Sets the alpha of a rectangle of pixels (0 clears it); the colour does not matter to the finder
        void Fill(RgbaImage& image, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint8_t alpha = kSolid) {
            for (uint32_t row = y; row < y + height; ++row) {
                for (uint32_t column = x; column < x + width; ++column) {
                    image.pixels[(static_cast<size_t>(row) * image.width + column) * kBytesPerPixel + 3] = alpha;
                }
            }
        }

        IslandSpec Spec(int minSize = 1, int mergeGap = 0, int padding = 0, int alphaThreshold = kDefaultIslandAlphaThreshold) {
            IslandSpec spec;
            spec.alphaThreshold = alphaThreshold;
            spec.minSize = minSize;
            spec.padding = padding;
            spec.mergeGap = mergeGap;
            return spec;
        }

        void ExpectRect(const Rect& actual, float x, float y, float w, float h) {
            EXPECT_EQ(actual.x, x);
            EXPECT_EQ(actual.y, y);
            EXPECT_EQ(actual.w, w);
            EXPECT_EQ(actual.h, h);
        }

        Vector<Rect> MustFind(const RgbaImage& image, const IslandSpec& spec) {
            const auto found = FindAlphaIslands(image, spec);
            EXPECT_TRUE(found.has_value()) << (found ? "" : SheetIslandErrorText(found.error()).data());
            return found ? *found : Vector<Rect>();
        }

        SheetIslandError MustFail(const RgbaImage& image, const IslandSpec& spec) {
            const auto found = FindAlphaIslands(image, spec);
            EXPECT_FALSE(found.has_value());
            return found ? SheetIslandError::NoImage : found.error();
        }

        /// Dots two pixels apart on both axes: no two are 8-connected and one empty pixel lies between neighbours
        RgbaImage Dots(uint32_t countPerSide) {
            RgbaImage image = Blank(countPerSide * 2, countPerSide * 2);
            for (uint32_t row = 0; row < countPerSide; ++row) {
                for (uint32_t column = 0; column < countPerSide; ++column) {
                    Fill(image, column * 2, row * 2, 1, 1);
                }
            }
            return image;
        }

    }  // namespace

    TEST(SpriteSheetIslands, OneIslandGivesItsExactBox) {
        RgbaImage image = Blank(20, 20);
        Fill(image, 5, 6, 8, 4);
        const auto rects = MustFind(image, Spec());
        ASSERT_EQ(rects.size(), 1u);
        ExpectRect(rects[0], 5, 6, 8, 4);
    }

    TEST(SpriteSheetIslands, TwoSeparateIslandsComeLeftToRight) {
        RgbaImage image = Blank(30, 20);
        Fill(image, 12, 3, 5, 5);
        Fill(image, 2, 2, 6, 6);
        const auto rects = MustFind(image, Spec());
        ASSERT_EQ(rects.size(), 2u);
        ExpectRect(rects[0], 2, 2, 6, 6);
        ExpectRect(rects[1], 12, 3, 5, 5);
    }

    TEST(SpriteSheetIslands, ADiagonalLinkJoinsAndAOnePixelGapDoesNot) {
        RgbaImage touching = Blank(20, 20);
        Fill(touching, 2, 2, 4, 4);
        Fill(touching, 6, 6, 4, 4);  // its corner pixel touches the other's corner pixel
        const auto joined = MustFind(touching, Spec());
        ASSERT_EQ(joined.size(), 1u);
        ExpectRect(joined[0], 2, 2, 8, 8);

        RgbaImage apart = Blank(20, 20);
        Fill(apart, 2, 2, 4, 4);
        Fill(apart, 7, 7, 4, 4);
        EXPECT_EQ(MustFind(apart, Spec()).size(), 2u);
    }

    TEST(SpriteSheetIslands, AlphaEqualToTheThresholdIsNotSolid) {
        RgbaImage image = Blank(10, 10);
        Fill(image, 2, 2, 4, 4, 16);
        EXPECT_EQ(MustFail(image, Spec(1, 0, 0, 16)), SheetIslandError::NoIslands);

        Fill(image, 2, 2, 4, 4, 17);
        const auto rects = MustFind(image, Spec(1, 0, 0, 16));
        ASSERT_EQ(rects.size(), 1u);
        ExpectRect(rects[0], 2, 2, 4, 4);
    }

    TEST(SpriteSheetIslands, ADotOnePixelFromABodyNeedsAMergeGapOfOne) {
        RgbaImage image = Blank(20, 12);
        Fill(image, 2, 2, 6, 6);
        Fill(image, 9, 2, 1, 1);  // column 8 stays empty

        const auto apart = MustFind(image, Spec(1, 0));
        ASSERT_EQ(apart.size(), 2u);
        ExpectRect(apart[0], 2, 2, 6, 6);
        ExpectRect(apart[1], 9, 2, 1, 1);

        const auto merged = MustFind(image, Spec(1, 1));
        ASSERT_EQ(merged.size(), 1u);
        ExpectRect(merged[0], 2, 2, 8, 6);
    }

    TEST(SpriteSheetIslands, ABoxInsideAnotherAlwaysMerges) {
        RgbaImage image = Blank(20, 20);
        Fill(image, 2, 2, 10, 10);
        Fill(image, 4, 4, 6, 6, 0);  // a ring
        Fill(image, 7, 7, 1, 1);     // the spark inside
        const auto rects = MustFind(image, Spec(1, 0));
        ASSERT_EQ(rects.size(), 1u);
        ExpectRect(rects[0], 2, 2, 10, 10);
    }

    TEST(SpriteSheetIslands, MergeRunsInAChain) {
        RgbaImage image = Blank(20, 10);
        Fill(image, 0, 0, 3, 3);
        Fill(image, 4, 0, 3, 3);
        Fill(image, 8, 0, 3, 3);  // the first and the last are 5 pixels apart
        const auto rects = MustFind(image, Spec(1, 1));
        ASSERT_EQ(rects.size(), 1u);
        ExpectRect(rects[0], 0, 0, 11, 3);
    }

    TEST(SpriteSheetIslands, AMergeDoesNotDependOnTheScanOrder) {
        // R bridges P and Q, which are too far apart to merge alone, and the mirrored images scan the three boxes in another order
        const auto draw = [](bool mirrorX, bool mirrorY) {
            RgbaImage image = Blank(12, 10);
            const auto place = [&](uint32_t x, uint32_t y, uint32_t w, uint32_t h) { Fill(image, mirrorX ? 12 - x - w : x, mirrorY ? 10 - y - h : y, w, h); };
            place(0, 0, 3, 3);
            place(7, 0, 3, 3);
            place(4, 4, 2, 2);
            return image;
        };
        const auto plain = MustFind(draw(false, false), Spec(1, 2));
        ASSERT_EQ(plain.size(), 1u);
        ExpectRect(plain[0], 0, 0, 10, 6);
        const auto mirroredX = MustFind(draw(true, false), Spec(1, 2));
        ASSERT_EQ(mirroredX.size(), 1u);
        ExpectRect(mirroredX[0], 2, 0, 10, 6);
        const auto mirroredY = MustFind(draw(false, true), Spec(1, 2));
        ASSERT_EQ(mirroredY.size(), 1u);
        ExpectRect(mirroredY[0], 0, 4, 10, 6);
        EXPECT_EQ(MustFind(draw(false, false), Spec(1, 0)).size(), 3u);
    }

    TEST(SpriteSheetIslands, ASpeckIsDroppedOnlyAfterTheMerge) {
        RgbaImage image = Blank(20, 12);
        Fill(image, 2, 2, 6, 6);
        Fill(image, 9, 3, 1, 1);

        const auto dropped = MustFind(image, Spec(4, 0));
        ASSERT_EQ(dropped.size(), 1u);
        ExpectRect(dropped[0], 2, 2, 6, 6);

        const auto kept = MustFind(image, Spec(4, 1));
        ASSERT_EQ(kept.size(), 1u);
        ExpectRect(kept[0], 2, 2, 8, 6);
    }

    TEST(SpriteSheetIslands, AnIslandNarrowerThanTheMinimumIsDropped) {
        RgbaImage image = Blank(20, 20);
        Fill(image, 2, 2, 6, 3);  // tall enough, but 3 high
        EXPECT_EQ(MustFail(image, Spec(4)), SheetIslandError::NoIslands);
        EXPECT_EQ(MustFind(image, Spec(3)).size(), 1u);
    }

    TEST(SpriteSheetIslands, PaddingGrowsEachSideAndClampsAtTheSheet) {
        RgbaImage image = Blank(20, 20);
        Fill(image, 8, 8, 4, 4);
        Fill(image, 0, 0, 3, 3);
        Fill(image, 16, 16, 4, 4);
        const auto rects = MustFind(image, Spec(1, 0, 2));
        ASSERT_EQ(rects.size(), 3u);
        ExpectRect(rects[0], 0, 0, 5, 5);
        ExpectRect(rects[1], 6, 6, 8, 8);
        ExpectRect(rects[2], 14, 14, 6, 6);
    }

    TEST(SpriteSheetIslands, PaddedRectsMayOverlap) {
        RgbaImage image = Blank(20, 12);
        Fill(image, 2, 2, 4, 4);
        Fill(image, 9, 2, 4, 4);
        const auto rects = MustFind(image, Spec(1, 0, 2));
        ASSERT_EQ(rects.size(), 2u);
        ExpectRect(rects[0], 0, 0, 8, 8);
        ExpectRect(rects[1], 7, 0, 8, 8);  // starts one pixel before the first one ends
    }

    TEST(SpriteSheetIslands, TwoRowsOfThreeReadRowByRow) {
        RgbaImage image = Blank(60, 40);
        // Row one: the tall island in the middle starts highest and leads, the others are lower by a few pixels
        Fill(image, 2, 3, 8, 8);
        Fill(image, 20, 2, 8, 14);
        Fill(image, 40, 5, 8, 8);
        // Row two: the middle one leads again
        Fill(image, 2, 24, 8, 8);
        Fill(image, 20, 22, 8, 8);
        Fill(image, 40, 25, 8, 8);

        const auto rects = MustFind(image, Spec(1));
        ASSERT_EQ(rects.size(), 6u);
        ExpectRect(rects[0], 2, 3, 8, 8);
        ExpectRect(rects[1], 20, 2, 8, 14);
        ExpectRect(rects[2], 40, 5, 8, 8);
        ExpectRect(rects[3], 2, 24, 8, 8);
        ExpectRect(rects[4], 20, 22, 8, 8);
        ExpectRect(rects[5], 40, 25, 8, 8);

        // The same shapes drawn in another order are the same image and give the same list
        RgbaImage reversed = Blank(60, 40);
        Fill(reversed, 40, 25, 8, 8);
        Fill(reversed, 20, 22, 8, 8);
        Fill(reversed, 2, 24, 8, 8);
        Fill(reversed, 40, 5, 8, 8);
        Fill(reversed, 20, 2, 8, 14);
        Fill(reversed, 2, 3, 8, 8);
        const auto again = MustFind(reversed, Spec(1));
        ASSERT_EQ(again.size(), rects.size());
        for (size_t i = 0; i < rects.size(); ++i) {
            ExpectRect(again[i], rects[i].x, rects[i].y, rects[i].w, rects[i].h);
        }
    }

    TEST(SpriteSheetIslands, TheChessSheetShapeGivesTwelveRectsInGridOrder) {
        constexpr uint32_t kWidth = 700;
        constexpr uint32_t kHeight = 381;
        constexpr uint32_t kColumns = 6;
        constexpr uint32_t kRows = 2;
        constexpr uint32_t kPieceWidth = 76;
        constexpr uint32_t kPieceHeight = 140;

        RgbaImage image = Blank(kWidth, kHeight);
        Vector<Rect> expected;
        for (uint32_t row = 0; row < kRows; ++row) {
            for (uint32_t column = 0; column < kColumns; ++column) {
                // Tops differ a little from piece to piece, as the pieces of the real sheet do
                const uint32_t x = column * 116 + 20;
                const uint32_t y = row * 190 + 25 + (column * 3) % 7;
                Fill(image, x, y, kPieceWidth, kPieceHeight);
                expected.push_back(Rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(kPieceWidth), static_cast<float>(kPieceHeight)));
            }
        }

        const auto rects = MustFind(image, IslandSpec{});
        ASSERT_EQ(rects.size(), 12u);
        const auto cells = ComputeGridCells(kWidth, kHeight, SheetGridSpec{kColumns, kRows, 0, 0});
        ASSERT_TRUE(cells.has_value());
        for (size_t i = 0; i < rects.size(); ++i) {
            ExpectRect(rects[i], expected[i].x, expected[i].y, expected[i].w, expected[i].h);
            const Rect& cell = (*cells)[i];
            EXPECT_GE(rects[i].x, cell.x) << i;
            EXPECT_GE(rects[i].y, cell.y) << i;
            EXPECT_LE(rects[i].x + rects[i].w, cell.x + cell.w) << i;
            EXPECT_LE(rects[i].y + rects[i].h, cell.y + cell.h) << i;
        }
    }

    TEST(SpriteSheetIslands, ASameImageAndSpecAlwaysGiveTheSameList) {
        RgbaImage image = Blank(60, 40);
        Fill(image, 2, 3, 8, 8);
        Fill(image, 30, 2, 8, 14);
        Fill(image, 10, 24, 8, 8);
        const auto first = MustFind(image, Spec(1));
        const auto second = MustFind(image, Spec(1));
        ASSERT_EQ(first.size(), second.size());
        for (size_t i = 0; i < first.size(); ++i) {
            ExpectRect(second[i], first[i].x, first[i].y, first[i].w, first[i].h);
        }
    }

    TEST(SpriteSheetIslands, AnImageWithoutSizeOrWithAWrongPixelCountIsNoImage) {
        EXPECT_EQ(MustFail(RgbaImage{}, Spec()), SheetIslandError::NoImage);
        RgbaImage noHeight = Blank(10, 10);
        noHeight.height = 0;
        EXPECT_EQ(MustFail(noHeight, Spec()), SheetIslandError::NoImage);
        RgbaImage mismatch = Blank(10, 10);
        mismatch.pixels.pop_back();
        EXPECT_EQ(MustFail(mismatch, Spec()), SheetIslandError::NoImage);
    }

    TEST(SpriteSheetIslands, ABadSpecIsRefused) {
        RgbaImage image = Blank(10, 10);
        Fill(image, 2, 2, 4, 4);
        EXPECT_EQ(MustFail(image, Spec(1, 0, 0, 255)), SheetIslandError::BadSpec);
        EXPECT_EQ(MustFail(image, Spec(1, 0, 0, -1)), SheetIslandError::BadSpec);
        EXPECT_EQ(MustFail(image, Spec(0)), SheetIslandError::BadSpec);
        EXPECT_EQ(MustFail(image, Spec(1, 0, -1)), SheetIslandError::BadSpec);
        EXPECT_EQ(MustFail(image, Spec(1, 0, kMaxIslandPadding + 1)), SheetIslandError::BadSpec);
        EXPECT_EQ(MustFail(image, Spec(1, -1)), SheetIslandError::BadSpec);
        EXPECT_EQ(MustFail(image, Spec(1, kMaxIslandMergeGap + 1)), SheetIslandError::BadSpec);
        EXPECT_EQ(MustFind(image, Spec(1, kMaxIslandMergeGap, kMaxIslandPadding, kMaxIslandAlphaThreshold)).size(), 1u);
    }

    TEST(SpriteSheetIslands, ASheetWithNoSolidPixelHasNoIslands) {
        EXPECT_EQ(MustFail(Blank(10, 10), Spec()), SheetIslandError::NoIslands);
    }

    TEST(SpriteSheetIslands, MoreIslandsThanTheGridLimitIsTooMany) {
        const RgbaImage image = Dots(40);  // 1600 single pixels
        ASSERT_GT(1600, kMaxSheetGridCells);
        EXPECT_EQ(MustFail(image, Spec(1, 0)), SheetIslandError::TooManyIslands);
        // A merge that brings them under the limit is allowed: only the result is counted
        EXPECT_EQ(MustFind(image, Spec(1, 1)).size(), 1u);
    }

    TEST(SpriteSheetIslands, MoreRawPiecesThanTheComponentLimitIsTooMany) {
        const RgbaImage image = Dots(130);  // 16900 pieces
        ASSERT_GT(130u * 130u, kMaxIslandComponents);
        EXPECT_EQ(MustFail(image, Spec(1, kMaxIslandMergeGap)), SheetIslandError::TooManyIslands);
    }

    TEST(SpriteSheetIslands, EveryErrorHasAText) {
        for (const auto error : {SheetIslandError::NoImage, SheetIslandError::BadSpec, SheetIslandError::TooManyIslands, SheetIslandError::NoIslands}) {
            EXPECT_FALSE(SheetIslandErrorText(error).empty());
        }
    }

}  // namespace BECore::Tests
