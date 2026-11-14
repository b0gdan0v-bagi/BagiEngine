#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>
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

    eastl::string IRenderer::GetNoGpuDeviceReason() const {
        return eastl::string("this renderer backend has no GPU device");
    }

    std::expected<IGpuDevice*, eastl::string> RequireGpuDevice(IRenderer& renderer) {
        if (IGpuDevice* device = renderer.GetGpuDevice()) {
            return device;
        }
        return std::unexpected(Format("{} cannot draw in 3D: {}", renderer.GetTypeMeta().typeName, renderer.GetNoGpuDeviceReason()));
    }

    RenderCounters IRenderer::TakeRenderCounters() {
        const RenderCounters taken = _counters;
        _counters = {};
        _lastCountedTexture = nullptr;  // the first textured draw of the next frame is a switch
        return taken;
    }

    void IRenderer::DrawGeometry(ITexture* /*texture*/, eastl::span<const GeometryVertex> /*vertices*/, eastl::span<const int> /*indices*/, float /*translateX*/, float /*translateY*/) {
        if (!_geometryWarned) {
            _geometryWarned = true;
            LOG_WARNING("IRenderer: this backend cannot draw geometry, so what needs it is not drawn");
        }
    }

    namespace {
        // Two triangles over the corners in the order of MakeTransformedQuad(): top-left, top-right, bottom-left, bottom-right
        constexpr std::array<int, 6> kQuadIndices = {0, 1, 2, 2, 1, 3};
    }  // namespace

    void IRenderer::DrawTransformedRect(float x, float y, float w, float h, const Color& color, const DrawTransform& transform) {
        if (transform.IsIdentity()) {
            DrawFilledRect(x, y, w, h, color);
            return;
        }
        const std::array<Vec2, 4> corners = MakeTransformedQuad(Rect{x, y, w, h}, transform);
        const std::array<GeometryVertex, 4> vertices = {
            GeometryVertex{corners[0].x, corners[0].y, color, 0.0f, 0.0f},
            GeometryVertex{corners[1].x, corners[1].y, color, 1.0f, 0.0f},
            GeometryVertex{corners[2].x, corners[2].y, color, 0.0f, 1.0f},
            GeometryVertex{corners[3].x, corners[3].y, color, 1.0f, 1.0f},
        };
        DrawGeometry(nullptr, {vertices.data(), vertices.size()}, {kQuadIndices.data(), kQuadIndices.size()}, 0.0f, 0.0f);
    }

    void IRenderer::DrawTransformedTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint, const DrawTransform& transform) {
        if (transform.IsIdentity()) {
            DrawTexture(texture, srcRect, dstX, dstY, dstW, dstH, tint);
            return;
        }
        const float textureWidth = texture.GetWidth();
        const float textureHeight = texture.GetHeight();
        if (textureWidth <= 0.0f || textureHeight <= 0.0f) {
            return;
        }

        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
        if (srcRect && !srcRect->IsEmpty()) {
            u0 = srcRect->x / textureWidth;
            v0 = srcRect->y / textureHeight;
            u1 = (srcRect->x + srcRect->w) / textureWidth;
            v1 = (srcRect->y + srcRect->h) / textureHeight;
        }
        if (transform.flipX) {
            std::swap(u0, u1);
        }
        if (transform.flipY) {
            std::swap(v0, v1);
        }

        const std::array<Vec2, 4> corners = MakeTransformedQuad(Rect{dstX, dstY, dstW, dstH}, transform);
        const std::array<GeometryVertex, 4> vertices = {
            GeometryVertex{corners[0].x, corners[0].y, tint, u0, v0},
            GeometryVertex{corners[1].x, corners[1].y, tint, u1, v0},
            GeometryVertex{corners[2].x, corners[2].y, tint, u0, v1},
            GeometryVertex{corners[3].x, corners[3].y, tint, u1, v1},
        };
        DrawGeometry(&texture, {vertices.data(), vertices.size()}, {kQuadIndices.data(), kQuadIndices.size()}, 0.0f, 0.0f);
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

    Optional<Rect> IRenderer::GetClipRect() const {
        if (_clipStack.empty()) {
            return std::nullopt;
        }
        return _clipStack.back();
    }

    bool IRenderer::IsClipEmpty() const {
        return !_clipStack.empty() && _clipStack.back().IsEmpty();
    }

}  // namespace BECore
