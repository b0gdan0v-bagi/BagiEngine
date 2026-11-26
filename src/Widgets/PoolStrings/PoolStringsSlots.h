#pragma once

#include <Widgets/PanelAssets/PanelSlots.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    class PoolStringsPanelModel;
    struct PoolGrowthFrame;

    /**
     * @brief The drawers of `PoolStringsPanel.xml` that the format cannot say: the table of blocks with their fill bars, the sortable list of the
     * strings, the plots of the growth per frame and the tree of the recorded strings by zone
     *
     * Owned by `PoolStringsPanelModel`, which registers them in its `Initialize` and takes them back when it dies. A slot draws only the visible
     * rows of what the model holds and allocates nothing for a row. A click on a header of the strings is told to the model (`SetSort`).
     */
    class PoolStringsSlots {
    public:
        explicit PoolStringsSlots(PoolStringsPanelModel& model) : _model(model) {}

        /// Registers the slots in the global `PanelSlots`; the handles end with this object
        void Register();

        /// How many rows the last draw of the blocks table drew
        [[nodiscard]] size_t GetDrawnBlockRows() const {
            return _drawnBlockRows;
        }

        /// How many rows the last draw of the list of strings drew
        [[nodiscard]] size_t GetDrawnContentRows() const {
            return _drawnContentRows;
        }

        /// How many bars the last draw of the growth plots drew, in both together: only a frame that added a runtime string has one
        [[nodiscard]] size_t GetDrawnGrowthBars() const {
            return _drawnGrowthBars;
        }

        /// How many records the last draw of the zones drew, in every open zone together
        [[nodiscard]] size_t GetDrawnRecordRows() const {
            return _drawnRecordRows;
        }

    private:
        void DrawBlocks();
        void DrawContents();
        void DrawGrowth();
        /// One plot over the frames of the model, the bar of a frame as high as its @p value
        void DrawGrowthPlot(const char* id, const char* title, uint64_t PoolGrowthFrame::* value);
        void DrawZones();

        PoolStringsPanelModel& _model;
        size_t _drawnBlockRows = 0;
        size_t _drawnContentRows = 0;
        size_t _drawnGrowthBars = 0;
        size_t _drawnRecordRows = 0;
        PanelSlotHandle _blocksSlot;
        PanelSlotHandle _contentsSlot;
        PanelSlotHandle _growthSlot;
        PanelSlotHandle _zonesSlot;
    };

}  // namespace BECore
