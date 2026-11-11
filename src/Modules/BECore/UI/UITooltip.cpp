#include "UITooltip.h"

namespace BECore {

    Rect UITooltip::Place(const Rect& target, const Vec2& size, const Rect& bounds) {
        const float boundsRight = bounds.x + bounds.w;
        const float boundsBottom = bounds.y + bounds.h;

        // Centred on the button, then kept inside the bounds (a plate wider than they are starts at the left edge)
        const float centred = target.x + (target.w - size.x) * 0.5f;
        const float x = std::max(bounds.x, std::min(centred, boundsRight - size.x));

        const float below = target.y + target.h + kGap;
        const float above = target.y - kGap - size.y;
        float y = below;
        if (below + size.y > boundsBottom) {
            y = above >= bounds.y ? above : std::max(bounds.y, std::min(below, boundsBottom - size.y));
        }
        return {x, y, size.x, size.y};
    }

    void UITooltip::Tracker::Update(const UIElement* target, bool touch, bool pressed, double now) {
        const bool same = target == _target && touch == _touch;
        // The hold that has lasted by now counts before this call changes anything, so a release after a long wait is a long press even when nobody asked in between
        if (same && _target && _touch && _pressed && !_longPress && now - _since >= kLongPress) {
            _longPress = true;
        }

        if (!same) {
            _target = target;
            _touch = touch;
            _pressed = pressed;
            _suppressed = pressed;
            _longPress = false;
            _lingerEnd = 0.0;
            _since = now;
        } else if (pressed != _pressed) {
            _pressed = pressed;
            if (pressed) {
                _suppressed = true;
                _longPress = false;
                _since = now;
            } else if (_longPress && _touch) {
                _lingerEnd = now + kTouchLinger;
            }
        }

        if (!_target) {
            _shown = false;
        } else if (_touch) {
            _shown = _longPress && (_pressed || now < _lingerEnd);
        } else {
            _shown = !_suppressed && !_pressed && now - _since >= kHoverDelay;
        }
    }

    void UITooltip::Tracker::Reset() {
        *this = Tracker{};
    }

}  // namespace BECore
