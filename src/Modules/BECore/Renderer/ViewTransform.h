#pragma once

namespace BECore {

    /**
     * @brief Maps scene units to pixels of the render output: pixel = unit * scale + offset.
     *
     * The default is the identity (one unit is one pixel). Fit() builds the transform that shows a view of a fixed size
     * in an output of any size.
     */
    struct ViewTransform {
        float scale = 1.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;

        /**
         * @brief Scale a view of viewWidth x viewHeight units uniformly to the largest size that fits the output, and
         *        center it. The free sides of the output stay empty (bars). The identity when a size is not positive.
         */
        [[nodiscard]] static constexpr ViewTransform Fit(float viewWidth, float viewHeight, float outputWidth, float outputHeight) {
            if (viewWidth <= 0.0f || viewHeight <= 0.0f || outputWidth <= 0.0f || outputHeight <= 0.0f) {
                return {};
            }
            const float scaleX = outputWidth / viewWidth;
            const float scaleY = outputHeight / viewHeight;
            const float scale = scaleX < scaleY ? scaleX : scaleY;
            return {scale, (outputWidth - viewWidth * scale) * 0.5f, (outputHeight - viewHeight * scale) * 0.5f};
        }

        [[nodiscard]] constexpr Rect ToOutput(float x, float y, float w, float h) const {
            return {x * scale + offsetX, y * scale + offsetY, w * scale, h * scale};
        }

        [[nodiscard]] constexpr float ToViewX(float outputX) const {
            return (outputX - offsetX) / scale;
        }

        [[nodiscard]] constexpr float ToViewY(float outputY) const {
            return (outputY - offsetY) / scale;
        }
    };

}  // namespace BECore
