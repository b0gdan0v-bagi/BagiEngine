#pragma once

#include <BECore/Input/HitClip.h>
#include <BECore/Input/PointerEvent.h>
#include <BECore/Input/PointerShape.h>
#include <BECore/Input/PointerTracker.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief The base of a game's pointer targets: a shape of a node that takes the pointer at its place in the draw order.
     *
     * It is an IUIInputHandler registered with Scene::GetInputOrder(), so a canvas drawn above it takes the pointer first and it takes
     * it before everything drawn below, and a hidden node, an inactive scene and a point outside the renderer's clip at its place take
     * nothing. A derived class overrides the OnPointer* callbacks; there is no list of presses to keep: the press a callback took is
     * held by UIInput, and the base takes the release and the drag of that press only.
     *
     * Shape in scene units (SetShape): the rect of the node's TransformComponent by default, an explicit rect, a circle or an ellipse,
     * or ContainsScenePoint overridden for a shape of the game's own.
     *
     * OnAttached(), OnDraw() and OnEnabledChanged() are final so a derived class cannot forget the base's work and lose its input
     * silently; it overrides OnTargetAttached(), OnTargetDraw() and OnTargetEnabledChanged(). A callback may destroy the component (a scene switch): the base touches no member after a
     * callback that could be followed by another call, and a derived class copies what it needs before the call that may destroy it.
     * A target that sleeps (IComponent::IsEnabled()) or has no scene (removed and still held, IComponent::TryGetScene()) stays
     * registered and gets no callback, also not the exit or the release of what it had begun.
     *
     * The base is concrete (the generator registers every reflected class of a factory in AbstractFactory, which needs one). A bare
     * PointerTargetComponent in a scene file takes the motion over its shape and no press or wheel; a derived class says what else.
     *
     * The release and the motion of a pointer the game holds, a release of another button or pointer, and a motion while a button of
     * another handler is held are not taken: they reach the game or the handler below.
     */
    class PointerTargetComponent : public IComponent, public IUIInputHandler {
        BE_CLASS(PointerTargetComponent, ICON = mouse - pointer - click, CATEGORY = logic)
    public:
        PointerTargetComponent() = default;
        ~PointerTargetComponent() override;

        void OnAttached() final;
        void OnDraw() final;

        void SetShape(const PointerShape& shape) {
            _shape = shape;
        }

        [[nodiscard]] const PointerShape& GetShape() const {
            return _shape;
        }

        [[nodiscard]] bool ContainsPoint(float outputX, float outputY) const final;
        bool OnMouseButtonDown(const SDL_MouseButtonEvent& event) final;
        bool OnMouseButtonUp(const SDL_MouseButtonEvent& event) final;
        bool OnMouseMotion(const SDL_MouseMotionEvent& event) final;
        bool OnMouseWheel(const SDL_MouseWheelEvent& event) final;
        void OnPointerLeave(uint32_t which) final;

    protected:
        /// Node-dependent setup of the derived class; the base has registered the target before it
        virtual void OnTargetAttached() {}

        /// Draw work of the derived class; the base has stamped the pass, kept the clip and refreshed the order before it
        virtual void OnTargetDraw() {}

        /// What the derived class does when the target is disabled or enabled again; the base has forgotten the pointers over it
        virtual void OnTargetEnabledChanged() {}

        /// Whether a point in scene units is on the target, after the scene, the draw pass and the clip said yes. The default is the
        /// shape; a node without a TransformComponent has one only when the shape names its own rect
        [[nodiscard]] virtual bool ContainsScenePoint(float sceneX, float sceneY) const;

        /// @return true to take the press: this target then holds it, and gets OnPointerDrag and OnPointerRelease for it. The default takes none
        virtual bool OnPointerPress(const PointerEvent& /*event*/) {
            return false;
        }

        /// The release of a press this target took, wherever the pointer is
        virtual void OnPointerRelease(const PointerEvent& /*event*/) {}

        /// The first motion of a pointer that is on the shape and was not over the target, before OnPointerMove or OnPointerDrag of it
        virtual void OnPointerEnter(const PointerEvent& /*event*/) {}

        /// The pointer that this target took left it, or the target no longer takes its motion. Its position is the last one over the target
        virtual void OnPointerExit(const PointerEvent& /*event*/) {}

        /// A motion over the shape of a pointer that holds no press of this target
        /// @return true to take it: what is drawn below gets no hover there (the default). A target that takes none of the motion
        /// answers false from ContainsScenePoint instead: a declined motion still gets OnPointerEnter and OnPointerExit after it
        virtual bool OnPointerMove(const PointerEvent& /*event*/) {
            return true;
        }

        /// A motion of a pointer that holds a press of this target, wherever it is; sceneDx / sceneDy are the step
        virtual void OnPointerDrag(const PointerEvent& /*event*/) {}

        /// @return true to take the wheel. The default takes none
        virtual bool OnPointerWheel(const PointerEvent& /*event*/) {
            return false;
        }

    private:
        class CallbackGuard;
        friend class CallbackGuard;

        /// A pointer this target took the motion of and has not told the exit of
        struct OverPointer {
            uint32_t which = 0;
            float outputX = 0.0f;
            float outputY = 0.0f;
        };

        void OnEnabledChanged() final;
        void RefreshInputOrder();
        /// @return true for a target that is disabled or has no scene, which forgets the pointers over it and calls nothing
        [[nodiscard]] bool ForgetIfDeaf();
        [[nodiscard]] Optional<Rect> GetNodeRect() const;
        [[nodiscard]] PointerEvent MakeEvent(uint32_t which, uint8_t button, uint8_t clicks, float outputX, float outputY) const;
        /// @return true when the pointer was not over the target before
        bool NoteOver(const PointerEvent& event);
        void ForgetOver(uint32_t which);

        PointerShape _shape;
        HitClip _clip;            // the renderer's clip at this place of the draw order, kept for the pointer test
        uint32_t _drawnPass = 0;  // Scene::GetDrawPass() of the last pass that drew this component
        int32_t _inputOrder = 0;
        bool _registered = false;
        FixedVector<OverPointer, PointerTracker::kMaxEntries> _over;  // a further pointer is not tracked: it gets no enter and no exit
        CallbackGuard* _guard = nullptr;                              // the innermost callback in progress, to learn that it destroyed the component
    };

}  // namespace BECore
