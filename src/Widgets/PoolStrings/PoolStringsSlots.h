#pragma once

#include <Widgets/PanelAssets/PanelSlots.h>
#include <cstddef>

namespace BECore {

    class PoolStringsPanelModel;

    /**
     * @brief The drawers of `PoolStringsPanel.xml` that the format cannot say: the table of blocks with their fill bars
     *
     * Owned by `PoolStringsPanelModel`, which registers them in its `Initialize` and takes them back when it dies. A slot draws the rows the model
     * made when it took the snapshot, only the visible ones, and formats nothing.
     */
    class PoolStringsSlots {
    public:
        explicit PoolStringsSlots(PoolStringsPanelModel& model) : _model(model) {}

        /// Registers the slot in the global `PanelSlots`; the handle ends with this object
        void Register();

        /// How many rows the last draw of the blocks table drew
        [[nodiscard]] size_t GetDrawnBlockRows() const {
            return _drawnBlockRows;
        }

    private:
        void DrawBlocks();

        PoolStringsPanelModel& _model;
        size_t _drawnBlockRows = 0;
        PanelSlotHandle _blocksSlot;
    };

}  // namespace BECore
