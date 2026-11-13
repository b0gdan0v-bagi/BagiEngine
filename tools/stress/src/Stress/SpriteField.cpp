#include "SpriteField.h"

#include <cmath>

namespace BECore::Stress {

    namespace {

        constexpr uint32_t kBlendCount = 3;  // Alpha, Add, Multiply
        constexpr uint32_t kChannelCount = 4;
        constexpr uint32_t kOpaque = 255;
        constexpr uint32_t kCornerAlpha = 96;
        constexpr uint32_t kCornerSize = 4;     // the translucent corner of a cell, in pixels
        constexpr uint32_t kMarkerMargin = 10;  // the marker of a cell is the square that starts this far from the cell's edge
        constexpr uint32_t kFullColor = 255;

        /// Streams of the hash: one per property of a sprite, so the properties do not follow each other
        constexpr uint64_t kSaltX = 1;
        constexpr uint64_t kSaltY = 2;
        constexpr uint64_t kSaltSize = 3;
        constexpr uint64_t kSaltSpin = 4;
        constexpr uint64_t kSaltCell = 5;

        /// splitmix64: a fixed integer mix, the same on every machine
        uint64_t Mix(uint64_t value) {
            value += 0x9e3779b97f4a7c15ULL;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
            return value ^ (value >> 31);
        }

        uint64_t Hash(uint32_t index, uint64_t salt) {
            return Mix((static_cast<uint64_t>(index) << 8) ^ salt);
        }

        /// A value of the hash in 0..1
        float Unit(uint32_t index, uint64_t salt) {
            constexpr uint64_t kBits = 24;
            return static_cast<float>(Hash(index, salt) >> (64 - kBits)) / static_cast<float>(1ULL << kBits);
        }

        float Between(float low, float high, float unit) {
            return low + (high - low) * unit;
        }

        /// A colour channel of cell @p cell of atlas @p atlas: spread over 40..255 so that no two cells of one atlas look alike
        uint8_t Channel(uint32_t atlas, uint32_t cell, uint64_t channel) {
            constexpr uint32_t kLowest = 40;
            const uint64_t mixed = Mix((static_cast<uint64_t>(atlas) << 32) ^ (static_cast<uint64_t>(cell) << 8) ^ channel);
            return static_cast<uint8_t>(kLowest + mixed % (kFullColor - kLowest + 1));
        }

    }  // namespace

    uint32_t AtlasRunLength(uint32_t count, uint32_t atlases) {
        return std::max<uint32_t>(1, count / std::max<uint32_t>(1, atlases));
    }

    SpritePlacement Place(uint32_t index, uint32_t count, const StressSettings& settings, const FieldView& view) {
        SpritePlacement placement;
        placement.size = std::floor(Between(kSpriteMinSize, kSpriteMaxSize + 1.0f, Unit(index, kSaltSize)));
        placement.x = std::floor(Unit(index, kSaltX) * (view.width - placement.size));
        placement.y = std::floor(Unit(index, kSaltY) * (view.height - placement.size));
        const float spinSpeed = Between(kSpriteMinSpin, kSpriteMaxSpin, Unit(index, kSaltSpin));
        placement.spin = (Hash(index, kSaltSpin) & 1) != 0 ? spinSpeed : -spinSpeed;

        const uint32_t atlases = std::max<uint32_t>(1, settings.atlases);
        if (settings.interleave) {
            placement.atlas = index % atlases;
            placement.blend = static_cast<BlendMode>(index % kBlendCount);
        } else {
            placement.atlas = (index / AtlasRunLength(count, atlases)) % atlases;
            placement.blend = static_cast<BlendMode>((index / kBlendRunLength) % kBlendCount);
        }

        const uint32_t cell = static_cast<uint32_t>(Hash(index, kSaltCell) % (kAtlasCellsPerSide * kAtlasCellsPerSide));
        const float cellSize = static_cast<float>(kAtlasCellSize);
        placement.srcRect = Rect(static_cast<float>(cell % kAtlasCellsPerSide) * cellSize, static_cast<float>(cell / kAtlasCellsPerSide) * cellSize, cellSize, cellSize);
        return placement;
    }

    eastl::vector<uint8_t> MakeAtlasPixels(uint32_t atlasIndex, uint32_t cells, uint32_t cellSize) {
        const uint32_t side = cells * cellSize;
        eastl::vector<uint8_t> pixels(static_cast<size_t>(side) * side * kChannelCount, 0);
        for (uint32_t y = 0; y < side; ++y) {
            for (uint32_t x = 0; x < side; ++x) {
                const uint32_t cellX = x / cellSize;
                const uint32_t cellY = y / cellSize;
                const uint32_t localX = x % cellSize;
                const uint32_t localY = y % cellSize;
                const uint32_t cell = cellY * cells + cellX;

                uint8_t red = Channel(atlasIndex, cell, 0);
                uint8_t green = Channel(atlasIndex, cell, 1);
                uint8_t blue = Channel(atlasIndex, cell, 2);
                uint32_t alpha = kOpaque;

                // Translucent corners: the quarter of a disc of kCornerSize around each corner of the cell
                const uint32_t edgeX = std::min(localX, cellSize - 1 - localX);
                const uint32_t edgeY = std::min(localY, cellSize - 1 - localY);
                if (edgeX + edgeY < kCornerSize) {
                    alpha = kCornerAlpha;
                }
                // The marker: a square of the inverted colour in the middle of the cell, its side grows with the column, so a wrong cell shows
                const uint32_t markerHalf = (cellSize - 2 * kMarkerMargin) / 2 * (1 + cell % cells) / cells;
                const uint32_t middle = cellSize / 2;
                if (localX + markerHalf >= middle && localX < middle + markerHalf && localY + markerHalf >= middle && localY < middle + markerHalf) {
                    red = static_cast<uint8_t>(kFullColor - red);
                    green = static_cast<uint8_t>(kFullColor - green);
                    blue = static_cast<uint8_t>(kFullColor - blue);
                }

                const size_t offset = (static_cast<size_t>(y) * side + x) * kChannelCount;
                pixels[offset] = red;
                pixels[offset + 1] = green;
                pixels[offset + 2] = blue;
                pixels[offset + 3] = static_cast<uint8_t>(alpha);
            }
        }
        return pixels;
    }

}  // namespace BECore::Stress
