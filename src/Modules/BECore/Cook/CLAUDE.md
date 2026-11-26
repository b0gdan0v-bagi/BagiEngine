# Cook: XML content to binary content

A release build reads `config/*.bin` instead of parsing XML (`Config/CLAUDE.md`, "Binary configs"; the format: `.claude/rules/reflection-module.md`, "Binary Format", content mode). The **cook** turns the XML a developer edits into those files. XML stays the source; nobody edits a `.bin`, and no content file of a game changes.

## Why it runs in the game's executable

Every `.bin` starts with a content stamp: the root type and its **layout hash** (`ComputeLayoutHash`), which covers the types linked into the program that reads it, the derived types of factory bases included. Only the game's own executable has the game's types, so a separate cook program would stamp every file with another hash and the loader would refuse all of them. The cook is therefore a mode of every game executable (`src/main.cpp`, before `Application::Initialize`):

- `<exe> --cook=<dir> [--cook-verify]`, and the custom target `<Name>Cook` (not in `ALL`; output `build/<preset>/cooked/<Name>`, always with `--cook-verify`).
- `ParseCookArguments` (`CookCommand.h`): a bad flag, an empty `<dir>`, a repeated `--cook` or `--cook-verify` without `--cook` is a message on stderr and exit code 2. `RunCookCommand` returns 0, or 1 when any row failed.
- **Lifecycle:** `main` calls `Application::RunCook`, which calls `CoreManager::OnCookInit` (the file system with its mounts and the task manager that `ConfigManager` loads on, nothing else: no window, GPU, logger, audio or widgets) and `OnCookDeinit` after. `InitializeCoreServices` is not used: it opens `engine.log` and starts runtime work a cook must not. The report goes to stdout (no log sink exists at that point).

## Output and what is cooked

`CookContent` (`ContentCook.h`) writes **one output folder per source layer**: `<dir>/layer<k>/config/<relative path>`, `k = 0` the lowest layer. The layers, the one-stem-per-layer rule and the web overlay keep the meaning they have for the XML; a merged tree would need its own "which layer wins". `<dir>/layer*` is removed first; an output folder inside a content root, and a `.xml` and a `.bin` of one stem in one layer, are `Failed`. The walk is sorted and dot files and the editor's `imgui.ini` (its window layout, kept in `config/`) are skipped, so the same inputs give the same bytes (`ListContentFiles` is shared with the verify).

What a file becomes is decided by its root element, then by the registry:

1. Root `SceneManager`: node references expanded as `LoadSceneManager` expands them (the node loader sees every layer), then stamped `SceneManager`. A node file, root `node`: stamped `SceneNode` (a node of a subclass fails: the binary root carries no type); it keeps its relative path (`nodes/X.bin`) because `<node ref="nodes/X"/>` finds it there.
2. A stem with a cooker in `ContentCookRegistry`: read under `DiagnosticsMode::Error`, written with the stamp of its root type.
3. Anything else is **copied** byte for byte and listed `Copied`.

**The failure rule: any finding fails the file** (an unknown key, a malformed number under strict values, a content version that is not `kContentVersion`, an expansion error, a parse error), and no `.bin` is left for it. This is where `--strict-config` findings are caught for a release.

The engine's copied files, each with its reason (checked by `ContentCookEngineConfigTest`, which fails for a shipped file that is neither cooked nor listed): `TextConfig`, `UIConfig` (read by hand, entry `config-xmlnode-by-hand`), `SDLWindowConfig` (the path in `ApplicationConfig` names its `.xml`), `WidgetsConfig` (the editor widgets' `Initialize` starts runtime state, ImGui among it, so a cook would abort; Game mode never reads it, and a build without the editor cannot read its types). Making those widgets safe to read without ImGui would let it be cooked: an entry of its own. `GameWidgetsConfig` is cooked, but only the application executable has its widget types, so `--cook-verify` checks it.

## Registering a config

A cooker is `{stem, rootType, cook, describe}`. A reflected type read as `MakeFromConfig<T>` reads it needs one line at namespace scope in a `.cpp` of its module: `CookableConfig<T> registrar{"Stem"};` (a plain struct is accepted too). `CookableConfigWith` takes the functions of a hand-written reader (`TextureLibrary`, root type `TextureLibraryFile`). The registry is a function-local static, so a namespace-scope registrar is safe; a second registration of a stem is an `ASSERT`. The engine's registrars are `EngineCookables.cpp` and `Games/GamesConfig.cpp`; a game registers its own beside its type. **A registration claims that the file reads the same from XML and from `.bin`**: register only when the verify below passes; a type whose `Serialize` is not the inverse of its read stays copied.

## VerifyCooked

`VerifyCooked` (`ContentVerify.h`) reads each layer's cooked tree and its source through two `ConfigManager`s and compares the text each cooker's `describe` gives (scenes through `LoadSceneManagerFromConfig`, node files through the node loader). A difference is a `Failed` row with the first differing line; a `.bin` the cook did not write, or one `ConfigManager` refuses, is `Failed`. It is the guard that the path a release runs is the path developers run. Each layer is checked on its own, so a file an upper layer shadows is verified too.

A game runs it in `ctest` with `be_add_game_cook_test(<ExeTarget>)` in its CMakeLists (entry `<ExeTarget>CookVerify`, label `smoke`, no window or port); no test code of its own.

## Web package

The page reads the cooked files too: `cmake -DENGINE_WEB_COOKED_DIR=<dir>` (a folder `<Name>Cook` wrote) preloads `<dir>/layer0/config` and `layer1/config` in place of the ENGINE and GAME `config/`; `assets/` and the WEB layer stay as they are. A missing folder or layer stops the configure and names `<Name>Cook`. Cook a desktop build with the **same type set as the web build (no editor)**: the layout hash does not depend on the compiler or platform, but it does on the types.

## How a game follows

After `Update engine`: build the release preset, run `<Name>Cook` (or `<exe> --cook=<dir> --cook-verify`) and ship `<dir>/layer*/config` in place of the `config/` folders. A game's content files do not change. A change to any cooked type moves its hash: cook again (a stale `.bin` is refused with a message that says so).
