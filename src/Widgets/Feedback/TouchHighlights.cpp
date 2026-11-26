#include "TouchHighlights.h"

#include <BECore/Control/ControlTouches.h>
#include <Widgets/Feedback/UIMotion.h>

namespace BECore {

    namespace {

        /// The entry of the key among the first @p count of @p slots; else a free one, else the one reported longest ago, which then holds the key
        template <typename TSlot>
        TSlot& Place(Vector<TSlot>& slots, size_t& count, const ControlTouch& touch) {
            TSlot* oldest = nullptr;
            for (size_t i = 0; i < count; ++i) {
                TSlot& slot = slots[i];
                if (slot.Is(touch.nodeId, touch.component, touch.field)) {
                    slot.sequence = touch.sequence;
                    return slot;
                }
                if (oldest == nullptr || slot.sequence < oldest->sequence) {
                    oldest = &slot;
                }
            }
            TSlot& slot = count < slots.size() ? slots[count++] : *oldest;
            slot.sequence = touch.sequence;
            slot.nodeId = touch.nodeId;
            slot.component = touch.component;
            slot.field = touch.field;
            return slot;
        }

        /// Frees the live entry @p index: the last one takes its place, and the buffers of its strings stay in the table
        template <typename TSlot>
        void Remove(Vector<TSlot>& slots, size_t& count, size_t index) {
            --count;
            if (index != count) {
                eastl::swap(slots[index], slots[count]);
            }
        }

    }  // namespace

    void TouchHighlights::Connect(const ControlTouches& touches) {
        Reset();
        _touches = &touches;
        _nextSequence = touches.GetNextSequence();
    }

    void TouchHighlights::Update(float seconds) {
        for (size_t i = 0; i < _flashCount;) {
            _flashes[i].age += seconds;
            if (_flashes[i].age < kTouchFlashSeconds) {
                ++i;
            } else {
                Remove(_flashes, _flashCount, i);
            }
        }
        ReadTouches();
    }

    void TouchHighlights::Reset() {
        for (Mark& mark : _marks) {
            mark.client = {};
        }
        _flashCount = 0;
        _markCount = 0;
        _touches = nullptr;
    }

    template <typename TMatch>
    float TouchHighlights::StrongestAlpha(const TMatch& matches) const {
        float alpha = 0.0f;
        for (size_t i = 0; i < _flashCount; ++i) {
            const Flash& flash = _flashes[i];
            if (matches(flash)) {
                alpha = eastl::max(alpha, EaseOut(1.0f - flash.age / kTouchFlashSeconds));
            }
        }
        return alpha;
    }

    float TouchHighlights::NodeAlpha(uint64_t nodeId) const {
        return StrongestAlpha([nodeId](const Flash& flash) { return flash.nodeId == nodeId; });
    }

    float TouchHighlights::ComponentAlpha(uint64_t nodeId, StringView component) const {
        return StrongestAlpha([nodeId, component](const Flash& flash) { return flash.nodeId == nodeId && StringView(flash.component) == component; });
    }

    float TouchHighlights::FieldAlpha(uint64_t nodeId, StringView component, StringView field) const {
        return StrongestAlpha([nodeId, component, field](const Flash& flash) { return flash.Is(nodeId, component, field); });
    }

    const ControlClient* TouchHighlights::FindMark(uint64_t nodeId, StringView component, StringView field) const {
        for (size_t i = 0; i < _markCount; ++i) {
            if (_marks[i].Is(nodeId, component, field)) {
                return _marks[i].client.Get();
            }
        }
        return nullptr;
    }

    void TouchHighlights::ForEachMark(uint64_t nodeId, StringView component, FunctionRef<void(StringView field, const ControlClient& client)> visit) const {
        for (size_t i = 0; i < _markCount; ++i) {
            const Mark& mark = _marks[i];
            if (mark.nodeId == nodeId && StringView(mark.component) == component) {
                visit(mark.field, *mark.client);
            }
        }
    }

    void TouchHighlights::Dismiss(uint64_t nodeId, StringView component, StringView field) {
        for (size_t i = 0; i < _markCount; ++i) {
            if (_marks[i].Is(nodeId, component, field)) {
                RemoveMark(i);
                return;
            }
        }
    }

    void TouchHighlights::DismissComponent(uint64_t nodeId, StringView component) {
        for (size_t i = 0; i < _markCount;) {
            if (_marks[i].nodeId == nodeId && StringView(_marks[i].component) == component) {
                RemoveMark(i);
            } else {
                ++i;
            }
        }
    }

    void TouchHighlights::RemoveMark(size_t index) {
        _marks[index].client = {};
        Remove(_marks, _markCount, index);
    }

    void TouchHighlights::ReadTouches() {
        if (_touches == nullptr || _touches->GetNextSequence() == _nextSequence) {
            return;
        }
        size_t first = _touches->Size();
        while (first > 0 && _touches->At(first - 1).sequence >= _nextSequence) {
            --first;
        }
        for (size_t i = first; i < _touches->Size(); ++i) {
            const ControlTouch& touch = _touches->At(i);
            Place(_flashes, _flashCount, touch).age = 0.0f;
            if (!touch.field.empty()) {
                Place(_marks, _markCount, touch).client = touch.client;
            }
        }
        _nextSequence = _touches->GetNextSequence();
    }

}  // namespace BECore
