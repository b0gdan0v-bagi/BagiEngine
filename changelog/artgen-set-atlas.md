# Art generation: an accepted set lands as loose PNGs, never in an atlas (no atlas group for a set or a preset)

- Version: 0.5.8
- Entry: artgen-set-atlas
- Date: 2026-11-19
- Bump: patch

## Added
- A set and a preset can name an atlas group (`atlas`): every sprite accepted from it joins the group, and Accept all packs the group once inside the accept batch, before the one library save; rows are marked after the save. The loose PNGs and their `.artgen.json` stay the `atlas.source` of the packed entries, so a later Accept of one member repacks the group.
- `ArtGenSession::Accept` takes an optional atlas group (`atlas`, null: the run's, empty: none); old calls compile unchanged.
- The control API's accept answer names the atlas when one was packed.
- The `artgen` skill makes a set of sprites of one kind with an atlas group.
