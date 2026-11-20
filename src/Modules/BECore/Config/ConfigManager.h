#pragma once

#include <BECore/Config/XmlDocument.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringMap.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Types/Types.h>
#include <TaskSystem/Task.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace BECore {

    /// The bare command-line flag that makes the findings of a config load errors (ConfigManager::GetDiagnostics)
    inline constexpr StringView kStrictConfigFlag = "--strict-config";

    /**
     * @brief Loads and caches every XML config of the `config` mount layers by file stem
     *
     * Reached through CoreManager::GetConfigManager(). Every accessor asserts that Initialize() ran. Layering and the
     * editor calls: `Config/CLAUDE.md`.
     */
    class ConfigManager {
    public:
        ConfigManager() = default;

        /// Scans and resolves through @p fileSystem instead of CoreManager's, so a test can give a manager folders of its own; it must outlive the manager
        explicit ConfigManager(const FileSystem& fileSystem) : _fileSystem(&fileSystem) {}
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
         * @brief The diagnostics mode of every read of a config: Warn, or Error when the command line has `--strict-config`
         *
         * Read at each call, so nothing depends on the order of start-up. A reader of a config that does not come through
         * CreateDeserializer (OpenDeserializer, a scene's own XmlDeserializer) sets it on its deserializer itself.
         */
        static DiagnosticsMode GetDiagnostics();

        /**
         * @brief Get an IDeserializer positioned at the root of a loaded config
         *
         * The one way to read a named config through the reflection: the caller does not pick XmlDeserializer itself.
         * The deserializer has the diagnostics mode of GetDiagnostics(); the caller ends the read with FinishLoad().
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

        /// Writes the cached document back to the file it was loaded from, then emits ConfigEvents::ConfigChangedEvent
        bool SaveConfig(PoolString name);

        /// Replaces the cached document with the file's current content and emits ConfigEvents::ConfigChangedEvent; an XmlNode taken before is stale after it
        bool ReloadConfig(PoolString name);

        /// Emits ConfigEvents::ConfigChangedEvent for a caller that changed the cached document in place
        void NotifyConfigChanged(PoolString name) const;

        /**
         * @brief Reloads a config whose file changed on disk (needs a running FileWatcher; `Config/CLAUDE.md`, "Hot reload")
         *
         * Watches the `config` mount point until StopWatching() or the destruction. A file whose text is the one the manager last read or wrote
         * (its own SaveConfig, a second post of one save) changes nothing.
         */
        void StartWatching(FileWatcher& watcher);

        /// Ends the watch of StartWatching(); the FileWatcher may be stopped or destroyed after it
        void StopWatching();

    private:
        struct ConfigEntry {
            IntrusivePtrAtomic<XmlDocument> doc;
            std::filesystem::path filePath;
            uint64_t fileHash = 0;  ///< of the file text the document reflects: set at load, SaveConfig and ReloadConfig
        };

        const FileSystem& GetFileSystem() const;

        /// The config whose file is @p virtualPath (`config/<relative>`) in the layer that won, or an empty name
        PoolString FindConfigByVirtualPath(StringView virtualPath);

        void OnFileChanged(const FileChange& change);

        /// Reads the file and replaces the document. With @p skipIfSame a missing file or one with the text already read is not an error and changes nothing
        bool ReloadFromDisk(PoolString name, bool skipIfSame);

        void ScanDirectory(const std::filesystem::path& dir, Vector<Pair<std::filesystem::path, PoolString>>& filesToLoad) const;

        Task<void> LoadConfigAsync(std::filesystem::path path, PoolString name);

        const FileSystem* _fileSystem = nullptr;  ///< null: CoreManager's
        FileWatchHandle _watch;
        Mutex _writeMutex{BE_LOCK_SITE("Config write")};
        UnorderedPoolMap<ConfigEntry> _configs;
        std::atomic<bool> _initialized{false};
    };

}  // namespace BECore
