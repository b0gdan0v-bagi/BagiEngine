#pragma once

#include <SDL3/SDL.h>
#include <entt/entt.hpp>

namespace Core {

    struct BaseEvent {
        virtual ~BaseEvent() = default;
    };

    struct QuitEvent : public BaseEvent {};
    struct NewFrameEvent : public BaseEvent {};
    struct RenderClearEvent : public BaseEvent {};
    struct RenderPresentEvent : public BaseEvent {};
    struct ApplicationCleanUpEvent : public BaseEvent {};

    // События клавиатуры
    struct KeyDownEvent : public BaseEvent {
        SDL_Keycode key;
        SDL_Keymod mod;
        bool repeat;

        KeyDownEvent(SDL_Keycode k, SDL_Keymod m, bool r) : key(k), mod(m), repeat(r) {}
    };

    struct KeyUpEvent : public BaseEvent {
        SDL_Keycode key;
        SDL_Keymod mod;

        KeyUpEvent(SDL_Keycode k, SDL_Keymod m) : key(k), mod(m) {}
    };

    // События мыши
    struct MouseButtonDownEvent : public BaseEvent {
        SDL_MouseButtonEvent button;

        explicit MouseButtonDownEvent(const SDL_MouseButtonEvent& b) : button(b) {}
    };

    struct MouseButtonUpEvent : public BaseEvent {
        SDL_MouseButtonEvent button;

        explicit MouseButtonUpEvent(const SDL_MouseButtonEvent& b) : button(b) {}
    };

    struct MouseMotionEvent : public BaseEvent {
        SDL_MouseMotionEvent motion;

        explicit MouseMotionEvent(const SDL_MouseMotionEvent& m) : motion(m) {}
    };

    struct MouseWheelEvent : public BaseEvent {
        SDL_MouseWheelEvent wheel;

        explicit MouseWheelEvent(const SDL_MouseWheelEvent& w) : wheel(w) {}
    };

    // События окна
    struct WindowEvent : public BaseEvent {
        SDL_WindowEvent window;

        explicit WindowEvent(const SDL_WindowEvent& w) : window(w) {}
    };

    // Обёртка для любого SDL события (для совместимости)
    struct SDLEventWrapper : public BaseEvent {
        SDL_Event event;

        explicit SDLEventWrapper(const SDL_Event& e) : event(e) {}
    };

}  // namespace Core
