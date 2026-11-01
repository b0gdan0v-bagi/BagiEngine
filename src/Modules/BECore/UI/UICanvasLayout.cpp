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
            for (auto it = element.children.rbegin(); it != element.children.rend(); ++it) {
                if (UIElement* hit = HitTestIn(**it, x, y)) {
                    return hit;
                }
            }
            const Rect& rect = element.rect;
            const bool inside = x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
            return element.IsHitTestable() && inside ? &element : nullptr;
        }

    }  // namespace

    bool UICanvasLayout::Update(const UILayoutContext& context) {
        if (_valid && _hasContext && context == _context && params == _params) {
            return false;
        }

        _context = context;
        _params = params;
        _hasContext = true;
        _resolved = UICanvasScaler::Resolve(params, context);

        // The variants depend on the bounds only: a change of the safe area or of the density that leaves them alone does not choose again
        if (!_valid || !_variantsChosen || !SameRect(_resolved.bounds, _variantBounds)) {
            UIVariants::Apply(*root, variants, _resolved.bounds);
            _variantBounds = _resolved.bounds;
            _variantsChosen = true;
            ++_variantRuns;
        }

        UITreeLayout::Run(*root, _resolved.contentBounds, _resolved.bounds);
        ++_layoutRuns;
        _valid = true;
        return true;
    }

    std::pair<float, float> UICanvasLayout::OutputToCanvas(float outputX, float outputY) const {
        return {_resolved.transform.ToViewX(outputX), _resolved.transform.ToViewY(outputY)};
    }

    UIElement* UICanvasLayout::HitTest(float canvasX, float canvasY) const {
        return _hasContext && root ? HitTestIn(*root, canvasX, canvasY) : nullptr;
    }

    eastl::vector<UILayoutIssue> UICanvasLayout::Validate(const UILayoutValidatorOptions& options) const {
        return UILayoutValidator::Validate(*root, _resolved, options);
    }

}  // namespace BECore
