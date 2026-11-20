#pragma once

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <atomic>
#include <cstdint>
#include <filesystem>

namespace BECore {

    class FileWatcher;

    /// How long a path must stay quiet before its change is delivered, in seconds: an editor's save is a burst (a temp file, a rename, sometimes a second write)
    inline constexpr double kFileWatchSettleSeconds = 0.15;

    /// A path whose file or folder changed. There is one kind: the callback asks FileSystem::Exists when it needs to tell a removal from a write
    struct FileChange {
        PoolString virtualPath;
    };

    using FileWatchCallback = MoveOnlyFunction<void(const FileChange&)>;

    /**
     * @brief Owns one watch of a FileWatcher: the watch ends when the handle is destroyed or Reset().
     *
     * A handle may be destroyed inside its own callback (the removal waits for the end of the Update). It must not outlive the FileWatcher.
     * An inactive handle (the watcher is not running, nothing to watch) is valid and does nothing.
     */
    class FileWatchHandle {
    public:
        FileWatchHandle() = default;
        ~FileWatchHandle();

        FileWatchHandle(const FileWatchHandle&) = delete;
        FileWatchHandle& operator=(const FileWatchHandle&) = delete;
        FileWatchHandle(FileWatchHandle&& other) noexcept;
        FileWatchHandle& operator=(FileWatchHandle&& other) noexcept;

        /// Whether the watch still exists: false after Reset(), after FileWatcher::Stop() and for a handle the watcher refused
        [[nodiscard]] bool IsActive() const;

        /// Ends the watch now
        void Reset();

    private:
        friend class FileWatcher;

        FileWatchHandle(FileWatcher* watcher, uint64_t id) : _watcher(watcher), _id(id) {}

        FileWatcher* _watcher = nullptr;
        uint64_t _id = 0;
    };

    /**
     * @brief Tells the game that a file or a folder of the file system changed on disk. `CoreManager::GetFileWatcher()`.
     *
     * Contract, delivery rules and the traps: `FileSystem/CLAUDE.md`, "FileWatcher". The API and every callback are main thread only; a backend may post
     * from its own thread. Without Start() (a web build, the setting off) Watch* returns an inactive handle and Update() does nothing.
     */
    class FileWatcher final : private IFileWatchSink {
    public:
        /// @p fileSystem resolves the virtual paths; it must outlive the watcher
        explicit FileWatcher(const FileSystem& fileSystem);
        ~FileWatcher() override;

        FileWatcher(const FileWatcher&) = delete;
        FileWatcher& operator=(const FileWatcher&) = delete;
        FileWatcher(FileWatcher&&) = delete;
        FileWatcher& operator=(FileWatcher&&) = delete;

        /// Runs on @p backend (null: Stop()). A running watcher is stopped first, so its watches end
        void Start(IntrusivePtrAtomic<IFileWatchBackend> backend);

        /// Ends every watch (their handles turn inactive) and shuts the backend down. Not from a callback
        void Stop();

        [[nodiscard]] bool IsRunning() const {
            return _backend != nullptr;
        }

        /**
         * @brief Calls @p callback when the file at @p virtualPath changes (written, replaced by a rename, created, removed)
         *
         * The file is watched in every layer of its mount point whose folder exists, and reported by its virtual path. A path that is not under a mount
         * point, or whose folder exists in no layer, gives an inactive handle.
         */
        [[nodiscard]] FileWatchHandle WatchFile(StringView virtualPath, FileWatchCallback callback);

        /// Calls @p callback for every file inside the folder at @p virtualPath, subfolders included, with the virtual path of that file
        [[nodiscard]] FileWatchHandle WatchFolder(StringView virtualFolder, FileWatchCallback callback);

        /// Once per frame (CoreManager::OnGameCycle): ticks the backend, takes the posts and delivers the paths that were quiet for kFileWatchSettleSeconds
        void Update();

        /// Update() at @p nowSeconds of a steady clock; tests pass their own time, so no sleep drives the settle window
        void Update(double nowSeconds);

    private:
        friend class FileWatchHandle;

        struct Watch {
            uint64_t id = 0;
            bool folder = false;
            bool alive = true;
            String virtualPath;
            PoolString virtualName;                     ///< the reported path of a file watch
            Vector<std::filesystem::path> targets;      ///< the file (or the folder) in each layer, absolute and normalised
            Vector<std::filesystem::path> directories;  ///< what this watch holds in the backend
            FileWatchCallback callback;
        };

        /// A directory several watches share: the backend sees it once, recursive while any user wants it so
        struct DirectoryUse {
            std::filesystem::path path;
            int uses = 0;
            int recursiveUses = 0;
        };

        struct Pending {
            uint64_t watchId = 0;
            PoolString virtualPath;
            double lastPost = 0.0;
        };

        void Post(const std::filesystem::path& realPath) override;

        FileWatchHandle MakeWatch(StringView virtualPath, bool folder, FileWatchCallback callback);
        void AcquireDirectory(const std::filesystem::path& directory, bool recursive);
        void ReleaseDirectory(const std::filesystem::path& directory, bool recursive);
        void RouteChange(const std::filesystem::path& realPath, double nowSeconds);
        void Schedule(uint64_t watchId, PoolString virtualPath, double nowSeconds);
        void Deliver(double nowSeconds);
        [[nodiscard]] Watch* FindWatch(uint64_t id) const;
        [[nodiscard]] bool IsWatchActive(uint64_t id) const;
        void EndWatch(uint64_t id);
        void SweepDeadWatches();

        const FileSystem& _fileSystem;
        IntrusivePtrAtomic<IFileWatchBackend> _backend;
        uint64_t _nextId = 1;
        bool _delivering = false;
        bool _hasDeadWatches = false;
        Vector<UniquePtr<Watch>> _watches;  ///< a callback may add a watch: the Watch of the running callback must not move
        Vector<DirectoryUse> _directories;
        Vector<Pending> _pending;
        Vector<Pending> _due;
        Vector<std::filesystem::path> _routing;  ///< the posts being routed; a member so Update keeps its capacity

        Mutex _queueMutex{BE_LOCK_SITE("File watch queue")};
        Vector<std::filesystem::path> _incoming;  ///< posted from any thread, guarded by _queueMutex
        std::atomic<bool> _hasPosts{false};       ///< lets Update skip the lock when nothing was posted
    };

}  // namespace BECore
