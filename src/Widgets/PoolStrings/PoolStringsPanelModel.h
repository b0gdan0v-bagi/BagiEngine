#pragma once

#include <BECore/PoolString/PoolStringAnalysis.h>
#include <BECore/PoolString/PoolStringSnapshot.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/PoolStrings/PoolStringsSlots.h>
#include <cstddef>
#include <cstdint>

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

    /**
     * @brief What `PoolStringsPanel.xml` binds to: the texts of one snapshot of the pool and the choices of its contents list
     *
     * `search` and `sourceIndex` are written by the panel's controls with no command and read by the model in its next `Update`; the model writes the rest.
     */
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

        /// A part of the strings the contents list shows, ASCII case ignored
        BE_REFLECT_FIELD String search;
        /// An index into `sourceItems`; one outside it means the first, every source
        BE_REFLECT_FIELD int32_t sourceIndex = 0;
        /// All, static, runtime, adopted, then the source names of the adopted blocks of the snapshot, sorted
        BE_REFLECT_FIELD Vector<String> sourceItems;
        BE_REFLECT_FIELD String matchText;
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
     * The contents list is `GetMatches`, the rows of the snapshot that the view's choices and the sort select; the search walks every string, so it
     * runs only when the snapshot, a choice or the sort changed.
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

        /// The last snapshot; empty before the first one
        [[nodiscard]] const PoolStringSnapshot& GetSnapshot() const {
            return _snapshot;
        }

        /// The listed strings as indices into `GetSnapshot().strings`, in the order of the sort
        [[nodiscard]] const Vector<uint32_t>& GetMatches() const {
            return _matches;
        }

        /// How many searches the model has run
        [[nodiscard]] uint64_t GetSearchCount() const {
            return _searchCount;
        }

        /// The order of the contents list; a change searches at once, so the frame of a header click draws the new order
        void SetSort(PoolStringSortKey key, bool descending);

        /// `static`, `runtime`, the file name of the adopted block of @p row (a row of `GetSnapshot`), or `adopted` for a block with none
        [[nodiscard]] StringView SourceOf(const PoolStringRow& row) const;

        [[nodiscard]] const PoolStringsSlots& GetSlots() const {
            return _slots;
        }

    private:
        [[nodiscard]] PoolStringMonitor& Monitor() const;
        void TakeSnapshot();
        void BuildSummary();
        void BuildBlocks();
        void BuildChecks();
        /// Keeps a chosen file chosen by its name, or goes back to every source when the new snapshot has no such file
        void BuildSourceItems();
        void Search();

        PoolStringsPanelView _view;
        PoolStringsSlots _slots{*this};
        PoolStringMonitor* _monitor = nullptr;
        bool _snapshotDue = false;
        PoolStringSnapshot _snapshot;
        Vector<PoolPanelBlock> _blocks;
        Vector<uint32_t> _matches;
        PoolStringSortKey _sortBy = PoolStringSortKey::Text;
        bool _sortDescending = false;
        // The choices of the view that `_matches` answers
        String _searchedText;
        int32_t _searchedSource = 0;
        uint64_t _searchCount = 0;
    };

}  // namespace BECore
