# Fix: `clang-format` is not found on macOS

## Context

`python CI/scripts/format_sources.py --changed|--all` (the `clang-format` skill, `CLAUDE.md` → "After Making Changes") fails with
`Error: clang-format not found. Add it to PATH or set LIBCLANG_PATH to LLVM bin directory.`

The binary exists on this machine, the script just cannot see it:

| Where | Result |
|-------|--------|
| `PATH` | no `clang-format` (Homebrew `llvm` is keg-only, so `/opt/homebrew/bin` has no link) |
| `/opt/homebrew/opt/llvm/bin/clang-format` | **21.1.8**, works with the repo's `.clang-format` |
| Xcode / CLT (`xcrun --find clang-format`, `/Library/Developer/CommandLineTools/usr/bin`) | Apple 17.0.0, also works |
| `.venv/bin` | not there, although `requirements.txt` lists `clang-format>=14.0.0` |
| `LIBCLANG_PATH` | the repo's convention is the **`lib`** dir (`.vscode/settings.json`: `/opt/homebrew/opt/llvm/lib`; `meta_generator/setup_env.sh` checks for `libclang.dylib` in it). The shell here has a broken Xcode path. |

Root causes in `find_clang_format` ([format_sources.py:46-65](CI/scripts/format_sources.py#L46-L65)):

1. It only tries `PATH`, then `LIBCLANG_PATH` and `LIBCLANG_PATH/bin`. With the conventional `.../llvm/lib` value it looks at `.../llvm/lib/bin/clang-format`, which does not exist. `.../llvm/bin` (`LIBCLANG_PATH/../bin`) is never tried.
2. There are no macOS fallbacks (Homebrew LLVM, Xcode, project `.venv`), so a plain `brew install llvm` setup never works.
3. The script reads only `os.environ`; the launcher loads `.env` (`CI/launcher/main.py`) but a terminal or agent run does not.

## Changes

1. **[CI/scripts/format_sources.py](CI/scripts/format_sources.py)** — rewrite `find_clang_format` as an ordered search, first hit wins:
   1. `shutil.which("clang-format")`
   2. `LIBCLANG_PATH`: candidates `base`, `base/bin`, **`base.parent/bin`** (covers the `.../lib` convention). Keep the existing `.exe` handling for Windows.
   3. Project venv: `.venv/bin/clang-format` (`.venv/Scripts/clang-format.exe` on Windows), the pip package from `requirements.txt`.
   4. macOS only: Homebrew `/opt/homebrew/opt/llvm/bin`, `/usr/local/opt/llvm/bin` (Intel) and versioned `llvm@*` kegs (newest first), then `xcrun --find clang-format`.
   - Name the search constants (no magic strings inline), `Path | None` return type unchanged.
   - Print `Using clang-format: <path> (<first line of --version>)` before formatting, because Homebrew 21 and Xcode 17 can format slightly differently and the user should see which one ran.
   - On failure, list the places that were searched and hint `brew install llvm` / `pip install -r requirements.txt`.
2. **Same file, format loop** ([format_sources.py:153-167](CI/scripts/format_sources.py#L153-L167)) — `subprocess.run` is called without checking `returncode`, so a clang-format that rejects the config would still print every filename and `Done.`. Record non-zero exits (with `stderr`) in `failed`. Small, but it keeps the newly found binary from failing silently.
3. **[.claude/skills/clang-format/SKILL.md](.claude/skills/clang-format/SKILL.md)** — update "Requirements": describe the lookup order above and note that Homebrew LLVM is keg-only (not on `PATH`) so no manual setup is needed. Keep it short.

Not changed on purpose:
- `LIBCLANG_PATH` in `.env`, `.vscode/settings.json` or the user's shell: meta_generator expects the `lib` form, so the finder should adapt to it, not the other way round.
- `CI/scripts/tidy_fix.py::find_clang_tidy` has the same weakness (macOS fallbacks missing). Left alone to keep this change small; easy follow-up if wanted.

## Verification

1. Finder only, no files touched: import `find_clang_format` and print the result under
   - normal env (expect `/opt/homebrew/opt/llvm/bin/clang-format`),
   - `PATH` stripped of Homebrew and `LIBCLANG_PATH=/opt/homebrew/opt/llvm/lib` (expect the `../bin` hit),
   - `LIBCLANG_PATH` set to the broken Xcode path (expect the Homebrew fallback),
   - Homebrew path hidden (expect `xcrun` / Xcode fallback),
   - everything hidden (expect the new error message listing searched places).
2. `python3 CI/scripts/format_sources.py --changed` from the repo root: prints `Using clang-format: ...`, formats the uncommitted Chess sources, ends with `Done.`; then `git diff --stat` shows formatting-only changes and a second run produces no further diff (idempotent).
3. `python3 CI/scripts/format_sources.py --all` is not needed for verification.
4. Per `CLAUDE.md`, the only C++ sources touched by step 2 are the user's already-modified ones; no includes change, so `strip-pch-includes` is not needed.
