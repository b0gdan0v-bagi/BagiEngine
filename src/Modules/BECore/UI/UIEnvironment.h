#pragma once

#include <BECore/UI/UILayoutContext.h>
#include <BECore/UI/UISettings.h>

namespace BECore {

    class IMainWindow;

    /**
     * @brief What the UI reads from the outside world: the settings and the window metrics, as a layout context. `CoreManager::GetUIEnvironment()`.
     *
     * MakeContext() puts the window's pixel density, display scale and safe area together with the output size. The editor viewport is
     * not the window, so it overrides the safe area (SetSafeAreaOverride) around the scene draw: the whole target, or a simulated notch;
     * and the pixel density (SetPixelDensityOverride): its target has one pixel per ImGui unit whatever the window's density is.
     */
    class UIEnvironment {
    public:
        /// Reads `config/UIConfig.xml` (the defaults when it is missing)
        void Initialize();

        [[nodiscard]] const UISettings& GetSettings() const {
            return _settings;
        }
        void SetSettings(const UISettings& settings);

        /// Changes the accessibility scale at run time (kept inside UISettings's range); canvases lay out again on the next draw
        void SetUserScale(float scale);

        /// The safe area in output pixels that replaces the window's, or nothing to ask the window again
        void SetSafeAreaOverride(std::optional<Rect> safeArea) {
            _safeAreaOverride = safeArea;
        }
        [[nodiscard]] const std::optional<Rect>& GetSafeAreaOverride() const {
            return _safeAreaOverride;
        }

        /// The pixel density (output pixels per window unit) that replaces the window's, or nothing to ask the window again; a value that is not positive is ignored
        void SetPixelDensityOverride(std::optional<float> density) {
            _pixelDensityOverride = density;
        }
        [[nodiscard]] const std::optional<float>& GetPixelDensityOverride() const {
            return _pixelDensityOverride;
        }

        /**
         * @param window the window whose metrics apply, or null for the neutral ones (density 1, display scale 1, the whole output is safe)
         * @param outputWidth,outputHeight the size in pixels of what is being drawn into
         * @return the context without the scene fields (sceneViewWidth, sceneViewHeight, worldBox): the canvas adds those
         */
        [[nodiscard]] UILayoutContext MakeContext(const IMainWindow* window, float outputWidth, float outputHeight) const;

    private:
        UISettings _settings;
        std::optional<Rect> _safeAreaOverride;
        std::optional<float> _pixelDensityOverride;
    };

}  // namespace BECore
