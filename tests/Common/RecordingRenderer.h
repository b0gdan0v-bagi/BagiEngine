#pragma once

#include <BECore/Renderer/IRenderTarget.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>

namespace BECore::Tests {

    /// A texture that holds the pixels it was made from. Made by RecordingRenderer::CreateTexture().
    class RecordedTexture : public ITexture {
    public:
        RecordedTexture(eastl::vector<uint8_t> rgba, uint32_t width, uint32_t height) : _rgba(std::move(rgba)), _width(width), _height(height) {}

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

        const eastl::vector<uint8_t>& GetPixels() const {
            return _rgba;
        }

    private:
        eastl::vector<uint8_t> _rgba;
        uint32_t _width = 0;
        uint32_t _height = 0;
    };

    CORE_ENUM(DrawKind, uint8_t, Rect, Texture)

    /// One call of DrawFilledRect() or DrawTexture() as the backend would see it
    struct DrawCall {
        DrawKind kind = DrawKind::Rect;
        Rect rect;    ///< the destination in output pixels, after the view transform
        Color color;  ///< the fill color of a rect, the tint of a texture
        const ITexture* texture = nullptr;
        std::optional<Rect> source;  ///< the source rect of a texture draw, as it was passed
        std::optional<Rect> clip;    ///< the clip in effect for this call
    };

    /**
     * @brief An IRenderer that draws nothing, records what it was asked to draw and reports a settable output size.
     *
     * Needs no window or GPU, so layout and draw-order tests can run anywhere. It applies the view transform and the
     * clip like a backend does, which lets a test read the calls in output pixels. Textures it makes keep their pixels.
     */
    class RecordingRenderer : public IRenderer {
    public:
        bool Initialize(IMainWindow&) override {
            return true;
        }
        void Destroy() override {}
        void BeginFrame() override {}
        void EndFrame() override {}
        void Clear(const Color&) override {}
        void Present() override {}

        void DrawFilledRect(float x, float y, float w, float h, const Color& color) override {
            if (IsClipEmpty()) {
                return;
            }
            DrawCall call;
            call.kind = DrawKind::Rect;
            call.rect = GetViewTransform().ToOutput(x, y, w, h);
            call.color = color;
            call.clip = GetClipRect();
            calls.push_back(call);
        }

        using IRenderer::DrawTexture;
        void DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint) override {
            if (IsClipEmpty()) {
                return;
            }
            DrawCall call;
            call.kind = DrawKind::Texture;
            call.rect = GetViewTransform().ToOutput(dstX, dstY, dstW, dstH);
            call.color = tint;
            call.texture = &texture;
            if (srcRect) {
                call.source = *srcRect;
            }
            call.clip = GetClipRect();
            calls.push_back(call);
        }

        IntrusivePtr<ITexture> CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height) override {
            if (!rgba || width == 0 || height == 0) {
                return {};
            }
            ++texturesCreated;
            return New<RecordedTexture>(eastl::vector<uint8_t>(rgba, rgba + static_cast<size_t>(width) * height * 4), width, height);
        }

        IntrusivePtr<IRenderTarget> CreateRenderTarget(uint32_t, uint32_t) override {
            return {};
        }
        void SetRenderTarget(IRenderTarget*) override {}
        void UnsetRenderTarget() override {}

        float GetOutputWidth() const override {
            return width;
        }
        float GetOutputHeight() const override {
            return height;
        }

        void ClearCalls() {
            calls.clear();
        }

        float width = 800.0f;
        float height = 600.0f;
        eastl::vector<DrawCall> calls;
        uint32_t texturesCreated = 0;
    };

}  // namespace BECore::Tests
