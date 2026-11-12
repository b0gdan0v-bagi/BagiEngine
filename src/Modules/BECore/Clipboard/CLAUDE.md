# Clipboard Guidelines

The text clipboard of the game: `CoreManager::GetClipboard()` (`Clipboard`), with a pluggable backend (`IClipboardBackend`). Plain text only; no images, files, rich text or primary selection. ImGui's own clipboard in the editor is separate (it keeps using the SDL backend of ImGui). Main thread only.

| File | Purpose |
|------|---------|
| `ClipboardTypes.h` | `ClipboardStatus` (`Ok`, `Empty`, `Refused`, `Unavailable`) and `ToString` (`ok` / `empty` / `refused` / `unavailable`, the words of the control API), `ClipboardText {status, text}`, `ClipboardCallback`, `kMaxClipboardBytes` (1 MiB) |
| `IClipboardBackend.h` | The platform half: `IsAvailable`, `WriteText`, `BeginRead` / `PollRead`, `TakePasted` (default false) |
| `Clipboard.h/.cpp` | The facade: orders the requests, shares one read, applies the size limit, normalises the status |
| `FakeClipboard.h` | Header-only backend for tests (public, game tests use it too) and `ScopedClipboardBackend` |
| `../../Events/src/Events/ClipboardEvents.h` | `ClipboardEvents::PasteEvent{text}` |

## Statuses

`Ok`: the text is there (a write: the platform took it). `Empty`: the clipboard holds no text. `Refused`: the platform or the user said no, or the text is longer than `kMaxClipboardBytes`. `Unavailable`: there is no clipboard API at all (no backend, a page over plain `http://`). None is an assert or a crash. `ClipboardText::text` is non-empty only with `Ok`: the facade turns an `Ok` with no text into `Empty` and drops the text of every other status, whatever the backend said.

## API

`SetBackend` (null is no clipboard), `IsAvailable()`, `bool SetText(text)` (false when unavailable, over the limit or refused), `RequestText(callback)`, `RequestPaste()`, `Update()`, `HasPending()`.

## Delivery rules

- **A callback never runs inside `RequestText`.** Every read is answered from `Update()`, which `CoreManager::OnGameCycle` calls right after `ProcessEvents()` (inside "Input") for every `FrameKind`, a Held frame too. A browser read is asynchronous, and one rule is simpler than two. No backend or `IsAvailable() == false` is answered with `Unavailable` at the next `Update()`.
- **One read in flight.** Requests that arrive while it runs share its result. Callbacks run in the order of the requests.
- **A callback may ask again.** The new request is answered at the next `Update()` (no unbounded loop in one `Update()`). A callback must not call `Update()` (ignored).
- **`SetBackend` during a read** asks the new backend again for the requests of the old read, first in line.
- **Pastes.** `RequestPaste()` is a `RequestText` whose `Ok` answer is emitted as `PasteEvent` (the shortcut of the SDL backend uses it); `Empty` and `Refused` emit nothing. `Update()` also asks `TakePasted` (up to a few per call) and emits `PasteEvent` for each, but **drops a paste while `CoreManager::GetPause().IsPaused()`**: a paste the game was not running for is not kept for later. An answer to a request is not dropped by a pause.
- If a browser delivers both the shortcut and the `paste` event for one press the game gets two identical `PasteEvent`s. This is accepted: pasting the same text twice is harmless.
- **Zero allocations** in a steady frame: `Update()` with nothing pending does not allocate (the containers are members and keep their capacity).

## Desktop

`SDLClipboardBackend` (`SDL/src/CoreSDL/SDLClipboard.h`) runs on `SDL_SetClipboardText` / `SDL_GetClipboardText`; `SDL3ApplicationFabric::Create` installs it (`MakePlatformClipboardBackend()`). It is available once SDL video is initialised, and a read finishes at once (answered at the next `Update()` all the same). A headless run (the `dummy` driver) keeps the clipboard in SDL's memory, so a control smoke never touches the system one. The paste shortcut (`IsPasteShortcut`: V with Cmd on Apple, Ctrl elsewhere, Shift allowed, Alt and the other primary modifier not, no key repeat) is turned into `RequestPaste()` by `SDLEventsProvider` under the rules of keys: not while paused and not while ImGui wants the keyboard; the `KeyDownEvent` is emitted as before.

## Tests

`FakeClipboard`: content as a string, `SetContent`, `Content()`, `SetAvailable(false)`, `SetRefuse(true)` (reads answer `Refused`, writes return false), `SetDeferred(n)` (a read finishes after `n` polls), `QueuePasted(text)`, counters `WriteCount()` / `ReadCount()`. `ScopedClipboardBackend` installs a backend on `CoreManager::GetClipboard()` and puts the old one back, so a test never leaks a fake into the next. The real system clipboard is never touched by ctest.
