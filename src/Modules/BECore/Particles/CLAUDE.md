# Particles

A 2D particle effect in three layers; this folder holds the first two (`ParticleEmitterComponent`, in `Scene/Components/`, is the third).

| Layer | Where | Knows |
|-------|-------|-------|
| `ParticleEffect` | `Particles/ParticleEffect.{h,cpp}` | reflected data only: what to emit and how it looks |
| `ParticleSimulation` | `Particles/` | plain C++ over an effect; no `CoreManager`, no scene, no renderer |
| `ParticleEmitterComponent` | `Scene/Components/` | glue: game time, the node's rect, the texture, `OnDraw()` |

## The effect (`ParticleEffect`)

Reflected like `SpriteEntry`: the field names have no underscore, so they are the attribute names of the file. `BE_CLASS` gives the struct and its helpers a vptr, so none is an aggregate (`ParticleCurve` has a constructor for that). Helpers: `ParticleCurve` (`from`, `to`, `ease` of a float), `ParticleColorCurve` (the same for a `Color`) and `ParticleFlipbook` (a grid of frames in the sprite, "The simulation"), all written as child elements; the enums `ParticleShape` (`Point`, `Rect`, `Circle`) and `ParticleFrameClass` (`Interactive`, `FiniteEffect`, `DecorativeLoop`) are attributes. The defaults are a usable small burst: a one-shot of 24 white squares flying out in all directions and fading in about half a second.

- **Units:** scene units and seconds; angles in degrees, 0 is right and positive is clockwise (the direction of `TransformComponent::_rotation`, y points down). `spread` is the full width of the cone around `direction`, 360 is all around.
- **Sprite:** `spriteId` (a `TextureLibrary` entry) wins over `texturePath` (the whole texture); neither draws plain squares.
- **`frameClass`** is the whole contract with the frame loop of the render-on-demand entry: a looping effect that only decorates says `DecorativeLoop`.

### Sanitizing

`ParticleEffect::Sanitize()` / `Sanitized()` bring any values into the ranges of `ParticleLimits`: a float that is not finite becomes the field's default, the others are clamped; a min above its max swaps with it; `maxParticles` is `1..kMaxParticles`, `burstCount` does not exceed it, rates and counts are not negative; lifetimes are at least `kMinLifetime` (so `age / lifetime` is finite); an enum that is no name (a cast of a bad number) becomes its default; a flipbook has `columns` and `rows` in `1..kMaxFlipbookAxis`, `frames` in `0..columns * rows` and `fps` in `0..kMaxFlipbookFps`. The simulation runs on a sanitized copy and the effect keeps what was written, so a typo in a file or in the inspector never loops or allocates without bound and the inspector still shows the typo. A bad enum *text* in a file is a reader error and the field keeps its default.

### File shape

```xml
<effect looping="false" burstCount="24" maxParticles="256" seed="1" lifetimeMin="0.4" lifetimeMax="0.8"
        shape="Circle" shapeRadius="6" speedMin="40" speedMax="120" direction="270" spread="60"
        gravityY="200" sizeJitter="0.25" spriteId="Spark" blend="Add" frameClass="FiniteEffect">
  <size from="12" to="2" ease="QuadOut"/>
  <rotation from="0" to="180"/>
  <color ease="Linear">
    <from r="255" g="200" b="50" a="255"/>
    <to r="200" g="20" b="10" a="0"/>
  </color>
</effect>
```

Every attribute and element is optional. A flipbook is one more child element (the attributes are its fields, the defaults are off):

```xml
<flipbook columns="4" rows="2" frames="7" mode="Loop" fps="24" randomStart="true"/>
```

Tests: `tests/BECore/ParticleEffectTest.cpp`.

## The simulation (`ParticleSimulation`)

Plain C++ over `ParticleEffect`: `Update(effect, area, dt)`, `Burst(effect, center, count, tint)`, `Play` / `Stop` / `Clear`, and `BuildGeometry` (fills caller-owned `GeometryVertex` / index vectors, one rotated square per live particle, returns the quads). `BuildGeometry` is `PrepareGeometry` (serial: sanitizes the effect, sizes the vertices, writes the index pattern, returns a `GeometryFrame`) plus `BuildQuads(frame, begin, end, vertices)`, a `const` function of one particle's data and the frame that writes only `vertices[4 * begin, 4 * end)`: no member, lock or allocation, so disjoint ranges may run on several threads and the bytes do not depend on the split (any order, any number of ranges). The simulation stays free of `TaskSystem`; the component forks. Emitter time (`GetTime()`) is the sum of the `dt` given, in double; a `dt` that is not finite or is negative advances nothing.

