# Software renderer: a skinned UI box is a cached nine-slice texture, about ten times cheaper per frame

- Version: 0.12.3
- Entry: ui-box-software-raster
- Date: 2026-11-23
- Bump: patch

## Added
- `UIBoxTexture` (a CPU raster of a box's mesh into a nine-slice picture) and `UIBoxTextureCache` (`CoreManager::GetUIBoxTextureCache()`): at `Reduced` geometry detail, the software renderer's detail, a box with an effect (rounded fill, border, gradient, shadow, glow) is drawn as blits of a cached texture instead of a mesh rasterized every frame. Adapt: nothing; a game's skinned boxes take the new path by themselves.
- The benchmark `ui-box-raster` (the box part of the benchmark screen at 1x and 2x) in the perf set, with a budget test of the screen. Adapt: nothing.

## Changed
- On the SDL software renderer the benchmark screen's boxes cost about 1.6 ms instead of 17.9 ms at 1x (1.75 instead of 24.0 at 2x); the look on the GPU backends (`Full` detail) is unchanged. A box moves in whole output pixels there, and corners and the shadow rim can differ by a few blended pixels. Adapt: regenerate a game's software-rendered reference screenshots if their threshold is 0.
