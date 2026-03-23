#pragma once
#include <BECore/PoolString/PoolStringMap.h>

namespace BECore {

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

        void Initialize();

        bool Mount(PoolString virtualPath, const std::filesystem::path& realPath);
        void Unmount(PoolString virtualPath);

        std::filesystem::path ResolvePath(eastl::string_view virtualPath) const;

        bool Exists(eastl::string_view virtualPath) const;

        const std::filesystem::path& GetRootPath() const {
            return _rootPath;
        }

        void SetRootPath(const std::filesystem::path& rootPath) {
            _rootPath = rootPath;
        }

        std::filesystem::path GetMountedPath(PoolString virtualPath) const;

        /**
         * @brief Recursively enumerate files in a mounted directory matching given extensions
         *
         * @param mountPoint Virtual mount point (e.g., "assets")
         * @param extensions File extensions to match (e.g., {".png", ".jpg"})
         * @return Vector of virtual paths (e.g., "assets/subdir/file.png"), sorted
         */
        eastl::vector<PoolString> EnumerateFiles(PoolString mountPoint, eastl::span<const eastl::string_view> extensions) const;

    private:
        std::filesystem::path _rootPath;
        UnorderedPoolMap<std::filesystem::path> _mountPoints;

        /**
         * Найти корневую директорию проекта автоматически
         */
        std::filesystem::path FindRootDirectory() const;
    };

}  // namespace BECore
