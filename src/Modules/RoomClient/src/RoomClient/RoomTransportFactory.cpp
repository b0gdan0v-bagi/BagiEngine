#include "RoomTransport.h"

#if BE_WITH_REMOTE_CONTROL
#include "NativeRoomTransport.h"
#endif

#if PLATFORM_WEB
#include "EmscriptenRoomTransport.h"
#endif

namespace BECore {

    UniquePtr<IRoomTransport> MakeDefaultRoomTransport() {
#if PLATFORM_WEB
        return eastl::make_unique<EmscriptenRoomTransport>();
#elif BE_WITH_REMOTE_CONTROL
        return eastl::make_unique<NativeRoomTransport>();
#else
        // A build without sockets that is not the browser has no transport
        return nullptr;
#endif
    }

}  // namespace BECore
