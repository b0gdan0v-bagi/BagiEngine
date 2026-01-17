#pragma once

#include <Events/EventBase.h>
#include <SDL3/SDL.h>

namespace BECore {
namespace SDLEvents {

    // События клавиатуры
    struct KeyDownEvent : public EventBase<KeyDownEvent> {
        SDL_Keycode key;
        SDL_Keymod mod;
        bool repeat;

        KeyDownEvent(SDL_Keycode k, SDL_Keymod m, bool r) : key(k), mod(m), repeat(r) {}
    };

    struct KeyUpEvent : public EventBase<KeyUpEvent> {
        SDL_Keycode key;
        SDL_Keymod mod;

        KeyUpEvent(SDL_Keycode k, SDL_Keymod m) : key(k), mod(m) {}
    };

    // События мыши
    struct MouseButtonDownEvent : public EventBase<MouseButtonDownEvent> {
        SDL_MouseButtonEvent button;

        explicit MouseButtonDownEvent(const SDL_MouseButtonEvent& b) : button(b) {}
    };

    struct MouseButtonUpEvent : public EventBase<MouseButtonUpEvent> {
        SDL_MouseButtonEvent button;

        explicit MouseButtonUpEvent(const SDL_MouseButtonEvent& b) : button(b) {}
    };

    struct MouseMotionEvent : public EventBase<MouseMotionEvent> {
        SDL_MouseMotionEvent motion;

        explicit MouseMotionEvent(const SDL_MouseMotionEvent& m) : motion(m) {}
    };

    struct MouseWheelEvent : public EventBase<MouseWheelEvent> {
        SDL_MouseWheelEvent wheel;

        explicit MouseWheelEvent(const SDL_MouseWheelEvent& w) : wheel(w) {}
    };

    // События окна
    struct WindowEvent : public EventBase<WindowEvent> {
        SDL_WindowEvent window;

        explicit WindowEvent(const SDL_WindowEvent& w) : window(w) {}
    };

    // Обёртка для любого SDL события (для совместимости)
    struct SDLEventWrapper : public EventBase<SDLEventWrapper> {
        SDL_Event event;

        explicit SDLEventWrapper(const SDL_Event& e) : event(e) {}
    };

}  // namespace SDLEvents
}  // namespace BECore

