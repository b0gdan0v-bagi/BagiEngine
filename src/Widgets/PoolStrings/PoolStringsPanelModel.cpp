#include "PoolStringsPanelModel.h"

#include <BECore/File/Path.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/PoolString/PoolStringsAnswers.h>
#include <BECore/Profiler/Alloc/AllocSiteTable.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Generated/PoolStringsPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/PoolStrings/PoolStringsText.h>

namespace BECore {

    namespace {

        constexpr StringView kCollision = "collision";
        constexpr StringView kDuplicate = "duplicate";
        constexpr StringView kNoFindings = "Hash collisions and texts held twice: none";
        constexpr StringView kNoGrowth = "No runtime string was added in the frames kept";

        struct SourceChoice {
            StringView label;
            Optional<PoolStringOrigin> origin;
        };

        /// The choices of the source filter that do not name a file, in the order of the combo
        const SourceChoice kSourceChoices[] = {
            {"All", std::nullopt},
            {"Static", PoolStringOrigin::Static},
            {"Runtime", PoolStringOrigin::Runtime},
            {"Adopted", PoolStringOrigin::Adopted},
        };
        constexpr size_t kFirstFileChoice = eastl::size(kSourceChoices);

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
        } else if (_view.search != _searchedText || _view.sourceIndex != _searchedSource) {
            Search();
        }
        SyncRecording();
        ReadGrowth();
        ReadRecords();
    }

    void PoolStringsPanelModel::SetSort(PoolStringSortKey key, bool descending) {
        if (key == _sortBy && descending == _sortDescending) {
            return;
        }
        _sortBy = key;
        _sortDescending = descending;
        Search();
    }

    bool PoolStringsPanelModel::RunCommand(StringView id, StringView /*argument*/) {
        if (id == EditorCommandIds::kPoolStringsRefresh.ToPoolString().ToStringView()) {
            TakeSnapshot();
            return true;
        }
        if (id == EditorCommandIds::kPoolStringsClearRecords.ToPoolString().ToStringView()) {
            Monitor().ClearRecords();
            GroupRecords();
            return true;
        }
        return false;
    }

    void PoolStringsPanelModel::TakeSnapshot() {
        _snapshotDue = false;
        Monitor().GetStorage().TakeSnapshot(_snapshot);
        BuildSummary();
        BuildBlocks();
        BuildChecks();  // after the blocks, whose file names it shows
        BuildSourceItems();
        Search();
        GroupRecords();
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

    void PoolStringsPanelModel::BuildSourceItems() {
        String chosenFile;
        const size_t chosen = static_cast<size_t>(_view.sourceIndex);
        if (chosen >= kFirstFileChoice && chosen < _view.sourceItems.size()) {
            chosenFile = _view.sourceItems[chosen];
        }

        Vector<String> files;
        for (const PoolBlockRow& block : _snapshot.blocks) {
            if (block.kind == PoolBlockKind::Adopted && !block.source.empty()) {
                files.push_back(block.source);
            }
        }
        eastl::sort(files.begin(), files.end());
        files.erase(eastl::unique(files.begin(), files.end()), files.end());

        _view.sourceItems.clear();
        for (const SourceChoice& choice : kSourceChoices) {
            _view.sourceItems.push_back(String(choice.label));
        }
        _view.sourceItems.insert(_view.sourceItems.end(), files.begin(), files.end());

        if (!chosenFile.empty()) {
            const auto found = eastl::find(files.begin(), files.end(), chosenFile);
            _view.sourceIndex = found != files.end() ? static_cast<int32_t>(kFirstFileChoice + (found - files.begin())) : 0;
        }
    }

    void PoolStringsPanelModel::Search() {
        // A negative index becomes a huge one here
        if (static_cast<size_t>(_view.sourceIndex) >= _view.sourceItems.size()) {
            _view.sourceIndex = 0;
        }
        _searchedText = _view.search;
        _searchedSource = _view.sourceIndex;

        PoolStringQuery query;
        query.text = StringView(_searchedText);
        const size_t source = static_cast<size_t>(_searchedSource);
        if (source < kFirstFileChoice) {
            query.origin = kSourceChoices[source].origin;
        } else {
            query.blockSource = StringView(_view.sourceItems[source]);
        }
        query.sortBy = _sortBy;
        query.descending = _sortDescending;
        PoolStringAnalysis::Search(_snapshot, query, _matches);
        _view.matchText = Format("{} of {} strings", _matches.size(), _snapshot.strings.size());
        ++_searchCount;
    }

    void PoolStringsPanelModel::SyncRecording() {
        PoolStringMonitor& monitor = Monitor();
        if (_view.recording != _shownRecording) {
            monitor.SetRecording(_view.recording);
        }
        _shownRecording = monitor.IsRecording();
        _view.recording = _shownRecording;
    }

    void PoolStringsPanelModel::ReadGrowth() {
        _growthCount = 0;
        GrowthSums sums;
        Monitor().ForEachRecent(_growth.size(), [this, &sums](const PoolGrowthFrame& frame) {
            _growth[_growthCount++] = frame;
            if (frame.runtimeStrings != 0) {
                ++sums.frames;
                sums.strings += frame.runtimeStrings;
                sums.bytes += frame.runtimeBytes;
            }
        });
        if (_shownGrowth.has_value() && *_shownGrowth == sums) {
            return;
        }
        _shownGrowth = sums;
        _view.hasGrowth = sums.frames != 0;
        _view.growthText = _view.hasGrowth ? Format("Frames with new runtime strings: {} ({} strings, {})", sums.frames, sums.strings, FormatBytes(sums.bytes)) : String(kNoGrowth);
    }

    void PoolStringsPanelModel::ReadRecords() {
        const PoolRecordCounts counts = Monitor().GetRecordCounts();
        ++_framesSinceGroup;
        const bool cleared = counts.clears != _groupedClears;
        const bool grew = counts.records != _records.size() && _framesSinceGroup >= PoolStringsPanelLimits::kRegroupFrames;
        if (cleared || grew) {
            GroupRecords();
        }
        if (_view.recordsText.empty() || counts != _shownCounts) {
            _shownCounts = counts;
            _view.recordsText = Format("Recorded: {}, dropped past the limit: {}", counts.records, counts.dropped);
        }
    }

    void PoolStringsPanelModel::GroupRecords() {
        const PoolStringMonitor& monitor = Monitor();
        const PoolRecordCounts counts = monitor.GetRecordCounts();
        if (counts.records != 0) {
            // Once, so that a recording that goes on grows nothing here
            _records.reserve(PoolStringMonitor::kMaxRecords);
        }
        monitor.GroupByZone(_records, _groups);
        _groupedClears = counts.clears;
        _framesSinceGroup = 0;
        _zones.resize(_groups.size());
        for (size_t i = 0; i < _zones.size(); ++i) {
            const PoolZoneGroup& group = _groups[i];
            PoolPanelZone& zone = _zones[i];
            const char* name = group.zone != nullptr ? group.zone->name : AllocSiteStats::kNoZone;
            zone.group = group;
            zone.label = Format("{} ({} strings, {}, last frame {})", name, group.count, FormatBytes(group.bytes), group.lastFrame);
            zone.location = group.zone != nullptr ? Format("{}:{}", group.zone->file, group.zone->line) : String();
        }
    }

}  // namespace BECore
