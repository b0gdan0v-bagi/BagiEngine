# Particles

A 2D particle effect in three layers; this folder holds the first two.

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
