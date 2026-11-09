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

#ifdef __EMSCRIPTEN__
    /**
     * @brief Makes the window follow the browser window: sets its size to the inner size of the page now and on every
     * resize of the page (SDL then resizes the canvas, and the backing store is devicePixelRatio times larger with
     * SDL_WINDOW_HIGH_PIXEL_DENSITY). Call it once, after the window is created. Destroying the window removes the handler (SDL
     * clears every resize handler of the page window). Emscripten builds only.
     */
    void InstallWebCanvasResize(SDL_Window* window);
#endif

}  // namespace BECore
