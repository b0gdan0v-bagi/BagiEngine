#pragma once

#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/RefCounted/IntrusivePtr.h>

namespace BECore {

    /// The backend of this platform (the table in `FileSystem/CLAUDE.md`, "FileWatcher"); null where the platform has no file system to watch (the web)
    IntrusivePtrAtomic<IFileWatchBackend> MakePlatformFileWatchBackend();

}  // namespace BECore
