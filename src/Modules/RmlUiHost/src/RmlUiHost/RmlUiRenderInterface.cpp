#include "RmlUiRenderInterface.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>

namespace BECore {

    namespace {

        constexpr int kChannelMax = 255;
        constexpr size_t kBytesPerPixel = 4;

        /// The straight-alpha value of a premultiplied channel
        uint8_t Straight(int channel, int alpha) {
            return static_cast<uint8_t>(alpha == 0 ? 0 : std::min(kChannelMax, (channel * kChannelMax + alpha / 2) / alpha));
        }

        Color ToStraight(const Rml::ColourbPremultiplied& colour) {
            return Color{Straight(colour.red, colour.alpha), Straight(colour.green, colour.alpha), Straight(colour.blue, colour.alpha), colour.alpha};
        }

    }  // namespace

    Rml::CompiledGeometryHandle RmlUiRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
        Geometry geometry;
        geometry.vertices.reserve(vertices.size());
        for (const Rml::Vertex& vertex : vertices) {
            geometry.vertices.push_back(GeometryVertex{vertex.position.x, vertex.position.y, ToStraight(vertex.colour), vertex.tex_coord.x, vertex.tex_coord.y});
        }
        geometry.indices.assign(indices.begin(), indices.end());
        const Rml::CompiledGeometryHandle handle = _nextGeometry++;
        _geometries.emplace(handle, std::move(geometry));
        ++_stats.geometryCompiled;
        return handle;
    }

    void RmlUiRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) {
        const auto& renderer = CoreManager::GetRenderer();
        const auto found = _geometries.find(geometry);
        if (!renderer || found == _geometries.end()) {
            return;
        }
        ITexture* sprite = nullptr;
        if (texture != 0) {
            const auto foundTexture = _textures.find(texture);
            if (foundTexture == _textures.end()) {
                return;
            }
            sprite = foundTexture->second.Get();
        }
        const Geometry& data = found->second;
        renderer->DrawGeometry(sprite, {data.vertices.data(), data.vertices.size()}, {data.indices.data(), data.indices.size()}, translation.x, translation.y);
        ++_stats.drawCalls;
        _stats.verticesDrawn += static_cast<uint32_t>(data.vertices.size());
    }

    void RmlUiRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
        _geometries.erase(geometry);
    }

    Rml::TextureHandle RmlUiRenderInterface::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) {
        auto loaded = CoreManager::GetResourceManager().Load<ITexture>(FromStd(source));
        if (!loaded || loaded->GetState() != ResourceState::Loaded) {
            LOG_ERROR("RmlUi: cannot load the texture '{}'"_format(FromStd(source)));
            return 0;
        }
        textureDimensions.x = static_cast<int>(loaded->GetWidth());
        textureDimensions.y = static_cast<int>(loaded->GetHeight());
        const Rml::TextureHandle handle = _nextTexture++;
        _textures.emplace(handle, IntrusivePtr<ITexture>(loaded.Get()));
        ++_stats.texturesMade;
        return handle;
    }

    Rml::TextureHandle RmlUiRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) {
        const auto& renderer = CoreManager::GetRenderer();
        const size_t expected = static_cast<size_t>(sourceDimensions.x) * static_cast<size_t>(sourceDimensions.y) * kBytesPerPixel;
        if (!renderer || sourceDimensions.x <= 0 || sourceDimensions.y <= 0 || source.size() != expected) {
            return 0;
        }
        // The pixels are premultiplied, the renderer blends straight alpha
        Vector<uint8_t> pixels(source.begin(), source.end());
        for (size_t i = 0; i < pixels.size(); i += kBytesPerPixel) {
            const int alpha = pixels[i + 3];
            pixels[i] = Straight(pixels[i], alpha);
            pixels[i + 1] = Straight(pixels[i + 1], alpha);
            pixels[i + 2] = Straight(pixels[i + 2], alpha);
        }
        auto texture = renderer->CreateTexture(pixels.data(), static_cast<uint32_t>(sourceDimensions.x), static_cast<uint32_t>(sourceDimensions.y));
        if (!texture) {
            return 0;
        }
        const Rml::TextureHandle handle = _nextTexture++;
        _textures.emplace(handle, std::move(texture));
        ++_stats.texturesMade;
        return handle;
    }

    void RmlUiRenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
        _textures.erase(texture);
    }

    void RmlUiRenderInterface::ApplyScissor() {
        const auto& renderer = CoreManager::GetRenderer();
        if (!renderer) {
            return;
        }
        if (_clipPushed) {
            renderer->PopClipRect();
            _clipPushed = false;
        }
        if (_scissorEnabled) {
            // The region is in the pixels of the context; the view transform of the host (scale 1) moves it to output pixels
            const Rect region = renderer->GetViewTransform().ToOutput(static_cast<float>(_scissorRegion.Left()), static_cast<float>(_scissorRegion.Top()), static_cast<float>(_scissorRegion.Width()),
                                                                      static_cast<float>(_scissorRegion.Height()));
            renderer->PushClipRect(region);
            _clipPushed = true;
        }
    }

    void RmlUiRenderInterface::EnableScissorRegion(bool enable) {
        _scissorEnabled = enable;
        ApplyScissor();
    }

    void RmlUiRenderInterface::SetScissorRegion(Rml::Rectanglei region) {
        _scissorRegion = region;
        if (_scissorEnabled) {
            ApplyScissor();
        }
    }

    void RmlUiRenderInterface::EndPass() {
        _scissorEnabled = false;
        ApplyScissor();
    }

    void RmlUiRenderInterface::Clear() {
        _geometries.clear();
        _textures.clear();
        _clipPushed = false;
        _scissorEnabled = false;
    }

}  // namespace BECore
