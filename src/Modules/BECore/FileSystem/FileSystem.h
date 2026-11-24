#pragma once

#include <BECore/File/Path.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringMap.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// @p path absolute and lexically normal (`AbsolutePath` of the file layer); the path as given when the OS cannot say, an empty one stays empty
    [[nodiscard]] Path AbsoluteNormalPath(const Path& path);

    /**
     * @brief Virtual paths over content roots: a mount point has several directories (layers), the last added wins
     *
     * Layers, roots and the lookup order: `FileSystem/CLAUDE.md`. Reached through CoreManager::GetFileSystem().
     */
    class FileSystem {
    public:
        FileSystem() = default;
        ~FileSystem() = default;

        /**
         * @brief Mount config/ and assets/ of every content root
         *
         * With no root added, the project root is searched upwards from the working directory.
         */
        void Initialize();

        /**
         * @brief Add a content root (a directory with config/ and/or assets/ inside)
         *
         * Call before Initialize(). The root added last has the highest priority: the engine first, then the game.
         */
        void AddContentRoot(const Path& root);

        /**
         * @brief Add a directory as the highest priority layer of a mount point
         *
         * @return false if the directory does not exist. A directory that is already a layer is kept as is.
         */
        bool Mount(PoolString virtualPath, const Path& realPath);

        /// Remove a mount point together with all its layers
        void Unmount(PoolString virtualPath);

        Path ResolvePath(StringView virtualPath) const;

        bool Exists(StringView virtualPath) const;

        /// Content root with the highest priority (the game's, when one is added)
        const Path& GetRootPath() const {
            return _rootPath;
        }

        /// Replace all content roots with this one
        void SetRootPath(const Path& rootPath);

        /// Highest priority layer of a mount point: where a file written by an editor lands
        Path GetMountedPath(PoolString virtualPath) const;

        /// All layers of a mount point, lowest priority first
        Vector<Path> GetMountLayers(PoolString virtualPath) const;

        /**
         * @brief Recursively enumerate files in all layers of a mount point matching given extensions
         *
         * A file present in several layers is listed once.
         *
         * @param extensions With the dot, matched case-insensitively (e.g. ".png")
         * @return Virtual paths such as "assets/subdir/file.png", sorted
         */
        Vector<PoolString> EnumerateFiles(PoolString mountPoint, Span<const StringView> extensions) const;

    private:
        Vector<Path> _roots;                          // lowest priority first
        Path _rootPath;                               // _roots.back()
        UnorderedPoolMap<Vector<Path>> _mountPoints;  // layers, lowest priority first

        Path FindRootDirectory() const;
    };

}  // namespace BECore
