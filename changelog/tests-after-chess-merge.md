# Tests: after the games/chess merge into main, one hygiene check is red and the chess tests predate the tests-run-cost rules

- Version: 0.6.6
- Entry: tests-after-chess-merge
- Date: 2026-11-19
- Bump: patch

## Changed
- `VulkanTexturePipeline.h` includes the `Vector` it uses, so it compiles alone again (`BagiHeadersStandalone` is green). Adapt: nothing.
- Test support: `tests/Common/TestFiles.h` has `ReadFile` (moved from `MigrationTwin.h`) and `ReadFileBytes` next to `WriteFile`; the ArtGen tests share `tests/Common/ArtGenTestSupport.h`. The tests that came with `games/chess` use these helpers and `TempPath.h`, and their cases that only repeated a neighbour are merged or removed. Adapt: a game's test with its own `ReadFile(path)` of the same signature that includes `TempPath.h` or `TestFiles.h` gets an ambiguous call; delete the local copy.
