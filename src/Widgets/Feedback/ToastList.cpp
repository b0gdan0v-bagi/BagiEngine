#include "ToastList.h"

#include <EASTL/algorithm.h>

namespace BECore {

    void ToastList::Push(ToastLevel level, StringView text) {
        const auto end = _toasts.begin() + _size;
        const auto same = eastl::find_if(_toasts.begin(), end, [level, text](const Toast& toast) { return toast.level == level && StringView(toast.text.data(), toast.text.size()) == text; });
        if (same != end) {
            ++same->count;
            same->age = eastl::min(same->age, kFadeInSeconds);
            eastl::rotate(same, same + 1, end);
            return;
        }
        if (_size == kCapacity) {
            eastl::rotate(_toasts.begin(), _toasts.begin() + 1, _toasts.end());
            --_size;
        }
        Toast& added = _toasts[_size++];
        added.text.assign(text.data(), text.size());
        added.level = level;
        added.count = 1;
        added.age = 0.0f;
    }

    void ToastList::Advance(float seconds) {
        size_t kept = 0;
        for (size_t i = 0; i < _size; ++i) {
            _toasts[i].age += seconds;
            if (_toasts[i].age < kLifeSeconds) {
                // A swap, so the slot that is given up keeps a text buffer
                if (kept != i) {
                    eastl::swap(_toasts[kept], _toasts[i]);
                }
                ++kept;
            }
        }
        _size = kept;
    }

    float ToastList::GetAlpha(const Toast& toast) {
        const float rising = toast.age / kFadeInSeconds;
        const float falling = (kLifeSeconds - toast.age) / kFadeOutSeconds;
        return eastl::clamp(eastl::min(rising, falling), 0.0f, 1.0f);
    }

}  // namespace BECore
