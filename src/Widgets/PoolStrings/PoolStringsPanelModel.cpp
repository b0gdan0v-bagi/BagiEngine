#include "PoolStringsPanelModel.h"

#include <BECore/File/Path.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/PoolString/PoolStringAnalysis.h>
#include <BECore/PoolString/PoolStringMonitor.h>
#include <BECore/PoolString/PoolStringsAnswers.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <EASTL/algorithm.h>
#include <Generated/PoolStringsPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/PoolStrings/PoolStringsText.h>

namespace BECore {

    namespace {

        constexpr StringView kCollision = "collision";
        constexpr StringView kDuplicate = "duplicate";
        constexpr StringView kNoFindings = "Hash collisions and texts held twice: none";

        StringView BlockKindName(PoolBlockKind kind) {
            switch (kind) {
                case PoolBlockKind::Runtime:
                    return "runtime";
                case PoolBlockKind::Allocated:
                    return "allocated";
                case PoolBlockKind::Adopted:
                    return "adopted";
            }
            return {};
        }

        PoolPanelSourceRow SourceRow(StringView source, size_t strings, size_t bytes, size_t blocks, size_t blockBytes) {
            PoolPanelSourceRow row;
            row.source = String(source);
            row.strings = Format("{}", strings);
            row.bytes = FormatBytes(bytes);
            row.blocks = Format("{}", blocks);
            row.blockBytes = FormatBytes(blockBytes);
            return row;
        }

        PoolPanelSourceRow SourceRow(PoolStringOrigin origin, const PoolSourceStats& stats) {
            return SourceRow(GetPoolStringOriginName(origin), stats.strings, stats.bytes, stats.blocks, stats.blockBytes);
        }

    }  // namespace

    void PoolStringsPanelModel::Initialize() {
        _slots.Register();
    }

    TypedRef PoolStringsPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void PoolStringsPanelModel::SetMonitor(PoolStringMonitor* monitor) {
        _monitor = monitor;
    }

    PoolStringMonitor& PoolStringsPanelModel::Monitor() const {
        return _monitor != nullptr ? *_monitor : CoreManager::GetPoolStringMonitor();
    }

    void PoolStringsPanelModel::OnOpenChanged(bool open) {
        _snapshotDue = open;
    }

    void PoolStringsPanelModel::Update() {
        if (_snapshotDue) {
            TakeSnapshot();
        }
    }

    bool PoolStringsPanelModel::RunCommand(StringView id, StringView /*argument*/) {
        if (id != EditorCommandIds::kPoolStringsRefresh.ToPoolString().ToStringView()) {
            return false;
        }
        TakeSnapshot();
        return true;
    }

    void PoolStringsPanelModel::TakeSnapshot() {
        _snapshotDue = false;
        Monitor().GetStorage().TakeSnapshot(_snapshot);
        BuildSummary();
        BuildBlocks();
        BuildChecks();  // after the blocks, whose file names it shows
    }

    void PoolStringsPanelModel::BuildSummary() {
        const PoolStringStats& stats = _snapshot.stats;
        _view.sources.clear();
        _view.sources.push_back(SourceRow(PoolStringOrigin::Static, stats.staticEntries));
        _view.sources.push_back(SourceRow(PoolStringOrigin::Runtime, stats.runtime));
        _view.sources.push_back(SourceRow(PoolStringOrigin::Adopted, stats.adopted));
        _view.sources.push_back(SourceRow("total", stats.stringCount, stats.stringBytes, stats.blockCount, stats.blockBytes));

        const double load = stats.tableCapacity != 0 ? static_cast<double>(stats.tableUsed) / static_cast<double>(stats.tableCapacity) : 0.0;
        _view.deadText = Format("Dead entries of adopted blocks: {} ({})", stats.deadStrings, FormatBytes(stats.deadBytes));
        _view.tableText = Format("Table: {} of {} slots used (load {:.2f}), longest probe {}", stats.tableUsed, stats.tableCapacity, load, stats.longestProbe);
        _view.internText = Format("Intern calls: {} ({} hits, {} misses)", stats.internCalls, stats.hits, stats.misses);
        _view.batchText = Format("InternBatch calls: {}, strings given: {}", stats.batchCalls, stats.batchStrings);
        _view.countersText = Format("Static duplicates: {}, write locks: {}, heap fallback blocks: {}", stats.staticDuplicates, stats.writeLocks, stats.heapFallbackBlocks);
    }

    void PoolStringsPanelModel::BuildBlocks() {
        _blocks.resize(_snapshot.blocks.size());
        for (size_t i = 0; i < _blocks.size(); ++i) {
            const PoolBlockRow& row = _snapshot.blocks[i];
            PoolPanelBlock& block = _blocks[i];
            block.kind = BlockKindName(row.kind);
            block.file = String(Path(StringView(row.source)).Filename().View());
            block.source = row.source;
            block.usage = Format("{} / {}", FormatBytes(row.used), FormatBytes(row.capacity));
            block.deadStrings = Format("{}", row.deadStrings);
            block.deadBytes = FormatBytes(row.deadBytes);
            block.fill = row.capacity != 0 ? static_cast<float>(row.used) / static_cast<float>(row.capacity) : 0.0f;
        }
    }

    StringView PoolStringsPanelModel::SourceOf(const PoolStringRow& row) const {
        if (row.origin == PoolStringOrigin::Adopted && row.block != kNoPoolBlock && !_blocks[row.block].file.empty()) {
            return StringView(_blocks[row.block].file);
        }
        return GetPoolStringOriginName(row.origin);
    }

    void PoolStringsPanelModel::BuildChecks() {
        Vector<PoolStringAnalysis::RowPair> collisions;
        PoolStringAnalysis::FindHashCollisions(_snapshot, collisions);
        Vector<PoolStringAnalysis::RowPair> duplicates;
        PoolStringAnalysis::FindDuplicateTexts(_snapshot, duplicates);

        const size_t found = collisions.size() + duplicates.size();
        const size_t listed = eastl::min(found, PoolStringsPanelLimits::kMaxCheckRows);
        _view.hasFindings = found != 0;
        _view.checksText = found != 0 ? Format("Hash collisions: {}, texts held twice: {}", collisions.size(), duplicates.size()) : String(kNoFindings);
        _view.checks.resize(listed);
        for (size_t i = 0; i < listed; ++i) {
            const bool collision = i < collisions.size();
            const PoolStringAnalysis::RowPair& pair = collision ? collisions[i] : duplicates[i - collisions.size()];
            const PoolStringRow& first = _snapshot.strings[pair.first];
            const PoolStringRow& second = _snapshot.strings[pair.second];
            const StringView firstText = PoolStringLine(first.text.ToStringView(), PoolStringsPanelLimits::kMaxCheckTextBytes);
            PoolPanelCheckRow& row = _view.checks[i];
            row.kind = String(collision ? kCollision : kDuplicate);
            row.text = collision ? Format("{} / {}", firstText, PoolStringLine(second.text.ToStringView(), PoolStringsPanelLimits::kMaxCheckTextBytes)) : String(firstText);
            row.firstSource = String(SourceOf(first));
            row.secondSource = String(SourceOf(second));
        }
        _view.checksCut = found > listed;
        _view.checksCutText = _view.checksCut ? Format("{} more pairs are not listed", found - listed) : String();
    }

}  // namespace BECore
