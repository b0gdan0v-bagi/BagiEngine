#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/Console/ConsoleLogView.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <cstdint>

namespace BECore {

    class LogHistory;
    class LogHistorySink;

    /**
     * @brief The editor's Console panel: the engine's log with filters by level, category and text, Pause and Clear.
     *
     * Hidden at start; the command `view.console` shows and hides it. Editor only (src/Widgets/Console/ is in the editor folders of
     * src/CMakeLists.txt). The lines live in the LogHistorySink of the LoggerManager, not in the panel, so they survive a switch of the
     * run mode that destroys and creates the widgets; a game whose LoggerConfig.xml has no such sink gets a panel that says so.
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

        /// Makes the window title again when the number of errors or warnings moved
        void SyncTitle(const LogHistory& history);
        void DrawToolbar(LogHistory& history);
        void DrawCategoryCombo(const LogHistory& history);
        void DrawRows(const LogHistory& history);

        BE_REFLECT_FIELD bool _open = false;
        LogHistorySink* _sink = nullptr;  // owned by the LoggerManager, which outlives the widgets
        ConsoleLogFilter _filter;
        ConsoleLogView _view;
        bool _paused = false;
        String _title = "Console###Console";
        uint32_t _titleErrors = 0;
        uint32_t _titleWarnings = 0;
        char _textBuffer[kMaxConsoleFilterLength + 1] = {};
    };

}  // namespace BECore
