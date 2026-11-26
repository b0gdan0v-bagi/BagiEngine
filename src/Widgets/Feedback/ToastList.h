#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/Feedback/ToastEvents.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    struct Toast {
        String text;
        ToastLevel level = ToastLevel::Info;
        uint32_t count = 1;  ///< how many times it was pushed while it lived
        float age = 0.0f;    ///< seconds since it appeared, set back by a repeat
    };

    /**
     * @brief The notifications that live now, oldest first: at most kCapacity, each for kLifeSeconds. No ImGui.
     *
     * The slots and the capacity of their texts are kept, so a list whose texts have been seen before takes a toast, ages and drops it with no allocation.
     */
    class ToastList {
    public:
        static constexpr size_t kCapacity = 5;
        static constexpr float kLifeSeconds = 5.0f;
        static constexpr float kFadeInSeconds = 0.15f;
        static constexpr float kFadeOutSeconds = 0.6f;

        /// A live toast of this level and text counts one more, becomes the newest and starts its life again without fading in a second time.
        /// Otherwise a new one is added; the oldest leaves a full list
        void Push(ToastLevel level, StringView text);

        /// Ages every toast by @p seconds and drops those whose life is over
        void Advance(float seconds);

        [[nodiscard]] size_t Size() const {
            return _size;
        }

        /// @p index 0 is the oldest; below Size(). Good until the next Push() or Advance()
        [[nodiscard]] const Toast& At(size_t index) const {
            return _toasts[index];
        }

        /// The opacity at its age, 0 to 1: rises at the start of the life and falls at its end
        [[nodiscard]] static float GetAlpha(const Toast& toast);

    private:
        Array<Toast, kCapacity> _toasts;
        size_t _size = 0;
    };

}  // namespace BECore
