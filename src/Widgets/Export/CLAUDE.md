# Export

The editor's side of the desktop export. The work (build, package, cancel of the build) is `tools/export/export.py`; the editor starts it, reads its output and shows it. Read `tools/export/CLAUDE.md` first: the command line, the output lines and the exit codes are written twice, there and here. Editor only (`.claude/rules/widgets-module.md`).

| File | What |
|------|------|
| `ExportLaunch.{h,cpp}` | the argument lists, the path of the running program, `IExportProcess` / `IExportLauncher` and their SDL implementation |
| `ExportLog.{h,cpp}` | the output as classified lines, read from the log file; no ImGui |
| `ExportPanelModel.{h,cpp}` | the model of the Export window and its reflected view; no ImGui |

The script is found among the content roots and run by the Python of `Widgets/EditorLayout/EngineTools.h`, which the Generate panel's bridge shares. What it gets as `--exe` is `argv[0]` (`CommandLine::GetProgramPath`) made absolute: an editor started by a bare name from `PATH`, or a packaged one, cannot export.

## Process

- **Output goes to a file, not a pipe**: nobody reads while the panel is closed, and a full pipe would stop the build. Both streams share the handle, so their lines stay in order.
- **Cancel** writes `cancel` to the script's input and closes it; the script then stops the whole build. The script is killed only after `kExportKillGraceMs`, longer than the grace it gives the build, and only from `Poll`. Destroying a running process cancels it and does not wait, so a mode switch never blocks.
- Not an SDL background process: that one has no exit code.

## Log

- `ExportLog` follows the file from its start and opens it at the first read after the process was started. Lines are classified by the marks CMake, the compilers, ninja and the script write; the counts include the lines no longer kept.
- A read that finds nothing allocates nothing; the revision moves only with the lines.
- **Windows:** SDL opens a file for writing with no sharing and for reading only without a writer (`BECore/File/CLAUDE.md`, "Traps"), so the log is expected to open only after the export ended: the lines arrive at the end. Not run there.

## Model

- One export at a time: `export.start` does nothing while one runs. The exit code decides the state; a cancelled script that was killed has none of its own and counts as cancelled.
- **Lifetime.** The model owns the process. Closing the window keeps the export running (the next `Update` takes its end and the rest of the log); destroying the model, which a switch to Game mode does, cancels it.
- **The log** is `export.log` in the editor's user folder, removed and made again by each start, so a cancelled export that is still ending writes on into its own file. Two editors share the name.
- `export.run` starts the package through the script and does not follow it: only a script that cannot be started is reported.
- An unsaved editor document shows a line and does not block the export, which reads the saved files. Documents are asked when the window opens, when the pointer comes back to it and at a start, not each frame (asking allocates).

## Tests

`ExportLaunchTest` (arguments, the program's path, a stand-in Python script for the streams, the cancel, the destroy and the kill), `ExportLogTest` (classification, partial lines, the bound, the result lines), `ExportPanelModelTest` (the states over the fakes of `tests/BECore/ExportFakes.h`). No test starts the export.
