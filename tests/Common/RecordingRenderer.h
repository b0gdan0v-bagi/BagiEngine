#pragma once

#include <BECore/Math/Color.h>
#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Renderer/Gpu/IGpuDevice.h>
#include <BECore/Renderer/IRenderTarget.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace BECore::Tests {

    /// A texture that holds the pixels it was made from. Made by RecordingRenderer::CreateTexture().
    class RecordedTexture : public ITexture {
    public:
        RecordedTexture(Vector<uint8_t> rgba, uint32_t width, uint32_t height) : _rgba(std::move(rgba)), _width(width), _height(height) {}

        ResourceState GetState() const override {
            return ResourceState::Loaded;
        }
        PoolString GetPath() const override {
            return "<recorded>"_intern;
        }
        uint64_t GetMemoryUsage() const override {
            return _rgba.size();
        }
        PoolString GetTypeName() const override {
            return "RecordedTexture"_intern;
        }
        float GetWidth() const override {
            return static_cast<float>(_width);
        }
        float GetHeight() const override {
            return static_cast<float>(_height);
        }
        void* GetNativeHandle() const override {
            return nullptr;
        }

        const Vector<uint8_t>& GetPixels() const {
            return _rgba;
        }

    private:
        Vector<uint8_t> _rgba;
        uint32_t _width = 0;
        uint32_t _height = 0;
    };

    CORE_ENUM(DrawKind, uint8_t, Rect, Texture, Geometry, Batch)

    /// One call of DrawFilledRect(), DrawTexture() or DrawGeometry(), or one submitted batch, as the backend would see it
    struct DrawCall {
        DrawKind kind = DrawKind::Rect;
        Rect rect;    ///< the destination in output pixels, after the view transform; for geometry the box around its vertices
        Color color;  ///< the fill color of a rect, the tint of a texture, the color of the first vertex of geometry
        const ITexture* texture = nullptr;
        Optional<Rect> source;               ///< the source rect of a texture draw, as it was passed
        Optional<Rect> clip;                 ///< the clip in effect for this call
        BlendMode blend = BlendMode::Alpha;  ///< the blend mode in effect for this call
        Vector<GeometryVertex> vertices;     ///< geometry and batch: the vertices with their positions in output pixels (translation and view transform applied)
        Vector<int> indices;                 ///< geometry: the indices as they were passed; batch: four vertices and six indices per quad
        uint32_t quads = 0;                  ///< batch: how many quads it holds
    };

    /**
     * @brief An IRenderer that draws nothing, records what it was asked to draw and reports a settable output size.
     *
     * Needs no window or GPU, so layout and draw-order tests can run anywhere. It applies the view transform and the
     * clip like a backend does, which lets a test read the calls in output pixels. Textures it makes keep their pixels.
     *
     * Batching is off until SetBatchingEnabled(true); then the rect, texture and transformed draws gather into DrawKind::Batch calls that are
     * recorded in `calls` where the batch was submitted, so a test reads the order a backend would see. DrawGeometry() and the frame and target
     * calls submit the open batch first, as a backend does.
     *
     * An allocation test calls ClearCalls() between frames: it keeps the storage of the calls, so from the second equal frame on the recorder
     * allocates nothing and the test counts only the code it measures. `calls.clear()` gives the geometry storage up.
     */
    class RecordingRenderer : public IRenderer {
    public:
        static constexpr size_t kGeometryIndicesPerTriangle = 3;

        bool Initialize(IMainWindow&) override {
            return true;
        }
        void Destroy() override {
            DiscardBatch();
        }
        void BeginFrame(const Color& clearColor) override {
            Flush();
            frameClearColor = clearColor;
            ++beginFrameCalls;
        }
        void EndFrame() override {
            Flush();
        }
        void Clear(const Color&) override {
            Flush();
        }
        void Present() override {
            Flush();
        }
        void WaitIdle() override {
            ++waitIdleCalls;
        }

        void DrawFilledRect(float x, float y, float w, float h, const Color& color) override {
            if (IsBatchingEnabled()) {
                BatchRect(x, y, w, h, color);
                return;
            }
            if (IsClipEmpty()) {
                return;
            }
            DrawCall call;
            call.kind = DrawKind::Rect;
            call.rect = GetViewTransform().ToOutput(x, y, w, h);
            call.color = ApplyOpacity(color);
            call.clip = GetClipRect();
            call.blend = GetBlendMode();
            calls.push_back(call);
            CountDraw(nullptr, kQuadTriangles);
        }

        using IRenderer::DrawTexture;
        void DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint) override {
            if (IsBatchingEnabled()) {
                BatchTexture(texture, srcRect, dstX, dstY, dstW, dstH, tint);
                return;
            }
            if (IsClipEmpty()) {
                return;
            }
            DrawCall call;
            call.kind = DrawKind::Texture;
            call.rect = GetViewTransform().ToOutput(dstX, dstY, dstW, dstH);
            call.color = ApplyOpacity(tint);
            call.texture = &texture;
            if (srcRect) {
                call.source = *srcRect;
            }
            call.clip = GetClipRect();
            call.blend = GetBlendMode();
            calls.push_back(call);
            CountDraw(&texture, kQuadTriangles);
        }

        void DrawGeometry(ITexture* texture, Span<const GeometryVertex> vertices, Span<const int> indices, float translateX, float translateY) override {
            Flush();
            if (IsClipEmpty() || vertices.empty()) {
                return;
            }
            DrawCall call = MakeCallWithSpareStorage();
            call.kind = DrawKind::Geometry;
            call.texture = texture;
            call.color = ApplyOpacity(vertices[0].color);
            call.clip = GetClipRect();
            call.blend = GetBlendMode();
            const ViewTransform& view = GetViewTransform();
            float left = 0.0f;
            float top = 0.0f;
            float right = 0.0f;
            float bottom = 0.0f;
            for (const GeometryVertex& vertex : vertices) {
                GeometryVertex mapped = vertex;
                mapped.color = ApplyOpacity(vertex.color);
                mapped.x = (vertex.x + translateX) * view.scale + view.offsetX;
                mapped.y = (vertex.y + translateY) * view.scale + view.offsetY;
                const bool first = call.vertices.empty();
                left = first ? mapped.x : std::min(left, mapped.x);
                top = first ? mapped.y : std::min(top, mapped.y);
                right = first ? mapped.x : std::max(right, mapped.x);
                bottom = first ? mapped.y : std::max(bottom, mapped.y);
                call.vertices.push_back(mapped);
            }
            call.rect = Rect{left, top, right - left, bottom - top};
            call.indices.assign(indices.begin(), indices.end());
            calls.push_back(std::move(call));
            CountDraw(texture, indices.size() / kGeometryIndicesPerTriangle);
        }

        IntrusivePtr<ITexture> CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height) override {
            if (!rgba || width == 0 || height == 0) {
                return {};
            }
            ++texturesCreated;
            return New<RecordedTexture>(Vector<uint8_t>(rgba, rgba + static_cast<size_t>(width) * height * 4), width, height);
        }

        IntrusivePtr<IRenderTarget> CreateRenderTarget(uint32_t, uint32_t) override {
            return {};
        }
        void SetRenderTarget(IRenderTarget*) override {
            Flush();
        }
        void UnsetRenderTarget() override {
            Flush();
        }

        float GetOutputWidth() const override {
            return width;
        }
        float GetOutputHeight() const override {
            return height;
        }

        /// Makes GetGpuDevice() answer with @p device (a RecordingGpuDevice), so the 3D renderer has a renderer to ask; none by default, like the SDL renderer
        void SetGpuDevice(IntrusivePtr<IGpuDevice> device) {
            _gpuDevice = std::move(device);
        }
        IGpuDevice* GetGpuDevice() override {
            return _gpuDevice.Get();
        }
        String GetNoGpuDeviceReason() const override {
            return String("the recording renderer has no GPU device set");
        }

        void ClearCalls() {
            // Last call first: the spare lists are taken from the back, so each draw of an equal frame gets the list that already fits it
            for (size_t i = calls.size(); i > 0; --i) {
                DrawCall& call = calls[i - 1];
                if (call.kind == DrawKind::Geometry || call.kind == DrawKind::Batch) {
                    call.vertices.clear();
                    call.indices.clear();
                    _spareGeometry.push_back(GeometryStorage{std::move(call.vertices), std::move(call.indices)});
                }
            }
            calls.clear();
        }

        float width = 800.0f;
        float height = 600.0f;
        Vector<DrawCall> calls;
        uint32_t texturesCreated = 0;
        uint32_t waitIdleCalls = 0;
        Color frameClearColor{0, 0, 0, 0};  ///< the colour of the last BeginFrame()
        uint32_t beginFrameCalls = 0;

    protected:
        void SubmitBatch(const RenderBatch& batch) override {
            DrawCall call = MakeCallWithSpareStorage();
            call.kind = DrawKind::Batch;
            call.texture = batch.texture.Get();
            call.clip = GetClipRect();
            call.blend = batch.blend;
            call.quads = batch.quads;
            call.vertices.assign(batch.vertices.begin(), batch.vertices.end());
            call.indices.assign(batch.indices.begin(), batch.indices.end());
            call.color = call.vertices[0].color;
            float left = call.vertices[0].x;
            float top = call.vertices[0].y;
            float right = left;
            float bottom = top;
            for (const GeometryVertex& vertex : call.vertices) {
                left = std::min(left, vertex.x);
                top = std::min(top, vertex.y);
                right = std::max(right, vertex.x);
                bottom = std::max(bottom, vertex.y);
            }
            call.rect = Rect{left, top, right - left, bottom - top};
            calls.push_back(std::move(call));
        }

        /// Like a backend: only a texture this renderer made can be drawn
        [[nodiscard]] bool CanDrawTexture(const ITexture& texture) const override {
            return dynamic_cast<const RecordedTexture*>(&texture) != nullptr;
        }

    private:
        DrawCall MakeCallWithSpareStorage() {
            DrawCall call;
            if (!_spareGeometry.empty()) {
                call.vertices = std::move(_spareGeometry.back().vertices);
                call.indices = std::move(_spareGeometry.back().indices);
                _spareGeometry.pop_back();
            }
            return call;
        }

        struct GeometryStorage {
            Vector<GeometryVertex> vertices;
            Vector<int> indices;
        };

        IntrusivePtr<IGpuDevice> _gpuDevice;
        Vector<GeometryStorage> _spareGeometry;
    };

    /// An opaque white texture of @p width by @p height made by @p renderer
    inline IntrusivePtr<ITexture> MakeTexture(RecordingRenderer& renderer, uint32_t width = 1, uint32_t height = 1) {
        constexpr uint8_t kWhite = 255;
        constexpr size_t kBytesPerPixel = 4;
        const Vector<uint8_t> pixels(static_cast<size_t>(width) * height * kBytesPerPixel, kWhite);
        return renderer.CreateTexture(pixels.data(), width, height);
    }

}  // namespace BECore::Tests
