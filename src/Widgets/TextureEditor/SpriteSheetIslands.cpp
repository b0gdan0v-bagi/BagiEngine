#include "SpriteSheetIslands.h"

#include <EASTL/sort.h>
#include <Widgets/TextureEditor/SpriteSheetGrid.h>

namespace BECore {

    namespace {

        constexpr size_t kAlphaOffset = 3;

        /// Half-open: `right` and `bottom` are the first pixel outside
        struct Box {
            int x = 0;
            int y = 0;
            int right = 0;
            int bottom = 0;

            [[nodiscard]] int Width() const {
                return right - x;
            }
            [[nodiscard]] int Height() const {
                return bottom - y;
            }
        };

        bool LessByLeft(const Box& a, const Box& b) {
            if (a.x != b.x) {
                return a.x < b.x;
            }
            if (a.y != b.y) {
                return a.y < b.y;
            }
            if (a.right != b.right) {
                return a.right < b.right;
            }
            return a.bottom < b.bottom;
        }

        bool LessByTop(const Box& a, const Box& b) {
            if (a.y != b.y) {
                return a.y < b.y;
            }
            return LessByLeft(a, b);
        }

        bool SpecIsValid(const IslandSpec& spec) {
            return spec.alphaThreshold >= 0 && spec.alphaThreshold <= kMaxIslandAlphaThreshold && spec.minSize >= 1 && spec.padding >= 0 && spec.padding <= kMaxIslandPadding && spec.mergeGap >= 0 &&
                   spec.mergeGap <= kMaxIslandMergeGap;
        }

        /// The empty pixels between two boxes on one axis; negative when they overlap
        int AxisGap(int beginA, int endA, int beginB, int endB) {
            return std::max(beginB - endA, beginA - endB);
        }

        bool AreClose(const Box& a, const Box& b, int mergeGap) {
            return AxisGap(a.x, a.right, b.x, b.right) <= mergeGap && AxisGap(a.y, a.bottom, b.y, b.bottom) <= mergeGap;
        }

        void Absorb(Box& into, const Box& other) {
            into.x = std::min(into.x, other.x);
            into.y = std::min(into.y, other.y);
            into.right = std::max(into.right, other.right);
            into.bottom = std::max(into.bottom, other.bottom);
        }

