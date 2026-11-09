#include "SDLWebCanvas.h"

#include <cmath>

#ifdef __EMSCRIPTEN__
#include <SDL3/SDL.h>
#include <emscripten/em_asm.h>
#include <emscripten/html5.h>
#endif

namespace BECore {

    namespace {

        int FitAxis(double inner, int minimum) {
            if (!std::isfinite(inner) || inner <= 0.0) {
                return minimum;
            }
            // A page cannot be larger than what an int holds; the clamp keeps the cast defined for a wild value
            constexpr double kLargest = 1.0e6;
            return std::max(minimum, static_cast<int>(std::lround(std::min(inner, kLargest))));
        }

    }  // namespace

    WebCanvasSize FitWindowSize(double innerWidth, double innerHeight, int minWidth, int minHeight) {
        return {FitAxis(innerWidth, minWidth), FitAxis(innerHeight, minHeight)};
    }

#ifdef __EMSCRIPTEN__
    namespace {

        // The smallest window the renderer is asked for
        constexpr int kMinWindowSize = 64;

        void FitWindowToPage(SDL_Window* window, double innerWidth, double innerHeight) {
            const WebCanvasSize size = FitWindowSize(innerWidth, innerHeight, kMinWindowSize, kMinWindowSize);
            SDL_SetWindowSize(window, size.width, size.height);
        }

        EM_BOOL OnPageResize(int /*eventType*/, const EmscriptenUiEvent* event, void* userData) {
            FitWindowToPage(static_cast<SDL_Window*>(userData), event->windowInnerWidth, event->windowInnerHeight);
            return false;  // not consumed: SDL's own resize handler sees the event too
        }

    }  // namespace

    void InstallWebCanvasResize(SDL_Window* window) {
        // unverified: needs emsdk. The page size at the start (the window of the config is only a start size), then on every resize
        FitWindowToPage(window, EM_ASM_DOUBLE({ return window.innerWidth; }), EM_ASM_DOUBLE({ return window.innerHeight; }));
        emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, window, false, &OnPageResize);
    }
#endif

}  // namespace BECore
