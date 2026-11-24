#pragma once

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <atomic>
#include <cstdint>

namespace BECore {

    class FileWatcher;

    /// How long a path must stay quiet before its change is delivered, in seconds: an editor's save is a burst (a temp file, a rename, sometimes a second write)
    inline constexpr double kFileWatchSettleSeconds = 0.15;

    /**
     * A path whose file or folder changed. A delivery has one kind: the callback asks FileSystem::Exists when it needs to tell a removal from a write.
     *
     * A consumer that could not use the change (a config that did not parse) sets @p ok to false and @p error to the reason. Only FileWatcher::Apply
     * reports it back; the delivery of a native notification has nobody to tell.
     */
    struct FileChange {
        PoolString virtualPath;
        bool ok = true;
        String error;
    };

    using FileWatchCallback = MoveOnlyFunction<void(FileChange&)>;

    /// What a rescan found about one file compared with the state at Start or its last delivery
    CORE_ENUM(FileChangeKind, uint8_t, Changed, Added, Removed)

    /// A change a rescan found and has not delivered: the virtual path, what happened and the consumers (the `consumer` names of the watches) it would notify
    struct PendingFileChange {
        PoolString virtualPath;
        FileChangeKind kind = FileChangeKind::Changed;
        Vector<PoolString> consumers;
    };

    /// A change FileWatcher::Apply delivered; @p ok is false and @p error set when a consumer reported that it could not use it
    struct AppliedFileChange {
        PoolString virtualPath;
        Vector<PoolString> consumers;
        bool ok = true;
        String error;
    };

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
        void Start(IntrusivePtr<IFileWatchBackend> backend);

        /// Ends every watch (their handles turn inactive) and shuts the backend down. Not from a callback
        void Stop();

        [[nodiscard]] bool IsRunning() const {
            return _backend != nullptr;
        }

        /**
         * @brief Calls @p callback when the file at @p virtualPath changes (written, replaced by a rename, created, removed)
         *
         * The file is watched in every layer of its mount point whose folder exists, and reported by its virtual path. A path that is not under a mount
         * point, or whose folder exists in no layer, gives an inactive handle. @p consumer names the watch in a rescan (empty: the watched path).
         */
        [[nodiscard]] FileWatchHandle WatchFile(StringView virtualPath, FileWatchCallback callback, StringView consumer = {});

        /// Calls @p callback for every file inside the folder at @p virtualPath, subfolders included, with the virtual path of that file
        [[nodiscard]] FileWatchHandle WatchFolder(StringView virtualFolder, FileWatchCallback callback, StringView consumer = {});

        /// Once per frame (CoreManager::OnGameCycle): ticks the backend, takes the posts and delivers the paths that were quiet for kFileWatchSettleSeconds
        void Update();

        /// Update() at @p nowSeconds of a steady clock; tests pass their own time, so no sleep drives the settle window
        void Update(double nowSeconds);

        /**
         * @brief Looks at the disk now and lists what differs from the state at Start or the last delivery; delivers nothing
         *
         * Compares the write time and size of every file under the watched directories, sorted by virtual path. Empty when the watcher is not
         * running. Not from a callback. It stats every watched file: for a request (the editor's button, the control API), never per frame.
         */
        [[nodiscard]] Vector<PendingFileChange> Rescan();

        /// Delivers what Rescan() would list through the normal path (the callbacks, the settle window skipped) and returns it with what the consumers said
        Vector<AppliedFileChange> Apply();

        /// Apply() for the listed virtual paths only; a path that has no pending change is left out of the result
        Vector<AppliedFileChange> Apply(Span<const StringView> virtualPaths);

    private:
        friend class FileWatchHandle;

        struct Watch {
            uint64_t id = 0;
            bool folder = false;
            bool alive = true;
            String virtualPath;
            PoolString virtualName;  ///< the reported path of a file watch
            PoolString consumer;
            Vector<Path> targets;      ///< the file (or the folder) in each layer, absolute and normalised
            Vector<Path> directories;  ///< what this watch holds in the backend
            FileWatchCallback callback;
        };

