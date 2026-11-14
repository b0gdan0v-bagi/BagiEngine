#pragma once

namespace BECore {

    /**
     * @brief Packs rectangles into a fixed-size area in rows ("shelves"). Pure code.
     *
     * A rect goes onto the existing shelf that is tall enough and wastes the least height; when none has room a new
     * shelf is opened below the last one. Feed it rects from tall to short for a tight result. Nothing is ever moved or
     * freed: a glyph page is filled once and then only read.
     */
    class ShelfPacker {
    public:
        struct Slot {
            uint32_t x = 0;
            uint32_t y = 0;
        };

        ShelfPacker(uint32_t width, uint32_t height) : _width(width), _height(height) {}

        /// Where a rect of this size goes, or nothing when it does not fit (or when a side is 0)
        [[nodiscard]] Optional<Slot> Pack(uint32_t width, uint32_t height);

        [[nodiscard]] uint32_t GetWidth() const {
            return _width;
        }
        [[nodiscard]] uint32_t GetHeight() const {
            return _height;
        }
        /// The bottom edge of the lowest shelf
        [[nodiscard]] uint32_t GetUsedHeight() const {
            return _nextShelfY;
        }

    private:
        struct Shelf {
            uint32_t y = 0;
            uint32_t height = 0;
            uint32_t usedWidth = 0;
        };

        uint32_t _width = 0;
        uint32_t _height = 0;
        uint32_t _nextShelfY = 0;
        Vector<Shelf> _shelves;
    };

}  // namespace BECore
