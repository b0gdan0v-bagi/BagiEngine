#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/Console/ConsoleLogView.h>
#include <Widgets/Console/ConsoleSession.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <cstdint>

struct ImGuiInputTextCallbackData;

namespace BECore {

    class LogHistory;
    class LogHistorySink;

    /**
     * @brief The editor's Console panel: the engine's log with filters by level, category and text, Pause and Clear, and a command line for the
     * control API at its foot.
     *
     * Hidden at start; the command `view.console` shows and hides it. Editor only (src/Widgets/Console/ is in the editor folders of
     * src/CMakeLists.txt). The lines live in the LogHistorySink of the LoggerManager, not in the panel, so they survive a switch of the
     * run mode that destroys and creates the widgets; a game whose LoggerConfig.xml has no such sink gets a panel that says so. A double click on a
     * line with a source file opens it through the `externalEditor` preference. The answers of the command line are not log lines: they go to
     * an output area of the panel (ConsoleSession).
     */
    class ConsoleWidget : public IWidget {
        BE_CLASS(ConsoleWidget)
    public:
        ConsoleWidget() = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        void OnCommand(const EditorCommandEvent& event);
        void OnPreferencesChanged(const EditorPreferencesChangedEvent& event);

        /// Makes the window title again when the number of errors or warnings moved
        void SyncTitle(const LogHistory& history);
        void DrawToolbar(LogHistory& history);
        void DrawCategoryCombo(const LogHistory& history);
        /// @p footerHeight is what the output area and the command line below take
        void DrawRows(const LogHistory& history, float footerHeight);
        void OpenSite(const char* file, uint32_t line);
        [[nodiscard]] float GetFooterHeight() const;
        [[nodiscard]] bool HasCandidates() const;
        void DrawOutput();
        void DrawCommandLine();
        void RefreshMethods();
        void Complete(ImGuiInputTextCallbackData& data);
        void WalkHistory(ImGuiInputTextCallbackData& data);
        static int OnInputCallback(ImGuiInputTextCallbackData* data);

        BE_REFLECT_FIELD bool _open = false;
        LogHistorySink* _sink = nullptr;  // owned by the LoggerManager, which outlives the widgets
        ConsoleLogFilter _filter;
        ConsoleLogView _view;
        bool _paused = false;
        String _title = "Console###Console";
        uint32_t _titleErrors = 0;
        uint32_t _titleWarnings = 0;
        char _textBuffer[kMaxConsoleFilterLength + 1] = {};

        String _externalEditor;
        bool _historyLoaded = false;  // the stored history is taken once, so a later preference change does not reset a walk
        ConsoleSession _session;
        Vector<ControlMethodListing> _methods;  // GetMethods() of the control service, made again when its version moves
        uint64_t _methodsVersion = 0;
        bool _methodsKnown = false;
        char _commandBuffer[kMaxConsoleCommandLength + 1] = {};
        String _candidates;     // the candidates of the last Tab, one line
        String _candidatesFor;  // the text of the command line they belong to; shown only while the line still reads so
        uint64_t _outputVersion = 0;
    };

}  // namespace BECore