        /// The bounding box of every 8-connected group of solid pixels. Iterative: a sheet can be thousands of pixels wide, so a recursive
        /// fill would overflow the stack. Stops with false at kMaxIslandComponents groups.
        bool FindComponents(const RgbaImage& image, int threshold, Vector<Box>& out) {
            const int width = static_cast<int>(image.width);
            const int height = static_cast<int>(image.height);
            const auto isSolid = [&image, threshold](size_t index) { return image.pixels[index * kBytesPerPixel + kAlphaOffset] > threshold; };

            Vector<uint8_t> seen(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
            Vector<uint32_t> stack;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t start = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                    if (seen[start] != 0 || !isSolid(start)) {
                        continue;
                    }
                    if (out.size() >= kMaxIslandComponents) {
                        return false;
                    }
                    Box box{x, y, x + 1, y + 1};
                    seen[start] = 1;
                    stack.push_back(static_cast<uint32_t>(start));
                    while (!stack.empty()) {
                        const size_t index = stack.back();
                        stack.pop_back();
                        const int px = static_cast<int>(index % static_cast<size_t>(width));
                        const int py = static_cast<int>(index / static_cast<size_t>(width));
                        box.x = std::min(box.x, px);
                        box.y = std::min(box.y, py);
                        box.right = std::max(box.right, px + 1);
                        box.bottom = std::max(box.bottom, py + 1);
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int nx = px + dx;
                                const int ny = py + dy;
                                if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                                    continue;
                                }
                                const size_t next = static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                                if (seen[next] == 0 && isSolid(next)) {
                                    seen[next] = 1;
                                    stack.push_back(static_cast<uint32_t>(next));
                                }
                            }
                        }
                    }
                    out.push_back(box);
                }
            }
            return true;
        }

        /// Merges until no two boxes are close. Sorted by the left edge, a box only looks right until the next one starts farther away than
        /// the gap; a merge can make an earlier skip close, so passes repeat until one changes nothing. The fixed point does not depend on the scan.
        void MergeBoxes(Vector<Box>& boxes, int mergeGap) {
            bool changed = true;
            while (changed) {
                changed = false;
                eastl::sort(boxes.begin(), boxes.end(), LessByLeft);
                Vector<uint8_t> absorbed(boxes.size(), 0);
                for (size_t i = 0; i < boxes.size(); ++i) {
                    if (absorbed[i] != 0) {
                        continue;
                    }
                    for (size_t j = i + 1; j < boxes.size(); ++j) {
                        if (absorbed[j] != 0) {
                            continue;
                        }
                        if (boxes[j].x - boxes[i].right > mergeGap) {
                            break;
                        }
                        if (AreClose(boxes[i], boxes[j], mergeGap)) {
                            Absorb(boxes[i], boxes[j]);
                            absorbed[j] = 1;
                            changed = true;
                        }
                    }
                }
                size_t kept = 0;
                for (size_t i = 0; i < boxes.size(); ++i) {
                    if (absorbed[i] == 0) {
                        boxes[kept++] = boxes[i];
                    }
                }
                boxes.resize(kept);
            }
        }

        /// The reading order of the header: rows led by the topmost box that is left, each row left to right
        Vector<Box> InReadingOrder(Vector<Box> boxes) {
            eastl::sort(boxes.begin(), boxes.end(), LessByTop);
            Vector<uint8_t> placed(boxes.size(), 0);
            Vector<Box> ordered;
            ordered.reserve(boxes.size());
            Vector<Box> row;
            for (size_t i = 0; i < boxes.size(); ++i) {
                if (placed[i] != 0) {
                    continue;
                }
                const Box leader = boxes[i];
                row.clear();
                for (size_t j = i; j < boxes.size(); ++j) {
                    // Twice the centre, so the test needs no fractions
                    const int doubledCentre = boxes[j].y + boxes[j].bottom;
                    if (placed[j] == 0 && doubledCentre >= 2 * leader.y && doubledCentre <= 2 * leader.bottom) {
                        placed[j] = 1;
                        row.push_back(boxes[j]);
                    }
                }
                eastl::sort(row.begin(), row.end(), LessByLeft);
                ordered.insert(ordered.end(), row.begin(), row.end());
            }
            return ordered;
        }

    }  // namespace

    StringView SheetIslandErrorText(SheetIslandError error) {
        switch (error) {
            case SheetIslandError::NoImage:
                return "No image to scan";
            case SheetIslandError::BadSpec:
                return "Threshold 0 to 254, minimum size at least 1, padding and merge gap 0 to 64";
            case SheetIslandError::TooManyIslands:
                return "Too many islands: raise the minimum size or the merge gap";
            case SheetIslandError::NoIslands:
                return "No island found: lower the alpha threshold or the minimum size";
        }
        return {};
    }

    std::expected<Vector<Rect>, SheetIslandError> FindAlphaIslands(const RgbaImage& image, const IslandSpec& spec) {
        if (image.width == 0 || image.height == 0 || image.pixels.size() != static_cast<size_t>(image.width) * image.height * kBytesPerPixel) {
            return std::unexpected(SheetIslandError::NoImage);
        }
        if (!SpecIsValid(spec)) {
            return std::unexpected(SheetIslandError::BadSpec);
        }

        Vector<Box> boxes;
        if (!FindComponents(image, spec.alphaThreshold, boxes)) {
            return std::unexpected(SheetIslandError::TooManyIslands);
        }
        MergeBoxes(boxes, spec.mergeGap);

        Vector<Box> kept;
        for (const Box& box : boxes) {
            if (box.Width() >= spec.minSize && box.Height() >= spec.minSize) {
                kept.push_back(box);
            }
        }
        if (kept.empty()) {
            return std::unexpected(SheetIslandError::NoIslands);
        }
        if (kept.size() > static_cast<size_t>(kMaxSheetGridCells)) {
            return std::unexpected(SheetIslandError::TooManyIslands);
        }

        const int sheetWidth = static_cast<int>(image.width);
        const int sheetHeight = static_cast<int>(image.height);
        Vector<Rect> rects;
        rects.reserve(kept.size());
        for (const Box& box : InReadingOrder(eastl::move(kept))) {
            const int x = std::max(box.x - spec.padding, 0);
            const int y = std::max(box.y - spec.padding, 0);
            const int right = std::min(box.right + spec.padding, sheetWidth);
            const int bottom = std::min(box.bottom + spec.padding, sheetHeight);
            rects.push_back(Rect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(right - x), static_cast<float>(bottom - y)));
        }
        return rects;
    }

}  // namespace BECore
