#include "ControlClock.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>
#include <Generated/ControlClock.gen.hpp>

namespace BECore {

    ControlClock ControlClock::Read() {
        const Time& time = CoreManager::GetTime();
        ControlClock clock;
        clock.frame = time.GetFrameCount();
        clock.totalTime = time.GetTotalTime();
        clock.deltaTime = static_cast<double>(time.GetDeltaTime());
        clock.fixedDeltaTime = static_cast<double>(time.GetFixedDeltaTime());
        return clock;
    }

}  // namespace BECore
