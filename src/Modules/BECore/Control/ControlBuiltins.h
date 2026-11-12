#pragma once

namespace BECore {

    class ControlService;

    /// Most frames one `step` may ask for: ten minutes at 60 fps. `quit` still works while a step runs, so a long one can be ended
    inline constexpr uint32_t kMaxStepFrames = 36000;

    /**
     * @brief Registers the methods that need nothing but the core services.
     *
     * | Method | Params | Result |
     * |--------|--------|--------|
     * | ping   | none | {"pong":true,"protocol":ControlService::kProtocolVersion} |
     * | status | none | {"frame","totalTime","deltaTime","fixedDeltaTime"} of CoreManager::GetTime(), "manualClock" and "paused" |
     * | step   | {"frames": 1 to kMaxStepFrames (default 1), "dt": 0 to Time::kMaxFrameTime seconds (default 1/60)} | the clock as `status` gives it, after the last frame |
     * | pause  | none | {"paused":true}; the game holds from the next frame (a no-op that answers ok with the manual clock, which is a pause) |
     * | resume | none | {"paused":false}; the code `manual_clock` with the manual clock, which only `step` advances |
     * | quit   | none | {}; then ApplicationEvents::QuitEvent, so the application stops at the end of the frame. Bypasses the barrier |
     *
     * `step` answers when the frames have run and holds back the requests behind it (a barrier), so a client that
     * pipelines `step` and `get_state` gets the state after the step. With the manual clock (--manual-clock) the frames run
     * only because of it and every one lasts `dt`; on the real clock `dt` is ignored and the frames take their own time, unless the game is paused: then `step` makes
     * `frames` stepped frames of `dt`.
     *
     * Registering twice replaces the handlers, so calling this again is harmless.
     */
    void RegisterControlBuiltins(ControlService& service);

}  // namespace BECore
