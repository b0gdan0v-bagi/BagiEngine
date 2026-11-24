#pragma once

#include <BECore/File/File.h>
#include <BECore/File/Path.h>
#include <BECore/RefCounted/RefCounted.h>

namespace BECore {

    /// Where a backend reports a change. Implemented by FileWatcher
    class IFileWatchSink {
    public:
        virtual ~IFileWatchSink() = default;

        /// A file or a folder at @p realPath changed. Any thread, never blocks on the main thread; the facade filters and coalesces
        virtual void Post(const Path& realPath) = 0;
    };

    /**
     * @brief The platform half of the file watcher: FSEvents, inotify, ReadDirectoryChangesW, stat polling, FakeFileWatchBackend in tests.
     *
     * A backend watches **directories** only and reports the real path of whatever changed inside; FileWatcher (the facade) maps
     * virtual paths to directories, filters by file name, shares a directory between watches and coalesces the bursts of a save.
     * The calls of this interface come from the main thread; only IFileWatchSink::Post may come from another one.
     * Atomic reference counting: a backend with a reader thread is held by that thread's owner and by the facade.
     */
    class IFileWatchBackend : public RefCountedAtomic {
    public:
        ~IFileWatchBackend() override = default;

        /// Starts reporting changes of @p realPath (an existing directory, absolute and normalised). Called once per directory until RemoveDirectory
        virtual void AddDirectory(const Path& realPath, bool recursive) = 0;

        /// Stops reporting @p realPath; what was posted already stays posted
        virtual void RemoveDirectory(const Path& realPath) = 0;

        /// The facade sets itself before the first AddDirectory and clears the sink (nullptr) after Shutdown()
        virtual void SetSink(IFileWatchSink* sink) = 0;

        /// Called from FileWatcher::Update with the steady clock in seconds. A backend with its own thread leaves it; polling works here
        virtual void Tick(double /*nowSeconds*/) {}

        /// Stops every thread and watch of the backend. No Post happens after it returns
        virtual void Shutdown() = 0;
    };

    /**
     * For a backend whose queue overflowed (events were lost): posts every entry below @p dir, so the facade learns of what may have changed.
     * The facade routes files, not folders, so posting the folder itself would tell it nothing. Errors end the walk quietly: a folder
     * that vanished is a change the backend reports anyway.
     */
    inline void PostTree(IFileWatchSink& sink, const Path& dir, bool recursive) {
        static_cast<void>(WalkDirectory(dir.View(), recursive ? WalkMode::Tree : WalkMode::Folder, [&](StringView relative, FileKind /*kind*/) {
            sink.Post(dir / relative);
            return true;
        }));
    }

}  // namespace BECore
