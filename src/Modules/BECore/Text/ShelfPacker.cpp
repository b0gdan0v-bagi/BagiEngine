#include "ShelfPacker.h"

namespace BECore {

    Optional<ShelfPacker::Slot> ShelfPacker::Pack(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0 || width > _width || height > _height) {
            return std::nullopt;
        }

        Shelf* best = nullptr;
        for (Shelf& shelf : _shelves) {
            if (shelf.height >= height && _width - shelf.usedWidth >= width && (best == nullptr || shelf.height < best->height)) {
                best = &shelf;
            }
        }
        if (best == nullptr) {
            if (_height - _nextShelfY < height) {
                return std::nullopt;
            }
            _shelves.push_back({_nextShelfY, height, 0});
            _nextShelfY += height;
            best = &_shelves.back();
        }

        const Slot slot{best->usedWidth, best->y};
        best->usedWidth += width;
        return slot;
    }

}  // namespace BECore
