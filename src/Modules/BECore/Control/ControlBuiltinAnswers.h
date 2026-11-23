#pragma once

#include <BECore/Control/ControlClock.h>
#include <BECore/Control/ControlEnvelope.h>
#include <BECore/Control/ControlWindowStatus.h>
#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// `ping`
    struct ControlPingAnswer {
        BE_CLASS(ControlPingAnswer)

        BE_REFLECT_FIELD bool pong = true;
        BE_REFLECT_FIELD int32_t protocol = 0;
    };

    /// The totals of warnings and errors logged since the start, the `log` member of `status`
    struct ControlLogTotals {
        BE_CLASS(ControlLogTotals)

        BE_REFLECT_FIELD uint64_t errors = 0;
        BE_REFLECT_FIELD uint64_t warnings = 0;
    };

    /// `status`: the clock, the log totals, whether the clock is manual, whether the game is paused and the window
    struct ControlStatusAnswer : ControlClock {
        BE_CLASS(ControlStatusAnswer)

        BE_REFLECT_FIELD ControlLogTotals log;
        BE_REFLECT_FIELD bool manualClock = false;
        BE_REFLECT_FIELD bool paused = false;
        BE_REFLECT_FIELD ControlWindowStatus window;
    };

    /// `pause` and `resume`
    struct ControlPausedAnswer {
        BE_CLASS(ControlPausedAnswer)

        BE_REFLECT_FIELD bool paused = false;
    };

    /// `log_mark`
    struct ControlLogMarkAnswer {
        BE_CLASS(ControlLogMarkAnswer)

        BE_REFLECT_FIELD uint64_t errors = 0;
        BE_REFLECT_FIELD uint64_t mark = 0;
        BE_REFLECT_FIELD uint64_t warnings = 0;
    };

    /// `log_subscribe`: `level` is one of the two level names the log answers with
    struct ControlLogSubscribeAnswer {
        BE_CLASS(ControlLogSubscribeAnswer)

        BE_REFLECT_FIELD bool enabled = false;
        BE_REFLECT_FIELD PoolString level;
    };

    /// One parameter of a method in `list_methods`. A mirror of `ControlParamInfo`, which stays an aggregate that modules brace-initialise (`BE_CLASS` adds a virtual)
    struct ControlListedParam {
        BE_CLASS(ControlListedParam)

        BE_REFLECT_FIELD TextView description;
        BE_REFLECT_FIELD TextView name;
        BE_REFLECT_FIELD bool required = false;
        BE_REFLECT_FIELD TextView type;
    };

    /// One method of `list_methods`; a method nobody described has an empty `summary` and no `params`
    struct ControlListedMethod {
        BE_CLASS(ControlListedMethod)

        BE_REFLECT_FIELD TextView name;
        BE_REFLECT_FIELD ArenaVector<ControlListedParam> params;
        BE_REFLECT_FIELD TextView summary;
    };

    /// `list_methods`
    struct ControlMethodsAnswer {
        BE_CLASS(ControlMethodsAnswer)

        BE_REFLECT_FIELD ArenaVector<ControlListedMethod> methods;
    };

    /// One call of a `batch`: `ok` and its `result` (the call's own document, borrowed from the batch), or the `error` of a failed one
    struct ControlBatchResult {
        BE_CLASS(ControlBatchResult)

        BE_REFLECT_FIELD Optional<ControlErrorAnswer> error;
        BE_REFLECT_FIELD bool ok = false;
        BE_REFLECT_FIELD Optional<JsonPayload> result;
    };

    /// `batch`: the answers in call order; `stopped` is the index of the call that ended the batch. A local of the handler: the results are views of the batch's answers
    struct ControlBatchAnswer {
        BE_CLASS(ControlBatchAnswer)

        BE_REFLECT_FIELD ArenaVector<ControlBatchResult> results;
        BE_REFLECT_FIELD Optional<uint64_t> stopped;
    };

    /// `call_method`: `result` is what the method's own serializer wrote, absent for a method that returns nothing. `method` and `root` are views of the client's text, which the handler holds
    struct ReflectionMethodAnswer {
        BE_CLASS(ReflectionMethodAnswer)

        BE_REFLECT_FIELD TextView method;
        BE_REFLECT_FIELD Optional<JsonPayload> result;
        BE_REFLECT_FIELD TextView root;
    };

}  // namespace BECore
