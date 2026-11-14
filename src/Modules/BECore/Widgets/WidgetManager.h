#pragma once

#include <BECore/Application/RunMode.h>
#include <BECore/Widgets/IWidget.h>

namespace BECore {

    class WidgetManager {
    public:
        /// Builds the widgets of a run mode. The default reads the config named by GetWidgetsConfigName(mode); a test replaces it
        /// (SetWidgetLoader) to get widgets with no config files and no real editor.
        using WidgetLoader = eastl::function<Vector<IntrusivePtr<IWidget>>(RunMode)>;

        WidgetManager() = default;
        /// Destroys the widgets the way DestroyAll does (the last created first), without waiting for the GPU.
        ~WidgetManager();

        void RegisterWidget(IntrusivePtr<IWidget> widget);
        void UpdateAll() const;
        void DrawScreenAll() const;
        void DrawAll() const;

        /// Creates the widgets of the resolved run mode of ApplicationConfig (ResolveRunMode(ApplicationConfig::_mode)).
        void CreateWidgets();

        /// Creates the widgets of @p mode and appends them; the mode is what GetMode() reports from now on.
        /// With the default loader the list stays empty (and a log line is written) when the config of the mode is missing.
        void CreateWidgets(RunMode mode);

        /// The mode of the widgets created last (Game before any CreateWidgets).
        RunMode GetMode() const {
            return _mode;
        }

        /// Waits until the GPU is idle (when there is a renderer: it may still read what the widgets own) and destroys every widget,
        /// the last created first: a later widget may use what an earlier one owns (the viewport's render target needs the ImGui
        /// context of ImGuiWidget). Their subscriptions go with them. Not for the middle of a frame (ApplyPendingMode).
        void DestroyAll();

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
         *  2. destroy every widget, the last created first (DestroyAll; their subscriptions go with them),
         *  3. create the widgets of the new mode, as CreateWidgets(mode) does,
         *  4. log the new mode.
         * Works without a renderer (step 1 is skipped).
         */
        bool ApplyPendingMode();
#endif

    private:
        Vector<IntrusivePtr<IWidget>> _widgets;
        RunMode _mode = RunMode::Game;
        WidgetLoader _loader;
        // Not under BE_WITH_EDITOR: the layout of a class must not depend on the define, or objects built in a mixed build crash
        Optional<RunMode> _pendingMode;
    };

}  // namespace BECore
