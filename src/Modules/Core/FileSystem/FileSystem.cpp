#include "FileSystem.h"

#include <filesystem>

namespace Core {

    void FileSystem::Initialize() {
        if (_rootPath.empty()) {
            _rootPath = FindRootDirectory();
        }

        // Автоматически монтируем стандартные директории, если они существуют
        if (!_rootPath.empty()) {
            std::filesystem::path configPath = _rootPath / "config";
            if (std::filesystem::exists(configPath) && std::filesystem::is_directory(configPath)) {
                Mount("config", configPath);
            }

            std::filesystem::path assetsPath = _rootPath / "assets";
            if (std::filesystem::exists(assetsPath) && std::filesystem::is_directory(assetsPath)) {
                Mount("assets", assetsPath);
            }
        }
    }

    bool FileSystem::Mount(std::string_view virtualPath, const std::filesystem::path& realPath) {
        if (virtualPath.empty()) {
            return false;
        }

        std::filesystem::path normalizedPath = std::filesystem::absolute(realPath);
        if (!std::filesystem::exists(normalizedPath)) {
            return false;
        }

        if (!std::filesystem::is_directory(normalizedPath)) {
            return false;
        }

        _mountPoints[std::string(virtualPath)] = normalizedPath;
        return true;
    }

    void FileSystem::Unmount(const std::string& virtualPath) {
        _mountPoints.erase(virtualPath);
    }

    std::filesystem::path FileSystem::ResolvePath(std::string_view virtualPath) const {
        if (virtualPath.empty()) {
            return {};
        }

        // Если путь абсолютный, возвращаем как есть
        std::filesystem::path path(virtualPath);
        if (path.is_absolute()) {
            if (std::filesystem::exists(path)) {
                return path;
            }
            return {};
        }

        // Ищем первый компонент пути как точку монтирования
        size_t firstSlash = virtualPath.find_first_of("/\\");
        std::string mountPoint;
        std::string remainingPath;

        if (firstSlash != std::string::npos) {
            mountPoint = virtualPath.substr(0, firstSlash);
            remainingPath = virtualPath.substr(firstSlash + 1);
        } else {
            // Если нет слеша, весь путь - это точка монтирования
            mountPoint = virtualPath;
        }

        // Ищем в смонтированных директориях
        auto it = _mountPoints.find(mountPoint);
        if (it != _mountPoints.end()) {
            std::filesystem::path fullPath = it->second;
            if (!remainingPath.empty()) {
                fullPath /= remainingPath;
            }
            
            if (std::filesystem::exists(fullPath)) {
                return fullPath;
            }
        }

        // Если не нашли в смонтированных, пробуем относительно корневой директории
        if (!_rootPath.empty()) {
            std::filesystem::path rootPath = _rootPath / virtualPath;
            if (std::filesystem::exists(rootPath)) {
                return rootPath;
            }
        }

        // Пробуем относительно текущей рабочей директории
        std::filesystem::path currentPath = std::filesystem::current_path() / virtualPath;
        if (std::filesystem::exists(currentPath)) {
            return currentPath;
        }

        return {};
    }

    bool FileSystem::Exists(std::string_view virtualPath) const {
        std::filesystem::path resolved = ResolvePath(virtualPath);
        return !resolved.empty() && std::filesystem::exists(resolved);
    }

    std::filesystem::path FileSystem::GetMountedPath(const std::string& virtualPath) const {
        auto it = _mountPoints.find(virtualPath);
        if (it != _mountPoints.end()) {
            return it->second;
        }
        return {};
    }

    std::filesystem::path FileSystem::FindRootDirectory() const {
        // Получаем путь к исполняемому файлу
        std::filesystem::path exePath;
        
        // Пробуем найти корневую директорию, поднимаясь вверх от exe
        std::filesystem::path current = std::filesystem::current_path();
        
        // Ищем директорию с config/example_config.xml или другими маркерами
        std::filesystem::path searchPath = current;
        int maxDepth = 10; // Ограничение глубины поиска
        
        for (int i = 0; i < maxDepth; ++i) {
            std::filesystem::path configDir = searchPath / "config";
            if (std::filesystem::exists(configDir) && std::filesystem::is_directory(configDir)) {
                return searchPath;
            }
            
            if (searchPath.has_parent_path() && searchPath != searchPath.parent_path()) {
                searchPath = searchPath.parent_path();
            } else {
                break;
            }
        }
        
        // Если не нашли, возвращаем текущую директорию
        return current;
    }

}  // namespace Core

