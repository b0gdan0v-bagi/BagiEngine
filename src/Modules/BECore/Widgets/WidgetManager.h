#pragma once

#include <BECore/Application/RunMode.h>
#include <BECore/Widgets/IWidget.h>

namespace BECore {

    class WidgetManager {
    public:
        /// Builds the widgets of a run mode. The default reads the config named by GetWidgetsConfigName(mode); a test replaces it
        /// (SetWidgetLoader) to get widgets with no config files and no real editor.
        using WidgetLoader = eastl::function<eastl::vector<IntrusivePtr<IWidget>>(RunMode)>;

        WidgetManager() = default;
        ~WidgetManager() = default;

        void RegisterWidget(IntrusivePtr<IWidget> widget);
        void UpdateAll() const;
        void DrawScreenAll() const;
        void DrawAll() const;

        /// Creates the widgets of the resolved run mode of ApplicationConfig (ResolveRunMode(ReadRunMode(..))).
        void CreateWidgets();

        /// Creates the widgets of @p mode and appends them; the mode is what GetMode() reports from now on.
        /// An empty list (and a log line) when the config of the mode is missing.
        void CreateWidgets(RunMode mode);

        /// The mode of the widgets created last (Game before any CreateWidgets).
        RunMode GetMode() const {
            return _mode;
        }

        /// Replaces how the widgets of a mode are built; an empty function restores the config reader.
        void SetWidgetLoader(WidgetLoader loader) {
            _loader = std::move(loader);
        }

#if BE_WITH_EDITOR
        /// Asks for @p mode at the end of this frame. The last request before the apply wins; asking for the mode that runs
        /// clears a pending request. Nothing changes until ApplyPendingMode().
        void RequestMode(RunMode mode);

        /// Asks for the other mode than the one that would run after the pending request (two toggles in a row cancel out).
        void RequestToggle();

        /// True while a request waits for ApplyPendingMode().
        bool HasPendingMode() const {
            return _pendingMode.has_value();
        }

        /**
         * @brief Switches to the pending mode, if there is one; returns whether it did.
         *
         * Call it only at the end of a frame, after the present and outside UpdateAll / DrawScreenAll / DrawAll: ImGui's frame
         * is open while the widgets draw, and a widget cannot be destroyed in the middle of its own call. The order is fixed:
         *  1. wait until the GPU is idle (it may still read what the widgets own: ImGui's descriptors, the viewport's target),
         *  2. destroy every widget (their subscriptions go with them),
         *  3. create the widgets of the new mode, as CreateWidgets(mode) does,
         *  4. log the new mode.
         * Works without a renderer (step 1 is skipped).
         */
        bool ApplyPendingMode();
#endif

    private:
        eastl::vector<IntrusivePtr<IWidget>> _widgets;
        RunMode _mode = RunMode::Game;
        WidgetLoader _loader;
#if BE_WITH_EDITOR
        std::optional<RunMode> _pendingMode;
#endif
    };

}  // namespace BECore
