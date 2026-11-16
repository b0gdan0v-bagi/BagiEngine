#pragma once

#include <BECore/Input/PointerTracker.h>
#include <BECore/Types/Types.h>
#include <SDL3/SDL_events.h>

namespace BECore {

    /**
     * @brief A receiver of mouse input that sees an event before game code does (a UI canvas, a scene target).
     *
     * The dispatcher asks a handler only when ContainsPoint holds for the event's position. Every On* method returns true
     * when the handler used the event: the event then goes to no other handler and not to the game.
     *
     * A handler that consumed a press holds it (UIInput keeps the capture): the matching up and the motion of that pointer
     * reach it first, wherever the pointer is and whatever ContainsPoint says, and the up is consumed whatever it answers.
     * So a handler needs no pressed state of its own to get its release, and a motion it answers true to is a drag.
     */
    class IUIInputHandler {
    public:
        virtual ~IUIInputHandler() = default;

        /// Whether the event at this point (render output pixels) is the handler's to answer. The default asks for everything,
        /// as a canvas does: it decides for itself. A scene target answers false outside its shape, where it is hidden or
        /// clipped, so the handler below it gets the event. Called for presses, motions, releases without a capture and wheels
        [[nodiscard]] virtual bool ContainsPoint(float /*outputX*/, float /*outputY*/) const {
            return true;
        }

        /// The pointer `which` was over this handler (the last motion it consumed) and now is over another one or over nothing.
        /// Sent once per change. A held press is not affected: the handler still gets the motion and the up of the capture
        virtual void OnPointerLeave(uint32_t /*which*/) {}

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
     * SDLEvents themselves: they call Dispatch*(). The handlers are asked first, topmost first (the higher order, among
     * equal orders the later registered), only those whose ContainsPoint holds, and the first one that answers true consumes
     * the event. An event nobody consumed is emitted as the matching SDLEvents event for game code, so a click on a button
     * does not also hit the board under it. Positions are in render output pixels, as everywhere else.
     *
     * Capture and hover are kept per pointer (the `which` of the event):
     * - a handler that consumed a press holds it. The up of that button goes to the holder first and is consumed whatever the
     *   holder answers (a release never leaks to the game or to a handler that did not see the press); the motion of a pointer
     *   that holds a press goes to the holder first and, when it answers false, on to the walk without it;
     * - the handler that consumed the last motion of a pointer is its hover. When another handler, or none, takes the pointer
     *   the old one gets OnPointerLeave once. A press consumed by nobody emits the raw event and starts no capture.
     *
     * Registering and unregistering inside a handler is allowed: it takes effect once the dispatch in progress is done, and
     * an unregistered handler is not called again, even by that same dispatch. Unregister forgets the handler's captures and
     * hovers at once and never calls it (a destroyed component is not called). A handler that unregisters itself while it
     * answers true starts no capture. A dispatch makes no container: it allocates nothing.
     */
    class UIInput {
    public:
        /// Adds a handler. The one with the higher order is asked first, and among equal orders the later registered one; a canvas passes its draw order
        void Register(IUIInputHandler& handler, int order);

        /// Removes a handler and forgets its capture and hover without calling it, so a handler that changes its order by
        /// Unregister and Register loses a press it holds (its own press state must survive that) and gets no leave.
        /// A handler that is not registered is ignored
        void Unregister(IUIInputHandler& handler);

        [[nodiscard]] bool IsRegistered(const IUIInputHandler& handler) const;

        /// Whether the handler holds a press (of any button) of the pointer `which`. Asked from a handler's own On* method, it
        /// tells a motion of its drag from the motion of a pointer that merely passes. The press of an up being delivered
        /// is already ended: inside OnMouseButtonUp the answer does not count it
        [[nodiscard]] bool HoldsPress(const IUIInputHandler& handler, uint32_t which) const;

        /// True only inside the OnMouseButtonUp that reaches the handler as the holder of that press. A release that reaches
        /// it by the walk (another button, another pointer, a press nobody held) is not a delivery, so the handler can tell
        /// "the release of my press" from a release that is the game's. A dispatch nested inside the up answers for itself
        [[nodiscard]] bool IsDeliveringCapture(const IUIInputHandler& handler) const;

        /// The pointer `which` is gone (a finger was lifted): the handler it was over gets OnPointerLeave once and the hover is
        /// forgotten. A mouse has no such moment; a press the pointer still holds is not touched
        void EndHover(uint32_t which);

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

        /// The outcome of asking the handlers: `consumed`, and the consumer when it is still registered afterwards
        struct Consumption {
            bool consumed = false;
            IUIInputHandler* handler = nullptr;
        };

        template <typename SDLEventType>
        Consumption Walk(const SDLEventType& event, float x, float y, bool (IUIInputHandler::*handle)(const SDLEventType&), const IUIInputHandler* skip);

        [[nodiscard]] bool IsLive(const IUIInputHandler& handler) const;
        void BeginDispatch();
        void EndDispatch();
        void Insert(const Entry& entry);
        void ApplyDeferredChanges();

        Vector<Entry> _entries;
        Vector<Entry> _registeredDuringDispatch;
        PointerTracker _tracker;
        int _dispatchDepth = 0;
        const IUIInputHandler* _deliveringHolder = nullptr;  // the holder DispatchMouseButtonUp is calling now; compared only, never called
    };

}  // namespace BECore
