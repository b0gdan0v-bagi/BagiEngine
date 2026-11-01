#pragma once

#include <BECore/Resource/IResourceLoader.h>

namespace BECore {

    class Font;

    /**
     * @brief Loads .ttf and .otf files into Font resources.
     *
     * Reads the bytes and parses the font on the CPU only, so it may run on a pool thread (`LoadAsync`). Glyph textures
     * are made later, lazily, on the main thread (FontAtlas).
     */
    class FontLoader : public IResourceLoader {
        BE_CLASS(FontLoader)
    public:
        FontLoader() = default;
        ~FontLoader() override = default;

        bool CanLoad(eastl::string_view extension) const override;
        Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) override;
        IntrusivePtr<IResource> LoadSync(PoolString path) override;

    private:
        IntrusivePtr<Font> LoadInternal(PoolString path);
    };

}  // namespace BECore
