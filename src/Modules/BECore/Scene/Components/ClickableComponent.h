#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/Components/PointerTargetComponent.h>

namespace BECore {

    /**
     * @brief A rect of the node's TransformComponent that emits InputEvents::ActionEvent(_actionName) when it is pressed.
     *
     * A PointerTargetComponent at its place in the draw order (Scene::GetInputOrder()): a canvas drawn above it takes the press first,
     * and it takes the press before a canvas or a component below it. The press, its release and the motion over the rect are consumed,
     * so game code gets no raw down there and what is drawn below does not light up under it. While it holds its own press it takes every
     * motion of that pointer; the release of another button or pointer and a motion of another pointer pass over it, as does a motion
     * while a button of the game is held.
     * The rect is not rotated. A hidden node, a scene that is not active, a point outside the renderer's clip at its place and an empty
     * action take nothing.
     */
    class ClickableComponent : public PointerTargetComponent {
        BE_CLASS(ClickableComponent, ICON = mouse - pointer - click)
    public:
        ClickableComponent() = default;
        ~ClickableComponent() override = default;

        BE_REFLECT_FIELD PoolString _actionName;

    protected:
        void OnTargetAttached() override;
        [[nodiscard]] bool ContainsScenePoint(float sceneX, float sceneY) const override;
        bool OnPointerPress(const PointerEvent& event) override;
    };

}  // namespace BECore
