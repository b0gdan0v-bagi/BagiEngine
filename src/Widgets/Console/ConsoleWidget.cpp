#include "ConsoleWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogHistorySink.h>
#include <BECore/Logger/LogRingBuffer.h>
#include <BECore/Logger/LoggerManager.h>
#include <Generated/ConsoleWidget.gen.hpp>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <cstdio>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr size_t kLabelSize = 96;
        constexpr size_t kSiteTextSize = 160;
        constexpr float kCategoryComboWidthInEm = 11.0f;
        constexpr float kFilterWidthInEm = 14.0f;
        /// Closer than this to the end of the list counts as at the bottom
        constexpr float kBottomSlack = 1.0f;

        /// A toolbar toggle: Debug, Info, Warning, and Error with Fatal
        struct LevelGroup {
            const char* name;
            uint32_t mask;
            LogLevel color;  ///< the level whose colour the toggle takes
        };

        constexpr LevelGroup kLevelGroups[] = {
            {"Debug", LogLevelBit(LogLevel::Debug), LogLevel::Debug},
            {"Info", LogLevelBit(LogLevel::Info), LogLevel::Info},
            {"Warning", LogLevelBit(LogLevel::Warning), LogLevel::Warning},
            {"Error", LogLevelBit(LogLevel::Error) | LogLevelBit(LogLevel::Fatal), LogLevel::Error},
        };

        const char* ShortName(LogLevel level) {
            switch (level) {
                case LogLevel::Debug:
                    return "DBG";
                case LogLevel::Info:
                    return "INF";
                case LogLevel::Warning:
                    return "WRN";
                case LogLevel::Error:
                    return "ERR";
                case LogLevel::Fatal:
                    return "FTL";
                default:
                    return "???";
            }
        }

        ImVec4 ColorOf(LogLevel level) {
            const ImGuiPalette& palette = GetImGuiPalette();
            switch (level) {
                case LogLevel::Debug:
                    return palette.textDisabled;
                case LogLevel::Warning:
                    return palette.warning;
                case LogLevel::Error:
                case LogLevel::Fatal:
                    return palette.error;
                default:
                    return palette.text;
            }
        }

        uint32_t CountOf(const LogHistory& history, uint32_t mask) {
            uint32_t count = 0;
            for (size_t level = 0; level < EnumUtils<LogLevel>::Count(); ++level) {
                if ((mask & (1U << level)) != 0) {
                    count += history.GetCount(static_cast<LogLevel>(level));
                }
            }
            return count;
        }

        const char* FileNameOf(const char* path) {
            const char* name = path;
            for (const char* c = path; *c != '\0'; ++c) {
                if (*c == '/' || *c == '\\') {
                    name = c + 1;
                }
            }
            return name;
        }

        bool ToggleButton(const char* label, bool on, LogLevel level) {
            const ImGuiPalette& palette = GetImGuiPalette();
            ImGui::PushStyleColor(ImGuiCol_Text, on ? ColorOf(level) : palette.textDisabled);
            if (on) {
                ImGui::PushStyleColor(ImGuiCol_Button, palette.accentMuted);
            }
            const bool clicked = ImGui::Button(label);
            ImGui::PopStyleColor(on ? 2 : 1);
            return clicked;
        }

        void TextColored(const ImVec4& color, const char* begin, const char* end = nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(begin, end);
            ImGui::PopStyleColor();
        }

    }  // namespace

    bool ConsoleWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorCommandEvent, &ConsoleWidget::OnCommand>(this);
        _sink = CoreManager::GetLoggerManager().GetSink<LogHistorySink>();
        return true;
    }

    void ConsoleWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewConsole) {
            _open = !_open;
        }
    }

    void ConsoleWidget::Draw() {}

    void ConsoleWidget::Update() {
        if (!_open) {
            return;
        }
        LogHistory* history = _sink != nullptr ? _sink->GetHistory() : nullptr;
        if (history != nullptr) {
            SyncTitle(*history);
        }
        ImGui::SetNextWindowSize(ImVec2(720.0f, 280.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(_title.c_str(), &_open)) {
            if (history == nullptr) {
                EmptyState(ICON_LC_SCROLL_TEXT, "No log history: the LoggerConfig.xml in use has no LogHistorySink entry");
            } else {
                DrawToolbar(*history);
                _view.Update(*history, _filter, _paused);
                DrawRows(*history);
            }
        }
        ImGui::End();
    }

    void ConsoleWidget::SyncTitle(const LogHistory& history) {
        const uint32_t errors = history.GetCount(LogLevel::Error) + history.GetCount(LogLevel::Fatal);
        const uint32_t warnings = history.GetCount(LogLevel::Warning);
        if (errors != _titleErrors || warnings != _titleWarnings) {
            _titleErrors = errors;
            _titleWarnings = warnings;
            FormatConsoleTitle(history, _title);
        }
    }

    void ConsoleWidget::DrawToolbar(LogHistory& history) {
        char label[kLabelSize];
        for (const LevelGroup& group : kLevelGroups) {
            const bool on = (_filter.levelMask & group.mask) == group.mask;
            snprintf(label, sizeof(label), "%s %u###level%s", group.name, CountOf(history, group.mask), group.name);
            if (ToggleButton(label, on, group.color)) {
                _filter.levelMask = on ? (_filter.levelMask & ~group.mask) : (_filter.levelMask | group.mask);
                ++_filter.revision;
            }
            ItemTooltip(on ? "Hide these lines" : "Show these lines");
            ImGui::SameLine();
        }

        DrawCategoryCombo(history);
        ImGui::SameLine();

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFilterWidthInEm);
        if (ImGui::InputTextWithHint("##consoleFilter", ICON_LC_FILTER " Filter text", _textBuffer, sizeof(_textBuffer))) {
            _filter.text.assign(_textBuffer);
            ++_filter.revision;
        }
        ItemTooltip("Lines whose text holds this (upper and lower case alike)");
        ImGui::SameLine();

        if (IconTextButton(_paused ? ICON_LC_PLAY : ICON_LC_PAUSE, _paused ? "Resume##consolePause" : "Pause##consolePause",
                           _paused ? "Show the lines that came while paused" : "Freeze the list; the lines keep coming behind it")) {
            _paused = !_paused;
        }
        ImGui::SameLine();
        if (IconTextButton(ICON_LC_TRASH_2, "Clear", "Remove every line of the log history")) {
            history.Clear();
        }

        const size_t lost = LogRingBuffer<>::GetGlobal().GetDroppedCount();
        if (lost != 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%zu lost", lost);
            ItemTooltip("Lines that did not fit the log queue before they reached the history");
        }
    }

    void ConsoleWidget::DrawCategoryCombo(const LogHistory& history) {
        const size_t count = history.GetCategoryCount();
        const bool known = _filter.category.has_value() && *_filter.category < count;
        const StringView current = known ? history.GetCategoryName(*_filter.category) : StringView("All");
        char preview[kLabelSize];
        snprintf(preview, sizeof(preview), "%.*s", static_cast<int>(current.size()), current.data());

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kCategoryComboWidthInEm);
        if (ImGui::BeginCombo("##consoleCategory", preview)) {
            if (ImGui::Selectable("All", !known) && known) {
                _filter.category.reset();
                ++_filter.revision;
            }
            for (size_t i = 0; i < count; ++i) {
                const StringView name = history.GetCategoryName(i);
                snprintf(preview, sizeof(preview), "%.*s", static_cast<int>(name.size()), name.data());
                const bool selected = known && *_filter.category == i;
                if (ImGui::Selectable(preview, selected) && !selected) {
                    _filter.category = static_cast<uint16_t>(i);
                    ++_filter.revision;
                }
            }
            ImGui::EndCombo();
        }
        ItemTooltip("The module the line comes from");
    }

    void ConsoleWidget::DrawRows(const LogHistory& history) {
        const Vector<uint32_t>& rows = _view.GetRows();
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImGuiPalette& palette = GetImGuiPalette();
        ImGui::BeginChild("##consoleRows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        if (rows.empty()) {
            EmptyState(ICON_LC_SCROLL_TEXT, history.Size() == 0 ? "No log lines yet" : "No line passes the filter");
            ImGui::EndChild();
            return;
        }

        // The state of the last frame: the list grows below the view, so this frame's rows are not in the scroll range yet
        const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - kBottomSlack;

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const LogRecord& record = history.At(rows[static_cast<size_t>(i)]);
                const char* siteFile = record.site.file;
                ImGui::PushID(static_cast<int>(record.sequence));

                // A selectable under the texts gives the whole row a hover highlight and a place for the menu
                const ImVec2 rowStart = ImGui::GetCursorPos();
                ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap);
                if (ImGui::BeginPopupContextItem("##rowMenu")) {
                    if (ImGui::MenuItem(ICON_LC_COPY " Copy line")) {
                        String text = Format("{} {} {}", record.time, ShortName(record.level), record.message);
                        if (siteFile != nullptr) {
                            text += Format("  {}:{}", FileNameOf(siteFile), record.site.line);
                        }
                        ImGui::SetClipboardText(text.c_str());
                    }
                    ImGui::EndPopup();
                }
                ImGui::SetCursorPos(rowStart);

                const ImVec4 color = ColorOf(record.level);
                {
                    const ScopedMonoFont mono;
                    TextColored(palette.textDisabled, record.time);
                    ImGui::SameLine();
                    TextColored(color, ShortName(record.level));
                }
                ImGui::SameLine();
                TextColored(color, record.message.c_str(), record.message.c_str() + record.message.size());

                if (siteFile != nullptr) {
                    char site[kSiteTextSize];
                    snprintf(site, sizeof(site), "%s:%u", FileNameOf(siteFile), record.site.line);
                    const float width = ImGui::CalcTextSize(site).x;
                    const float afterMessage = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX();
                    const float rightEdge = ImGui::GetScrollX() + ImGui::GetWindowWidth() - style.ScrollbarSize - style.WindowPadding.x - width;
                    ImGui::SameLine(std::max(afterMessage + style.ItemSpacing.x, rightEdge));
                    TextColored(palette.textDisabled, site);
                    ItemTooltip(siteFile);
                }
                ImGui::PopID();
            }
        }
        clipper.End();

        if (atBottom && !_paused) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

}  // namespace BECore
