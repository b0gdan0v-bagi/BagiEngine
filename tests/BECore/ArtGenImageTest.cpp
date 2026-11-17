#include <Common/Printers.h>
#include <Widgets/ArtGen/ArtGenImage.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        struct Rgba {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uint8_t a = 0;
        };

        ArtGenPixels MakePixels(uint32_t width, uint32_t height, bool hasAlpha = true) {
            ArtGenPixels pixels;
            pixels.width = width;
            pixels.height = height;
            pixels.hasAlpha = hasAlpha;
            pixels.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
            return pixels;
        }

        void SetPixel(ArtGenPixels& pixels, uint32_t x, uint32_t y, Rgba color) {
            uint8_t* at = pixels.rgba.data() + (static_cast<size_t>(y) * pixels.width + x) * 4;
            at[0] = color.r;
            at[1] = color.g;
            at[2] = color.b;
            at[3] = color.a;
        }

        Rgba GetPixel(const ArtGenPixels& pixels, uint32_t x, uint32_t y) {
            const uint8_t* at = pixels.rgba.data() + (static_cast<size_t>(y) * pixels.width + x) * 4;
            return Rgba{at[0], at[1], at[2], at[3]};
        }

        void FillRect(ArtGenPixels& pixels, ArtGenIntRect rect, Rgba color) {
            for (int32_t y = rect.y; y < rect.y + rect.h; ++y) {
                for (int32_t x = rect.x; x < rect.x + rect.w; ++x) {
                    SetPixel(pixels, static_cast<uint32_t>(x), static_cast<uint32_t>(y), color);
                }
            }
        }

        constexpr Rgba kOpaqueRed{255, 0, 0, 255};

        ArtGenImageSettings Settings(bool trim, int32_t padding, int32_t maxSize) {
            ArtGenImageSettings settings;
            settings.trim = trim;
            settings.padding = padding;
            settings.maxSize = maxSize;
            return settings;
        }

        ArtGenImageFrame FrameOf(Span<const ArtGenIntRect> bounds, int32_t width, int32_t height, const ArtGenImageSettings& settings) {
            const auto frame = MakeArtGenFrame(bounds, width, height, settings);
            EXPECT_TRUE(frame.has_value()) << (frame ? "" : frame.error().c_str());
            return frame.value_or(ArtGenImageFrame{});
        }

        ArtGenImageFrame FrameOfOne(ArtGenIntRect box, int32_t width, int32_t height, const ArtGenImageSettings& settings) {
            return FrameOf(Span<const ArtGenIntRect>(&box, 1), width, height, settings);
        }

    }  // namespace

    TEST(ArtGenImageTest, TheDrawnBoundsAreTheBoxOfThePixelsAboveTheCutoff) {
        ArtGenPixels pixels = MakePixels(20, 10);
        FillRect(pixels, {4, 2, 6, 3}, kOpaqueRed);
        const auto bounds = DrawnBounds(pixels);
        ASSERT_TRUE(bounds.has_value());
        EXPECT_EQ(*bounds, (ArtGenIntRect{4, 2, 6, 3}));
    }

    TEST(ArtGenImageTest, AnImageWithNothingDrawnHasNoBounds) {
        EXPECT_FALSE(DrawnBounds(MakePixels(8, 8)).has_value());
        EXPECT_FALSE(DrawnBounds(MakePixels(0, 0)).has_value());
    }

    TEST(ArtGenImageTest, TheCutoffEdgeIsExclusive) {
        ArtGenPixels pixels = MakePixels(8, 8);
        SetPixel(pixels, 1, 1, Rgba{255, 255, 255, kArtGenAlphaCutoff});
        EXPECT_FALSE(DrawnBounds(pixels).has_value());
        SetPixel(pixels, 5, 6, Rgba{255, 255, 255, kArtGenAlphaCutoff + 1});
        const auto bounds = DrawnBounds(pixels);
        ASSERT_TRUE(bounds.has_value());
        EXPECT_EQ(*bounds, (ArtGenIntRect{5, 6, 1, 1}));
    }

    TEST(ArtGenImageTest, AnImageWithoutAlphaIsAllDrawn) {
        const ArtGenPixels pixels = MakePixels(12, 7, false);
        const auto bounds = DrawnBounds(pixels);
        ASSERT_TRUE(bounds.has_value());
        EXPECT_EQ(*bounds, (ArtGenIntRect{0, 0, 12, 7}));
    }

    TEST(ArtGenImageTest, ABufferThatDoesNotMatchItsSizeHasNoBounds) {
        ArtGenPixels pixels = MakePixels(4, 4);
        pixels.rgba.pop_back();
        EXPECT_FALSE(DrawnBounds(pixels).has_value());
    }

    TEST(ArtGenImageTest, TheFrameOfOneBoxIsGrownByThePaddingOfItsLongerSide) {
        // Longer side 100, 10 percent: 10 pixels on every side
        const ArtGenImageFrame frame = FrameOfOne({100, 200, 100, 40}, 1000, 1000, Settings(true, 10, 0));
        EXPECT_EQ(frame, (ArtGenImageFrame{90, 190, 120, 60, 120, 60}));
    }

    TEST(ArtGenImageTest, ThePaddingIsClampedAtTheImageEdge) {
        const ArtGenImageFrame frame = FrameOfOne({2, 3, 100, 100}, 110, 108, Settings(true, 10, 0));
        EXPECT_EQ(frame, (ArtGenImageFrame{0, 0, 110, 108, 110, 108}));
    }

    TEST(ArtGenImageTest, TheFrameOfSeveralBoxesIsTheirUnionWithThePaddingOfTheUnion) {
        const Array<ArtGenIntRect, 3> bounds{ArtGenIntRect{300, 300, 50, 50}, ArtGenIntRect{400, 320, 20, 100}, ArtGenIntRect{280, 380, 30, 30}};
        // The union is x 280..420, y 300..420: 140 x 120, so 5 percent is 7
        const ArtGenImageFrame frame = FrameOf(bounds, 1024, 1024, Settings(true, 5, 0));
        EXPECT_EQ(frame, (ArtGenImageFrame{273, 293, 154, 134, 154, 134}));
    }

    TEST(ArtGenImageTest, WithoutTrimTheFrameIsTheWholeImage) {
        const ArtGenImageFrame frame = FrameOfOne({100, 100, 10, 10}, 640, 480, Settings(false, 10, 0));
        EXPECT_EQ(frame, (ArtGenImageFrame{0, 0, 640, 480, 640, 480}));
    }

    TEST(ArtGenImageTest, AFrameNeedsBoundsInsideTheImage) {
        EXPECT_FALSE(MakeArtGenFrame({}, 100, 100, ArtGenImageSettings{}).has_value());
        const ArtGenIntRect outside{90, 0, 20, 10};
        const auto frame = MakeArtGenFrame(Span<const ArtGenIntRect>(&outside, 1), 100, 100, ArtGenImageSettings{});
        EXPECT_FALSE(frame.has_value());
        const ArtGenIntRect empty{0, 0, 0, 5};
        EXPECT_FALSE(MakeArtGenFrame(Span<const ArtGenIntRect>(&empty, 1), 100, 100, ArtGenImageSettings{}).has_value());
    }

    TEST(ArtGenImageTest, AMaxSizeOfZeroOrBeyondTheFrameKeepsTheScaleAtOne) {
        const ArtGenIntRect box{10, 10, 300, 200};
        const ArtGenImageFrame none = FrameOfOne(box, 1024, 1024, Settings(true, 0, 0));
        EXPECT_EQ(none.outWidth, 300);
        EXPECT_EQ(none.outHeight, 200);
        const ArtGenImageFrame big = FrameOfOne(box, 1024, 1024, Settings(true, 0, 4096));
        EXPECT_EQ(big.outWidth, 300);
        EXPECT_EQ(big.outHeight, 200);
        const ArtGenImageFrame exact = FrameOfOne(box, 1024, 1024, Settings(true, 0, 300));
        EXPECT_EQ(exact.outWidth, 300);
        EXPECT_EQ(exact.outHeight, 200);
    }

    TEST(ArtGenImageTest, AMaxSizeBelowTheFrameScalesItsLongerSideToIt) {
        const ArtGenImageFrame frame = FrameOfOne({0, 0, 730, 612}, 1024, 1024, Settings(true, 0, 256));
        EXPECT_EQ(frame.outWidth, 256);
        EXPECT_EQ(frame.outHeight, 215);  // 612 * 256 / 730 = 214.6
    }

    TEST(ArtGenImageTest, TheShorterSideNeverRoundsToZero) {
        const ArtGenImageFrame frame = FrameOfOne({0, 0, 1000, 3}, 1024, 1024, Settings(true, 0, 16));
        EXPECT_EQ(frame.outWidth, 16);
        EXPECT_EQ(frame.outHeight, 1);
    }

    TEST(ArtGenImageTest, TwoImagesInOneFrameComeOutTheSameSizeAndKeepTheirRatio) {
        // A king 400 high and a pawn 200 high, both 100 wide, drawn on one canvas size
        ArtGenPixels king = MakePixels(1024, 1024);
        FillRect(king, {400, 300, 100, 400}, kOpaqueRed);
        ArtGenPixels pawn = MakePixels(1024, 1024);
        FillRect(pawn, {400, 500, 100, 200}, kOpaqueRed);

        const Array<ArtGenIntRect, 2> bounds{*DrawnBounds(king), *DrawnBounds(pawn)};
        const ArtGenImageFrame frame = FrameOf(bounds, 1024, 1024, Settings(true, 0, 100));
        EXPECT_EQ(frame, (ArtGenImageFrame{400, 300, 100, 400, 25, 100}));

        const auto kingOut = ProcessArtGenImage(king, frame);
        const auto pawnOut = ProcessArtGenImage(pawn, frame);
        ASSERT_TRUE(kingOut.has_value());
        ASSERT_TRUE(pawnOut.has_value());
        EXPECT_EQ(kingOut->width, pawnOut->width);
        EXPECT_EQ(kingOut->height, pawnOut->height);

        const auto kingBox = DrawnBounds(*kingOut);
        const auto pawnBox = DrawnBounds(*pawnOut);
        ASSERT_TRUE(kingBox.has_value());
        ASSERT_TRUE(pawnBox.has_value());
        EXPECT_EQ(kingBox->h, 100);
        EXPECT_EQ(pawnBox->h, 50);
        // The pawn stays at the bottom where it was drawn
        EXPECT_EQ(pawnBox->y, 50);
    }

    TEST(ArtGenImageTest, ASolidSquareComesOutTheSizeTheFrameSays) {
        ArtGenPixels pixels = MakePixels(64, 64);
        FillRect(pixels, {0, 0, 64, 64}, Rgba{10, 200, 30, 255});
        const ArtGenImageFrame frame = FrameOfOne({0, 0, 64, 64}, 64, 64, Settings(true, 0, 16));
        const auto out = ProcessArtGenImage(pixels, frame);
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(out->width, 16u);
        EXPECT_EQ(out->height, 16u);
        for (uint32_t y = 0; y < out->height; ++y) {
            for (uint32_t x = 0; x < out->width; ++x) {
                const Rgba pixel = GetPixel(*out, x, y);
                EXPECT_EQ(pixel.r, 10);
                EXPECT_EQ(pixel.g, 200);
                EXPECT_EQ(pixel.b, 30);
                EXPECT_EQ(pixel.a, 255);
            }
        }
    }

    TEST(ArtGenImageTest, ADownscaledHalfTransparentEdgeStaysRed) {
        // Left half: half-transparent red; right half: transparent with a black colour a plain average would mix in
        ArtGenPixels pixels = MakePixels(32, 32);
        FillRect(pixels, {0, 0, 16, 32}, Rgba{255, 0, 0, 128});
        FillRect(pixels, {16, 0, 16, 32}, Rgba{0, 0, 0, 0});
        const ArtGenImageFrame frame{0, 0, 32, 32, 8, 8};
        const auto out = ProcessArtGenImage(pixels, frame);
        ASSERT_TRUE(out.has_value());
        const Rgba solid = GetPixel(*out, 1, 3);
        EXPECT_EQ(solid.r, 255);
        EXPECT_EQ(solid.a, 128);
        // Every output pixel is wholly in one half here, so none mixes; shift the edge into a pixel
        ArtGenPixels edge = MakePixels(32, 32);
        FillRect(edge, {0, 0, 18, 32}, Rgba{255, 0, 0, 128});
        const auto edgeOut = ProcessArtGenImage(edge, frame);
        ASSERT_TRUE(edgeOut.has_value());
        const Rgba mixed = GetPixel(*edgeOut, 4, 3);
        EXPECT_EQ(mixed.r, 255);
        EXPECT_EQ(mixed.g, 0);
        EXPECT_EQ(mixed.b, 0);
        EXPECT_NEAR(mixed.a, 64, 1);
    }

    TEST(ArtGenImageTest, AScaleOfOneIsAPureCropPixelForPixel) {
        ArtGenPixels pixels = MakePixels(16, 16);
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t x = 0; x < 16; ++x) {
                SetPixel(pixels, x, y, Rgba{static_cast<uint8_t>(x * 16), static_cast<uint8_t>(y * 16), static_cast<uint8_t>(x + y), static_cast<uint8_t>(100 + x)});
            }
        }
        const ArtGenImageFrame frame{3, 5, 8, 6, 8, 6};
        const auto out = ProcessArtGenImage(pixels, frame);
        ASSERT_TRUE(out.has_value());
        ASSERT_EQ(out->width, 8u);
        ASSERT_EQ(out->height, 6u);
        for (uint32_t y = 0; y < 6; ++y) {
            for (uint32_t x = 0; x < 8; ++x) {
                const Rgba source = GetPixel(pixels, x + 3, y + 5);
                const Rgba got = GetPixel(*out, x, y);
                EXPECT_EQ(got.r, source.r);
                EXPECT_EQ(got.g, source.g);
                EXPECT_EQ(got.b, source.b);
                EXPECT_EQ(got.a, source.a);
            }
        }
    }

    TEST(ArtGenImageTest, AFrameOutsideTheImageOrBiggerOnOutputIsRefused) {
        const ArtGenPixels pixels = MakePixels(16, 16);
        EXPECT_FALSE(ProcessArtGenImage(pixels, ArtGenImageFrame{10, 0, 10, 10, 10, 10}).has_value());
        EXPECT_FALSE(ProcessArtGenImage(pixels, ArtGenImageFrame{0, 0, 8, 8, 16, 16}).has_value());
        EXPECT_FALSE(ProcessArtGenImage(pixels, ArtGenImageFrame{0, 0, 8, 8, 0, 4}).has_value());
    }

    TEST(ArtGenImageTest, AFrameContainsABoxOnlyWhollyInside) {
        const ArtGenImageFrame frame{10, 10, 100, 50, 100, 50};
        EXPECT_TRUE(FrameContains(frame, {10, 10, 100, 50}));
        EXPECT_TRUE(FrameContains(frame, {20, 20, 10, 10}));
        EXPECT_FALSE(FrameContains(frame, {9, 10, 10, 10}));
        EXPECT_FALSE(FrameContains(frame, {100, 50, 11, 10}));
    }

    TEST(ArtGenImageTest, TheDefaultsAreTrimPaddingTwoAndMaxSize256) {
        const ArtGenImageSettings settings;
        EXPECT_TRUE(settings.trim);
        EXPECT_EQ(settings.padding, 2);
        EXPECT_EQ(settings.maxSize, 256);
        EXPECT_FALSE(settings.IsIdentity());
    }

    TEST(ArtGenImageTest, NoTrimAndNoMaxSizeIsTheIdentity) {
        EXPECT_TRUE(Settings(false, 7, 0).IsIdentity());
        EXPECT_FALSE(Settings(true, 0, 0).IsIdentity());
        EXPECT_FALSE(Settings(false, 0, 64).IsIdentity());
    }

    TEST(ArtGenImageTest, ASettingsJsonWithMissingKeysKeepsTheDefaults) {
        const auto empty = ArtGenImageSettingsFromJson(Json::object());
        ASSERT_TRUE(empty.has_value());
        EXPECT_EQ(*empty, ArtGenImageSettings{});
        const auto none = ArtGenImageSettingsFromJson(Json());
        ASSERT_TRUE(none.has_value());
        EXPECT_EQ(*none, ArtGenImageSettings{});

        const auto partial = ArtGenImageSettingsFromJson(*ParseJson(StringView("{\"padding\": 5}")));
        ASSERT_TRUE(partial.has_value());
        EXPECT_EQ(partial->padding, 5);
        EXPECT_TRUE(partial->trim);
        EXPECT_EQ(partial->maxSize, 256);
    }

    TEST(ArtGenImageTest, ASettingsJsonRoundTrips) {
        const ArtGenImageSettings settings = Settings(false, 12, 1024);
        const auto back = ArtGenImageSettingsFromJson(ArtGenImageSettingsToJson(settings));
        ASSERT_TRUE(back.has_value());
        EXPECT_EQ(*back, settings);
    }

    TEST(ArtGenImageTest, ABadSettingsJsonNamesTheKey) {
        auto expectError = [](StringView text, StringView key) {
            const auto parsed = ParseJson(text);
            ASSERT_TRUE(parsed.has_value());
            const auto settings = ArtGenImageSettingsFromJson(*parsed);
            ASSERT_FALSE(settings.has_value()) << text.data();
            EXPECT_NE(settings.error().find(String(key.data(), key.size())), String::npos) << settings.error().c_str();
        };
        expectError("{\"trim\": 1}", "trim");
        expectError("{\"padding\": \"2\"}", "padding");
        expectError("{\"padding\": 1.5}", "padding");
        expectError("{\"padding\": 26}", "padding");
        expectError("{\"padding\": -1}", "padding");
        expectError("{\"maxSize\": 15}", "maxSize");
        expectError("{\"maxSize\": 4097}", "maxSize");
        expectError("{\"maxSize\": null}", "maxSize");
        const auto notObject = ArtGenImageSettingsFromJson(*ParseJson(StringView("[1]")));
        EXPECT_FALSE(notObject.has_value());
    }

    TEST(ArtGenImageTest, MaxSizeZeroAndTheLimitsAreAccepted) {
        EXPECT_TRUE(ArtGenImageSettingsFromJson(*ParseJson(StringView("{\"maxSize\": 0, \"padding\": 0}"))).has_value());
        EXPECT_TRUE(ArtGenImageSettingsFromJson(*ParseJson(StringView("{\"maxSize\": 16, \"padding\": 25}"))).has_value());
        EXPECT_TRUE(ArtGenImageSettingsFromJson(*ParseJson(StringView("{\"maxSize\": 4096}"))).has_value());
    }

    TEST(ArtGenImageTest, AFrameJsonRoundTripsAndABadOneIsRefused) {
        const ArtGenImageFrame frame{4, 5, 100, 80, 50, 40};
        const auto back = ArtGenImageFrameFromJson(ArtGenImageFrameToJson(frame));
        ASSERT_TRUE(back.has_value());
        EXPECT_EQ(*back, frame);

        EXPECT_FALSE(ArtGenImageFrameFromJson(*ParseJson(StringView("{\"x\": 0, \"y\": 0, \"w\": 10, \"h\": 10, \"outWidth\": 10}"))).has_value());
        EXPECT_FALSE(ArtGenImageFrameFromJson(*ParseJson(StringView("{\"x\": -1, \"y\": 0, \"w\": 10, \"h\": 10, \"outWidth\": 10, \"outHeight\": 10}"))).has_value());
        EXPECT_FALSE(ArtGenImageFrameFromJson(*ParseJson(StringView("{\"x\": 0, \"y\": 0, \"w\": 10, \"h\": 10, \"outWidth\": 20, \"outHeight\": 10}"))).has_value());
        EXPECT_FALSE(ArtGenImageFrameFromJson(Json::array()).has_value());
    }

}  // namespace BECore::Tests
