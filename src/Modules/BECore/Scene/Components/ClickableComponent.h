#pragma once

#include <BECore/Input/HitClip.h>
#include <BECore/Input/UIInput.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/IComponent.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief A rect of the node's TransformComponent that emits InputEvents::ActionEvent(_actionName) when it is pressed.
     *
     * It is a target of UIInput at its place in the draw order (Scene::GetInputOrder()): a canvas drawn above it takes the press first,
     * and it takes the press before a canvas or a component below it. The press is consumed, so game code gets no raw mouse event for it.
     * The rect is not rotated. A hidden node, a scene that is not active, a point outside the renderer's clip at its place and an empty
     * action take nothing.
     */
    class ClickableComponent : public IComponent, public IUIInputHandler {
        BE_CLASS(ClickableComponent, ICON = mouse - pointer - click)
    public:
        ClickableComponent() = default;
        ~ClickableComponent() override;

        BE_REFLECT_FIELD PoolString _actionName;

        void OnAttached() override;
        void OnDraw() override;

        [[nodiscard]] bool ContainsPoint(float outputX, float outputY) const override;
        bool OnMouseButtonDown(const SDL_MouseButtonEvent& event) override;

    private:
        void RefreshInputOrder();

        HitClip _clip;            // the renderer's clip at this place of the draw order, kept for the pointer test
        uint32_t _drawnPass = 0;  // Scene::GetDrawPass() of the last pass that drew this component
        int32_t _inputOrder = 0;
        bool _registered = false;
    };

}  // namespace BECore
