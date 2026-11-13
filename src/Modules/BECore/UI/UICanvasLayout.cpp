#include "UICanvasLayout.h"

#include <BECore/UI/UITreeLayout.h>

namespace BECore {

    namespace {

        bool SameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

        /// The topmost visible element at a point that @p accepts: a child before its parent, a later sibling before an earlier one
        template <typename TAccept>
        UIElement* FindIn(UIElement& element, float x, float y, const TAccept& accept) {
            if (!element.active.visible) {
                return nullptr;
            }
            // The children of a popup element are found through the overlay pass of FindTopmost(), not here
            if (!element.HasPopupChildren()) {
                for (auto it = element.children.rbegin(); it != element.children.rend(); ++it) {
                    if (UIElement* hit = FindIn(**it, x, y, accept)) {
                        return hit;
                    }
                }
            }
            const Rect& rect = element.rect;
            const bool inside = x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
            return inside && accept(element) ? &element : nullptr;
        }

        void CollectOpenPopups(UIElement& element, eastl::vector<UIElement*>& popups) {
            if (!element.active.visible) {
                return;
            }
            if (element.HasPopupChildren()) {
                if (element.IsPopupOpen()) {
                    popups.push_back(&element);
                }
                return;
            }
            for (const auto& child : element.children) {
                CollectOpenPopups(*child, popups);
            }
        }

        /// The pass of the transitions over the whole tree (a hidden element too: its children have their own flags). @p variantsChosen: `active` was just reset from `style`, so a `held` element
        /// is not visible by its own state any more or again; otherwise the pass of the last layout left `active.visible` true for it and gives the authored `false` back first
        void SyncTransitions(UIElement& element, double now, bool inheritedLock, bool variantsChosen, eastl::vector<IntrusivePtr<UITransitionComponent>>& found) {
            bool lock = inheritedLock;
            if (UITransitionComponent* transition = element.GetComponent<UITransitionComponent>()) {
                if (transition->held && !variantsChosen) {
                    element.active.visible = false;
                }
                transition->held = false;
                const UITransitionSpec spec = transition->MakeSpec();
                transition->run.Sync(element.active.visible, now, spec);
                if (transition->run.IsLeaving(now)) {
                    element.active.visible = true;  // it is drawn, laid out and hit until the run ends
                    transition->held = true;
                }
                lock = lock || (transition->run.IsTransitioning(now) && !spec.inputDuringTransition);
                found.push_back(IntrusivePtr<UITransitionComponent>(transition));
            }
            element.inputLocked = lock;
            for (const auto& child : element.children) {
                SyncTransitions(*child, now, lock, variantsChosen, found);
            }
        }

    }  // namespace

    bool UICanvasLayout::Update(const UILayoutContext& context) {
        BE_PROFILE_SCOPE("UICanvasLayout::Update");
        if (_valid && _hasContext && context == _context && params == _params && regions == _regions) {
            return false;
        }

        _context = context;
        _params = params;
        _regions = regions;
        _hasContext = true;
        _resolved = UICanvasScaler::Resolve(params, context);

        // The variants depend on the bounds only: a change of the safe area or of the density that leaves them alone does not choose again
        bool variantsChosen = false;
        if (!_valid || !_variantsChosen || !SameRect(_resolved.bounds, _variantBounds)) {
            UIVariants::Apply(*root, variants, _resolved.bounds);
            _variantBounds = _resolved.bounds;
            _variantsChosen = true;
            variantsChosen = true;
            ++_variantRuns;
        }

        _transitions.clear();
        SyncTransitions(*root, time, false, variantsChosen, _transitions);

        UITreeLayout::Run(*root, _resolved.contentBounds, _resolved.bounds, _regions);
        ++_layoutRuns;
        _valid = true;
        return true;
    }

    bool UICanvasLayout::AdvanceTransitions(double now) {
        bool ended = false;
        for (const auto& transition : _transitions) {
            ended = transition->run.TakeFinished(now) || ended;
        }
        if (ended) {
            Invalidate();
        }
        return ended;
    }

    std::pair<float, float> UICanvasLayout::OutputToCanvas(float outputX, float outputY) const {
        return {_resolved.transform.ToViewX(outputX), _resolved.transform.ToViewY(outputY)};
    }

    eastl::vector<UIElement*> UICanvasLayout::OpenPopups() const {
        eastl::vector<UIElement*> popups;
        if (root) {
            CollectOpenPopups(*root, popups);
        }
        return popups;
    }

    namespace {

        /// The walk of HitTest() and PickElement(): the open popups first, then the tree; they differ in what @p accept takes
        template <typename TAccept>
        UIElement* FindTopmost(const UICanvasLayout& layout, float x, float y, const TAccept& accept) {
            if (!layout.HasLayout() || !layout.root) {
                return nullptr;
            }
            // The open popups are drawn above the whole tree, so they are tested first: the later one, then the later child
            const eastl::vector<UIElement*> popups = layout.OpenPopups();
            for (auto popup = popups.rbegin(); popup != popups.rend(); ++popup) {
                for (auto child = (*popup)->children.rbegin(); child != (*popup)->children.rend(); ++child) {
                    if (UIElement* hit = FindIn(**child, x, y, accept)) {
                        return hit;
                    }
                }
            }
            return FindIn(*layout.root, x, y, accept);
        }

    }  // namespace

    UIElement* UICanvasLayout::HitTest(float canvasX, float canvasY) const {
        return FindTopmost(*this, canvasX, canvasY, [](const UIElement& element) { return element.IsHitTestable(); });
    }

    UIElement* UICanvasLayout::PickElement(float canvasX, float canvasY) const {
        const UIElement* canvasRoot = root.Get();
        return FindTopmost(*this, canvasX, canvasY,
                           [canvasRoot](const UIElement& element) { return &element != canvasRoot && (element.IsHitTestable() || element.color.a > 0 || !element.components.empty()); });
    }

    eastl::vector<UILayoutIssue> UICanvasLayout::Validate(const UILayoutValidatorOptions& options) const {
        return UILayoutValidator::Validate(*root, _resolved, options, _regions);
    }

}  // namespace BECore
