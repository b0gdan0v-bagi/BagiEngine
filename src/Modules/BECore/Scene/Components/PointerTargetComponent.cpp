#include "PointerTargetComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/PointerId.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <EASTL/algorithm.h>
#include <Generated/PointerTargetComponent.gen.hpp>

namespace BECore {

    /// Tells a sequence of callbacks that one of them destroyed the component, so it stops before it touches a member.
    /// The guards form a stack: a callback may dispatch again, and the component's destructor marks every one in progress.
    class PointerTargetComponent::CallbackGuard {
    public:
        explicit CallbackGuard(PointerTargetComponent& target) : _target(&target), _outer(target._guard) {
            target._guard = this;
        }

        ~CallbackGuard() {
            if (!_destroyed) {
                _target->_guard = _outer;
            }
        }

        CallbackGuard(const CallbackGuard&) = delete;
        CallbackGuard& operator=(const CallbackGuard&) = delete;

        [[nodiscard]] bool Destroyed() const {
            return _destroyed;
        }

        static void MarkDestroyed(CallbackGuard* innermost) {
            for (CallbackGuard* guard = innermost; guard != nullptr; guard = guard->_outer) {
                guard->_destroyed = true;
            }
        }

    private:
        PointerTargetComponent* _target;
        CallbackGuard* _outer;
        bool _destroyed = false;
    };

    PointerTargetComponent::~PointerTargetComponent() {
        CallbackGuard::MarkDestroyed(_guard);
        if (_registered) {
            CoreManager::GetUIInput().Unregister(*this);
        }
    }

    void PointerTargetComponent::OnAttached() {
        if (!_registered) {
            // The scene attaches its nodes one by one while it fills its tree, so this order may be provisional: OnDraw() settles it
            _inputOrder = GetScene().GetInputOrder(*this);
            CoreManager::GetUIInput().Register(*this, _inputOrder);
            _registered = true;
        }
        OnTargetAttached();
    }

    void PointerTargetComponent::OnDraw() {
        _drawnPass = GetScene().GetDrawPass();
        _clip = HitClip::FromRenderer(CoreManager::GetRenderer().Get());
        RefreshInputOrder();
        OnTargetDraw();
    }

    void PointerTargetComponent::OnEnabledChanged() {
        // The press stays with UIInput, so its release is still not the game's
        _over.clear();
        OnTargetEnabledChanged();
    }

    void PointerTargetComponent::RefreshInputOrder() {
        // Unregister forgets a held press and the hover, so the order is touched only when the draw order really moved (a node
        // added, a layer edited). The pointers over the target are forgotten with them: no exit is told for those
        const int32_t order = GetScene().GetInputOrder(*this);
        if (order == _inputOrder) {
            return;
        }
        UIInput& input = CoreManager::GetUIInput();
        input.Unregister(*this);
        _over.clear();
        _inputOrder = order;
        input.Register(*this, _inputOrder);
    }

    Optional<Rect> PointerTargetComponent::GetNodeRect() const {
        const auto transform = GetNode().GetComponent<TransformComponent>();
        if (!transform) {
            return {};
        }
        return Rect{transform->_x, transform->_y, transform->_width, transform->_height};
    }

    bool PointerTargetComponent::ContainsScenePoint(float sceneX, float sceneY) const {
        const Optional<Rect> nodeRect = GetNodeRect();
        const bool ownsRect = _shape.kind != PointerShapeKind::TransformRect && _shape.rect.has_value();
        if (!nodeRect.has_value() && !ownsRect) {
            return false;
        }
        return _shape.Contains(nodeRect.value_or(Rect{}), sceneX, sceneY);
    }

    bool PointerTargetComponent::ContainsPoint(float outputX, float outputY) const {
        // Asked here too: before the first draw pass nothing else leaves a disabled target out
        if (!IsEnabled() || !IsSceneActive()) {
            return false;
        }
        const Scene& scene = GetScene();
        // A pass that never ran (a test that does not draw) counts everything as drawn
        if (scene.GetDrawPass() != 0 && _drawnPass != scene.GetDrawPass()) {
            return false;
        }
        if (!_clip.Contains(outputX, outputY)) {
            return false;
        }
        return ContainsScenePoint(scene.ScreenToSceneX(outputX), scene.ScreenToSceneY(outputY));
    }

