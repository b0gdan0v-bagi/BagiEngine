#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/Console/ConsoleLogView.h>
#include <Widgets/Console/ConsoleSession.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <cstdint>

namespace BECore {

    class LogHistory;

    /**
     * @brief What `ConsolePanel.xml` binds to: the state of the Console's toolbar and its window title
     *
     * The four level toggles, the category and the filter text are written by the controls of the panel (no command) and read by the model in its next
     * `Update`; the labels, the category list, the counters and the title are written by the model.
     */
    struct ConsolePanelView {
        BE_CLASS(ConsolePanelView)

        BE_REFLECT_FIELD bool showDebug = true;
        BE_REFLECT_FIELD bool showInfo = true;
        BE_REFLECT_FIELD bool showWarning = true;
        BE_REFLECT_FIELD bool showError = true;  ///< Error and Fatal
        BE_REFLECT_FIELD String debugLabel;
        BE_REFLECT_FIELD String infoLabel;
        BE_REFLECT_FIELD String warningLabel;
        BE_REFLECT_FIELD String errorLabel;
        /// `All`, then the categories of the history in its order; `category` is an index into it
        BE_REFLECT_FIELD Vector<String> categories;
        BE_REFLECT_FIELD int32_t category = 0;
        BE_REFLECT_FIELD String filterText;
        BE_REFLECT_FIELD bool paused = false;
        BE_REFLECT_FIELD bool hasHistory = false;
        /// Lines that did not fit the log queue before they reached the history
        BE_REFLECT_FIELD uint32_t lost = 0;
        BE_REFLECT_FIELD String lostText;
        BE_REFLECT_FIELD String title = "Console";
    };

    /**
     * @brief The model of the Console panel asset: the filter, the pause, the command line's session and the preferences it keeps, with no ImGui
     *
     * The lines live in the `LogHistorySink` of the `LoggerManager`, found in `Update` (a test sets its own history with `SetHistory`). `Update` copies the
     * toggles, the category and the filter text of the view into a `ConsoleLogFilter` and bumps its `revision` only on a real change, then updates the
     * `ConsoleLogView`; the labels, the category list, the lost counter and the title are made again only when what they show moved, so a steady frame
     * allocates nothing.
     *
     * The model owns the `ConsoleSession` (the output area and the command history) and takes the external editor and the stored history from
     * `EditorPreferencesChangedEvent` (the history from the first one only, so a later change does not reset a walk). The widget that hosts the model
     * makes it in its `Initialize`, before the first `Update` of `EditorLayoutWidget`, which is what sends the first event.
     *
     * Commands (`EditorCommandIds`, run only while the panel is open): `console.pause` toggles the pause, `console.clear` empties the log history and
     * `console.clearOutput` the output area of the command line.
     */
    class ConsolePanelModel : public IPanelModel, public SubscriptionHolder {
        BE_CLASS(ConsolePanelModel)
    public:
        ConsolePanelModel();

        void Initialize() override;
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        /// Reads this history instead of the one of the logger's sink (a test); it must outlive the model. Null goes back to the sink's
        void SetHistory(LogHistory* history);

        /// The history the panel shows now: the one set, else the sink's; null when the logger has no `LogHistorySink`
        [[nodiscard]] LogHistory* GetHistory() const;

        [[nodiscard]] const ConsolePanelView& GetView() const {
            return _view;
        }

        /// The rows that pass the filter, as of the last `Update`
        [[nodiscard]] const ConsoleLogView& GetLogView() const {
            return _logView;
        }

        [[nodiscard]] ConsoleSession& GetSession() {
            return _session;
        }

        /// The `externalEditor` preference: the template that opens a file of a line
        [[nodiscard]] const String& GetExternalEditor() const {
            return _externalEditor;
        }

    private:
        void OnPreferencesChanged(const EditorPreferencesChangedEvent& event);
        void SyncLabels(const LogHistory& history);
        void SyncCategories(const LogHistory& history);
        void SyncFilter();
        void SyncTitle(const LogHistory& history);
        void SyncLost();

        ConsolePanelView _view;
        ConsoleLogFilter _filter;
        ConsoleLogView _logView;
        ConsoleSession _session;
        LogHistory* _history = nullptr;
        String _externalEditor;
        bool _historyLoaded = false;  // the stored history is taken once, so a later preference change does not reset a walk
        bool _labelsBuilt = false;
        bool _titleBuilt = false;
        uint32_t _labelCounts[4] = {};
        uint32_t _titleErrors = 0;
        uint32_t _titleWarnings = 0;
        size_t _lostCount = 0;
        bool _lostBuilt = false;
    };

}  // namespace BECore
