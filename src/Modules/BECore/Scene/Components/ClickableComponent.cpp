#include "ClickableComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/ActionsLibrary.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/InputEvents.h>
#include <Generated/ClickableComponent.gen.hpp>

namespace BECore {

    ClickableComponent::~ClickableComponent() {
        if (_registered) {
            CoreManager::GetUIInput().Unregister(*this);
        }
    }

    void ClickableComponent::OnAttached() {
        if (!_actionName.Empty()) {
            ActionsLibrary::GetInstance().Add(_actionName);
        }
        if (_registered) {
            return;
        }
        // The scene attaches its nodes one by one while it fills its tree, so this order may be provisional: OnDraw() settles it
        _inputOrder = GetScene().GetInputOrder(*this);
        CoreManager::GetUIInput().Register(*this, _inputOrder);
        _registered = true;
    }

    void ClickableComponent::OnDraw() {
        _drawnPass = GetScene().GetDrawPass();
        _clip = HitClip::FromRenderer(CoreManager::GetRenderer().Get());
        RefreshInputOrder();
    }

    void ClickableComponent::RefreshInputOrder() {
        // Unregister forgets a held press, so the order is touched only when the draw order really moved (a node added, a layer edited)
        const int32_t order = GetScene().GetInputOrder(*this);
        if (order == _inputOrder) {
            return;
        }
        UIInput& input = CoreManager::GetUIInput();
        input.Unregister(*this);
        _inputOrder = order;
        input.Register(*this, _inputOrder);
    }

    bool ClickableComponent::ContainsPoint(float outputX, float outputY) const {
        if (_actionName.Empty() || !IsSceneActive()) {
            return false;
        }
        const Scene& scene = GetScene();
        // A pass that never ran (a test that does not draw) counts everything as drawn
        if (scene.GetDrawPass() != 0 && _drawnPass != scene.GetDrawPass()) {
            return false;
        }
        const auto transform = GetNode().GetComponent<TransformComponent>();
        if (!transform || !_clip.Contains(outputX, outputY)) {
            return false;
        }
        const float x = scene.ScreenToSceneX(outputX);
        const float y = scene.ScreenToSceneY(outputY);
        return x >= transform->_x && x <= transform->_x + transform->_width && y >= transform->_y && y <= transform->_y + transform->_height;
    }

    bool ClickableComponent::OnMouseButtonDown(const SDL_MouseButtonEvent& /*event*/) {
        const PoolString action = _actionName;  // a copy: a handler of the event may switch the scene and destroy this component
        InputEvents::ActionEvent::Emit(action);
        return true;
    }

}  // namespace BECore
