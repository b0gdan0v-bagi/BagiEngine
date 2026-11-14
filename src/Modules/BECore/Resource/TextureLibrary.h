#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringMap.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <cstddef>

namespace BECore {

    class IDeserializer;
    class ISerializer;

    /// One named sprite: a texture and a rectangle of it. Reflected: the fields are the file's attributes (`Resource/CLAUDE.md`,
    /// "TextureLibrary"). `BE_CLASS` adds a vptr, so it is not an aggregate: default-construct and assign the fields.
    struct SpriteEntry {
        BE_CLASS(SpriteEntry)

        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD PoolString texturePath;
        BE_REFLECT_FIELD Rect srcRect{};
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
