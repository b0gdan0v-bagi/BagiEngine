#include <Stress/SpriteField.h>
#include <gtest/gtest.h>

/**
 * Coverage of SpriteField: the placement of a sprite is a pure function of its index, the count and the settings, and the atlas pixels
 * are what the scene feeds to IRenderer::CreateTexture.
 */
namespace BECore::Stress::Tests {

    namespace {

        constexpr uint32_t kCount = 2000;
        const FieldView kView{1920.0f, 1080.0f};

        StressSettings Settings(bool interleave) {
            StressSettings settings;
            settings.interleave = interleave;
            return settings;
        }

        size_t CountChanges(const StressSettings& settings, uint32_t count, bool atlas) {
            size_t changes = 0;
            SpritePlacement previous = Place(0, count, settings, kView);
            for (uint32_t i = 1; i < count; ++i) {
                const SpritePlacement current = Place(i, count, settings, kView);
                changes += atlas ? (current.atlas != previous.atlas) : (current.blend != previous.blend);
                previous = current;
            }
            return changes;
        }

    }  // namespace

    TEST(SpriteFieldTest, TheSameIndexGivesTheSamePlacement) {
        const StressSettings settings = Settings(false);
        for (uint32_t i : {0u, 1u, 77u, 1999u}) {
            const SpritePlacement a = Place(i, kCount, settings, kView);
            const SpritePlacement b = Place(i, kCount, settings, kView);
            EXPECT_EQ(a.x, b.x);
            EXPECT_EQ(a.y, b.y);
            EXPECT_EQ(a.size, b.size);
            EXPECT_EQ(a.spin, b.spin);
            EXPECT_EQ(a.atlas, b.atlas);
            EXPECT_EQ(a.srcRect.x, b.srcRect.x);
            EXPECT_EQ(a.blend, b.blend);
        }
    }

    /// The numbers of sprite 0 are pinned on purpose: the hash is our own so that they are the same in every standard library
    TEST(SpriteFieldTest, TheHashIsFixed) {
        const SpritePlacement a = Place(0, kCount, Settings(false), kView);
        EXPECT_EQ(a.x, 1077.0f);
        EXPECT_EQ(a.y, 627.0f);
        EXPECT_EQ(a.size, 19.0f);
        EXPECT_NEAR(a.spin, -133.5494f, 0.001f);
        EXPECT_EQ(a.atlas, 0u);
        EXPECT_EQ(a.srcRect.x, 64.0f);
        EXPECT_EQ(a.srcRect.y, 64.0f);
    }

    TEST(SpriteFieldTest, PositionSizeAndSpinDoNotDependOnTheCount) {
        const StressSettings settings = Settings(false);
        for (uint32_t i : {0u, 5u, 99u}) {
            const SpritePlacement small = Place(i, 100, settings, kView);
            const SpritePlacement large = Place(i, 5000, settings, kView);
            EXPECT_EQ(small.x, large.x) << i;
            EXPECT_EQ(small.y, large.y) << i;
            EXPECT_EQ(small.size, large.size) << i;
            EXPECT_EQ(small.spin, large.spin) << i;
            EXPECT_EQ(small.srcRect.x, large.srcRect.x) << i;
            EXPECT_EQ(small.srcRect.y, large.srcRect.y) << i;
        }
    }

    TEST(SpriteFieldTest, EveryPlacementIsInsideTheViewAndItsAtlas) {
        const StressSettings settings = Settings(false);
        const float atlasSide = static_cast<float>(kAtlasCellsPerSide * kAtlasCellSize);
        bool positive = false;
        bool negative = false;
        for (uint32_t i = 0; i < kCount; ++i) {
            const SpritePlacement p = Place(i, kCount, settings, kView);
            EXPECT_GE(p.size, kSpriteMinSize);
            EXPECT_LE(p.size, kSpriteMaxSize);
            EXPECT_GE(p.x, 0.0f);
            EXPECT_GE(p.y, 0.0f);
            EXPECT_LE(p.x + p.size, kView.width);
            EXPECT_LE(p.y + p.size, kView.height);
            EXPECT_GE(std::abs(p.spin), kSpriteMinSpin);
            EXPECT_LE(std::abs(p.spin), kSpriteMaxSpin);
            positive |= p.spin > 0.0f;
            negative |= p.spin < 0.0f;
            EXPECT_LT(p.atlas, settings.atlases);
            EXPECT_GE(p.srcRect.x, 0.0f);
            EXPECT_GE(p.srcRect.y, 0.0f);
            EXPECT_LE(p.srcRect.x + p.srcRect.w, atlasSide);
            EXPECT_LE(p.srcRect.y + p.srcRect.h, atlasSide);
        }
        EXPECT_TRUE(positive);
        EXPECT_TRUE(negative);
    }

