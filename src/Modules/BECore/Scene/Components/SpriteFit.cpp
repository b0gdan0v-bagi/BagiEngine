#include "SpriteFit.h"

namespace BECore {

    Rect FitSpriteRect(float srcWidth, float srcHeight, const Rect& dst, SpriteFit fit) {
        if (fit == SpriteFit::Stretch) {
            return dst;
        }
        if (srcWidth <= 0.0f || srcHeight <= 0.0f || dst.w <= 0.0f || dst.h <= 0.0f) {
            return dst;
        }

        const float scale = std::min(dst.w / srcWidth, dst.h / srcHeight);
        const float width = srcWidth * scale;
        const float height = srcHeight * scale;
        return Rect{dst.x + (dst.w - width) * 0.5f, dst.y + (dst.h - height) * 0.5f, width, height};
    }

}  // namespace BECore
