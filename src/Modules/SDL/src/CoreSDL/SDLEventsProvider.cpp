#include "SDLEventsProvider.h"

#include "Application/Application.h"

#include <BECore/GameManager/CoreManager.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/ApplicationEvents.h>
#include <imgui.h>

namespace BECore {

    namespace {

        // The Game run mode has no ImGui context at all, and ImGui::GetIO() needs one.
        bool IsMouseCapturedByUI() {
            return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
        }

        bool IsKeyboardCapturedByUI() {
            return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
        }

    }  // namespace

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
        SDLEvents::SDLEventWrapper::Emit(sdlEvent);

        switch (sdlEvent.type) {
            case SDL_EVENT_QUIT:
                ApplicationEvents::QuitEvent::Emit();
                break;

            // The mouse goes to the engine's own UI first (UIInput) and reaches game code only when no UI consumed it,
            // and only when ImGui is not consuming it either.
            // Editor: ViewportWidget re-emits with viewport-local coords when the image is hovered.
            // Game: no ImGui, the window coordinates are the scene coordinates.
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (!IsMouseCapturedByUI()) {
                    CoreManager::GetUIInput().DispatchMouseButtonDown(sdlEvent.button);
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (!IsMouseCapturedByUI()) {
                    CoreManager::GetUIInput().DispatchMouseButtonUp(sdlEvent.button);
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (!IsMouseCapturedByUI()) {
                    CoreManager::GetUIInput().DispatchMouseMotion(sdlEvent.motion);
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (!IsMouseCapturedByUI()) {
                    CoreManager::GetUIInput().DispatchMouseWheel(sdlEvent.wheel);
                }
                break;

            case SDL_EVENT_KEY_DOWN:
                if (!IsKeyboardCapturedByUI()) {
                    SDLEvents::KeyDownEvent::Emit(sdlEvent.key.key, sdlEvent.key.mod, sdlEvent.key.repeat);
                }
                break;

            case SDL_EVENT_KEY_UP:
                if (!IsKeyboardCapturedByUI()) {
                    SDLEvents::KeyUpEvent::Emit(sdlEvent.key.key, sdlEvent.key.mod);
                }
                break;

            case SDL_EVENT_TEXT_INPUT:
                if (!IsKeyboardCapturedByUI()) {
                    SDLEvents::TextInputEvent::Emit(eastl::string_view(sdlEvent.text.text != nullptr ? sdlEvent.text.text : ""));
                }
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                SDLEvents::WindowEvent::Emit(sdlEvent.window);
                break;

            case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
                SDLEvents::SafeAreaChangedEvent::Emit();
                break;

            case SDL_EVENT_DISPLAY_ORIENTATION:
                SDLEvents::DisplayOrientationChangedEvent::Emit(sdlEvent.display.displayID, static_cast<SDL_DisplayOrientation>(sdlEvent.display.data1));
                break;

            default:
                break;
        }
    }

}  // namespace BECore
