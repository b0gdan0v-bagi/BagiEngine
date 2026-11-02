---
stage: 3
id: 2d-rendering-is-minimal-no-rotation-camera-or-blending
---
# 2D rendering is minimal: no rotation, camera or blending

**Where:** `src/Modules/BECore/Renderer/IRenderer.h` (`DrawFilledRect`, `DrawTexture` are the whole drawing surface), `SDLRendererBackend.cpp`, `Vulkan/src/CoreVulkan/VulkanRendererBackend.cpp`, `Scene/Components/TransformComponent.h` (position and size in scene units, which are output pixels unless the scene sets a view size).

**What is missing** (found by reading the code, not by running a game):

1. Layers exist, but only per node: `SceneNode::GetLayer()` plus the scene order sort the draw pass (`Scene/CLAUDE.md`, "Draw order"). A child does not inherit its parent's layer, and there is no sort key finer than the node (a sprite and a quad on one node cannot interleave with other nodes).
2. No rotation or scale of a sprite, no flip. (A tint with alpha, `IRenderer::PushClipRect()` and `CreateTexture()` exist: `rendering-module.md`, "Drawing primitives".)
3. No movable 2D camera: a scene can fit a fixed view size to the output (`viewWidth` / `viewHeight`, `Scene/CLAUDE.md`, "View size"), but scrolling and zoom need a camera transform on top of that.
4. One blend mode only: straight alpha over the target. No additive or multiply.
5. A node has only absolute pixel coordinates, no parent-relative transform.

**A fix has to cover:**

- each capability first in `IRenderer` / `TransformComponent`, then in both backends (`.claude/rules/rendering-module.md`; a backend that cannot do it documents the fallback);
- layer inheritance from the parent, if a hierarchy of drawn nodes needs it;
- a camera that maps world to screen, used by the renderer components;
- a decision on the backends before the work starts (see [the entry on renderer backends](renderer-backends.md)).
