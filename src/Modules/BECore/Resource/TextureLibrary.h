#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringMap.h>
#include <EASTL/vector.h>

namespace BECore {

    struct SpriteEntry {
        PoolString name;
        PoolString texturePath;
        Rect srcRect{};
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
         * Must be called after ConfigManager::Initialize().
         * Safe to call if the config file doesn't exist — library will be empty.
         */
        void Initialize();

        /**
         * @brief Look up a sprite entry by name
         * @return Pointer to entry, or nullptr if not found
         */
        [[nodiscard]] const SpriteEntry* GetSprite(PoolString name) const;

        /**
         * @brief All entries in insertion order (used by editor for display)
         */
        [[nodiscard]] const eastl::vector<SpriteEntry>& GetAll() const {
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
         * @return True if saved successfully
         */
        bool Save();

    private:
        void RebuildIndex();

        eastl::vector<SpriteEntry> _entries;
        UnorderedPoolMap<size_t> _index;
    };

}  // namespace BECore
