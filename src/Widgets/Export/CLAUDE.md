# Export

The editor's side of the export, desktop and web. The work (build, package, cancel of the build, serving a web package) is `tools/export/export.py`; the editor starts it, reads its output and shows it. Read `tools/export/CLAUDE.md` first: the command line, the output lines and the exit codes are written twice, there and here. Editor only (`.claude/rules/widgets-module.md`).

| File | What |
|------|------|
| `ExportLaunch.{h,cpp}` | the argument lists, the path of the running program, `IExportProcess` / `IExportLauncher` and their SDL implementation |
| `ExportLog.{h,cpp}` | the output as classified lines, read from the log file; no ImGui |
| `ExportScenes.{h,cpp}` | the scene names of the game's content; no ImGui |
| `ExportPanelModel.{h,cpp}` | the model of the Export window and its reflected view; no ImGui |
| `ExportSlots.{h,cpp}` | the slot `exportOutput`, the output list: the one part that draws with ImGui |

The script is found among the content roots and run by the Python of `Widgets/EditorLayout/EngineTools.h`, which the Generate panel's bridge shares. What it gets as `--exe` is `argv[0]` (`CommandLine::GetProgramPath`) made absolute: an editor started by a bare name from `PATH`, or a packaged one, cannot export.

## Process

- **Output goes to a file, not a pipe**: nobody need read while the panel is closed, and a full pipe would stop the build. Both streams share the handle, so their lines stay in order.
- **Cancel** writes `cancel` to the script's input and closes it; the script then stops the whole build. The script is killed only after `kExportKillGraceMs`, longer than the grace it gives the build, and only from `Poll`. Destroying a running process cancels it and does not wait, so a mode switch never blocks.
- Not an SDL background process: that one has no exit code.

## Log

- `ExportLog` follows the file from its start and opens it at the first read after the process was started. Lines are classified by the marks CMake, the compilers, ninja and the script write; the counts include the lines no longer kept.
- A read that finds nothing allocates nothing; the revision moves only with the lines.
- **Windows:** SDL opens a file for writing with no sharing and for reading only without a writer (`BECore/File/CLAUDE.md`, "Traps"), so the log is expected to open only after the export ended: the lines arrive at the end. Not run there.

## Model

- One export at a time: `export.start` does nothing while one runs. The exit code decides the state; a cancelled script that was killed has none of its own and counts as cancelled.
- **Lifetime.** The model owns the process. Closing the window keeps the export running: the progress source of the status bar (`PollProgress`; `src/Widgets/Feedback/CLAUDE.md`), else the next `Update`, takes its end and the rest of the log; destroying the model, which a switch to Game mode does, cancels it.
- **The log** is `export.log` in the editor's user folder, removed and made again by each start, so a cancelled export that is still ending writes on into its own file. Two editors share the name.
- `export.run` of a desktop result starts the package through the script and does not follow it: only a script that cannot be started is reported. What counts is the platform the export was started with, not the choice now.
- An unsaved editor document shows a line and does not block the export, which reads the saved files. Documents are asked and the scenes listed when the window opens, when the pointer comes back to it and at a start, not each frame (both allocate).

## The server of a web result

- `export.run` of a web result starts `serve` through the launcher's followed `Start`, with a log of its own (`export-serve.log` beside the export's); one at a time. The script opens the browser, so it opens on Windows too, where the log is read late.
- It lives until `export.stop`, a new export (which replaces the folder it serves) or the end of the model. Closing the window leaves it. A stop cancels and keeps the process, polled until it ends: the kill after the grace works only through `Poll`.
- A server that ends by itself is a problem line with the script's reason.

## Scenes and the check

- **Scenes** are listed from the file the script cuts (`tools/export/CLAUDE.md`, "Scenes left out"), so the same rule is written twice. The first one and every scene a registered game's `GetSceneName()` names are locked: the game opens on them. A scene the game switches to by another name is not known.
- The choice is kept by name and a new scene is exported. A list that cannot be read exports every scene and says so; the choice waits for the next list.
- **The check** is `CheckContentAssets` (`src/Widgets/AssetBrowser/CLAUDE.md`, "The content check"), on `export.check` and at each start. It reads every source on the main thread and never stops an export: its lists can miss a name built in code. Scenes left out are checked too, since their assets ship all the same.

## Window

`config/panels/ExportPanel.xml` over `ExportPanelModel`: the floating window `Export###Export`, hidden at start (View > Export, `view.export`): the target and configuration, the sections Scenes and Assets (the check), the actions, the status and the output. The status is one text per `statusStyle`, because a text's style is a literal of the asset. The output list takes the height the elements above it leave, follows the end unless the user scrolled up, and a right click on a line copies it; the server's output is not listed. Its slot is what tells the model that the pointer is over the panel. Not covered by a test or a shot scene: the colours and the layout, the following of the end, a long real build, Open folder, Run on a real package and a real server.

## Tests

`ExportLaunchTest` (arguments, the program's path, a stand-in Python script for the streams, the cancel, the destroy and the kill), `ExportLogTest` (classification, partial lines, the bound, the result lines), `ExportScenesTest` (which file gives the scenes), `ExportPanelModelTest` (the states, the scene choice and the check over the fakes of `tests/BECore/ExportFakes.h`), `ExportAssetTest` (the shipped asset over those fakes). No test starts the export.
