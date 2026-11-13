# Particles

A 2D particle effect in three layers; this folder holds the first two (`ParticleEmitterComponent` is the third).

| Layer | Where | Knows |
|-------|-------|-------|
| `ParticleEffect` | `Particles/ParticleEffect.{h,cpp}` | reflected data only: what to emit and how it looks |
| `ParticleSimulation` | `Particles/` | plain C++ over an effect; no `CoreManager`, no scene, no renderer |
| `ParticleEmitterComponent` | `Scene/Components/` | glue: game time, the node's rect, the texture, `OnDraw()` |

## The effect (`ParticleEffect`)

Reflected like `SpriteEntry`: the field names have no underscore, so they are the attribute names of the file. `BE_CLASS` gives the struct and its helpers a vptr, so none is an aggregate (`ParticleCurve` has a constructor for that). Helpers: `ParticleCurve` (`from`, `to`, `ease` of a float) and `ParticleColorCurve` (the same for a `Color`), both written as child elements; the enums `ParticleShape` (`Point`, `Rect`, `Circle`) and `ParticleFrameClass` (`Interactive`, `FiniteEffect`, `DecorativeLoop`) are attributes. The defaults are a usable small burst: a one-shot of 24 white squares flying out in all directions and fading in about half a second.

- **Units:** scene units and seconds; angles in degrees, 0 is right and positive is clockwise (the direction of `TransformComponent::_rotation`, y points down). `spread` is the full width of the cone around `direction`, 360 is all around.
- **Sprite:** `spriteId` (a `TextureLibrary` entry) wins over `texturePath` (the whole texture); neither draws plain squares.
- **`frameClass`** is the whole contract with the frame loop of the render-on-demand entry: a looping effect that only decorates says `DecorativeLoop`.

### Sanitizing

`ParticleEffect::Sanitize()` / `Sanitized()` bring any values into the ranges of `ParticleLimits`: a float that is not finite becomes the field's default, the others are clamped; a min above its max swaps with it; `maxParticles` is `1..kMaxParticles`, `burstCount` does not exceed it, rates and counts are not negative; lifetimes are at least `kMinLifetime` (so `age / lifetime` is finite); an enum that is no name (a cast of a bad number) becomes its default. The simulation runs on a sanitized copy and the effect keeps what was written, so a typo in a file or in the inspector never loops or allocates without bound and the inspector still shows the typo. A bad enum *text* in a file is a reader error and the field keeps its default.

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

Every attribute and element is optional. Tests: `tests/BECore/ParticleEffectTest.cpp`.

## The simulation (`ParticleSimulation`)

Plain C++ over `ParticleEffect`: `Update(effect, area, dt)`, `Burst(effect, center, count, tint)`, `Play` / `Stop` / `Clear`, and `BuildGeometry` (fills caller-owned `GeometryVertex` / index vectors, one rotated square per live particle, returns the quads). Emitter time (`GetTime()`) is the sum of the `dt` given, in double; a `dt` that is not finite or is negative advances nothing.

- **A position is a function of time, not an integration.** A particle stores its spawn time and what was drawn at birth (origin, velocity, lifetime, jitters, tint); at emitter time `t`, `age = t - spawnTime`, position = `origin + velocity * age + gravity * age^2 / 2`, and size, rotation and color are the effect's curves of `age / lifetime` (`Ease`, `Lerp`). So a lower or uneven frame rate gives the same picture at the same game time, and a burst that arrives in a long frame is not late. Gravity and the curves are read from the effect at draw time, so an inspector edit changes the live particles too. No drag, no collisions: they would need integration.
- **Looping emission is a schedule.** Particle `n` (from 1) is born at `n / rate` of the time since `Play()`; an update spawns every `n` that is due, spawn time backdated, so a 0.25 s frame loses none. A rate edited while playing moves the schedule: nothing is spawned until the new count catches up. A one-shot effect fires `burstCount` at `Play()`: it is fired by the next `Update`, which knows the effect and the area, with the spawn time of `Play()`, and then `IsEmitting()` is false. A particle already dead on arrival (frame longer than its life) is neither spawned nor dropped.
- **Deterministic by seed.** `ParticleRandom.h`: a stateless hash of (seed, serial, stream) (splitmix64 finalizer, no `std::uniform_*` or `rand`), one stream per quantity so lifetime, angle, speed, jitters and the place in the shape do not correlate. A looping particle's serial is its number, a burst particle's counts the burst particles since `Play()` with the top bit set, so the order and the number of updates change nothing and a fixed seed gives fixed positions on every platform (to float precision). The only clock is the sum of `SceneUpdateEvent::deltaTime` (game time; a held frame has 0), so `--manual-clock` and a replay see the same picture.
- **The pool.** At most `maxParticles` live (reserved by `Reserve`, or at the first `Update`, and again only when the field grows); a particle that does not fit is dropped and counted (`GetDroppedCount()`), never grows the pool, and a hostile count of a burst or a due number of a loop is counted in one step, not looped. Dead particles are removed by a stable compaction, only in an update where one died (the earliest death is remembered), so spawn order is draw order, older first. `BuildGeometry` and `Update` allocate nothing once `Reserve` and `ReserveGeometry` have run, with or without live particles.
- **World space.** A particle keeps the place it was born at. `Point` and `Circle` use the center of the `area` of the update, `Rect` fills it; `Burst` takes the center from the call.
- **`NeedsFrames()`** is what the frame loop will ask (render-on-demand): true from `Play()` or a burst until the last particle died, for a looping emitter while it plays. The activity token itself and the cap of a `DecorativeLoop` belong to that entry.
- **Tint:** the tint of a `Burst` multiplies the whole color curve of the particles of that burst and no other.
- **Not there:** drag, collisions, sub-emitters, local space (particles that follow a moving node), trails. Tests: `tests/BECore/ParticleSimulationTest.cpp`.
