---
paths:
  - "src/Modules/BECore/Control/**/*"
  - "src/Modules/BECore/Time/**/*"
  - "tests/BECore/Control*"
---

# The manual clock as a pause, held frames and `pause` / `resume`

The detail behind `src/Modules/BECore/Control/CLAUDE.md`, "Manual clock and `step`", which has the summary and what the clocks mean for a client.

**The manual clock is a pause** (`CoreManager::GetPause()`, `Time/GamePause.h`; there is no second mechanism): `--manual-clock` starts with the pause set, and a deferred call on the manual clock requests its frames from it when `ControlService::Settle` takes it in (`SetPause` attaches the pause; a service without one, as in most tests, only counts). `Application::RunFrame` asks the pause for the kind of the frame: `Stepped` frames advance the clock by the step's seconds, and the idle cycle runs while nothing is due. On a real clock that is paused (the editor's Pause), `step` requests `N` steps itself and the call waits for `N + 1` frames: the frame that drains the request ends too and counts. A `step` inside a `batch` or `run_until`, which runs in a continuation (`DeferSteppedFrames`), waits for exactly `N`: its frame has ended. `pause` and `resume` drive the same object from the control API (the answer is `{paused}`, the effect starts with the next frame; `status` and every frame of `frame_stats` carry `paused`). With the manual clock, which is always paused, `pause` is a no-op that answers ok and `resume` fails with `manual_clock`. A **held** frame (paused, no step) runs the window, input polling, the control API (`Drain`: `status`, `ui_click`, `screenshot` answer), the widgets and the drawing, and skips game time, tasks, tweens and the scene update; `EndFrame` still counts the deferred calls down, so a `screenshot` on a paused game is answered by a held frame.
