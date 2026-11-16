#include "ConsoleWidget.h"

#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogHistorySink.h>
#include <BECore/Logger/LogRingBuffer.h>
#include <BECore/Logger/LoggerManager.h>
#include <Generated/ConsoleWidget.gen.hpp>
#include <Widgets/Console/ConsoleCommandLine.h>
#include <Widgets/Console/EditorLauncher.h>
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
        /// The output area shows this many lines before it scrolls
        constexpr size_t kOutputVisibleLines = 6;
        constexpr const char* kCandidateSeparator = "  ";

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
        Subscribe<EditorPreferencesChangedEvent, &ConsoleWidget::OnPreferencesChanged>(this);
        _sink = CoreManager::GetLoggerManager().GetSink<LogHistorySink>();
        return true;
    }

    void ConsoleWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewConsole) {
            _open = !_open;
        }
    }

    void ConsoleWidget::OnPreferencesChanged(const EditorPreferencesChangedEvent& event) {
        _externalEditor = event.externalEditor;
        if (!_historyLoaded) {
            _historyLoaded = true;
            _session.GetHistory().Set(event.consoleHistory);
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
                DrawRows(*history, GetFooterHeight());
            }
            DrawOutput();
            DrawCommandLine();
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

    void ConsoleWidget::OpenSite(const char* file, uint32_t line) {
        String error;
        if (!LaunchEditor(_externalEditor, file, line, error)) {
            LOG_WARNING(Format("Console: cannot open {}:{}: {}", file, line, error));
        }
    }

    void ConsoleWidget::DrawRows(const LogHistory& history, float footerHeight) {
        const Vector<uint32_t>& rows = _view.GetRows();
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImGuiPalette& palette = GetImGuiPalette();
        ImGui::BeginChild("##consoleRows", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
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
                const bool openable = siteFile != nullptr && IsSiteOpenable(siteFile);
                ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick);
                if (openable) {
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            OpenSite(siteFile, record.site.line);
                        }
                    }
                    ItemTooltip("Double-click to open");
                }
                if (ImGui::BeginPopupContextItem("##rowMenu")) {
                    if (ImGui::MenuItem(ICON_LC_COPY " Copy line")) {
                        String text = Format("{} {} {}", record.time, ShortName(record.level), record.message);
                        if (siteFile != nullptr) {
                            text += Format("  {}:{}", FileNameOf(siteFile), record.site.line);
                        }
                        ImGui::SetClipboardText(text.c_str());
                    }
                    if (ImGui::MenuItem(ICON_LC_EXTERNAL_LINK " Open file", nullptr, false, openable)) {
                        OpenSite(siteFile, record.site.line);
                    }
                    if (!openable) {
                        ItemTooltip(siteFile == nullptr ? "This line has no source file" : "The source file is not an absolute path that exists on this machine");
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

    bool ConsoleWidget::HasCandidates() const {
        return !_candidates.empty() && _candidatesFor == _commandBuffer;
    }

    float ConsoleWidget::GetFooterHeight() const {
        const ImGuiStyle& style = ImGui::GetStyle();
        float height = style.ItemSpacing.y + ImGui::GetFrameHeight();
        if (HasCandidates()) {
            height += ImGui::GetTextLineHeightWithSpacing();
        }
        const size_t lines = _session.GetOutput().Size();
        if (lines != 0) {
            height += static_cast<float>(std::min(lines, kOutputVisibleLines)) * ImGui::GetTextLineHeightWithSpacing() + style.WindowPadding.y * 2.0f + style.ItemSpacing.y;
        }
        return height;
    }

    void ConsoleWidget::DrawOutput() {
        const ConsoleOutput& output = _session.GetOutput();
        const size_t count = output.Size();
        if (count == 0) {
            return;
        }
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImGuiPalette& palette = GetImGuiPalette();
        const float height = static_cast<float>(std::min(count, kOutputVisibleLines)) * ImGui::GetTextLineHeightWithSpacing() + style.WindowPadding.y * 2.0f;
        ImGui::BeginChild("##consoleOutput", ImVec2(0.0f, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(count));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const ConsoleOutputLine& line = output.At(static_cast<size_t>(i));
                ImVec4 color = palette.text;
                switch (line.kind) {
                    case ConsoleOutputKind::Result:
                        color = palette.success;
                        break;
                    case ConsoleOutputKind::Error:
                        color = palette.error;
                        break;
                    case ConsoleOutputKind::Note:
                        color = palette.textDisabled;
                        break;
                    default:
                        break;
                }
                if (line.kind == ConsoleOutputKind::Input) {
                    TextColored(color, "> ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                TextColored(color, line.text.c_str(), line.text.c_str() + line.text.size());
            }
        }
        clipper.End();
        if (output.GetVersion() != _outputVersion) {
            _outputVersion = output.GetVersion();
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    void ConsoleWidget::RefreshMethods() {
        const ControlService& service = CoreManager::GetControlService();
        if (!_methodsKnown || service.GetMethodsVersion() != _methodsVersion) {
            _methods = service.GetMethods();
            _methodsVersion = service.GetMethodsVersion();
            _methodsKnown = true;
        }
    }

    void ConsoleWidget::DrawCommandLine() {
        RefreshMethods();
        if (HasCandidates()) {
            TextColored(GetImGuiPalette().textDisabled, _candidates.c_str(), _candidates.c_str() + _candidates.size());
        }

        constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
        ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x));
        const bool entered = ImGui::InputTextWithHint("##consoleCommand", "ping, step frames=3, list_methods ... (Tab completes)", _commandBuffer, sizeof(_commandBuffer), kFlags,
                                                      &ConsoleWidget::OnInputCallback, this);
        ItemTooltip("Enter runs a control API command: method, method {\"a\":1} or method key=value. Tab completes, Up and Down walk the history");
        if (entered) {
            const Optional<String> command = _session.Submit(CoreManager::GetControlService(), _commandBuffer);
            if (command) {
                ConsoleCommandSubmittedEvent::Emit(*command);
                _commandBuffer[0] = '\0';  // a line that did not parse stays, to be fixed
            }
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::SameLine();
        if (IconButton(ICON_LC_ERASER, "Clear output", "consoleClearOutput")) {
            _session.GetOutput().Clear();
        }
    }

    namespace {

        void ReplaceText(ImGuiInputTextCallbackData& data, StringView text) {
            data.DeleteChars(0, data.BufTextLen);
            data.InsertChars(0, text.data(), text.data() + text.size());
        }

    }  // namespace

    void ConsoleWidget::Complete(ImGuiInputTextCallbackData& data) {
        const StringView text(data.Buf, static_cast<size_t>(data.BufTextLen));
        const ConsoleCompletion completion = CompleteConsoleCommand(text, _methods);
        if (StringView(completion.text) != text) {
            ReplaceText(data, completion.text);
        }
        _candidates.clear();
        for (const String& candidate : completion.candidates) {
            if (!_candidates.empty()) {
                _candidates += kCandidateSeparator;
            }
            _candidates += candidate;
        }
        _candidatesFor.assign(data.Buf, static_cast<size_t>(data.BufTextLen));
    }

    void ConsoleWidget::WalkHistory(ImGuiInputTextCallbackData& data) {
        ConsoleCommandHistory& history = _session.GetHistory();
        const Optional<StringView> text = data.EventKey == ImGuiKey_UpArrow ? history.Previous(StringView(data.Buf, static_cast<size_t>(data.BufTextLen))) : history.Next();
        if (text) {
            ReplaceText(data, *text);
        }
    }

    int ConsoleWidget::OnInputCallback(ImGuiInputTextCallbackData* data) {
        auto* self = static_cast<ConsoleWidget*>(data->UserData);
        if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
            self->Complete(*data);
        } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
            self->WalkHistory(*data);
        }
        return 0;
    }

}  // namespace BECore
