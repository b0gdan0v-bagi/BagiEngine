#pragma once

#include <BECore/Input/HitClip.h>
#include <BECore/Input/PointerTracker.h>
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
     * and it takes the press before a canvas or a component below it. The press, its release and the motion over the rect are consumed,
     * so game code gets no raw down there and what is drawn below does not light up under it. A motion or a release that belongs to a press
     * of the game (another button or pointer than the ones this component holds) passes over it. While it holds its own press it takes every
     * motion of that pointer.
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
        bool OnMouseButtonUp(const SDL_MouseButtonEvent& event) override;
        bool OnMouseMotion(const SDL_MouseMotionEvent& event) override;

    private:
        /// A press this component took, as UIInput keys its capture: the release of this pair is the one to take
        struct HeldPress {
            uint32_t which = 0;
            uint8_t button = 0;
        };
        using HeldPresses = FixedVector<HeldPress, PointerTracker::kMaxEntries>;  // as many as UIInput holds, so it never allocates

        void RefreshInputOrder();
        [[nodiscard]] HeldPresses::iterator FindHeldPress(uint32_t which, uint8_t button);
        [[nodiscard]] bool HoldsPressOf(uint32_t which) const;

        HitClip _clip;            // the renderer's clip at this place of the draw order, kept for the pointer test
        uint32_t _drawnPass = 0;  // Scene::GetDrawPass() of the last pass that drew this component
        int32_t _inputOrder = 0;
        bool _registered = false;
        HeldPresses _heldPresses;  // the presses it took and not yet released; UIInput delivers each release as the capture's holder
    };

}  // namespace BECore
