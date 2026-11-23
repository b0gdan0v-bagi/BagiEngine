#pragma once

#include <BECore/Renderer/ITexture.h>
#include <BECore/UI/UIBoxTexture.h>

namespace BECore {

    class IRenderer;

    /**
     * @brief The textures of the boxes a renderer that pays for every triangle draws instead of their geometry: `CoreManager::GetUIBoxTextureCache()`.
     *
     * A box of a style is drawn as at most nine blits of one texture (`UIBoxTexture`) at whole output pixels, so a screen of skinned boxes costs the area they cover once
     * instead of a blended pass per layer. `Draw` returns false when the box cannot be drawn this way (a size or scale out of range, a texture that would be too large, a
     * renderer that cannot make textures) and the caller draws the mesh. Only the renderer that asks for `GeometryDetail::Reduced` is sent here.
     *
     * The textures belong to one renderer. A call with another renderer (told by `IRenderer::GetId()`) drops them all, and `Application::Cleanup()` calls Clear() before the
     * renderer is destroyed, like `FontAtlasLibrary`. The cache is bounded in entries and bytes; the least recently drawn texture goes first. A steady frame (the same boxes with the
     * same colours) makes no texture and allocates nothing; a new look makes one.
     *
     * Main thread only.
     */
    class UIBoxTextureCache {
    public:
        static constexpr size_t kMaxEntries = 128;
        static constexpr size_t kMaxBytes = size_t{64} << 20;

        /// Draws the box of @p area (UI units, with the view transform of @p renderer) with @p style and the colours of the draw; false when nothing was drawn
        [[nodiscard]] bool Draw(IRenderer& renderer, const Rect& area, const UIBoxStyle& style, const Color& fill, const Color& gradientEnd);

        /// Waits for the GPU of the current renderer, then drops every texture
        void Clear();

        [[nodiscard]] size_t GetEntryCount() const {
            return _entries.size();
        }
        [[nodiscard]] size_t GetBytes() const {
            return _bytes;
        }
        /// The textures made since the start: a steady frame leaves it as it is
        [[nodiscard]] uint64_t GetCreatedCount() const {
            return _created;
        }

    private:
        struct Entry {
            UIBoxTextureKey key;
            IntrusivePtr<ITexture> texture;
            uint64_t lastUsed = 0;
            size_t bytes = 0;
        };

        /// The texture of @p layout, made now when it does not exist; null when the renderer cannot make it
        [[nodiscard]] ITexture* Acquire(IRenderer& renderer, const UIBoxTextureLayout& layout);
        void Trim(const ITexture* keep);

        Vector<Entry> _entries;
        size_t _bytes = 0;
        uint64_t _tick = 0;
        uint64_t _created = 0;
        uint64_t _rendererId = 0;
        bool _rendererFailed = false;  ///< CreateTexture returned nothing: the renderer cannot, so every box is geometry (and no frame pays a raster for nothing)
    };

}  // namespace BECore
