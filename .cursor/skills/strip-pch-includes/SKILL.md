---
name: strip-pch-includes
description: Remove redundant #include lines from C/C++ sources that are already in the PCH. Use when cleaning includes, stripping PCH duplicates, or optimizing includes in BagiEngine sources.
---

# Strip PCH Includes (BagiEngine)

Remove `#include` lines from C/C++ sources that are already present in the precompiled header ([src/Modules/BECore/pch.h](src/Modules/BECore/pch.h)). Reduces redundant includes and keeps sources tidy. You can limit to a subfolder and run in parts.

## When to Use

- User asks to remove redundant includes, strip PCH duplicates, or clean up includes.
- After adding or changing many C++ files; run (optionally with dry-run) to drop includes already in PCH.

## How to Run

### From the Launcher (GUI)

1. Open the BagiEngine launcher (e.g. via `!start.cmd` or `!start.command`).
2. Click **Strip PCH includes** in Quick Actions.
3. **Limit to folder:** **All (src/, config/)** or **Choose folder...** (Browse to pick e.g. `src/Widgets`, `src/Modules/BECore`).
4. **Scope:** **Only changed files (git)** or **All files**.
5. Optionally check **Dry run** to report only, without modifying files.
6. Click **OK**. Output appears in the launcher log.

### From Terminal or Agent

From the **project root**:

```bash
# Dry run on all sources (report only)
python CI/scripts/strip_pch_includes.py --dry-run --all

# Actually remove PCH includes in all sources
python CI/scripts/strip_pch_includes.py --all

# Only changed files
python CI/scripts/strip_pch_includes.py --changed

# Limit to a subfolder (e.g. run in parts)
python CI/scripts/strip_pch_includes.py --dry-run --all --dir src/Widgets
python CI/scripts/strip_pch_includes.py --all --dir src/Modules/BECore
```

With **uv**:

```bash
uv run python CI/scripts/strip_pch_includes.py --dry-run --all
uv run python CI/scripts/strip_pch_includes.py --all --dir src/Widgets
```

## Requirements

- Script: [CI/scripts/strip_pch_includes.py](CI/scripts/strip_pch_includes.py).
- PCH (default): [src/Modules/BECore/pch.h](src/Modules/BECore/pch.h). Same ignore-style as format_sources; BECore is not excluded so `--dir src/Modules/BECore` works.

## Scope

| Option   | Meaning                                                                 |
|---------|-------------------------------------------------------------------------|
| Folder  | **All** = entire `src/` and `config/`; **Choose folder** = only that path (e.g. `src/Widgets`). |
| Scope   | **Changed** = only git-modified files; **All files** = all matching sources in the folder.     |
| Dry run | If set, only reports what would be removed; does not write files.      |
