# tools/dev

`dev.py` builds and runs the products of a repository on BagiEngine (a game that carries the engine as `engine/`, or the engine itself): one command line for the VS Code panel `tools/vscode/bagi-dev`, the terminal and the agents. Standard library only; tests `tools/dev/tests` (pytest, no CMake needed).

```bash
python3 engine/tools/dev/dev.py info [--json]                       # presets for this host, dev presets, flags, products, chains, tools
python3 engine/tools/dev/dev.py flags macos-debug --set ENGINE_PROFILER=ON --set ENGINE_PROFILER_ALLOC=ON
python3 engine/tools/dev/dev.py build tests [--preset P] [--configure]
python3 engine/tools/dev/dev.py run game [--preset P] [--no-build] [-- args]
python3 engine/tools/dev/dev.py chain online-local [--preset P] [--web-preset W]
python3 engine/tools/dev/dev.py tool ui-shots [--preset P] [-- args]
```

## The contract

- **The repository** is the nearest folder upwards with a `CMakePresets.json` (or `--root`). Its presets are read from the JSON with the `include`s, not from `cmake --list-presets`, so `info` needs no CMake; a preset whose `${hostSystemName}` condition fails here is left out (`linux-server-release` on a Mac).
- **`dev.json`** at the root describes what the engine cannot know: `products` (`name`, `label`, `kind` `desktop` | `web`, `prefer` `debug` | `release`, `target` or `targets`, `after` steps, `run`, `env`, `url`), `chains` (`steps` of product names or `{product, args, env}`, `env`, `delay`), `tools` (`command`, optional `build`: a product built first, whose preset fills `{bin}`) and optionally `flags` (`name`, `label`, `default`; without it the engine's list `DEFAULT_FLAGS`). In commands `{bin}` is `build/<preset>/bin`, `{preset}`, `{root}`, `{python}` (the interpreter running dev.py) and `{exe}` (`.exe` on Windows).
- **Flags are presets, not settings of a tool:** `flags <preset>` writes `<preset>-dev` (configure: inherits `<preset>`, `cacheVariables`; build: inherits the base build preset when there is one; test: only when the base has a test preset) into the git-ignored `CMakeUserPresets.json`, keeping every other preset of that file. A `-dev` preset builds into `build/<preset>-dev/`, so a flag never forces a rebuild of the plain folder. No flag left: the three presets go (and the file, when it held nothing else).
- **Presets by default:** `<host>-<prefer>` (`macos-debug`), `web-<prefer>` for a web product; a preset of the other kind is refused.
- **Every step** runs with `engine/.venv/bin` (or `.venv/bin`) first on `PATH`: `cmake --preset` runs the reflection generator, which needs its `jinja2`. Configure runs only when the build folder has no `CMakeCache.txt` or with `--configure`; the `build` skill's triggers (new sources, reflected classes) are not checked here, so after such a change pass `--configure`.
- `run` replaces itself with the product (exit codes and Ctrl+C reach it); `chain` starts its steps `delay` seconds apart (a server before its clients) and stops all of them on Ctrl+C or when one ends.
