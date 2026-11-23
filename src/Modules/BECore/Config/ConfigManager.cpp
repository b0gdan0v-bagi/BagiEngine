#include "ConfigManager.h"

#include <BECore/Application/CommandLine.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Memory/ScratchArena.h>
#include <BECore/Reflection/BinaryContent.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <EASTL/sort.h>
#include <Events/ConfigEvents.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/TaskGroup.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskManager.h>

namespace BECore {

    namespace {

        /// The document of @p text, or nothing when it does not parse
        IntrusivePtr<XmlDocument> ParseConfigText(StringView text) {
            auto doc = BECore::New<XmlDocument>();
            if (!doc->LoadFromString(text)) {
                return {};
            }
            return doc;
        }

        uint64_t HashFileText(StringView text) {
            return StringUtils::GetHash(text);
        }

        /// A config file bigger than this is read into a heap `String`: the chunks of a scratch arena stay allocated after the
        /// scope ends, and a pool thread must not keep one the size of the biggest file it ever loaded
        constexpr size_t kMaxScratchConfigBytes = 256 * 1024;

        /// Calls @p use with the text of the file at @p path and returns true, or returns false when the file cannot be read.
        /// The text is valid inside @p use only. No `co_await` may sit between this call and the end of @p use: a scratch scope
        /// ends on the thread that made it, and a coroutine may resume on another
        template <typename Use>
        bool WithConfigText(const std::filesystem::path& path, Use&& use) {
            {
                ScratchScope scratch;
                if (const Optional<StringView> text = XmlDocument::ReadFileInto(path, scratch.GetArena(), kMaxScratchConfigBytes)) {
                    use(*text);
                    return true;
                }
            }
            // A big file, or one that cannot be read (then this fails the same way, at the first check)
            const Optional<String> text = XmlDocument::ReadFileBytes(path);
            if (!text) {
                return false;
            }
            use(StringView{*text});
            return true;
        }

        /// The hash of the file as it is now; 0 when it cannot be read, so the next change of it reloads
        uint64_t HashFileOnDisk(const std::filesystem::path& path) {
            uint64_t hash = 0;
            WithConfigText(path, [&hash](StringView text) { hash = HashFileText(text); });
            return hash;
        }

        constexpr StringView kXmlExtension = ".xml";
        constexpr StringView kBinaryExtension = ".bin";

        bool IsBinaryPath(const std::filesystem::path& path) {
            return path.extension() == kBinaryExtension.data();
        }

        /// The content of the `.bin` at @p path once it is a valid document with a stamp that this program accepts, or the reason it is not
        std::expected<BinaryContent, String> LoadCheckedBinary(const std::filesystem::path& path) {
            auto content = LoadContentFile(path);
            if (!content) {
                return content;
            }
            BinaryDeserializer reader;
            if (!reader.LoadFromContent(*content)) {
                const auto& errors = reader.GetErrors();
                return std::unexpected(errors.empty() ? String("the file is not a binary document") : errors.front().errorMessage);
            }
            // After the adoption: the stamp is in the entries, which are read after the section, so a file that another layout
            // refuses leaves its type and field names in the pool
            const std::string source = path.string();  //-NoStd: the path API's own text
            const auto stamp = CheckContentStamp(reader, StringView(source.data(), source.size()));
            if (!stamp) {
                return std::unexpected(stamp.error());
            }
            return content;
        }

    }  // namespace

    const FileSystem& ConfigManager::GetFileSystem() const {
        return _fileSystem != nullptr ? *_fileSystem : CoreManager::GetFileSystem();
    }

