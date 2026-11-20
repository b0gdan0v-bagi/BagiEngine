#include "PlatformFileWatchBackend.h"

#include <BECore/FileSystem/PollingFileWatchBackend.h>

namespace BECore {

    IntrusivePtrAtomic<IFileWatchBackend> MakePlatformFileWatchBackend() {
#if defined(__EMSCRIPTEN__)
        return {};
#elif defined(__APPLE__)
        return MakeFSEventsFileWatchBackend();
#else
        return New<PollingFileWatchBackend>();
#endif
    }

}  // namespace BECore
