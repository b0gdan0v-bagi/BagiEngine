#include "UICanvasComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UIDocument.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UITransitionComponent.h>
#include <Generated/UICanvasComponent.gen.hpp>

namespace BECore {

    namespace {

        constexpr uint32_t ButtonBit(uint8_t button) {
            return button < 32 ? 1u << button : 0u;
        }

        /// SDL tells a finger by the id of the mouse its events are made for
        template <typename TEvent>
        bool IsTouch(const TEvent& event) {
            return event.which == SDL_TOUCH_MOUSEID;
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

        bool SetElementVisibleIn(SceneNode& node, PoolString elementName, bool visible) {
            bool found = false;
            if (auto canvas = node.GetComponent<UICanvasComponent>()) {
                found = canvas->SetElementVisible(elementName, visible);
            }
            for (const auto& child : node.GetChildren()) {
                found |= SetElementVisibleIn(*child, elementName, visible);
            }
            return found;
        }

        bool Contains(const UIElement& root, const UIElement& searched) {
            if (&root == &searched) {
                return true;
            }
            for (const auto& child : root.children) {
                if (Contains(*child, searched)) {
                    return true;
                }
            }
            return false;
        }

        /// What a transition does to what is drawn inside the scope: the view transform composed with the scale and the slide about @p pivot, and the opacity multiplied by the progress. Puts both
        /// back
        class ScopedTransitionDraw {
        public:
            ScopedTransitionDraw(IRenderer& renderer, const UITransition::Evaluated& evaluated, Vec2 pivot) : _renderer(renderer), _view(renderer.GetViewTransform()), _opacity(renderer.GetOpacity()) {
                _renderer.SetViewTransform(UITransition::Compose(_view, pivot, evaluated));
                _renderer.SetOpacity(_opacity * IRenderer::ClampOpacity(evaluated.opacity));
            }
            ~ScopedTransitionDraw() {
                _renderer.SetViewTransform(_view);
                _renderer.SetOpacity(_opacity);
            }

            ScopedTransitionDraw(const ScopedTransitionDraw&) = delete;
            ScopedTransitionDraw& operator=(const ScopedTransitionDraw&) = delete;

        private:
            IRenderer& _renderer;
            ViewTransform _view;
            float _opacity;
        };

        Vec2 CentreOf(const Rect& rect) {
            return {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
        }

    }  // namespace

    UICanvasComponent::~UICanvasComponent() {
        if (_registered) {
            CoreManager::GetUIInput().Unregister(*this);
        }
    }

    void UICanvasComponent::AdoptDocument() {
        const auto document = CoreManager::GetResourceManager().Load<UIDocument>(_source.ToStringView());
        if (!document || document->GetState() != ResourceState::Loaded) {
            LOG_ERROR("UICanvasComponent: cannot load the screen '{}'"_format(_source.ToStringView()));
            return;
        }
        // Errors in the file are logged by Instantiate(); what could be read is used
        UIScreenContent content;
        static_cast<void>(document->Instantiate(content));
        for (const auto& element : content.elements) {
            _layout.root->children.push_back(element);
        }
        _layout.variants.insert(_layout.variants.end(), content.variants.begin(), content.variants.end());
    }

    void UICanvasComponent::Initialize() {
        // Called after loading and again by AddComponent: the written elements are handed over once
        if (_authoredAdopted) {
            return;
        }
        _authoredAdopted = true;
        if (!_source.Empty()) {
            AdoptDocument();
        }
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
        params.extendToOutput = _extendToOutput;

        const bool ignored = _extendToOutput && _space != CanvasSpace::SceneView;
        if (ignored && !_extendWarned) {
            LOG_WARNING("UICanvasComponent: extendToOutput applies to a SceneView canvas only; ignored on a {} canvas"_format(EnumUtils<CanvasSpace>::ToString(_space)));
        }
        _extendWarned = ignored;
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
        BE_PROFILE_SCOPE("UICanvas::UpdateLayout");
        if (outputWidth <= 0.0f || outputHeight <= 0.0f) {
            return;
        }
        SyncParams();
        const double now = CoreManager::GetTime().GetTotalTime();
        _layout.time = now;
        _layout.AdvanceTransitions(now);  // a run that ended lays out again, so a hidden element goes and a shown one unlocks
        _layout.Update(MakeContext(outputWidth, outputHeight));
    }

    void UICanvasComponent::DrawElement(IRenderer& renderer, UIElement& element, const UIStateLook::State* inheritedState, float inheritedDuration) const {
        if (!element.active.visible) {
            return;
        }
        const ScopedOpacity opacity(renderer, element.opacity);  // the element and its subtree, nested by multiplying
        std::optional<ScopedTransitionDraw> transitionDraw;
        if (const UITransitionComponent* transition = element.GetComponent<UITransitionComponent>()) {
            const double now = CoreManager::GetTime().GetTotalTime();
            if (transition->run.IsTransitioning(now)) {
                transitionDraw.emplace(renderer, UITransition::Evaluate(transition->MakeSpec(), transition->run.Progress(now)), CentreOf(element.rect));
            }
        }
        element.ResolveTheme(CoreManager::GetUIThemes());
        // An element with a clicker is drawn in its own state and hands it to its descendants; one without is drawn in the state it was given
        element.SetInheritedState(inheritedState, inheritedDuration);
        UIStateLook::State ownState;
        if (element.OwnsState()) {
            ownState = element.GetVisualState();
            inheritedState = &ownState;
            inheritedDuration = element.GetStateDuration();
        }
        element.DrawSelf(renderer);
        element.DrawComponents(renderer);
        if (element.HasPopupChildren()) {
            return;  // drawn by DrawPopups() after the whole tree
        }
        for (const auto& child : element.children) {
            DrawElement(renderer, *child, inheritedState, inheritedDuration);
        }
    }

    void UICanvasComponent::DrawPopups(IRenderer& renderer) const {
        for (UIElement* popup : _layout.OpenPopups()) {
            for (const auto& child : popup->children) {
                DrawElement(renderer, *child);
            }
        }
    }

    void UICanvasComponent::UpdateTooltip(UIElement* under, bool touch) {
        _pointerOver = under ? IntrusivePtr<UIElement>(under) : IntrusivePtr<UIElement>{};
        _pointerTouch = touch;
        RefreshTooltip();
    }

    void UICanvasComponent::RefreshTooltip() {
        const UIElement* target = _pointerOver.Get();
        const bool hasTooltip = target && !target->GetTooltipText().empty() && target->active.visible && !target->rect.IsEmpty();
        // A mouse hides the tooltip for a press of any button on this canvas; a finger only counts the press that began on the element it is on
        const bool pressed = _pointerTouch ? (_pressed && _pressed == _pointerOver) : _consumedButtons != 0;
        _tooltip.Update(hasTooltip ? target : nullptr, _pointerTouch, pressed, CoreManager::GetTime().GetTotalTime());
    }

    void UICanvasComponent::DrawTooltip(IRenderer& renderer) {
        const UIElement* target = _pointerOver.Get();
        if (!target || !_tooltip.IsShown(target)) {
            return;
        }
        const eastl::string& tooltipText = target->GetTooltipText();
        if (!_tooltipPlate) {
            _tooltipPlate = New<UIElement>();
            _tooltipPlate->active.fontSize = UITooltip::kFontSize;
            _tooltipPlate->AddComponent<UITextComponent>().textColor = UITooltip::kTextColor;
        }
        UITextComponent& tooltipLabel = *_tooltipPlate->GetComponent<UITextComponent>();
        if (tooltipLabel.text != tooltipText) {
            tooltipLabel.text = tooltipText;
        }
        const float scale = renderer.GetViewTransform().scale;
        const Vec2 text = tooltipLabel.MeasureText(*_tooltipPlate, UITooltip::kMaxWidth, scale);
        if (text.x <= 0.0f || text.y <= 0.0f) {
            return;
        }
        const Rect plate = UITooltip::Place(target->rect, {text.x + 2.0f * UITooltip::kPadding, text.y + 2.0f * UITooltip::kPadding}, _layout.GetResolved().contentBounds);
        renderer.DrawFilledRect(plate.x, plate.y, plate.w, plate.h, UITooltip::kPlateColor);
        // The text is laid out against the width it was measured with, so it wraps where the measure did; the plate (its left edge and its size) is what shows
        _tooltipPlate->rect = {plate.x + UITooltip::kPadding, plate.y + UITooltip::kPadding, UITooltip::kMaxWidth, text.y};
        tooltipLabel.DrawSelf(renderer, *_tooltipPlate);
    }

    bool UICanvasComponent::SyncPopups() {
        const eastl::vector<UIElement*> open = _layout.OpenPopups();
        UIElement* keep = nullptr;
        for (UIElement* popup : open) {
            if (popup != _popup.Get()) {
                keep = popup;  // opened since the last look; the last one in tree order wins
            }
        }
        if (!keep && !open.empty()) {
            keep = _popup.Get();
        }
        bool changed = keep != _popup.Get();
        if (_popup && _popup->IsPopupOpen() && eastl::find(open.begin(), open.end(), _popup.Get()) == open.end()) {
            _popup->ClosePopup();  // it left the list of open popups without being closed: it (or an ancestor) was hidden
        }
        for (UIElement* popup : open) {
            if (popup != keep) {
                popup->ClosePopup();
                changed = true;
            }
        }
        _popup = keep ? IntrusivePtr<UIElement>(keep) : IntrusivePtr<UIElement>{};
        if (changed) {
            Invalidate();
        }
        return changed;
    }

    void UICanvasComponent::CloseAllPopups() {
        bool changed = static_cast<bool>(_popup);
        for (UIElement* popup : _layout.OpenPopups()) {
            popup->ClosePopup();
            changed = true;
        }
        _popup.Reset();
        if (changed) {
            Invalidate();
        }
    }

    UIElement* UICanvasComponent::FindPopupOf(const UIElement& element, UIElement*& child) const {
        const eastl::vector<UIElement*> open = _layout.OpenPopups();
        for (auto popup = open.rbegin(); popup != open.rend(); ++popup) {
            for (const auto& candidate : (*popup)->children) {
                if (Contains(*candidate, element)) {
                    child = candidate.Get();
                    return *popup;
                }
            }
        }
        return nullptr;
    }

    bool UICanvasComponent::IsInOpenPopup(const UIElement* element) const {
        if (!element) {
            return false;
        }
        for (UIElement* popup : _layout.OpenPopups()) {
            if (Contains(*popup, *element)) {
                return true;
            }
        }
        return false;
    }

    void UICanvasComponent::OnDraw() {
        BE_PROFILE_SCOPE("UICanvas::OnDraw");
        // Also while hidden, so a canvas that is shown later (a modal dialog) already has its place in the input order
        RefreshInputOrder();

        const auto& renderer = CoreManager::GetRenderer();
        if (!_visible) {
            ClearPointerState();  // a hidden screen keeps no hover and no press
        }
        if (!renderer || !_visible) {
            return;
        }
        if (!TakesInput()) {
            _tooltip.Reset();  // a screen that takes no input (turned off while the pointer rested on it) shows no tooltip
            _pointerOver.Reset();
        }
        SyncPopups();  // a popup opened or closed by code since the last draw gets its rects now
        UpdateLayout(renderer->GetOutputWidth(), renderer->GetOutputHeight());
        if (!_layout.HasLayout()) {
            return;
        }

        // The scene's own transform is in effect here; the canvas sets its own and gives the scene's back
        const ViewTransform sceneTransform = renderer->GetViewTransform();
        renderer->SetViewTransform(_layout.GetResolved().transform);
        {
            const ScopedOpacity opacity(*renderer, _opacity);  // the tooltip plate stays outside
            DrawElement(*renderer, *_layout.root);
            DrawPopups(*renderer);
        }
        RefreshTooltip();
        DrawTooltip(*renderer);
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

    void UICanvasComponent::SetHovered(UIElement* element) {
        if (element == _hovered.Get()) {
            return;
        }
        if (_hovered) {
            _hovered->hovered = false;
        }
        _hovered = element ? IntrusivePtr<UIElement>(element) : IntrusivePtr<UIElement>{};
        if (element) {
            element->hovered = true;
        }
    }

    void UICanvasComponent::ClearPointerState() {
        if (_popup) {
            CloseAllPopups();  // a canvas that cannot take input keeps no open list; one that has seen none (SyncPopups) has nothing to close, so no walk of the tree
        }
        if (_hovered) {
            _hovered->hovered = false;
            _hovered.Reset();
        }
        _tooltip.Reset();
        _pointerOver.Reset();
        if (_pressed) {
            _pressed->pressed = false;
            _pressed.Reset();
        }
    }

    IntrusivePtr<UIElement> UICanvasComponent::TakeClick(const SDL_MouseButtonEvent& event) {
        if (event.button != SDL_BUTTON_LEFT || !_pressed) {
            return {};
        }
        IntrusivePtr<UIElement> element = _pressed;
        _pressed.Reset();
        element->pressed = false;
        // Letting go anywhere else cancels the click, and so does a transition that locked the element since the press
        return !element->inputLocked && HitTestOutput(event.x, event.y) == element.Get() ? element : IntrusivePtr<UIElement>{};
    }

    bool UICanvasComponent::OnMouseButtonDown(const SDL_MouseButtonEvent& event) {
        BE_PROFILE_SCOPE("UICanvas::OnMouseButtonDown");
        if (!TakesInput()) {
            ClearPointerState();
            return false;
        }
        UIElement* hit = HitTestOutput(event.x, event.y);
        if (event.button == SDL_BUTTON_LEFT && !IsInOpenPopup(hit) && !_layout.OpenPopups().empty()) {
            // A press outside the open list only closes it: it is no click on what is below, and its release is ours too
            CloseAllPopups();
            _consumedButtons |= ButtonBit(event.button);
            UpdateTooltip(hit, IsTouch(event));
            return true;
        }
        const bool consumed = _modal || hit != nullptr;
        if (consumed) {
            _consumedButtons |= ButtonBit(event.button);
        }
        // An element that is showing or hiding without input shields what is below it and gets nothing
        const bool locked = hit && hit->inputLocked;
        IntrusivePtr<UIElement> toPress;
        if (!locked && hit && hit->IsInteractive() && event.button == SDL_BUTTON_LEFT) {
            if (_pressed) {
                _pressed->pressed = false;  // a release that never arrived
            }
            _pressed = IntrusivePtr<UIElement>(hit);
            hit->pressed = true;
            toPress = _pressed;
        }
        UpdateTooltip(locked ? nullptr : hit, IsTouch(event));
        if (toPress) {
            // Last, like OnClick: the handler may do anything, and this canvas is done with the event by then. The copy keeps the element alive through it
            const auto [x, y] = _layout.OutputToCanvas(event.x, event.y);
            toPress->OnPress(x, y);
        }
        return consumed;
    }

    bool UICanvasComponent::ConsumeButtonUp(const SDL_MouseButtonEvent& event) {
        // A press that this canvas took is followed to its release wherever the pointer is
        const uint32_t bit = ButtonBit(event.button);
        if ((_consumedButtons & bit) != 0) {
            _consumedButtons &= ~bit;
            return true;
        }
        return _modal || HitTestOutput(event.x, event.y) != nullptr;
    }

    bool UICanvasComponent::OnMouseButtonUp(const SDL_MouseButtonEvent& event) {
        BE_PROFILE_SCOPE("UICanvas::OnMouseButtonUp");
        if (!TakesInput()) {
            _consumedButtons = 0;
            ClearPointerState();
            return false;
        }
        // The click runs last: its handlers may do anything, and this canvas is done with the event by then
        IntrusivePtr<UIElement> clicked = TakeClick(event);
        const bool consumed = ConsumeButtonUp(event);
        UIElement* under = HitTestOutput(event.x, event.y);
        UpdateTooltip(under && under->inputLocked ? nullptr : under, IsTouch(event));
        if (clicked && _tooltip.IsLongPress()) {
            clicked.Reset();  // a finger held long enough showed the tooltip: the release is no click
        }
        if (clicked) {
            UIElement* choice = nullptr;
            if (UIElement* popup = FindPopupOf(*clicked, choice)) {
                const IntrusivePtr<UIElement> keep(popup);
                popup->OnPopupChoice(*choice);
            } else {
                clicked->OnClick();
            }
            SyncPopups();  // the click may have opened or closed a list
        }
        return consumed;
    }

    bool UICanvasComponent::OnMouseMotion(const SDL_MouseMotionEvent& event) {
        BE_PROFILE_SCOPE("UICanvas::OnMouseMotion");
        if (!TakesInput()) {
            ClearPointerState();
            return false;
        }
        UIElement* hit = HitTestOutput(event.x, event.y);
        // A button is held and this canvas did not take its press: a drag of the game, which passing over the UI does not interrupt or light up
        const bool gameDrag = event.state != 0 && _consumedButtons == 0;
        const bool locked = hit && hit->inputLocked;  // consumed below like any hit, but it gets no hover and no tooltip
        SetHovered(gameDrag || locked ? nullptr : hit);
        UpdateTooltip(gameDrag || locked ? nullptr : hit, IsTouch(event));
        if (_pressed && !_pressed->inputLocked && (event.state & SDL_BUTTON_LMASK) != 0) {
            // The press that began on this element is held: the element follows the pointer wherever it goes. The copy keeps it alive through the handler
            const IntrusivePtr<UIElement> pressed = _pressed;
            const auto [x, y] = _layout.OutputToCanvas(event.x, event.y);
            pressed->OnDrag(x, y);
        }
        if (_modal || _consumedButtons != 0) {
            return true;
        }
        if (gameDrag) {
            return false;
        }
        return hit != nullptr;  // hovering over an element
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

    bool UICanvasComponent::SetElementVisible(PoolString elementName, bool visible) {
        UIElement* element = _layout.root->FindByName(elementName);
        if (!element) {
            return false;
        }
        element->style.visible = visible;
        Invalidate();
        return true;
    }

    bool UICanvasComponent::SetElementVisibleByName(Scene& scene, PoolString elementName, bool visible) {
        return SetElementVisibleIn(scene.GetRootNode(), elementName, visible);
    }

}  // namespace BECore
