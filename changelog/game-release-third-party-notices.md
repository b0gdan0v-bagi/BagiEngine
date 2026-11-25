# A game's release build carries the third-party notices

- Version: 0.25.5
- Entry: game-release-third-party-notices
- Date: 2026-11-25
- Bump: patch

## Added
- `be_add_game_executable` writes `THIRD_PARTY_NOTICES.txt` next to every game executable (inside `Resources` of a macOS app) from the engine's `THIRD_PARTY_NOTICES.md` plus the game's own; `NOTICES <file>` names the game's table, by default `<GAME_ROOT>/THIRD_PARTY_NOTICES.md` when it exists. A ctest `<Game>ThirdPartyNotices` (label `smoke`) checks the built file. Adapt: nothing; a game with its own libraries, fonts or assets adds `THIRD_PARTY_NOTICES.md` in the engine's table format (`NEW_GAME.md`).
- The web package publishes the build's notices as `third-party-notices.txt` and `web/shell.html` links it. Adapt: a game with its own `shell.html` adds the link if it wants one.
- `CI/scripts/third_party_notices_bundle.py` (`write`, `verify`) builds and checks the notices file from the tables. Adapt: nothing.
