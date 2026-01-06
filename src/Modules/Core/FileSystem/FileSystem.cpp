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
                Mount("config"_intern, configPath);
            }

            std::filesystem::path assetsPath = _rootPath / "assets";
            if (std::filesystem::exists(assetsPath) && std::filesystem::is_directory(assetsPath)) {
                Mount("assets"_intern, assetsPath);
            }
        }
    }

    bool FileSystem::Mount(PoolString virtualPath, const std::filesystem::path& realPath) {
        if (virtualPath.Empty()) {
            return false;
        }

        std::filesystem::path normalizedPath = std::filesystem::absolute(realPath);
        if (!std::filesystem::exists(normalizedPath)) {
            return false;
        }

        if (!std::filesystem::is_directory(normalizedPath)) {
            return false;
        }

        _mountPoints[virtualPath] = normalizedPath;
        return true;
    }

    void FileSystem::Unmount(PoolString virtualPath) {
        _mountPoints.erase(virtualPath);
    }

    std::filesystem::path FileSystem::ResolvePath(std::string_view virtualPath) const {
        if (virtualPath.empty()) {
            return {};
        }

        eastl::string_view eview(virtualPath.data(), virtualPath.size());

        // 2. Если путь абсолютный
        std::filesystem::path path(virtualPath);  // Здесь аллокация неизбежна для std::filesystem::path
        if (path.is_absolute()) {
            return std::filesystem::exists(path) ? path : std::filesystem::path{};
        }

        // 3. Разделяем путь на точку монтирования и остаток через string_view
        size_t firstSlash = eview.find_first_of("/\\");

        eastl::string_view mountPoint;
        eastl::string_view remainingPath;

        if (firstSlash != eastl::string_view::npos) {
            mountPoint = eview.substr(0, firstSlash);
            remainingPath = eview.substr(firstSlash + 1);
        } else {
            mountPoint = eview;
        }

        // 4. Поиск по точке монтирования (Гетерогенный поиск без аллокаций)
        auto it = _mountPoints.Find(mountPoint);

        if (it != _mountPoints.end()) {
            // it->second это std::filesystem::path
            std::filesystem::path fullPath = it->second;

            if (!remainingPath.empty()) {
                // Конвертируем оставшийся кусочек в path при склейке
                fullPath /= std::string_view(remainingPath.data(), remainingPath.size());
            }

            if (std::filesystem::exists(fullPath)) {
                return fullPath;
            }
        }

        // 5. Поиск относительно корня
        if (!_rootPath.empty()) {
            std::filesystem::path rootFullPath = _rootPath / virtualPath;
            if (std::filesystem::exists(rootFullPath)) {
                return rootFullPath;
            }
        }

        // 6. Относительно текущей директории
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

    std::filesystem::path FileSystem::GetMountedPath(PoolString virtualPath) const {
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

