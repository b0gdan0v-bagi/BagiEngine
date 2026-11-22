#include "ConfigManager.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
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
        IntrusivePtrAtomic<XmlDocument> ParseConfigText(const String& text) {
            auto doc = BECore::New<XmlDocument>();
            if (!doc->LoadFromString(text)) {
                return {};
            }
            return doc;
        }

        uint64_t HashFileText(const String& text) {
            return StringUtils::GetHash(StringView(text));
        }

        /// The hash of the file as it is now; 0 when it cannot be read, so the next change of it reloads
        uint64_t HashFileOnDisk(const std::filesystem::path& path) {
            const Optional<String> text = XmlDocument::ReadFileBytes(path);
            return text ? HashFileText(*text) : 0;
        }

        constexpr StringView kXmlExtension = ".xml";
        constexpr StringView kBinaryExtension = ".bin";

        bool IsBinaryPath(const std::filesystem::path& path) {
            return path.extension() == kBinaryExtension.data();
        }

        /// The bytes of a `.bin` file, or the reason it cannot be read
        std::expected<Vector<uint8_t>, String> ReadBinaryFile(const std::filesystem::path& path) {
            Vector<uint8_t> bytes;
            const std::expected<void, StringView> read = ReadBinaryDocumentFile(path, bytes);
            if (!read.has_value()) {
                return std::unexpected(String(read.error()));
            }
            return bytes;
        }

        /// The bytes of the `.bin` at @p path once they are a valid document with a stamp that this program accepts, or the reason they are not
        std::expected<Vector<uint8_t>, String> LoadCheckedBinary(const std::filesystem::path& path) {
            auto bytes = ReadBinaryFile(path);
            if (!bytes) {
                return bytes;
            }
            BinaryDeserializer reader;
            if (!reader.LoadFromView(bytes->data(), bytes->size())) {
                const auto& errors = reader.GetErrors();
                return std::unexpected(errors.empty() ? String("the file is not a binary document") : errors.front().errorMessage);
            }
            const std::string source = path.string();  //-NoStd: the path API's own text
            const auto stamp = CheckContentStamp(reader, StringView(source.data(), source.size()));
            if (!stamp) {
                return std::unexpected(stamp.error());
            }
            return bytes;
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
                    LOG_INFO(Format("[ConfigManager] Config '{}' from {} overrides {}", layerFile.second.ToStringView(), layerFile.first.string(), existing->first.string()).c_str());
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

        LOG_INFO(Format("[ConfigManager] Found {} config files", filesToLoad.size()).c_str());

        TaskGroup group;
        for (const auto& [path, name] : filesToLoad) {
            auto handle = TaskManager::GetInstance().Run(LoadConfigAsync(path, name), TaskPriority::Normal, ThreadType::Background, BE_TASK_LABEL("Load config"));
            group.Add(handle);
        }

        group.WaitAll();

        auto loadedCount = _configs.size();
        _initialized.store(true, std::memory_order_release);

        LOG_INFO(Format("[ConfigManager] Loaded {} configs", loadedCount).c_str());
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

        UniquePtr<IDeserializer> MakeBinaryDeserializer(const Vector<uint8_t>& bytes) {
            auto deserializer = MakeUnique<BinaryDeserializer>();
            if (!deserializer->LoadFromView(bytes.data(), bytes.size())) {
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
            return MakeBinaryDeserializer(entry->bytes);
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
                LOG_ERROR(Format("[ConfigManager] SaveConfig: config '{}' is a binary file and cannot be saved", name.ToStringView()).c_str());
                return false;
            }
            if (it == _configs.end() || !it->second.doc) {
                LOG_ERROR(Format("[ConfigManager] SaveConfig: config '{}' not found", name.ToStringView()).c_str());
                return false;
            }
            auto& entry = it->second;
            if (entry.filePath.empty()) {
                LOG_ERROR(Format("[ConfigManager] SaveConfig: no file path for config '{}'", name.ToStringView()).c_str());
                return false;
            }
            ok = entry.doc->SaveToFile(entry.filePath);
            if (ok) {
                // The file keeps the layout of what was there, so its text is read back: the hash must be the one the watcher will see
                entry.fileHash = HashFileOnDisk(entry.filePath);
                LOG_INFO(Format("[ConfigManager] Saved config '{}' to {}", name.ToStringView(), entry.filePath.string()).c_str());
            } else {
                LOG_ERROR(Format("[ConfigManager] Failed to save config '{}' to {}", name.ToStringView(), entry.filePath.string()).c_str());
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
                LOG_ERROR(Format("[ConfigManager] ReloadConfig: config '{}' not found", name.ToStringView()).c_str());
                return false;
            }
            if (it->second.binary) {
                // Hot reload of a cooked file is not built: the bytes of an entry never change, a deserializer may hold them
                if (skipIfSame) {
                    LOG_DEBUG(Format("[ConfigManager] Config '{}': {} changed, binary configs are not reloaded", name.ToStringView(), it->second.filePath.string()).c_str());
                } else {
                    LOG_ERROR(Format("[ConfigManager] ReloadConfig: config '{}' is a binary file and cannot be reloaded", name.ToStringView()).c_str());
                }
                return false;
            }
            filePath = it->second.filePath;
            knownHash = it->second.fileHash;
        }

        const Optional<String> text = XmlDocument::ReadFileBytes(filePath);
        if (!text) {
            if (failure != nullptr) {
                *failure = Format("the file is gone or unreadable, the loaded '{}' is kept", name.ToStringView());
            }
            if (skipIfSame) {
                LOG_DEBUG(Format("[ConfigManager] Config '{}': {} is gone or unreadable, keeping the loaded document", name.ToStringView(), filePath.string()).c_str());
            } else {
                LOG_ERROR(Format("[ConfigManager] ReloadConfig: failed to reload '{}' from {}", name.ToStringView(), filePath.string()).c_str());
            }
            return false;
        }
        const uint64_t hash = HashFileText(*text);
        if (skipIfSame && hash == knownHash) {
            return false;
        }

        IntrusivePtrAtomic<XmlDocument> newDoc = ParseConfigText(*text);
        if (!newDoc) {
            if (failure != nullptr) {
                *failure = Format("the file does not parse, the old '{}' is kept", name.ToStringView());
            }
            LOG_ERROR(Format("[ConfigManager] ReloadConfig: failed to reload '{}' from {}", name.ToStringView(), filePath.string()).c_str());
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

        LOG_INFO(Format("[ConfigManager] Reloaded config '{}'", name.ToStringView()).c_str());
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
            LOG_DEBUG(Format("[ConfigManager] {} changed, no loaded config owns it", change.virtualPath.ToStringView()).c_str());
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

                LOG_DEBUG(Format("[ConfigManager] Found config: {} -> {}", stemStr, entry.path().string()).c_str());
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
                    LOG_ERROR(Format("[ConfigManager] Config '{}' has both {} and {} in one layer, it is not loaded; remove one of them", names[i].ToStringView(), files.xml->string(),
                                     files.binary->string())
                                  .c_str());
                    conflicted.push_back(names[i]);
                }
                continue;
            }
            filesToLoad.push_back({eastl::move(paths[i]), names[i]});
        }
    }

    Task<void> ConfigManager::LoadConfigAsync(std::filesystem::path path, PoolString name) {
        LOG_DEBUG(Format("[ConfigManager] Loading config: {}", name.ToStringView()).c_str());

        // The same stem twice in one layer: the first task to get the lock wins
        {
            LockGuard lock(_writeMutex);
            if (_configs.find(name) != _configs.end()) {
                LOG_WARNING(Format("[ConfigManager] Config {} already loaded, skipping duplicate", name.ToStringView()).c_str());
                co_return;
            }
        }

        if (IsBinaryPath(path)) {
            auto bytes = LoadCheckedBinary(path);
            if (!bytes) {
                LOG_ERROR(Format("[ConfigManager] Failed to load config: {} from {}: {}", name.ToStringView(), path.string(), bytes.error()).c_str());
                co_return;
            }
            LockGuard lock(_writeMutex);
            if (_configs.find(name) != _configs.end()) {
                LOG_WARNING(Format("[ConfigManager] Config {} was loaded by another thread, discarding duplicate", name.ToStringView()).c_str());
                co_return;
            }
            ConfigEntry entry;
            entry.filePath = path;
            entry.bytes = eastl::move(*bytes);
            entry.binary = true;
            _configs[name] = eastl::move(entry);
            co_return;
        }

        const Optional<String> text = XmlDocument::ReadFileBytes(path);
        IntrusivePtrAtomic<XmlDocument> doc = text ? ParseConfigText(*text) : IntrusivePtrAtomic<XmlDocument>{};
        if (!doc) {
            LOG_ERROR(Format("[ConfigManager] Failed to load config: {} from {}", name.ToStringView(), path.string()).c_str());
            co_return;
        }

        {
            LockGuard lock(_writeMutex);
            // The file was parsed outside the lock, so another task may have added the name meanwhile
            if (_configs.find(name) != _configs.end()) {
                LOG_WARNING(Format("[ConfigManager] Config {} was loaded by another thread, discarding duplicate", name.ToStringView()).c_str());
                co_return;
            }
            _configs[name] = ConfigEntry{doc, path, HashFileText(*text), {}, false};
        }

        LOG_DEBUG(Format("[ConfigManager] Config loaded: {}", name.ToStringView()).c_str());
    }

}  // namespace BECore
