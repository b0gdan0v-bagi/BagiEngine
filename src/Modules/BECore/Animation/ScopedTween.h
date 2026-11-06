#pragma once

#include <BECore/Animation/Tween.h>

namespace BECore {

    /**
     * @brief Owns one tween and cancels it when it goes out of scope: the member of the object whose fields the setter writes.
     *
     * The destructor, `Reset` and a move-assignment CANCEL the tween they held, and the cancel callback of that tween runs (like
     * `TweenHandle::Cancel`); a tween that has finished already is not touched. Not copyable. Main thread only.
     */
    class ScopedTween {
    public:
        ScopedTween() = default;

        explicit ScopedTween(IntrusivePtr<TweenHandle> handle) : _handle(std::move(handle)) {}

        ScopedTween(ScopedTween&& other) noexcept : _handle(std::move(other._handle)) {}

        ScopedTween& operator=(ScopedTween&& other) noexcept {
            if (this != &other) {
                Reset(std::move(other._handle));
            }
            return *this;
        }

        ScopedTween(const ScopedTween&) = delete;
        ScopedTween& operator=(const ScopedTween&) = delete;

        ~ScopedTween() {
            Reset();
        }

        /// Cancels the tween held now (when it still runs) and holds @p handle instead (nothing when it is empty)
        void Reset(IntrusivePtr<TweenHandle> handle = {}) {
            // The new handle is taken first: a cancel callback that reaches this object sees the new state
            IntrusivePtr<TweenHandle> old = std::move(_handle);
            _handle = std::move(handle);
            if (old) {
                old->Cancel();
            }
        }

        [[nodiscard]] const IntrusivePtr<TweenHandle>& Get() const {
            return _handle;
        }

    private:
        IntrusivePtr<TweenHandle> _handle;
    };

}  // namespace BECore
