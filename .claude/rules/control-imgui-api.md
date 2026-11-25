---
paths:
  - "src/Modules/BECore/Input/ImGui*"
  - "tools/control/**/*"
  - "src/Widgets/ImGuiWidget*"
---

# ImGui over the control API (`imgui_input`, `imgui_find`)

Additive to protocol version 1. A client drives and reads the editor's ImGui the way a person does with a mouse and a keyboard: find an item by name, click its centre, type, press a chord. The code: `src/Modules/BECore/Input/CLAUDE.md` ("The ImGui control methods"); the protocol: `src/Modules/BECore/Control/CLAUDE.md`; the Python side: `tools/control/CLAUDE.md`. Both methods have a `DescribeMethod` (`list_methods`).

## Scope and errors

- **ImGui only.** `click_at`, `wheel_at` and `ui_click` go through `UIInput` to the game and its canvases and never reach `ImGuiIO`; `imgui_input` goes to `ImGuiIO` and never to `UIInput` directly. A click ImGui did not want (`wantCaptureMouse: false`) is not forwarded by this method: the `Viewport` panel is a widget of ImGui and forwards the pointer inside its image to the game itself, as for a person.
- `no_imgui`: no ImGui context (the Game run mode, or a game with no `ImGuiWidget`). `invalid_params`: the text names `events[i]` and the reason, or the filter that is wrong. `imgui_timeout`: ImGui did not finish the frames within `kMaxImGuiWaitFrames`.
- Both methods run their own frames of zero seconds and answer after them, so the manual clock needs no `step`; inside a `batch` each is a barrier like `step`.

## Coordinates

Positions and rects are **output pixels**, the pixels of `screenshot`, `click_at` and `ui_tree` `outputRect`: ImGui's window units times `io.DisplayFramebufferScale` (the window's pixel density, which `set_window` changes). The centre of a match goes straight into `imgui_input` and `click_at`. `space: "imgui"` takes ImGui's own units instead; a position that is not finite or beyond +-1e6 is `invalid_params`. `imgui_find` says the scale it used in `display` (`width`, `height`, `scaleX`, `scaleY`).

## `imgui_input {events, space?}`

At most `kMaxImGuiInputEvents` (256) events, validated whole before the first is queued: a bad one leaves ImGui as it was.

| Event | Fields |
|-------|--------|
| `move` | `x`, `y` |
| `click` | `x?`, `y?` (a move first), `button?` (`left`, `right`, `middle`), `count?` (1 to 3 presses), `mods?` |
| `down`, `up` | `button?` |
| `wheel` | `dx?`, `dy?` (positive scrolls up, as SDL) |
| `key` | `key` (ImGui's key name, case-insensitive: `Enter`, `Escape`, `A`, `F6`, `LeftArrow`; an unknown one is an error, never a default), `action?` (`press` default, `down`, `up`), `mods?` |
| `text` | `text` (UTF-8, at most `kMaxImGuiInputTextBytes`, valid) |

`mods` are `ctrl`, `shift`, `alt`, `super`, held around the event. `ctrl` is `ImGuiMod_Ctrl`: Cmd on macOS, which is what the editor's chords call Ctrl (`Ctrl+Shift+G` of `view.game`).

Answer `{events, frames, wantCaptureMouse, wantCaptureKeyboard, wantTextInput}`: the flags are those of the frame after the last event, so a client sees whether a text field has the focus (`wantTextInput`) and whether ImGui took the pointer.

**Why it answers late.** ImGui's input queue trickles (`ConfigInputTrickleEventQueue`): one change of a mouse button, one key change or one wheel step per frame, and a move that comes with a button change is applied a frame before it. A click is three frames (move, down, up), a double click five. The method runs frames until the queue is empty and one more, so the next call already sees what the input did; a client that answered at once would read the frame before the click.

## `imgui_find {label?, labelContains?, id?, window?, kind?, visibleOnly?, limit?}`

Arms `ImGuiItemIndex`, waits one frame, answers from that frame's list and disarms: a steady frame costs nothing. Answer `{frame, display, wantCapture*, wantTextInput, hoveredWindow?, focusedWindow?, matches, truncated?}`; a match is `{kind: "window"|"item", id, label, window?, x, y, width, height, visible, hovered}`, windows before items, each in the order ImGui drew them, at most `limit` (default 50, at most 500; `truncated` when more matched). No filter lists the windows only. A name that is not there is `matches: []`, not an error.

- `label` is the whole label or its part before `##` (`inc##perform`, the label of a table button of an asset panel, is found as `inc`); `labelContains` any part (so an icon button whose visible part is a glyph is found by its `##id`); `id` is ImGui's hex text, `0x1A2B3C4D`; `window` is a window name or its part before `##`. A child window is a window of its own with a generated name (`Game###GamePanel/table_<hash>`, the scroll region of a table with `scrollRows`): the action buttons of the Game panel belong to that child, so name it or leave `window` out.
- **An item exists only in a frame that drew it.** A closed menu or a collapsed tree has no items: click the menu, then find its item (the menu closes again after a click on an item). A window that is hidden has none. An item scrolled out of its window is listed with `visible: false` (and `visibleOnly` leaves it out), and a clipped widget may carry no label at all: ImGui reports one only when the widget got that far.
- `hovered` says the pointer was inside the item's rect when it was drawn (ImGui's hovered-rect flag: an item under another window is still `hovered`), `hoveredWindow` / `focusedWindow` are ImGui's.

## ImGui's clock

On the manual clock `io.DeltaTime` is the frame's `dt` (`ImGuiFrameSeconds`, at least `kMinImGuiFrameSeconds` because ImGui asserts on 0, which `screenshot` and these methods use), not the wall time the SDL backend measures. A double click, a tooltip delay, the caret blink and window fades are then the same on every run, and `step` means the same time to ImGui and to the game. A double click is `count: 2` in one event: ImGui reads it from the clock between the presses. With the real clock nothing changes.

## What it cannot reach

- **A real window's physical mouse competes.** The SDL backend writes the mouse position each frame while its window has the focus, over what `imgui_input` queued; a headless window has no focus and no mouse, which is the supported case.
- **Platform windows.** A panel dragged out of the main window on a real desktop is an OS window; the headless run keeps everything in the one window, which is all the API reaches. Drag and drop and gamepads are not events here.

## Recording and the editor

`GameSession` and the MCP tools (`imgui_find`, `imgui_click`, `imgui_type`, `imgui_key`; `tools/control/CLAUDE.md`) record the calls as any other. A scenario that holds them has `--editor` in its header (`launch(editor=True)`) and replays in the editor only: in Game mode the first line answers `no_imgui`. The smoke of the editor (`tools/stress/tests/stress_smoke.py`, scenario `editor`) is the worked example: View menu, `Game Panel`, a chord, the filter field, an action button.
