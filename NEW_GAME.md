# A new game in a repository of its own

Adding a game is making a repository like **BagiEngine-Chess**: it lives in its own git repository and carries this engine as the git submodule `engine/`. The files to copy from Chess are named below. This guide is written for an agent that does the setup with the owner. What a game is (the `IGame`, the CMake functions, the scene, the config) is in [src/Modules/Games/CLAUDE.md](src/Modules/Games/CLAUDE.md), "Adding a game". This file covers the repository around it: git, the identity of the commits, the submodule, the shared skills and the todo store.

Names used below: `<Name>` is the game in CamelCase (`Chess`). The executable is `Bagi<Name>`, the module `<Name>Module` and the tests `<Name>Tests`. `<area>` is the id of the game in the todo store, in lower case (`chess`). `<repo>` is the repository name (`BagiEngine-<Name>`).

## 0. Ask the owner first

Do not guess any of these. Ask, write the answers down and repeat them back before the first command that writes anything:

| What | Why it is asked |
|------|-----------------|
| `<Name>`, `<area>`, `<repo>` | the names of everything below |
| the remote URL of the new repository | the owner creates the empty repository on GitHub; the agent creates no remote repository |
| **the git email and the name for the commits** | see step 2: the global git identity of the machine may be a different one (a work account), and these repositories must not get it |
| the todo token (`TODO_API_TOKEN`) | the `.env` of the new repository; never committed, never printed |
| whether to create the todo area and its Vikunja project now | it is a setting of the shared store (step 5) |

**The email:** never take it from `git config --global`, from the environment, or from another repository's config or history without asking. Ask: "Which email and name should the commits of `<repo>` and of its `engine/` submodule use?" Use exactly the answer.

## 1. The engine branch

Each game follows a permanent branch of the engine, `games/<area>` (Chess: `games/chess`), not `main`. It is the base branch of engine work done from the game: `todo-loop` cuts its `todo/<id>` branches from it and `todo-merge` merges them back. The owner merges `main` into it when the game should get newer engine work.

```bash
git -C <engine checkout> switch -c games/<area> main
```

The owner pushes it (the agent never pushes). Until the branch is on the remote, `git submodule add -b games/<area>` of a fresh clone cannot find it.

## 2. The repository and the identity of the commits

```bash
mkdir <repo> && cd <repo>
git init -b main
git config user.email "<the email the owner gave>"
git config user.name  "<the name the owner gave>"

git submodule add -b games/<area> https://github.com/b0gdan0v-bagi/BagiEngine.git engine
git submodule update --init --recursive

git -C engine config user.email "<the same email>"
git -C engine config user.name  "<the same name>"
```

**Set the identity twice: once in the game and once in the submodule.** `engine/` is a repository of its own with its own config (`.git/modules/engine/config`). Engine work is committed from inside the submodule (todo branches, docs), and without its own setting those commits take the global identity.

Check both before the first commit:

```bash
git config --show-origin user.email           # file:.git/config
git -C engine config --show-origin user.email # file:.../.git/modules/engine/config
```

Both must show a local file, not `~/.gitconfig`, and the address the owner gave.

The setting is not versioned. **Every new clone needs the four `git config` commands again** (after `git submodule update --init`), so say this in the "Get it" section of the game's `README.md`. Do not write the address itself into any committed file.

If a commit was already made with the wrong identity, stop and tell the owner. A fix (`git commit --amend --reset-author`, or a rebase of the commits not pushed yet) needs the owner's yes. A commit that is already pushed stays as it is unless the owner decides otherwise.

## 3. The files of the game

Copy them from BagiEngine-Chess and replace the names:

| File | What it holds |
|------|---------------|
| `CMakeLists.txt` | the standalone block (`if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)` → `project(Bagi<Name>)` + `include(engine/cmake/BagiEngine.cmake)`), then `be_add_module`, `be_add_game_reflection`, `be_add_game_executable`, `be_add_game_tests` ([src/Modules/Games/CLAUDE.md](src/Modules/Games/CLAUDE.md), [.claude/rules/cmake-rules.md](.claude/rules/cmake-rules.md)) |
| `CMakePresets.json` | `{"version": 6, "cmakeMinimumRequired": {"major": 3, "minor": 31, "patch": 0}, "include": ["engine/CMakePresets.json"]}`; it defines no preset of its own and names no `targets` |
| `.clang-format`, `.gitattributes` | copies of the engine's |
| `.clang-format-ignore` | `engine/`, `build/`, `.venv/` (the format scripts run with `--root .`) |
| `.gitignore` | `/build/`, `/out/`, `/.cmake/`, IDE folders, `.venv/`, `__pycache__/`, `.env`, `CMakeUserPresets.json`, `.claude/settings.local.json`, `config/imgui.ini`, `engine.log`, local config overrides (`config/*.local.xml`), `.DS_Store`, and the todo guard: `.claude/plans/`, `*.plan.md`, `todo-export/` |
| `.env.example` | the keys of the todo CLI (step 5), with an empty token |
| `CI/setup_venv.{sh,bat,ps1}` | fetch `engine/` if it is missing, run the engine's `CI/setup_venv.*` (the venv is `engine/.venv`), install the game's `requirements.txt` when there is one |
| `CI/Bagi<Name>.code-workspace` | the VS Code workspace (CMake Tools on the presets, launch configurations, build tasks) |
| `config/ApplicationConfig.xml` | the engine's file with `mode="Game"` |
| `config/SceneConfig.xml`, `config/TextureLibrary.xml` | every scene the game uses (it replaces the engine's file) |
| `src/<Name>/`, `tests/`, `assets/` | the game: `Game/` (plain C++ rules), `Components/` (scene glue), `<Name>Game.{h,cpp}` |
| `CLAUDE.md` | the game's guide for agents (the model is Chess's) |
| `README.md` | get it (clone with submodules, `CI/setup_venv.*`, **the identity commands of step 2**), build and test, the layout, how the engine is updated |
| `TODO.md` | a short note: the entries are in the todo store, not in git (step 5) |

