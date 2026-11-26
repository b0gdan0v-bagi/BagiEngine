# tools/export

Turns a game into a folder that runs without the sources or the build: a desktop package or a web page. Python, standard library only. `export.py` is the command, `package_desktop.py` and `package_web.py` the packaging, `scenes.py` what it leaves out.

## Command

```bash
export.py build (--exe <path> | --project <dir> --target <name>) --config debug|release [--platform desktop|web]
                [--out <dir>] [--preset <p>] [--exclude-scene <name>]... [--cancel-on-stdin-close] [--dry-run]
export.py run --dir <package>
export.py serve --dir <web package> [--port <p>] [--open] [--cancel-on-stdin-close]
```

- `--exe` takes the executable of a build (what a running editor knows of itself): the first `CMakeCache.txt` above it names the project, its file name the target. A packaged executable has none and cannot export.
- **Build folder** `<project>/build/export/<preset>`, tests off, never the preset's own `build/<preset>`: building there would overwrite the editor that runs the export. So the first export is a full build. The desktop preset is the host's Ninja preset of `--config` (macOS, or Windows through `CI/scripts/vs_env.py`; another host needs `--preset`), the web one `web-<config>` on every host ("Web target").
- **Output** `--out`, by default `<project>/dist/<target>-<config>`, on the web `<project>/dist/<target>-web-<config>`.
- `run` (desktop packages only) starts the executable the marker names with `--game`, the package as its working folder, detached and with no standard streams.

## Serving a web package

`serve` listens on `127.0.0.1` (`--port`, default 0: a free one) through `make_server` of `tools/web/serve.py`, with the isolation headers `manifest.json` asks for and nothing else of that server: the `BASIC_AUTH_*`, sign-in and match store of the environment are not read. It prints `== serve: http://127.0.0.1:<port>/` once it listens and with `--open` then opens that address in the system browser (a browser that does not open is a line, not a failure). It lives until cancelled ("Cancel") and so ends with `== export cancelled`; there is no `== export ok:`. A folder without the marker, or a desktop package, is refused, as `run` refuses a web one. On Windows a taken `--port` may not be refused (`SO_REUSEADDR`).

## Output

The editor parses it, so a change here changes `src/Widgets/Export/` too. Stdout, UTF-8, flushed before each child (which writes to the same place):

- `== step <k>/<n>: <name>` per step (`configure`, `build`, `package`), and `$ <command>` before each command; `== serve: <url>` of `serve`;
- the last line is one of `== export ok: <absolute folder>`, `== export failed: <reason on one line>`, `== export cancelled`.

Exit codes: 0 ok, 1 failed, 2 a wrong command line (no result line), 3 cancelled.

## Cancel

- With `--cancel-on-stdin-close`, a line `cancel` on stdin or its end gives the export up, so a caller that dies does not leave a build running. Without the flag stdin is not read: a run by hand with no input goes on. SIGTERM and Ctrl+C always cancel.
- Children take no input. On POSIX each runs in a process group of its own, and a cancel signals the group (ninja then ends its compilers, which have groups of their own), waits `KILL_GRACE_SECONDS`, then kills the group and every descendant listed before the signal. On Windows it is `taskkill /T /F`. A caller that kills the script on a timeout must give it longer than that grace.
- `<out>.partial` is removed and the old package stays.

## Web target

- **Steps:** `configure` clears `ENGINE_WEB_COOKED_DIR`, reads the manifest and checks the output folder before the long build; `package` runs `tools/web/package.py` as a child (so a cancel stops it) with `--game`, `--single-thread` for a preset whose name holds `-single-thread`, and `--app <project>/web/app.json` when that file exists. The package is what it writes plus the marker, whose first line is `index.html`.
- It needs the Emscripten SDK of the engine checkout (`CI/scripts/setup_emsdk.py`); without it the export fails before the configure.
- **Scenes left out:** the `config/` of the engine and the game are copied to `<build folder>/export-content/layer<k>/config` (numbered as a cook's output, times kept), the scenes are cut there, and a second configure points `ENGINE_WEB_COOKED_DIR` at it. That variable already replaces those two folders in the page's content, so CMake needs no option of its own; the first configure of every export empties it, or an earlier export's content would stay in the cache.
- The page ships XML in both configurations: a cook needs a desktop build with the same types.
- It does not go through `tools/web/build_and_deploy.py`: that script takes no build folder, so the content override would stay in the cache of the developer's `build/web-*`.

## Package layout (desktop)

| In the package | What |
|----------------|------|
| `<exe>` | the executable, with its runtime libraries and `THIRD_PARTY_NOTICES.txt` beside it |
| `content/engine/`, `content/game/` | `config/` and `assets/` of the two content roots, also when one folder is both |
| `bagi-package.txt` | the marker; its first line is the executable's file name |

The executable reads `content/` only because of the marker (`src/Modules/BECore/FileSystem/CLAUDE.md`, "Content roots and layers"), so the names are written twice: `package_desktop.py` and `PackagedContent.h`.

Left out of the content: dot files and folders, `imgui.ini` (the editor's window layout) and `*.artgen.json` (the art pipeline's records). A **release** package ships cooked config (`src/Modules/BECore/Cook/CLAUDE.md`): the packaged executable cooks its own packaged content with `--cook-verify`, so what is checked is what will be read, and each packaged `config/` is replaced by its `layer<k>/config`, `k` counting only the roots that have a `config/`, lowest first. The cook runs in the temp folder, because the executable leaves a log file where it runs. A debug package ships the XML.

Only the libraries the manifest lists travel: a game that links shared libraries of its own has to add them.

## Scenes left out

`--exclude-scene` (once per scene) takes the names of the child elements of `<scenes>` in the `SceneConfig.xml` of the highest content root that has one, which is the file the game reads (`scenes.py`). Only those elements are cut from the **packaged** copy, before the cook; every asset still ships, because a name built in code cannot be seen. The sources are never touched.

Refused, with the old package kept: an unknown name, the first scene (the game opens on it), a top file that is only a `.bin` (nothing to edit), no such file, and one that does not parse. A scene the game's code or content switches to by name is not checked.

`scenes.py` reads the file with patterns, not `xml.etree`: a Homebrew Python can lack a working `pyexpat`, and expat refuses a `--` inside a comment, which the engine accepts.

## Manifest

`<build folder>/export/<Target>.json`, written at generate time for every game executable of a Ninja build (`cmake/BagiExport.cmake`). A web one has `target`, `engineRoot` and `gameRoot`. A desktop one has also `executable`, `notices` and `libraries`, a list of `{source, name}`: `name` is the file name the executable asks the system's loader for, which is not always the name of the source file. Every file it names must exist, else the target is not built.

## Safety of the output folder

The export replaces the output folder as a whole, so it refuses, before it writes anything:

- a folder that exists, is not empty and has no marker (the owner's files);
- one that is or contains the project or a content root (a package there would replace the sources; on the web the engine's `web/` overlay is a root too);
- one inside a content root's `config/` or `assets/` (the game would read the package as content) or inside the export's build folder.

The checks compare the folders themselves, not their spelling, so a link or another letter case is refused too. The package is made in `<out>.partial` and takes the place of the old one (moved to `<out>.previous` for that moment) only after every step succeeded: a failed or cancelled export leaves the old package as it was. Both siblings obey the first rule. The marker is written first into `<out>.partial`, which is what lets a later export remove the left-over of a killed one.

## Tests

`.venv/bin/python -m pytest tools/export/tests`: a fake built game in a temp folder and a fake runner for the commands; no build is made. The cancel cases start real processes, the serve cases a real server on a free port.
