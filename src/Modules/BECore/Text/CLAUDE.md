# Text Module Guidelines

## Overview

Text drawn by the engine through `IRenderer`, in both backends and both run modes (ImGui is not involved; it stays the tool UI). The font file is parsed with `stb_truetype` (`src/Modules/stb/`), glyphs are drawn into page textures, and a text is one textured quad per glyph. Nothing here needs a window to be measured or laid out, so layout is tested without a GPU.

```
Font (.ttf/.otf bytes, CPU)  ->  TextLayout (pure: lines, glyph positions)  ->  FontAtlas (pages of glyph pictures; GlyphOutline grows them)  ->  TextRenderer (DrawTexture per glyph)
        ^ FontLoader                                                              ^ FontAtlasLibrary (one atlas per font and pixel size)
                                                            TextComponent (a scene component that does all of it) 
```

| Class | Role |
|-------|------|
| `Font` (`IResource`) | The bytes of a font file and the parsed `stbtt_fontinfo` (hidden behind `Impl`; the only file with the stb implementation is `Font.cpp`). Metrics, advances, kerning (the `kern` table only), `RasterizeGlyph()` to a coverage bitmap. `GetFamilyName()` reads the family from the name table ("PT Sans"; the RmlUi host registers the font under it). `FindGlyph(cp)` is 0 for a missing code point, `ResolveGlyph(cp)` falls back to U+FFFD, then `?`, then `.notdef`. All queries are const and thread safe |
| `FontLoader` (`IResourceLoader`) | `.ttf` / `.otf`. Reads the file and parses it: CPU only, so it may run in `LoadAsync` on a pool thread |
| `Utf8` | `DecodeNext` (a malformed byte gives U+FFFD and consumes one byte, so a loop always ends), `Append`, `CountCodePoints` |
| `TextLayout::Layout(font, utf8, options)` | Pure. Lines, wrapping, alignment, line spacing, overflow (`Clip`, `Ellipsis`, `AutoFit`) to `PositionedGlyph`s: pen position of each glyph on its baseline, relative to the top-left of the text box. Rules are in the header comment |
| `ShelfPacker` | Pure rectangle packer used by the atlas |
| `GlyphOutline::Dilate` | Pure. A glyph bitmap grown by a radius of pixels on every side (the max of coverage times a one-pixel soft edge); the picture of an outline contains the fill. The radius is cut to `kMaxOutlinePixels` |
| `FontAtlas` | The glyphs of one font at one whole pixel size, in page textures made by `IRenderer::CreateTexture()`. White glyphs with the coverage in alpha, a 1 px transparent border around each. With `outlinePixels` above 0 the pictures are the dilated glyphs (offsets follow). `Prepare()` adds missing glyphs as **new pages**; a page is never written again, so a frame in flight never reads a page that changes |
| `FontAtlasLibrary` (`CoreManager::GetFontAtlasLibrary()`) | One atlas per (font, pixel size, outline pixels), made on first use; a plain one is preloaded with the ranges of `TextConfig.xml`, an outline one is not (its glyphs come on first use: dilating is the costly part). `QuantizeOutline` makes a width whole (0 stays 0, anything above is at least 1). Drops everything when the renderer changes (`IRenderer::GetId()`); `Application::Cleanup()` calls `Clear()` before the renderer is destroyed |
| `TextSettings` | `config/TextConfig.xml`: `defaultFont`, `defaultFamily`, `pageSize`, the `families` table (family, faces of weight and path) with `ResolveFontPath(family, weight)` (the CSS nearest-weight rule, in the header), the `preload` code point ranges (Basic Latin, Latin-1, Cyrillic, the general punctuation by default) |
| `TextRenderer::Draw` | Prepares the glyphs, then draws one quad each at a whole output pixel. With `TextEffects` (outline colour and width, hard shadow colour and offset, all in output pixels; paint, never layout) the whole layout is drawn in three passes: shadow (the outline pictures when there is an outline), outline, fill, so neighbours never paint over each other's effects. The caller passes the outline atlas of `QuantizeOutline(width)`; clipping stays the caller's |
| `TextLayoutCache` | Keeps the last layout of one text and makes a new one only when the font, the text or an option changed; `TextComponent` and `UILabel` each hold one |
| `TextComponent` | A scene component: text in the box of the node's `TransformComponent` |
| `UILabel` (`UI/CLAUDE.md`) | The same path in a UI canvas: the effective size is the UI size times the canvas scale, with a vertical alignment of its own |

## Sizes and pixels

- A font size is the **em square in pixels** (CSS `font-size`), not the ascent plus descent. At 16 the line is a little taller than 16 (`Font::GetVerticalMetrics(16).LineHeight()`).
- A text is laid out and rasterized in **output pixels**. `TextComponent` takes its size in scene units, multiplies by the scale of `IRenderer::GetViewTransform()` and rounds to a whole pixel (`FontAtlasLibrary::QuantizeSize`): a scene that is fitted to a bigger output gets sharp text, not a stretched picture. `TextRenderer::Draw()` converts back to the units of the view and snaps each glyph to a whole output pixel, so the atlas picture is drawn 1:1.
- The UI canvases (`UILabel`, the UI plan in the local plan store) use the same path: the effective size is the UI size times the canvas scale times the pixel density, and a canvas sets its own view transform around its draw.
- An outline width is in output pixels too and whole (`QuantizeOutline`); the outline atlas is keyed by it, so every width is an atlas of its own.
- `AutoFit` steps in whole pixels, which also keeps the number of atlases small. One atlas costs about 40 ms the first time (the preload), then nothing.

