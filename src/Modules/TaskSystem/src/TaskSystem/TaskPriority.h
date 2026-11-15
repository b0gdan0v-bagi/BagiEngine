#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /** Orders the main-thread queue (equal priorities are not FIFO); a job on the pool ignores it for now (TaskScheduler::DispatchToThreadPool). */
    CORE_ENUM(TaskPriority, uint8_t, Low, Normal, High, Critical);

    /** The context a task runs in. */
    CORE_ENUM(ThreadType, uint8_t,
              MainThread,  ///< The main thread: its queue is drained in TaskManager::Update (UI and scene state belong here)
              Background,  ///< A thread of the pool
              IO, Custom);

    /** The clock that counts the delay of a delayed task. */
    CORE_ENUM(DelayClock, uint8_t,
              WallClock,  ///< Real time (steady_clock): independent of pause, frame limiting and the manual clock
              GameTime    ///< Game time: moves only through TaskScheduler::AdvanceGameTime (the frame delta from Time)
    );

    /** The state of a task as its handle reports it. */
    CORE_ENUM(TaskStatus, uint8_t, Pending, Running, Completed, Cancelled, Failed);

    /** The error of std::expected<T, TaskError> that a task or a handle returns. */
    CORE_ENUM(TaskError, uint8_t, None, Cancelled, Timeout,
              Exception,  ///< Set by the promise's unhandled_exception (which also asserts)
              InvalidState);

}  // namespace BECore
