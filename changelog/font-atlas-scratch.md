# Font atlas build allocates per glyph on the heap (19k blocks, 90 MB per ui atlas) and is rebuilt on a scene switch

- Version: 0.10.4
- Entry: font-atlas-scratch
- Date: 2026-11-23
- Bump: patch

## Added
- `Font::GetGlyphBox()` and `Font::RasterizeGlyphInto()` measure and draw a glyph into a caller's buffer with no heap allocation; `GlyphOutline::Kernel` and a `Dilate` into a caller's buffer do the same for outlines.

## Changed
- Building a font atlas keeps its temporaries, and stb_truetype's, in the thread's scratch arena and rasterizes straight into the page: about 8 times fewer heap allocations per atlas, the same pixels. Showing a scene again builds no atlas (a test guards it).
