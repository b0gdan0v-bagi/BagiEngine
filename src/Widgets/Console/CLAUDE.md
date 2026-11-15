# Console panel

The editor's log viewer. Editor only: the folder is in the `BE_WITH_EDITOR` regex of `src/CMakeLists.txt` (`.claude/rules/widgets-module.md`), so a build without the editor has neither the panel nor `ConsoleLogViewTest`.

- **Where the lines live.** In the `LogHistorySink` of `LoggerManager` (`BECore/Logger/CLAUDE.md`, "The history"), not in the widget: widgets are destroyed and created at a mode switch, the history is not, so the panel shows the same lines after it. `ConsoleWidget::Initialize` finds the sink with `GetSink<LogHistorySink>()`. A game that ships its own `LoggerConfig.xml` has to add the sink entry; without it the panel says so and draws nothing else.
- **Model and glue.** `ConsoleLogView` (no ImGui, tested alone) turns a `LogHistory`, a `ConsoleLogFilter` and the pause flag into `GetRows()`, indices for `LogHistory::At`, and rebuilds them only when the history's `GetVersion()`, the filter's `revision` or the pause changed. Whoever changes a filter field bumps `revision`. `ConsoleWidget` draws the toolbar and the rows and owns the filter and the pause.
- **Pause** freezes the list at the sequence the history had when it began; the history goes on behind it, and resuming lists what came. A line the history drops while paused leaves the rows too (a row is an index).
- **Category** is the module folder of the line's `file` (`LogCategoryOfFile`), since a log call has none of its own; the combo offers the categories the history has seen.
- **Zero allocations in a steady frame:** rows and the title are made again only on a change (`FormatConsoleTitle` when the number of errors or warnings moved; `###Console` keeps the window id), labels are built in stack buffers, the message is drawn straight from the record.
- **Colours** come from `GetImGuiPalette()`; Fatal is drawn as an error. The right-click menu of a line has "Copy line" only.
- **Command:** `view.console` (`Ctrl+Shift+L`), also in the View menu; the panel is hidden at start. It is not read from the raw SDL event like the Profiler's chord, so it can be rebound.
- **Tests:** `tests/BECore/ConsoleLogViewTest.cpp` (filters, rebuilds, pause, a dropped oldest, the title). The ImGui drawing is not covered; look at it by hand in an editor build.
