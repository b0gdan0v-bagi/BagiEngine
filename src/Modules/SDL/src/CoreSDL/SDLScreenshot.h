#pragma once

#include <BECore/Renderer/IRenderer.h>

namespace BECore {

    class ControlService;

    /// Longest side, in pixels, of the picture that `screenshot` returns when the client does not ask for another size
    inline constexpr uint32_t kDefaultScreenshotSize = 1024;

    /// The range of the `maxSize` parameter of `screenshot`
    inline constexpr uint32_t kMinScreenshotSize = 16;
    inline constexpr uint32_t kMaxScreenshotSize = 4096;

    /// Error codes of `screenshot` (the parameter errors are ControlErrors::kInvalidParams)
    namespace ScreenshotErrors {
        inline constexpr StringView kCaptureFailed = "capture_failed";
    }  // namespace ScreenshotErrors

    /// A picture as a PNG file in memory
    struct PngImage {
        uint32_t width = 0;
        uint32_t height = 0;
        eastl::vector<uint8_t> bytes;
    };

    /**
     * @brief Encodes @p image as PNG, scaled down first when its longer side is above @p maxSide.
     *
     * The proportions are kept and the picture is never scaled up. @p maxSide of 0 means no scaling. Fails for an image
     * that is empty or whose pixels do not fill `width * height * 4` bytes.
     */
    [[nodiscard]] std::expected<PngImage, String> EncodePng(const PixelBuffer& image, uint32_t maxSide);

    /**
     * @brief Registers the `screenshot` method of the control API in @p service.
     *
     * | Params | Result |
     * |--------|--------|
     * | `{"maxSize": 16 to 4096}` (optional, default 1024: the longer side of the picture) | `{"width", "height", "png_base64"}` |
     *
     * The picture is what the renderer presents at the end of one frame that has a clock step of zero seconds: on the manual
     * clock that is a frame of its own, on the real clock the frame in which the request is handled. Nothing moves in it, so
     * a screenshot after an action shows the action's result. `capture_failed` when the renderer cannot read its output
     * back (the Vulkan backend) or no frame was presented. The method looks the renderer up on every call.
     */
    void RegisterScreenshotControl(ControlService& service);

}  // namespace BECore
