# Sprite Library panel

`TextureEditorWidget` edits the `TextureLibrary` (the sprite entries: name, texture, `srcRect`, the optional atlas group) and shows it: a list with a thumbnail, the texture and an atlas badge per sprite, and a preview of an atlas; Save and the `texture.save` command write the game's `TextureLibrary.xml`. Editor only (`TextureEditor` is one of the editor folders of `src/CMakeLists.txt`).

## Slice sheet

The section "Slice sheet" (`SpriteSheetSlicePanel`) cuts the selected entry's texture into one `SpriteEntry` per rect (`texturePath` of the sheet, the rect as `srcRect`) and draws the rects over the preview. Two modes make the rects: **Grid** (equal cells) and **Alpha islands** (the separate shapes of the file). It shows only for a loaded texture and adds entries to the library; nothing is written until Save.

- **The rules are pure.** `SpriteSheetGrid` (no ImGui, no `CoreManager`) holds the grid math, the names and the conflicts, and `SpriteSheetGridTest` tests it without a window; the panel only edits a `SheetGridSpec` and a `SheetGridNaming` and shows the result. A rule goes into the helper, not the panel.
- **Cells tile in whole pixels.** The room left after the margins and the spacings is divided by integer arithmetic: the edge of cell `i` is `margin + i * avail / n + i * spacing`. So nothing overlaps, nothing is left over and the widths differ by at most one pixel; a rect that starts inside a pixel is filtered across the pixel boundary at its edges.
- **Plan, then apply.** `PlanRectSlice` is the one plan for both modes (`PlanGridSlice` is `ComputeGridCells` and then it): it returns what would happen (the entries, the names that overwrite an entry, the count skipped) and changes nothing, so the panel can show the numbers before the button; `ApplyGridSlice` is `AddOrUpdate` per entry, which keeps the place of an old entry and appends new ones in list order. The conflict policy `Overwrite` or `Skip` decides what a name the library already holds does.
- **Names** are valid sprite names (`IsValidSpriteName`: the one rule of the Generate panel's Accept) and unique inside a slice; the panel never writes a name the library could not save back.
- **A steady frame allocates nothing.** The plan is made again only when a field, the texture or the names in the library changed (a fingerprint of them is compared every frame); the fields are fixed `char` buffers.

## Alpha islands and the image helper

- **`SpriteImage`** holds pixels on the CPU as straight RGBA (`RgbaImage`) over SDL_image, which the build already has (it decodes with its bundled stb; the engine adds no stb image code of its own). `SaveRgbaPng` writes the same bytes for the same image, so the packer's output can be compared and a rewrite is not a diff; it is a plain write, and `ReplaceRgbaPng` is the same bytes through a temp file renamed over the old one, for a caller that must not lose it.
- **`FindAlphaIslands`** (pure, no SDL or ImGui) rules: a pixel is solid when its alpha is above the threshold; solid pixels are 8-connected; boxes merge by overlap or by a gap before the small ones are dropped, so a dot joins its body instead of vanishing; padding is clamped to the sheet and not resolved against neighbours (rects may overlap); the order is reading order by rows, the topmost island leading a row, because sprite names follow it.
- **The finder runs on the button, never per frame**: it decodes the file and scans it, which a drag field would repeat every frame. The panel keeps the rects, not the pixels, and clears them when an island field, the texture or its size changes, so the overlay never shows a stale result. The file's size must equal the preview texture's, or the file changed under the editor and the result is refused.

## Atlas

The section "Atlas" (`SpriteAtlasPanel`) packs the sprites of one **atlas group** into one PNG (`assets/atlases/<name>.png`) and unpacks them again. The rules are `SpriteAtlasPacker` (pure placement) and `SpriteAtlas` (the model and the files), tested without ImGui; the panel is the fields, the two buttons, and the refresh of what showed the old pixels (the texture cache, the sprites of the active scene). Nothing reaches `TextureLibrary.xml` until Save.

- **The model is two optional fields of the entry.** `atlas.name` is the group; `atlas.source` (the file and rect the pixels came from) is engaged exactly when the sprite is packed, and then the entry's own `texturePath` and `srcRect` point into the atlas. So a repack reads the sources, never the old atlas (a changed source is a plain Pack), and Unpack is the exact inverse: the float rect comes back as it was and the group name stays, so a later Pack repacks the group.
- **The packer's order is a contract.** Shelf placement in a fixed sort order with fixed tie rules, padding only between sprites (never at the atlas edge), the width chosen by area then by the smaller width, power of two or tight: the same sprites give the same file, so a repack is not a diff of the asset. Members are taken sorted by name for the same reason, whatever the library's order.
- **Plan, then pixels.** `PlanAtlas` (sizes in, layout out; no pixel) is the one planning step, and `BuildAtlas` is it plus the pixel copy. The checks, the error texts and the crop below live there, so what the preview shows is never another layout than the file.
- **Crop.** A rect is cut at its edges rounded to the nearest pixel (a side is at least a pixel), so the fractional rects of a sheet do not take in a neighbour; the float rect is kept in `source`, so the rounding costs nothing on Unpack.
- **Write order.** The PNG is replaced atomically first, the library changes after: a failure leaves both as they were. Unpack leaves the atlas file on disk (the engine never deletes a game's file) and refuses when no member is packed or a source file is gone.
- **A packed sprite is read-only** in the panel: its texture combo, source rect fields, drag-to-select and the Slice section are off until Unpack, because editing them would cut the link. The pack settings (padding, size mode, max side) are saved in every member's link (`atlas.pack`, written by `BuildAtlas`, kept by Unpack, dropped when a sprite changes group); `PackSettingsOf` reads them back and the section loads them into its fields when the selected sprite's group changes (a group with no record keeps the fields), so a repack here and an Accept of the Generate panel make the same file (Accept repacks a packed sprite's group by itself: `ArtGen/CLAUDE.md`, "Accept").
- **Pack and Unpack run on the click**, synchronously (a few hundred sprites at most); the status line is a fixed buffer and the counts are ImGui-formatted, so a steady frame allocates nothing.

## Library list

- **The rules are pure** (`SpriteLibraryView`, tested alone; `SpriteLibraryPanel` draws). The filter works on the group name (`No atlas` is an empty name whatever the state); the state (none, in a group, packed) is a separate badge, because a group member that is not packed is neither.
- **The list is made again on a key** of names, texture paths, group names and packed flags, never of rects, so a dragged rect rebuilds nothing; a filter whose group vanished falls back to All. The panel owns the filter; the selection stays on the widget, and a selected sprite the filter hides is still the selected one.
- **Only visible rows load a texture** (a clipper over fixed-height rows). `SpriteTextureCache` loads a path once, loads it again when the resource cache no longer holds the handle (Pack, Unpack and Accept evict), and **remembers a failed load** so a missing file logs once, until `ForgetFailed` (Rescan, Pack, Unpack).

## Atlas preview

- **Two modes.** `Current` draws the packed members on the atlas file as the library has them; `Plan` is what Pack would make with the Atlas section's settings, from the sources, and writes nothing. `Current` is off while nothing is packed.
- **The plan is `PlanAtlas`**, the step `BuildAtlas` runs, so a preview is never another layout than the file (a test holds it: the plan's rects equal what `WriteAtlas` then does to the entries). `Plan` takes the sizes of the sources from the loaded textures, so an unloaded one is an error line, not a guess.
- **The fill ratio** is the sum of the rect areas over the atlas area; overlaps are not looked for, the packer makes none. Rects that leave the picture and members of the group not on this file are counted and said.
- **Made again on `AtlasPreviewKey`** (members' name, texture, rect, packed flag, source), the mode, the settings in `Plan`, or a change in the size of a texture it read (a load, an evict, a rewritten file); a steady frame allocates nothing. The key walks the library in its own order, so it never builds a list.
