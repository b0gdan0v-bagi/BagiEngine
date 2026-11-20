# Loading an asset allocates field by field: no size hints in the file for one arena and exact reserves

- Version: 0.8.2
- Entry: asset-load-alloc-hints
- Date: 2026-11-20
- Bump: patch

## Changed
- Loading a reflected array reserves its exact element count first (the XML reader counts the elements before collecting them; the vector-like accessors reserve, a fixed-capacity container without overflow is left alone), so a load no longer grows its vectors step by step.
  Adapt: nothing.
- `TextureLibrary` keeps its name index as a flat open-addressing table instead of a node per sprite, and `Load` reserves its entries and index: loading a sprite sheet allocates a few blocks instead of one per sprite (an XML sheet 2034 to 34 allocations, binary 2011 to 11).
  Adapt: nothing.
