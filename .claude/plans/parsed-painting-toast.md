# Chess: pieces cover the squares — transparent atlas and texture byte order

## Context

Part 1 (done, commit `693be62d` on branch `draw-layers`): the board was painted over the pieces because draw order followed the event dispatcher. `Scene::Draw()` now sorts nodes by (`layer`, traversal index) and calls `IComponent::OnDraw()`; the Chess `Board` is on layer -1.

Part 2 (this plan): the pieces are now drawn on top, but each one hides its square. "Piece drawing overlaps the cell drawing." Two causes, both found by reading the code and decoding the atlas (no screenshot is possible from the agent session):

1. **The atlas is fully opaque.** `games/Chess/assets/chess_test.png` is stored as RGBA, but the alpha of all 266,700 pixels is 255 (decoded with a small zlib script), and the background is near-white (`253..255` per channel). Every sprite therefore paints an opaque white 80x80 rectangle over its square, so the square color and the legal-move highlights under a piece are never seen. Blending is not the problem: both Vulkan pipelines already use `SRC_ALPHA / ONE_MINUS_SRC_ALPHA` (`VulkanTexturePipeline.cpp:119-127`, `texture.frag.glsl` returns the texel unchanged).
2. **Engine bug in the texture upload, which a transparent atlas would hit at once.** `VulkanTextureLoader.cpp:140` converts the surface to `SDL_PIXELFORMAT_RGBA8888` and uploads it as `VK_FORMAT_R8G8B8A8_SRGB`. `RGBA8888` is a packed format: on little-endian (macOS arm64, Windows x86) its bytes in memory are A,B,G,R (`external/SDL/include/SDL3/SDL_pixels.h:690-693`: `RGBA32` is `ABGR8888`, `ABGR32` is `RGBA8888`). Vulkan then reads R=A, G=B, B=G and **alpha = the red channel**. With the opaque white background it hid itself (alpha = 254), but dark pieces (red about 40-60) already get alpha about 0.2 and a red cast, and a transparent background stored as `(255,255,255,0)` would be read as alpha 255 and stay opaque. So step 1 below is a precondition of step 2, not an extra.

Ruled out: draw order (fixed in part 1), the `DrawTexture` pipeline and shader, the `TextureLibrary` entries (srcRects stay valid as the atlas size does not change).

## Changes

1. **`src/Modules/Vulkan/src/CoreVulkan/VulkanTextureLoader.cpp:139-140`**: convert to `SDL_PIXELFORMAT_RGBA32` (the byte-array order R,G,B,A on any endianness) and fix the comment to say that. One line in `src/Modules/Vulkan/CLAUDE.md` (`VulkanTextureLoader` row): textures are uploaded in RGBA byte order to match `VK_FORMAT_R8G8B8A8_SRGB`. The SDL backend (`SDLTextureLoader`) is untouched: `SDL_CreateTextureFromSurface` does not have this problem.
2. **Atlas with a real alpha channel**, same file name and size (700x381), so `TextureLibrary.xml` keeps its srcRects:
   - A one-off script in the scratchpad (pure Python with `zlib`; PIL is not installed, and the script is not added to the repo). Flood fill from the image border over near-white pixels (every channel >= 235, which covers the `253..255` noise); those pixels get alpha 0. The outlines of the pieces are closed, so the white bodies of the white pieces stay opaque.
   - Edge pixels that touch the removed region get a partial alpha from their darkness and the neighbouring outline color, so there is no light halo on the brown board.
   - Overwrite `games/Chess/assets/chess_test.png`; the original stays in git history.
   - `TextureLibrary.xml`: the header comment says the atlas has a transparent background.
3. **Docs**: `games/Chess/CLAUDE.md` one line (the piece atlas needs an alpha channel: a sprite with an opaque background hides the square under it).
4. No new gtest: the loader needs a Vulkan device (nothing in `tests/` creates one), so step 1 is checked against the SDL header and by running the game. Part 1's draw-order tests stay as they are.

## Not changing (noting only)

- The atlas cell is 116.67x190.5 and is drawn into an 80x80 square, so the pieces look stretched horizontally. Fixing it needs a decision (fit by height and center, or crop the cells). Left out unless asked.
- Selected-square and legal-move highlights are drawn under the pieces (board on layer -1). With a transparent atlas they now show around the figure, which is the intended picture.

## Verification

1. Check the new PNG with the same decoder: corner and border pixels alpha 0, the body of a white piece alpha 255, no fully transparent cell, and the opaque share per cell in a plausible range. Composite it over the board colors (light `240,217,181`, dark `181,136,99`) into a preview PNG in the scratchpad and look at it (`Read` shows images) for leaks into the white bodies and for halos.
2. `cmake --build --preset macos-debug --target BagiEngineTests ChessTests Match3Tests BagiChess BagiMatch3` (no reflection change, so no re-configure), then `ctest --preset macos-debug` (284 tests must still pass).
3. Run `build/ClangmacOS/BagiChess` and look: squares visible around every figure, no white boxes, black pieces dark (not ghostly red), a click on a pawn shows the highlights around the figure. If the black pieces still look wrong, the byte-order reading is incomplete: compare with `BagiMatch3`, which uses the same upload path for its textures.
4. `clang-format` skill on `VulkanTextureLoader.cpp`; no include changes, so `strip-pch-includes` has nothing to do.
5. Commit on `draw-layers` only when asked; no push.

## Status (done)

- `VulkanTextureLoader.cpp` converts to `SDL_PIXELFORMAT_RGBA32`; `Vulkan/CLAUDE.md` and `games/Chess/CLAUDE.md` say so; `TextureLibrary.xml` comment updated.
- `games/Chess/assets/chess_test.png` now has a real alpha channel: 203,460 transparent pixels, 12 connected figures (one per piece), the border is fully transparent, the bodies of the white pieces stay opaque. The same pass rebuilt the bottom-right corner of the black pawn's base, which the image generator's watermark had covered with a grey triangle (mirror of the left side, the pawn is symmetric about x = 639.5). The one-off script is not in the repo; the original atlas is in git history.
- Build of all five targets is clean, `ctest --preset macos-debug` 284/284. `BagiChess` starts with a clean log and loads the texture. Not verified: the picture itself (no screenshot access from the agent session).
- Not committed (commit on `draw-layers` only when asked).
