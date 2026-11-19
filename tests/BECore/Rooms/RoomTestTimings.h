#pragma once

#include <Rooms/RoomLimits.h>

namespace BECore::Tests {

    /// The usual grace and abandon timeout with no pause between the moves of computers, so a test never waits for a computer
    [[nodiscard]] inline RoomTimings NoComputerDelayTimings() {
        RoomTimings timings;
        timings.computerDelay = std::chrono::milliseconds(0);
        return timings;
    }

}  // namespace BECore::Tests
