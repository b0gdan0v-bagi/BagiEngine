---
paths:
  - "src/Modules/BECore/MainWindow/**/*"
  - "src/Modules/BECore/Control/ControlWindow*"
  - "src/Modules/SDL/src/CoreSDL/SDLMainWindow*"
  - "tests/BECore/Window*"
  - "tests/BECore/ControlWindow*"
  - "tools/control/**/*"
---

# The simulated window of a headless game (`set_window`, `status.window`, `--window`, `--density`)

Additive to protocol version 1 (`kProtocolVersion` stays). A headless game has a window the control API can change: its size, the pixel density, the user scale and the safe area, so a QA run sees a phone, a Retina display or a notch without a device. The module: `src/Modules/BECore/Control/CLAUDE.md`; the type and its limits: `BECore/MainWindow/WindowSimulation.h`; the Python side: `tools/control/CLAUDE.md`.

## Decisions

- **Only a headless window.** `set_window` on a real window is `not_headless` and changes nothing, the user scale too: the call is all or nothing. A real window's size, density and notch belong to the OS and the person; faking them would lie to the platform. The start-up flags need `--headless` the same way.
- **Window units and pixels.** `width` / `height` are window units (what `IMainWindow::GetWidth()` says); the output (screenshot, `ui_tree` `outputRect`, the renderer) is `round(width * density)` by `round(height * density)` pixels (`SimulatedOutputPixels`). The dummy SDL window is given the output size in pixels; `SDLMainWindow` keeps the window units itself and answers `GetPixelDensity()` and `GetSafeArea()` from the simulation, because the dummy driver has no density and no notch.
- **Omitted optional values keep their current value**; `width` and `height` are required. A safe area is an object `{top, right, bottom, left}` of window units, all four required in it; `{0,0,0,0}` clears it.
- **The answer waits one frame** (`DeferFrames(1, 0.0, ...)`, like `screenshot`): the frame lays the scene out for the new size, so `ui_tree` and `screenshot` right after the answer are of the new size on the manual clock too.
- **Not settable:** `displayScale` (the user scale is the lever a QA run needs), rotation and orientation events.

## Limits

Client data, so a violation is `invalid_params` whose message names the field and the range, never an `ASSERT`. They are named constants (`WindowLimits`) and are checked after the request is merged with the current values (`ValidateWindowSimulation`); nothing is applied before everything is valid.

| Field | Allowed |
|-------|---------|
| `width`, `height` | integers, `kMinSize` to `kMaxSize` window units |
| `density` | a finite number, `kMinDensity` to `kMaxDensity` |
| output side | `round(size * density)` at most `kMaxOutputSide` pixels per side |
| `userScale` | `UISettings::kMinUserScale` to `kMaxUserScale`; never clamped silently, out of range is an error |
| `safeArea` insets | finite, not negative; `left + right < width` and `top + bottom < height`, so the safe area is never empty |

`IMainWindow` has two virtuals for it: `IsHeadless()` (false by default) and `Simulate(const WindowSimulation&)` (an error by default: "this window cannot be simulated"). The value passed to `Simulate` has passed `ValidateWindowSimulation`.

## `set_window` and `status.window`

| Method | Params | Answer |
|--------|--------|--------|
| `set_window` | `{width, height, density?, userScale?, safeArea?: {top, right, bottom, left}}` | the `window` object of `status`, after one frame |
| `status` | none | also `window: {width, height, density, displayScale, safeArea: {top, right, bottom, left}, outputWidth, outputHeight, userScale, headless}` |

- `window` reads `IMainWindow`, the renderer (`GetOutputWidth/Height`, 0 without one) and `UIEnvironment`, so it tells a real window's values too (`headless: false`). `width`, `height` and the insets are window units; the insets are made from `GetSafeArea()` against the size. Without a window the shape stays: size and output 0, density and display scale 1, `userScale` the one of `UIEnvironment`.
- Errors: `not_headless` for a real window or no window (the message says why; checked first, nothing changes); `invalid_params` for a wrong type, a fractional or missing `width` / `height`, a `safeArea` that is not an object or lacks one of its four members, a value outside the limits above, and a `Simulate()` that refuses. The message names the field.
- Order inside the handler: read, merge with the current values, validate everything, then `IMainWindow::Simulate`, then `UIEnvironment::SetUserScale`, so a refusal never leaves the user scale changed. The answer is built after the frame, so it is the state `ui_tree` and `screenshot` see next.
- `ControlWindow.cpp` holds no SDL: a test drives it with a fake headless `IMainWindow` (`tests/BECore/ControlWindowTest.cpp`).

## The headless SDL window (`SDLMainWindow`)

