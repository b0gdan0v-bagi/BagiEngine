#include "SpriteAtlasPacker.h"

#include <EASTL/sort.h>
#include <bit>
#include <cmath>

namespace BECore {

    namespace {

        struct ShelfResult {
            uint32_t width = 0;
            uint32_t height = 0;
        };

        bool SettingsAreValid(const PackSettings& settings) {
            return settings.padding >= 0 && settings.padding <= kMaxAtlasPadding && settings.maxSide >= 1 && settings.maxSide <= kMaxSpriteImageSide;
        }

        /// The packing order; the index makes it total, so the result does not depend on how the sort treats equal items
        Vector<uint32_t> PackingOrder(const Vector<PackSize>& sizes) {
            Vector<uint32_t> order(sizes.size());
            for (size_t i = 0; i < order.size(); ++i) {
                order[i] = static_cast<uint32_t>(i);
            }
            eastl::sort(order.begin(), order.end(), [&sizes](uint32_t a, uint32_t b) {
                if (sizes[a].height != sizes[b].height) {
                    return sizes[a].height > sizes[b].height;
                }
                if (sizes[a].width != sizes[b].width) {
                    return sizes[a].width > sizes[b].width;
                }
                return a < b;
            });
            return order;
        }

        /// Fills @p placements for an atlas @p atlasWidth wide and returns the bounding box of what was placed
        ShelfResult PlaceOnShelves(const Vector<PackSize>& sizes, const Vector<uint32_t>& order, uint32_t atlasWidth, uint32_t padding, Vector<PackedRect>& placements) {
            ShelfResult used;
            uint32_t x = 0;
            uint32_t shelfTop = 0;
            uint32_t shelfHeight = 0;
            for (const uint32_t index : order) {
                const PackSize& size = sizes[index];
                if (shelfHeight != 0 && x + size.width > atlasWidth) {
                    shelfTop += shelfHeight + padding;
                    shelfHeight = 0;
                    x = 0;
                }
                if (shelfHeight == 0) {
                    shelfHeight = size.height;
                }
                placements[index] = PackedRect{x, shelfTop};
                used.width = std::max(used.width, x + size.width);
                used.height = std::max(used.height, shelfTop + size.height);
                x += size.width + padding;
            }
            return used;
        }

        uint32_t CeilSqrt(uint64_t value) {
            auto root = static_cast<uint64_t>(std::ceil(std::sqrt(static_cast<double>(value))));
            while (root * root < value) {
                ++root;
            }
            while (root > 1 && (root - 1) * (root - 1) >= value) {
                --root;
            }
            return static_cast<uint32_t>(root);
        }

        Vector<uint32_t> CandidateWidths(uint32_t widest, uint64_t paddedArea, const PackSettings& settings) {
            Vector<uint32_t> widths;
            for (uint32_t width = std::bit_ceil(widest); width <= settings.maxSide; width *= 2) {
                widths.push_back(width);
            }
            if (settings.sizeMode == AtlasSizeMode::Tight) {
                const uint32_t squareWidth = std::min(std::max(CeilSqrt(paddedArea), widest), settings.maxSide);
                if (std::find(widths.begin(), widths.end(), squareWidth) == widths.end()) {
                    widths.push_back(squareWidth);
                }
            }
            return widths;
        }

    }  // namespace

    StringView AtlasPackErrorText(AtlasPackError error) {
        switch (error) {
            case AtlasPackError::NoSprites:
                return "No sprites to pack";
            case AtlasPackError::TooManySprites:
                return "Too many sprites for one atlas";
            case AtlasPackError::BadSettings:
                return "Padding 0 to 16, maximum side 1 to 8192, every sprite at least 1x1";
            case AtlasPackError::SpriteTooLarge:
                return "A sprite is larger than the maximum atlas side";
            case AtlasPackError::DoesNotFit:
                return "The sprites do not fit under the maximum atlas side";
        }
        return {};
    }

    std::expected<PackedLayout, AtlasPackError> PackSizes(const Vector<PackSize>& sizes, const PackSettings& settings) {
        if (sizes.empty()) {
            return std::unexpected(AtlasPackError::NoSprites);
        }
        if (sizes.size() > kMaxAtlasSprites) {
            return std::unexpected(AtlasPackError::TooManySprites);
        }
        if (!SettingsAreValid(settings)) {
            return std::unexpected(AtlasPackError::BadSettings);
        }
        const auto padding = static_cast<uint32_t>(settings.padding);
        uint32_t widest = 0;
        for (const PackSize& size : sizes) {
            if (size.width == 0 || size.height == 0) {
                return std::unexpected(AtlasPackError::BadSettings);
            }
            if (size.width > settings.maxSide || size.height > settings.maxSide) {
                return std::unexpected(AtlasPackError::SpriteTooLarge);
            }
            widest = std::max(widest, size.width);
        }

        uint64_t paddedArea = 0;
        for (const PackSize& size : sizes) {
            paddedArea += static_cast<uint64_t>(size.width + padding) * (size.height + padding);
        }

        const Vector<uint32_t> order = PackingOrder(sizes);
        const bool powerOfTwo = settings.sizeMode == AtlasSizeMode::PowerOfTwo;

        PackedLayout best;
        uint64_t bestArea = 0;
        Vector<PackedRect> placements(sizes.size());
        for (const uint32_t candidate : CandidateWidths(widest, paddedArea, settings)) {
            ShelfResult result = PlaceOnShelves(sizes, order, candidate, padding, placements);
            if (powerOfTwo) {
                result.width = std::bit_ceil(result.width);
                result.height = std::bit_ceil(result.height);
            }
            if (result.width > settings.maxSide || result.height > settings.maxSide) {
                continue;
            }
            const uint64_t area = static_cast<uint64_t>(result.width) * result.height;
            const bool better = bestArea == 0 || area < bestArea || (area == bestArea && result.width < best.width);
            if (better) {
                bestArea = area;
                best.width = result.width;
                best.height = result.height;
                best.placements = placements;
            }
        }
        if (bestArea == 0) {
            return std::unexpected(AtlasPackError::DoesNotFit);
        }
        return best;
    }

}  // namespace BECore
