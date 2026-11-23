#include <BECore/UI/UIBoxTexture.h>
#include <Common/EngineEnvironment.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kSideTolerance = 1;  // one step of an 8-bit channel: the stretched middle and the whole box differ only by the rounding of the same float

        /// A skin with every layer: the rim of a shadow and a glow, a gradient, a border and a highlight
        UIBoxStyle SkinnedStyle(UIGradientDirection direction) {
            UIBoxStyle style;
            style.cornerRadius = UICorners::Uniform(10.0f);
            style.borderWidth = 2.0f;
            style.borderColor = Color{255, 255, 255, 200};
            style.gradientDirection = direction;
            style.shadow = {2.0f, 6.0f, 8.0f, 1.0f, Color{0, 0, 0, 160}};
            style.glow = {0.0f, 0.0f, 5.0f, 0.0f, Color{255, 215, 0, 80}};
            style.highlight = Color{255, 255, 255, 120};
            return style;
        }

        const Color kFill{200, 40, 40, 255};
        const Color kEnd{40, 40, 200, 255};

        UIBoxTextureLayout LayoutOf(const UIBoxStyle& style, float scale, int width, int height) {
            const UIBoxLook look = UIBoxTexture::MakeLook(style, kFill, kEnd, scale, static_cast<float>(width) / scale, static_cast<float>(height) / scale);
            const auto layout = UIBoxTexture::MakeLayout(look, width, height);
            EXPECT_TRUE(layout.has_value());
            return layout.value_or(UIBoxTextureLayout{});
        }

        /// The box drawn as the blits would draw it (nearest, one to one, the stretched middle copied wide), into an image that covers the margins
        Vector<uint8_t> Assemble(const UIBoxTextureLayout& layout, const Vector<uint8_t>& texture) {
            const int width = layout.x.size + layout.x.marginBefore + layout.x.marginAfter;
            const int height = layout.y.size + layout.y.marginBefore + layout.y.marginAfter;
            Vector<uint8_t> image(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
            Array<UIBoxTexturePiece, UIBoxTexture::kMaxPieces> pieces;
            const size_t count = UIBoxTexture::MakePieces(layout, pieces);
            for (size_t i = 0; i < count; ++i) {
                const Rect& source = pieces[i].source;
                const Rect& destination = pieces[i].destination;
                for (int row = 0; row < static_cast<int>(destination.h); ++row) {
                    for (int column = 0; column < static_cast<int>(destination.w); ++column) {
                        const int sourceColumn = static_cast<int>(source.x) + column * static_cast<int>(source.w) / static_cast<int>(destination.w);
                        const int sourceRow = static_cast<int>(source.y) + row * static_cast<int>(source.h) / static_cast<int>(destination.h);
                        const int imageColumn = static_cast<int>(destination.x) + column + layout.x.marginBefore;
                        const int imageRow = static_cast<int>(destination.y) + row + layout.y.marginBefore;
                        for (int channel = 0; channel < 4; ++channel) {
                            image[(static_cast<size_t>(imageRow) * width + imageColumn) * 4 + channel] = texture[(static_cast<size_t>(sourceRow) * layout.Width() + sourceColumn) * 4 + channel];
                        }
                    }
                }
            }
            return image;
        }

        int LargestDifference(const Vector<uint8_t>& a, const Vector<uint8_t>& b) {
            int largest = 0;
            for (size_t i = 0; i < a.size(); ++i) {
                largest = std::max(largest, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
            }
            return largest;
        }

        struct Case {
            UIGradientDirection direction;
            float scale;
            int width;
            int height;
        };

    }  // namespace

    class UIBoxTextureTest : public ::testing::TestWithParam<Case> {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    // The stretched middle is only right if every layer is really the same along the axis beyond the caps: the box drawn as caps and a middle, stretched, is the box drawn whole
    TEST_P(UIBoxTextureTest, ASlicedBoxIsTheWholeBox) {
        const Case param = GetParam();
        const UIBoxStyle style = SkinnedStyle(param.direction);
        const UIBoxTextureLayout sliced = LayoutOf(style, param.scale, param.width, param.height);
        ASSERT_TRUE(sliced.x.sliced || sliced.y.sliced) << "the case must slice an axis";

        UIBoxTextureLayout whole = sliced;
        whole.x.sliced = false;
        whole.y.sliced = false;

        const Vector<uint8_t> assembled = Assemble(sliced, UIBoxTexture::Render(sliced));
        const Vector<uint8_t> reference = Assemble(whole, UIBoxTexture::Render(whole));
        ASSERT_EQ(assembled.size(), reference.size());
        EXPECT_LE(LargestDifference(assembled, reference), kSideTolerance);
    }

    INSTANTIATE_TEST_SUITE_P(Boxes, UIBoxTextureTest,
                             ::testing::Values(Case{UIGradientDirection::Vertical, 1.0f, 300, 120}, Case{UIGradientDirection::Vertical, 2.0f, 300, 120},
                                               Case{UIGradientDirection::None, 1.0f, 300, 200}, Case{UIGradientDirection::None, 2.0f, 260, 180},
                                               Case{UIGradientDirection::Horizontal, 1.0f, 300, 200}));

    TEST(UIBoxTextureLayoutTest, ABigBoxIsASmallTextureAndASmallBoxIsWhole) {
        const UIBoxStyle style = SkinnedStyle(UIGradientDirection::None);
        const UIBoxTextureLayout big = LayoutOf(style, 1.0f, 1200, 800);
        EXPECT_TRUE(big.x.sliced);
        EXPECT_TRUE(big.y.sliced);
        EXPECT_LT(big.Width() * big.Height(), 200 * 200) << "nine slices, whatever the box";
        EXPECT_EQ(big.key.wholeWidth, 0);
        EXPECT_EQ(big.key.wholeHeight, 0);

        const UIBoxTextureLayout small = LayoutOf(style, 1.0f, 12, 16);
        EXPECT_FALSE(small.x.sliced);
        EXPECT_FALSE(small.y.sliced);
        EXPECT_EQ(small.key.wholeWidth, 12);
        EXPECT_EQ(small.key.wholeHeight, 16);
    }

    TEST(UIBoxTextureLayoutTest, AGradientAxisIsWholeAndKeepsItsSizeInTheKey) {
        const UIBoxStyle style = SkinnedStyle(UIGradientDirection::Vertical);
        const UIBoxTextureLayout layout = LayoutOf(style, 1.0f, 600, 400);
        EXPECT_TRUE(layout.x.sliced);
        EXPECT_FALSE(layout.y.sliced);
        EXPECT_EQ(layout.key.wholeHeight, 400);
        EXPECT_FALSE(layout.key == LayoutOf(style, 1.0f, 600, 401).key);
        EXPECT_TRUE(layout.key == LayoutOf(style, 1.0f, 900, 400).key) << "the width is stretched: boxes of one height share the texture";
    }

    TEST(UIBoxTextureLayoutTest, TheKeyFollowsTheLookOnly) {
        UIBoxStyle style = SkinnedStyle(UIGradientDirection::None);
        const UIBoxLook look = UIBoxTexture::MakeLook(style, kFill, kEnd, 1.0f, 300.0f, 200.0f);
        EXPECT_TRUE(look == UIBoxTexture::MakeLook(style, kFill, kEnd, 1.0f, 500.0f, 90.0f));
        EXPECT_FALSE(look == UIBoxTexture::MakeLook(style, Color{201, 40, 40, 255}, kEnd, 1.0f, 300.0f, 200.0f));
        EXPECT_FALSE(look == UIBoxTexture::MakeLook(style, kFill, kEnd, 2.0f, 300.0f, 200.0f));
        style.shadow.blur = 9.0f;
        EXPECT_FALSE(look == UIBoxTexture::MakeLook(style, kFill, kEnd, 1.0f, 300.0f, 200.0f));
    }

    TEST(UIBoxTextureLayoutTest, ThePiecesTileTheBoxAndItsMargins) {
        const UIBoxTextureLayout layout = LayoutOf(SkinnedStyle(UIGradientDirection::None), 1.0f, 300, 200);
        Array<UIBoxTexturePiece, UIBoxTexture::kMaxPieces> pieces;
        ASSERT_EQ(UIBoxTexture::MakePieces(layout, pieces), 9u);
        float area = 0.0f;
        for (size_t i = 0; i < 9; ++i) {
            area += pieces[i].destination.w * pieces[i].destination.h;
        }
        EXPECT_FLOAT_EQ(area, static_cast<float>((300 + layout.x.marginBefore + layout.x.marginAfter) * (200 + layout.y.marginBefore + layout.y.marginAfter)));
    }

    TEST(UIBoxTextureLayoutTest, ANumberOutOfRangeIsNoTexture) {
        const UIBoxStyle style = SkinnedStyle(UIGradientDirection::None);
        EXPECT_FALSE(UIBoxTexture::MakeLayout(UIBoxTexture::MakeLook(style, kFill, kEnd, 1.0f, 100.0f, 100.0f), 0, 10).has_value());
        EXPECT_FALSE(UIBoxTexture::MakeLayout(UIBoxTexture::MakeLook(style, kFill, kEnd, 0.0f, 100.0f, 100.0f), 100, 100).has_value());
        EXPECT_FALSE(UIBoxTexture::MakeLayout(UIBoxTexture::MakeLook(style, kFill, kEnd, 1.0f, 100.0f, 100.0f), UIBoxTexture::kMaxSide + 1, 100).has_value());
        const UIBoxStyle graded = SkinnedStyle(UIGradientDirection::Vertical);
        EXPECT_FALSE(UIBoxTexture::MakeLayout(UIBoxTexture::MakeLook(graded, kFill, kEnd, 1.0f, 100.0f, 4090.0f), 3000, 4090).has_value()) << "a whole axis that would need too many pixels";
    }

}  // namespace BECore::Tests
