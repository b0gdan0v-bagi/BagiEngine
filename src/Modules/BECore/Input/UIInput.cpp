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

    template <typename GameEvent, typename SDLEventType>
    bool UIInput::Dispatch(const SDLEventType& event, bool (IUIInputHandler::*handle)(const SDLEventType&)) {
        bool consumed = false;

        ++_dispatchDepth;
        for (size_t i = 0; i < _entries.size() && !consumed; ++i) {
            if (IUIInputHandler* handler = _entries[i].handler; handler != nullptr) {
                consumed = (handler->*handle)(event);
            }
        }
        --_dispatchDepth;

        if (_dispatchDepth == 0) {
            ApplyDeferredChanges();
        }
        if (!consumed) {
            GameEvent::Emit(event);
        }
        return consumed;
    }

    bool UIInput::DispatchMouseButtonDown(const SDL_MouseButtonEvent& event) {
        return Dispatch<SDLEvents::MouseButtonDownEvent>(event, &IUIInputHandler::OnMouseButtonDown);
    }

    bool UIInput::DispatchMouseButtonUp(const SDL_MouseButtonEvent& event) {
        return Dispatch<SDLEvents::MouseButtonUpEvent>(event, &IUIInputHandler::OnMouseButtonUp);
    }

    bool UIInput::DispatchMouseMotion(const SDL_MouseMotionEvent& event) {
        return Dispatch<SDLEvents::MouseMotionEvent>(event, &IUIInputHandler::OnMouseMotion);
    }

    bool UIInput::DispatchMouseWheel(const SDL_MouseWheelEvent& event) {
        return Dispatch<SDLEvents::MouseWheelEvent>(event, &IUIInputHandler::OnMouseWheel);
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
