# Editor layout

The layout widget and the editor's commands, hotkeys, settings and bars: `.claude/rules/widgets-module.md`, "Editor commands, hotkeys and settings" and "Bars". The processor and GPU readings of the status bar: `Hardware/CLAUDE.md`.

## The editor in a browser

A web build with `BE_WITH_EDITOR` (preset `web-editor`) runs the same editor. `EditorHost.h` holds what differs: `GetEditorHost()` is `Browser` under Emscripten, and a desktop test sets it (`ScopedEditorHost`) **before** it makes panels, which ask once.

- **What is hidden** is one table in `EditorHost.cpp`: commands that need a process, a file watch or a file the user takes away (Generate, File changes, the Profiler's Chrome trace). `IsEditorCommandAvailable` is asked by the interpreter (a button, a menu item and list menu items of such a command are not drawn), `AssetPanelWidget` (an asset whose `command` is unavailable stays closed and makes no model), the palette's filter, the hotkey rows of Preferences and `ProfilerPanelModel` (`traceFileSupported`, which shows the trace section). The command table stays whole, because shipped panel assets name those ids and the validator has to know them. A new command of that kind is a row of the host's table; a panel that cannot work in a browser needs its asset `command` there.
- **The notice.** Once per page load (`GetReloadNotice`, so a mode switch does not repeat it) `EditorLayoutWidget` opens a modal that says a reload brings back the shipped content, and logs it with `LOG_INFO`: the page's stderr stays empty.
- **Nothing is kept.** The editor's own files (preferences, hotkeys, layouts, `imgui.ini`) live in the page's in-memory file system, like every content file a Save writes.
