#pragma once


namespace BECore {

    /**
     * @brief The paint of a text besides its fill: an outline and a hard drop shadow, in output pixels (all zero: no effect).
     *
     * Paint, not layout: neither enters the measure of a text or the layout cache key, the same rule as the shadow and the
     * glow of a box. The outline is as wide as the outline atlas the caller draws with (FontAtlasLibrary::QuantizeOutline()).
     */
    struct TextEffects {
        Color outlineColor;
        float outlineWidth = 0.0f;  ///< output pixels
        Color shadowColor;
        float shadowOffsetX = 0.0f;  ///< output pixels, to the right
        float shadowOffsetY = 0.0f;  ///< output pixels, down

        /// A width above 0 and a colour that is not transparent
        [[nodiscard]] bool HasOutline() const {
            return outlineWidth > 0.0f && outlineColor.a > 0;
        }
        /// A colour that is not transparent and an offset that is not (0, 0): a shadow right under the text is invisible, so it is not drawn
        [[nodiscard]] bool HasShadow() const {
            return shadowColor.a > 0 && (shadowOffsetX != 0.0f || shadowOffsetY != 0.0f);
        }
    };

}  // namespace BECore
