#pragma once

#include <SDL3/SDL_events.h>

namespace BECore {

    /**
     * @brief A receiver of mouse input that sees an event before game code does (a UI canvas).
     *
     * Every method returns true when the handler used the event: the event then goes to no other handler and not to the game.
     * A handler that holds a pressed button (a UI button between press and release) returns true for the matching up event
     * and for the motion in between, wherever the pointer is.
     */
    class IUIInputHandler {
    public:
        virtual ~IUIInputHandler() = default;

        virtual bool OnMouseButtonDown(const SDL_MouseButtonEvent& /*event*/) {
            return false;
        }

        virtual bool OnMouseButtonUp(const SDL_MouseButtonEvent& /*event*/) {
            return false;
        }

        virtual bool OnMouseMotion(const SDL_MouseMotionEvent& /*event*/) {
            return false;
        }

        virtual bool OnMouseWheel(const SDL_MouseWheelEvent& /*event*/) {
            return false;
        }
    };

    /**
     * @brief The single entry point of mouse input for the engine's own UI, owned by CoreManager.
     *
     * The platform layer (SDLEventsProvider) and the editor viewport (ViewportWidget) do not emit the mouse events of
     * SDLEvents themselves: they call Dispatch*(). The handlers are asked first, topmost first, and the first one that
     * answers true consumes the event. An event nobody consumed is emitted as the matching SDLEvents event for game code, so
     * a click on a button does not also hit the board under it. Positions are in render output pixels, as everywhere else.
     *
     * Registering and unregistering inside a handler is allowed: it takes effect once the dispatch in progress is done, and
     * an unregistered handler is not called again, even by that same dispatch.
     */
    class UIInput {
    public:
        /// Adds a handler. The one with the higher order is asked first, and among equal orders the later registered one; a canvas passes its draw order
        void Register(IUIInputHandler& handler, int order);

        /// Removes a handler; a handler that is not registered is ignored
        void Unregister(IUIInputHandler& handler);

        [[nodiscard]] bool IsRegistered(const IUIInputHandler& handler) const;

        /// @return true when a handler consumed the event, so game code did not get it
        bool DispatchMouseButtonDown(const SDL_MouseButtonEvent& event);
        bool DispatchMouseButtonUp(const SDL_MouseButtonEvent& event);
        bool DispatchMouseMotion(const SDL_MouseMotionEvent& event);
        bool DispatchMouseWheel(const SDL_MouseWheelEvent& event);

    private:
        struct Entry {
            IUIInputHandler* handler = nullptr;  // null: unregistered during a dispatch, dropped when it ends
            int order = 0;
        };

        template <typename GameEvent, typename SDLEventType>
        bool Dispatch(const SDLEventType& event, bool (IUIInputHandler::*handle)(const SDLEventType&));

        void Insert(const Entry& entry);
        void ApplyDeferredChanges();

        eastl::vector<Entry> _entries;
        eastl::vector<Entry> _registeredDuringDispatch;
        int _dispatchDepth = 0;
    };

}  // namespace BECore
