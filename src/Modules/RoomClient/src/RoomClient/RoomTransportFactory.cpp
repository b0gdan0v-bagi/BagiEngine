#include "RoomTransport.h"

namespace BECore {

    eastl::unique_ptr<IRoomTransport> MakeDefaultRoomTransport() {
        return nullptr;
    }

}  // namespace BECore
