#pragma once

#include <Core/Utils/Singleton.h>
#include <string>
#include <filesystem>
#include <vector>
#include <map>

namespace Core {

    /**
     * Система монтирования файловой системы
     * Позволяет монтировать директории под виртуальными путями
     * и преобразовывать виртуальные пути в реальные
     */
    class FileSystem : public Singleton<FileSystem> {
    public:
        FileSystem() = default;
        ~FileSystem() override = default;

        /**
         * Инициализация файловой системы
         * Устанавливает корневую директорию приложения
         */
        void Initialize();

        /**
         * Монтировать директорию под виртуальным путем
         * @param virtualPath Виртуальный путь (например, "config", "assets")
         * @param realPath Реальный путь к директории
         * @return true если монтирование успешно
         */
        bool Mount(const std::string& virtualPath, const std::filesystem::path& realPath);

        /**
         * Размонтировать виртуальный путь
         * @param virtualPath Виртуальный путь для размонтирования
         */
        void Unmount(const std::string& virtualPath);

        /**
         * Преобразовать виртуальный путь в реальный
         * Ищет файл во всех смонтированных директориях
         * @param virtualPath Виртуальный путь (например, "config/example_config.xml")
         * @return Реальный путь к файлу или пустой путь, если не найден
         */
        std::filesystem::path ResolvePath(std::string_view virtualPath) const;

        /**
         * Проверить существование файла по виртуальному пути
         * @param virtualPath Виртуальный путь к файлу
         * @return true если файл существует
         */
        bool Exists(const std::string& virtualPath) const;

        /**
         * Получить корневую директорию приложения
         */
        const std::filesystem::path& GetRootPath() const { return _rootPath; }

        /**
         * Установить корневую директорию приложения
         */
        void SetRootPath(const std::filesystem::path& rootPath) { _rootPath = rootPath; }

        /**
         * Получить реальный путь к смонтированной директории
         * @param virtualPath Виртуальный путь
         * @return Реальный путь или пустой, если не смонтирован
         */
        std::filesystem::path GetMountedPath(const std::string& virtualPath) const;

    private:
        std::filesystem::path _rootPath;
        std::map<std::string, std::filesystem::path> _mountPoints;
        
        /**
         * Найти корневую директорию проекта автоматически
         */
        std::filesystem::path FindRootDirectory() const;
    };

}  // namespace Core

