#include "SDLEventsProvider.h"

#include "Application/Application.h"
#include <Core/Events/Events.h>

namespace Core {

    bool SDLEventsProvider::Initialize() {
        if (_isInitialized) {
            return true;
        }

        _isInitialized = true;
        return true;
    }

    void SDLEventsProvider::Destroy() {
        _isInitialized = false;
    }

    void SDLEventsProvider::ProcessEvents() {
        if (!_isInitialized) {
            return;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ConvertSDLEvent(event);
        }
    }

    void SDLEventsProvider::ConvertSDLEvent(const SDL_Event& sdlEvent) {
        auto& eventsManager = Application::GetEventManager();
        switch (sdlEvent.type) {
            case SDL_EVENT_QUIT:
                eventsManager.Emit(QuitEvent{});
                break;

            /*case SDL_EVENT_KEY_DOWN: {
                const auto& keyEvent = sdlEvent.key;
                eventsManager.Emit(KeyDownEvent{
                    keyEvent.key,
                    static_cast<SDL_Keymod>(keyEvent.mod),
                    keyEvent.repeat != 0
                });
                break;
            }

            case SDL_EVENT_KEY_UP: {
                const auto& keyEvent = sdlEvent.key;
                eventsManager.Emit(KeyUpEvent{
                    keyEvent.key,
                    static_cast<SDL_Keymod>(keyEvent.mod)
                });
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                eventsManager.Emit(MouseButtonDownEvent{sdlEvent.button});
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                eventsManager.Emit(MouseButtonUpEvent{sdlEvent.button});
                break;

            case SDL_EVENT_MOUSE_MOTION:
                eventsManager.Emit(MouseMotionEvent{sdlEvent.motion});
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                eventsManager.Emit(MouseWheelEvent{sdlEvent.wheel});
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                eventsManager.Emit(WindowEvent{sdlEvent.window});
                break;
                */

            default:
                // Для всех остальных событий отправляем обёртку
                eventsManager.Emit(SDLEventWrapper{sdlEvent});
                break;
        }
    }

} // namespace Core

