#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    // The motion of the editor: values that move by the frame's time, so the manual clock repeats it. No ImGui, and nothing allocates

    /// How long an asset panel takes to fade in, and to fade out
    inline constexpr float kPanelFadeSeconds = 0.15f;
    /// How long the command palette takes to fade in
    inline constexpr float kPaletteFadeSeconds = 0.1f;
    /// How long a button takes to blend into its hover colour, and back
    inline constexpr float kHoverBlendSeconds = 0.12f;
    /// How long what a client of the control API touched flashes
    inline constexpr float kTouchFlashSeconds = 1.0f;

    /// @p value moved toward @p target by the share that @p seconds are of @p duration (above 0), never past it
    [[nodiscard]] float Approach(float value, float target, float seconds, float duration);

    /// Fast at the start and slow at the end, over 0..1. Above 0 for every progress above 0, so a first frame of next to no time is still drawn
    [[nodiscard]] float EaseOut(float progress);

    /// Something that fades in while it is shown and out while it is not. Starts hidden
    class Fade {
    public:
        /// The end of the fade at once
        void Snap(bool shown) {
            _progress = shown ? 1.0f : 0.0f;
        }

        /// A frame of @p seconds on the way to @p shown; a turn midway goes back from where it is
        void Advance(bool shown, float seconds, float duration) {
            _progress = Approach(_progress, shown ? 1.0f : 0.0f, seconds, duration);
        }

        [[nodiscard]] float Alpha() const {
            return EaseOut(_progress);
        }

        /// Faded out all the way
        [[nodiscard]] bool IsHidden() const {
            return _progress <= 0.0f;
        }

    private:
        float _progress = 0.0f;
    };

    /**
     * @brief The few items that are blending into a hover state or out of it, by id; an item at rest holds no slot.
     *
     * An item is advanced in every frame that draws it. One that missed a frame starts from rest again, so a button whose window went away under the
     * pointer does not come back lit.
     */
    class HoverBlendTable {
    public:
        /// More than the pointer can leave blending at once; past it the faintest item gives its slot away
        static constexpr size_t kCapacity = 16;

        /// How far @p item is blended in @p frame, 0 at rest to 1
        [[nodiscard]] float Get(uint32_t item, int frame) const;

        /// Moves @p item toward 1 while @p hovered and toward 0 otherwise, once per frame that draws it
        void Advance(uint32_t item, bool hovered, float seconds, int frame);

        /// The items that are not at rest, those that were not drawn lately included
        [[nodiscard]] size_t Size() const;

    private:
        struct Slot {
            uint32_t item = 0;
            int frame = 0;          ///< the last one that advanced it
            float progress = 0.0f;  ///< 0: the slot is free
        };

        /// The slot of a new item: a free one, else one nothing drew lately, else the faintest
        [[nodiscard]] Slot& Take(int frame);

        Array<Slot, kCapacity> _slots{};
    };

}  // namespace BECore
