#pragma once

#include <BECore/Config/XmlDocument.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringMap.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <TaskSystem/Task.h>
#include <atomic>
#include <cstddef>
#include <filesystem>

namespace BECore {

    class IDeserializer;

    /**
     * @brief Loads and caches every XML config of the `config` mount layers by file stem
     *
     * Reached through CoreManager::GetConfigManager(). Every accessor asserts that Initialize() ran. Layering and the
     * editor calls: `Config/CLAUDE.md`.
     */
    class ConfigManager {
    public:
        ConfigManager() = default;
        ~ConfigManager() = default;

        ConfigManager(const ConfigManager&) = delete;
        ConfigManager& operator=(const ConfigManager&) = delete;
        ConfigManager(ConfigManager&&) = delete;
        ConfigManager& operator=(ConfigManager&&) = delete;

        /// Scans every layer, loads the files in parallel and blocks until done; a second call is a no-op
        void Initialize();

        /// The root of a loaded config by name (file stem), or an invalid node when the name is unknown
        XmlNode GetConfig(PoolString name) const;

        /// The StringView overloads avoid interning the name
        XmlNode GetConfig(StringView name) const;

        /**
         * @brief Get an IDeserializer positioned at the root of a loaded config
         *
         * The one way to read a named config through the reflection: the caller does not pick XmlDeserializer itself.
         * The deserializer reads the cached document, so it must not outlive a ReloadConfig of that config.
         * @return The deserializer, or nullptr if the config is unknown
         */
        UniquePtr<IDeserializer> CreateDeserializer(PoolString name) const;

        /// As above, without interning the name
        UniquePtr<IDeserializer> CreateDeserializer(StringView name) const;

        bool HasConfig(PoolString name) const;
        bool HasConfig(StringView name) const;

        size_t GetConfigCount() const;

        /// Sorted by name
        Vector<PoolString> GetAllConfigNames() const;

        /// The file the config was loaded from (the layer that won), or an empty path when the name is unknown
        std::filesystem::path GetConfigFilePath(PoolString name) const;

        /// Writes the cached document back to the file it was loaded from
        bool SaveConfig(PoolString name);

        /// Replaces the cached document with the file's current content; an XmlNode taken before is stale after it
        bool ReloadConfig(PoolString name);

    private:
        struct ConfigEntry {
            IntrusivePtrAtomic<XmlDocument> doc;
            std::filesystem::path filePath;
        };

        void ScanDirectory(const std::filesystem::path& dir, Vector<Pair<std::filesystem::path, PoolString>>& filesToLoad) const;

        Task<void> LoadConfigAsync(std::filesystem::path path, PoolString name);

        Mutex _writeMutex{BE_LOCK_SITE("Config write")};
        UnorderedPoolMap<ConfigEntry> _configs;
        std::atomic<bool> _initialized{false};
    };

}  // namespace BECore
