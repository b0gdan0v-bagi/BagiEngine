#include "PlatformFileWatchBackend.h"

#include <BECore/FileSystem/PollingFileWatchBackend.h>

namespace BECore {

    IntrusivePtrAtomic<IFileWatchBackend> MakePlatformFileWatchBackend() {
#if defined(__EMSCRIPTEN__)
        return {};
#else
        return New<PollingFileWatchBackend>();
#endif
    }

}  // namespace BECore
