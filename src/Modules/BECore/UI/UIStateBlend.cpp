#include "UIStateBlend.h"

#include <BECore/Animation/Lerp.h>

namespace BECore {

    namespace {

        bool Same(const Color& a, const Color& b) {
            return ColorsEqual(a, b);
        }

        bool Same(const UIStateLook::Look& a, const UIStateLook::Look& b) {
            return ColorsEqual(a.fill, b.fill) && ColorsEqual(a.gradientEnd, b.gradientEnd) && a.box == b.box;
        }

        Color Blend(const Color& from, const Color& to, float t) {
            return Lerp(from, to, t);
        }

        UIStateLook::Look Blend(const UIStateLook::Look& from, const UIStateLook::Look& to, float t) {
            return UIStateLook::Lerp(from, to, t);
        }

    }  // namespace

    template <typename T>
    const T& UIBlend<T>::Update(const T& target, double now, float duration, EaseType ease) {
        if (!_started || !(duration > 0.0f)) {
            _started = true;
            _running = false;
            _target = target;
            _shown = target;
            return _shown;
        }
        if (!Same(target, _target)) {
            // A new target starts from what is on screen at this moment
            _from = _running ? Blend(_from, _target, Ease(_ease, static_cast<float>(std::clamp((now - _start) / static_cast<double>(_length), 0.0, 1.0)))) : _shown;
            _target = target;
            _start = now;
            _length = duration;
            _ease = ease;
            _running = true;
        }
        if (_running) {
            const float t = static_cast<float>(std::clamp((now - _start) / static_cast<double>(_length), 0.0, 1.0));
            if (t >= 1.0f) {
                _running = false;
                _shown = _target;
            } else {
                _shown = Blend(_from, _target, Ease(_ease, t));
            }
        }
        return _shown;
    }

    template class UIBlend<Color>;
    template class UIBlend<UIStateLook::Look>;

}  // namespace BECore