- **A position is a function of time, not an integration.** A particle stores its spawn time and what was drawn at birth (origin, velocity, lifetime, jitters, tint); at emitter time `t`, `age = t - spawnTime`, position = `origin + velocity * age + gravity * age^2 / 2`, and size, rotation and color are the effect's curves of `age / lifetime` (`Ease`, `Lerp`). So a lower or uneven frame rate gives the same picture at the same game time, and a burst that arrives in a long frame is not late. Gravity and the curves are read from the effect at draw time, so an inspector edit changes the live particles too. No drag, no collisions: they would need integration.
- **Looping emission is a schedule.** Particle `n` (from 1) is born at `n / rate` of the time since `Play()`; an update spawns every `n` that is due, spawn time backdated, so a 0.25 s frame loses none. A rate edited while playing moves the schedule: nothing is spawned until the new count catches up. A one-shot effect fires `burstCount` at `Play()`: it is fired by the next `Update`, which knows the effect and the area, with the spawn time of `Play()`, and then `IsEmitting()` is false. A particle already dead on arrival (frame longer than its life) is neither spawned nor dropped.
- **Deterministic by seed.** `ParticleRandom.h`: a stateless hash of (seed, serial, stream) (splitmix64 finalizer, no `std::uniform_*` or `rand`), one stream per quantity so lifetime, angle, speed, jitters and the place in the shape do not correlate. A looping particle's serial is its number, a burst particle's counts the burst particles since `Play()` with the top bit set, so the order and the number of updates change nothing and a fixed seed gives fixed positions on every platform (to float precision). The only clock is the sum of `SceneUpdateEvent::deltaTime` (game time; a held frame has 0), so `--manual-clock` and a replay see the same picture.
- **The pool.** At most `maxParticles` live (reserved by `Reserve`, or at the first `Update`, and again only when the field grows); a particle that does not fit is dropped and counted (`GetDroppedCount()`), never grows the pool, and a hostile count of a burst or a due number of a loop is counted in one step, not looped. Dead particles are removed by a stable compaction, only in an update where one died (the earliest death is remembered), so spawn order is draw order, older first. `BuildGeometry` and `Update` allocate nothing once `Reserve` and `ReserveGeometry` have run, with or without live particles.
- **World space.** A particle keeps the place it was born at. `Point` and `Circle` use the center of the `area` of the update, `Rect` fills it; `Burst` takes the center from the call.
- **`NeedsFrames()`** is what the frame loop will ask (render-on-demand): true from `Play()` or a burst until the last particle died, for a looping emitter while it plays. The activity token itself and the cap of a `DecorativeLoop` belong to that entry.
- **Flipbook.** `flipbook` cuts the sprite's source rect (the whole texture when there is none) into `columns x rows` equal cells, counted row by row from the top left; the first `frames` of them (0 is all) are the frames, and the default 1 x 1 is off: the whole rect, the UVs of a plain sprite. The frame is a function of the age too: `OverLife` shows every frame once over the life (`floor(progress * count)`), `Loop` advances `fps` frames a second and wraps; `randomStart` shifts it by `floor(frameSeed * count)`, the seed being one more `ParticleRandom` stream (`StartFrame`, last in the enum so no other stream moves) that is drawn at birth whether or not `randomStart` is set. The offset is made at draw time, so an edit of `frames` keeps every particle valid. Still one `DrawGeometry` per emitter: only the UVs of each quad change (`ParticleSimulation::FrameOf`). **Trap:** with linear filtering the edge of a cell mixes in its neighbour, so a strip needs padding between frames or a sampler without filtering; the engine does not pad. Two frames are never blended (the 2D path has no shader for two samples).
- **Tint:** the tint of a `Burst` multiplies the whole color curve of the particles of that burst and no other.
- **Not there:** drag, collisions, sub-emitters, local space (particles that follow a moving node), trails. Tests: `tests/BECore/ParticleSimulationTest.cpp`.


