# Clipboard Guidelines

The text clipboard of the game: `CoreManager::GetClipboard()` (`Clipboard`), with a pluggable backend (`IClipboardBackend`). Plain text only; no images, files, rich text or primary selection. ImGui's own clipboard in the editor is separate (it keeps using the SDL backend of ImGui). Main thread only.

| File | Purpose |
|------|---------|
| `ClipboardTypes.h` | `ClipboardStatus` (`Ok`, `Empty`, `Refused`, `Unavailable`) and `ToString` (`ok` / `empty` / `refused` / `unavailable`, the words of the control API), `ClipboardText {status, text}`, `ClipboardCallback`, `kMaxClipboardBytes` (1 MiB) |
| `IClipboardBackend.h` | The platform half: `IsAvailable`, `WriteText`, `BeginRead` / `PollRead`, `TakePasted` (default false) |
| `Clipboard.h/.cpp` | The facade: orders the requests, shares one read, applies the size limit, normalises the status |
| `../../SDL/src/CoreSDL/SDLClipboard.h/.cpp` | The two real backends (desktop, browser) and their pure helpers |
| `FakeClipboard.h` | Header-only backend for tests (public, game tests use it too) and `ScopedClipboardBackend` |
| `../../Events/src/Events/ClipboardEvents.h` | `ClipboardEvents::PasteEvent{text}` |

## Statuses

`Ok`: the text is there (a write: the platform took it). `Empty`: the clipboard holds no text. `Refused`: the platform or the user said no, or the text is longer than `kMaxClipboardBytes`. `Unavailable`: there is no clipboard API at all (no backend, a page over plain `http://`). None is an assert or a crash. `ClipboardText::text` is non-empty only with `Ok`: the facade turns an `Ok` with no text into `Empty` and drops the text of every other status, whatever the backend said.

## API

`SetBackend` (null is no clipboard), `IsAvailable()`, `bool SetText(text)` (false when unavailable, over the limit or refused), `RequestText(callback)`, `RequestPaste()`, `Update()`, `HasPending()`.

## Delivery rules

- **A callback never runs inside `RequestText`.** Every read is answered from `Update()`, which `CoreManager::OnGameCycle` calls right after `ProcessEvents()` (inside "Input") for every `FrameKind`, a Held frame too. A browser read is asynchronous, and one rule is simpler than two. No backend or `IsAvailable() == false` is answered with `Unavailable` at the next `Update()`.
- **One read in flight.** Requests that arrive while it runs share its result. Callbacks run in the order of the requests.
- **No cancel.** `RequestText` cannot be withdrawn and a read on the web may wait up to 60 s: a callback that captures an object must be sure the object outlives the answer (a handle or a guard, never a bare `this` of something that can die first).
- **A callback may ask again.** The new request is answered at the next `Update()` (no unbounded loop in one `Update()`). A callback must not call `Update()` (ignored).
- **`SetBackend` during a read** asks the new backend again for the requests of the old read, first in line.
- **Pastes.** `RequestPaste()` is a `RequestText` whose `Ok` answer is emitted as `PasteEvent` (the shortcut of the SDL backend uses it); `Empty` and `Refused` emit nothing. `Update()` also asks `TakePasted` (up to a few per call; also when `IsAvailable()` is false: a page over plain `http://` has no API but its `paste` event comes) and emits `PasteEvent` for each, but **drops a paste while `CoreManager::GetPause().IsPaused()`**: a paste the game was not running for is not kept for later. An answer to a request is not dropped by a pause.
- If a browser delivers both the shortcut and the `paste` event for one press the game gets two identical `PasteEvent`s. This is accepted: pasting the same text twice is harmless.
- **Zero allocations** in a steady frame: `Update()` with nothing pending does not allocate (the containers are members and keep their capacity).

## Desktop

`SDLClipboardBackend` (`SDL/src/CoreSDL/SDLClipboard.h`) runs on `SDL_SetClipboardText` / `SDL_GetClipboardText`; `SDL3ApplicationFabric::Create` installs it (`MakePlatformClipboardBackend()`). It is available once SDL video is initialised, and a read finishes at once (answered at the next `Update()` all the same). A headless run (the `dummy` driver) keeps the clipboard in SDL's memory, so a control smoke never touches the system one. The paste shortcut (`IsPasteShortcut`: V with Cmd on Apple, Ctrl elsewhere, Shift allowed, Alt and the other primary modifier not, no key repeat) is turned into `RequestPaste()` by `SDLEventsProvider` under the rules of keys: not while paused and not while ImGui wants the keyboard; the `KeyDownEvent` is emitted as before.

