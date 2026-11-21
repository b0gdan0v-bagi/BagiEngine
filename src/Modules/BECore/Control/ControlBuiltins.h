#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonFwd.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace BECore {

    class ControlService;

    /// Most frames one `step` may ask for. `quit` still works while a step runs, so a long one can be ended
    inline constexpr uint32_t kMaxStepFrames = 36000;

    /// Most calls one `batch` may hold
    inline constexpr size_t kMaxBatchCalls = 1000;

    /// The frame clock as `status` and `step` answer it: {"deltaTime","fixedDeltaTime","frame","totalTime"}
    [[nodiscard]] Json DescribeControlClock();

    /// Writes DescribeControlClock() as one object, the form `step` and the writers of the games use; the same text as `DescribeControlClock()` dumps to
    void WriteControlClock(JsonWriter& writer);

    /// The frame count at @p key of @p params, 1 to kMaxStepFrames, @p fallback when absent; invalid_params when it is not an integer in range. Client data
    [[nodiscard]] std::expected<uint32_t, ControlError> ReadControlFrames(const Json& params, StringView key, uint32_t fallback);

    /// The seconds per frame at `dt` of @p params, 0 to Time::kMaxFrameTime, kDefaultStepSeconds when absent; invalid_params when out of range. Client data
    [[nodiscard]] std::expected<double, ControlError> ReadControlFrameSeconds(const Json& params);

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
     * | batch | {"calls":[{"method","params"?}] (1 to kMaxBatchCalls), "continueOnError"?} | {"results":[{"ok":true,"result"} or {"ok":false,"error":{"code","message"}}],"stopped"?: index} |
     * | quit   | none | {}; then ApplicationEvents::QuitEvent, so the application stops at the end of the frame. Bypasses the barrier |
     *
     * `status`, `log_mark` and `log_since` flush the log first, so a line logged earlier in the same frame is counted. It also starts `ControlService::Log()`.
     *
     * `batch` runs the calls in order inside the one request and an error ends it (`stopped` is its index) unless `continueOnError`; `batch` and `quit` are refused per call, and a call that waits
     * for frames (`step`) makes the whole batch wait.
     *
     * `status` also answers `window` (WriteWindowStatus). It also registers the by-name methods (`RegisterReflectionControl`) and `set_window` (`RegisterWindowControl`).
     *
     * `step` is a barrier; what it means on the real and the manual clock: Control/CLAUDE.md, "Manual clock and `step`".
     *
     * Registering twice replaces the handlers, so calling this again is harmless.
     */
    void RegisterControlBuiltins(ControlService& service);

}  // namespace BECore
