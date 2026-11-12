#include "SDLEventsProvider.h"

#include "Application/Application.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <CoreSDL/SDLClipboard.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/ApplicationEvents.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kNeutralDensity = 1.0f;

        // The Game run mode has no ImGui context at all, and ImGui::GetIO() needs one.
        bool IsMouseCapturedByUI() {
            return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
        }

        bool IsKeyboardCapturedByUI() {
            return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
        }

        // A paused game gets no input of the window: dropped here, never queued, so a click made in the pause does not fire on resume.
        // The raw SDLEventWrapper is not affected (ImGui and the editor chords read it), nor is injected input (the control API).
        bool DropsGameInput() {
            return CoreManager::GetPause().IsPaused();
        }

        bool MouseReachesGame() {
            return !DropsGameInput() && !IsMouseCapturedByUI();
        }

        bool KeyboardReachesGame() {
            return !DropsGameInput() && !IsKeyboardCapturedByUI();
        }

        /// Output pixels per window unit; read per event, since the window can move to a display of another density.
        float PixelDensity() {
            const IntrusivePtr<IMainWindow>& window = CoreManager::GetMainWindow();
            return window ? window->GetPixelDensity() : kNeutralDensity;
        }

        // SDL reports the mouse in window units; the engine's input is in the pixels of the output.
        SDL_MouseButtonEvent InOutputPixels(SDL_MouseButtonEvent e) {
            const float density = PixelDensity();
            e.x *= density;
            e.y *= density;
            return e;
        }

        SDL_MouseMotionEvent InOutputPixels(SDL_MouseMotionEvent e) {
            const float density = PixelDensity();
            e.x *= density;
            e.y *= density;
            e.xrel *= density;
            e.yrel *= density;
            return e;
        }

        // The scroll amount (x, y) is not a position and stays as it is.
        SDL_MouseWheelEvent InOutputPixels(SDL_MouseWheelEvent e) {
            const float density = PixelDensity();
            e.mouse_x *= density;
            e.mouse_y *= density;
            return e;
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
            // and only when ImGui is not consuming it either and the game is not paused.
            // Editor: ViewportWidget re-emits with viewport-local coords when the image is hovered.
            // Game: no ImGui, the positions are the output pixels (window units times the pixel density).
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (MouseReachesGame()) {
                    CoreManager::GetUIInput().DispatchMouseButtonDown(InOutputPixels(sdlEvent.button));
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (MouseReachesGame()) {
                    CoreManager::GetUIInput().DispatchMouseButtonUp(InOutputPixels(sdlEvent.button));
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (MouseReachesGame()) {
                    CoreManager::GetUIInput().DispatchMouseMotion(InOutputPixels(sdlEvent.motion));
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (MouseReachesGame()) {
                    CoreManager::GetUIInput().DispatchMouseWheel(InOutputPixels(sdlEvent.wheel));
                }
                break;

            case SDL_EVENT_KEY_DOWN:
                if (KeyboardReachesGame()) {
                    SDLEvents::KeyDownEvent::Emit(sdlEvent.key.key, sdlEvent.key.mod, sdlEvent.key.repeat);
                    if (IsPasteShortcut(sdlEvent.key.key, sdlEvent.key.mod, sdlEvent.key.repeat)) {
                        CoreManager::GetClipboard().RequestPaste();  // answered by Clipboard::Update() after this frame's input
                    }
                }
                break;

            case SDL_EVENT_KEY_UP:
                if (KeyboardReachesGame()) {
                    SDLEvents::KeyUpEvent::Emit(sdlEvent.key.key, sdlEvent.key.mod);
                }
                break;

            case SDL_EVENT_TEXT_INPUT:
                if (KeyboardReachesGame()) {
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
