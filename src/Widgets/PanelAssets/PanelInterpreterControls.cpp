#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelDrawSupport.h>
#include <Widgets/PanelAssets/PanelInterpreter.h>
#include <Widgets/SceneEditor/FieldPresentation.h>
#include <cmath>
#include <imgui.h>

namespace BECore {

    using namespace PanelDraw;

    namespace {

        /// A toggle is a button: the accent fill while on, the label in the style's colour while on and disabled otherwise
        bool ToggleButton(const char* label, bool on, PanelTextStyle style) {
            const ImGuiPalette& palette = GetImGuiPalette();
            const ImVec4* onColor = ColorOf(style);
            const bool coloured = !on || onColor != nullptr;
            if (coloured) {
                ImGui::PushStyleColor(ImGuiCol_Text, on ? *onColor : palette.textDisabled);
            }
            if (on) {
                ImGui::PushStyleColor(ImGuiCol_Button, palette.accentMuted);
            }
            const bool clicked = ImGui::Button(label);
            ImGui::PopStyleColor((coloured ? 1 : 0) + (on ? 1 : 0));
            return clicked;
        }

        void SetWidth(float widthInEm) {
            if (widthInEm > 0.0f) {
                ImGui::SetNextItemWidth(widthInEm * ImGui::GetFontSize());
            }
        }

        /// The click step of a stepper: `step`, or 1 when it is 0 (as for a drag, 0 picks one)
        double StepOf(const PanelNumber& number) {
            return number.step > 0.0f ? static_cast<double>(number.step) : 1.0;
        }

        /// An index of a combo held in a number field; none when it is not a whole non-negative number
        bool AsIndex(double value, size_t& out) {
            if (!(value >= 0.0) || value > static_cast<double>(UINT32_MAX)) {
                return false;
            }
            out = static_cast<size_t>(std::llround(value));
            return true;
        }

    }  // namespace

    void PanelInterpreter::DrawCheckbox(const PreparedElement& prepared) {
        const PanelCheckbox& checkbox = *prepared.element->Cast<PanelCheckbox>();
        bool value = false;
        if (prepared.bind.state != PreparedBindingState::Ok || _reader.ReadBool(*_sources, prepared.bind.binding, value) != PanelReadStatus::Ok) {
            DrawUnreadable();
            return;
        }
        StringView text = checkbox.text;
        if (prepared.label.state != PreparedBindingState::Empty) {
            StringView bound;
            const bool read = prepared.label.state == PreparedBindingState::Ok && _reader.ReadText(*_sources, prepared.label.binding, _scratch, bound) == PanelReadStatus::Ok;
            text = read ? bound : (checkbox.text.empty() ? StringView(kUnreadable) : StringView(checkbox.text));
        }
        // The visible text may change from frame to frame, the id after `###` does not
        _label.clear();
        if (prepared.glyph != nullptr) {
            _label.append(prepared.glyph);
            if (!text.empty()) {
                _label.append(kGlyphGap);
            }
        }
        _label.append(text.data(), text.size());
        _label.append(kStableIdSuffix);
        _label.append(checkbox.id);

        ImGui::BeginDisabled(!ReadCondition(prepared.enableWhen));
        bool changed = false;
        bool next = !value;
        if (checkbox.look == PanelCheckLook::Toggle) {
            changed = ToggleButton(_label.c_str(), value, checkbox.style);
        } else {
            const ScopedTextStyle style(checkbox.style);
            next = value;
            changed = ImGui::Checkbox(_label.c_str(), &next);
        }
        ElementTooltip(prepared);
        ImGui::EndDisabled();
        if (changed) {
            static_cast<void>(_writer.WriteBool(*_sources, prepared.bind.binding, next));
        }
    }

    void PanelInterpreter::DrawRadio(const PreparedElement& prepared) {
        const PanelRadio& radio = *prepared.element->Cast<PanelRadio>();
        if (prepared.bind.state != PreparedBindingState::Ok) {
            DrawUnreadable();
            return;
        }
        // A value that cannot be read, or that the enum has no name for, selects no radio of the group
        const bool selected = prepared.match.state == PreparedBindingState::Ok && ReadCondition(prepared.match, false);
        _label.clear();
        if (prepared.glyph != nullptr) {
            _label.append(prepared.glyph);
            if (!radio.text.empty()) {
                _label.append(kGlyphGap);
            }
        }
        _label.append(radio.text);
        _label.append(kStableIdSuffix);
        _label.append(radio.id);

        ImGui::BeginDisabled(!ReadCondition(prepared.enableWhen));
        const bool clicked = ImGui::RadioButton(_label.c_str(), selected);
        ElementTooltip(prepared);
        ImGui::EndDisabled();
        if (clicked && !selected) {
            WriteRadioValue(prepared);
        }
    }

