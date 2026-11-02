#include <BECore/Renderer/IRenderer.h>
#include <Generated/IRenderer.gen.hpp>

namespace BECore {

    IRenderer::IRenderer() {
        static std::atomic<uint64_t> nextId{1};
        _id = nextId.fetch_add(1, std::memory_order_relaxed);
    }

    void IRenderer::RequestCapture(CaptureCallback callback) {
        if (callback) {
            callback(std::unexpected(eastl::string("this renderer backend cannot read its output back")));
        }
    }

    void IRenderer::DrawGeometry(ITexture* /*texture*/, eastl::span<const GeometryVertex> /*vertices*/, eastl::span<const int> /*indices*/, float /*translateX*/, float /*translateY*/) {
        if (!_geometryWarned) {
            _geometryWarned = true;
            LOG_WARNING("IRenderer: this backend cannot draw geometry, so what needs it is not drawn");
        }
    }

    void IRenderer::PushClipRect(const Rect& rect) {
        Rect clip = rect;
        if (!_clipStack.empty()) {
            const Rect& outer = _clipStack.back();
            const float left = std::max(clip.x, outer.x);
            const float top = std::max(clip.y, outer.y);
            const float right = std::min(clip.x + clip.w, outer.x + outer.w);
            const float bottom = std::min(clip.y + clip.h, outer.y + outer.h);
            clip = Rect{left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
        } else if (clip.w < 0.0f || clip.h < 0.0f) {
            clip = Rect{clip.x, clip.y, std::max(0.0f, clip.w), std::max(0.0f, clip.h)};
        }
        _clipStack.push_back(clip);
        OnClipRectChanged();
    }

    void IRenderer::PopClipRect() {
        ASSERT(!_clipStack.empty(), "PopClipRect without a matching PushClipRect");
        if (_clipStack.empty()) {
            return;
        }
        _clipStack.pop_back();
        OnClipRectChanged();
    }

    std::optional<Rect> IRenderer::GetClipRect() const {
        if (_clipStack.empty()) {
            return std::nullopt;
        }
        return _clipStack.back();
    }

    bool IRenderer::IsClipEmpty() const {
        return !_clipStack.empty() && _clipStack.back().IsEmpty();
    }

}  // namespace BECore
