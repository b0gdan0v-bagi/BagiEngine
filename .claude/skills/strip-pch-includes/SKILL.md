---
name: strip-pch-includes
description: Remove redundant #include lines from C/C++ sources that are already in the engine's PCH. Use when cleaning includes, stripping PCH duplicates, or optimizing includes.
---

# Strip PCH Includes

Removes `#include` lines from C/C++ sources of `<R>` that are already present in the precompiled header of the engine, `<engine>/src/Modules/BECore/pch.h` (the PCH is always the engine's, also for a game). Reduces redundant includes and keeps sources tidy. You can limit it to a subfolder and run in parts.

## Rules file first

Read `.claude/rules/build-project.md` of the repository you run in before anything else. This skill is the same text in the engine repository and in a game that carries the engine as the submodule `engine/`; the rules file says what differs (section "Strip PCH includes"). Terms (defined there): `<engine>` (`.` or `engine`: the folder with `CI/`), `<R>` (the repository to process: `.` by default, `engine` only for engine work done from a game checkout, then run everything from `engine/` with `<engine>` = `.`), `<py>` (the Python interpreter, field `Python`), `<tests>` (the test targets).

## When to Use

- User asks to remove redundant includes, strip PCH duplicates, or clean up includes.
- After adding or changing many C++ files; run (optionally with dry-run) to drop includes already in the PCH.

## How to Run

From the root of the repository you work in:

```bash
# Dry run on all sources (report only)
<py> <engine>/CI/scripts/strip_pch_includes.py --dry-run --all --root <R>

# Only changed files
<py> <engine>/CI/scripts/strip_pch_includes.py --changed --root <R>

# One folder (run in parts); the folder is relative to <R>
<py> <engine>/CI/scripts/strip_pch_includes.py --dry-run --all --root <R> --dir <dir>
<py> <engine>/CI/scripts/strip_pch_includes.py --all --root <R> --dir <dir>
```

`<dir>` is a folder of `<R>`; the field `Strip example dir` of the rules file gives one. The script uses only the Python standard library: no venv is needed (`uv run python ...` works too). **Without `--root` the script works on the engine: never leave it out in a game.**

## Requirements

- The script is `<engine>/CI/scripts/strip_pch_includes.py`. The PCH defaults to `src/Modules/BECore/pch.h` of the engine (the optional positional argument `pch`, relative to the engine).
- It finds sources like `format_sources.py` (the `clang-format` skill): the same scope of `--all` and the same `.clang-format-ignore` of `<R>`, with one difference: the engine's `BECore` module is not excluded, so `--dir src/Modules/BECore` works in the engine. The PCH file itself is never edited.

## Scope

| Option  | Meaning |
|---------|---------|
| `--all` | All matching sources of `<R>` under the folders of the field `Format scope` of the rules file. |
| `--changed` | Only git-modified tracked files. |
| `--dir <dir>` | Only files under this path (relative to `<R>`). |
| `--dry-run` | Only reports what would be removed; writes no files. |

## Recommended Execution Order

1. Dry-run first (`--dry-run --changed` or `--dry-run --all --dir <subfolder>`).
2. Apply to a bounded scope (single module or folder).
3. Validate the build for that scope (`build` skill), and run `<tests>` when the folder has tests. The rules file names what to build and run.
4. Expand to a wider scope only after successful verification.