    void ConfigManager::Initialize() {
        BE_PROFILE_SCOPE("ConfigManager::Initialize");
        if (_initialized.load(std::memory_order_acquire)) {
            LOG_INFO("[ConfigManager] Already initialized");
            return;
        }

        LOG_INFO("[ConfigManager] Initializing...");

        const auto layers = GetFileSystem().GetMountLayers(MountPoints::config);
        if (layers.empty()) {
            LOG_WARNING("[ConfigManager] Config directory not found, skipping initialization");
            _initialized.store(true, std::memory_order_release);
            return;
        }

        Vector<Pair<std::filesystem::path, PoolString>> filesToLoad;
        Vector<PoolString> conflicted;
        for (const auto& layer : layers) {
            Vector<Pair<std::filesystem::path, PoolString>> layerFiles;
            ScanDirectory(layer, layerFiles, conflicted);

            for (auto& layerFile : layerFiles) {
                auto existing = eastl::find_if(filesToLoad.begin(), filesToLoad.end(), [&](const auto& loaded) { return loaded.second == layerFile.second; });
                if (existing != filesToLoad.end()) {
                    LOG_INFO("[ConfigManager] Config '{}' from {} overrides {}", layerFile.second.ToStringView(), layerFile.first.string(), existing->first.string());
                    *existing = eastl::move(layerFile);
                } else {
                    filesToLoad.push_back(eastl::move(layerFile));
                }
            }
        }

        // A stem in conflict in any layer is absent, not resolved to another layer's file
        for (const PoolString& name : conflicted) {
            filesToLoad.erase(eastl::remove_if(filesToLoad.begin(), filesToLoad.end(), [&](const auto& file) { return file.second == name; }), filesToLoad.end());
        }

        if (filesToLoad.empty()) {
            LOG_WARNING("[ConfigManager] No config files found in config directory");
            _initialized.store(true, std::memory_order_release);
            return;
        }

        LOG_INFO("[ConfigManager] Found {} config files", filesToLoad.size());

        TaskGroup group;
        for (const auto& [path, name] : filesToLoad) {
            auto handle = TaskManager::GetInstance().Run(LoadConfigAsync(path, name), TaskPriority::Normal, ThreadType::Background, BE_TASK_LABEL("Load config"));
            group.Add(handle);
        }

        group.WaitAll();

        auto loadedCount = _configs.size();
        _initialized.store(true, std::memory_order_release);

        LOG_INFO("[ConfigManager] Loaded {} configs", loadedCount);
    }

    XmlNode ConfigManager::GetConfig(PoolString name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        auto it = _configs.find(name);
        if (it != _configs.end() && it->second.doc) {
            return it->second.doc->GetRoot();
        }
        return XmlNode();
    }

    namespace {

        UniquePtr<IDeserializer> MakeXmlDeserializer(const XmlNode& root) {
            if (!root) {
                return nullptr;
            }
            auto deserializer = MakeUnique<XmlDeserializer>();
            deserializer->SetDiagnostics(ConfigManager::GetDiagnostics());
            if (!deserializer->LoadFromXmlNode(root)) {
                return nullptr;
            }
            return deserializer;
        }

        UniquePtr<IDeserializer> MakeBinaryDeserializer(const BinaryContent& content) {
            auto deserializer = MakeUnique<BinaryDeserializer>();
            if (!deserializer->LoadFromContent(content)) {
                return nullptr;
            }
            return deserializer;
        }

    }  // namespace

    UniquePtr<IDeserializer> ConfigManager::MakeDeserializerOf(const ConfigEntry* entry) {
        if (entry == nullptr) {
            return nullptr;
        }
        if (entry->binary) {
            return MakeBinaryDeserializer(entry->content);
        }
        return entry->doc ? MakeXmlDeserializer(entry->doc->GetRoot()) : nullptr;
    }

    DiagnosticsMode ConfigManager::GetDiagnostics() {
        return CoreManager::GetCommandLine().HasFlag(kStrictConfigFlag) ? DiagnosticsMode::Error : DiagnosticsMode::Warn;
    }

    UniquePtr<IDeserializer> ConfigManager::CreateDeserializer(PoolString name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        const auto it = _configs.find(name);
        return MakeDeserializerOf(it != _configs.end() ? &it->second : nullptr);
    }

    UniquePtr<IDeserializer> ConfigManager::CreateDeserializer(StringView name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        const auto it = _configs.Find(name);
        return MakeDeserializerOf(it != _configs.end() ? &it->second : nullptr);
    }

    bool ConfigManager::IsBinaryConfig(PoolString name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        const auto it = _configs.find(name);
        return it != _configs.end() && it->second.binary;
    }

