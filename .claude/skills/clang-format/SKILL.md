---
name: clang-format
description: Apply the project .clang-format to the C/C++ sources of this repository. Use when the user asks to format code, apply style, or run clang-format.
---

# Clang-Format

Applies the `.clang-format` in the root of `<R>` to its C/C++ sources. Only project code is formatted; the script has an ignore file, so vendored and third-party code is never touched.

## Rules file first

Read `.claude/rules/build-project.md` of the repository you run in before anything else. This skill is the same text in the engine repository and in a game that carries the engine as the submodule `engine/`; the rules file says what differs (section "Format"). Terms (defined there): `<engine>` (`.` in the engine repository, `engine` in a game: the folder with `CI/`), `<R>` (the repository to format: `.` by default, `engine` only for engine work done from a game checkout, then run everything from `engine/` with `<engine>` = `.`), `<py>` (the Python interpreter, field `Python`).

## When to Use

- User asks to format code, apply `.clang-format`, or fix formatting.
- User wants to ensure style consistency before commit or review.

## How to Run

From the root of the repository you work in:

```bash
# Only files with uncommitted changes (tracked files)
<py> <engine>/CI/scripts/format_sources.py --changed --root <R>

# All sources of the scope in the rules file (field "Format scope")
<py> <engine>/CI/scripts/format_sources.py --all --root <R>
```

The script uses only the Python standard library: no venv is needed to run it (`uv run python ...` works too). `--root <R>` makes it use the `.clang-format`, the `.clang-format-ignore` and the sources of `<R>`; **without `--root` the script works on the engine: never leave it out in a game** (in the engine repository `--root .` is the default and does no harm). Paths are relative to `<R>`.

## Requirements

- The script is `<engine>/CI/scripts/format_sources.py`. Config: `.clang-format` in the root of `<R>`. Ignore list: `.clang-format-ignore` in the root of `<R>` (required; the script stops when it is missing).
- `clang-format` is looked up in this order: `PATH`; `LIBCLANG_PATH` (the LLVM directory, its `bin/`, or `../bin` when it points at `lib/`, same as for Meta Generator); the `.venv` of `<R>` and of the engine (pip package from `requirements.txt`); on macOS Homebrew LLVM (`/opt/homebrew/opt/llvm`, keg-only, so it is not on `PATH`) and then Xcode (`xcrun --find clang-format`). The script prints which binary and version it uses.

## Scope

| Scope   | Files included |
|---------|----------------|
| Changed | Git-modified **tracked** files (`git diff --name-only HEAD`: staged + unstaged), extensions `.cpp`, `.c`, `.h`, `.hpp`, `.cxx`, `.hxx`, `.cc`. Untracked files are skipped. |
| All     | All such files under the folders of the field `Format scope` of the rules file. |

**Ignore:** lines of `.clang-format-ignore` of `<R>`: one path or prefix per line, `#` comments. It is required and is not merged with a default. What the file of this repository excludes is in the rules file. Add more paths there.

## Safe Workflow

- Prefer `--changed` for normal iteration; use `--all` only for intentional mass-format passes, in a commit of their own, and check `git status` afterwards: files you did not change should not move.
- A new file is untracked, so `--changed` skips it. Format it with the binary the script printed: `git ls-files --others --exclude-standard -- '<dir>/*.h' '<dir>/*.cpp' | xargs <clang-format> -i --style=file` (run it in `<R>`). If `clang-format` is not on `PATH`, give its full path (Homebrew LLVM on macOS) and set `PATH` only for that one command; **never put Homebrew LLVM on `PATH` for `cmake --preset`**, it switches the compiler and the build then fails.
- If you touch PCH-heavy or broadly included headers (for example `pch.h` or central reflection headers), format narrowly and review the diff before continuing.
- When a large formatting pass is required, do it in isolated commits/chunks to keep functional changes easy to review.
- Changed includes: run the `strip-pch-includes` skill afterwards.
