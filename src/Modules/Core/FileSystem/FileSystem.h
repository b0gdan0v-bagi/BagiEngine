#pragma once
#include <Core/PoolString/PoolStringMap.h>

namespace Core {

    /**
     * @brief File system with virtual mount points
     * 
     * Allows mounting directories under virtual paths and resolving
     * virtual paths to real filesystem paths.
     * 
     * @note Access via CoreManager::GetFileSystem()
     */
    class FileSystem {
    public:
        FileSystem() = default;
        ~FileSystem() = default;

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
        bool Mount(PoolString virtualPath, const std::filesystem::path& realPath);

        /**
         * Размонтировать виртуальный путь
         * @param virtualPath Виртуальный путь для размонтирования
         */
        void Unmount(PoolString virtualPath);

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
        bool Exists(std::string_view virtualPath) const;

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
        std::filesystem::path GetMountedPath(PoolString virtualPath) const;

    private:
        std::filesystem::path _rootPath;
        UnorderedPoolMap<std::filesystem::path> _mountPoints;
        
        /**
         * Найти корневую директорию проекта автоматически
         */
        std::filesystem::path FindRootDirectory() const;
    };

}  // namespace Core

