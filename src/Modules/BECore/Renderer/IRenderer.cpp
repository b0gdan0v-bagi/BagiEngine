#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
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
            callback(std::unexpected(String("this renderer backend cannot read its output back")));
        }
    }

    String IRenderer::GetNoGpuDeviceReason() const {
        return String("this renderer backend has no GPU device");
    }

    std::expected<IGpuDevice*, String> RequireGpuDevice(IRenderer& renderer) {
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

    void IRenderer::DrawGeometry(ITexture* /*texture*/, Span<const GeometryVertex> /*vertices*/, Span<const int> /*indices*/, float /*translateX*/, float /*translateY*/) {
        if (!_geometryWarned) {
            _geometryWarned = true;
            LOG_WARNING("IRenderer: this backend cannot draw geometry, so what needs it is not drawn");
        }
    }

    namespace {
        // Two triangles over the corners in the order of MakeTransformedQuad(): top-left, top-right, bottom-left, bottom-right
        constexpr Array<int, 6> kQuadIndices = {0, 1, 2, 2, 1, 3};
        constexpr size_t kQuadCorners = 4;

        // The UVs of the four corners for the source rect over the texture size (the whole texture when the rect is null or empty), the flips
        // swapping them. The one place both the immediate default and the batch take them from. Empty for a texture without size.
        Optional<Array<Vec2, 4>> MakeQuadUvs(const ITexture& texture, const Rect* srcRect, bool flipX, bool flipY) {
            const float textureWidth = texture.GetWidth();
            const float textureHeight = texture.GetHeight();
            if (textureWidth <= 0.0f || textureHeight <= 0.0f) {
                return std::nullopt;
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
            if (flipX) {
                std::swap(u0, u1);
            }
            if (flipY) {
                std::swap(v0, v1);
            }
            return Array<Vec2, 4>{Vec2{u0, v0}, Vec2{u1, v0}, Vec2{u0, v1}, Vec2{u1, v1}};
        }

        constexpr Array<Vec2, 4> kRectUvs = {Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, Vec2{0.0f, 1.0f}, Vec2{1.0f, 1.0f}};

        Array<Vec2, 4> RectCorners(const Rect& rect) {
            return {Vec2{rect.x, rect.y}, Vec2{rect.x + rect.w, rect.y}, Vec2{rect.x, rect.y + rect.h}, Vec2{rect.x + rect.w, rect.y + rect.h}};
        }

        Array<Vec2, 4> MapToOutput(const Array<Vec2, 4>& corners, const ViewTransform& view) {
            Array<Vec2, 4> mapped = corners;
            for (Vec2& corner : mapped) {
                corner = Vec2{corner.x * view.scale + view.offsetX, corner.y * view.scale + view.offsetY};
            }
            return mapped;
        }
    }  // namespace

    void IRenderer::DrawTransformedRect(float x, float y, float w, float h, const Color& color, const DrawTransform& transform) {
        if (transform.IsIdentity()) {
            DrawFilledRect(x, y, w, h, color);
            return;
        }
        if (_batchingEnabled) {
            BatchTransformedRect(x, y, w, h, color, transform);
            return;
        }
        const auto corners = MakeTransformedQuad(Rect{x, y, w, h}, transform);
        const Array<GeometryVertex, 4> vertices = {
            GeometryVertex{corners[0].x, corners[0].y, color, kRectUvs[0].x, kRectUvs[0].y},
            GeometryVertex{corners[1].x, corners[1].y, color, kRectUvs[1].x, kRectUvs[1].y},
            GeometryVertex{corners[2].x, corners[2].y, color, kRectUvs[2].x, kRectUvs[2].y},
            GeometryVertex{corners[3].x, corners[3].y, color, kRectUvs[3].x, kRectUvs[3].y},
        };
        DrawGeometry(nullptr, {vertices.data(), vertices.size()}, {kQuadIndices.data(), kQuadIndices.size()}, 0.0f, 0.0f);
    }

    void IRenderer::DrawTransformedTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint, const DrawTransform& transform) {
        if (transform.IsIdentity()) {
            DrawTexture(texture, srcRect, dstX, dstY, dstW, dstH, tint);
            return;
        }
        if (_batchingEnabled) {
            BatchTransformedTexture(texture, srcRect, dstX, dstY, dstW, dstH, tint, transform);
            return;
        }
        const Optional<Array<Vec2, 4>> uvs = MakeQuadUvs(texture, srcRect, transform.flipX, transform.flipY);
        if (!uvs) {
            return;
        }
        const auto corners = MakeTransformedQuad(Rect{dstX, dstY, dstW, dstH}, transform);
        const Array<GeometryVertex, 4> vertices = {
            GeometryVertex{corners[0].x, corners[0].y, tint, (*uvs)[0].x, (*uvs)[0].y},
            GeometryVertex{corners[1].x, corners[1].y, tint, (*uvs)[1].x, (*uvs)[1].y},
            GeometryVertex{corners[2].x, corners[2].y, tint, (*uvs)[2].x, (*uvs)[2].y},
            GeometryVertex{corners[3].x, corners[3].y, tint, (*uvs)[3].x, (*uvs)[3].y},
        };
        DrawGeometry(&texture, {vertices.data(), vertices.size()}, {kQuadIndices.data(), kQuadIndices.size()}, 0.0f, 0.0f);
    }

    void IRenderer::BatchRect(float x, float y, float w, float h, const Color& color) {
        AppendQuad(nullptr, RectCorners(GetViewTransform().ToOutput(x, y, w, h)), kRectUvs, color);
    }

    void IRenderer::BatchTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint) {
        if (const Optional<Array<Vec2, 4>> uvs = MakeQuadUvs(texture, srcRect, false, false)) {
            AppendQuad(&texture, RectCorners(GetViewTransform().ToOutput(dstX, dstY, dstW, dstH)), *uvs, tint);
        }
    }

    void IRenderer::BatchTransformedRect(float x, float y, float w, float h, const Color& color, const DrawTransform& transform) {
        AppendQuad(nullptr, MapToOutput(MakeTransformedQuad(Rect{x, y, w, h}, transform), GetViewTransform()), kRectUvs, color);
    }

    void IRenderer::BatchTransformedTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint, const DrawTransform& transform) {
        if (const Optional<Array<Vec2, 4>> uvs = MakeQuadUvs(texture, srcRect, transform.flipX, transform.flipY)) {
            AppendQuad(&texture, MapToOutput(MakeTransformedQuad(Rect{dstX, dstY, dstW, dstH}, transform), GetViewTransform()), *uvs, tint);
        }
    }

    void IRenderer::AppendQuad(ITexture* texture, const Array<Vec2, 4>& corners, const Array<Vec2, 4>& uvs, const Color& color) {
        if (IsClipEmpty()) {
            return;
        }
        const BlendMode blend = GetBlendMode();
        const bool open = _batch.quads > 0;
        const bool sameKey = open && _batch.texture.Get() == texture && _batch.blend == blend;
        // Asked when a quad opens a batch for a texture; a refused quad is dropped without disturbing the batch that is open
        if (!sameKey && texture != nullptr && !CanDrawTexture(*texture)) {
            return;
        }
        if (open && (!sameKey || _batch.quads >= kMaxBatchQuads)) {
            Flush();
        }
        if (_batch.quads == 0) {
            _batch.texture = IntrusivePtr<ITexture>(texture);
            _batch.blend = blend;
        }

        const Color vertexColor = ApplyOpacity(color);
        const int first = static_cast<int>(_batch.vertices.size());
        for (size_t i = 0; i < kQuadCorners; ++i) {
            _batch.vertices.push_back(GeometryVertex{corners[i].x, corners[i].y, vertexColor, uvs[i].x, uvs[i].y});
        }
        for (const int index : kQuadIndices) {
            _batch.indices.push_back(first + index);
        }
        ++_batch.quads;
        CountPrimitive(kQuadTriangles);
    }

    void IRenderer::Flush() {
        if (_batch.quads == 0) {
            return;
        }
        CountSubmission(_batch.texture.Get());
        {
            Optional<FrameStatsCollector::StageScope> timed;
            if (_countersPaused == 0) {
                timed.emplace(CoreManager::GetFrameStats(), FrameStage::Submit);
            }
            SubmitBatch(_batch);
        }
        _batch.Reset();
    }

    void IRenderer::SetBatchingEnabled(bool enabled) {
        if (!enabled) {
            Flush();
        }
        _batchingEnabled = enabled;
    }

    void IRenderer::PauseCounters() {
        Flush();
        ++_countersPaused;
    }

    void IRenderer::ResumeCounters() {
        Flush();
        --_countersPaused;
    }

    void IRenderer::PushClipRect(const Rect& rect) {
        Flush();  // the batch was drawn under the clip that is about to change
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
        Flush();
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
