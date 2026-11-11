# Math

Small value types, header-only except `Color`: `Vec2`, `Vec3`, `Vec4`, `Mat4`, `Quat`, `Rect`, `Color`. `Vec2` and `Rect` are the 2D scene's; `Vec3`, `Vec4`, `Mat4` and `Quat` are the 3D renderer's (`Renderer/Render3D/`, `.claude/rules/rendering-module.md`, "Generic GPU interface and 3D"). Every type has the `Serialize` / `Deserialize` / `VisitProperties` templates.

## 3D conventions

One set for the scene, `Renderer3D` and every backend, so no matrix has a variant per backend. A test pins each by numbers (`tests/BECore/Mat4Test.cpp`).

- `float`, **right-handed**, **Y up** in the world; the camera looks down **-Z** in view space. `Cross(X, Y) == Z`.
- **Angles are radians** in the math types. The scene's degrees stay in the scene (and 2D `DrawTransform::rotation` is degrees, clockwise on screen: a different thing).
- `Mat4` is **column-major in memory** (`Data()[12..14]` are the translation), **column vectors**, `Mat4 * Vec4`; `a * b` applies `b` first (`Translation * Rotation` rotates, then moves). The bytes go to a constant buffer as they are; a Slang shader declares `float4x4` with its default column-major storage and uses `mul(M, v)` (checked, rule file).
- **Clip space:** depth **0 to 1** (Vulkan, Metal, D3D), Y **up** as in Metal and D3D. The Vulkan backend flips with a negative-height viewport; the matrices stay the same. `Perspective()` maps `near` to depth 0 and `far` to 1; `Orthographic()` takes distances along -Z.
- `Mat4::Inverse()` is `std::optional` (`nullopt` when singular), `Normalized()` of a zero vector is zero (no NaN).
- `Quat` is `{x, y, z, w}` with `w` the scalar part, **unit**, Hamilton product, `a * b` applies `b` first like `Mat4`. `q` and `-q` are one rotation but not `==`; `Slerp` takes the short way. There is no Euler constructor: the order of three angles is the scene's decision (`scene-3d`).
- Colors in the 3D constants are linear floats; `Color` stays sRGB bytes (`SrgbToLinear()`, `Renderer/Render3D/Renderer3DConstants.h`).
