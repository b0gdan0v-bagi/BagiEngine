#include "ViewportInput.h"

#include <BECore/Input/UIInput.h>
#include <EASTL/algorithm.h>
#include <SDL3/SDL.h>

namespace BECore {

    namespace {

        // The ImGui button index (0 left, 1 right, 2 middle) to the SDL button
        constexpr Uint8 kSDLButtons[kViewportButtonCount] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};

        // The `which` of the events of a real window for a touch and a pen, 0 for a mouse
        SDL_MouseID WhichOf(ViewportPointerSource source) {
            switch (source) {
                case ViewportPointerSource::Touch:
                    return SDL_TOUCH_MOUSEID;
                case ViewportPointerSource::Pen:
                    return SDL_PEN_MOUSEID;
                case ViewportPointerSource::Mouse:
                    break;
            }
            return 0;
        }

    }  // namespace

    ViewportInputTarget ResolveTarget(ViewportInputMode mode, bool oneOffModifier) {
        const bool toGame = (mode == ViewportInputMode::Play) != oneOffModifier;
        return toGame ? ViewportInputTarget::Game : ViewportInputTarget::Editor;
    }

    bool ViewportPointer::IsGameHeld() const {
        return eastl::any_of(std::begin(_held), std::end(_held), [](Held held) { return held == Held::Game; });
    }

    void ViewportPointer::Reset() {
        eastl::fill(std::begin(_held), std::end(_held), Held::None);
    }

    void ViewportPointer::Forward(const ViewportPointerFrame& frame, ViewportInputMode mode, bool oneOffModifier, UIInput& uiInput, const EditClickSink& editClickSink) {
        const SDL_MouseID which = WhichOf(frame.source);
        const bool editorHeld = eastl::any_of(std::begin(_held), std::end(_held), [](Held held) { return held == Held::Editor; });
        const bool hoverReachesGame = !editorHeld && ResolveTarget(mode, oneOffModifier) == ViewportInputTarget::Game;

        // A game press is followed to its release even when the pointer has left the image
        if (((frame.hovered && hoverReachesGame) || IsGameHeld()) && (frame.deltaX != 0.0f || frame.deltaY != 0.0f)) {
            SDL_MouseMotionEvent motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.which = which;
            motion.x = frame.x;
            motion.y = frame.y;
            motion.xrel = frame.deltaX;
            motion.yrel = frame.deltaY;
            for (int i = 0; i < kViewportButtonCount; ++i) {
                if (frame.down[i]) {
                    motion.state |= SDL_BUTTON_MASK(kSDLButtons[i]);
                }
            }
            uiInput.DispatchMouseMotion(motion);
        }

        for (int i = 0; i < kViewportButtonCount; ++i) {
            if (frame.hovered && frame.clicked[i]) {
                const ViewportInputTarget target = ResolveTarget(mode, oneOffModifier);
                if (target == ViewportInputTarget::Game) {
                    _held[i] = Held::Game;
                    SDL_MouseButtonEvent button{};
                    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    button.which = which;
                    button.button = kSDLButtons[i];
                    button.down = true;
                    button.clicks = frame.clicks[i];
                    button.x = frame.x;
                    button.y = frame.y;
                    uiInput.DispatchMouseButtonDown(button);
                } else {
                    _held[i] = Held::Editor;
                    if (editClickSink) {
                        editClickSink(ViewportEditClickEvent(frame.x, frame.y, static_cast<uint8_t>(i)));
                    }
                }
            }

            // After the press: a press and a release in one frame are both seen
            if (frame.released[i] && _held[i] != Held::None) {
                if (_held[i] == Held::Game) {
                    SDL_MouseButtonEvent button{};
                    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
                    button.which = which;
                    button.button = kSDLButtons[i];
                    button.down = false;
                    button.x = frame.x;
                    button.y = frame.y;
                    uiInput.DispatchMouseButtonUp(button);
                }
                _held[i] = Held::None;
            }
        }

        if (frame.hovered && hoverReachesGame && (frame.wheelX != 0.0f || frame.wheelY != 0.0f)) {
            SDL_MouseWheelEvent wheel{};
            wheel.type = SDL_EVENT_MOUSE_WHEEL;
            wheel.which = which;
            wheel.x = frame.wheelX;
            wheel.y = frame.wheelY;
            wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            wheel.mouse_x = frame.x;
            wheel.mouse_y = frame.y;
            uiInput.DispatchMouseWheel(wheel);
        }
    }

}  // namespace BECore
