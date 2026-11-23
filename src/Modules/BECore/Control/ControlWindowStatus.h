#pragma once

#include <BECore/MainWindow/WindowSimulation.h>
#include <BECore/Math/Rect.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// The insets of @p safe inside a window of @p width by @p height window units; rounding never makes one negative
    [[nodiscard]] WindowInsets SafeAreaInsets(const Rect& safe, int width, int height);

    /// The safe area of the window as `status` and `set_window` answer it, in window units; the floats are widened to double, which is what the `Json` DOM stored
    struct ControlWindowInsets {
        BE_CLASS(ControlWindowInsets)

        BE_REFLECT_FIELD double bottom = 0.0;
        BE_REFLECT_FIELD double left = 0.0;
        BE_REFLECT_FIELD double right = 0.0;
        BE_REFLECT_FIELD double top = 0.0;
    };

    /**
     * @brief The window as `status` (its `window` member) and `set_window` answer it, read from the main window, the renderer and the UI environment.
     *
     * `width`, `height` and the insets are in window units, `outputWidth` and `outputHeight` in pixels (0 without a renderer). Without a window the size and the insets
     * are 0, the density and the scales 1 and `headless` false. Details: `.claude/rules/control-window-api.md`.
     */
    struct ControlWindowStatus {
        BE_CLASS(ControlWindowStatus)

        BE_REFLECT_FIELD double density = 1.0;
        BE_REFLECT_FIELD double displayScale = 1.0;
        BE_REFLECT_FIELD bool headless = false;
        BE_REFLECT_FIELD int32_t height = 0;
        BE_REFLECT_FIELD int32_t outputHeight = 0;
        BE_REFLECT_FIELD int32_t outputWidth = 0;
        BE_REFLECT_FIELD ControlWindowInsets safeArea;
        BE_REFLECT_FIELD double userScale = 1.0;
        BE_REFLECT_FIELD int32_t width = 0;

        /// The window as it is now; main thread
        [[nodiscard]] static ControlWindowStatus Read();
    };

}  // namespace BECore
