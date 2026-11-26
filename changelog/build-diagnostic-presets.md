# Diagnostic presets: thread sanitizer and coverage

- Version: 0.29.7
- Entry: build-diagnostic-presets
- Date: 2026-11-26
- Bump: patch

## Added
- `ENGINE_SANITIZE=thread` and the preset `macos-tsan` (suppressions in `cmake/sanitizers/tsan.supp`); `ENGINE_COVERAGE`, the preset `macos-coverage` and `CI/scripts/coverage_report.py` (an HTML and text report of the test run). Adapt: nothing; a game may run its tests under them from its own root.
- "Presets by purpose" in `.claude/rules/build-project.md`. Adapt: a game adds the same section to its own `build-project.md`.
