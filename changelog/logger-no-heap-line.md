# LOG_X takes a compile-time checked format and its arguments, and formats without a heap String

- Version: 0.12.1
- Entry: logger-no-heap-line
- Date: 2026-11-23
- Bump: patch

## Added
- `LOG_DEBUG` / `LOG_INFO` / `LOG_WARNING` / `LOG_ERROR` / `LOG_FATAL("text {}", args...)`: the format string is checked at compile time (a placeholder without an argument or a type with no formatter does not compile) and the line is formatted into a stack buffer of the ring cell's size, so a formatted log line allocates nothing on any thread; a line longer than the cell is cut as before. Adapt: write new lines as `LOG_X("x {}", v)`.
- The codemod family `log` (`tools/codemod/codemod.py --family log`) rewrites `LOG_X(Format(...))` and `LOG_X("..."_format(...))` (and a trailing `.c_str()`) to the formatted form and reports what it cannot unwrap; `--fail-on-change` is the engine's extended ctest `BagiLogCalls`. Adapt: after `Update engine` run `python3 engine/tools/codemod/codemod.py --root src --family log` (and `--root tests`), then build; a format whose placeholders do not match its arguments is now a compile error: fix the text.

## Changed
- `LOG_X(text)` with one argument still logs the text as it is (a `{` in it is not parsed), and `LOG_X(Format(...))` / `LOG_X("..."_format(...))` still compile; they build a heap `String` per line, so the engine's own call sites moved to the formatted form. `Logger::Info(text, site)` and the other direct calls are unchanged. Adapt: nothing is required to keep building.