## 4. `.claude/`: the shared skills and the game's rules

The skills and agents are **one copy, in the engine**. The game links them; it does not copy them:

```bash
mkdir -p .claude/skills .claude/agents .claude/rules
for s in build clang-format strip-pch-includes todo-loop todo-merge todo-add todo-db; do
    ln -s ../../engine/.claude/skills/$s .claude/skills/$s
done
for a in todo-architect todo-classifier todo-developer todo-reviewer; do
    ln -s ../../engine/.claude/agents/$a.md .claude/agents/$a.md
done
```

The links work only with the submodule initialised. Change a skill in the engine repository, never through the link in a game.

The rules are the game's own files, with the same headings as the engine's, because the shared skills read them:

| File | Template | What to fill in |
|------|----------|-----------------|
| `.claude/rules/build-project.md` | [.claude/rules/build-project.md](.claude/rules/build-project.md) of the engine (Chess has a filled one) | Product `Bagi<Name>`, Tests `<Name>Tests`, source paths `src tests`, the extra configure trigger "the pointer of `engine` moved", `--root .` everywhere, format scope `src/`, `config/`, `tests/` |
| `.claude/rules/todo-project.md` | Chess's | two areas: game (`<area>`, `<R>` = `.`, base `main`, Vikunja `<repo>`) and engine (`engine`, `<R>` = `engine`, base `games/<area>`, Vikunja `BagiEngine`); the zones nobody edits (the vendored code of `engine/`); the classifier's `needs engine change` |
| `.claude/rules/engine-and-todo.md` | Chess's | the names of the game and of the branch `games/<area>` |

The skill texts give `games/chess` as an example of the engine's base branch. For another game the base is the one in its `todo-project.md`.

`.claude/settings.local.json` (permissions) is per machine and git-ignored. A useful start: allow `cmake`, `ctest`, `git switch/add/commit` and deny `git push`.

## 5. The todo store

The entries, plans and reviews of every area live in the todo store, a service on Railway ([tools/todo/CLAUDE.md](tools/todo/CLAUDE.md)). Nothing of it is in git. The game gets an area of its own, and its Vikunja project is mirrored by the service.

1. `.env.example` (committed):
   ```
   TODO_API_URL=<the service URL, the same as in the engine's .env.example>
   TODO_API_TOKEN=
   ```
   `.env` (git-ignored): the same keys with the token the owner gave. Do not print the token or write it anywhere else.
2. `python3 engine/tools/todo/todo.py ping` must answer.
3. The area, **only after the owner's yes** (it changes the shared store, and the mirror creates or links the Vikunja project):
   ```bash
   python3 engine/tools/todo/todo.py area set <area> --title "<Name>" --vikunja-project <repo>
   python3 engine/tools/todo/todo.py area list
   ```
4. The first entries go in with `/todo-add` (or `todo.py add <id> --area <area> ...`), not as files. Plans and reviews are never committed (`todo.py plan get` / `put` refuse a path inside a git work tree).

## 6. Build and check

```bash
CI/setup_venv.sh                               # Windows: CI\setup_venv.bat or .ps1
export PATH="$PWD/engine/.venv/bin:$PATH"      # the reflection generator needs jinja2
cmake --preset macos-debug                     # cmake --list-presets=build
cmake --build --preset macos-debug
ctest --preset macos-debug
```

- The `build` skill finds `.claude/rules/build-project.md` and builds `Bagi<Name>` and `<Name>Tests`.
- A game with a control API adds `be_add_game_control_tests(Bagi<Name> ...)`. ctest skips it (77) when the executable is not built.
- `ctest --preset <p>` runs 8 tests at once (`execution.jobs` of `test-base` in the included presets; `-j1` switches it off), so the game's own tests must be safe side by side: temp names from `<Common/TempPath.h>` (`engine/tests/Common/TempPath.h`), port 0, polling instead of fixed sleeps, or `RUN_SERIAL` / `RESOURCE_LOCK` on the one that cannot (`.claude/rules/tests-module.md`).
- `python3 engine/tools/todo/todo.py list --area <area>` works (empty at first).

## 7. Commits, and what the agent does not do

- Commit in steps, like Chess: the submodule (`Add the engine as the submodule engine/`), the infrastructure (`.claude/`, `CI/`, rules, ignore files), then the game.
- Before you say the work is done, check `git log --format='%an <%ae>'` in the game **and** in `engine/` (for any engine commit made during the setup). Every line must show the identity the owner gave.
- The agent never pushes and never creates remote repositories. It does not move `main` or `games/*` of the engine and does not run `todo.py area set` without the owner.
- Later the pointer of `engine/` moves only as a commit of its own: by `todo-merge` after an engine branch is merged, or by the owner (`git submodule update --remote engine`).

## What stays in the engine

Nothing in the engine names a game: no code, CMake or config, and there is no `games/` folder ([CLAUDE.md](CLAUDE.md), "Engine and games"). The only engine change is the branch `games/<area>`. When the game needs something from the engine, that is an entry of the area `engine`, worked from the game's checkout in a `todo/<id>` branch cut from `games/<area>`.
