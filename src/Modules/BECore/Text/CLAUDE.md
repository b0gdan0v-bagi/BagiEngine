# Text Module Guidelines

## Overview

Text drawn by the engine through `IRenderer`, in both backends and both run modes (ImGui is not involved; it stays the tool UI). The font file is parsed with `stb_truetype` (`src/Modules/stb/`), glyphs are drawn into page textures, and a text is one textured quad per glyph. Nothing here needs a window to be measured or laid out, so layout is tested without a GPU.

```
Font (.ttf/.otf bytes, CPU)  ->  TextLayout (pure: lines, glyph positions)  ->  FontAtlas (pages of glyph pictures)  ->  TextRenderer (DrawTexture per glyph)
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
| `FontAtlas` | The glyphs of one font at one whole pixel size, in page textures made by `IRenderer::CreateTexture()`. White glyphs with the coverage in alpha, a 1 px transparent border around each. `Prepare()` adds missing glyphs as **new pages**; a page is never written again, so a frame in flight never reads a page that changes |
| `FontAtlasLibrary` (`CoreManager::GetFontAtlasLibrary()`) | One atlas per (font, pixel size), made on first use and preloaded with the ranges of `TextConfig.xml`. Drops everything when the renderer changes (`IRenderer::GetId()`); `Application::Cleanup()` calls `Clear()` before the renderer is destroyed |
| `TextSettings` | `config/TextConfig.xml`: `defaultFont`, `defaultFamily`, `pageSize`, the `families` table (family, faces of weight and path) with `ResolveFontPath(family, weight)` (the CSS nearest-weight rule, in the header), the `preload` code point ranges (Basic Latin, Latin-1, Cyrillic, the general punctuation by default) |
| `TextRenderer::Draw` | Prepares the glyphs, then draws one quad each at a whole output pixel |
| `TextLayoutCache` | Keeps the last layout of one text and makes a new one only when the font, the text or an option changed; `TextComponent` and `UILabel` each hold one |
| `TextComponent` | A scene component: text in the box of the node's `TransformComponent` |
| `UILabel` (`UI/CLAUDE.md`) | The same path in a UI canvas: the effective size is the UI size times the canvas scale, with a vertical alignment of its own |

## Sizes and pixels

- A font size is the **em square in pixels** (CSS `font-size`), not the ascent plus descent. At 16 the line is a little taller than 16 (`Font::GetVerticalMetrics(16).LineHeight()`).
- A text is laid out and rasterized in **output pixels**. `TextComponent` takes its size in scene units, multiplies by the scale of `IRenderer::GetViewTransform()` and rounds to a whole pixel (`FontAtlasLibrary::QuantizeSize`): a scene that is fitted to a bigger output gets sharp text, not a stretched picture. `TextRenderer::Draw()` converts back to the units of the view and snaps each glyph to a whole output pixel, so the atlas picture is drawn 1:1.
- The UI canvases (`UILabel`, the UI plan in the local plan store) use the same path: the effective size is the UI size times the canvas scale times the pixel density, and a canvas sets its own view transform around its draw.
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

A width or height of 0 means no limit. `fontPath` empty is the default font. Change the text at run time with `SetText()` (or write `_text`; the next draw lays it out again). `overflow="Clip"` clips the drawing to the box when the text does not fit; the clip is pushed and popped around the glyphs.

## Rules

- **Main thread for the GPU.** `FontAtlas` and `FontAtlasLibrary` make textures; never call them from `LoadAsync` or a pool thread. `Font` and `TextLayout` have no such limit.
- **Only your own fonts.** `stb_truetype` does no range checking of offsets in the file (its header says so), so a font file is a trusted asset of the engine or the game, never user input.
- **No GPU object outlives its renderer.** Hold glyph pages only through `FontAtlas` / the library. Anything that caches atlases itself must clear them before the renderer is destroyed (`IRenderer::WaitIdle()` first, then free).
- Code points the font lacks are drawn with its fallback glyph; do not substitute strings in game code.
- The default font is `assets/fonts/PT_Sans-Web-Regular.ttf` (ParaType, SIL Open Font License 1.1, license text next to it; the name "PT Sans" is a reserved font name, so ship it unmodified). It covers Basic Latin, Latin-1, Cyrillic including Ё and the general punctuation, which `tests/BECore/FontTest.cpp` checks. A game adds its own font to its `assets/` and sets `fontPath`, or replaces `TextConfig.xml` (a game's file replaces the engine's whole file).
- The engine ships an `assets/` directory now (`assets/fonts/`); `FileSystem` mounts it as the lowest layer of `assets`, and a game's file of the same relative path replaces it.

## Not done

Text shaping, ligatures, right-to-left and complex scripts, kerning from GPOS (PT Sans keeps its kerning there, so there is none), several fonts per text with fallback fonts, bold and italic as styles, rich text, vertical alignment inside the box, signed distance fields, color glyphs and emoji. A glyph that is larger than a page (very large sizes) is skipped with a log line.

## Tests

`tests/BECore/`: `Utf8Test`, `FontTest` (loads the shipped font, checks its coverage), `TextLayoutTest` (wrapping, alignment, ellipsis, auto-fit, Cyrillic), `ShelfPackerTest`, `FontAtlasTest` (pages, padding, never rewriting a page, the library, `TextRenderer`), `TextSettingsTest`, `TextComponentTest` (a scene from XML into a `RecordingRenderer`). The pixels on screen were checked on the SDL backend with a headless run and a screenshot; the Vulkan backend ran the same scene without errors but its picture was not looked at (no capture on Vulkan).
