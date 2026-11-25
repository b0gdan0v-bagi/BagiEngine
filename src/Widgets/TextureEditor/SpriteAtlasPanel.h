#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <Widgets/TextureEditor/SpriteAtlasPacker.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief The "Atlas" section as the Sprite Library asset binds it (`sprites.atlas`)
     *
     * `name` (applied by its `commitCommand`), `padding` and `sizeMode` are written by the controls and taken by `SpriteAtlasPanel::Sync`, which clamps
     * the padding in place; the rest is written by the model.
     */
    struct SpriteAtlasView {
        BE_CLASS(SpriteAtlasView)

        /// The group of the selected sprite; empty: none
        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD int32_t padding = kDefaultAtlasPadding;
        BE_REFLECT_FIELD AtlasSizeMode sizeMode = AtlasSizeMode::PowerOfTwo;
        /// The selected sprite is packed: its group waits for Unpack (`packedText` says so)
        BE_REFLECT_FIELD bool packed = false;
        BE_REFLECT_FIELD String packedText;
        BE_REFLECT_FIELD bool hasGroup = false;
        /// The group's members and how many are packed; empty with no group
        BE_REFLECT_FIELD String summary;
        BE_REFLECT_FIELD bool canPack = false;
        BE_REFLECT_FIELD bool canUnpack = false;
        /// The answer of the last name change, Pack or Unpack; cleared when another sprite is selected
        BE_REFLECT_FIELD String status;
        BE_REFLECT_FIELD bool statusError = false;
        BE_REFLECT_FIELD bool hasStatus = false;
    };

    /// Where Pack writes (`io`) and how Unpack checks that a source file is still there; a test's own folder instead of the game's
    struct SpriteAtlasFiles {
        AtlasIo io;
        FunctionRef<bool(PoolString path)> exists;
    };

    /**
     * @brief The "Atlas" section of the Sprite Library panel: the atlas group of the selected sprite, the pack settings, a summary of the
     * group, Pack and Unpack. The rules and the files are SpriteAtlas's; this holds the section's state over them, the refresh of what showed the old
     * pixels (the texture cache and the sprites of the active scene), and takes its fields from `Render` (the ImGui form of `TextureEditorWidget`) or
     * from `Sync` (the asset's `SpriteAtlasView`).
     *
     * The fields are the pack settings; Pack records them in the group's entries and the section loads them back when the selected
     * sprite's group changes, so a repack here and an Accept of the Generate panel make the same file.
     * Numbers and the summary are formatted by ImGui into its own buffers and the texts are fixed buffers, so a steady frame allocates nothing.
     */
    class SpriteAtlasPanel {
    public:
        /**
         * @brief Draws the section for the selected sprite @p entry (a copy taken this frame).
         * @return Whether the library changed; the caller drops its copy of the entry and marks the library unsaved.
         */
        bool Render(TextureLibrary& library, const SpriteEntry& entry);

        /**
         * @brief Takes the fields of @p view for the selected sprite @p entry and writes what the section shows.
         *
         * When the sprite or its group moved, the name field is the group again and the settings follow the group's pack record (a group with no
         * record keeps them); the texts are made again only when what they show moved, so a steady call allocates nothing.
         */
        void Sync(SpriteAtlasView& view, const TextureLibrary& library, const SpriteEntry& entry);

        /**
         * @brief Moves @p entry into the group @p name (empty: none), the name field's commit.
         * @return Whether the library changed; a refused name is the status error
         */
        bool ApplyGroupName(TextureLibrary& library, const SpriteEntry& entry, StringView name);

        /// Packs and unpacks @p group; true when the library changed. The status line says what happened
        bool Pack(TextureLibrary& library, PoolString group);
        bool Unpack(TextureLibrary& library, PoolString group);

        /// Packs into and checks @p files instead of the game's folders (a test); it must outlive the panel. Null goes back to the FileSystem's
        void UseFiles(const SpriteAtlasFiles* files) {
            _files = files;
        }

        /// The pack settings of the section, for the preview that shows what Pack would make with them
        [[nodiscard]] const PackSettings& Settings() const {
            return _settings;
        }

    private:
        /// The sprite and the group the name field was last loaded for: the field is reloaded when either moves, the settings when the group does.
        /// @return Whether it was reloaded
        bool SyncNameField(const TextureLibrary& library, const SpriteEntry& entry, PoolString group);
        void SetStatus(StringView text, bool isError);

        static constexpr size_t kNameSize = 65;
        static constexpr size_t kStatusSize = 256;

        PackSettings _settings;
        char _name[kNameSize] = {};
        PoolString _boundSprite;
        PoolString _boundGroup;
        bool _hasError = false;
        char _status[kStatusSize] = {};
        uint32_t _statusRevision = 0;  // moves at every SetStatus, so Sync copies the status only when it changed

        const SpriteAtlasFiles* _files = nullptr;
        // What the view's texts were last made from
        bool _viewBuilt = false;
        uint32_t _shownStatusRevision = 0;
        PoolString _shownGroup;
        bool _shownPacked = false;
        AtlasCounts _shownCounts;
    };

}  // namespace BECore
