#include "UIMotion.h"

namespace BECore {

    namespace {

        /// Whether a slot advanced in @p last still counts in @p frame: the same frame or the one before
        bool IsRecent(int last, int frame) {
            return frame == last || frame == last + 1;
        }

    }  // namespace

    float Approach(float value, float target, float seconds, float duration) {
        const float step = seconds / duration;
        return value < target ? eastl::min(value + step, target) : eastl::max(value - step, target);
    }

    float EaseOut(float progress) {
        // Not `1 - (1 - t)^2`, which rounds a tiny progress to 0
        return progress * (2.0f - progress);
    }

    float HoverBlendTable::Get(uint32_t item, int frame) const {
        for (const Slot& slot : _slots) {
            if (slot.progress > 0.0f && slot.item == item) {
                return IsRecent(slot.frame, frame) ? slot.progress : 0.0f;
            }
        }
        return 0.0f;
    }

    void HoverBlendTable::Advance(uint32_t item, bool hovered, float seconds, int frame) {
        Slot* held = nullptr;
        for (Slot& slot : _slots) {
            if (slot.progress > 0.0f && slot.item == item) {
                held = &slot;
                break;
            }
        }
        const float from = held != nullptr && IsRecent(held->frame, frame) ? held->progress : 0.0f;
        const float progress = Approach(from, hovered ? 1.0f : 0.0f, seconds, kHoverBlendSeconds);
        if (held == nullptr) {
            if (progress <= 0.0f) {
                return;
            }
            held = &Take(frame);
        }
        *held = Slot{item, frame, progress};
    }

    size_t HoverBlendTable::Size() const {
        size_t size = 0;
        for (const Slot& slot : _slots) {
            size += slot.progress > 0.0f ? 1 : 0;
        }
        return size;
    }

    HoverBlendTable::Slot& HoverBlendTable::Take(int frame) {
        Slot* faintest = &_slots[0];
        for (Slot& slot : _slots) {
            if (slot.progress <= 0.0f || !IsRecent(slot.frame, frame)) {
                return slot;
            }
            if (slot.progress < faintest->progress) {
                faintest = &slot;
            }
        }
        return *faintest;
    }

}  // namespace BECore
