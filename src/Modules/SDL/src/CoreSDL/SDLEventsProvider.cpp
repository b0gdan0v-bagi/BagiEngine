#include "SDLEventsProvider.h"

#include <Application/Application.h>
#include <BECore/Clipboard/Clipboard.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/Time/GamePause.h>
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

        // SDL sends the mouse event it synthesizes from the first finger (SDL_TOUCH_MOUSEID) just before that finger's own event, so a
        // "first finger event seen" latch would let the first tap through twice. While SDL sends finger events the synthesized ones
        // are the same touch again and are dropped; an application that disabled them keeps the mouse events as its only source.
        bool FingerEventsAreSent() {
            return SDL_EventEnabled(SDL_EVENT_FINGER_DOWN) && SDL_EventEnabled(SDL_EVENT_FINGER_MOTION) && SDL_EventEnabled(SDL_EVENT_FINGER_UP);
        }

        bool IsDuplicateOfFinger(SDL_MouseID which) {
            return which == SDL_TOUCH_MOUSEID && FingerEventsAreSent();
        }

        // A touch screen of a window. A trackpad of macOS (touch id SDL_MOUSE_TOUCHID, no window) and a pen's touch are not fingers on the
        // scene: the mouse cursor and the pen events carry them
        bool IsFingerOnWindow(const SDL_TouchFingerEvent& finger) {
            return finger.windowID != 0 && finger.touchID != SDL_MOUSE_TOUCHID && finger.touchID != SDL_PEN_TOUCHID;
        }

        // The position of a finger is normalized to the window; its output position is that times the window size (units) times the density
        struct FingerPlace {
            float x = 0.0f;
            float y = 0.0f;
            float dx = 0.0f;
            float dy = 0.0f;
        };

        Optional<FingerPlace> PlaceOf(const SDL_TouchFingerEvent& finger) {
            const IntrusivePtr<IMainWindow>& window = CoreManager::GetMainWindow();
            if (!window) {
                return std::nullopt;
            }
            const float scaleX = static_cast<float>(window->GetWidth()) * window->GetPixelDensity();
            const float scaleY = static_cast<float>(window->GetHeight()) * window->GetPixelDensity();
            return FingerPlace{finger.x * scaleX, finger.y * scaleY, finger.dx * scaleX, finger.dy * scaleY};
        }

        SDL_MouseMotionEvent MotionOf(const SDL_TouchFingerEvent& finger, uint32_t which, const FingerPlace& place, SDL_MouseButtonFlags state) {
            SDL_MouseMotionEvent motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.timestamp = finger.timestamp;
            motion.windowID = finger.windowID;
            motion.which = which;
            motion.state = state;
            motion.x = place.x;
            motion.y = place.y;
            motion.xrel = place.dx;
            motion.yrel = place.dy;
            return motion;
        }

        SDL_MouseButtonEvent ButtonOf(const SDL_TouchFingerEvent& finger, uint32_t which, const FingerPlace& place, bool down) {
            SDL_MouseButtonEvent button{};
            button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            button.timestamp = finger.timestamp;
            button.windowID = finger.windowID;
            button.which = which;
            button.button = SDL_BUTTON_LEFT;
            button.down = down;
            button.clicks = 1;
            button.x = place.x;
            button.y = place.y;
            return button;
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
        _fingers = FingerPointers();
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
                if (MouseReachesGame() && !IsDuplicateOfFinger(sdlEvent.button.which)) {
                    CoreManager::GetUIInput().DispatchMouseButtonDown(InOutputPixels(sdlEvent.button));
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (MouseReachesGame() && !IsDuplicateOfFinger(sdlEvent.button.which)) {
                    CoreManager::GetUIInput().DispatchMouseButtonUp(InOutputPixels(sdlEvent.button));
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (MouseReachesGame() && !IsDuplicateOfFinger(sdlEvent.motion.which)) {
                    CoreManager::GetUIInput().DispatchMouseMotion(InOutputPixels(sdlEvent.motion));
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (MouseReachesGame()) {
                    CoreManager::GetUIInput().DispatchMouseWheel(InOutputPixels(sdlEvent.wheel));
                }
                break;

            // A finger is a pointer of its own (FingerPointers) and takes the same road as the mouse, under the same gates.
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_MOTION:
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                ConvertFingerEvent(sdlEvent.tfinger, static_cast<SDL_EventType>(sdlEvent.type));
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
                    SDLEvents::TextInputEvent::Emit(StringView(sdlEvent.text.text != nullptr ? sdlEvent.text.text : ""));
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

    void SDLEventsProvider::ConvertFingerEvent(const SDL_TouchFingerEvent& finger, SDL_EventType type) {
        if (!IsFingerOnWindow(finger)) {
            return;
        }
        const Optional<FingerPlace> place = PlaceOf(finger);
        if (!place) {
            return;
        }
        UIInput& input = CoreManager::GetUIInput();

        switch (type) {
            case SDL_EVENT_FINGER_DOWN: {
                if (!MouseReachesGame()) {
                    return;
                }
                const Optional<uint32_t> which = _fingers.Begin(finger.fingerID);
                if (!which) {
                    return;
                }
                // The arrival first, as SDL's own mouse emulation does it, so the target under the finger gets its enter
                input.DispatchMouseMotion(MotionOf(finger, *which, FingerPlace{place->x, place->y, 0.0f, 0.0f}, 0));
                input.DispatchMouseButtonDown(ButtonOf(finger, *which, *place, true));
                break;
            }

            case SDL_EVENT_FINGER_MOTION: {
                const Optional<uint32_t> which = _fingers.Find(finger.fingerID);
                if (which && MouseReachesGame()) {
                    input.DispatchMouseMotion(MotionOf(finger, *which, *place, SDL_BUTTON_LMASK));
                }
                break;
            }

            // A cancelled finger releases its press like an up, so no capture stays; the place is free in either case
            // even when the gates dropped the release (a pause that began while the finger was down)
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED: {
                const Optional<uint32_t> which = _fingers.End(finger.fingerID);
                if (!which) {
                    return;
                }
                if (MouseReachesGame()) {
                    input.DispatchMouseButtonUp(ButtonOf(finger, *which, *place, false));
                }
                // SDL sends no leave for a lifted finger, and its place is reused by the next one
                input.EndHover(*which);
                break;
            }

            default:
                break;
        }
    }

}  // namespace BECore