## Browser

The Emscripten half of `SDLClipboard.cpp` (`WebClipboardBackend`, made by `MakePlatformClipboardBackend()`; vendored SDL 3.2 has no clipboard backend for the web, so the glue is ours). No new page code: the JavaScript is installed from C++ with `EM_ASM` into `window.bagiClipboard`, once, and the runtime functions it calls are declared with `EM_JS_DEPS`; nothing is exported, `cmake/BagiWeb.cmake` and `web/shell.html` stay as they are. Rules of `EM_ASM` text: `SDL/CLAUDE.md`, "Web build", Tests. The pure parts (`StatusFromWebCode`, `IsGestureReadFresh`, `MakeWebClipboardText`) are compiled and tested on the desktop.

- **`IsAvailable()`** is whether `navigator.clipboard` has `writeText` or `readText`; it has not on plain `http://` from a host that is not `localhost`. Reads and writes answer `Unavailable` then (one log warning at start); the `paste` event of the page still works.
- **A read must start inside a gesture** (Safari refuses one made a frame later, and Chrome asks for its permission at the first). SDL's key handler returns `preventDefault` for any key held with Ctrl, so the browser's `paste` event does not fire for the shortcut and the shortcut reaches the game as a normal SDL key event, one frame after the key press. So the page's own capture-phase `keydown` handler starts `readText()` for Ctrl/Cmd+V (no Alt, no repeat) at once and keeps it as `gestureRead`. `BeginRead` takes it if `IsGestureReadFresh` (at most `kGestureReadWindowMs`), else starts a read now (which Safari may refuse outside a gesture, a `Refused` value). A `Paste` button has no gesture read (the game asks a frame after the tap), so Safari may answer `Refused`: the game says so. `IsPasteShortcut` accepts Cmd or Ctrl on the web (the page knows no platform). A key layout whose V is not `v` (Russian) does not match: the same limit as SDL's keycode.
- **Writes** call `writeText` at once. A synchronous throw or a rejected promise keeps the text as `pendingWrite`; the next `pointerup`, `touchend` or `keydown` (capture phase, registered once) tries again inside that gesture (Safari accepts a write only there), up to `kWriteRetryGestures` gestures, one console warning per page load. A new `SetText` replaces a pending text and drops a gesture read (the clipboard is not what it read). `SetText` returns true when the API exists: a web write can still fail later.
- **`PollRead`** costs one `EM_ASM_INT` only while a read is pending; a read that has not finished after `kReadTimeoutMs` (a prompt nobody answers) or was lost is `Refused`, so the facade never waits for ever. The text is copied in two steps (`lengthBytesUTF8`, then `stringToUTF8` into a buffer the C++ side sized) and a text over `kMaxClipboardBytes` is `Refused` before it is copied.
- **`TakePasted`**: the `paste` listener of the window queues `clipboardData.getData('text/plain')` (a short queue, the oldest dropped); an empty one is `Empty` and the facade ignores it. One `EM_ASM_INT` per frame.
- A refusal or a missing API is a value everywhere: one `LOG_WARNING` per reason per page load, no log per call.
- **Not checked:** no real browser has run it (Chrome, Safari desktop and iOS with the paste bubble and a write inside a tap, Firefox, a denied permission, plain `http://`): the owner's look, best with a game that has `Copy` / `Paste` buttons.

## Control API

`RegisterClipboardControl` (`ClipboardControl.h`, called by `InitializeCoreServices()`): `clipboard_set {text}` answers `{ok}` (a missing or non-string `text` is `invalid_params`); `clipboard_get` answers `{status, text}` with the words of `ToString`; `clipboard_paste` answers `{}`. The two that wait defer their answer one frame of zero seconds at a time until the clipboard has answered (a read is answered from the next `Update()`), so they work with the manual clock, and fail with `clipboard_timeout` after `kMaxClipboardWaitFrames` frames. `clipboard_paste` answers when no request is pending any more, so the `PasteEvent` has reached the game by then. A browser page cannot reach this API, so the later look in a browser needs a game with `Copy` / `Paste` buttons.

## Tests

`FakeClipboard`: content as a string, `SetContent`, `Content()`, `SetAvailable(false)`, `SetRefuse(true)` (reads answer `Refused`, writes return false), `SetDeferred(n)` (a read finishes after `n` polls), `QueuePasted(text)`, counters `WriteCount()` / `ReadCount()`. `ScopedClipboardBackend` installs a backend on `CoreManager::GetClipboard()` and puts the old one back, so a test never leaks a fake into the next. The real system clipboard is never touched by ctest.
