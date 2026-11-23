#pragma once

#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/RefCounted/IntrusivePtr.h>

namespace BECore {

    /// The backend of this platform (the table in `FileSystem/CLAUDE.md`, "FileWatcher"); null where the platform has no file system to watch (the web)
    IntrusivePtr<IFileWatchBackend> MakePlatformFileWatchBackend();

#if defined(__APPLE__)
    /// FSEvents (`FSEventsFileWatchBackend.cpp`)
    IntrusivePtr<IFileWatchBackend> MakeFSEventsFileWatchBackend();
#endif
#if defined(__linux__)
    /// inotify (`InotifyFileWatchBackend.cpp`)
    IntrusivePtr<IFileWatchBackend> MakeInotifyFileWatchBackend();
#endif
#if defined(_WIN32)
    /// ReadDirectoryChangesW (`WindowsFileWatchBackend.cpp`)
    IntrusivePtr<IFileWatchBackend> MakeWindowsFileWatchBackend();
#endif

}  // namespace BECore
