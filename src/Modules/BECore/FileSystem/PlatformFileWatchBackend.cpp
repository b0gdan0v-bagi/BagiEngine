#include "PlatformFileWatchBackend.h"

#include <BECore/FileSystem/PollingFileWatchBackend.h>

namespace BECore {

    IntrusivePtr<IFileWatchBackend> MakePlatformFileWatchBackend() {
#if defined(__EMSCRIPTEN__)
        return {};
#elif defined(__APPLE__)
        return MakeFSEventsFileWatchBackend();
#elif defined(__linux__)
        return MakeInotifyFileWatchBackend();
#elif defined(_WIN32)
        return MakeWindowsFileWatchBackend();
#else
        return New<PollingFileWatchBackend>();
#endif
    }

}  // namespace BECore
