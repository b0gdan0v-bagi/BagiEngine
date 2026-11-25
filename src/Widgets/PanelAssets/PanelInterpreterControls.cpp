#include "PanelInterpreter.h"

#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelDrawSupport.h>
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
        _label.assign(text.data(), text.size());
        _label.append(kStableIdSuffix);
        _label.append(checkbox.id);

        bool changed = false;
        bool next = !value;
        if (checkbox.look == PanelCheckLook::Toggle) {
            changed = ToggleButton(_label.c_str(), value, checkbox.style);
        } else {
            const ScopedTextStyle style(checkbox.style);
            next = value;
            changed = ImGui::Checkbox(_label.c_str(), &next);
        }
        ItemTooltip(OrNull(checkbox.tooltip));
        if (changed) {
            static_cast<void>(_writer.WriteBool(*_sources, prepared.bind.binding, next));
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
        if (prepared.items.state == PreparedBindingState::Empty) {
            DrawEnumCombo(combo, prepared);
        } else {
            DrawListCombo(combo, prepared);
        }
        ItemTooltip(OrNull(combo.tooltip));
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
        if (!ImGui::BeginCombo(_label.c_str(), _preview.c_str())) {
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
            if (AsIndex(number, index) && index < count && _reader.ReadRowText(*_sources, prepared.items.binding, index, combo.itemField, _scratch, item) == PanelReadStatus::Ok) {
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
        if (!ImGui::BeginCombo(_label.c_str(), _preview.c_str())) {
            return;
        }
        for (size_t row = 0; row < count; ++row) {
            if (_reader.ReadRowText(*_sources, prepared.items.binding, row, combo.itemField, _scratch, item) != PanelReadStatus::Ok) {
                item = kUnreadable;
            }
            const bool isSelected = byIndex ? row == selected : item == StringView(_preview);
            // The label is a copy: a row's text is no C string, and `_scratch` is read again for the next row
            _cellScratch.assign(item.data(), item.size());
            ImGui::PushID(static_cast<int>(row));
            if (ImGui::Selectable(_cellScratch.c_str(), isSelected)) {
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
                changed = slider ? ImGui::SliderScalar(_label.c_str(), ImGuiDataType_S64, &held, &low, &high, nullptr, flags)
                                 : ImGui::DragScalar(_label.c_str(), ImGuiDataType_S64, &held, speed, bounded ? &low : nullptr, bounded ? &high : nullptr, nullptr, flags);
                written = static_cast<double>(held);
            } else {
                float held = static_cast<float>(value);
                changed = slider ? ImGui::SliderScalar(_label.c_str(), ImGuiDataType_Float, &held, &number.min, &number.max, nullptr, flags)
                                 : ImGui::DragScalar(_label.c_str(), ImGuiDataType_Float, &held, speed, bounded ? &number.min : nullptr, bounded ? &number.max : nullptr, nullptr, flags);
                written = static_cast<double>(held);
            }
        }
        ItemTooltip(OrNull(number.tooltip));
        if (!number.text.empty()) {
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextUnformatted(number.text.c_str(), number.text.c_str() + number.text.size());
        }
        if (changed) {
            static_cast<void>(_writer.WriteNumber(*_sources, prepared.bind.binding, written));
        }
    }

}  // namespace BECore
