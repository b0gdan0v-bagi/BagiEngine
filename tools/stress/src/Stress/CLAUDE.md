# The sources of BagiStress

The app, its flags, scenes, control API and the two measuring modes: `tools/stress/CLAUDE.md`. Here: the scene `sprites` and the loads on top of it.

## The `sprites` scene

`SpriteField` (pure) places sprite `i` of `count`: position, size, spin and the cell of the atlas depend on the index alone (`StressHash.h`: an own integer hash, never `std::rand` or the `std::` distributions: the numbers must not differ between standard libraries), so a bigger count keeps the first sprites where they are. The atlas and the blend mode are what a batcher sees: by default consecutive sprites share them (`count / atlases` per atlas, `kBlendRunLength` per blend mode: the way a game sorts), `--stress-interleave` changes both on every sprite (the worst case). So the runs depend on the count, and a count change re-applies atlas and blend mode to the kept nodes (once, not per frame).

`StressSpritesComponent` makes `count` child nodes of its node (`TransformComponent` + `SpriteRendererComponent`, one shared name) when `StressState::Version()` moved: it appends at the tail or removes from the tail, never rebuilds. The atlases are made once at the first update with `IRenderer::CreateTexture` (`SpriteField::MakeAtlasPixels`; the renderer can be missing at `OnAttached`) and handed to the sprites with `SetTexture`; at `ApplicationCleanUpEvent` it waits for the GPU and lets go of them before the renderer dies. A steady frame only adds `spin * deltaTime` to each `_rotation` through cached pointers. One `IRenderer` call per sprite, which is what the renderer's batcher gathers; `--stress-no-batching` turns the batcher off at the first update of `StressRunComponent` (the comparison of a GPU backend with and without it).

## Loads

A load is a flag of this scene, not a scene of its own: they combine, and the control API and the scene file do not change. `StressSettings::load` holds them, and the line of a run carries a key for each, so two lines say whether they measured the same thing. Without a flag the field is the plain one.

- **Sprite size** (`--stress-sprite-size=S`, key `spriteSize`): every sprite is a square of side `S`, still placed inside the field. Large sprites overlap many deep, so the load is overdraw: the GPU's fill (`gpuMs`) grows with `count * S^2` while the CPU side stays that of the count.
- **World** (`--stress-world=W`, key `world`): the field is `W` x `W` views. `count` stays the count of the whole field, so about `count / W^2` sprites are on screen. The engine has no 2D camera, so the app emulates one: `CameraOffset` goes to and fro over the field and each frame every `_x` / `_y` is the sprite's place in the field minus that offset. The shift is therefore part of `updateMs`. A world of one view writes no position, so the plain run pays nothing for the load.
