#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringMap.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <cstddef>

namespace pugi {
    class xml_node;
}

namespace BECore {

    class IDeserializer;
    class ISerializer;

    /// Where the pixels of a packed sprite came from: a file and the rectangle of it.
    struct SpriteSource {
        BE_CLASS(SpriteSource)

        BE_REFLECT_FIELD PoolString texturePath;
        BE_REFLECT_FIELD Rect rect{};
    };

    /// The settings a group was last packed with, plain data so the library file does not depend on the editor: `sizeMode` is the
    /// name of the editor's `AtlasSizeMode`. A repack that must give the owner's atlas again reads them back.
    struct AtlasPackRecord {
        BE_CLASS(AtlasPackRecord)

        BE_REFLECT_FIELD int padding = 0;
        BE_REFLECT_FIELD PoolString sizeMode;
        BE_REFLECT_FIELD uint32_t maxSide = 0;
    };

    /// The atlas group a sprite belongs to. `source` is engaged exactly when the sprite is packed: the entry's own `texturePath`
    /// and `srcRect` then point into the atlas file and `source` keeps what Unpack restores. `pack` is engaged once the group was
    /// packed and is the same for every member.
    struct SpriteAtlasLink {
        BE_CLASS(SpriteAtlasLink)

        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD Optional<SpriteSource> source;
        BE_REFLECT_FIELD Optional<AtlasPackRecord> pack;
    };

    /// One named sprite: a texture and a rectangle of it. Reflected: the fields are the file's attributes (`Resource/CLAUDE.md`,
    /// "TextureLibrary"). `BE_CLASS` adds a vptr, so it is not an aggregate: default-construct and assign the fields. An optional
    /// field is written only when engaged, so a sprite without an atlas writes what it always did.
    struct SpriteEntry {
        BE_CLASS(SpriteEntry)

        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD PoolString texturePath;
        BE_REFLECT_FIELD Rect srcRect{};
        BE_REFLECT_FIELD Optional<SpriteAtlasLink> atlas;
    };

    /** @brief Named sprites from the active game's TextureLibrary.xml; reached through CoreManager::GetTextureLibrary(). */
    class TextureLibrary {
    public:
        TextureLibrary() = default;
        ~TextureLibrary() = default;

        TextureLibrary(const TextureLibrary&) = delete;
        TextureLibrary& operator=(const TextureLibrary&) = delete;

        /** @brief Needs ConfigManager::Initialize() first. A missing config file leaves the library empty. */
        void Initialize();

        /**
         * @brief Replaces the library with the `<sprites>` of @p deserializer, positioned at the config root.
         *
         * A missing `sprites` leaves it empty; a missing field is the default. The skip and overwrite rules and the lenient reading
         * are in `Resource/CLAUDE.md`, "TextureLibrary".
         */
        void Load(IDeserializer& deserializer);

        /** @brief Writes every entry in insertion order, in the shape Load reads; `sprites` is written even when empty. */
        void Write(ISerializer& serializer) const;

        /** @return The entry, or nullptr; valid until the library changes. */
        [[nodiscard]] const SpriteEntry* GetSprite(PoolString name) const;

        [[nodiscard]] const Vector<SpriteEntry>& GetAll() const {
            return _entries;
        }

        void AddOrUpdate(SpriteEntry entry);

        void Remove(PoolString name);

        /**
         * @brief Renames the entry @p from to @p to; it keeps its position and data.
         * @return false, and nothing changed, when @p from is missing, @p to is empty or @p to names another entry; `from == to` is true.
         */
        bool Rename(PoolString from, PoolString to);

        /** @return @p base when no entry has that name, else `base_2`, `base_3`, ... the first free one. */
        [[nodiscard]] PoolString MakeUniqueName(StringView base) const;

        /**
         * @brief Puts the library into @p root, the root element of a config document: `sprites` is replaced where it stands (the
         * other children and the comments are not touched), or appended when the document has none. The pure half of Save.
         * @return false when @p root is not a valid node.
         */
        bool WriteInto(pugi::xml_node root) const;

        /**
         * @brief Writes the library back into the config file, leaving the rest of the file as it is.
         * @return false when the config is not loaded or the file cannot be written.
         */
        bool Save();

    private:
        void Clear();
        void RebuildIndex();

        Vector<SpriteEntry> _entries;
        UnorderedPoolMap<size_t> _index;
    };

}  // namespace BECore
