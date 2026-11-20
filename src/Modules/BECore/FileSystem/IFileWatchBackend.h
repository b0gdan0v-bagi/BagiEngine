#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <filesystem>

namespace BECore {

    /// Where a backend reports a change. Implemented by FileWatcher
    class IFileWatchSink {
    public:
        virtual ~IFileWatchSink() = default;

        /// A file or a folder at @p realPath changed. Any thread, never blocks on the main thread; the facade filters and coalesces
        virtual void Post(const std::filesystem::path& realPath) = 0;
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
        virtual void AddDirectory(const std::filesystem::path& realPath, bool recursive) = 0;

        /// Stops reporting @p realPath; what was posted already stays posted
        virtual void RemoveDirectory(const std::filesystem::path& realPath) = 0;

        /// The facade sets itself before the first AddDirectory and clears the sink (nullptr) after Shutdown()
        virtual void SetSink(IFileWatchSink* sink) = 0;

        /// Called from FileWatcher::Update with the steady clock in seconds. A backend with its own thread leaves it; polling works here
        virtual void Tick(double /*nowSeconds*/) {}

        /// Stops every thread and watch of the backend. No Post happens after it returns
        virtual void Shutdown() = 0;
    };

}  // namespace BECore