    void PanelInterpreter::WriteRadioValue(const PreparedElement& prepared) {
        if (prepared.match.state != PreparedBindingState::Ok) {
            return;
        }
        const PanelConditionTerm& term = prepared.match.condition.alternatives.front().front();
        if (term.numeric) {
            static_cast<void>(_writer.WriteNumber(*_sources, prepared.bind.binding, term.number));
            return;
        }
        if (!prepared.radioIndexKnown) {
            Span<const StringView> names;
            if (_reader.EnumNames(*_sources, prepared.bind.binding, names) != PanelReadStatus::Ok) {
                return;
            }
            const auto found = eastl::find(names.begin(), names.end(), StringView(term.word));
            prepared.radioIndex = found != names.end() ? Optional<size_t>(static_cast<size_t>(found - names.begin())) : std::nullopt;
            prepared.radioIndexKnown = true;
        }
        if (prepared.radioIndex.has_value()) {
            static_cast<void>(_writer.WriteEnumIndex(*_sources, prepared.bind.binding, *prepared.radioIndex));
        }
    }

    void PanelInterpreter::DrawCombo(const PreparedElement& prepared) {
        const PanelCombo& combo = *prepared.element->Cast<PanelCombo>();
        if (prepared.bind.state != PreparedBindingState::Ok) {
            DrawUnreadable();
            return;
        }
        _label.assign(kIdSuffix);
        _label.append(combo.id);
        SetWidth(combo.width);
        ImGui::BeginDisabled(!ReadCondition(prepared.enableWhen));
        if (prepared.items.state == PreparedBindingState::Empty) {
            DrawEnumCombo(combo, prepared);
        } else {
            DrawListCombo(combo, prepared);
        }
        ElementTooltip(prepared);
        DrawTrailingText(combo.text);
        ImGui::EndDisabled();
    }

    const char* PanelInterpreter::ComboPreview(const PreparedElement& prepared, const String& chosen) {
        StringView bound;
        if (prepared.label.state != PreparedBindingState::Ok || _reader.ReadText(*_sources, prepared.label.binding, _scratch, bound) != PanelReadStatus::Ok) {
            return chosen.c_str();
        }
        _boundPreview.assign(bound.data(), bound.size());
        return _boundPreview.c_str();
    }

