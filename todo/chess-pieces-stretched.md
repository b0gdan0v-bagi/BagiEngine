---
id: chess-pieces-are-drawn-stretched-the-atlas-cell-is-not
---
# Chess pieces are drawn stretched: the atlas cell is not square

**Where:** `games/Chess/config/TextureLibrary.xml` (every cell is 116.67x190.5 of `assets/chess_test.png`), `games/Chess/src/Chess/Components/ChessPieceComponent.cpp` (sets the transform to the square size), `Scene/Components/SpriteRendererComponent.cpp` (`DrawTexture` into the whole transform rect).

**What is wrong:** a sprite is drawn into the full rect of its node, and a piece node is a square, so a 116.67x190.5 cell (width to height 0.61) is stretched horizontally by about 1.6x: the pieces look wide and squat. There is no way to keep the aspect ratio of a sprite.

**A fix has to cover:**

- either an aspect-preserving mode in `SpriteRendererComponent` (fit by height and center, which also helps every other sprite; see [the 2D rendering entry](rendering-2d.md)), or cells cropped to squares in the atlas and the `srcRect` values;
- the legal-move and selection highlights are drawn by the board in the full square, so they must still line up with the piece after the change.
