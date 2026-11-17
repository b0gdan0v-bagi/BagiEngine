#include "ConfigManager.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <EASTL/sort.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/TaskGroup.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskManager.h>

namespace BECore {

    void ConfigManager::Initialize() {
        BE_PROFILE_SCOPE("ConfigManager::Initialize");
        if (_initialized.load(std::memory_order_acquire)) {
            LOG_INFO("[ConfigManager] Already initialized");
            return;
        }

        LOG_INFO("[ConfigManager] Initializing...");

        const auto layers = CoreManager::GetFileSystem().GetMountLayers(MountPoints::config);
        if (layers.empty()) {
            LOG_WARNING("[ConfigManager] Config directory not found, skipping initialization");
            _initialized.store(true, std::memory_order_release);
            return;
        }

        Vector<Pair<std::filesystem::path, PoolString>> filesToLoad;
        for (const auto& layer : layers) {
            Vector<Pair<std::filesystem::path, PoolString>> layerFiles;
            ScanDirectory(layer, layerFiles);

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

        if (filesToLoad.empty()) {
            LOG_WARNING("[ConfigManager] No XML files found in config directory");
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

        UniquePtr<IDeserializer> MakeDeserializer(const XmlNode& root) {
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

    }  // namespace

    DiagnosticsMode ConfigManager::GetDiagnostics() {
        return CoreManager::GetCommandLine().HasFlag(kStrictConfigFlag) ? DiagnosticsMode::Error : DiagnosticsMode::Warn;
    }

    UniquePtr<IDeserializer> ConfigManager::CreateDeserializer(PoolString name) const {
        return MakeDeserializer(GetConfig(name));
    }

    UniquePtr<IDeserializer> ConfigManager::CreateDeserializer(StringView name) const {
        return MakeDeserializer(GetConfig(name));
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
        LockGuard lock(_writeMutex);
        auto it = _configs.find(name);
        if (it == _configs.end() || !it->second.doc) {
            LOG_ERROR(Format("[ConfigManager] SaveConfig: config '{}' not found", name.ToStringView()).c_str());
            return false;
        }
        const auto& entry = it->second;
        if (entry.filePath.empty()) {
            LOG_ERROR(Format("[ConfigManager] SaveConfig: no file path for config '{}'", name.ToStringView()).c_str());
            return false;
        }
        const bool ok = entry.doc->SaveToFile(entry.filePath);
        if (ok) {
            LOG_INFO(Format("[ConfigManager] Saved config '{}' to {}", name.ToStringView(), entry.filePath.string()).c_str());
        } else {
            LOG_ERROR(Format("[ConfigManager] Failed to save config '{}' to {}", name.ToStringView(), entry.filePath.string()).c_str());
        }
        return ok;
    }

    bool ConfigManager::ReloadConfig(PoolString name) {
        BE_PROFILE_SCOPE("ConfigManager::ReloadConfig");
        ASSERT(_initialized.load(std::memory_order_acquire));
        std::filesystem::path filePath;
        {
            LockGuard lock(_writeMutex);
            auto it = _configs.find(name);
            if (it == _configs.end()) {
                LOG_ERROR(Format("[ConfigManager] ReloadConfig: config '{}' not found", name.ToStringView()).c_str());
                return false;
            }
            filePath = it->second.filePath;
        }

        auto newDoc = BECore::New<XmlDocument>();
        if (!newDoc->LoadFromFile(filePath)) {
            LOG_ERROR(Format("[ConfigManager] ReloadConfig: failed to reload '{}' from {}", name.ToStringView(), filePath.string()).c_str());
            return false;
        }

        {
            LockGuard lock(_writeMutex);
            auto it = _configs.find(name);
            if (it != _configs.end()) {
                it->second.doc = newDoc;
            }
        }

        LOG_INFO(Format("[ConfigManager] Reloaded config '{}'", name.ToStringView()).c_str());
        return true;
    }

    void ConfigManager::ScanDirectory(const std::filesystem::path& dir, Vector<Pair<std::filesystem::path, PoolString>>& filesToLoad) const {
        Vector<std::filesystem::path> paths;
        Vector<String> stems;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".xml") {
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

        for (size_t i = 0; i < paths.size(); ++i) {
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

        auto doc = BECore::New<XmlDocument>();

        if (!doc->LoadFromFile(path)) {
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
            _configs[name] = ConfigEntry{doc, path};
        }

        LOG_DEBUG(Format("[ConfigManager] Config loaded: {}", name.ToStringView()).c_str());
    }

}  // namespace BECore
