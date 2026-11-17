#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <Widgets/TextureEditor/SpriteAtlasPacker.h>

namespace BECore {

    /**
     * @brief The "Atlas" section of the Sprite Library panel: the atlas group of the selected sprite, the pack settings, a summary of the
     * group, Pack and Unpack. The rules and the files are SpriteAtlas's; this is the ImGui over them and the refresh of what showed the
     * old pixels (the texture cache and the sprites of the active scene).
     *
     * The pack settings live only here: a repack with other settings is another file, and nothing but the group name is saved.
     * Numbers and the summary are formatted by ImGui into its own buffers and the texts are fixed buffers, so a steady frame allocates nothing.
     */
    class SpriteAtlasPanel {
    public:
        /**
         * @brief Draws the section for the selected sprite @p entry (a copy taken this frame).
         * @return Whether the library changed; the caller drops its copy of the entry and marks the library unsaved.
         */
        bool Render(TextureLibrary& library, const SpriteEntry& entry);

        /// The pack settings of the section, for the preview that shows what Pack would make with them
        [[nodiscard]] const PackSettings& Settings() const {
            return _settings;
        }

    private:
        /// The sprite and the group the name field was last loaded for: the field is reloaded when either moves
        void SyncNameField(const SpriteEntry& entry, PoolString group);
        bool ApplyGroupName(TextureLibrary& library, const SpriteEntry& entry);
        bool Pack(TextureLibrary& library, PoolString group);
        bool Unpack(TextureLibrary& library, PoolString group);
        void SetStatus(StringView text, bool isError);

        static constexpr size_t kNameSize = 65;
        static constexpr size_t kStatusSize = 256;

        PackSettings _settings;
        char _name[kNameSize] = {};
        PoolString _boundSprite;
        PoolString _boundGroup;
        bool _hasError = false;
        char _status[kStatusSize] = {};
    };

}  // namespace BECore
