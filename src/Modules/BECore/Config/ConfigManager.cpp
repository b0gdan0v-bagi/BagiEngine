#include "ConfigManager.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/Logger.h>
#include <BECore/Format/Format.h>
#include <BECore/RefCounted/New.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/Awaitables.h>

#include <pugixml.hpp>
#include <filesystem>

namespace BECore {

    void ConfigManager::Initialize() {
        if (_initialized) {
            LOG_INFO("[ConfigManager] Already initialized");
            return;
        }

        LOG_INFO("[ConfigManager] Initializing...");

        // Получаем путь к config/ через FileSystem из CoreManager
        auto configPath = CoreManager::GetFileSystem().ResolvePath("config");
        if (configPath.empty() || !std::filesystem::exists(configPath)) {
            LOG_WARNING("[ConfigManager] Config directory not found, skipping initialization");
            _initialized = true;
            return;
        }

        // Сканируем директорию для поиска XML файлов
        eastl::vector<std::pair<std::filesystem::path, PoolString>> filesToLoad;
        ScanDirectory(configPath, filesToLoad);

        if (filesToLoad.empty()) {
            LOG_WARNING("[ConfigManager] No XML files found in config directory");
            _initialized = true;
            return;
        }

        LOG_INFO(Format("[ConfigManager] Found {} config files", filesToLoad.size()).c_str());

        // Запускаем параллельные задачи загрузки
        eastl::vector<IntrusivePtr<TaskHandle<void>>> handles;
        handles.reserve(filesToLoad.size());

        for (const auto& [path, name] : filesToLoad) {
            auto handle = TaskManager::GetInstance().Run(LoadConfigAsync(path, name));
            handles.push_back(handle);
        }

        // Ждём завершения всех задач
        // Нужно вызывать Update для обработки задач
        bool allDone = false;
        while (!allDone) {
            allDone = true;
            for (auto& handle : handles) {
                if (!handle->IsDone()) {
                    allDone = false;
                    break;
                }
            }

            if (!allDone) {
                // Даём TaskManager обработать задачи главного потока и отложенные задачи
                TaskManager::GetInstance().Update(PassKey<CoreManager>{});
                
                // Небольшая пауза для предотвращения busy-wait
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        LOG_INFO(Format("[ConfigManager] Loaded {} configs", _configs.size()).c_str());
        _initialized = true;
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
                                     eastl::vector<std::pair<std::filesystem::path, PoolString>>& filesToLoad) const {
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
        // Переключаемся на фоновый поток
        co_await SwitchToBackground();

        LOG_DEBUG(Format("[ConfigManager] Loading config: {}", name.ToStringView()).c_str());

        // Создаём ConfigImpl напрямую
        auto impl = BECore::New<XmlConfigImpl>(pugi::xml_document{});
        
        // Используем LoadFromFile для физического пути
        if (!impl->LoadFromFile(path)) {
            LOG_ERROR(Format("[ConfigManager] Failed to load config: {} from {}", 
                           name.ToStringView(), path.string()).c_str());
            co_return;
        }

        // Добавляем в кэш (потокобезопасно)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _configs[name] = std::move(impl);
        }

        LOG_DEBUG(Format("[ConfigManager] Config loaded: {}", name.ToStringView()).c_str());
    }

}  // namespace BECore
