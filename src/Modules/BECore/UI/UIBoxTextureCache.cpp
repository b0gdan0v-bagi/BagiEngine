#include "UIBoxTextureCache.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UIGeometryBounds.h>
#include <cmath>

namespace BECore {

    namespace {

        /// SDL's software renderer cuts a destination rect to whole pixels (`SW_QueueCopy`, `(int)` of the float), and a rect of `w` pixels that comes out as `w - epsilon` would
        /// no longer be a one-to-one blit. The rect is placed a quarter of a pixel inside its pixel, away from zero, so the cut gives the pixel back whatever the float error is.
        constexpr float kPlacement = 0.25f;

        [[nodiscard]] bool IsPixelRange(float value) {
            return std::isfinite(value) && std::fabs(value) < static_cast<float>(UIBoxTexture::kMaxSide) * 64.0f;
        }

        [[nodiscard]] float PlaceStart(int pixel, float offset, float scale) {
            const float inside = pixel < 0 ? -kPlacement : kPlacement;
            return (static_cast<float>(pixel) + inside - offset) / scale;
        }

    }  // namespace

    bool UIBoxTextureCache::Draw(IRenderer& renderer, const Rect& area, const UIBoxStyle& style, const Color& fill, const Color& gradientEnd) {
        const ViewTransform& view = renderer.GetViewTransform();
        if (!(view.scale >= UIBounds::kMinScale && view.scale <= UIBounds::kMaxScale) || !UIBounds::IsBounded(area.x) || !UIBounds::IsBounded(area.y) || !UIBounds::IsBounded(area.w) ||
            !UIBounds::IsBounded(area.h)) {
            return false;
        }
        if (renderer.GetId() != _rendererId) {
            _entries.clear();  // the textures belong to another renderer, which has its own way of freeing them
            _bytes = 0;
            _rendererId = renderer.GetId();
            _rendererFailed = false;
        }
        if (_rendererFailed) {
            return false;
        }

        // The box goes to whole output pixels: the same rounding for both edges, so neighbouring boxes keep their gap
        const Rect out = view.ToOutput(area.x, area.y, area.w, area.h);
        if (!IsPixelRange(out.x) || !IsPixelRange(out.y) || !IsPixelRange(out.w) || !IsPixelRange(out.h)) {
            return false;
        }
        const int left = static_cast<int>(std::lround(out.x));
        const int top = static_cast<int>(std::lround(out.y));
        const int width = static_cast<int>(std::lround(out.x + out.w)) - left;
        const int height = static_cast<int>(std::lround(out.y + out.h)) - top;

        const UIBoxLook look = UIBoxTexture::MakeLook(style, fill, gradientEnd, view.scale, area.w, area.h);
        const Optional<UIBoxTextureLayout> layout = UIBoxTexture::MakeLayout(look, width, height);
        if (!layout) {
            return false;
        }
        ITexture* texture = Acquire(renderer, *layout);
        if (texture == nullptr) {
            return false;
        }

        Array<UIBoxTexturePiece, UIBoxTexture::kMaxPieces> pieces;
        const size_t count = UIBoxTexture::MakePieces(*layout, pieces);
        for (size_t i = 0; i < count; ++i) {
            const UIBoxTexturePiece& piece = pieces[i];
            const float x = PlaceStart(left + static_cast<int>(piece.destination.x), view.offsetX, view.scale);
            const float y = PlaceStart(top + static_cast<int>(piece.destination.y), view.offsetY, view.scale);
            renderer.DrawTexture(*texture, &piece.source, x, y, (piece.destination.w + kPlacement) / view.scale, (piece.destination.h + kPlacement) / view.scale);
        }
        return true;
    }

    ITexture* UIBoxTextureCache::Acquire(IRenderer& renderer, const UIBoxTextureLayout& layout) {
        ++_tick;
        for (Entry& entry : _entries) {
            if (entry.key == layout.key) {
                entry.lastUsed = _tick;
                return entry.texture.Get();
            }
        }

        BE_PROFILE_SCOPE("UIBoxTextureCache::Make");
        const Vector<uint8_t> pixels = UIBoxTexture::Render(layout);
        IntrusivePtr<ITexture> texture = renderer.CreateTexture(pixels.data(), static_cast<uint32_t>(layout.Width()), static_cast<uint32_t>(layout.Height()));
        if (!texture) {
            _rendererFailed = true;
            return nullptr;
        }
        ++_created;
        ITexture* result = texture.Get();
        _bytes += pixels.size();
        _entries.push_back({layout.key, std::move(texture), _tick, pixels.size()});
        Trim(result);
        return result;
    }

    void UIBoxTextureCache::Trim(const ITexture* keep) {
        while (_entries.size() > kMaxEntries || _bytes > kMaxBytes) {
            size_t oldest = _entries.size();
            for (size_t i = 0; i < _entries.size(); ++i) {
                if (_entries[i].texture.Get() != keep && (oldest == _entries.size() || _entries[i].lastUsed < _entries[oldest].lastUsed)) {
                    oldest = i;
                }
            }
            if (oldest == _entries.size()) {
                return;
            }
            _bytes -= _entries[oldest].bytes;
            _entries.erase(_entries.begin() + static_cast<ptrdiff_t>(oldest));
        }
    }

    void UIBoxTextureCache::Clear() {
        if (_entries.empty()) {
            return;
        }
        // A frame in flight may still read a texture: the GPU has to be idle before one is freed
        const auto& renderer = CoreManager::GetRenderer();
        if (renderer && renderer->GetId() == _rendererId) {
            renderer->WaitIdle();
        }
        _entries.clear();
        _bytes = 0;
        _rendererId = 0;
    }

}  // namespace BECore
