#include "UICanvasComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Generated/UICanvasComponent.gen.hpp>

namespace BECore {

    namespace {

        constexpr uint32_t ButtonBit(uint8_t button) {
            return button < 32 ? 1u << button : 0u;
        }

        bool SetVisibleIn(SceneNode& node, PoolString nodeName, bool visible) {
            bool found = false;
            if (node.GetName() == nodeName) {
                if (auto canvas = node.GetComponent<UICanvasComponent>()) {
                    canvas->_visible = visible;
                    found = true;
                }
            }
            for (const auto& child : node.GetChildren()) {
                found |= SetVisibleIn(*child, nodeName, visible);
            }
            return found;
        }

    }  // namespace

    UICanvasComponent::~UICanvasComponent() {
        if (_registered) {
            CoreManager::GetUIInput().Unregister(*this);
        }
    }

    void UICanvasComponent::Initialize() {
        // Called after loading and again by AddComponent: the written elements are handed over once
        if (_authoredAdopted) {
            return;
        }
        _authoredAdopted = true;
        for (const auto& element : _elements) {
            _layout.root->children.push_back(element);
        }
        _layout.variants.insert(_layout.variants.end(), _variants.begin(), _variants.end());
        _layout.Invalidate();
    }

    void UICanvasComponent::OnAttached() {
        // The scene attaches its nodes one by one while it fills its tree, so this rank may be provisional: RefreshInputOrder() settles it on every draw
        _inputOrder = GetScene().GetDrawRank(GetNode());
        CoreManager::GetUIInput().Register(*this, _inputOrder);
        _registered = true;
    }

    void UICanvasComponent::RefreshInputOrder() {
        // The draw order can change after attaching (a node added, a layer edited): the input order follows it
        const int rank = GetScene().GetDrawRank(GetNode());
        if (rank == _inputOrder) {
            return;
        }
        UIInput& input = CoreManager::GetUIInput();
        input.Unregister(*this);
        _inputOrder = rank;
        input.Register(*this, _inputOrder);
    }

    void UICanvasComponent::SyncParams() {
        UICanvasScaleParams& params = _layout.params;
        params.space = _space;
        params.scaleMode = _scaleMode;
        params.referenceWidth = _referenceWidth;
        params.referenceHeight = _referenceHeight;
        params.match = _match;
        params.minScale = _minScale;
        params.maxScale = _maxScale;
        params.useSafeArea = _useSafeArea;
    }

    UILayoutContext UICanvasComponent::MakeContext(float outputWidth, float outputHeight) const {
        UILayoutContext context = CoreManager::GetUIEnvironment().MakeContext(CoreManager::GetMainWindow().Get(), outputWidth, outputHeight);
        context.sceneViewWidth = GetScene().GetViewWidth();
        context.sceneViewHeight = GetScene().GetViewHeight();
        if (_space == CanvasSpace::World) {
            if (auto transform = GetNode().GetComponent<TransformComponent>()) {
                context.worldBox = {transform->_x, transform->_y, transform->_width, transform->_height};
            }
        }
        return context;
    }

    void UICanvasComponent::UpdateLayout(float outputWidth, float outputHeight) {
        if (outputWidth <= 0.0f || outputHeight <= 0.0f) {
            return;
        }
        SyncParams();
        _layout.Update(MakeContext(outputWidth, outputHeight));
    }

    void UICanvasComponent::DrawElement(IRenderer& renderer, const UIElement& element) const {
        if (!element.active.visible) {
            return;
        }
        if (element.color.a > 0 && !element.rect.IsEmpty()) {
            renderer.DrawFilledRect(element.rect.x, element.rect.y, element.rect.w, element.rect.h, element.color);
        }
        for (const auto& child : element.children) {
            DrawElement(renderer, *child);
        }
    }

    void UICanvasComponent::OnDraw() {
        // Also while hidden, so a canvas that is shown later (a modal dialog) already has its place in the input order
        RefreshInputOrder();

        const auto& renderer = CoreManager::GetRenderer();
        if (!renderer || !_visible) {
            return;
        }
        UpdateLayout(renderer->GetOutputWidth(), renderer->GetOutputHeight());
        if (!_layout.HasLayout()) {
            return;
        }

        // The scene's own transform is in effect here; the canvas sets its own and gives the scene's back
        const ViewTransform sceneTransform = renderer->GetViewTransform();
        renderer->SetViewTransform(_layout.GetResolved().transform);
        DrawElement(*renderer, *_layout.root);
        renderer->SetViewTransform(sceneTransform);
    }

    bool UICanvasComponent::TakesInput() const {
        return _visible && _inputEnabled && IsSceneActive();
    }

    UIElement* UICanvasComponent::HitTestOutput(float outputX, float outputY) const {
        if (!_layout.HasLayout()) {
            return nullptr;
        }
        const auto [x, y] = _layout.OutputToCanvas(outputX, outputY);
        return _layout.HitTest(x, y);
    }

    bool UICanvasComponent::OnMouseButtonDown(const SDL_MouseButtonEvent& event) {
        if (!TakesInput()) {
            return false;
        }
        const bool consumed = _modal || HitTestOutput(event.x, event.y) != nullptr;
        if (consumed) {
            _consumedButtons |= ButtonBit(event.button);
        }
        return consumed;
    }

    bool UICanvasComponent::OnMouseButtonUp(const SDL_MouseButtonEvent& event) {
        if (!TakesInput()) {
            _consumedButtons = 0;
            return false;
        }
        // A press that this canvas took is followed to its release wherever the pointer is
        const uint32_t bit = ButtonBit(event.button);
        if ((_consumedButtons & bit) != 0) {
            _consumedButtons &= ~bit;
            return true;
        }
        return _modal || HitTestOutput(event.x, event.y) != nullptr;
    }

    bool UICanvasComponent::OnMouseMotion(const SDL_MouseMotionEvent& event) {
        if (!TakesInput()) {
            return false;
        }
        if (_modal || _consumedButtons != 0) {
            return true;
        }
        // A button is held and this canvas did not take its press: a drag of the game, which passing over the UI does not interrupt
        if (event.state != 0) {
            return false;
        }
        return HitTestOutput(event.x, event.y) != nullptr;  // hovering over an element
    }

    bool UICanvasComponent::OnMouseWheel(const SDL_MouseWheelEvent& event) {
        if (!TakesInput()) {
            return false;
        }
        return _modal || HitTestOutput(event.mouse_x, event.mouse_y) != nullptr;
    }

    bool UICanvasComponent::SetVisibleByName(Scene& scene, PoolString nodeName, bool visible) {
        return SetVisibleIn(scene.GetRootNode(), nodeName, visible);
    }

}  // namespace BECore
