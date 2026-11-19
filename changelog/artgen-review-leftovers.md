# Art generation: small leftovers of the set reviews (one save per Accept all, untested evict, tie of FindNewerOwner, docs thinned for budget)

- Version: 0.4.4
- Entry: artgen-review-leftovers
- Date: 2026-11-19
- Bump: patch

## Added
- `ArtGenLibraryBatch` and the editor's accept batch (`BeginAcceptBatch` / `EndAcceptBatch`): Accept all of a set saves the game's `TextureLibrary.xml` once, and a failed save puts the library back as it was.
- `EvictAcceptedTextures`, the cache step of an accept, tied to a test with `IsEvictedHandle`.

## Changed
- `UISprite` and `ParticleEmitterComponent` load their texture again when the resource cache evicted its path (an Accept or a switch of the Generate panel rewrote the file); a texture given with `SetTexture` is never checked.
- The owner of a sprite name is the accept with a strictly later date; an accept without a provenance file removes the stale `.artgen.json` of an earlier one.
- The docs of the Generate panel and the sets that the budget had cut are restored.
