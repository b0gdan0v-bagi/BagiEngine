#include "FontAtlasLibrary.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Text/GlyphOutline.h>

namespace BECore {

    void FontAtlasLibrary::Initialize() {
        _settings = TextSettings::FromConfig(CoreManager::GetConfigManager().GetConfig("TextConfig"_intern));
    }

    PoolString FontAtlasLibrary::ResolveFontPath(const FontRequest& request, bool* known) const {
        if (!request.font.Empty()) {
            if (known) {
                *known = true;
            }
            return request.font;
        }
        return _settings.ResolveFontPath(request.family, request.weight, known);
    }

    int FontAtlasLibrary::QuantizeSize(float effectivePixelSize) {
        if (!(effectivePixelSize > 1.0f)) {  // also catches NaN
            return 1;
        }
        return std::min(static_cast<int>(std::lround(effectivePixelSize)), kMaxPixelSize);
    }

    int FontAtlasLibrary::QuantizeOutline(float widthInPixels) {
        if (!(widthInPixels > 0.0f)) {  // also catches NaN
            return 0;
        }
        return std::clamp(static_cast<int>(std::lround(widthInPixels)), 1, kMaxOutlinePixels);
    }

    IntrusivePtr<FontAtlas> FontAtlasLibrary::Get(IRenderer& renderer, const IntrusivePtr<Font>& font, int pixelSize, int outlinePixels) {
        if (!font || font->GetState() != ResourceState::Loaded) {
            return {};
        }
        if (renderer.GetId() != _rendererId) {
            _atlases.clear();  // the textures belong to another renderer, which has its own way of freeing them
            _rendererId = renderer.GetId();
        }
        pixelSize = std::clamp(pixelSize, 1, kMaxPixelSize);
        outlinePixels = std::clamp(outlinePixels, 0, kMaxOutlinePixels);

        for (const Entry& entry : _atlases) {
            if (entry.font == font && entry.pixelSize == pixelSize && entry.outlinePixels == outlinePixels) {
                return entry.atlas;
            }
        }

        auto atlas = New<FontAtlas>(font, pixelSize, _settings.pageSize, outlinePixels);
        if (outlinePixels == 0) {
            atlas->PrepareCodePoints(renderer, _settings.preload);
        }
        _atlases.push_back({font, pixelSize, outlinePixels, atlas});
        return atlas;
    }

    void FontAtlasLibrary::Clear() {
        if (_atlases.empty()) {
            return;
        }
        // A frame in flight may still read a page: the GPU has to be idle before a texture is freed
        const auto& renderer = CoreManager::GetRenderer();
        if (renderer && renderer->GetId() == _rendererId) {
            renderer->WaitIdle();
        }
        _atlases.clear();
        _rendererId = 0;
    }

}  // namespace BECore
