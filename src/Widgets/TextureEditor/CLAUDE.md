# Sprite Library panel

`TextureEditorWidget` edits the `TextureLibrary` (the sprite entries: name, texture, `srcRect`); Save and the `texture.save` command write the game's `TextureLibrary.xml`. Editor only (`TextureEditor` is one of the editor folders of `src/CMakeLists.txt`).

## Slice sheet

The section "Slice sheet" (`SpriteSheetSlicePanel`) cuts the selected entry's texture into one `SpriteEntry` per grid cell (`texturePath` of the sheet, the cell as `srcRect`) and draws the cells over the preview. It shows only for a loaded texture and adds entries to the library; nothing is written until Save.

- **The rules are pure.** `SpriteSheetGrid` (no ImGui, no `CoreManager`) holds the grid math, the names and the conflicts, and `SpriteSheetGridTest` tests it without a window; the panel only edits a `SheetGridSpec` and a `SheetGridNaming` and shows the result. A rule goes into the helper, not the panel.
- **Cells tile in whole pixels.** The room left after the margins and the spacings is divided by integer arithmetic: the edge of cell `i` is `margin + i * avail / n + i * spacing`. So nothing overlaps, nothing is left over and the widths differ by at most one pixel; a rect that starts inside a pixel is filtered across the pixel boundary at its edges.
- **Plan, then apply.** `PlanGridSlice` returns what would happen (the entries, the names that overwrite an entry, the count skipped) and changes nothing, so the panel can show the numbers before the button; `ApplyGridSlice` is `AddOrUpdate` per entry, which keeps the place of an old entry and appends new ones in cell order. The conflict policy `Overwrite` or `Skip` decides what a name the library already holds does.
- **Names** are valid sprite names (`IsValidSpriteName`: the one rule of the Generate panel's Accept) and unique inside a slice; the panel never writes a name the library could not save back.
- **A steady frame allocates nothing.** The plan is made again only when a field, the texture or the names in the library changed (a fingerprint of them is compared every frame); the fields are fixed `char` buffers.
