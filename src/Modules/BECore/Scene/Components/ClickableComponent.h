#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/Components/PointerTargetComponent.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /// The area of a ClickableComponent inside the node's rect: the rect itself, or the ellipse inscribed in it (a circle for a square)
    CORE_ENUM(ClickableShape, uint8_t, Rect, Ellipse)

    /**
     * @brief A rect of the node's TransformComponent that emits InputEvents::ActionEvent(_actionName) when it is pressed.
     *
     * A PointerTargetComponent at its place in the draw order (Scene::GetInputOrder()): a canvas drawn above it takes the press first,
     * and it takes the press before a canvas or a component below it. The press, its release and the motion over the rect are consumed,
     * so game code gets no raw down there and what is drawn below does not light up under it. While it holds its own press it takes every
     * motion of that pointer; the release of another button or pointer and a motion of another pointer pass over it, as does a motion
     * while a button of the game is held.
     * The rect is not rotated. A hidden node, a scene that is not active, a point outside the renderer's clip at its place and an empty
     * action take nothing, unless _blocksInput is set.
     */
    class ClickableComponent : public PointerTargetComponent {
        BE_CLASS(ClickableComponent, ICON = mouse - pointer - click)
    public:
        ClickableComponent() = default;
        ~ClickableComponent() override = default;

        BE_REFLECT_FIELD PoolString _actionName;
        BE_REFLECT_FIELD ClickableShape _shape = ClickableShape::Rect;
        /// Takes the press, its release and the motion over the shape even with no action, and emits nothing: a shield for what is drawn below
        BE_REFLECT_FIELD bool _blocksInput = false;

    protected:
        void OnTargetAttached() override;
        [[nodiscard]] bool ContainsScenePoint(float sceneX, float sceneY) const override;
        bool OnPointerPress(const PointerEvent& event) override;
    };

}  // namespace BECore
