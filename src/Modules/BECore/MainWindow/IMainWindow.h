#pragma once

namespace BECore {
    class IRendererHolder;

    class IMainWindow : public RefCounted {
    public:
        ~IMainWindow() override = default;

        virtual bool Initialize(eastl::string_view configPath) = 0;
        virtual void Destroy() = 0;

        virtual bool IsValid() const = 0;

        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;

        /**
         * @brief Output pixels per window unit (1 on a standard display, 2 on a Retina one).
         *
         * GetWidth() / GetHeight() and GetSafeArea() are in window units; multiply by this to get the pixels of the render output.
         */
        virtual float GetPixelDensity() const {
            return 1.0f;
        }

        /// The scale the user set for the display the window is on (1 = 100%); text and touch targets grow with it
        virtual float GetDisplayScale() const {
            return 1.0f;
        }

        /// The part of the window that notches, rounded corners and system bars leave free, in window units; the whole window by default
        virtual Rect GetSafeArea() const {
            return Rect(0.0f, 0.0f, static_cast<float>(GetWidth()), static_cast<float>(GetHeight()));
        }
    };
}  // namespace BECore
