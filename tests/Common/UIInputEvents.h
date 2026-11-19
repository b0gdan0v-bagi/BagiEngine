#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SubscriptionHolder.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace BECore::Tests {

    /// A mouse button event at (@p x, @p y) as SDL delivers it; feed it to `CoreManager::GetUIInput().DispatchMouseButtonDown/Up`
    [[nodiscard]] inline SDL_MouseButtonEvent Button(float x, float y, bool down, uint8_t button = SDL_BUTTON_LEFT) {
        SDL_MouseButtonEvent event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button = button;
        event.down = down;
        event.x = x;
        event.y = y;
        return event;
    }

    /// A mouse motion to (@p x, @p y); @p held is the mask of the buttons held down (`SDL_BUTTON_LMASK` for a drag)
    [[nodiscard]] inline SDL_MouseMotionEvent Motion(float x, float y, SDL_MouseButtonFlags held = 0) {
        SDL_MouseMotionEvent event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.x = x;
        event.y = y;
        event.state = held;
        return event;
    }

    /// A left press at (@p x, @p y) delivered the way the window does: through UIInput, of which a clickable is a target
    inline void PressLeftAt(float x, float y) {
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
    }

    /// Every `InputEvents::ActionEvent` emitted while it is alive, with its value, in order
    class ActionLog : public SubscriptionHolder {
    public:
        struct Entry {
            PoolString action;
            PoolString value;
        };

        ActionLog() {
            Subscribe<InputEvents::ActionEvent, &ActionLog::OnAction>(this);
        }

        [[nodiscard]] const Vector<Entry>& All() const {
            return _entries;
        }

        [[nodiscard]] size_t Total() const {
            return _entries.size();
        }

        /// How many times @p action was emitted
        [[nodiscard]] size_t Count(StringView action) const {
            return static_cast<size_t>(std::count_if(_entries.begin(), _entries.end(), [action](const Entry& entry) { return entry.action.ToStringView() == action; }));
        }

        /// The action of the entry @p index (an empty one past the end)
        [[nodiscard]] PoolString ActionAt(size_t index) const {
            return index < _entries.size() ? _entries[index].action : PoolString{};
        }

        /// The last action, empty when there was none
        [[nodiscard]] PoolString LastAction() const {
            return _entries.empty() ? PoolString{} : _entries.back().action;
        }

        /// The value of the last action, empty when there was none
        [[nodiscard]] PoolString LastValue() const {
            return _entries.empty() ? PoolString{} : _entries.back().value;
        }

        /// The values of @p action, in order, joined with spaces
        [[nodiscard]] String Values(StringView action) const {
            String joined;
            for (const Entry& entry : _entries) {
                if (entry.action.ToStringView() != action) {
                    continue;
                }
                if (!joined.empty()) {
                    joined += " ";
                }
                joined.append(entry.value.ToStringView().data(), entry.value.ToStringView().size());
            }
            return joined;
        }

        void Clear() {
            _entries.clear();
        }

    private:
        void OnAction(const InputEvents::ActionEvent& event) {
            _entries.push_back({event.action, event.value});
        }

        Vector<Entry> _entries;
    };

}  // namespace BECore::Tests
