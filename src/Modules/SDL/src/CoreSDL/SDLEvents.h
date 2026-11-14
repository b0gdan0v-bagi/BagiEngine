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

        /// Typed text, already UTF-8 (SDL sends it only between SDL_StartTextInput and SDL_StopTextInput)
        struct TextInputEvent : public EventBase<TextInputEvent> {
            eastl::string text;

            explicit TextInputEvent(StringView t) : text(t.data(), t.size()) {}
        };

        // События окна
        // Resize, pixel size, display scale, move, focus and close request; window.type says which
        struct WindowEvent : public EventBase<WindowEvent> {
            SDL_WindowEvent window;

            explicit WindowEvent(const SDL_WindowEvent& w) : window(w) {}
        };

        /// The area that notches and system bars leave free has changed: read it from IMainWindow::GetSafeArea()
        struct SafeAreaChangedEvent : public EventBase<SafeAreaChangedEvent> {};

        /// The display has been rotated (a phone or tablet turned)
        struct DisplayOrientationChangedEvent : public EventBase<DisplayOrientationChangedEvent> {
            SDL_DisplayID display;
            SDL_DisplayOrientation orientation;

            DisplayOrientationChangedEvent(SDL_DisplayID d, SDL_DisplayOrientation o) : display(d), orientation(o) {}
        };

        // Обёртка для любого SDL события (для совместимости)
        struct SDLEventWrapper : public EventBase<SDLEventWrapper> {
            SDL_Event event;

            explicit SDLEventWrapper(const SDL_Event& e) : event(e) {}
        };

    }  // namespace SDLEvents
}  // namespace BECore