## Using text in a scene

```xml
<node name="Score"><components>
  <component type="TransformComponent" x="20" y="20" width="300" height="48"/>
  <component type="TextComponent" text="Score: 0" fontSize="32" align="Left" overflow="Ellipsis" wrap="false">
    <color r="255" g="255" b="255" a="255"/>
  </component>
</components></node>
```

Outline and hard shadow: `_outlineWidth` + `_outlineColor` and `_shadowOffsetX/Y` + `_shadowColor`, in scene units (times the view scale, like the size). The width is rounded to whole output pixels for the outline atlas and the offsets are rounded, so a shadow lands on a pixel. They are paint: the layout and its cache never see them, and a `Clip` box cuts them with the text. A `UILabel` has the same through `textOutline="width,color"` and `textShadow="offsetX,offsetY,color"` (`UITextEffects`), faded with the text of a disabled control.

A width or height of 0 means no limit. `fontPath` empty is the face of `fontFamily` (empty: the default family) at `fontWeight` (400 or 700, the nearest face of the table); a path wins over both. A changed path, family or weight is loaded by the next draw. A family that the table does not know is logged once per change and the default font is used. Change the text at run time with `SetText()` (or write `_text`; the next draw lays it out again). `overflow="Clip"` clips the drawing to the box when the text does not fit; the clip is pushed and popped around the glyphs.

## Rules

- **Main thread for the GPU.** `FontAtlas` and `FontAtlasLibrary` make textures; never call them from `LoadAsync` or a pool thread. `Font` and `TextLayout` have no such limit.
- **Only your own fonts.** `stb_truetype` does no range checking of offsets in the file (its header says so), so a font file is a trusted asset of the engine or the game, never user input.
- **No GPU object outlives its renderer.** Hold glyph pages only through `FontAtlas` / the library. Anything that caches atlases itself must clear them before the renderer is destroyed (`IRenderer::WaitIdle()` first, then free).
- Code points the font lacks are drawn with its fallback glyph; do not substitute strings in game code.
- **One choice of the face:** `FontAtlasLibrary::ResolveFontPath(FontRequest)` (path, else family and weight), used by `UILabel` and `TextComponent`; the layout check of a label measures through the same font it draws with.
- The default font is `assets/fonts/PT_Sans-Web-Regular.ttf` (ParaType, SIL Open Font License 1.1, license text next to it; the name "PT Sans" is a reserved font name, so ship it unmodified). It covers Basic Latin, Latin-1, Cyrillic including Ё and the general punctuation, which `tests/BECore/FontTest.cpp` checks. `TextConfig.xml` registers the families: PT Sans at 400 and 700 (`PT_Sans-Web-Bold.ttf`, the same OFL text `PT_Sans-OFL.txt`, unmodified) and Russo One at 400 (`RussoOne-Regular.ttf`, SIL OFL 1.1 in `RussoOne-OFL.txt`, a display face with Latin and Cyrillic; "Russo" is a reserved name, so unmodified too); any other weight takes the nearest face, so Russo One has bold only as its one face. Bold is a face, not a style: a width that fits at 400 may not fit at 700, and the validator measures with the chosen face. The web page grows by about 0.5 MB (`assets/` is packed whole). A game adds its own font to its `assets/` and sets `fontPath`, or replaces `TextConfig.xml` (a game's file replaces the engine's whole file).
- The engine ships an `assets/` directory now (`assets/fonts/`); `FileSystem` mounts it as the lowest layer of `assets`, and a game's file of the same relative path replaces it.

## Not done

Text shaping, ligatures, right-to-left and complex scripts, kerning from GPOS (PT Sans keeps its kerning there, so there is none), several fonts per text with fallback fonts, italic and synthetic bold (a weight is a face of its own file; a missing weight takes the nearest face), rich text, vertical alignment inside the box, shadow blur, RmlUi text in another face (its host loads the default face only), signed distance fields, color glyphs and emoji. A glyph that is larger than a page (very large sizes) is skipped with a log line.

## Tests

`tests/BECore/`: `Utf8Test`, `FontTest` (loads the shipped font, checks its coverage), `TextLayoutTest` (wrapping, alignment, ellipsis, auto-fit, Cyrillic), `ShelfPackerTest`, `FontAtlasTest` (pages, padding, never rewriting a page, the library, `TextRenderer`), `TextSettingsTest`, `TextComponentTest` (a scene from XML into a `RecordingRenderer`). The pixels on screen were checked on the SDL backend with a headless run and a screenshot; the Vulkan backend ran the same scene without errors but its picture was not looked at (no capture on Vulkan).
