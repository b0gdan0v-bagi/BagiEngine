# Art generation: the Generate panel mixes a single run, the set queue and the review, and never says what reaches the game

- Version: 0.2.6
- Entry: artgen-panel-ux
- Date: 2026-11-17
- Bump: patch

## Added
- `ResourceCache::Holds(path, resource)`: whether the cache still holds that very object under the path (an evicted or replaced handle answers false).
- The Generate panel says what is a draft and what reaches the game: `artgen/out/` is a git-ignored draft; Accept, Accept all, Keep and Keep all name `assets/artgen/<name>` and `config/TextureLibrary.xml` (in git); each Review row has a status line (not in the game, in the game with its file, kept, replaced by a newer set); `ArtGenRowStatus` / `StatusOf` and `ArtGenGameFile` hold these facts.

## Changed
- The Generate panel: the preset (Settings collapsed) is shared above the tabs; Subject, Count, seed, Generate and Cancel live only on the Generate tab, which shows a set's running job as one line; the Set tab has its own Count and seed; the Review footer counts live, kept and replaced rows. Adapt: nothing in a game.
- The Sprite Library panel loads its preview again after the texture is evicted, so a re-pick or re-accept of a generated sprite shows at once; an unchanged sprite entry is not saved to `TextureLibrary.xml` again. Adapt: nothing in a game.
