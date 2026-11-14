#pragma once

namespace BECore {

    /**
     * @brief File system with virtual mount points
     *
     * Allows mounting directories under virtual paths and resolving
     * virtual paths to real filesystem paths.
     *
     * Content is layered: a mount point can have several directories ("layers"), and the layer added
     * last has the highest priority. The engine and a game each contribute their own config/ and assets/,
     * and a game file overrides an engine file of the same relative path.
     *
     * @note Access via CoreManager::GetFileSystem()
     */
    class FileSystem {
    public:
        FileSystem() = default;
        ~FileSystem() = default;

        /**
         * @brief Mount config/ and assets/ of every content root
         *
         * Without any content root added beforehand, the project root is searched upwards from the
         * current working directory (first directory that contains config/).
         */
        void Initialize();

        /**
         * @brief Add a content root (a directory with config/ and assets/ inside)
         *
         * Call before Initialize(). The root added last has the highest priority, so add the engine first
         * and the game after it. Adding a root twice is a no-op.
         */
        void AddContentRoot(const std::filesystem::path& root);

        /**
         * @brief Add a directory as the highest priority layer of a mount point
         *
         * @return false if the directory does not exist. A directory that is already a layer is kept as is.
         */
        bool Mount(PoolString virtualPath, const std::filesystem::path& realPath);

        /// Remove a mount point together with all its layers
        void Unmount(PoolString virtualPath);

        std::filesystem::path ResolvePath(eastl::string_view virtualPath) const;

        bool Exists(eastl::string_view virtualPath) const;

        /// Content root with the highest priority (the game's, when one is added)
        const std::filesystem::path& GetRootPath() const {
            return _rootPath;
        }

        /// Replace all content roots with this one
        void SetRootPath(const std::filesystem::path& rootPath);

        /// Highest priority layer of a mount point (where a file written by an editor lands by default)
        std::filesystem::path GetMountedPath(PoolString virtualPath) const;

        /// All layers of a mount point, lowest priority first
        eastl::vector<std::filesystem::path> GetMountLayers(PoolString virtualPath) const;

        /**
         * @brief Recursively enumerate files in all layers of a mount point matching given extensions
         *
         * A file present in several layers is listed once.
         *
         * @param mountPoint Virtual mount point (e.g., "assets")
         * @param extensions File extensions to match (e.g., {".png", ".jpg"})
         * @return Vector of virtual paths (e.g., "assets/subdir/file.png"), sorted
         */
        eastl::vector<PoolString> EnumerateFiles(PoolString mountPoint, eastl::span<const eastl::string_view> extensions) const;

    private:
        eastl::vector<std::filesystem::path> _roots;                          // lowest priority first
        std::filesystem::path _rootPath;                                      // _roots.back()
        UnorderedPoolMap<eastl::vector<std::filesystem::path>> _mountPoints;  // layers, lowest priority first

        /**
         * Найти корневую директорию проекта автоматически
         */
        std::filesystem::path FindRootDirectory() const;
    };

}  // namespace BECore