    void PanelInterpreter::DrawEnumCombo(const PanelCombo& /*combo*/, const PreparedElement& prepared) {
        Span<const StringView> names;
        if (_reader.EnumNames(*_sources, prepared.bind.binding, names) != PanelReadStatus::Ok) {
            DrawUnreadable();
            return;
        }
        size_t current = 0;
        const bool known = _reader.ReadEnumIndex(*_sources, prepared.bind.binding, current) == PanelReadStatus::Ok && current < names.size();
        _preview.assign(known ? names[current].data() : kUnreadable, known ? names[current].size() : std::strlen(kUnreadable));
        if (!ImGui::BeginCombo(_label.c_str(), ComboPreview(prepared, _preview))) {
            return;
        }
        for (size_t index = 0; index < names.size(); ++index) {
            // A name is static type data, not promised to end in a terminator
            _preview.assign(names[index].data(), names[index].size());
            ImGui::PushID(static_cast<int>(index));
            const bool isSelected = known && index == current;
            if (ImGui::Selectable(_preview.c_str(), isSelected)) {
                static_cast<void>(_writer.WriteEnumIndex(*_sources, prepared.bind.binding, index));
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    void PanelInterpreter::DrawListCombo(const PanelCombo& combo, const PreparedElement& prepared) {
        size_t count = 0;
        if (prepared.items.state != PreparedBindingState::Ok || _reader.ListCount(*_sources, prepared.items.binding, count) != PanelReadStatus::Ok) {
            DrawUnreadable();
            return;
        }
        // The value is an index into the list when it is a number, otherwise the text of a row
        double number = 0.0;
        const bool byIndex = _reader.ReadNumber(*_sources, prepared.bind.binding, number) == PanelReadStatus::Ok;
        size_t selected = count;
        StringView item;
        if (byIndex) {
            size_t index = 0;
            // A separator row is never a choice, so an index that names one shows as a value that cannot be read
            if (AsIndex(number, index) && index < count && !ReadRowFlag(prepared.items.binding, index, combo.separatorField) &&
                _reader.ReadRowText(*_sources, prepared.items.binding, index, combo.itemField, _scratch, item) == PanelReadStatus::Ok) {
                selected = index;
                _preview.assign(item.data(), item.size());
            } else {
                _preview.assign(kUnreadable);
            }
        } else if (_reader.ReadText(*_sources, prepared.bind.binding, _scratch, item) == PanelReadStatus::Ok) {
            _preview.assign(item.data(), item.size());
        } else {
            DrawUnreadable();
            return;
        }
        if (!ImGui::BeginCombo(_label.c_str(), ComboPreview(prepared, _preview))) {
            return;
        }
        for (size_t row = 0; row < count; ++row) {
            if (ReadRowFlag(prepared.items.binding, row, combo.separatorField)) {
                ImGui::Separator();
                continue;
            }
            if (_reader.ReadRowText(*_sources, prepared.items.binding, row, combo.itemField, _scratch, item) != PanelReadStatus::Ok) {
                item = kUnreadable;
            }
            const bool isSelected = byIndex ? row == selected : item == StringView(_preview);
            // The label is a copy: a row's text is no C string, and `_scratch` is read again for the next row
            _cellScratch.assign(item.data(), item.size());
            ImGui::PushID(static_cast<int>(row));
            // A disabled row is shown but never picked; one whose flag cannot be read stays a choice, so a broken binding is seen
            ImGui::BeginDisabled(!ReadRowCondition(prepared.items.binding, row, combo.enableField));
            const bool picked = ImGui::Selectable(_cellScratch.c_str(), isSelected);
            RowTooltip(StringView{}, prepared.items.binding, row, combo.tooltipField);
            ImGui::EndDisabled();
            if (picked) {
                static_cast<void>(byIndex ? _writer.WriteNumber(*_sources, prepared.bind.binding, static_cast<double>(row)) : _writer.WriteText(*_sources, prepared.bind.binding, item));
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    void PanelInterpreter::DrawNumber(const PreparedElement& prepared) {
        const PanelNumber& number = *prepared.element->Cast<PanelNumber>();
        double value = 0.0;
        bool integer = false;
        if (prepared.bind.state != PreparedBindingState::Ok || _reader.ReadNumber(*_sources, prepared.bind.binding, value, &integer) != PanelReadStatus::Ok) {
            DrawUnreadable();
            return;
        }
        // The label is after the control, in the interface font, as the inspector draws a number
        _label.assign(kIdSuffix);
        _label.append(number.id);
        SetWidth(number.width);
        ImGui::BeginDisabled(!ReadCondition(prepared.enableWhen));
        if (number.stepper) {
            DrawStepperNumber(prepared, value, integer);
        } else if (number.input) {
            DrawTypedNumber(prepared, value, integer);
        } else {
            DrawDraggedNumber(prepared, value, integer);
        }
        ElementTooltip(prepared);
        DrawTrailingText(number.text);
        ImGui::EndDisabled();
    }

    void PanelInterpreter::DrawDraggedNumber(const PreparedElement& prepared, double value, bool integer) {
        const PanelNumber& number = *prepared.element->Cast<PanelNumber>();
        const bool bounded = number.min < number.max;
        const bool slider = number.slider && bounded;
        const ImGuiSliderFlags flags = bounded ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
        const float speed = number.step > 0.0f ? number.step : static_cast<float>(integer ? FieldPresentation::kIntegerDragSpeed : FieldPresentation::kFloatDragSpeed);
        bool changed = false;
        double written = value;
        {
            const ScopedMonoFont mono;
            if (integer) {
                int64_t held = FieldPresentation::FromDouble<int64_t>(value);
                const int64_t low = FieldPresentation::FromDouble<int64_t>(number.min);
                const int64_t high = FieldPresentation::FromDouble<int64_t>(number.max);
                const char* format = OrNull(prepared.integerFormat);
                changed = slider ? ImGui::SliderScalar(_label.c_str(), ImGuiDataType_S64, &held, &low, &high, format, flags)
                                 : ImGui::DragScalar(_label.c_str(), ImGuiDataType_S64, &held, speed, bounded ? &low : nullptr, bounded ? &high : nullptr, format, flags);
                written = static_cast<double>(held);
            } else {
                float held = static_cast<float>(value);
                const char* format = OrNull(prepared.format);
                changed = slider ? ImGui::SliderScalar(_label.c_str(), ImGuiDataType_Float, &held, &number.min, &number.max, format, flags)
                                 : ImGui::DragScalar(_label.c_str(), ImGuiDataType_Float, &held, speed, bounded ? &number.min : nullptr, bounded ? &number.max : nullptr, format, flags);
                written = static_cast<double>(held);
            }
        }
        if (changed) {
            static_cast<void>(_writer.WriteNumber(*_sources, prepared.bind.binding, written));
        }
        if (!number.commitCommand.empty() && ImGui::IsItemDeactivatedAfterEdit()) {
            NoteClick(number.commitCommand, StringView{});
        }
    }

    void PanelInterpreter::DrawTypedNumber(const PreparedElement& prepared, double value, bool integer) {
        const PanelNumber& number = *prepared.element->Cast<PanelNumber>();
        const ImGuiID id = ImGui::GetID(_label.c_str());
        // While the user types, the field shows its own text; what it last parsed is the value to write when it is left
        const double shown = _typedId == id ? _typedValue : value;
        bool changed = false;
        double typed = shown;
        {
            const ScopedMonoFont mono;
            if (integer) {
                int64_t held = FieldPresentation::FromDouble<int64_t>(shown);
                changed = ImGui::InputScalar(_label.c_str(), ImGuiDataType_S64, &held, nullptr, nullptr, OrNull(prepared.integerFormat));
                typed = static_cast<double>(held);
            } else {
                float held = static_cast<float>(shown);
                changed = ImGui::InputScalar(_label.c_str(), ImGuiDataType_Float, &held, nullptr, nullptr, OrNull(prepared.format));
                typed = static_cast<double>(held);
            }
        }
        if (changed) {
            _typedId = id;
            _typedValue = typed;
        }
        // A field that `enableWhen` turned off while it was being typed in is deactivated by ImGui, and what it held is dropped
        const bool enabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) == 0;
        if (enabled && ImGui::IsItemDeactivatedAfterEdit()) {
            if (number.min < number.max) {
                typed = eastl::clamp(typed, static_cast<double>(number.min), static_cast<double>(number.max));
            }
            static_cast<void>(_writer.WriteNumber(*_sources, prepared.bind.binding, typed));
            if (!number.commitCommand.empty()) {
                NoteClick(number.commitCommand, StringView{});
            }
        }
        if (_typedId == id && !ImGui::IsItemActive()) {
            _typedId = 0;
        }
    }

    void PanelInterpreter::DrawStepperNumber(const PreparedElement& prepared, double value, bool integer) {
        const PanelNumber& number = *prepared.element->Cast<PanelNumber>();
        // A Ctrl-click with no `stepFast` steps as a plain click does
        const bool hasFastStep = number.stepFast > 0.0f;
        bool changed = false;
        double written = value;
        {
            const ScopedMonoFont mono;
            if (integer) {
                int64_t held = FieldPresentation::FromDouble<int64_t>(value);
                // A fraction of a step would round to a button that does nothing, so an integer steps by at least one
                const int64_t step = eastl::max<int64_t>(1, FieldPresentation::FromDouble<int64_t>(StepOf(number)));
                const int64_t stepFast = eastl::max<int64_t>(1, FieldPresentation::FromDouble<int64_t>(number.stepFast));
                changed = ImGui::InputScalar(_label.c_str(), ImGuiDataType_S64, &held, &step, hasFastStep ? &stepFast : nullptr, OrNull(prepared.integerFormat));
                written = static_cast<double>(held);
            } else {
                float held = static_cast<float>(value);
                const float step = static_cast<float>(StepOf(number));
                changed = ImGui::InputScalar(_label.c_str(), ImGuiDataType_Float, &held, &step, hasFastStep ? &number.stepFast : nullptr, OrNull(prepared.format));
                written = static_cast<double>(held);
            }
        }
        if (changed) {
            if (number.min < number.max) {
                written = eastl::clamp(written, static_cast<double>(number.min), static_cast<double>(number.max));
            }
            static_cast<void>(_writer.WriteNumber(*_sources, prepared.bind.binding, written));
        }
        if (!number.commitCommand.empty() && ImGui::IsItemDeactivatedAfterEdit()) {
            NoteClick(number.commitCommand, StringView{});
        }
    }

}  // namespace BECore
