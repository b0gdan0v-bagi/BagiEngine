# Asset parts: the split file of the winning layer, and only when newer

- Version: 0.29.6
- Entry: asset-parts-layer-fixes
- Date: 2026-11-26
- Bump: patch

## Added
- `FileSystem::PathInTopLayer`: the path of a virtual path inside the top layer of its mount (empty when it would leave the layer). Adapt: nothing.

## Changed
- A split asset file is read only beside the whole file that wins the layer lookup and only when its write time is strictly later; a split alone in a higher layer no longer overrides an engine asset. Adapt: a layer that overrides an engine asset ships its whole file.
- `SplitAssetFile` fails, with a log line, when a stale split cannot be removed. Adapt: nothing.
- `AbstractFactory::FindMeta` and `Register` look a type up by its hash in one index. Adapt: nothing.
