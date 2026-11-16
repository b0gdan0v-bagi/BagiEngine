#include "UIInput.h"

#include <CoreSDL/SDLEvents.h>
#include <EASTL/algorithm.h>

namespace BECore {

    void UIInput::Register(IUIInputHandler& handler, int order) {
        ASSERT(!IsRegistered(handler), "UIInput: the handler is registered twice");
        if (IsRegistered(handler)) {
            return;
        }

        const Entry entry{&handler, order};
        if (_dispatchDepth > 0) {
            _registeredDuringDispatch.push_back(entry);
        } else {
            Insert(entry);
        }
    }

    void UIInput::Unregister(IUIInputHandler& handler) {
        // At once, even inside a dispatch: the tracker only compares pointers, and a handler that is going away must not be
        // given a leave or an up afterwards
        _tracker.Forget(handler);

        auto pending = eastl::find_if(_registeredDuringDispatch.begin(), _registeredDuringDispatch.end(), [&](const Entry& entry) { return entry.handler == &handler; });
        if (pending != _registeredDuringDispatch.end()) {
            _registeredDuringDispatch.erase(pending);
        }

        auto it = eastl::find_if(_entries.begin(), _entries.end(), [&](const Entry& entry) { return entry.handler == &handler; });
        if (it == _entries.end()) {
            return;
        }
        if (_dispatchDepth > 0) {
            it->handler = nullptr;  // the dispatch walks _entries by index, so it must not shift
        } else {
            _entries.erase(it);
        }
    }

    bool UIInput::IsRegistered(const IUIInputHandler& handler) const {
        const auto isHandler = [&](const Entry& entry) { return entry.handler == &handler; };
        return eastl::any_of(_entries.begin(), _entries.end(), isHandler) || eastl::any_of(_registeredDuringDispatch.begin(), _registeredDuringDispatch.end(), isHandler);
    }

    bool UIInput::HoldsPress(const IUIInputHandler& handler, uint32_t which) const {
        return _tracker.HoldsPress(handler, which);
    }

    bool UIInput::IsDeliveringCapture(const IUIInputHandler& handler) const {
        return _deliveringHolder == &handler;
    }

    bool UIInput::IsLive(const IUIInputHandler& handler) const {
        return eastl::any_of(_entries.begin(), _entries.end(), [&](const Entry& entry) { return entry.handler == &handler; });
    }

    template <typename SDLEventType>
    UIInput::Consumption UIInput::Walk(const SDLEventType& event, float x, float y, bool (IUIInputHandler::*handle)(const SDLEventType&), const IUIInputHandler* skip) {
        for (size_t i = 0; i < _entries.size(); ++i) {
            IUIInputHandler* handler = _entries[i].handler;
            if (handler == nullptr || handler == skip || !handler->ContainsPoint(x, y)) {
                continue;
            }
            if ((handler->*handle)(event)) {
                // It may have unregistered itself while answering; its entry is then empty and it must not be remembered
                return Consumption{true, _entries[i].handler == handler ? handler : nullptr};
            }
        }
        return Consumption{};
    }

    void UIInput::BeginDispatch() {
        ++_dispatchDepth;
    }

    void UIInput::EndDispatch() {
        --_dispatchDepth;
        if (_dispatchDepth == 0) {
            ApplyDeferredChanges();
        }
    }

    bool UIInput::DispatchMouseButtonDown(const SDL_MouseButtonEvent& event) {
        BeginDispatch();
        const Consumption result = Walk(event, event.x, event.y, &IUIInputHandler::OnMouseButtonDown, nullptr);
        if (result.handler != nullptr) {
            _tracker.BeginCapture(event.which, event.button, *result.handler);
        }
        EndDispatch();

        if (!result.consumed) {
            SDLEvents::MouseButtonDownEvent::Emit(event);
        }
        return result.consumed;
    }

    bool UIInput::DispatchMouseButtonUp(const SDL_MouseButtonEvent& event) {
        BeginDispatch();
        bool consumed = false;
        // A dispatch nested in a handler's up has its own answer to IsDeliveringCapture, so the outer holder is put back after
        const IUIInputHandler* const outerHolder = _deliveringHolder;
        if (IUIInputHandler* holder = _tracker.FindCapture(event.which, event.button); holder != nullptr) {
            // Ended first: the holder's answer does not matter and it may unregister or dispatch again inside the call
            _tracker.EndCapture(event.which, event.button);
            _deliveringHolder = holder;
            holder->OnMouseButtonUp(event);
            consumed = true;
        } else {
            _deliveringHolder = nullptr;
            consumed = Walk(event, event.x, event.y, &IUIInputHandler::OnMouseButtonUp, nullptr).consumed;
        }
        _deliveringHolder = outerHolder;
        EndDispatch();

        if (!consumed) {
            SDLEvents::MouseButtonUpEvent::Emit(event);
        }
        return consumed;
    }

    bool UIInput::DispatchMouseMotion(const SDL_MouseMotionEvent& event) {
        BeginDispatch();

        Consumption result;
        IUIInputHandler* holder = _tracker.FindCaptureOfPointer(event.which);
        if (holder != nullptr && holder->OnMouseMotion(event)) {
            result = Consumption{true, IsLive(*holder) ? holder : nullptr};
        } else {
            result = Walk(event, event.x, event.y, &IUIInputHandler::OnMouseMotion, holder);
        }

        // The old hover is told after the new one has answered: who consumes is known only from the answer
        IUIInputHandler* previous = _tracker.SetHover(event.which, result.handler);
        if (previous != nullptr && previous != result.handler) {
            previous->OnPointerLeave(event.which);
        }
        EndDispatch();

        if (!result.consumed) {
            SDLEvents::MouseMotionEvent::Emit(event);
        }
        return result.consumed;
    }

    bool UIInput::DispatchMouseWheel(const SDL_MouseWheelEvent& event) {
        BeginDispatch();
        const bool consumed = Walk(event, event.mouse_x, event.mouse_y, &IUIInputHandler::OnMouseWheel, nullptr).consumed;
        EndDispatch();

        if (!consumed) {
            SDLEvents::MouseWheelEvent::Emit(event);
        }
        return consumed;
    }

    void UIInput::Insert(const Entry& entry) {
        // Descending by order; a later registered handler goes in front of the ones with the same order
        auto position = eastl::find_if(_entries.begin(), _entries.end(), [&](const Entry& other) { return other.order <= entry.order; });
        _entries.insert(position, entry);
    }

    void UIInput::ApplyDeferredChanges() {
        _entries.erase(eastl::remove_if(_entries.begin(), _entries.end(), [](const Entry& entry) { return entry.handler == nullptr; }), _entries.end());

        for (const Entry& entry : _registeredDuringDispatch) {
            Insert(entry);
        }
        _registeredDuringDispatch.clear();
    }

}  // namespace BECore
