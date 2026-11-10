#pragma once

struct SDL_Window;

namespace BECore {

    /// A window size in window units (the units of SDL_SetWindowSize)
    struct WebCanvasSize {
        int width = 0;
        int height = 0;
    };

    /**
     * @brief The window size that makes the canvas fill a browser window of the given inner size.
     *
     * Rounds to whole units and never goes below the minimum (a minimised or not yet laid out page reports 0, and
     * the renderer cannot make a surface of that size). A size that is not a finite number counts as 0. Pure: no I/O,
     * so it runs, and is tested, on every platform.
     */
    WebCanvasSize FitWindowSize(double innerWidth, double innerHeight, int minWidth, int minHeight);

    /// The part of a window that is free of notches and system bars, in window units
    struct WebSafeArea {
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
    };

    /**
     * @brief The safe area of a window of the given size from the four insets (CSS env(safe-area-inset-*)).
     *
     * An inset that is not a finite number or is negative counts as 0, an inset larger than the window is cut to the
     * window. The result is never empty: when the insets leave no width or no height, it is the whole window. Pure,
     * so it runs, and is tested, on every platform.
     */
    WebSafeArea SafeAreaFromInsets(double width, double height, double top, double right, double bottom, double left);

#ifdef __EMSCRIPTEN__
    /**
     * @brief Makes the window follow the browser window: sets its size to the inner size of the page now and on every
     * resize of the page and every change of its fullscreen state (SDL then resizes the canvas, and the backing store is
     * devicePixelRatio times larger with SDL_WINDOW_HIGH_PIXEL_DENSITY). The same events (and orientationchange) re-read the
     * safe-area insets of the page and push SDL_EVENT_WINDOW_SAFE_AREA_CHANGED when they changed. Call it once, after the
     * window is created. Destroying the window removes the handlers (SDL clears every handler of the resize, fullscreenchange and
     * orientationchange events of the page). Emscripten builds only.
     */
    void InstallWebCanvasResize(SDL_Window* window);

    /// The safe area of the page for a window of the given size, from the insets read at the last change (not per call). Emscripten builds only.
    WebSafeArea GetWebSafeArea(double width, double height);
#endif

}  // namespace BECore
