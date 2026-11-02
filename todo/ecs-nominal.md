---
stage: 4
id: ecs-is-nominal-entt-only-hands-out-entity-ids
---
# ECS is nominal: EnTT only hands out entity ids

**Where:** `src/Modules/BECore/Scene/Scene.h` (`entt::registry _registry`), `SceneNode.cpp` (`_entity = scene->GetRegistry().create()`), `Scene/IComponent.h`, `Scene/CLAUDE.md`, `CLAUDE.md` ("modular ECS architecture").

**What is wrong** (found by reading the code):

1. The registry holds one empty entity per `SceneNode`. Components are polymorphic `IComponent` objects stored in the node; there are no registry views, systems or component storage. The architecture description in `CLAUDE.md` promises more than the code does.
2. Rendering is a per-component `OnDraw()` call from `Scene::Draw()`. The pass sorts nodes by layer, but a component still issues its `IRenderer` calls itself, so there is nowhere to batch draws.

**A fix has to cover** (a decision of the project owner, the recommendation is below):

- recommendation: keep the object model (`SceneNode` + `IComponent` fits reflection and the inspector) and make rendering a system that collects `TransformComponent` + renderer data from the registry, sorts by layer (the sorted node pass of `Scene::Draw()` is the first step) and hands a batch to `IRenderer`. This gives EnTT a real job and prepares the entries [2D rendering](rendering-2d.md) and [Renderer backends](renderer-backends.md);
- the alternative is to say plainly that this is a scene graph in the Unity style and drop EnTT if ids are all it provides;
- not a rewrite to pure ECS;
- record the decision in `Scene/CLAUDE.md` and correct the wording in `CLAUDE.md`.
