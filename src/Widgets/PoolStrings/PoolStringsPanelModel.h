#pragma once

#include <BECore/PoolString/PoolStringSnapshot.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/PoolStrings/PoolStringsSlots.h>
#include <cstddef>

namespace BECore {

    class PoolStringMonitor;

    struct PoolStringsPanelLimits {
        /// Pairs the checks list; the rest are counted in a line
        static constexpr size_t kMaxCheckRows = 100;
        /// Bytes of one text of a listed pair
        static constexpr size_t kMaxCheckTextBytes = 80;
    };

    /// A row of the summary: what one source of strings holds, or the whole pool
    struct PoolPanelSourceRow {
        BE_CLASS(PoolPanelSourceRow)

        BE_REFLECT_FIELD String source;
        BE_REFLECT_FIELD String strings;
        BE_REFLECT_FIELD String bytes;
        BE_REFLECT_FIELD String blocks;
        BE_REFLECT_FIELD String blockBytes;
    };

    /// A pair of entries that a check found
    struct PoolPanelCheckRow {
        BE_CLASS(PoolPanelCheckRow)

        BE_REFLECT_FIELD String kind;
        /// Both texts of a collision, the one text of a duplicate
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD String firstSource;
        BE_REFLECT_FIELD String secondSource;
    };

    /// What `PoolStringsPanel.xml` binds to: the texts of one snapshot of the pool
    struct PoolStringsPanelView {
        BE_CLASS(PoolStringsPanelView)

        /// Static, runtime, adopted and the total
        BE_REFLECT_FIELD Vector<PoolPanelSourceRow> sources;
        BE_REFLECT_FIELD String deadText;
        BE_REFLECT_FIELD String tableText;
        BE_REFLECT_FIELD String internText;
        BE_REFLECT_FIELD String batchText;
        BE_REFLECT_FIELD String countersText;

        BE_REFLECT_FIELD bool hasFindings = false;
        BE_REFLECT_FIELD String checksText;
        BE_REFLECT_FIELD Vector<PoolPanelCheckRow> checks;
        BE_REFLECT_FIELD bool checksCut = false;
        BE_REFLECT_FIELD String checksCutText;
    };

    /// A block of the snapshot as the blocks table draws it
    struct PoolPanelBlock {
        StringView kind;  ///< A literal
        String file;      ///< The file name of `source`; empty for a block that has none
        String source;    ///< `PoolBlockRow::source`
        String usage;
        String deadStrings;
        String deadBytes;
        float fill = 0.0f;  ///< Used over capacity
    };

    /**
     * @brief The model of the Pool Strings panel asset: one snapshot of a pool and the texts made from it, with no ImGui
     *
     * A snapshot walks the whole table under the pool's lock, so no frame takes one by itself: it is taken in the first `Update` after the panel
     * opens and by the command `poolStrings.refresh` (run only while the panel is open). Every text, row and check result is made then, so the
     * numbers agree with each other and a later frame formats nothing. The model never interns: showing the pool must not grow it.
     *
     * It watches the pool of `PoolStringMonitor::GetStorage()`. The slots (`PoolStringsSlots`, registered in `Initialize`) draw from the model.
     */
    class PoolStringsPanelModel : public IPanelModel {
        BE_CLASS(PoolStringsPanelModel)
    public:
        PoolStringsPanelModel() = default;

        void Initialize() override;
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        void OnOpenChanged(bool open) override;
        bool RunCommand(StringView id, StringView argument) override;

        /// Watches the pool of this monitor instead of `CoreManager::GetPoolStringMonitor()` (a test); it must outlive the model. Null goes back
        void SetMonitor(PoolStringMonitor* monitor);

        [[nodiscard]] const PoolStringsPanelView& GetView() const {
            return _view;
        }

        /// In the order of the snapshot's blocks, so `PoolStringRow::block` indexes it
        [[nodiscard]] const Vector<PoolPanelBlock>& GetBlocks() const {
            return _blocks;
        }

        [[nodiscard]] const PoolStringsSlots& GetSlots() const {
            return _slots;
        }

    private:
        [[nodiscard]] PoolStringMonitor& Monitor() const;
        void TakeSnapshot();
        void BuildSummary();
        void BuildBlocks();
        void BuildChecks();
        /// `static`, `runtime`, the file name of the string's adopted block, or `adopted` for a block with none
        [[nodiscard]] StringView SourceOf(const PoolStringRow& row) const;

        PoolStringsPanelView _view;
        PoolStringsSlots _slots{*this};
        PoolStringMonitor* _monitor = nullptr;
        bool _snapshotDue = false;
        PoolStringSnapshot _snapshot;
        Vector<PoolPanelBlock> _blocks;
    };

}  // namespace BECore
