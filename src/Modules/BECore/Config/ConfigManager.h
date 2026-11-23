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
     * @brief Loads and caches every XML and every cooked `.bin` config of the `config` mount layers by file stem
     *
     * Reached through CoreManager::GetConfigManager(). Every accessor asserts that Initialize() ran. Layering and the
     * editor calls: `Config/CLAUDE.md`.
     *
     * A `.bin` is a stamped binary document (`CheckContentStamp`), read through CreateDeserializer() only. What is XML-only for it:
     * GetConfig() (an invalid node), SaveConfig(), ReloadConfig() and the hot reload of a changed file.
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

        /**
         * @brief Scans every layer for `.xml` and `.bin` files, loads them in parallel and blocks until done; a second call is a no-op
         *
         * The upper layer wins whatever the extension. A stem that has both a `.xml` and a `.bin` in one layer is one `LOG_ERROR`
         * naming both files and the config is absent: no layer's file is used, because a lower layer's file under an error would be
         * a stale config nobody notices. A `.bin` that is unreadable, damaged, unstamped or stamped for another layout is one
         * `LOG_ERROR` and the config is absent, like an XML that does not parse.
         */
        void Initialize();

        /// The root of a loaded XML config by name (file stem), or an invalid node when the name is unknown or the config is a `.bin`
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
         *
         * For a `.bin` it is a `BinaryDeserializer` over the bytes the entry owns (`LoadFromView`): they are read once and never
         * replaced or moved, so the reader is valid as long as the manager lives. A binary document reports no findings, so the
         * caller's `SetDiagnostics` has no effect on it.
         * @return The deserializer, or nullptr if the config is unknown
         */
        UniquePtr<IDeserializer> CreateDeserializer(PoolString name) const;

        /// As above, without interning the name
        UniquePtr<IDeserializer> CreateDeserializer(StringView name) const;

        bool HasConfig(PoolString name) const;
        bool HasConfig(StringView name) const;

        /// True when the config is loaded from a `.bin`; false for an XML config and for an unknown name
        bool IsBinaryConfig(PoolString name) const;
        bool IsBinaryConfig(StringView name) const;

        size_t GetConfigCount() const;

        /// Sorted by name
        Vector<PoolString> GetAllConfigNames() const;

        /// The file the config was loaded from (the layer that won), or an empty path when the name is unknown
        std::filesystem::path GetConfigFilePath(PoolString name) const;

        /// Writes the cached document back to the file it was loaded from, then emits ConfigEvents::ConfigChangedEvent; a `.bin` config is read-only: false, one `LOG_ERROR`
        bool SaveConfig(PoolString name);

        /// Replaces the cached document with the file's current content and emits ConfigEvents::ConfigChangedEvent; an XmlNode taken before is stale after it. A `.bin` config is not reloaded: false,
        /// one `LOG_ERROR`
        bool ReloadConfig(PoolString name);

        /// Emits ConfigEvents::ConfigChangedEvent for a caller that changed the cached document in place
        void NotifyConfigChanged(PoolString name) const;

        /**
         * @brief Reloads a config whose file changed on disk (needs a running FileWatcher; `Config/CLAUDE.md`, "Hot reload")
         *
         * Watches the `config` mount point until StopWatching() or the destruction. A file whose text is the one the manager last read or wrote
         * (its own SaveConfig, a second post of one save) changes nothing. A changed `.bin` is ignored (a `LOG_DEBUG`, no event, no failure).
         */
        void StartWatching(FileWatcher& watcher);

        /// Ends the watch of StartWatching(); the FileWatcher may be stopped or destroyed after it
        void StopWatching();

    private:
        struct ConfigEntry {
            IntrusivePtr<XmlDocument> doc;  ///< null for a `.bin`
            std::filesystem::path filePath;
            uint64_t fileHash = 0;  ///< of the file text the document reflects: set at load, SaveConfig and ReloadConfig
            Vector<uint8_t> bytes;  ///< a `.bin`: the validated file, never replaced; a deserializer borrows it (a `Vector`'s block stays when the entry is moved)
            bool binary = false;
        };

        /// An XML or a binary deserializer over @p entry, or nullptr when the entry is null or cannot be read
        static UniquePtr<IDeserializer> MakeDeserializerOf(const ConfigEntry* entry);

        const FileSystem& GetFileSystem() const;

        /// The config whose file is @p virtualPath (`config/<relative>`) in the layer that won, or an empty name
        PoolString FindConfigByVirtualPath(StringView virtualPath);

        void OnFileChanged(FileChange& change);

        /// Reads the file and replaces the document. With @p skipIfSame a missing file or one with the text already read is not an error and changes nothing.
        /// @p failure, when given, gets the reason of a file that could not be read or parsed (not for a text that is the same)
        bool ReloadFromDisk(PoolString name, bool skipIfSame, String* failure);

        /// The `.xml` and `.bin` files of @p dir by stem. A stem with both is logged and added to @p conflicted instead of @p filesToLoad
        void ScanDirectory(const std::filesystem::path& dir, Vector<Pair<std::filesystem::path, PoolString>>& filesToLoad, Vector<PoolString>& conflicted) const;

        Task<void> LoadConfigAsync(std::filesystem::path path, PoolString name);

        const FileSystem* _fileSystem = nullptr;  ///< null: CoreManager's
        FileWatchHandle _watch;
        Mutex _writeMutex{BE_LOCK_SITE("Config write")};
        UnorderedPoolMap<ConfigEntry> _configs;
        std::atomic<bool> _initialized{false};
    };

}  // namespace BECore
