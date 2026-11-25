#include "PanelInterpreter.h"

#include <BECore/Reflection/TypedRef.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/EditorLayout/IconTable.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <cfloat>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr const char* kUnreadable = "?";
        constexpr const char* kSelectableSuffix = "##select";
        constexpr const char* kIdSuffix = "##";
        /// The unlimited width of ImGui: the item ends at the right edge of the window
        constexpr float kFillWidth = -FLT_MIN;
        /// The glyph of an icon and the text after it are one label
        constexpr const char* kGlyphGap = " ";

        /// How the text of a binding is parsed: the forms differ in what they accept
        enum class BindingForm : uint8_t { Field, Condition, Object };

        PreparedBinding PrepareBinding(StringView text, BindingForm form) {
            PreparedBinding prepared;
            if (text.empty()) {
                return prepared;
            }
            auto parsed = form == BindingForm::Condition ? ParseCondition(text) : form == BindingForm::Object ? ParseObjectBinding(text) : ParseBinding(text);
            if (parsed.has_value()) {
                prepared.state = PreparedBindingState::Ok;
                prepared.binding = std::move(*parsed);
            } else {
                prepared.state = PreparedBindingState::Bad;
            }
            return prepared;
        }

        const char* GlyphOf(StringView icon) {
            return icon.empty() ? nullptr : FindIcon(icon);
        }

        /// The hint of an input: its text, after the glyph of its icon when it has one
        String HintOf(const char* glyph, StringView hint) {
            String text;
            if (glyph != nullptr) {
                text.append(glyph);
                if (!hint.empty()) {
                    text.append(kGlyphGap);
                }
            }
            text.append(hint.data(), hint.size());
            return text;
        }

        /// How many bytes of @p text fit in @p capacity without cutting a UTF-8 sequence in half
        size_t FittingBytes(StringView text, size_t capacity) {
            if (text.size() <= capacity) {
                return text.size();
            }
            size_t size = capacity;
            // A byte of the form 10xxxxxx continues the sequence before it
            while (size > 0 && (static_cast<uint8_t>(text[size]) & 0xC0) == 0x80) {
                --size;
            }
            return size;
        }

        const char* OrNull(const String& text) {
            return text.empty() ? nullptr : text.c_str();
        }

        void TextOf(StringView text) {
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
        }

        void DrawUnreadable() {
            ImGui::TextDisabled("%s", kUnreadable);
        }

        /// The colour of a text style; null for the normal one, which keeps the theme's text colour
        const ImVec4* ColorOf(PanelTextStyle style) {
            const ImGuiPalette& palette = GetImGuiPalette();
            switch (style) {
                case PanelTextStyle::Disabled:
                    return &palette.textDisabled;
                case PanelTextStyle::Success:
                    return &palette.success;
                case PanelTextStyle::Warning:
                    return &palette.warning;
                case PanelTextStyle::Error:
                    return &palette.error;
                default:
                    return nullptr;
            }
        }

        /// Pushes the colour of a style for the next item; the destructor pops it
        class ScopedTextStyle {
        public:
            explicit ScopedTextStyle(PanelTextStyle style) {
                if (const ImVec4* color = ColorOf(style)) {
                    ImGui::PushStyleColor(ImGuiCol_Text, *color);
                    _pushed = true;
                }
            }
            ~ScopedTextStyle() {
                if (_pushed) {
                    ImGui::PopStyleColor();
                }
            }
            ScopedTextStyle(const ScopedTextStyle&) = delete;
            ScopedTextStyle& operator=(const ScopedTextStyle&) = delete;

        private:
            bool _pushed = false;
        };

        /// An id of the walk: `PushID` of a text or an index, popped when the scope ends
        class ScopedId {
        public:
            explicit ScopedId(const char* text) {
                ImGui::PushID(text);
            }
            explicit ScopedId(int index) {
                ImGui::PushID(index);
            }
            ~ScopedId() {
                ImGui::PopID();
            }
            ScopedId(const ScopedId&) = delete;
            ScopedId& operator=(const ScopedId&) = delete;
        };

        PreparedKind KindOf(const PanelElement& element) {
            if (element.Is<PanelSection>()) {
                return PreparedKind::Section;
            }
            if (element.Is<PanelRow>()) {
                return PreparedKind::Row;
            }
            if (element.Is<PanelText>()) {
                return PreparedKind::Text;
            }
            if (element.Is<PanelField>()) {
                return PreparedKind::Field;
            }
            if (element.Is<PanelFields>()) {
                return PreparedKind::Fields;
            }
            if (element.Is<PanelInput>()) {
                return PreparedKind::Input;
            }
            if (element.Is<PanelButton>()) {
                return PreparedKind::Button;
            }
            if (element.Is<PanelTable>()) {
                return PreparedKind::Table;
            }
            if (element.Is<PanelSeparator>()) {
                return PreparedKind::Separator;
            }
            if (element.Is<PanelEmptyState>()) {
                return PreparedKind::EmptyState;
            }
            if (element.Is<PanelSlot>()) {
                return PreparedKind::Slot;
            }
            return PreparedKind::Unknown;
        }

    }  // namespace

    bool PanelInterpreter::CanDraw(const PanelElement& element) {
        const PreparedKind kind = KindOf(element);
        return kind != PreparedKind::Unknown;
    }

    PanelInterpreter::PanelInterpreter() : _single(_inspector, StringView{}) {}

    void PanelInterpreter::Prepare(IntrusivePtr<PanelAsset> asset) {
        _asset = std::move(asset);
        _elements.clear();
        if (_asset) {
            PrepareElements(_asset->elements, _elements);
        }
    }

    void PanelInterpreter::PrepareElements(const Vector<IntrusivePtr<PanelElement>>& elements, Vector<PreparedElement>& out) {
        out.reserve(elements.size());
        for (const IntrusivePtr<PanelElement>& element : elements) {
            if (!element) {
                continue;
            }
            PreparedElement& prepared = out.emplace_back();
            prepared.element = element.Get();
            prepared.kind = KindOf(*element);
            prepared.visible = PrepareBinding(element->visible, BindingForm::Condition);
            switch (prepared.kind) {
                case PreparedKind::Section:
                case PreparedKind::Row:
                    PrepareElements(element->elements, prepared.children);
                    break;
                case PreparedKind::Text:
                    prepared.bind = PrepareBinding(element->Cast<PanelText>()->bind, BindingForm::Field);
                    break;
                case PreparedKind::Field:
                    prepared.bind = PrepareBinding(element->Cast<PanelField>()->bind, BindingForm::Field);
                    break;
                case PreparedKind::Fields:
                    prepared.bind = PrepareBinding(element->Cast<PanelFields>()->bind, BindingForm::Object);
                    break;
                case PreparedKind::Input: {
                    const PanelInput& input = *element->Cast<PanelInput>();
                    prepared.bind = PrepareBinding(input.bind, BindingForm::Field);
                    prepared.glyph = GlyphOf(input.icon);
                    prepared.hint = HintOf(prepared.glyph, input.hint);
                    break;
                }
                case PreparedKind::Button: {
                    const PanelButton& button = *element->Cast<PanelButton>();
                    prepared.enableWhen = PrepareBinding(button.enableWhen, BindingForm::Condition);
                    prepared.glyph = GlyphOf(button.icon);
                    break;
                }
                case PreparedKind::Table: {
                    const PanelTable& table = *element->Cast<PanelTable>();
                    prepared.bind = PrepareBinding(table.source, BindingForm::Field);
                    prepared.columns.reserve(table.columns.size());
                    for (const PanelColumn& column : table.columns) {
                        const char* glyph = GlyphOf(column.icon);
                        prepared.columns.push_back(PreparedColumn{&column, glyph, HintOf(glyph, column.hint)});
                    }
                    break;
                }
                case PreparedKind::EmptyState:
                    prepared.glyph = GlyphOf(element->Cast<PanelEmptyState>()->icon);
                    break;
                default:
                    break;
            }
        }
    }

    void PanelInterpreter::Draw(const PanelDataSources& sources, PanelDrawResult& result, PanelSlots* slots) {
        result.Clear();
        if (!_asset) {
            return;
        }
        _sources = &sources;
        _result = &result;
        _slots = slots != nullptr ? slots : (PanelSlots::IsInstanceCreated() ? &PanelSlots::GetInstance() : nullptr);
        DrawElements(_elements);
        _sources = nullptr;
        _result = nullptr;
        _slots = nullptr;
    }

    void PanelInterpreter::DrawElements(const Vector<PreparedElement>& elements) {
        for (const PreparedElement& prepared : elements) {
            if (IsVisible(prepared)) {
                DrawElement(prepared);
            }
        }
    }

    bool PanelInterpreter::ReadCondition(const PreparedBinding& condition) {
        bool value = true;
        if (condition.state == PreparedBindingState::Ok && _reader.ReadBool(*_sources, condition.binding, value) != PanelReadStatus::Ok) {
            return true;
        }
        return value;
    }

    bool PanelInterpreter::IsVisible(const PreparedElement& prepared) {
        return ReadCondition(prepared.visible);
    }

    void PanelInterpreter::NoteClick(StringView command, StringView argument) {
        _result->clicked = true;
        _result->command.assign(command.data(), command.size());
        _result->argument.assign(argument.data(), argument.size());
    }

    void PanelInterpreter::DrawElement(const PreparedElement& prepared) {
        const PanelElement& element = *prepared.element;
        const ScopedId id(element.id.c_str());
        switch (prepared.kind) {
            case PreparedKind::Section: {
                const auto& section = *element.Cast<PanelSection>();
                const ImGuiTreeNodeFlags flags = section.defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
                const bool open = ImGui::CollapsingHeader(section.title.empty() ? "##section" : section.title.c_str(), flags);
                ItemTooltip(OrNull(element.tooltip));
                if (open) {
                    DrawElements(prepared.children);
                }
                break;
            }
            case PreparedKind::Row:
                DrawRow(prepared);
                break;
            case PreparedKind::Text:
                DrawTextLine(prepared);
                break;
            case PreparedKind::Field:
                DrawField(prepared, false);
                break;
            case PreparedKind::Fields:
                DrawField(prepared, true);
                break;
            case PreparedKind::Input:
                DrawInput(prepared);
                break;
            case PreparedKind::Button:
                DrawButton(prepared);
                break;
            case PreparedKind::Table:
                DrawTable(prepared);
                break;
            case PreparedKind::Separator: {
                const String& title = element.Cast<PanelSeparator>()->title;
                if (title.empty()) {
                    ImGui::Separator();
                } else {
                    ImGui::SeparatorText(title.c_str());
                }
                break;
            }
            case PreparedKind::EmptyState:
                EmptyState(prepared.glyph != nullptr ? prepared.glyph : "", element.Cast<PanelEmptyState>()->text.c_str());
                break;
            case PreparedKind::Slot:
                DrawSlot(prepared);
                break;
            default:
                // A plain `PanelElement` of an unknown type: shown, so a file that names one is seen
                DrawUnreadable();
                break;
        }
    }

    void PanelInterpreter::DrawRow(const PreparedElement& prepared) {
        bool first = true;
        for (const PreparedElement& child : prepared.children) {
            if (!IsVisible(child)) {
                continue;
            }
            if (!first) {
                ImGui::SameLine();
            }
            first = false;
            DrawElement(child);
        }
    }

    void PanelInterpreter::DrawTextLine(const PreparedElement& prepared) {
        const PanelText& text = *prepared.element->Cast<PanelText>();
        StringView shown = text.text;
        if (text.text.empty() && (prepared.bind.state != PreparedBindingState::Ok || _reader.ReadText(*_sources, prepared.bind.binding, _scratch, shown) != PanelReadStatus::Ok)) {
            shown = kUnreadable;
        }
        const ScopedTextStyle style(text.style);
        const auto draw = [&] {
            if (text.maxLines == 0) {
                DrawLines(text.wrap, shown);
                return;
            }
            // The region is as high as its lines in the font drawn in, so it is sized inside the mono scope
            const float height = static_cast<float>(text.maxLines) * ImGui::GetTextLineHeightWithSpacing();
            if (ImGui::BeginChild("##lines", ImVec2(0.0f, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
                DrawLines(text.wrap, shown);
            }
            ImGui::EndChild();
        };
        if (text.mono) {
            const ScopedMonoFont mono;
            draw();
        } else {
            draw();
        }
        ItemTooltip(OrNull(prepared.element->tooltip));
    }

    void PanelInterpreter::DrawLines(bool wrap, StringView shown) {
        if (wrap) {
            ImGui::PushTextWrapPos(0.0f);
        }
        TextOf(shown);
        if (wrap) {
            ImGui::PopTextWrapPos();
        }
    }

    void PanelInterpreter::DrawButton(const PreparedElement& prepared) {
        const PanelButton& button = *prepared.element->Cast<PanelButton>();
        const char* tooltip = OrNull(button.tooltip);
        ImGui::BeginDisabled(!ReadCondition(prepared.enableWhen));
        bool clicked = false;
        if (prepared.glyph != nullptr) {
            clicked = IconTextButton(prepared.glyph, button.text.c_str(), tooltip);
        } else {
            clicked = ImGui::Button(button.text.empty() ? "##button" : button.text.c_str());
            ItemTooltip(tooltip);
        }
        ImGui::EndDisabled();
        // A button of its own has no row to read an argument from
        if (clicked && !button.command.empty()) {
            NoteClick(button.command, StringView{});
        }
    }

    bool PanelInterpreter::DrawTextBox(const char* label, StringView current, const char* hint, float widthInEm) {
        const size_t size = FittingBytes(current, kInputBufferSize - 1);
        std::memcpy(_inputBuffer, current.data(), size);
        _inputBuffer[size] = '\0';
        ImGui::SetNextItemWidth(widthInEm > 0.0f ? widthInEm * ImGui::GetFontSize() : kFillWidth);
        if (hint != nullptr) {
            return ImGui::InputTextWithHint(label, hint, _inputBuffer, sizeof(_inputBuffer));
        }
        return ImGui::InputText(label, _inputBuffer, sizeof(_inputBuffer));
    }

    void PanelInterpreter::DrawInput(const PreparedElement& prepared) {
        const PanelInput& input = *prepared.element->Cast<PanelInput>();
        StringView shown;
        if (prepared.bind.state != PreparedBindingState::Ok || _reader.ReadText(*_sources, prepared.bind.binding, _scratch, shown) != PanelReadStatus::Ok) {
            DrawUnreadable();
            return;
        }
        _label.assign(kIdSuffix);
        _label.append(input.id);
        if (DrawTextBox(_label.c_str(), shown, OrNull(prepared.hint), input.width)) {
            // A field that is not a `String` reads fine but refuses the write: the text then comes back as it was next frame
            static_cast<void>(_writer.WriteText(*_sources, prepared.bind.binding, StringView(_inputBuffer)));
        }
        ItemTooltip(OrNull(input.tooltip));
    }

    void PanelInterpreter::DrawField(const PreparedElement& prepared, bool openCompound) {
        TypedRef object;
        if (prepared.bind.state == PreparedBindingState::Ok) {
            object = _sources->Find(prepared.bind.binding.source);
        }
        if (!object || object.type->acceptVisitor == nullptr) {
            DrawUnreadable();
            return;
        }
        // The visitor numbers its widgets from a counter, so it starts again for each element: the ids stay the same from frame to frame
        _inspector = ImGuiPropertyVisitor();
        if (prepared.bind.binding.path.empty()) {
            ImGui::BeginDisabled(object.isConst);
            object.type->acceptVisitor(object.Address(), _inspector);
            ImGui::EndDisabled();
            return;
        }
        _single.Reset();
        _single.SetFieldName(prepared.bind.binding.path);
        if (openCompound) {
            // Consumed by the next item: the tree node of the field when it is an object or a list, otherwise the field's own widget
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        }
        ImGui::BeginDisabled(object.isConst);
        object.type->acceptVisitor(object.Address(), _single);
        ImGui::EndDisabled();
        if (!_single.FoundField()) {
            DrawUnreadable();
        }
    }

    bool PanelInterpreter::ReadArgument(const PanelBinding& list, size_t row, StringView path, StringView& out) {
        out = StringView{};
        return path.empty() || _reader.ReadRowText(*_sources, list, row, path, _scratch, out) == PanelReadStatus::Ok;
    }

    bool PanelInterpreter::ReadRowCondition(const PanelBinding& list, size_t row, StringView path) {
        bool value = true;
        if (!path.empty() && _reader.ReadRowBool(*_sources, list, row, path, value) != PanelReadStatus::Ok) {
            return true;
        }
        return value;
    }

    void PanelInterpreter::DrawTable(const PreparedElement& prepared) {
        const PanelTable& table = *prepared.element->Cast<PanelTable>();
        size_t count = 0;
        if (prepared.bind.state != PreparedBindingState::Ok || prepared.columns.empty() || _reader.ListCount(*_sources, prepared.bind.binding, count) != PanelReadStatus::Ok) {
            DrawUnreadable();
            return;
        }
        const size_t rows = table.maxRows > 0 ? eastl::min(count, static_cast<size_t>(table.maxRows)) : count;
        ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
        if (table.borders) {
            flags |= ImGuiTableFlags_Borders;
        }
        // A list that fits in `scrollRows` is an ordinary table: no scrollbar, no room left empty
        const bool scrolls = table.scrollRows > 0 && rows > static_cast<size_t>(table.scrollRows);
        ImVec2 outerSize(0.0f, 0.0f);
        if (scrolls) {
            flags |= ImGuiTableFlags_ScrollY;
            // A row is as high as its tallest cell: a button or an input is a frame, a text a line
            bool hasFrame = false;
            for (const PreparedColumn& column : prepared.columns) {
                hasFrame = hasFrame || column.column->type == PanelColumnType::Button || column.column->type == PanelColumnType::Input;
            }
            const float rowHeight = (hasFrame ? ImGui::GetFrameHeight() : ImGui::GetTextLineHeight()) + ImGui::GetStyle().CellPadding.y * 2.0f;
            const float headerRows = table.header ? 1.0f : 0.0f;
            outerSize.y = (static_cast<float>(table.scrollRows) + headerRows) * rowHeight;
        }
        if (!ImGui::BeginTable("table", static_cast<int>(prepared.columns.size()), flags, outerSize)) {
            return;
        }
        if (table.header) {
            for (const PreparedColumn& column : prepared.columns) {
                ImGui::TableSetupColumn(column.column->title.c_str());
            }
            if (scrolls) {
                ImGui::TableSetupScrollFreeze(0, 1);
            }
            ImGui::TableHeadersRow();
        }
        DrawRows(prepared, rows, scrolls);
        ImGui::EndTable();
    }

    void PanelInterpreter::DrawRows(const PreparedElement& prepared, size_t rows, bool clipped) {
        const auto drawRow = [&](size_t row) {
            ImGui::TableNextRow();
            const ScopedId rowId(static_cast<int>(row));
            for (size_t column = 0; column < prepared.columns.size(); ++column) {
                ImGui::TableSetColumnIndex(static_cast<int>(column));
                const ScopedId columnId(prepared.columns[column].column->id.c_str());
                DrawCell(prepared.columns[column], prepared.bind.binding, row);
            }
        };
        if (!clipped) {
            for (size_t row = 0; row < rows; ++row) {
                drawRow(row);
            }
            return;
        }
        // A row outside the window is not submitted, so the control API does not see it either
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                drawRow(static_cast<size_t>(row));
            }
        }
    }

    void PanelInterpreter::DrawCell(const PreparedColumn& prepared, const PanelBinding& list, size_t row) {
        const PanelColumn& column = *prepared.column;
        if (column.type == PanelColumnType::Button) {
            DrawButtonCell(prepared, list, row);
            return;
        }
        if (column.type == PanelColumnType::Input) {
            DrawInputCell(prepared, list, row);
            return;
        }

        PanelTextStyle style = PanelTextStyle::Normal;
        if (!column.styleField.empty()) {
            StringView name;
            if (_reader.ReadRowText(*_sources, list, row, column.styleField, _styleScratch, name) == PanelReadStatus::Ok) {
                style = EnumUtils<PanelTextStyle>::Cast(name).value_or(PanelTextStyle::Normal);
            }
        }
        StringView text;
        if (_reader.ReadRowText(*_sources, list, row, column.field, _scratch, text) != PanelReadStatus::Ok) {
            DrawUnreadable();
            return;
        }
        const ScopedTextStyle scoped(style);
        if (column.type != PanelColumnType::Selectable) {
            TextOf(text);
            return;
        }
        // The label is the text of the cell and a suffix, so an empty cell is still an item
        _label.assign(text.data(), text.size());
        _label.append(kSelectableSuffix);
        if (ImGui::Selectable(_label.c_str()) && !column.command.empty()) {
            StringView argument;
            if (ReadArgument(list, row, column.argument, argument)) {
                NoteClick(column.command, argument);
            }
        }
    }

    void PanelInterpreter::DrawButtonCell(const PreparedColumn& prepared, const PanelBinding& list, size_t row) {
        const PanelColumn& column = *prepared.column;
        StringView argument;
        const bool readable = ReadArgument(list, row, column.argument, argument);
        // A button whose argument cannot be read would run its command on nothing
        ImGui::BeginDisabled(!readable || !ReadRowCondition(list, row, column.enableField));
        bool clicked = false;
        if (column.field.empty()) {
            const char* glyph = prepared.glyph != nullptr ? prepared.glyph : (column.title.empty() ? kUnreadable : column.title.c_str());
            clicked = IconButton(glyph, OrNull(column.tooltip), column.id.c_str());
        } else {
            // Its own scratch: `argument` may still view `_scratch`
            StringView text;
            if (_reader.ReadRowText(*_sources, list, row, column.field, _cellScratch, text) != PanelReadStatus::Ok) {
                text = kUnreadable;
            }
            // The id part of the label is the column's, so the row's own text may repeat or change without moving the id
            _label.clear();
            if (prepared.glyph != nullptr) {
                _label.append(prepared.glyph);
                _label.append(kGlyphGap);
            }
            _label.append(text.data(), text.size());
            _label.append(kIdSuffix);
            _label.append(column.id);
            clicked = ImGui::Button(_label.c_str());
            ItemTooltip(OrNull(column.tooltip));
        }
        ImGui::EndDisabled();
        if (clicked && readable && !column.command.empty()) {
            NoteClick(column.command, argument);
        }
    }

    void PanelInterpreter::DrawInputCell(const PreparedColumn& prepared, const PanelBinding& list, size_t row) {
        const PanelColumn& column = *prepared.column;
        StringView text;
        if (_reader.ReadRowText(*_sources, list, row, column.field, _scratch, text) != PanelReadStatus::Ok) {
            DrawUnreadable();
            return;
        }
        _label.assign(kIdSuffix);
        _label.append(column.id);
        ImGui::BeginDisabled(!ReadRowCondition(list, row, column.enableField));
        if (DrawTextBox(_label.c_str(), text, OrNull(prepared.hint), 0.0f)) {
            static_cast<void>(_writer.WriteRowText(*_sources, list, row, column.field, StringView(_inputBuffer)));
        }
        ImGui::EndDisabled();
        ItemTooltip(OrNull(column.tooltip));
    }

    void PanelInterpreter::DrawSlot(const PreparedElement& prepared) {
        PanelSlotDraw* draw = _slots != nullptr ? _slots->Find(prepared.element->Cast<PanelSlot>()->name) : nullptr;
        if (draw != nullptr) {
            (*draw)();
        } else {
            DrawUnreadable();
        }
    }

}  // namespace BECore
