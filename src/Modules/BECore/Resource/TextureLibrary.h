#pragma once

namespace BECore {

    class IDeserializer;
    class ISerializer;

    /// One named sprite: a texture and the rectangle of it. Reflected, so the fields are exactly the attributes of a `<sprite>` in
    /// TextureLibrary.xml (`name`, `texturePath`) and the attributes `x y w h` of its child `<srcRect>`.
    /// `BE_CLASS` gives the struct virtual functions, so it has a vptr and is not an aggregate: build an entry by default
    /// construction and assign the fields.
    struct SpriteEntry {
        BE_CLASS(SpriteEntry)

        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD PoolString texturePath;
        BE_REFLECT_FIELD Rect srcRect{};
    };

    /**
     * @brief Library of named sprite definitions (name → texturePath + srcRect)
     *
     * Loaded from config/TextureLibrary.xml at startup.
     * Provides fast lookup by name for SpriteRendererComponent and editor widgets.
     *
     * @note Access via CoreManager::GetTextureLibrary()
     */
    class TextureLibrary {
    public:
        TextureLibrary() = default;
        ~TextureLibrary() = default;

        TextureLibrary(const TextureLibrary&) = delete;
        TextureLibrary& operator=(const TextureLibrary&) = delete;

        /**
         * @brief Load sprite definitions from config/TextureLibrary.xml
         *
         * Must be called after ConfigManager::Initialize(). Reads through ConfigManager::CreateDeserializer and Load, and logs the
         * errors the deserializer collected. Safe to call if the config file doesn't exist: the library will be empty.
         */
        void Initialize();

        /**
         * @brief Replace the library with the sprites read from @p deserializer (positioned at the root of the config)
         *
         * Reads `<sprites><sprite name texturePath><srcRect x y w h/></sprite>...</sprites>`. A missing `sprites` leaves the library
         * empty. An entry without a `name` (or with an empty one) is skipped silently, a repeated name overwrites the earlier entry in
         * place, a missing `texturePath` or `srcRect` is the default (empty path, empty rect). Numbers are read leniently (the default
         * IDeserializer, not strict values): a malformed number is 0. Every child node of `<sprites>` is read as a sprite, whatever its
         * name; a comment there is an entry without a name and is skipped.
         */
        void Load(IDeserializer& deserializer);

        /**
         * @brief Write every entry, in insertion order, to @p serializer as `<sprites><sprite .../>...</sprites>`
         *
         * Always writes `sprites`, also when the library is empty. The shape is the one Load reads.
         */
        void Write(ISerializer& serializer) const;

        /**
         * @brief Look up a sprite entry by name
         * @return Pointer to entry, or nullptr if not found
         */
        [[nodiscard]] const SpriteEntry* GetSprite(PoolString name) const;

        /**
         * @brief All entries in insertion order (used by editor for display)
         */
        [[nodiscard]] const Vector<SpriteEntry>& GetAll() const {
            return _entries;
        }

        /**
         * @brief Add a new entry or overwrite an existing one with the same name
         */
        void AddOrUpdate(SpriteEntry entry);

        /**
         * @brief Remove an entry by name. No-op if not found
         */
        void Remove(PoolString name);

        /**
         * @brief Persist current state back to config/TextureLibrary.xml
         *
         * Replaces the `sprites` section of the cached document with Write and saves the config; the rest of the file stays.
         * @return True if saved successfully, false when the config is not loaded or the file cannot be written
         */
        bool Save();

    private:
        void Clear();
        void RebuildIndex();

        Vector<SpriteEntry> _entries;
        UnorderedPoolMap<size_t> _index;
    };

}  // namespace BECore
