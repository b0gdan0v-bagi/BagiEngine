#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief The clock as `status`, `step` and `run_until` answer it; an answer that carries the clock derives this struct (its own keys merge with these, sorted).
     *
     * The floats of `Time` are widened to double: the text clients have always seen.
     */
    struct ControlClock {
        BE_CLASS(ControlClock)

        BE_REFLECT_FIELD uint64_t frame = 0;
        BE_REFLECT_FIELD double totalTime = 0.0;
        BE_REFLECT_FIELD double deltaTime = 0.0;
        BE_REFLECT_FIELD double fixedDeltaTime = 0.0;

        /// The clock of `CoreManager::GetTime()` as it is now; main thread
        [[nodiscard]] static ControlClock Read();
    };

}  // namespace BECore