    PointerEvent PointerTargetComponent::MakeEvent(uint32_t which, uint8_t button, uint8_t clicks, float outputX, float outputY) const {
        const Scene& scene = GetScene();
        PointerEvent event;
        event.sceneX = scene.ScreenToSceneX(outputX);
        event.sceneY = scene.ScreenToSceneY(outputY);
        event.outputX = outputX;
        event.outputY = outputY;
        event.which = which;
        event.button = button;
        event.clicks = clicks;
        event.touch = IsTouchPointer(which);
        return event;
    }

    bool PointerTargetComponent::NoteOver(const PointerEvent& event) {
        auto it = eastl::find_if(_over.begin(), _over.end(), [&](const OverPointer& over) { return over.which == event.which; });
        if (it != _over.end()) {
            it->outputX = event.outputX;
            it->outputY = event.outputY;
            return false;
        }
        if (_over.full()) {
            return false;
        }
        _over.push_back(OverPointer{event.which, event.outputX, event.outputY});
        return true;
    }

    void PointerTargetComponent::ForgetOver(uint32_t which) {
        _over.erase(eastl::remove_if(_over.begin(), _over.end(), [&](const OverPointer& over) { return over.which == which; }), _over.end());
    }

    bool PointerTargetComponent::ForgetIfDeaf() {
        if (IsEnabled() && TryGetScene() != nullptr) {
            return false;
        }
        _over.clear();
        return true;
    }

    bool PointerTargetComponent::OnMouseButtonDown(const SDL_MouseButtonEvent& event) {
        if (ForgetIfDeaf()) {
            return false;
        }
        const PointerEvent pointer = MakeEvent(event.which, event.button, event.clicks, event.x, event.y);
        return OnPointerPress(pointer);
    }

    bool PointerTargetComponent::OnMouseButtonUp(const SDL_MouseButtonEvent& event) {
        if (ForgetIfDeaf()) {
            return false;
        }
        // A release that reached this target by the walk belongs to a press of the game, or of another button or pointer: not ours to take
        if (!CoreManager::GetUIInput().IsDeliveringCapture(*this)) {
            return false;
        }
        const PointerEvent pointer = MakeEvent(event.which, event.button, event.clicks, event.x, event.y);
        OnPointerRelease(pointer);
        return true;
    }

    bool PointerTargetComponent::OnMouseMotion(const SDL_MouseMotionEvent& event) {
        if (ForgetIfDeaf()) {
            return false;
        }
        const bool holdsPress = CoreManager::GetUIInput().HoldsPress(*this, event.which);
        // A button is held and this target did not take its press: a drag of the game, which passing over the shape does not interrupt
        if (!holdsPress && event.state != 0) {
            return false;
        }

        const Scene& scene = GetScene();
        PointerEvent pointer = MakeEvent(event.which, 0, 0, event.x, event.y);
        pointer.sceneDx = pointer.sceneX - scene.ScreenToSceneX(event.x - event.xrel);
        pointer.sceneDy = pointer.sceneY - scene.ScreenToSceneY(event.y - event.yrel);

        CallbackGuard guard(*this);
        // A drag that left the shape keeps its pointer but never "enters" there: the enter is for a pointer that is on the shape
        const bool entered = ContainsPoint(event.x, event.y) && NoteOver(pointer);
        if (entered) {
            OnPointerEnter(pointer);
            if (guard.Destroyed() || ForgetIfDeaf()) {
                return true;
            }
        }
        if (holdsPress) {
            OnPointerDrag(pointer);
            return true;
        }
        if (OnPointerMove(pointer)) {
            return true;
        }
        if (entered && !guard.Destroyed() && !ForgetIfDeaf()) {
            // Not taken: UIInput records no hover for this target, so it will send no leave for this entry
            ForgetOver(pointer.which);
            OnPointerExit(pointer);
        }
        return false;
    }

    bool PointerTargetComponent::OnMouseWheel(const SDL_MouseWheelEvent& event) {
        if (ForgetIfDeaf()) {
            return false;
        }
        PointerEvent pointer = MakeEvent(event.which, 0, 0, event.mouse_x, event.mouse_y);
        pointer.wheelX = event.x;
        pointer.wheelY = event.y;
        return OnPointerWheel(pointer);
    }

    void PointerTargetComponent::OnPointerLeave(uint32_t which) {
        if (ForgetIfDeaf()) {
            return;
        }
        const auto it = eastl::find_if(_over.begin(), _over.end(), [&](const OverPointer& over) { return over.which == which; });
        if (it == _over.end()) {
            return;
        }
        const PointerEvent pointer = MakeEvent(which, 0, 0, it->outputX, it->outputY);
        _over.erase(it);
        OnPointerExit(pointer);
    }

}  // namespace BECore
