#pragma once

#include <cstdint>

namespace BECore {

    class ControlService;

    /// Most frames one `step` may ask for. `quit` still works while a step runs, so a long one can be ended
    inline constexpr uint32_t kMaxStepFrames = 36000;

    /**
     * @brief Registers the methods that need nothing but the core services.
     *
     * | Method | Params | Result |
     * |--------|--------|--------|
     * | ping   | none | {"pong":true,"protocol":ControlService::kProtocolVersion} |
     * | status | none | {"frame","totalTime","deltaTime","fixedDeltaTime"} of CoreManager::GetTime(), "manualClock", "paused" and "log":{"warnings","errors"} (totals since the start) |
     * | step   | {"frames": 1 to kMaxStepFrames (default 1), "dt": 0 to Time::kMaxFrameTime seconds (default 1/60)} | the clock as `status` gives it, after the last frame |
     * | pause  | none | {"paused":true}; the game holds from the next frame (a no-op that answers ok with the manual clock, which is a pause) |
     * | resume | none | {"paused":false}; the code `manual_clock` with the manual clock, which only `step` advances |
     * | list_methods | none | {"methods":[{"name","summary","params":[{"name","type","required","description"}]}]}, sorted by name |
     * | log_mark | none | {"mark","warnings","errors"}; `mark` selects the lines logged from now on |
     * | log_since | {"mark", "level": "warning" (default) or "error"} | {"lines":[{"sequence","level","time","message","category"?}],"warnings","errors","dropped","next","truncated"?} |
     * | log_subscribe | {"enabled", "level"?} | {"enabled","level"}; while on, every new line is pushed as the event `log` at the end of the frame |
     * | quit   | none | {}; then ApplicationEvents::QuitEvent, so the application stops at the end of the frame. Bypasses the barrier |
     *
     * `status`, `log_mark` and `log_since` flush the log first, so a line logged earlier in the same frame is counted. It also starts `ControlService::Log()`.
     *
     * It also registers the by-name methods (`RegisterReflectionControl`).
     *
     * `step` is a barrier; what it means on the real and the manual clock: Control/CLAUDE.md, "Manual clock and `step`".
     *
     * Registering twice replaces the handlers, so calling this again is harmless.
     */
    void RegisterControlBuiltins(ControlService& service);

}  // namespace BECore
