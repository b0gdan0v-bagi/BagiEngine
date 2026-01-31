#include "ConfigManager.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/Logger.h>
#include <BECore/Format/Format.h>
#include <BECore/RefCounted/New.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskGroup.h>
#include <TaskSystem/Awaitables.h>

#include <pugixml.hpp>
#include <filesystem>

namespace BECore {

    void ConfigManager::Initialize() {
        {
            std::lock_guard lock(_mutex);
            if (_initialized) {
                LOG_INFO("[ConfigManager] Already initialized");
                return;
            }
        }

        LOG_INFO("[ConfigManager] Initializing...");

        // Получаем путь к config/ через FileSystem из CoreManager
        auto configPath = CoreManager::GetFileSystem().ResolvePath("config");
        if (configPath.empty() || !std::filesystem::exists(configPath)) {
            LOG_WARNING("[ConfigManager] Config directory not found, skipping initialization");
            std::lock_guard lock(_mutex);
            _initialized = true;
            return;
        }

        // Сканируем директорию для поиска XML файлов
        eastl::vector<eastl::pair<std::filesystem::path, PoolString>> filesToLoad;
        ScanDirectory(configPath, filesToLoad);

        if (filesToLoad.empty()) {
            LOG_WARNING("[ConfigManager] No XML files found in config directory");
            std::lock_guard lock(_mutex);
            _initialized = true;
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

        size_t loadedCount;
        {
            std::lock_guard lock(_mutex);
            loadedCount = _configs.size();
            _initialized = true;
        }

        LOG_INFO(Format("[ConfigManager] Loaded {} configs", loadedCount).c_str());
    }

    XmlNode ConfigManager::GetConfig(PoolString name) const {
        std::lock_guard<std::mutex> lock(_mutex);

        auto it = _configs.find(name);
        if (it != _configs.end() && it->second) {
            return it->second->GetRoot();
        }

        return XmlNode();  // Невалидный узел
    }

    bool ConfigManager::HasConfig(PoolString name) const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _configs.find(name) != _configs.end();
    }

    size_t ConfigManager::GetConfigCount() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _configs.size();
    }

    void ConfigManager::ScanDirectory(const std::filesystem::path& dir,
                                     eastl::vector<eastl::pair<std::filesystem::path, PoolString>>& filesToLoad) const {
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
            std::scoped_lock lock(_mutex);
            if (_configs.find(name) != _configs.end()) {
                LOG_WARNING(Format("[ConfigManager] Config {} already loaded, skipping duplicate", 
                                 name.ToStringView()).c_str());
                co_return;
            }
        }

        // Создаём XmlDocument напрямую
        auto doc = BECore::New<XmlDocument>();
        
        // Используем LoadFromFile для физического пути
        if (!doc->LoadFromFile(path)) {
            LOG_ERROR(Format("[ConfigManager] Failed to load config: {} from {}", 
                           name.ToStringView(), path.string()).c_str());
            co_return;
        }

        // Добавляем в кэш (потокобезопасно)
        {
            std::scoped_lock lock(_mutex);
            // Повторная проверка на случай race condition
            if (_configs.find(name) != _configs.end()) {
                LOG_WARNING(Format("[ConfigManager] Config {} was loaded by another thread, discarding duplicate", 
                                 name.ToStringView()).c_str());
                co_return;
            }
            _configs[name] = doc;
        }

        LOG_DEBUG(Format("[ConfigManager] Config loaded: {}", name.ToStringView()).c_str());
    }

}  // namespace BECore
