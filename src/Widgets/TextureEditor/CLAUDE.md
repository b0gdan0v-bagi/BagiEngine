# Sprite Library panel

`TextureEditorWidget` edits the `TextureLibrary` (the sprite entries: name, texture, `srcRect`); Save and the `texture.save` command write the game's `TextureLibrary.xml`. Editor only (`TextureEditor` is one of the editor folders of `src/CMakeLists.txt`).

## Slice sheet

The section "Slice sheet" (`SpriteSheetSlicePanel`) cuts the selected entry's texture into one `SpriteEntry` per rect (`texturePath` of the sheet, the rect as `srcRect`) and draws the rects over the preview. Two modes make the rects: **Grid** (equal cells) and **Alpha islands** (the separate shapes of the file). It shows only for a loaded texture and adds entries to the library; nothing is written until Save.

- **The rules are pure.** `SpriteSheetGrid` (no ImGui, no `CoreManager`) holds the grid math, the names and the conflicts, and `SpriteSheetGridTest` tests it without a window; the panel only edits a `SheetGridSpec` and a `SheetGridNaming` and shows the result. A rule goes into the helper, not the panel.
- **Cells tile in whole pixels.** The room left after the margins and the spacings is divided by integer arithmetic: the edge of cell `i` is `margin + i * avail / n + i * spacing`. So nothing overlaps, nothing is left over and the widths differ by at most one pixel; a rect that starts inside a pixel is filtered across the pixel boundary at its edges.
- **Plan, then apply.** `PlanRectSlice` is the one plan for both modes (`PlanGridSlice` is `ComputeGridCells` and then it): it returns what would happen (the entries, the names that overwrite an entry, the count skipped) and changes nothing, so the panel can show the numbers before the button; `ApplyGridSlice` is `AddOrUpdate` per entry, which keeps the place of an old entry and appends new ones in list order. The conflict policy `Overwrite` or `Skip` decides what a name the library already holds does.
- **Names** are valid sprite names (`IsValidSpriteName`: the one rule of the Generate panel's Accept) and unique inside a slice; the panel never writes a name the library could not save back.
- **A steady frame allocates nothing.** The plan is made again only when a field, the texture or the names in the library changed (a fingerprint of them is compared every frame); the fields are fixed `char` buffers.

## Alpha islands and the image helper

- **`SpriteImage`** holds pixels on the CPU as straight RGBA (`RgbaImage`) over SDL_image, which the build already has; the engine carries no stb image code. `SaveRgbaPng` writes the same bytes for the same image, so a later packer's output can be compared and a rewrite is not a diff; it is a plain write, a caller that must not lose the old file replaces atomically itself.
- **`FindAlphaIslands`** (pure, no SDL or ImGui) rules: a pixel is solid when its alpha is above the threshold; solid pixels are 8-connected; boxes merge by overlap or by a gap before the small ones are dropped, so a dot joins its body instead of vanishing; padding is clamped to the sheet and not resolved against neighbours (rects may overlap); the order is reading order by rows, the topmost island leading a row, because sprite names follow it.
- **The finder runs on the button, never per frame**: it decodes the file and scans it, which a drag field would repeat every frame. The panel keeps the rects, not the pixels, and clears them when an island field, the texture or its size changes, so the overlay never shows a stale result. The file's size must equal the preview texture's, or the file changed under the editor and the result is refused.
