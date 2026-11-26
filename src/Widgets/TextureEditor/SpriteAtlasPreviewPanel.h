#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <Widgets/TextureEditor/SpriteAtlasPacker.h>
#include <Widgets/TextureEditor/SpriteAtlasPreview.h>
#include <Widgets/TextureEditor/SpriteTextureCache.h>
#include <cstdint>

namespace BECore {

    /// `Current`: the packed members as the library has them, on the atlas file. `Plan`: what Pack would make with the panel's settings, from the sources.
    CORE_ENUM(AtlasPreviewMode, uint8_t, Current, Plan)

    /**
     * @brief The mode of the "Atlas preview" section as the Sprite Library asset binds it (`sprites.preview`)
     *
     * `mode` is written by the radios; the model shows `Plan` while no member of the group is packed (`Current` is off then) and keeps the choice, so the
     * section returns to it once a member is packed.
     */
    struct SpriteAtlasPreviewView {
        BE_CLASS(SpriteAtlasPreviewView)

        BE_REFLECT_FIELD AtlasPreviewMode mode = AtlasPreviewMode::Current;
        BE_REFLECT_FIELD bool anyPacked = false;
        /// The selected sprite has a group: the section shows the picture only then
        BE_REFLECT_FIELD bool hasGroup = false;
    };

    /**
     * @brief The "Atlas preview" section of the Sprite Library panel: the atlas of the selected sprite's group drawn rect by rect with
     * its names, its size and its fill ratio. The model is `SpriteAtlasPreview`; this draws what it says: the body of the section (`DrawBody`,
     * the slot `spriteAtlasPreview`); the asset draws the header and the mode radios, and the model keeps the mode.
     *
     * The preview is made again only when `AtlasPreviewKey`, the mode, the pack settings (Plan) or the size of a texture it read
     * moved, so a steady frame allocates nothing. `Plan` takes the sizes from the loaded textures and writes no pixel.
     */
    class SpriteAtlasPreviewPanel {
    public:
        /**
         * @brief Draws the error line, the picture, the size and the fill of the atlas of @p group in @p mode; @p settings are the Atlas section's.
         * @return The name of the sprite whose rect was clicked this frame, else empty
         */
        PoolString DrawBody(const TextureLibrary& library, PoolString group, AtlasPreviewMode mode, const PackSettings& settings, SpriteTextureCache& textures);

    private:
        /// A texture the current preview was made from and the size it had, compared every frame so a load, an evict or a rewritten file makes the preview again
        struct SeenTexture {
            PoolString path;
            ImageSize size;
        };

        void BindGroup(PoolString group);
        void EnsureBuilt(const TextureLibrary& library, AtlasPreviewMode mode, const PackSettings& settings, SpriteTextureCache& textures);
        void Rebuild(const TextureLibrary& library, AtlasPreviewMode mode, SpriteTextureCache& textures);
        bool TexturesMoved(SpriteTextureCache& textures) const;
        /// @return The index of the rect clicked, or -1
        int DrawPicture(SpriteTextureCache& textures);

        static constexpr size_t kErrorSize = 256;

        PoolString _group;
        PoolString _atlasPath;  // `AtlasTexturePath(_group)`, made when the group changes
        AtlasPreview _preview;
        Vector<SeenTexture> _seen;
        bool _built = false;
        uint64_t _builtKey = 0;
        PoolString _builtGroup;
        AtlasPreviewMode _builtMode = AtlasPreviewMode::Current;
        PackSettings _builtSettings;
        bool _hasError = false;
        char _error[kErrorSize] = {};
    };

}  // namespace BECore
