#include "RoomTransport.h"

#if BE_WITH_REMOTE_CONTROL
#include "NativeRoomTransport.h"
#endif

namespace BECore {

    eastl::unique_ptr<IRoomTransport> MakeDefaultRoomTransport() {
#if BE_WITH_REMOTE_CONTROL
        return eastl::make_unique<NativeRoomTransport>();
#else
        // The browser transport comes with the web build; any other build without sockets has none
        return nullptr;
#endif
    }

}  // namespace BECore
