#include "PanelInterpreter.h"

#include <BECore/Reflection/TypedRef.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/EditorLayout/IconTable.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr const char* kUnreadable = "?";
        constexpr const char* kSelectableSuffix = "##select";

        PreparedBinding PrepareBinding(StringView text, bool isCondition) {
            PreparedBinding prepared;
            if (text.empty()) {
                return prepared;
            }
            auto parsed = isCondition ? ParseCondition(text) : ParseBinding(text);
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
            prepared.visible = PrepareBinding(element->visible, true);
            switch (prepared.kind) {
                case PreparedKind::Section:
                case PreparedKind::Row:
                    PrepareElements(element->elements, prepared.children);
                    break;
                case PreparedKind::Text:
                    prepared.bind = PrepareBinding(element->Cast<PanelText>()->bind, false);
                    break;
                case PreparedKind::Field:
                    prepared.bind = PrepareBinding(element->Cast<PanelField>()->bind, false);
                    break;
                case PreparedKind::Fields:
                    prepared.bind = PrepareBinding(element->Cast<PanelFields>()->bind, false);
                    break;
                case PreparedKind::Button: {
                    const PanelButton& button = *element->Cast<PanelButton>();
                    prepared.enableWhen = PrepareBinding(button.enableWhen, true);
                    prepared.glyph = GlyphOf(button.icon);
                    break;
                }
                case PreparedKind::Table: {
                    const PanelTable& table = *element->Cast<PanelTable>();
                    prepared.bind = PrepareBinding(table.source, false);
                    prepared.columns.reserve(table.columns.size());
                    for (const PanelColumn& column : table.columns) {
                        prepared.columns.push_back(PreparedColumn{&column, GlyphOf(column.icon)});
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
                // An input (stage 3) and a plain `PanelElement` of an unknown type: shown, so a file that names one is seen
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
            if (text.wrap) {
                ImGui::PushTextWrapPos(0.0f);
            }
            TextOf(shown);
            if (text.wrap) {
                ImGui::PopTextWrapPos();
            }
        };
        if (text.mono) {
            const ScopedMonoFont mono;
            draw();
        } else {
            draw();
        }
        ItemTooltip(OrNull(prepared.element->tooltip));
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

    void PanelInterpreter::DrawTable(const PreparedElement& prepared) {
        const PanelTable& table = *prepared.element->Cast<PanelTable>();
        size_t count = 0;
        if (prepared.bind.state != PreparedBindingState::Ok || prepared.columns.empty() || _reader.ListCount(*_sources, prepared.bind.binding, count) != PanelReadStatus::Ok) {
            DrawUnreadable();
            return;
        }
        ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
        if (table.borders) {
            flags |= ImGuiTableFlags_Borders;
        }
        if (!ImGui::BeginTable("table", static_cast<int>(prepared.columns.size()), flags)) {
            return;
        }
        if (table.header) {
            for (const PreparedColumn& column : prepared.columns) {
                ImGui::TableSetupColumn(column.column->title.c_str());
            }
            ImGui::TableHeadersRow();
        }
        const size_t rows = table.maxRows > 0 ? eastl::min(count, static_cast<size_t>(table.maxRows)) : count;
        for (size_t row = 0; row < rows; ++row) {
            ImGui::TableNextRow();
            const ScopedId rowId(static_cast<int>(row));
            for (size_t column = 0; column < prepared.columns.size(); ++column) {
                ImGui::TableSetColumnIndex(static_cast<int>(column));
                const ScopedId columnId(prepared.columns[column].column->id.c_str());
                DrawCell(prepared.columns[column], prepared.bind.binding, row);
            }
        }
        ImGui::EndTable();
    }

    void PanelInterpreter::DrawCell(const PreparedColumn& prepared, const PanelBinding& list, size_t row) {
        const PanelColumn& column = *prepared.column;
        if (column.type == PanelColumnType::Button) {
            StringView argument;
            const bool readable = ReadArgument(list, row, column.argument, argument);
            // A button whose argument cannot be read would run its command on nothing
            ImGui::BeginDisabled(!readable);
            const char* glyph = prepared.glyph != nullptr ? prepared.glyph : (column.title.empty() ? kUnreadable : column.title.c_str());
            const bool clicked = IconButton(glyph, OrNull(column.tooltip), column.id.c_str());
            ImGui::EndDisabled();
            if (clicked && readable && !column.command.empty()) {
                NoteClick(column.command, argument);
            }
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

    void PanelInterpreter::DrawSlot(const PreparedElement& prepared) {
        PanelSlotDraw* draw = _slots != nullptr ? _slots->Find(prepared.element->Cast<PanelSlot>()->name) : nullptr;
        if (draw != nullptr) {
            (*draw)();
        } else {
            DrawUnreadable();
        }
    }

}  // namespace BECore
