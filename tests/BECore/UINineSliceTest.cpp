#include <BECore/UI/UINineSlice.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-4f;

        void ExpectRect(const Rect& rect, float x, float y, float w, float h) {
            EXPECT_NEAR(rect.x, x, kTolerance);
            EXPECT_NEAR(rect.y, y, kTolerance);
            EXPECT_NEAR(rect.w, w, kTolerance);
            EXPECT_NEAR(rect.h, h, kTolerance);
        }

        float Area(const Rect& rect) {
            return rect.w * rect.h;
        }

    }  // namespace

    TEST(UINineSliceTest, CornersKeepTheirSizeAndTheMiddleStretches) {
        // A 16 x 16 picture with a border of 4 on every side, drawn 100 x 60 at (300, 100)
        const auto pieces = UINineSlice::Compute({0, 0, 16, 16}, {4, 4, 4, 4}, {300, 100, 100, 60});

        ExpectRect(pieces[0].destination, 300, 100, 4, 4);    // top-left
        ExpectRect(pieces[1].destination, 304, 100, 92, 4);   // top
        ExpectRect(pieces[2].destination, 396, 100, 4, 4);    // top-right
        ExpectRect(pieces[3].destination, 300, 104, 4, 52);   // left
        ExpectRect(pieces[4].destination, 304, 104, 92, 52);  // center
        ExpectRect(pieces[5].destination, 396, 104, 4, 52);   // right
        ExpectRect(pieces[6].destination, 300, 156, 4, 4);    // bottom-left
        ExpectRect(pieces[7].destination, 304, 156, 92, 4);   // bottom
        ExpectRect(pieces[8].destination, 396, 156, 4, 4);    // bottom-right

        ExpectRect(pieces[0].source, 0, 0, 4, 4);
        ExpectRect(pieces[4].source, 4, 4, 8, 8);
        ExpectRect(pieces[8].source, 12, 12, 4, 4);
    }

    TEST(UINineSliceTest, ThePiecesCoverTheDestinationAndTheSourceExactly) {
        const Rect source{8, 16, 32, 24};
        const Rect destination{5, 7, 123, 47};
        const auto pieces = UINineSlice::Compute(source, {3, 5, 7, 2}, destination);

        float destinationArea = 0.0f;
        float sourceArea = 0.0f;
        for (const UINineSlicePiece& piece : pieces) {
            destinationArea += Area(piece.destination);
            sourceArea += Area(piece.source);
        }
        EXPECT_NEAR(destinationArea, Area(destination), 1e-2f);
        EXPECT_NEAR(sourceArea, Area(source), 1e-2f);
    }

    TEST(UINineSliceTest, ASmallDestinationShrinksBothBordersOfAnAxisTogether) {
        // Borders of 4 + 4 on an 8-wide source, drawn 4 wide: each border gets 2, nothing overlaps and the middle is gone
        const auto pieces = UINineSlice::Compute({0, 0, 8, 8}, {4, 0, 4, 0}, {0, 0, 4, 8});

        ExpectRect(pieces[3].destination, 0, 0, 2, 8);
        ExpectRect(pieces[5].destination, 2, 0, 2, 8);
        EXPECT_TRUE(pieces[4].destination.IsEmpty());
        EXPECT_TRUE(pieces[4].source.IsEmpty());
    }

    TEST(UINineSliceTest, AZeroBorderLeavesOnlyTheCenter) {
        const auto pieces = UINineSlice::Compute({0, 0, 16, 16}, {}, {10, 20, 50, 30});

        for (size_t i = 0; i < pieces.size(); ++i) {
            if (i == 4) {
                ExpectRect(pieces[i].destination, 10, 20, 50, 30);
                ExpectRect(pieces[i].source, 0, 0, 16, 16);
            } else {
                EXPECT_TRUE(pieces[i].destination.IsEmpty()) << i;
            }
        }
    }

    TEST(UINineSliceTest, ABorderLargerThanTheSourceIsClamped) {
        const auto pieces = UINineSlice::Compute({0, 0, 8, 8}, {20, 20, 20, 20}, {0, 0, 80, 80});

        for (const UINineSlicePiece& piece : pieces) {
            EXPECT_GE(piece.source.w, 0.0f);
            EXPECT_GE(piece.source.h, 0.0f);
            EXPECT_LE(piece.source.x + piece.source.w, 8.0f + kTolerance);
        }
    }

}  // namespace BECore::Tests
