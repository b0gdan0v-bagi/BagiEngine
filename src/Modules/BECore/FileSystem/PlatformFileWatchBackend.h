#pragma once

#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/RefCounted/IntrusivePtr.h>

namespace BECore {

    /// The backend of this platform (the table in `FileSystem/CLAUDE.md`, "FileWatcher"); null where the platform has no file system to watch (the web)
    IntrusivePtrAtomic<IFileWatchBackend> MakePlatformFileWatchBackend();

#if defined(__APPLE__)
    /// FSEvents (`FSEventsFileWatchBackend.cpp`)
    IntrusivePtrAtomic<IFileWatchBackend> MakeFSEventsFileWatchBackend();
#endif
#if defined(__linux__)
    /// inotify (`InotifyFileWatchBackend.cpp`)
    IntrusivePtrAtomic<IFileWatchBackend> MakeInotifyFileWatchBackend();
#endif
#if defined(_WIN32)
    /// ReadDirectoryChangesW (`WindowsFileWatchBackend.cpp`)
    IntrusivePtrAtomic<IFileWatchBackend> MakeWindowsFileWatchBackend();
#endif

}  // namespace BECore