        /// A directory several watches share: the backend sees it once, recursive while any user wants it so
        struct DirectoryUse {
            Path path;
            int uses = 0;
            int recursiveUses = 0;
        };

        struct Pending {
            uint64_t watchId = 0;
            PoolString virtualPath;
            double lastPost = 0.0;
        };

        /// The write time and size of a file at the last look; the baseline of Rescan
        struct FileState {
            Path path;
            int64_t writeTimeNs = 0;
            uint64_t size = 0;
            bool byApply = false;  ///< set by Apply, cleared by a native delivery: a late notice of the stream for the same state is not delivered again
        };
        using FileStates = HashMap<String, FileState>;  ///< by the normalised real path

        /// A virtual path with a pending change and the watches it concerns
        struct Detected {
            PoolString virtualPath;
            FileChangeKind kind = FileChangeKind::Changed;
            Vector<uint64_t> watchIds;
            Vector<PoolString> consumers;
        };

        void Post(const Path& realPath) override;

        FileWatchHandle MakeWatch(StringView virtualPath, bool folder, FileWatchCallback callback, StringView consumer);
        void AcquireDirectory(const Path& directory, bool recursive);
        void ReleaseDirectory(const Path& directory, bool recursive);
        void RouteChange(const Path& realPath, double nowSeconds);
        void Schedule(uint64_t watchId, PoolString virtualPath, double nowSeconds);
        void Deliver(double nowSeconds);
        [[nodiscard]] Watch* FindWatch(uint64_t id) const;
        [[nodiscard]] bool IsWatchActive(uint64_t id) const;
        void EndWatch(uint64_t id);
        void SweepDeadWatches();

        /// Whether @p path (normalised) is a file @p watch reports; @p virtualPath is the path it reports it by
        [[nodiscard]] static bool Matches(const Watch& watch, const Path& path, PoolString& virtualPath);
        /// Whether @p path is below a target of @p watch; unlike Matches it interns nothing
        [[nodiscard]] static bool Covers(const Watch& watch, const Path& path);
        /// Whether Apply delivered the state the files of @p virtualPath have now, so a notice of the stream for it has nothing to add
        [[nodiscard]] bool IsAppliedAndUnchanged(const Watch& watch, StringView virtualPath) const;
        /// The file of @p virtualPath in each layer of @p watch
        [[nodiscard]] static Vector<Path> LayerFiles(const Watch& watch, StringView virtualPath);
        static void CollectFiles(const Watch& watch, FileStates& out);
        [[nodiscard]] Vector<Detected> Detect() const;
        /// Makes the baseline of @p realPath what the disk says now
        void RefreshBaseline(const Path& realPath, bool byApply);
        void RefreshBaseline(const Watch& watch, StringView virtualPath, bool byApply);
        /// Remembers the files of a new watch that no other watch knew
        void SnapshotWatch(const Watch& watch);
        void PruneBaseline();
        Vector<AppliedFileChange> ApplyDetected(const Vector<Detected>& detected);

        const FileSystem& _fileSystem;
        IntrusivePtr<IFileWatchBackend> _backend;
        uint64_t _nextId = 1;
        bool _delivering = false;
        bool _hasDeadWatches = false;
        Vector<UniquePtr<Watch>> _watches;  ///< a callback may add a watch: the Watch of the running callback must not move
        Vector<DirectoryUse> _directories;
        Vector<Pending> _pending;
        Vector<Pending> _due;
        FileStates _baseline;   ///< the files the watches cover, as they were at Start or at their last delivery
        Vector<Path> _routing;  ///< the posts being routed; a member so Update keeps its capacity

        Mutex _queueMutex{BE_LOCK_SITE("File watch queue")};
        Vector<Path> _incoming;              ///< posted from any thread, guarded by _queueMutex
        std::atomic<bool> _hasPosts{false};  ///< lets Update skip the lock when nothing was posted
    };

}  // namespace BECore
