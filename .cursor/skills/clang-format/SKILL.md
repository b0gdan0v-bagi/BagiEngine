---
name: clang-format
description: Apply project .clang-format to BagiEngine sources. Use when the user asks to format code, apply style, or run clang-format.
---

# Clang-Format (BagiEngine)

Apply the project code style from the root [.clang-format](.clang-format) to C/C++ sources. Only project code is formatted; the script uses its own ignore list so vendored and third-party code is never touched.

## When to Use

- User asks to format code, apply .clang-format, or fix formatting.
- User wants to ensure style consistency before commit or review.

## How to Run

### From the Launcher (GUI)

1. Open the BagiEngine launcher (e.g. via `!start.cmd` or `!start.command`).
2. Click **Clang-Format** in Quick Actions.
3. Choose scope: **Only changed files (git)** or **All files (src/, config/)**.
4. Click **Run**. Output appears in the launcher log.

### From Terminal or Agent

From the **project root**:

```bash
# Format only files with uncommitted changes
python CI/scripts/format_sources.py --changed

# Format all sources under src/ and config/
python CI/scripts/format_sources.py --all
```

With **uv**:

```bash
uv run python CI/scripts/format_sources.py --changed
uv run python CI/scripts/format_sources.py --all
```

## Requirements

- **clang-format** must be on `PATH`, or **LIBCLANG_PATH** set to the LLVM bin directory (same as for Meta Generator).
- Script path: [CI/scripts/format_sources.py](CI/scripts/format_sources.py).
- Config: root [.clang-format](.clang-format).

## Scope

| Scope    | Files included                                      |
|----------|------------------------------------------------------|
| Changed  | Git-modified files (staged + unstaged), extensions: .cpp, .c, .h, .hpp, .cxx, .hxx, .cc |
| All      | All such files under `src/` and `config/`            |

**Ignore:** The script reads [.clang-format-ignore](.clang-format-ignore) in the project root (one path or prefix per line; `#` comments). If the file is missing, a built-in default is used. By default, vendored libraries are excluded: `external/`, `build/`, `.venv/`, and the vendored modules under `src/Modules/` (BECore, EABase, EnTT, fmt, imgui, pugixml, Vulkan). You can add more paths in `.clang-format-ignore`; they are merged with the default.
