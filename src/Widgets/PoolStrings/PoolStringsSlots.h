#pragma once

#include <Widgets/PanelAssets/PanelSlots.h>
#include <cstddef>

namespace BECore {

    class PoolStringsPanelModel;

    /**
     * @brief The drawers of `PoolStringsPanel.xml` that the format cannot say: the table of blocks with their fill bars and the sortable list of
     * the strings
     *
     * Owned by `PoolStringsPanelModel`, which registers them in its `Initialize` and takes them back when it dies. A slot draws only the visible
     * rows of what the model made from its snapshot and allocates nothing for a row. A click on a header of the strings is told to the model
     * (`SetSort`).
     */
    class PoolStringsSlots {
    public:
        explicit PoolStringsSlots(PoolStringsPanelModel& model) : _model(model) {}

        /// Registers the two slots in the global `PanelSlots`; the handles end with this object
        void Register();

        /// How many rows the last draw of the blocks table drew
        [[nodiscard]] size_t GetDrawnBlockRows() const {
            return _drawnBlockRows;
        }

        /// How many rows the last draw of the list of strings drew
        [[nodiscard]] size_t GetDrawnContentRows() const {
            return _drawnContentRows;
        }

    private:
        void DrawBlocks();
        void DrawContents();

        PoolStringsPanelModel& _model;
        size_t _drawnBlockRows = 0;
        size_t _drawnContentRows = 0;
        PanelSlotHandle _blocksSlot;
        PanelSlotHandle _contentsSlot;
    };

}  // namespace BECore
