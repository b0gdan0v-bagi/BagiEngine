#pragma once

#include <BECore/Text/FontAtlas.h>
#include <BECore/Text/TextSettings.h>

namespace BECore {

    class IRenderer;

    /// Which font a text asks for: a file path (wins when not empty), else the face of a family at a weight
    struct FontRequest {
        PoolString font;
        PoolString family;  ///< empty: the default family of TextConfig.xml
        int weight = 400;

        bool operator==(const FontRequest&) const = default;
    };

    /**
     * @brief The glyph atlases that exist, one per (font, pixel size, outline pixels): `CoreManager::GetFontAtlasLibrary()`.
     *
     * Atlases are made on first use. A new plain atlas draws the `preload` ranges of the TextSettings up front; an
     * outline atlas (outline pixels above 0) is not preloaded, its glyphs come on first use. The size is a
     * whole number of pixels (QuantizeSize()): the effective size of a text is its font size times the scale of its
     * canvas times the pixel density, rounded, so a text is rasterized at the size it is shown at, and the atlas stays
     * small however the scale moves.
     *
     * The atlases hold GPU textures of one renderer. A call with a renderer other than the one the atlases were made
     * for drops them all (a renderer is told from another by IRenderer::GetId()). `Application::Cleanup()` calls Clear()
     * before the renderer is destroyed: Clear() waits for the GPU, and a texture freed after its device is gone crashes.
     * It is a direct call, not a handler of ApplicationCleanUpEvent: EnTT calls the handlers of an event in the reverse
     * order of their subscription, so the renderer's own handler would run first.
     *
     * Main thread only.
     */
    class FontAtlasLibrary {
    public:
        /// The biggest pixel size of an atlas: a page would not hold the glyphs of anything larger anyway
        static constexpr int kMaxPixelSize = 512;

        FontAtlasLibrary() = default;

        /// Reads `config/TextConfig.xml` (the defaults when it is missing)
        void Initialize();

        [[nodiscard]] const TextSettings& GetSettings() const {
            return _settings;
        }
        void SetSettings(TextSettings settings) {
            _settings = std::move(settings);
        }

        /// The font file of @p request: its `font` when set, else `TextSettings::ResolveFontPath` of its family and weight. The one rule of
        /// every text, so a label and a scene text choose the same face. @p known is as in TextSettings::ResolveFontPath (always true when `font` is set)
        [[nodiscard]] PoolString ResolveFontPath(const FontRequest& request, bool* known = nullptr) const;

        /// The effective size in pixels as a whole number from 1 to kMaxPixelSize
        [[nodiscard]] static int QuantizeSize(float effectivePixelSize);

        /// An outline width in output pixels as a whole number: 0 stays 0, anything above 0 is at least 1, at most kMaxOutlinePixels
        [[nodiscard]] static int QuantizeOutline(float widthInPixels);

        /// The atlas of @p font at @p pixelSize with the pictures grown by @p outlinePixels (0: the plain glyphs), made now when it
        /// does not exist; only a plain one is preloaded
        [[nodiscard]] IntrusivePtr<FontAtlas> Get(IRenderer& renderer, const IntrusivePtr<Font>& font, int pixelSize, int outlinePixels = 0);

        /// Waits for the GPU of the current renderer, then drops every atlas
        void Clear();

        [[nodiscard]] size_t GetAtlasCount() const {
            return _atlases.size();
        }

    private:
        struct Entry {
            IntrusivePtr<Font> font;
            int pixelSize = 0;
            int outlinePixels = 0;
            IntrusivePtr<FontAtlas> atlas;
        };

        TextSettings _settings = TextSettings::Default();
        eastl::vector<Entry> _atlases;
        uint64_t _rendererId = 0;
    };

}  // namespace BECore