## The component (`ParticleEmitterComponent`)

`<component type="ParticleEmitterComponent">` with the reflected `effect` (a child element, the file shape above), `playOnStart` (default true) and `burstAction`. Add it to a node that has a `TransformComponent`; the node's rect is the emission area.

```xml
<node name="Sparks"><components>
  <component type="TransformComponent" x="120" y="80" width="0" height="0"/>
  <component type="ParticleEmitterComponent" playOnStart="false" burstAction="Cleared">
    <effect burstCount="12" lifetimeMin="0.3" lifetimeMax="0.6" blend="Add"><size from="10" to="2"/></effect>
  </component>
</components></node>
```

- **Time:** it updates on `SceneUpdateEvent` (`SubscribeInScene`, so a scene that is not active does nothing and draws nothing) by `deltaTime`; a held frame has 0 and moves nothing. `playOnStart` calls `Play()` when the component is attached; a one-shot effect fires at the first update.
- **Code:** `Play`, `Stop`, `Clear`, `Burst(x, y, count, tint)` (scene units; `count` -1 is the effect's `burstCount`, `tint` multiplies the color of these particles only), `IsEmitting`, `GetLiveCount`, `NeedsFrames`, `GetSimulation`. **Action:** an `InputEvents::ActionEvent` named `burstAction` bursts `burstCount` at the center of the node's rect; empty is none.
- **Edits take effect at once:** the effect is read on every update and at draw time, so an inspector edit needs no restart; a larger `maxParticles` reserves the pool and the geometry vectors again at the next update (the only allocation after the first).
- **Sprite:** `spriteId` resolves through `TextureLibrary` (the part of the texture is its `srcRect`), else `texturePath` is the whole texture. An unknown sprite is one `LOG_ERROR` and plain squares. A change of either field loads the texture again. `SetTexture()` hands it a texture made from pixels (it lasts until the sprite or path changes).
- **Draw:** `OnDraw()` makes one `DrawGeometry` call for all live particles, none when there are none, under `ScopedBlendMode(effect.blend)`; it builds the vertices through `TaskManager::ParallelFor` (`TaskSystem/CLAUDE.md`, "ParallelFor") from `SetParallelMinParticles` live particles up (0 never; the default comes from the benchmark `particles-parallel` of `.claude/rules/perf-project.md`: below it a fork-join costs more than it saves) and inline below it, before `TaskManager` exists and in a single-thread build, with the same bytes as the serial path (a test compares them). **One fork-join per emitter at draw time**, not a scene-wide pass and not a build between update and draw: another handler can `Burst` / `Clear` the emitter between its `OnUpdate` and its `OnDraw`, which would race a build already running, and the texture size is settled only at draw. `Update` and the stable compaction stay serial: they are O(spawned + died) and order-bound. Many small emitters stay under the threshold, serial. Vulkan shows `Add` as straight alpha (`.claude/rules/rendering-module.md`). The texture is a GPU object and is let go at `ApplicationCleanUp`, like `RenderProbeComponent`. Tests: `tests/BECore/ParticleEmitterComponentTest.cpp`.

### The example effect

Sparks: a square of 20 with the color (100, 100, 100) that flies right at 100 a second under a gravity of 80, drawn from the right half of a sprite and added to what is behind it. Every range has min = max and the cone has no width, so the burst is one exact path and `tests/BECore/ParticleCaptureTest.cpp` reads its pixels back on the headless SDL renderer (`Scene::Draw()`, the fake clock): the position from the closed form, two `Add` particles brighter than one, the same picture from 16 steps or 2 steps of the same game time, the background again when the effect ended. The test holds this text as a constant and checks that this file still quotes it.

```xml
<effect burstCount="1" maxParticles="16" lifetimeMin="1" lifetimeMax="1" speedMin="100" speedMax="100"
        direction="0" spread="0" gravityY="80" spriteId="ParticleCaptureSpark" blend="Add" frameClass="FiniteEffect">
  <size from="20" to="20"/>
  <color>
    <from r="100" g="100" b="100" a="255"/>
    <to r="100" g="100" b="100" a="255"/>
  </color>
</effect>
```
