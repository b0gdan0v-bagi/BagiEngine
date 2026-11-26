# Editor layout

The layout widget and the editor's commands, hotkeys, settings and bars: `.claude/rules/widgets-module.md`, "Editor commands, hotkeys and settings" and "Bars". The processor and GPU readings of the status bar: `Hardware/CLAUDE.md`.

## The editor in a browser

A web build with `BE_WITH_EDITOR` (preset `web-editor`) runs the same editor. `EditorHost.h` holds what differs: `GetEditorHost()` is `Browser` under Emscripten, and a desktop test sets it (`ScopedEditorHost`) **before** it makes panels, which ask once.

- **What is hidden** is one table in `EditorHost.cpp`: commands that need a process, a file watch or a file the user takes away (Generate, File changes, the Profiler's Chrome trace). `IsEditorCommandAvailable` is asked by the interpreter (a button, a menu item and list menu items of such a command are not drawn), `AssetPanelWidget` (an asset whose `command` is unavailable stays closed and makes no model), the palette's filter, the hotkey rows of Preferences and `ProfilerPanelModel` (`traceFileSupported`, which shows the trace section). The command table stays whole, because shipped panel assets name those ids and the validator has to know them. A new command of that kind is a row of the host's table; a panel that cannot work in a browser needs its asset `command` there.
- **The notice.** Once per page load (`GetReloadNotice`, so a mode switch does not repeat it) `EditorLayoutWidget` opens a modal that says a reload brings back the shipped content, and logs it with `LOG_INFO`: the page's stderr stays empty.
- **Nothing is kept.** The editor's own files (preferences, hotkeys, layouts, `imgui.ini`) live in the page's in-memory file system, like every content file a Save writes.

### Input

`EditorBrowserInput` is a member of the layout widget in the browser host, so it comes and goes with the editor and Game mode keeps the game's input as it was.

- **Shortcuts.** SDL's key handler lets a Cmd chord through to the browser while a text field has the keyboard (Cmd+S would open the browser's save dialog), so a capture-phase `keydown` listener calls `preventDefault` for Ctrl or Cmd with the keys of `EditorBrowserKeys` (the default hotkeys, not a rebound one). C, V and X are never taken: the clipboard's own handler needs the browser's gesture.
- **Mac.** ImGui is compiled without `__APPLE__`, so on a Mac page `ConfigMacOSXBehaviors` is set and Cmd works as the editor's `Ctrl`. A browser there tells no release of a key let go while Cmd is held, so the release of Cmd releases every other key in ImGui (the next press of the same chord would be lost). Ctrl+Shift+E and Ctrl+Shift+P are read from SDL and stay the physical Ctrl, as on a desktop Mac.
- **Clipboard.** ImGui's callbacks go to `CoreManager::GetClipboard()` through `BrowserClipboardBridge`. A browser read answers later, so the paste asks first and the bridge presses the chord again when the text is there; a refusal changes nothing in the field.
- **Limits.** Ctrl+W, T, N and Q (with or without Shift) belong to the browser and are refused as hotkeys (`IsBrowserKeptChord`); so do some Cmd chords of a Mac, which no page can take. A phone is not a target.
