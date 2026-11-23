#pragma once

#include <BECore/Control/ControlClock.h>
#include <BECore/Control/ControlEnvelope.h>
#include <BECore/Control/JsonPayload.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// `record_start`: `path` is a view of the handler's own text
    struct ReplayRecordStartAnswer {
        BE_CLASS(ReplayRecordStartAnswer)

        BE_REFLECT_FIELD PoolString game;
        BE_REFLECT_FIELD TextView path;
    };

    /// `record_stop`
    struct ReplayRecordStopAnswer {
        BE_CLASS(ReplayRecordStopAnswer)

        BE_REFLECT_FIELD uint64_t entries = 0;
    };

    struct ReplayMismatch;

    /**
     * @brief The first entry of a replay that differs, as `replay` answers it; the shape `ReplayMismatchJson` also gives.
     *
     * `entry` is the entry as a line of the file holds it (owned: it is rebuilt from the parsed entry). `kind`, `expected` and `actual` are views of the
     * `ReplayMismatch` they were filled from (`kind` of a static name), which has to outlive every write.
     */
    struct ReplayMismatchAnswer {
        BE_CLASS(ReplayMismatchAnswer)

        BE_REFLECT_FIELD Optional<JsonPayload> actual;
        BE_REFLECT_FIELD Optional<JsonPayload> entry;
        BE_REFLECT_FIELD Optional<ControlErrorAnswer> error;
        BE_REFLECT_FIELD Optional<JsonPayload> expected;
        BE_REFLECT_FIELD uint64_t index = 0;
        BE_REFLECT_FIELD TextView kind;
        BE_REFLECT_FIELD uint64_t line = 0;
    };

    /// `replay`: the clock of `step` and the counts; `mismatch` is there when `ok` is false
    struct ReplayAnswer : ControlClock {
        BE_CLASS(ReplayAnswer)

        BE_REFLECT_FIELD uint64_t entries = 0;
        BE_REFLECT_FIELD uint64_t frames = 0;
        BE_REFLECT_FIELD Optional<ReplayMismatchAnswer> mismatch;
        BE_REFLECT_FIELD bool ok = false;
    };

    /// Fills @p answer from @p mismatch; @p mismatch has to outlive the writes of @p answer
    void FillReplayMismatchAnswer(ReplayMismatchAnswer& answer, const ReplayMismatch& mismatch);

}  // namespace BECore
