#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Types/Types.h>
#include <Widgets/Console/ConsoleSession.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/PanelAssets/PanelSlots.h>
#include <cstdint>

struct ImGuiInputTextCallbackData;

namespace BECore {

    class ConsolePanelModel;

    /**
     * @brief The three drawers of `ConsolePanel.xml` that the format cannot say: the log rows, the output area and the command line
     *
     * Owned by `ConsolePanelModel`, which registers them as the slots `consoleRows`, `consoleOutput` and `consoleCommandLine` in its `Initialize` and
     * takes them back when it dies. The rows slot sizes itself from the footer the output area and the command line take below it (`GetFooterHeight`),
     * so the three share their state here: the candidates of the last Tab and the control API's method list. The rows, the filter and the pause come
     * from the model.
     */
    class ConsoleSlots {
    public:
        explicit ConsoleSlots(ConsolePanelModel& model) : _model(model) {}

        /// Registers the three slots in the global `PanelSlots`; the handles end with this object
        void Register();

    private:
        void DrawRows();
        void DrawOutput();
        void DrawCommandLine();
        void OpenSite(const char* file, uint32_t line);
        [[nodiscard]] float GetFooterHeight() const;
        [[nodiscard]] bool HasCandidates() const;
        void RefreshMethods();
        void Complete(ImGuiInputTextCallbackData& data);
        void WalkHistory(ImGuiInputTextCallbackData& data);
        static int OnInputCallback(ImGuiInputTextCallbackData* data);

        ConsolePanelModel& _model;
        PanelSlotHandle _rowsSlot;
        PanelSlotHandle _outputSlot;
        PanelSlotHandle _commandSlot;
        Vector<ControlMethodListing> _methods;  // GetMethods() of the control service, made again when its version moves
        uint64_t _methodsVersion = 0;
        bool _methodsKnown = false;
        char _commandBuffer[kMaxConsoleCommandLength + 1] = {};
        String _candidates;     // the candidates of the last Tab, one line
        String _candidatesFor;  // the text of the command line they belong to; shown only while the line still reads so
        uint64_t _outputVersion = 0;
    };

}  // namespace BECore
