#include <Widgets/TextureEditor/SpriteAtlasPacker.h>
#include <bit>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        PackSettings Settings(int padding, AtlasSizeMode mode, uint32_t maxSide = kDefaultAtlasMaxSide) {
            PackSettings settings;
            settings.padding = padding;
            settings.sizeMode = mode;
            settings.maxSide = maxSide;
            return settings;
        }

        PackedLayout MustPack(const Vector<PackSize>& sizes, const PackSettings& settings) {
            const auto packed = PackSizes(sizes, settings);
            EXPECT_TRUE(packed.has_value()) << (packed ? "" : AtlasPackErrorText(packed.error()).data());
            return packed ? *packed : PackedLayout();
        }

        AtlasPackError MustFail(const Vector<PackSize>& sizes, const PackSettings& settings) {
            const auto packed = PackSizes(sizes, settings);
            EXPECT_FALSE(packed.has_value());
            return packed ? AtlasPackError::NoSprites : packed.error();
        }

        void ExpectAt(const PackedLayout& layout, size_t index, uint32_t x, uint32_t y) {
            ASSERT_LT(index, layout.placements.size());
            EXPECT_EQ(layout.placements[index].x, x) << "sprite " << index;
            EXPECT_EQ(layout.placements[index].y, y) << "sprite " << index;
        }

        bool SameLayout(const PackedLayout& a, const PackedLayout& b) {
            if (a.width != b.width || a.height != b.height || a.placements.size() != b.placements.size()) {
                return false;
            }
            for (size_t i = 0; i < a.placements.size(); ++i) {
                if (a.placements[i].x != b.placements[i].x || a.placements[i].y != b.placements[i].y) {
                    return false;
                }
            }
            return true;
        }

        /// A fixed generator, so a failing set is the same on every machine
        class Lcg {
        public:
            explicit Lcg(uint32_t seed) : _state(seed) {}

            uint32_t Between(uint32_t low, uint32_t high) {
                _state = _state * 1664525U + 1013904223U;
                return low + (_state >> 8) % (high - low + 1);
            }

        private:
            uint32_t _state;
        };

        Vector<PackSize> RandomSizes(Lcg& random) {
            constexpr uint32_t kMaxSide = 64;
            constexpr uint32_t kMaxCount = 40;
            Vector<PackSize> sizes(random.Between(1, kMaxCount));
            for (PackSize& size : sizes) {
                size.width = random.Between(1, kMaxSide);
                size.height = random.Between(1, kMaxSide);
            }
            return sizes;
        }

    }  // namespace

    TEST(SpriteAtlasPacker, OneSpriteSitsAtTheOriginAndPowerOfTwoRoundsBothSides) {
        const Vector<PackSize> sizes = {{10, 20}};
        const PackedLayout rounded = MustPack(sizes, Settings(1, AtlasSizeMode::PowerOfTwo));
        EXPECT_EQ(rounded.width, 16U);
        EXPECT_EQ(rounded.height, 32U);
        ExpectAt(rounded, 0, 0, 0);

        const PackedLayout tight = MustPack(sizes, Settings(1, AtlasSizeMode::Tight));
        EXPECT_EQ(tight.width, 10U);
        EXPECT_EQ(tight.height, 20U);
        ExpectAt(tight, 0, 0, 0);
    }

    TEST(SpriteAtlasPacker, TwoEqualSpritesTieOnAreaAndTheNarrowerAtlasWins) {
        const Vector<PackSize> sizes = {{8, 8}, {8, 8}};
        const PackedLayout flush = MustPack(sizes, Settings(0, AtlasSizeMode::Tight));
        EXPECT_EQ(flush.width, 8U);
        EXPECT_EQ(flush.height, 16U);
        ExpectAt(flush, 0, 0, 0);
        ExpectAt(flush, 1, 0, 8);

        const PackedLayout padded = MustPack(sizes, Settings(2, AtlasSizeMode::Tight));
        EXPECT_EQ(padded.width, 8U);
        EXPECT_EQ(padded.height, 18U);
        ExpectAt(padded, 0, 0, 0);
        ExpectAt(padded, 1, 0, 10);

        const PackedLayout rounded = MustPack(sizes, Settings(2, AtlasSizeMode::PowerOfTwo));
        EXPECT_EQ(rounded.width, 8U);
        EXPECT_EQ(rounded.height, 32U);
    }

    TEST(SpriteAtlasPacker, ShelvesFollowHeightDescendingAndPlacementsKeepInputOrder) {
        // Packing order is A (10 high), C (8), B (6); A and C share the first shelf in an 8 wide atlas, B opens the second
        const Vector<PackSize> sizes = {{4, 10}, {6, 6}, {2, 8}};
        const PackedLayout tight = MustPack(sizes, Settings(0, AtlasSizeMode::Tight));
        EXPECT_EQ(tight.width, 6U);
        EXPECT_EQ(tight.height, 16U);
        ExpectAt(tight, 0, 0, 0);
        ExpectAt(tight, 1, 0, 10);
        ExpectAt(tight, 2, 4, 0);

        const PackedLayout rounded = MustPack(sizes, Settings(0, AtlasSizeMode::PowerOfTwo));
        EXPECT_EQ(rounded.width, 8U);
        EXPECT_EQ(rounded.height, 16U);
        ExpectAt(rounded, 0, 0, 0);
        ExpectAt(rounded, 1, 0, 10);
        ExpectAt(rounded, 2, 4, 0);
    }

    TEST(SpriteAtlasPacker, EqualSizesKeepInputOrder) {
        const Vector<PackSize> sizes = {{5, 5}, {5, 5}, {5, 5}, {5, 5}};
        const PackedLayout layout = MustPack(sizes, Settings(0, AtlasSizeMode::Tight));
        // A 10 x 10 square and a 5 x 20 column have the same area; the narrower one wins
        EXPECT_EQ(layout.width, 5U);
        EXPECT_EQ(layout.height, 20U);
        ExpectAt(layout, 0, 0, 0);
        ExpectAt(layout, 1, 0, 5);
        ExpectAt(layout, 2, 0, 10);
        ExpectAt(layout, 3, 0, 15);
    }

    TEST(SpriteAtlasPacker, PaddingIsNotAddedAtTheAtlasEdges) {
        const Vector<PackSize> sizes = {{4, 4}};
        const PackedLayout layout = MustPack(sizes, Settings(kMaxAtlasPadding, AtlasSizeMode::Tight));
        EXPECT_EQ(layout.width, 4U);
        EXPECT_EQ(layout.height, 4U);
    }

    TEST(SpriteAtlasPacker, EveryRandomSetKeepsTheInvariants) {
        constexpr uint32_t kSets = 400;
        Lcg random(12345);
        for (uint32_t set = 0; set < kSets; ++set) {
            const Vector<PackSize> sizes = RandomSizes(random);
            const int padding = static_cast<int>(random.Between(0, 4));
            const AtlasSizeMode mode = random.Between(0, 1) == 0 ? AtlasSizeMode::PowerOfTwo : AtlasSizeMode::Tight;
            const PackedLayout layout = MustPack(sizes, Settings(padding, mode));
            ASSERT_EQ(layout.placements.size(), sizes.size()) << "set " << set;

            uint32_t extentX = 0;
            uint32_t extentY = 0;
            for (size_t i = 0; i < sizes.size(); ++i) {
                const PackedRect& a = layout.placements[i];
                extentX = std::max(extentX, a.x + sizes[i].width);
                extentY = std::max(extentY, a.y + sizes[i].height);
                ASSERT_LE(a.x + sizes[i].width, layout.width) << "set " << set << " sprite " << i;
                ASSERT_LE(a.y + sizes[i].height, layout.height) << "set " << set << " sprite " << i;
                for (size_t j = i + 1; j < sizes.size(); ++j) {
                    const PackedRect& b = layout.placements[j];
                    const auto grown = static_cast<uint32_t>(padding);
                    const bool apart = a.x + sizes[i].width + grown <= b.x || b.x + sizes[j].width + grown <= a.x || a.y + sizes[i].height + grown <= b.y || b.y + sizes[j].height + grown <= a.y;
                    ASSERT_TRUE(apart) << "set " << set << " sprites " << i << " and " << j << " overlap";
                }
            }
            if (mode == AtlasSizeMode::PowerOfTwo) {
                EXPECT_TRUE(std::has_single_bit(layout.width)) << "set " << set;
                EXPECT_TRUE(std::has_single_bit(layout.height)) << "set " << set;
                EXPECT_GE(layout.width, extentX);
                EXPECT_GE(layout.height, extentY);
            } else {
                EXPECT_EQ(layout.width, extentX) << "set " << set;
                EXPECT_EQ(layout.height, extentY) << "set " << set;
            }
        }
    }

    TEST(SpriteAtlasPacker, TheSameInputGivesTheSameLayout) {
        constexpr uint32_t kSets = 50;
        Lcg random(777);
        for (uint32_t set = 0; set < kSets; ++set) {
            const Vector<PackSize> sizes = RandomSizes(random);
            const Vector<PackSize> copy = sizes;
            for (const AtlasSizeMode mode : {AtlasSizeMode::PowerOfTwo, AtlasSizeMode::Tight}) {
                const PackedLayout first = MustPack(sizes, Settings(1, mode));
                EXPECT_TRUE(SameLayout(first, MustPack(sizes, Settings(1, mode)))) << "set " << set;
                EXPECT_TRUE(SameLayout(first, MustPack(copy, Settings(1, mode)))) << "set " << set;
            }
        }
    }

    TEST(SpriteAtlasPacker, ErrorsNameTheirCause) {
        const Vector<PackSize> one = {{8, 8}};
        EXPECT_EQ(MustFail({}, Settings(1, AtlasSizeMode::Tight)), AtlasPackError::NoSprites);

        Vector<PackSize> many(kMaxAtlasSprites + 1);
        for (PackSize& size : many) {
            size = {1, 1};
        }
        EXPECT_EQ(MustFail(many, Settings(0, AtlasSizeMode::Tight)), AtlasPackError::TooManySprites);

        EXPECT_EQ(MustFail({{8, 0}}, Settings(1, AtlasSizeMode::Tight)), AtlasPackError::BadSettings);
        EXPECT_EQ(MustFail({{0, 8}}, Settings(1, AtlasSizeMode::Tight)), AtlasPackError::BadSettings);
        EXPECT_EQ(MustFail(one, Settings(kMaxAtlasPadding + 1, AtlasSizeMode::Tight)), AtlasPackError::BadSettings);
        EXPECT_EQ(MustFail(one, Settings(-1, AtlasSizeMode::Tight)), AtlasPackError::BadSettings);
        EXPECT_EQ(MustFail(one, Settings(1, AtlasSizeMode::Tight, 0)), AtlasPackError::BadSettings);
        EXPECT_EQ(MustFail(one, Settings(1, AtlasSizeMode::Tight, kMaxSpriteImageSide + 1)), AtlasPackError::BadSettings);

        EXPECT_EQ(MustFail({{200, 8}}, Settings(0, AtlasSizeMode::Tight, 128)), AtlasPackError::SpriteTooLarge);
        EXPECT_EQ(MustFail({{8, 200}}, Settings(0, AtlasSizeMode::Tight, 128)), AtlasPackError::SpriteTooLarge);
    }

    TEST(SpriteAtlasPacker, SpritesThatCannotShareTheLimitDoNotFit) {
        const Vector<PackSize> three = {{100, 100}, {100, 100}, {100, 100}};
        EXPECT_EQ(MustFail(three, Settings(0, AtlasSizeMode::Tight, 150)), AtlasPackError::DoesNotFit);
        EXPECT_EQ(MustFail(three, Settings(0, AtlasSizeMode::PowerOfTwo, 150)), AtlasPackError::DoesNotFit);

        const PackedLayout fits = MustPack(three, Settings(0, AtlasSizeMode::Tight, 300));
        EXPECT_LE(fits.width, 300U);
        EXPECT_LE(fits.height, 300U);
    }

    TEST(SpriteAtlasPacker, AMaximumSideThatIsNotAPowerOfTwoIsNeverPassed) {
        const Vector<PackSize> sizes = {{70, 70}, {70, 70}};
        // The smallest power of two at or above 70 is 128, past the limit of 100: nothing can be offered in PowerOfTwo
        EXPECT_EQ(MustFail(sizes, Settings(0, AtlasSizeMode::PowerOfTwo, 100)), AtlasPackError::DoesNotFit);
        EXPECT_EQ(MustFail(sizes, Settings(0, AtlasSizeMode::Tight, 100)), AtlasPackError::DoesNotFit);
    }

    TEST(SpriteAtlasPacker, CompileTimeChecks) {
        static_assert(kDefaultAtlasPadding <= kMaxAtlasPadding);
        static_assert(kDefaultAtlasMaxSide <= kMaxSpriteImageSide);
        SUCCEED();
    }

}  // namespace BECore::Tests
