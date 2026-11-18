# Art generation: a generated set can be judged only in the panel's grid, not on the scene where it will live

- Version: 0.2.5
- Entry: artgen-live-alternatives
- Date: 2026-11-17
- Bump: patch

## Added
- Live alternatives of an art set: Accept all writes a candidate of every row (the pick, or the first variant of an image row) and keeps the other variants; a pick on an accepted, live row rewrites the game's file at once (`artgen_pick` answers `switched`), and the editor reloads the sprite.
- Keep (`artgen_keep {set, job?}`, the Review's Keep and Keep all) makes a row final: its other variants are trimmed and `"kept": {seed, date}` is written into the game's `.artgen.json` provenance; `artgen_set` rows carry `accepted.kept`, `artgen_state.sets[]` carries `live` and `kept` counts.
- A row whose name a newer set accepted later is shown as replaced: a switch, a Keep and a re-accept of it are refused or skipped, with the owning set named.
- Sets are handed to the service only while its generator is ready; default set names get `-2`, `-3` when the name of that second is taken.

## Changed
- `artgen_accept_set` no longer needs a pick on every image row; its rows report `{job, as, seed, alternatives}`. Adapt: a client that relied on the refusal of an unpicked row picks before accepting.
- The `artgen` skill ends a set accepted with alternatives alive and never calls `artgen_keep`. Adapt: nothing in a game.
