#pragma once

#include <BECore/Control/ControlClock.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// `record_start`
    struct ReplayRecordStartAnswer {
        BE_CLASS(ReplayRecordStartAnswer)

        BE_REFLECT_FIELD PoolString game;
        BE_REFLECT_FIELD String path;
    };

    /// `record_stop`
    struct ReplayRecordStopAnswer {
        BE_CLASS(ReplayRecordStopAnswer)

        BE_REFLECT_FIELD uint64_t entries = 0;
    };

    /// `replay`: the clock of `step` and the counts; the `mismatch` of an answer with `ok` false is added to the document by hand (`ReplayMismatchJson`)
    struct ReplayAnswer : ControlClock {
        BE_CLASS(ReplayAnswer)

        BE_REFLECT_FIELD uint64_t entries = 0;
        BE_REFLECT_FIELD uint64_t frames = 0;
        BE_REFLECT_FIELD bool ok = false;
    };

}  // namespace BECore
