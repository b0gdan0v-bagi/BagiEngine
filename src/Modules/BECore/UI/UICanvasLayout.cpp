#include "UICanvasLayout.h"

#include <BECore/UI/UITreeLayout.h>

namespace BECore {

    namespace {

        bool SameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

        UIElement* HitTestIn(UIElement& element, float x, float y) {
            if (!element.active.visible) {
                return nullptr;
            }
            // The children of a popup element are found through the overlay pass of HitTest(), not here
            if (!element.HasPopupChildren()) {
                for (auto it = element.children.rbegin(); it != element.children.rend(); ++it) {
                    if (UIElement* hit = HitTestIn(**it, x, y)) {
                        return hit;
                    }
                }
            }
            const Rect& rect = element.rect;
            const bool inside = x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
            return element.IsHitTestable() && inside ? &element : nullptr;
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

    }  // namespace

    bool UICanvasLayout::Update(const UILayoutContext& context) {
        if (_valid && _hasContext && context == _context && params == _params && regions == _regions) {
            return false;
        }

        _context = context;
        _params = params;
        _regions = regions;
        _hasContext = true;
        _resolved = UICanvasScaler::Resolve(params, context);

        // The variants depend on the bounds only: a change of the safe area or of the density that leaves them alone does not choose again
        if (!_valid || !_variantsChosen || !SameRect(_resolved.bounds, _variantBounds)) {
            UIVariants::Apply(*root, variants, _resolved.bounds);
            _variantBounds = _resolved.bounds;
            _variantsChosen = true;
            ++_variantRuns;
        }

        UITreeLayout::Run(*root, _resolved.contentBounds, _resolved.bounds, _regions);
        ++_layoutRuns;
        _valid = true;
        return true;
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

    UIElement* UICanvasLayout::HitTest(float canvasX, float canvasY) const {
        if (!_hasContext || !root) {
            return nullptr;
        }
        // The open popups are drawn above the whole tree, so they are tested first: the later one, then the later child
        const eastl::vector<UIElement*> popups = OpenPopups();
        for (auto popup = popups.rbegin(); popup != popups.rend(); ++popup) {
            for (auto child = (*popup)->children.rbegin(); child != (*popup)->children.rend(); ++child) {
                if (UIElement* hit = HitTestIn(**child, canvasX, canvasY)) {
                    return hit;
                }
            }
        }
        return HitTestIn(*root, canvasX, canvasY);
    }

    eastl::vector<UILayoutIssue> UICanvasLayout::Validate(const UILayoutValidatorOptions& options) const {
        return UILayoutValidator::Validate(*root, _resolved, options);
    }

}  // namespace BECore
