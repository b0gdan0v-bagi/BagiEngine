#pragma once

#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>
#include <RmlUi/Core/RenderInterface.h>

namespace BECore {

    /// What the render interface did since ResetStats(): the numbers the comparison of the UI authoring paths reads
    struct RmlUiRenderStats {
        uint32_t geometryCompiled = 0;
        uint32_t drawCalls = 0;
        uint32_t verticesDrawn = 0;
        uint32_t texturesMade = 0;  ///< generated (font pages, decorators) and loaded
    };

    /**
     * @brief Rml::RenderInterface over IRenderer: geometry becomes IRenderer::DrawGeometry(), textures become ITexture, the scissor region becomes the clip rect.
     *
     * RmlUi's colors and generated pixels are premultiplied; IRenderer blends straight alpha, so both are converted once, when the geometry is compiled or the texture is
     * generated. Draws go to CoreManager::GetRenderer() and obey the view transform in effect, so the host sets a transform whose scale is 1 and whose offset is the
     * origin of the document (RmlUiDocumentComponent). The scissor region is in the pixels of the context, which that transform moves to output pixels.
     *
     * Not supported, and left to RmlUi's defaults (which draw nothing for them): CSS transforms, the clip mask (`overflow` with rounded corners), layers, filters and
     * shaders (`filter`, `box-shadow`, `backdrop-filter`, gradients). A document that needs them lacks them, silently on RmlUi's side.
     */
    class RmlUiRenderInterface final : public Rml::RenderInterface {
    public:
        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
        void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

        Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override;
        void ReleaseTexture(Rml::TextureHandle texture) override;

        void EnableScissorRegion(bool enable) override;
        void SetScissorRegion(Rml::Rectanglei region) override;

        /// Ends the pass of a context: a clip that RmlUi left on is popped, so it cannot leak into the next draw
        void EndPass();

        /// Releases every geometry and texture. Call it before the renderer goes away
        void Clear();

        [[nodiscard]] const RmlUiRenderStats& GetStats() const {
            return _stats;
        }
        void ResetStats() {
            _stats = {};
        }
        [[nodiscard]] size_t GetTextureCount() const {
            return _textures.size();
        }
        [[nodiscard]] size_t GetGeometryCount() const {
            return _geometries.size();
        }

    private:
        struct Geometry {
            Vector<GeometryVertex> vertices;
            Vector<int> indices;
        };

        void ApplyScissor();

        eastl::hash_map<Rml::CompiledGeometryHandle, Geometry> _geometries;
        eastl::hash_map<Rml::TextureHandle, IntrusivePtr<ITexture>> _textures;
        Rml::CompiledGeometryHandle _nextGeometry = 1;
        Rml::TextureHandle _nextTexture = 1;

        bool _scissorEnabled = false;
        bool _clipPushed = false;
        Rml::Rectanglei _scissorRegion;
        RmlUiRenderStats _stats;
    };

}  // namespace BECore