    bool ConfigManager::IsBinaryConfig(StringView name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        const auto it = _configs.Find(name);
        return it != _configs.end() && it->second.binary;
    }

    bool ConfigManager::HasConfig(PoolString name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        return _configs.find(name) != _configs.end();
    }

    XmlNode ConfigManager::GetConfig(StringView name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        auto it = _configs.Find(name);
        if (it != _configs.end() && it->second.doc) {
            return it->second.doc->GetRoot();
        }
        return XmlNode();
    }

    bool ConfigManager::HasConfig(StringView name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        return _configs.Find(name) != _configs.end();
    }

    size_t ConfigManager::GetConfigCount() const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        return _configs.size();
    }

    Vector<PoolString> ConfigManager::GetAllConfigNames() const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        Vector<PoolString> names;
        names.reserve(_configs.size());
        for (const auto& [name, entry] : _configs) {
            names.push_back(name);
        }
        eastl::sort(names.begin(), names.end(), [](const PoolString& a, const PoolString& b) { return a.ToStringView() < b.ToStringView(); });
        return names;
    }

    std::filesystem::path ConfigManager::GetConfigFilePath(PoolString name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        auto it = _configs.find(name);
        if (it != _configs.end()) {
            return it->second.filePath;
        }
        return {};
    }

    bool ConfigManager::SaveConfig(PoolString name) {
        BE_PROFILE_SCOPE("ConfigManager::SaveConfig");
        ASSERT(_initialized.load(std::memory_order_acquire));
        bool ok = false;
        {
            LockGuard lock(_writeMutex);
            auto it = _configs.find(name);
            if (it != _configs.end() && it->second.binary) {
                LOG_ERROR("[ConfigManager] SaveConfig: config '{}' is a binary file and cannot be saved", name.ToStringView());
                return false;
            }
            if (it == _configs.end() || !it->second.doc) {
                LOG_ERROR("[ConfigManager] SaveConfig: config '{}' not found", name.ToStringView());
                return false;
            }
            auto& entry = it->second;
            if (entry.filePath.empty()) {
                LOG_ERROR("[ConfigManager] SaveConfig: no file path for config '{}'", name.ToStringView());
                return false;
            }
            ok = entry.doc->SaveToFile(entry.filePath);
            if (ok) {
                // The file keeps the layout of what was there, so its text is read back: the hash must be the one the watcher will see
                entry.fileHash = HashFileOnDisk(entry.filePath);
                LOG_INFO("[ConfigManager] Saved config '{}' to {}", name.ToStringView(), entry.filePath.string());
            } else {
                LOG_ERROR("[ConfigManager] Failed to save config '{}' to {}", name.ToStringView(), entry.filePath.string());
            }
        }
        if (ok) {
            NotifyConfigChanged(name);  // the lock is released: a subscriber may call the manager
        }
        return ok;
    }

    bool ConfigManager::ReloadConfig(PoolString name) {
        BE_PROFILE_SCOPE("ConfigManager::ReloadConfig");
        ASSERT(_initialized.load(std::memory_order_acquire));
        const bool reloaded = ReloadFromDisk(name, false, nullptr);
        if (reloaded) {
            NotifyConfigChanged(name);
        }
        return reloaded;
    }

    bool ConfigManager::ReloadFromDisk(PoolString name, bool skipIfSame, String* failure) {
        std::filesystem::path filePath;
        uint64_t knownHash = 0;
        {
            LockGuard lock(_writeMutex);
            auto it = _configs.find(name);
            if (it == _configs.end()) {
                LOG_ERROR("[ConfigManager] ReloadConfig: config '{}' not found", name.ToStringView());
                return false;
            }
            if (it->second.binary) {
                // Hot reload of a cooked file is not built: the bytes of an entry never change, a deserializer may hold them
                if (skipIfSame) {
                    LOG_DEBUG("[ConfigManager] Config '{}': {} changed, binary configs are not reloaded", name.ToStringView(), it->second.filePath.string());
                } else {
                    LOG_ERROR("[ConfigManager] ReloadConfig: config '{}' is a binary file and cannot be reloaded", name.ToStringView());
                }
                return false;
            }
            filePath = it->second.filePath;
            knownHash = it->second.fileHash;
        }

        // One read: the hash tells a rewrite with the same text from a change, the parse follows only for a change
        uint64_t hash = 0;
        bool unchanged = false;
        IntrusivePtr<XmlDocument> newDoc;
        const bool read = WithConfigText(filePath, [&](StringView text) {
            hash = HashFileText(text);
            unchanged = skipIfSame && hash == knownHash;
            if (!unchanged) {
                newDoc = ParseConfigText(text);
            }
        });
        if (!read) {
            if (failure != nullptr) {
                *failure = Format("the file is gone or unreadable, the loaded '{}' is kept", name.ToStringView());
            }
            if (skipIfSame) {
                LOG_DEBUG("[ConfigManager] Config '{}': {} is gone or unreadable, keeping the loaded document", name.ToStringView(), filePath.string());
            } else {
                LOG_ERROR("[ConfigManager] ReloadConfig: failed to reload '{}' from {}", name.ToStringView(), filePath.string());
            }
            return false;
        }
        if (unchanged) {
            return false;
        }

        if (!newDoc) {
            if (failure != nullptr) {
                *failure = Format("the file does not parse, the old '{}' is kept", name.ToStringView());
            }
            LOG_ERROR("[ConfigManager] ReloadConfig: failed to reload '{}' from {}", name.ToStringView(), filePath.string());
            return false;
        }

        {
            LockGuard lock(_writeMutex);
            auto it = _configs.find(name);
            if (it != _configs.end()) {
                it->second.doc = newDoc;
                it->second.fileHash = hash;
            }
        }

        LOG_INFO("[ConfigManager] Reloaded config '{}'", name.ToStringView());
        return true;
    }

    void ConfigManager::NotifyConfigChanged(PoolString name) const {
        ConfigEvents::ConfigChangedEvent::Emit(name);
    }

    void ConfigManager::StartWatching(FileWatcher& watcher) {
        _watch = watcher.WatchFolder(PoolString(MountPoints::config).ToStringView(), [this](FileChange& change) { OnFileChanged(change); }, "ConfigManager");
    }

    void ConfigManager::StopWatching() {
        _watch.Reset();
    }

    PoolString ConfigManager::FindConfigByVirtualPath(StringView virtualPath) {
        const PoolString mount = MountPoints::config;
        const StringView prefix = mount.ToStringView();
        if (virtualPath.size() <= prefix.size() + 1 || virtualPath.substr(0, prefix.size()) != prefix || virtualPath[prefix.size()] != '/') {
            return {};
        }
        const StringView relativeText = virtualPath.substr(prefix.size() + 1);
        const std::filesystem::path relative(relativeText.begin(), relativeText.end());
        LockGuard lock(_writeMutex);
        for (const std::filesystem::path& layer : GetFileSystem().GetMountLayers(MountPoints::config)) {
            const std::filesystem::path candidate = (layer / relative).lexically_normal();
            for (const auto& [name, entry] : _configs) {
                if (entry.filePath.lexically_normal() == candidate) {
                    return name;
                }
            }
        }
        return {};
    }

    void ConfigManager::OnFileChanged(FileChange& change) {
        const PoolString name = FindConfigByVirtualPath(change.virtualPath.ToStringView());
        if (name.Empty()) {
            // A new file, a removed one or a file of a layer that does not win: nothing owns it (the `hot-reload` entry builds on this)
            LOG_DEBUG("[ConfigManager] {} changed, no loaded config owns it", change.virtualPath.ToStringView());
            return;
        }
        String failure;
        if (ReloadFromDisk(name, true, &failure)) {
            NotifyConfigChanged(name);
        } else if (!failure.empty()) {
            change.ok = false;
            change.error = std::move(failure);
        }
    }

    void ConfigManager::ScanDirectory(const std::filesystem::path& dir, Vector<Pair<std::filesystem::path, PoolString>>& filesToLoad, Vector<PoolString>& conflicted) const {
        Vector<std::filesystem::path> paths;
        Vector<String> stems;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && (entry.path().extension() == kXmlExtension.data() || IsBinaryPath(entry.path()))) {
                const auto stemStr = entry.path().stem().string();
                paths.push_back(entry.path());
                stems.push_back(String(stemStr.c_str(), stemStr.length()));

                LOG_DEBUG("[ConfigManager] Found config: {} -> {}", stemStr, entry.path().string());
            }
        }

        // One batch for the layer: the names come from the disk, so the form that hashes them
        Vector<StringView> views;
        views.reserve(stems.size());
        for (const String& stem : stems) {
            views.push_back(StringView(stem));
        }
        Vector<PoolString> names(views.size());
        PoolString::InternBatch(Span<const StringView>(views.data(), views.size()), Span<PoolString>(names.data(), names.size()));

        // Whether a stem has a file of each extension, to tell a stem with both from a stem with two files of one kind
        struct StemFiles {
            const std::filesystem::path* xml = nullptr;
            const std::filesystem::path* binary = nullptr;
        };
        UnorderedPoolMap<StemFiles> seen;
        for (size_t i = 0; i < paths.size(); ++i) {
            StemFiles& files = seen[names[i]];
            (IsBinaryPath(paths[i]) ? files.binary : files.xml) = &paths[i];
        }

        for (size_t i = 0; i < paths.size(); ++i) {
            const StemFiles& files = seen[names[i]];
            if (files.xml != nullptr && files.binary != nullptr) {
                if (eastl::find(conflicted.begin(), conflicted.end(), names[i]) == conflicted.end()) {
                    LOG_ERROR("[ConfigManager] Config '{}' has both {} and {} in one layer, it is not loaded; remove one of them", names[i].ToStringView(), files.xml->string(),
                              files.binary->string());
                    conflicted.push_back(names[i]);
                }
                continue;
            }
            filesToLoad.push_back({eastl::move(paths[i]), names[i]});
        }
    }

    Task<void> ConfigManager::LoadConfigAsync(std::filesystem::path path, PoolString name) {
        LOG_DEBUG("[ConfigManager] Loading config: {}", name.ToStringView());

        // The same stem twice in one layer: the first task to get the lock wins
        {
            LockGuard lock(_writeMutex);
            if (_configs.find(name) != _configs.end()) {
                LOG_WARNING("[ConfigManager] Config {} already loaded, skipping duplicate", name.ToStringView());
                co_return;
            }
        }

        if (IsBinaryPath(path)) {
            auto content = LoadCheckedBinary(path);
            if (!content) {
                LOG_ERROR("[ConfigManager] Failed to load config: {} from {}: {}", name.ToStringView(), path.string(), content.error());
                co_return;
            }
            LockGuard lock(_writeMutex);
            if (_configs.find(name) != _configs.end()) {
                LOG_WARNING("[ConfigManager] Config {} was loaded by another thread, discarding duplicate", name.ToStringView());
                co_return;
            }
            ConfigEntry entry;
            entry.filePath = path;
            entry.content = eastl::move(*content);
            entry.binary = true;
            _configs[name] = eastl::move(entry);
            co_return;
        }

        // The scratch scope of WithConfigText is closed before any `co_await`: the read and the parse are plain calls
        IntrusivePtr<XmlDocument> doc;
        uint64_t hash = 0;
        WithConfigText(path, [&](StringView text) {
            doc = ParseConfigText(text);
            hash = HashFileText(text);
        });
        if (!doc) {
            LOG_ERROR("[ConfigManager] Failed to load config: {} from {}", name.ToStringView(), path.string());
            co_return;
        }

        {
            LockGuard lock(_writeMutex);
            // The file was parsed outside the lock, so another task may have added the name meanwhile
            if (_configs.find(name) != _configs.end()) {
                LOG_WARNING("[ConfigManager] Config {} was loaded by another thread, discarding duplicate", name.ToStringView());
                co_return;
            }
            _configs[name] = ConfigEntry{doc, path, hash, {}, false};
        }

        LOG_DEBUG("[ConfigManager] Config loaded: {}", name.ToStringView());
    }

}  // namespace BECore
