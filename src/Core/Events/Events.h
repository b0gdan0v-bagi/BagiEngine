#pragma once

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
#include <utility>

namespace Math {
    struct Color;
}

namespace Core {

    struct BaseEvent {
        virtual ~BaseEvent() = default;
    };

    // CRTP базовый класс для статических методов событий с индивидуальным dispatcher'ом
    template <typename Derived>
    struct EventBase : public BaseEvent {
        static entt::dispatcher& GetDispatcher() {
            static entt::dispatcher dispatcher;
            return dispatcher;
        }

        template <auto Candidate, typename Type>
        static void Subscribe(Type* instance) {
            GetDispatcher().sink<Derived>().template connect<Candidate>(instance);
        }

        // Статические методы для отправки
        // Для событий без параметров: QuitEvent::Emit()
        static void Emit() requires (std::is_default_constructible_v<Derived>) {
            GetDispatcher().trigger(Derived{});
        }

        template <typename... Args>
        static void Emit(Args&&... args) {
            GetDispatcher().trigger(Derived{std::forward<Args>(args)...});
        }

        static void Enqueue() requires (std::is_default_constructible_v<Derived>) {
            GetDispatcher().enqueue(Derived{});
        }

        template <typename... Args>
        static void Enqueue(Args&&... args) {
            GetDispatcher().enqueue(Derived{std::forward<Args>(args)...});
        }

        static void Update() {
            GetDispatcher().update();
        }
    };

    // События наследуются от EventBase с самим собой как параметр
    struct QuitEvent : public EventBase<QuitEvent> {};
    struct NewFrameEvent : public EventBase<NewFrameEvent> {};
    struct RenderClearEvent : public EventBase<RenderClearEvent> {};
    struct RenderPresentEvent : public EventBase<RenderPresentEvent> {};
    struct ApplicationCleanUpEvent : public EventBase<ApplicationCleanUpEvent> {};
    
    struct SetRenderDrawColorEvent : public EventBase<SetRenderDrawColorEvent> {
        constexpr explicit SetRenderDrawColorEvent(Math::Color color_) : color(color_) {}
        constexpr explicit SetRenderDrawColorEvent(unsigned char r, unsigned char g, unsigned char b, unsigned char a) : color(Math::Color{r, g, b, a}) {}
        Math::Color color;
    };

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

}  // namespace Core
