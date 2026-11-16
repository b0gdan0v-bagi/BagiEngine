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
