#include "RemotePicture.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/Utils/Base64.h>
#include <CoreSDL/SDLScreenshot.h>

namespace BECore {

    void RemotePicture::Load(JsonView result) {
        const JsonView png = FindParam(result, "png_base64").value_or(JsonView{});
        if (!png.IsString()) {
            _error.Clear();
            _error.message = "the answer holds no picture";
            return;
        }
        const StringView source = png.Raw();
        _error.Clear();
        if (source == StringView(_shownSource)) {
            return;
        }
        if (source != StringView(_brokenSource)) {
            auto image = Decode(result, png);
            if (image) {
                _image = std::move(*image);
                _shownSource.assign(source.begin(), source.end());
                ++_revision;
                return;
            }
            _brokenSource.assign(source.begin(), source.end());
            _brokenReason = std::move(image.error());
        }
        _error.message = _brokenReason;
    }

    void RemotePicture::Clear() {
        if (!_image.pixels.empty()) {
            ++_revision;
        }
        _image = {};
        _error.Clear();
        _shownSource.clear();
        _brokenSource.clear();
        _brokenReason.clear();
    }

    std::expected<RgbaImage, String> RemotePicture::Decode(JsonView result, JsonView png) {
        const int64_t width = GetParamInt(result, "width").value_or(0);
        const int64_t height = GetParamInt(result, "height").value_or(0);
        if (width <= 0 || height <= 0 || width > kMaxScreenshotSize || height > kMaxScreenshotSize) {
            return std::unexpected(Format("the answer gives no sides of 1 to {} pixels", kMaxScreenshotSize));
        }
        png.ReadText(_text);
        if (!Base64Decode(_text, _png)) {
            return std::unexpected(String("the picture is no base64 text"));
        }
        auto image = DecodePngImage(Span<const uint8_t>(_png.data(), _png.size()), kMaxScreenshotSize);
        if (image && (image->width != static_cast<uint32_t>(width) || image->height != static_cast<uint32_t>(height))) {
            return std::unexpected(Format("the picture is {}x{} pixels and the answer says {}x{}", image->width, image->height, width, height));
        }
        return image;
    }

}  // namespace BECore
