#include "ConsolePanelModel.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogHistorySink.h>
#include <BECore/Logger/LogRingBuffer.h>
#include <BECore/Logger/LoggerManager.h>
#include <Generated/ConsolePanelModel.gen.hpp>

namespace BECore {

    namespace {

        constexpr StringView kAllCategories = "All";
        constexpr StringView kLostSuffix = " lost";
        constexpr uint32_t kNoCount = std::numeric_limits<uint32_t>::max();

        /// A toolbar toggle: the view fields it owns and the levels it switches. Error carries Fatal, as the old toolbar did
        struct LevelGroup {
            StringView name;
            uint32_t mask;
            bool ConsolePanelView::* shown;
            String ConsolePanelView::* label;
        };

        constexpr size_t kLevelGroupCount = 4;
        constexpr LevelGroup kLevelGroups[kLevelGroupCount] = {
            {"Debug", LogLevelBit(LogLevel::Debug), &ConsolePanelView::showDebug, &ConsolePanelView::debugLabel},
            {"Info", LogLevelBit(LogLevel::Info), &ConsolePanelView::showInfo, &ConsolePanelView::infoLabel},
            {"Warning", LogLevelBit(LogLevel::Warning), &ConsolePanelView::showWarning, &ConsolePanelView::warningLabel},
            {"Error", LogLevelBit(LogLevel::Error) | LogLevelBit(LogLevel::Fatal), &ConsolePanelView::showError, &ConsolePanelView::errorLabel},
        };

        uint32_t CountOf(const LogHistory& history, uint32_t mask) {
            uint32_t count = 0;
            for (size_t level = 0; level < EnumUtils<LogLevel>::Count(); ++level) {
                if ((mask & LogLevelBit(static_cast<LogLevel>(level))) != 0) {
                    count += history.GetCount(static_cast<LogLevel>(level));
                }
            }
            return count;
        }

        /// The longest prefix of @p text within @p limit bytes that does not end inside a UTF-8 sequence
        StringView CutAtBoundary(StringView text, size_t limit) {
            if (text.size() <= limit) {
                return text;
            }
            constexpr unsigned char kContinuationMask = 0xC0;
            constexpr unsigned char kContinuation = 0x80;
            size_t end = limit;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & kContinuationMask) == kContinuation) {
                --end;
            }
            return text.substr(0, end);
        }

        bool SetIf(String& field, StringView value) {
            if (StringView(field) == value) {
                return false;
            }
            field.assign(value.data(), value.size());
            return true;
        }

    }  // namespace

    ConsolePanelModel::ConsolePanelModel() {
        _view.categories.push_back(String(kAllCategories));
        for (uint32_t& count : _labelCounts) {
            count = kNoCount;
        }
    }

    void ConsolePanelModel::Initialize() {
        Subscribe<EditorPreferencesChangedEvent, &ConsolePanelModel::OnPreferencesChanged>(this);
        _slots.Register();
    }

    TypedRef ConsolePanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void ConsolePanelModel::SetHistory(LogHistory* history) {
        _history = history;
    }

    LogHistory* ConsolePanelModel::GetHistory() const {
        if (_history != nullptr) {
            return _history;
        }
        LogHistorySink* sink = CoreManager::GetLoggerManager().GetSink<LogHistorySink>();
        return sink != nullptr ? sink->GetHistory() : nullptr;
    }

    void ConsolePanelModel::OnPreferencesChanged(const EditorPreferencesChangedEvent& event) {
        _externalEditor = event.externalEditor;
        if (!_historyLoaded) {
            _historyLoaded = true;
            _session.GetHistory().Set(event.consoleHistory);
        }
    }

    void ConsolePanelModel::Update() {
        SyncLost();
        LogHistory* history = GetHistory();
        _view.hasHistory = history != nullptr;
        if (history == nullptr) {
            return;
        }
        SyncCategories(*history);
        SyncFilter();
        SyncLabels(*history);
        SyncTitle(*history);
        _logView.Update(*history, _filter, _view.paused);
    }

    void ConsolePanelModel::SyncLabels(const LogHistory& history) {
        for (size_t i = 0; i < kLevelGroupCount; ++i) {
            const LevelGroup& group = kLevelGroups[i];
            const uint32_t count = CountOf(history, group.mask);
            if (count != _labelCounts[i]) {
                _labelCounts[i] = count;
                (_view.*group.label) = Format("{} {}", group.name, count);
            }
        }
    }

    void ConsolePanelModel::SyncCategories(const LogHistory& history) {
        const size_t count = history.GetCategoryCount();
        if (_view.categories.size() != count + 1) {
            _view.categories.resize(count + 1);
            for (size_t i = 0; i < count; ++i) {
                _view.categories[i + 1] = String(history.GetCategoryName(i));
            }
        }
        if (_view.category < 0 || static_cast<size_t>(_view.category) > count) {
            _view.category = 0;
        }
    }

    void ConsolePanelModel::SyncFilter() {
        uint32_t mask = 0;
        for (const LevelGroup& group : kLevelGroups) {
            if (_view.*group.shown) {
                mask |= group.mask;
            }
        }
        Optional<uint16_t> category;
        if (_view.category > 0) {
            category = static_cast<uint16_t>(_view.category - 1);
        }
        const StringView text = CutAtBoundary(StringView(_view.filterText), kMaxConsoleFilterLength);

        bool changed = false;
        if (mask != _filter.levelMask) {
            _filter.levelMask = mask;
            changed = true;
        }
        if (category != _filter.category) {
            _filter.category = category;
            changed = true;
        }
        changed = SetIf(_filter.text, text) || changed;
        if (changed) {
            ++_filter.revision;
        }
    }

    void ConsolePanelModel::SyncTitle(const LogHistory& history) {
        const uint32_t errors = history.GetCount(LogLevel::Error) + history.GetCount(LogLevel::Fatal);
        const uint32_t warnings = history.GetCount(LogLevel::Warning);
        if (!_titleBuilt || errors != _titleErrors || warnings != _titleWarnings) {
            _titleBuilt = true;
            _titleErrors = errors;
            _titleWarnings = warnings;
            FormatConsoleTitle(history, _view.title);
        }
    }

    void ConsolePanelModel::SyncLost() {
        const size_t lost = LogRingBuffer<>::GetGlobal().GetDroppedCount();
        if (_lostBuilt && lost == _lostCount) {
            return;
        }
        _lostBuilt = true;
        _lostCount = lost;
        _view.lost = static_cast<uint32_t>(std::min<size_t>(lost, std::numeric_limits<uint32_t>::max()));
        _view.lostText = Format("{}{}", _view.lost, kLostSuffix);
    }

    bool ConsolePanelModel::RunCommand(StringView id, StringView /*argument*/) {
        const auto is = [id](PoolString command) { return id == command.ToStringView(); };
        if (is(EditorCommandIds::kConsolePause)) {
            _view.paused = !_view.paused;
            return true;
        }
        if (is(EditorCommandIds::kConsoleClear)) {
            if (LogHistory* history = GetHistory()) {
                history->Clear();
            }
            return true;
        }
        if (is(EditorCommandIds::kConsoleClearOutput)) {
            _session.GetOutput().Clear();
            return true;
        }
        return false;
    }

}  // namespace BECore
