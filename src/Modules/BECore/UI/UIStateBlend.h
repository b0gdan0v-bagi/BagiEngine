#pragma once

#include <BECore/Animation/Easing.h>
#include <BECore/UI/UIStateLook.h>

namespace BECore {

    /**
     * @brief A value that eases from what is shown to a new target: the smooth hover and press of a control. Pure and plain data (no heap), so it lives as a runtime member of an element
     * or a component; it is not saved and not copied with the element.
     *
     * `Update` is called at every draw with the target of the state, the time (`CoreManager::GetTime().GetTotalTime()`) and the duration, and answers what to draw:
     * - the first call snaps to the target (a screen that opens under the pointer does not animate);
     * - the same target changes nothing; a **new** target starts from what is shown at @p now, so a hover cut by a press is continuous;
     * - a @p duration of 0 or less (or NaN) snaps, and the value is the target itself, untouched;
     * - at `now >= start + duration` the value is exactly the target.
     *
     * Made for `T` = `Color` (a tint, a text colour) and `T` = `UIStateLook::Look` (a box); other types do not link.
     */
    template <typename T>
    class UIBlend {
    public:
        /// The value to draw at @p now for @p target. The reference stays valid until the next call
        const T& Update(const T& target, double now, float duration, EaseType ease);

        /// `Update` when @p duration is above 0, else @p target itself, untouched, and the blend forgets what it showed, so the next change of the duration starts from the target and does not
        /// replay an old one. What a draw calls: a control with no transition costs one compare. The reference is @p target or the blend's own value
        const T& Follow(const T& target, double now, float duration, EaseType ease);

        /// Forgets the value and the blend on its way: the next `Update` snaps
        void Reset() {
            _started = false;
            _running = false;
        }

        /// What the last `Update` returned (a default value before the first call)
        [[nodiscard]] const T& Shown() const {
            return _shown;
        }

        /// A blend is on: the value is on its way and has not reached the target
        [[nodiscard]] bool IsRunning() const {
            return _running;
        }

    private:
        T _from{};
        T _target{};
        T _shown{};
        double _start = 0.0;
        float _length = 0.0f;
        EaseType _ease = EaseType::QuadOut;
        bool _started = false;
        bool _running = false;
    };

    extern template class UIBlend<Color>;
    extern template class UIBlend<UIStateLook::Look>;

}  // namespace BECore