- **Why pixels.** The dummy video driver has no density and no notch, and the software renderer takes its output size from the SDL window. So the SDL window is `round(width * density)` by `round(height * density)` pixels (`SDL_SetWindowSize`, then `SDL_SyncWindow`), and the window units, the density and the insets live in `SDLMainWindow` itself. The renderer, a capture, `ui_tree` and the layout of a Screen canvas follow without a line of their own: the dummy driver applies the size at once, so `GetOutputWidth/Height` answer the new size right after `Simulate` (`SDLCaptureTest`).
- **What it reports while the window exists.** `GetWidth()` / `GetHeight()` the units that were set (the config's size at density 1 before any simulation, or what `SetHeadlessSimulation` gave), `GetPixelDensity()` the simulated density, `GetSafeArea()` `SimulatedSafeRect` (the whole window until insets are set). `GetDisplayScale()` stays SDL's (1). `Destroy()` resets all of it; a new `Initialize()` starts from the config again (or from `SetHeadlessSimulation`, which survives `Destroy()` like `SetHeadless`).
- **A real window** keeps asking SDL for all of it, and `Simulate` on it is an error. A window without a handle is an error too.
- **Events.** `Simulate` emits `SDLEvents::SafeAreaChangedEvent` when the safe rect changed (a resize alone moves it), once per change. The resize and pixel-size events come from SDL on the next poll, the same path as a real resize. Mouse events stay in window units times the density, which is now the simulated one.
- **Start size.** `SetHeadlessSimulation(Optional<WindowSize>, Optional<float>)` before `Initialize()` creates the window at that size (a value not given stays the config's size / density 1); `Initialize()` validates the result and fails with a log line when it is outside the limits.

## Start-up flags `--window` and `--density`

| Flag | Value | Meaning |
|------|-------|---------|
| `--window=WxH` | two integers, `x` lower case | the start size of a headless window in window units; without it the size of `SDLWindowConfig.xml` |
| `--density=D` | a finite number | the start pixel density; without it 1 |

- Both need `--headless` (and so `--control`), else the start-up stops with the `ControlSettings` message: a real window's size and density belong to the system. They are flags only, no `ApplicationConfig` attributes (a per-run QA choice, not a game setting); a game's `headless="true"` in its config does satisfy them.
- `ResolveControlSettings` reads them with the limits of `WindowLimits` (`ValidateWindowSimulation`): a bare flag, a value that is not `WxH` of integers (or a number), a size or density outside its range, and the output side of both together are errors that name the flag. A flag that appears twice: the last wins. Unknown lookalikes (`--windows=`) are ignored like every unknown flag.
- Path: `ControlSettings` -> `ApplicationOptions` -> `SDL3ApplicationFabric::Create` -> `SDLMainWindow::SetHeadlessSimulation` before `Initialize()`, which creates the dummy window at `round(size * density)` pixels. `--density` alone keeps the config's size, whose output side only `Initialize()` can check (the check of the previous section).

## The Python side and the checks

- `GameSession.set_window(width, height, density=None, user_scale=None, safe_area=None)` and the MCP tool `set_window` send only what is given (`userScale`, `safeArea` in the wire names); `safe_area` is a dict or a tuple `top, right, bottom, left` (a tuple of another length is a `SessionError` before the call). The limits are the engine's alone: the session does not repeat them, so a bad value is the game's `invalid_params`.
- `GameSession.launch(window="WxH", density=D)` and the MCP `launch_game` add `--window=` / `--density=` to the flags; a malformed `window`, or either with `headless=False`, is a `SessionError` before the game starts. The flags are part of the header of a recording, and `bagi_play` hands them to the game as any other extra flag.
- The smoke (`tools/control/tests/smoke.py`, `check_window_control`) sets 480 x 480 at density 1 and at 2 with a notch, and checks the answer against `status.window`, the screenshot size and, for every laid-out Screen canvas of `ui_tree`, that `bounds * scale` is the output, the safe area keeps the inset and an `outputRect` is its `rect` times the scale; then bad values (`invalid_params`, nothing changed) and the restore. It does not demand that every element lies inside the output: a game may overflow a small window (`issues` says so). The default scene of `BagiStress` has no canvas, so its `ui` scene (`tools/stress/tests/stress_smoke.py`) runs the step too. A real-window run (`smoke.py --window`) only checks `not_headless` and skips the scenario that starts with `--window=` / `--density=`.
- The QA monkey (`tools/qa`) now gets `window` moves from a game that lists `set_window`: on `BagiStress` they find `OutsideCanvas` of the `ui` scene in small windows, so the walk of `test_stress_monkey.py` leaves that kind out.