    TEST(SpriteFieldTest, EveryBlendModeAndEveryAtlasOccursInRunsAndInterleaved) {
        for (bool interleave : {false, true}) {
            const StressSettings settings = Settings(interleave);
            bool atlases[kMaxAtlases] = {};
            bool blends[3] = {};
            for (uint32_t i = 0; i < kCount; ++i) {
                const SpritePlacement p = Place(i, kCount, settings, kView);
                atlases[p.atlas] = true;
                blends[static_cast<size_t>(p.blend)] = true;
            }
            for (uint32_t a = 0; a < settings.atlases; ++a) {
                EXPECT_TRUE(atlases[a]) << "atlas " << a << " interleave " << interleave;
            }
            for (bool seen : blends) {
                EXPECT_TRUE(seen) << "interleave " << interleave;
            }
        }
    }

    TEST(SpriteFieldTest, RunsChangeAtlasAndBlendRarelyAndInterleaveOnEverySprite) {
        const StressSettings runs = Settings(false);
        EXPECT_LE(CountChanges(runs, kCount, true), runs.atlases);
        EXPECT_LE(CountChanges(runs, kCount, false), kCount / kBlendRunLength + 1);

        const StressSettings interleaved = Settings(true);
        EXPECT_EQ(CountChanges(interleaved, kCount, true), kCount - 1);
        EXPECT_EQ(CountChanges(interleaved, kCount, false), kCount - 1);
    }

    TEST(SpriteFieldTest, AFewerAtlasesStaysInRange) {
        StressSettings settings = Settings(false);
        settings.atlases = 1;
        for (uint32_t i = 0; i < 300; ++i) {
            EXPECT_EQ(Place(i, 300, settings, kView).atlas, 0u);
        }
        EXPECT_EQ(AtlasRunLength(0, 4), 1u);
        EXPECT_EQ(AtlasRunLength(3, 4), 1u);
        EXPECT_EQ(AtlasRunLength(2000, 4), 500u);
    }

    TEST(SpriteFieldTest, AtlasPixelsHaveTheSizeAndTheCellsDiffer) {
        const uint32_t side = kAtlasCellsPerSide * kAtlasCellSize;
        const Vector<uint8_t> pixels = MakeAtlasPixels(0, kAtlasCellsPerSide, kAtlasCellSize);
        ASSERT_EQ(pixels.size(), static_cast<size_t>(side) * side * 4);

        // The flat colour of a cell is read at a point that is outside its marker and its corners
        auto colorOf = [&](uint32_t atlas, uint32_t cell) {
            const Vector<uint8_t> atlasPixels = atlas == 0 ? pixels : MakeAtlasPixels(atlas, kAtlasCellsPerSide, kAtlasCellSize);
            const uint32_t x = (cell % kAtlasCellsPerSide) * kAtlasCellSize + 8;
            const uint32_t y = (cell / kAtlasCellsPerSide) * kAtlasCellSize + 8;
            const size_t offset = (static_cast<size_t>(y) * side + x) * 4;
            return std::array<uint8_t, 4>{atlasPixels[offset], atlasPixels[offset + 1], atlasPixels[offset + 2], atlasPixels[offset + 3]};
        };
        for (uint32_t a = 0; a < kAtlasCellsPerSide * kAtlasCellsPerSide; ++a) {
            for (uint32_t b = a + 1; b < kAtlasCellsPerSide * kAtlasCellsPerSide; ++b) {
                EXPECT_NE(colorOf(0, a), colorOf(0, b)) << "cells " << a << " and " << b;
            }
        }
        EXPECT_NE(colorOf(0, 0), colorOf(1, 0)) << "the atlases differ";
    }

    TEST(SpriteFieldTest, AtlasCornersAreTranslucentAndTheMiddleOfACellIsOpaque) {
        const uint32_t side = kAtlasCellsPerSide * kAtlasCellSize;
        const Vector<uint8_t> pixels = MakeAtlasPixels(2, kAtlasCellsPerSide, kAtlasCellSize);
        auto alphaAt = [&](uint32_t x, uint32_t y) { return pixels[(static_cast<size_t>(y) * side + x) * 4 + 3]; };
        for (uint32_t cell = 0; cell < kAtlasCellsPerSide * kAtlasCellsPerSide; ++cell) {
            const uint32_t left = (cell % kAtlasCellsPerSide) * kAtlasCellSize;
            const uint32_t top = (cell / kAtlasCellsPerSide) * kAtlasCellSize;
            EXPECT_LT(alphaAt(left, top), 255) << cell;
            EXPECT_LT(alphaAt(left + kAtlasCellSize - 1, top + kAtlasCellSize - 1), 255) << cell;
            EXPECT_EQ(alphaAt(left + kAtlasCellSize / 2, top + kAtlasCellSize / 2), 255) << cell;
        }
    }

    TEST(SpriteFieldTest, TheSamePixelsComeTwice) {
        EXPECT_EQ(MakeAtlasPixels(3, kAtlasCellsPerSide, kAtlasCellSize), MakeAtlasPixels(3, kAtlasCellsPerSide, kAtlasCellSize));
    }

}  // namespace BECore::Stress::Tests
