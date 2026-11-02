#pragma once

namespace BECore {

    /// How a sprite fills the rect of its node.
    /// Stretch: the texture covers the whole rect, whatever its aspect (the default, what a sprite always did).
    /// Contain: the texture keeps the aspect of its source, is as big as the rect allows and is centered in it.
    CORE_ENUM(SpriteFit, uint8_t, Stretch, Contain)

    /**
     * @brief The rect a sprite is drawn into, inside the rect of its node.
     *
     * Pure code, in the units of its arguments (the caller passes scene units, the view transform comes after).
     * Stretch returns dst. Contain scales the source by min(dst.w / srcWidth, dst.h / srcHeight) and centers the result in dst,
     * so one side of the result equals the same side of dst and the other is shorter.
     * A zero or negative source or destination size has no aspect: dst is returned unchanged, whatever the fit.
     *
     * @param srcWidth  Width of the part of the texture that is drawn (the source rect, or the whole texture)
     * @param srcHeight Height of the same part
     * @param dst       The rect of the node
     * @param fit       The mode
     */
    [[nodiscard]] Rect FitSpriteRect(float srcWidth, float srcHeight, const Rect& dst, SpriteFit fit);

}  // namespace BECore
