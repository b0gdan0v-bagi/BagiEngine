# Perf: a benchmark set names one preset, so a game's set runs on one OS only (Chess: Windows, not the Mac)

- Version: 0.26.3
- Entry: perf-preset-per-platform
- Date: 2026-11-25
- Bump: patch

## Added
- `preset` and `profilePreset` of a game's benchmark set (`.claude/rules/perf-project.md`) may be an object keyed by platform (`macos`, `windows`, `linux`); `tools/perf/perf.py` takes the running OS's preset and stops with a message naming the platform when the set has none for it. A plain string works as before.
