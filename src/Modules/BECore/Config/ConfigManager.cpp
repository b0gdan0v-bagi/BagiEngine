#include "ConfigManager.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <EASTL/sort.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/TaskGroup.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskManager.h>

namespace BECore {

    void ConfigManager::Initialize() {
        if (_initialized.load(std::memory_order_acquire)) {
            LOG_INFO("[ConfigManager] Already initialized");
            return;
        }

        LOG_INFO("[ConfigManager] Initializing...");

        // Получаем путь к config/ через FileSystem из CoreManager
        auto configPath = CoreManager::GetFileSystem().ResolvePath("config");
        if (configPath.empty() || !std::filesystem::exists(configPath)) {
            LOG_WARNING("[ConfigManager] Config directory not found, skipping initialization");
            _initialized.store(true, std::memory_order_release);
            return;
        }

        // Сканируем директорию для поиска XML файлов
        eastl::vector<eastl::pair<std::filesystem::path, PoolString>> filesToLoad;
        ScanDirectory(configPath, filesToLoad);

        if (filesToLoad.empty()) {
            LOG_WARNING("[ConfigManager] No XML files found in config directory");
            _initialized.store(true, std::memory_order_release);
            return;
        }

        LOG_INFO(Format("[ConfigManager] Found {} config files", filesToLoad.size()).c_str());

        // Запускаем параллельные задачи загрузки
        TaskGroup group;
        for (const auto& [path, name] : filesToLoad) {
            auto handle = TaskManager::GetInstance().Run(LoadConfigAsync(path, name));
            group.Add(handle);
        }

        // Ждём завершения всех задач
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

    bool ConfigManager::HasConfig(PoolString name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        return _configs.find(name) != _configs.end();
    }

    XmlNode ConfigManager::GetConfig(eastl::string_view name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        auto it = _configs.Find(name);
        if (it != _configs.end() && it->second.doc) {
            return it->second.doc->GetRoot();
        }
        return XmlNode();
    }

    bool ConfigManager::HasConfig(eastl::string_view name) const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        return _configs.Find(name) != _configs.end();
    }

    size_t ConfigManager::GetConfigCount() const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        return _configs.size();
    }

    eastl::vector<PoolString> ConfigManager::GetAllConfigNames() const {
        ASSERT(_initialized.load(std::memory_order_acquire));
        eastl::vector<PoolString> names;
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
        ASSERT(_initialized.load(std::memory_order_acquire));
        std::scoped_lock lock(_writeMutex);
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
        ASSERT(_initialized.load(std::memory_order_acquire));
        std::filesystem::path filePath;
        {
            std::scoped_lock lock(_writeMutex);
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
            std::scoped_lock lock(_writeMutex);
            auto it = _configs.find(name);
            if (it != _configs.end()) {
                it->second.doc = newDoc;
            }
        }

        LOG_INFO(Format("[ConfigManager] Reloaded config '{}'", name.ToStringView()).c_str());
        return true;
    }

    void ConfigManager::ScanDirectory(const std::filesystem::path& dir, eastl::vector<eastl::pair<std::filesystem::path, PoolString>>& filesToLoad) const {
        // filesystem может бросать исключения, но exceptions отключены
        // Просто пропускаем ошибки
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".xml") {
                // Используем stem (имя без расширения) как ключ
                auto stemStr = entry.path().stem().string();
                // Конвертируем std::string в eastl::string_view через c_str()
                auto name = PoolString::Intern(eastl::string_view(stemStr.c_str(), stemStr.length()));
                filesToLoad.push_back({entry.path(), name});

                LOG_DEBUG(Format("[ConfigManager] Found config: {} -> {}", stemStr, entry.path().string()).c_str());
            }
        }
    }

    Task<void> ConfigManager::LoadConfigAsync(std::filesystem::path path, PoolString name) {
        // Задача уже запущена на background потоке через TaskManager::Run

        LOG_DEBUG(Format("[ConfigManager] Loading config: {}", name.ToStringView()).c_str());

        // Проверяем, не загружен ли уже конфиг (защита от дубликатов)
        {
            std::scoped_lock lock(_writeMutex);
            if (_configs.find(name) != _configs.end()) {
                LOG_WARNING(Format("[ConfigManager] Config {} already loaded, skipping duplicate", name.ToStringView()).c_str());
                co_return;
            }
        }

        // Создаём XmlDocument напрямую
        auto doc = BECore::New<XmlDocument>();

        // Используем LoadFromFile для физического пути
        if (!doc->LoadFromFile(path)) {
            LOG_ERROR(Format("[ConfigManager] Failed to load config: {} from {}", name.ToStringView(), path.string()).c_str());
            co_return;
        }

        // Добавляем в кэш (потокобезопасно)
        {
            std::scoped_lock lock(_writeMutex);
            // Повторная проверка на случай race condition
            if (_configs.find(name) != _configs.end()) {
                LOG_WARNING(Format("[ConfigManager] Config {} was loaded by another thread, discarding duplicate", name.ToStringView()).c_str());
                co_return;
            }
            _configs[name] = ConfigEntry{doc, path};
        }

        LOG_DEBUG(Format("[ConfigManager] Config loaded: {}", name.ToStringView()).c_str());
    }

}  // namespace BECore
