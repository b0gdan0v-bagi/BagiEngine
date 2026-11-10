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

    WebSafeArea SafeAreaFromInsets(double width, double height, double top, double right, double bottom, double left) {
        const WebSafeArea whole{0.0, 0.0, width, height};
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0) {
            return whole;
        }
        // Not a number or below zero counts as no inset; one that is larger than the window is cut to the window
        const auto inset = [](double value, double extent) { return std::isfinite(value) && value > 0.0 ? std::min(value, extent) : 0.0; };
        top = inset(top, height);
        bottom = inset(bottom, height);
        left = inset(left, width);
        right = inset(right, width);
        const WebSafeArea area{left, top, width - left - right, height - top - bottom};
        return area.width > 0.0 && area.height > 0.0 ? area : whole;
    }

#ifdef __EMSCRIPTEN__
    namespace {

        // The smallest window the renderer is asked for
        constexpr int kMinWindowSize = 64;

        // The sides of env(safe-area-inset-*), in the order of the CSS shorthand
        constexpr size_t kInsetSides = 4;
        enum InsetSide : size_t { InsetTop, InsetRight, InsetBottom, InsetLeft };
        using Insets = eastl::array<double, kInsetSides>;

        // The insets of the page as of the last change; the safe area is derived from them on demand. One window per page.
        Insets lastInsets{};

        // The page goes fullscreen through the shell (requestFullscreen on the document element), SDL's own fullscreen is the canvas
        // The JavaScript of EM_ASM is stringized as it stands, so clang-format must not touch it (it splits === into == =)
        // clang-format off
        bool IsCanvasFullscreen() {
            return EM_ASM_INT({
                var element = document.fullscreenElement || document.webkitFullscreenElement;
                return element && element.tagName === 'CANVAS' ? 1 : 0;
            }) != 0;
        }

        // The four sides of env(safe-area-inset-*), in CSS pixels, read through the padding of a hidden fixed element (CSS has
        // no other way to the value; 0 where the browser does not know env()). Written to the doubles at the given address.
        void ReadInsets(double* out) {
            EM_ASM({
                var probe = window.bagiSafeAreaProbe;
                if (!probe) {
                    probe = document.createElement('div');
                    probe.setAttribute('aria-hidden', 'true');
                    probe.style.cssText = 'position:fixed;left:0;top:0;width:0;height:0;visibility:hidden;pointer-events:none;' +
                        'padding:env(safe-area-inset-top,0px) env(safe-area-inset-right,0px) env(safe-area-inset-bottom,0px) env(safe-area-inset-left,0px)';
                    document.body.appendChild(probe);
                    window.bagiSafeAreaProbe = probe;
                }
                var style = window.getComputedStyle(probe);
                var sides = ['paddingTop', 'paddingRight', 'paddingBottom', 'paddingLeft'];
                for (var i = 0; i < sides.length; ++i) {
                    HEAPF64[($0 >> 3) + i] = parseFloat(style[sides[i]]);
                }
            }, out);
        }

        double InnerWidth() {
            return EM_ASM_DOUBLE({ return window.innerWidth; });
        }

        double InnerHeight() {
            return EM_ASM_DOUBLE({ return window.innerHeight; });
        }
        // clang-format on

        // Reads the insets and, when they differ from the last read, tells the engine the way SDL's own change reaches it
        void RefreshInsets(SDL_Window* window, bool announce) {
            Insets insets{};
            ReadInsets(insets.data());
            for (double& inset : insets) {
                inset = std::isfinite(inset) && inset > 0.0 ? inset : 0.0;  // an unreadable value is no inset
            }
            if (insets == lastInsets) {
                return;
            }
            lastInsets = insets;
            if (announce) {
                SDL_Event event{};
                event.type = SDL_EVENT_WINDOW_SAFE_AREA_CHANGED;
                event.window.windowID = SDL_GetWindowID(window);
                SDL_PushEvent(&event);  // SDLEventsProvider turns it into SafeAreaChangedEvent
            }
        }

        void FitWindowToPage(SDL_Window* window, double innerWidth, double innerHeight) {
            // SDL's own fullscreen (the canvas) is the size of the screen and its driver sizes the canvas itself. The page
            // in fullscreen is another matter: SDL sets SDL_WINDOW_FULLSCREEN for it too (its handler reacts to any element),
            // but the canvas has to follow the page, so the flag is not what decides here
            if (IsCanvasFullscreen()) {
                return;
            }
            const WebCanvasSize size = FitWindowSize(innerWidth, innerHeight, kMinWindowSize, kMinWindowSize);
            if (!SDL_SetWindowSize(window, size.width, size.height)) {
                LOG_WARNING(Format("[SDLWebCanvas] SDL_SetWindowSize({}, {}) failed: {}", size.width, size.height, SDL_GetError()).c_str());
            }
        }

        void FitAndRefresh(SDL_Window* window, bool announce) {
            FitWindowToPage(window, InnerWidth(), InnerHeight());
            RefreshInsets(window, announce);
        }

        EM_BOOL OnPageResize(int /*eventType*/, const EmscriptenUiEvent* event, void* userData) {
            auto* window = static_cast<SDL_Window*>(userData);
            FitWindowToPage(window, event->windowInnerWidth, event->windowInnerHeight);
            RefreshInsets(window, true);
            return false;  // the result only decides preventDefault; SDL's own resize handler gets the event as well
        }

        // For a fullscreen change SDL's handler sends SDL_EVENT_WINDOW_RESIZED with the size of the display's desktop mode
        // and does not resize the canvas (SDL_UpdateFullscreenMode). This handler is registered after SDL's, so it runs
        // after it and puts the size of the page back; the browser's own resize may come before the change or stale
        EM_BOOL OnFullscreenChange(int /*eventType*/, const EmscriptenFullscreenChangeEvent* /*event*/, void* userData) {
            FitAndRefresh(static_cast<SDL_Window*>(userData), true);
            return false;
        }

        EM_BOOL OnOrientationChange(int /*eventType*/, const EmscriptenOrientationChangeEvent* /*event*/, void* userData) {
            FitAndRefresh(static_cast<SDL_Window*>(userData), true);
            return false;
        }

    }  // namespace

    void InstallWebCanvasResize(SDL_Window* window) {
        // The page size at the start (the window of the config is only a start size), then on every change of the page
        FitAndRefresh(window, false);
        emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, window, false, &OnPageResize);
        emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, window, false, &OnFullscreenChange);
        emscripten_set_orientationchange_callback(window, false, &OnOrientationChange);
    }

    WebSafeArea GetWebSafeArea(double width, double height) {
        return SafeAreaFromInsets(width, height, lastInsets[InsetTop], lastInsets[InsetRight], lastInsets[InsetBottom], lastInsets[InsetLeft]);
    }
#endif

}  // namespace BECore
