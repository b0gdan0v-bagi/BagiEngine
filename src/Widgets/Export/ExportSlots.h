#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/PanelAssets/PanelSlots.h>
#include <cstdint>

namespace BECore {

    class ExportPanelModel;

    /**
     * @brief The drawer of `ExportPanel.xml` the format cannot say: the output of the export, the slot `exportOutput`
     *
     * Owned by `ExportPanelModel`, which registers it in its `Initialize`. A clipped list of the log's lines in the colours of their kinds, only the
     * errors with `errorsOnly`; it follows the end unless the user scrolled up, and a right click copies a line. A frame with no new line allocates nothing.
     */
    class ExportSlots {
    public:
        explicit ExportSlots(ExportPanelModel& model) : _model(model) {}

        /// Registers the slot in the global `PanelSlots`; the handle ends with this object
        void Register();

    private:
        void DrawOutput();
        void CollectErrors();
        void DrawLine(size_t index);

        ExportPanelModel& _model;
        PanelSlotHandle _outputSlot;
        /// The lines shown with errors only, as indices of the log; made again when the log's revision moved
        Vector<uint32_t> _errorLines;
        Optional<uint32_t> _errorRevision;
    };

}  // namespace BECore
