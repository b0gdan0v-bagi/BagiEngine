# Editor: sprites cannot be packed into an atlas or cut from a sheet; atlas rects are written by hand

- Version: 0.3.3
- Entry: atlas-pack
- Date: 2026-11-18
- Bump: patch

## Added
- The Sprite Library (`Widgets/TextureEditor/`) slices a sheet into sprites, by a grid (columns x rows) or by alpha islands, with names.
- The Atlas section packs a group of sprites into `assets/atlases/<atlas>.png`. Packing is deterministic shelf packing with padding, size mode and max side. It rewrites the sprites' rects, keeps their names, and can unpack them back to single textures.
- A preview of the current atlas or of the pack plan shows the rects, names, fill ratio and size.
- The library list shows a thumbnail per sprite, its texture and an atlas badge, and filters by atlas.
- A sprite entry of `TextureLibrary.xml` can carry an optional `atlas` link: the group name, the source image and rect, and the pack settings. The link is written only when engaged. Adapt: nothing; existing files load unchanged.
- An artgen Accept into a packed sprite repacks its atlas. The `artgen_accept` answer and the panel say so.
