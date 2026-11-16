#pragma once

#include <BECore/MainWindow/WindowSimulation.h>
#include <BECore/Math/Rect.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {
    class IRendererHolder;

    class IMainWindow : public RefCounted {
    public:
        ~IMainWindow() override = default;

        virtual bool Initialize(StringView configPath) = 0;
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

        /// Sets the title of the window (ComposeWindowTitle makes it); nothing by default, for a window that has none
        virtual void SetTitle(StringView /*title*/) {}

        /// The `title` the window config sets, empty when it sets none: the input of ComposeWindowTitle
        virtual StringView GetConfiguredTitle() const {
            return {};
        }

        /// The part of the window that notches, rounded corners and system bars leave free, in window units; the whole window by default
        virtual Rect GetSafeArea() const {
            return Rect(0.0f, 0.0f, static_cast<float>(GetWidth()), static_cast<float>(GetHeight()));
        }

        /// A window without a person and a display (the SDL dummy driver) that the control API may resize; a real window belongs to the OS
        virtual bool IsHeadless() const {
            return false;
        }

        /**
         * @brief Makes a headless window pretend to be @p simulation: the size, the pixel density and the safe area the getters answer.
         *
         * @p simulation passed ValidateWindowSimulation. The default refuses: only a headless window can be simulated.
         */
        virtual std::expected<void, String> Simulate(const WindowSimulation& /*simulation*/) {
            return std::unexpected(String("this window cannot be simulated"));
        }
    };
}  // namespace BECore
