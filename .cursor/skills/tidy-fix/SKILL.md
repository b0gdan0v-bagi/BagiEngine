---
name: tidy-fix
description: Runs clang-tidy fixes on project sources (readability-braces-around-statements). Use when the user asks to add braces around if/for/while, enforce "always { }", or run tidy-fix. Run manually once a week or before a PR — not a required step on every commit.
---

# Tidy Fix (BagiEngine)

Runs `clang-tidy readability-braces-around-statements --fix` on project sources to add `{ }` around `if`/`for`/`while`/`do` bodies. Always follow with **Clang-Format** to restore consistent style.

## When to Use

- User asks to add braces, enforce "always `{ }`", or run tidy-fix / clang-tidy fixes.
- Recommended frequency: weekly or before a PR.

## How to Run

### From the Launcher (GUI)

1. Open the BagiEngine launcher (`!start.cmd` / `!start.command`).
2. Click **Tidy Fix** in Quick Actions.
3. Choose scope: **Only changed files (git)** or **All files**.
4. Click **OK**. Output appears in the launcher log.

### From Terminal or Agent

From the **project root**:

```bash
# Only files with uncommitted changes
python CI/scripts/tidy_fix.py --changed

# All sources under src/ and config/
python CI/scripts/tidy_fix.py --all
```

With **uv**:

```bash
uv run python CI/scripts/tidy_fix.py --changed
```

Non-standard build directory:

```bash
python CI/scripts/tidy_fix.py --changed --build-dir build/ClangWindowsSolution
```

**After running**, apply Clang-Format to restore consistent style:

```bash
python CI/scripts/format_sources.py --changed
```

## Requirements

- **clang-tidy** on `PATH`, or **LIBCLANG_PATH** set to the LLVM bin directory (same as for clang-format and Meta Generator).
- **compile_commands.json**: requires at least one CMake configuration (`cmake -B build/...`). The script finds `build/*/compile_commands.json` automatically.
- Script: [CI/scripts/tidy_fix.py](CI/scripts/tidy_fix.py).
- Config: [.clang-tidy](.clang-tidy) in the project root.

## Scope

| Scope   | Files                                                 |
|---------|-------------------------------------------------------|
| Changed | Git-modified files (staged + unstaged), `.cpp`, `.c` |
| All     | All `.cpp`, `.c` under `src/` and `config/`           |

**Ignore:** Reads [.clang-format-ignore](.clang-format-ignore) (required). Same exclusions as clang-format — `external/`, `build/`, `.venv/`, vendored modules.
