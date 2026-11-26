"""Step 5 to 6: `enabled` on a scene component means "the component sleeps". Up to version 5 the engine ignored the attribute there (the component ran, and a Save dropped it).

    <component type="QuadRendererComponent" enabled="false"/>
becomes
    <component type="QuadRendererComponent"/>

It matches a `component` in the `components` of a `node` (an inline node, a node of `<children>`, a node file, an override of a `<node ref>`) whose `enabled` is not exactly
`true` or `1`, which is what the loader reads as false. On a component of the engine (`ENGINE_COMPONENTS`) the attribute is removed, so the component goes on running. A true value
stays. The components of a UI `element` are not matched: there `enabled="false"` has always dropped the component at load.

Refused, with the reason (the file stays as it was): such an attribute on any other type. The class is the game's, and it may have a field of its own serialized as `enabled`, which
the step cannot see.
"""

from migrate import step

# The classes of the engine that a node could hold when the attribute got its meaning; none had a field serialized as `enabled`. A class added later needs no row: no file of
# version 5 means anything by its name.
ENGINE_COMPONENTS = frozenset(
    {
        "AudioSourceComponent",
        "ChangeToRandomColorDebugComponent",
        "ClickableComponent",
        "ClipRectComponent",
        "ParticleEmitterComponent",
        "PointerTargetComponent",
        "QuadRendererComponent",
        "RenderProbeComponent",
        "RmlUiDocumentComponent",
        "SpriteRendererComponent",
        "TextComponent",
        "TransformComponent",
        "UICanvasComponent",
    }
)
TRUE_VALUES = ("true", "1")


def _is_a_sleeping_scene_component(element) -> bool:
    components = element.parent
    return (
        element.name == "component"
        and components is not None
        and components.name == "components"
        and components.parent is not None
        and components.parent.name == "node"
        and element.has("enabled")
        and element.get("enabled") not in TRUE_VALUES
    )


@step(5, "scene-component-enabled")
def run(doc, ctx):
    for component in doc.find_all(_is_a_sleeping_scene_component):
        kind = component.get("type", "")
        written = f'enabled="{component.get("enabled")}"'
        if kind in ENGINE_COMPONENTS:
            component.remove("enabled")
            ctx.report(component, f"{kind}: removed {written}, which the engine ignored")
        else:
            ctx.fail(
                component,
                f'component "{kind}" has {written}: the engine ignored it, from version 6 it puts the component to sleep, and the step cannot know whether this class has a field of '
                "its own called `enabled`. If it has one, rename the field in the code and the attribute here. If not, delete the attribute (the component goes on running as before); "
                "to have it sleep, add it again after the migration",
            )
