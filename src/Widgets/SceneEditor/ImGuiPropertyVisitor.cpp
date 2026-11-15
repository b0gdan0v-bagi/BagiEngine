#include "ImGuiPropertyVisitor.h"

#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/SceneEditor/FieldPresentation.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr size_t kHiddenLabelSize = 24;
        /// Room for a list's label, `name (size)###id`
        constexpr size_t kListLabelSize = 160;
        /// Room for a button id, a hidden label or an alternative's index
        constexpr size_t kIdSize = 32;
        /// The longest map key the key field takes
        constexpr size_t kNewKeySize = 128;

        /**
         * @brief The text typed into the key field of a map, kept between frames (the visitor is made again every frame)
         *
         * One draft for all maps: it belongs to the key field typed in last (`owner`, the field's ImGui id); every other
         * map's field shows empty until typed in, which takes the draft over.
         */
        struct NewKeyDraft {
            ImGuiID owner = 0;
            char text[kNewKeySize] = {};
        };

        NewKeyDraft& GetNewKeyDraft() {
            static NewKeyDraft draft;
            return draft;
        }

        void MakeLabel(char* buf, size_t bufSize, StringView name, int id) {
            snprintf(buf, bufSize, "%.*s##%d", static_cast<int>(name.size()), name.data(), id);
        }

        /// Draws the widgets of one field disabled when the field is `readonly`
        class DisabledScope {
        public:
            explicit DisabledScope(const FieldAttributes& attributes) : _disabled(attributes.readOnly) {
                if (_disabled) {
                    ImGui::BeginDisabled();
                }
            }
            ~DisabledScope() {
                if (_disabled) {
                    ImGui::EndDisabled();
                }
            }
            DisabledScope(const DisabledScope&) = delete;
            DisabledScope& operator=(const DisabledScope&) = delete;

        private:
            bool _disabled;
        };

        /// The tooltip of the field for the last item (the text is a string literal of the generated code)
        void FieldTooltip(const FieldAttributes& attributes) {
            if (!attributes.tooltip.empty()) {
                ItemTooltip(attributes.tooltip.data());
            }
        }

        /// A number field in the mono font, its name after it in the UI font (ImGui draws the label of a drag field with the
        /// font of the field, so the label is hidden and drawn here). The item width is the same as with the label inside.
        /// The tooltip shows over the field and over its name.
        template <typename Draw>
        bool DrawNumber(StringView name, int id, const FieldAttributes& attributes, Draw&& draw) {
            const DisabledScope disabled(attributes);
            char hiddenLabel[kHiddenLabelSize];
            snprintf(hiddenLabel, sizeof(hiddenLabel), "##%d", id);
            bool changed = false;
            {
                const ScopedMonoFont mono;
                changed = draw(hiddenLabel);
            }
            FieldTooltip(attributes);
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextUnformatted(name.data(), name.data() + name.size());
            FieldTooltip(attributes);
            return changed;
        }

        /// A number of type @p T: a slider with a `range`, a drag otherwise; an edit is clamped and snapped (`FieldPresentation::Constrain`)
        template <typename T>
        bool DrawTyped(StringView name, int id, const FieldAttributes& attributes, T& value, ImGuiDataType dataType) {
            constexpr FieldPresentation::NumberKind kind = std::is_floating_point_v<T> ? FieldPresentation::NumberKind::Float : FieldPresentation::NumberKind::Integer;
            const FieldPresentation::Presentation presentation = FieldPresentation::Describe(attributes, kind);
            const bool changed = DrawNumber(name, id, attributes, [&](const char* label) {
                if (presentation.widget == FieldPresentation::NumberWidget::Slider) {
                    const T low = FieldPresentation::FromDouble<T>(presentation.rangeMin);
                    const T high = FieldPresentation::FromDouble<T>(presentation.rangeMax);
                    return ImGui::SliderScalar(label, dataType, &value, &low, &high);
                }
                return ImGui::DragScalar(label, dataType, &value, static_cast<float>(presentation.speed));
            });
            if (changed) {
                value = FieldPresentation::FromDouble<T>(FieldPresentation::Constrain(presentation, static_cast<double>(value)));
            }
            return changed;
        }

    }  // namespace

    const FieldAttributes& ImGuiPropertyVisitor::CurrentField() const {
        static constexpr FieldAttributes kNone{};
        return _fields.empty() ? kNone : _fields.back();
    }

    void ImGuiPropertyVisitor::BeginField(const FieldAttributes& attributes) {
        _fields.push_back(attributes);
    }

    void ImGuiPropertyVisitor::EndField() {
        if (!_fields.empty()) {
            _fields.pop_back();
        }
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, bool& value) {
        const FieldAttributes& attributes = CurrentField();
        const DisabledScope disabled(attributes);
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        const bool changed = ImGui::Checkbox(label, &value);
        FieldTooltip(attributes);
        return changed;
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, int8_t& value) {
        return DrawTyped(name, _idCounter++, CurrentField(), value, ImGuiDataType_S8);
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, uint8_t& value) {
        return DrawTyped(name, _idCounter++, CurrentField(), value, ImGuiDataType_U8);
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, int16_t& value) {
        return DrawTyped(name, _idCounter++, CurrentField(), value, ImGuiDataType_S16);
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, uint16_t& value) {
        return DrawTyped(name, _idCounter++, CurrentField(), value, ImGuiDataType_U16);
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, int32_t& value) {
        return DrawTyped(name, _idCounter++, CurrentField(), value, ImGuiDataType_S32);
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, uint32_t& value) {
        return DrawTyped(name, _idCounter++, CurrentField(), value, ImGuiDataType_U32);
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, int64_t& value) {
        return DrawTyped(name, _idCounter++, CurrentField(), value, ImGuiDataType_S64);
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, uint64_t& value) {
        return DrawTyped(name, _idCounter++, CurrentField(), value, ImGuiDataType_U64);
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, float& value) {
        return DrawTyped(name, _idCounter++, CurrentField(), value, ImGuiDataType_Float);
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, double& value) {
        return DrawTyped(name, _idCounter++, CurrentField(), value, ImGuiDataType_Double);
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, PoolString& value) {
        const FieldAttributes& attributes = CurrentField();
        const DisabledScope disabled(attributes);
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);

        char buf[256] = {};
        const StringView sv = value.ToStringView();
        size_t copyLen = sv.size() < sizeof(buf) - 1 ? sv.size() : sizeof(buf) - 1;
        memcpy(buf, sv.data(), copyLen);
        buf[copyLen] = '\0';

        const bool changed = ImGui::InputText(label, buf, sizeof(buf));
        FieldTooltip(attributes);
        if (changed) {
            value = PoolString::Intern(StringView(buf));
        }
        return changed;
    }

    bool ImGuiPropertyVisitor::Visit(StringView name, String& value) {
        const FieldAttributes& attributes = CurrentField();
        const DisabledScope disabled(attributes);
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);

        char buf[256] = {};
        size_t copyLen = value.size() < sizeof(buf) - 1 ? value.size() : sizeof(buf) - 1;
        memcpy(buf, value.data(), copyLen);
        buf[copyLen] = '\0';

        const bool changed = ImGui::InputText(label, buf, sizeof(buf));
        FieldTooltip(attributes);
        if (changed) {
            value.assign(buf);
        }
        return changed;
    }

    bool ImGuiPropertyVisitor::BeginCompound(StringView name) {
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        const FieldAttributes& attributes = CurrentField();
        const bool open = ImGui::TreeNode(label);
        FieldTooltip(attributes);
        // The node itself stays clickable, so a read-only compound can be opened: its children are the ones disabled
        if (open && attributes.readOnly) {
            ImGui::BeginDisabled();
        }
        return open;
    }

    bool ImGuiPropertyVisitor::VisitEnum(StringView name, const StringView* enumNames, size_t enumCount, size_t& selectedIndex) {
        const FieldAttributes& attributes = CurrentField();
        const DisabledScope disabled(attributes);
        char label[128];
        MakeLabel(label, sizeof(label), name, _idCounter++);
        const char* preview = selectedIndex < enumCount ? enumNames[selectedIndex].data() : "";
        bool changed = false;
        if (ImGui::BeginCombo(label, preview)) {
            for (size_t i = 0; i < enumCount; ++i) {
                bool isSelected = (i == selectedIndex);
                if (ImGui::Selectable(enumNames[i].data(), isSelected)) {
                    selectedIndex = i;
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        FieldTooltip(attributes);
        return changed;
    }

    void ImGuiPropertyVisitor::EndCompound() {
        if (CurrentField().readOnly) {
            ImGui::EndDisabled();
        }
        ImGui::TreePop();
    }

    bool ImGuiPropertyVisitor::BeginList(StringView name, const ListInfo& info) {
        // `###id`: the id leaves out the size, so the node stays open when an element is added or removed
        char label[kListLabelSize];
        snprintf(label, sizeof(label), "%.*s (%zu)###%d", static_cast<int>(name.size()), name.data(), info.size, _idCounter++);
        const FieldAttributes& attributes = CurrentField();
        const bool open = ImGui::TreeNode(label);
        FieldTooltip(attributes);
        if (!open) {
            return false;
        }
        if (attributes.readOnly) {
            ImGui::BeginDisabled();
        }
        _lists.push_back(OpenList{info.canAdd, info.canRemove, false, false});
        return true;
    }

    void ImGuiPropertyVisitor::EndListElement(size_t index, ListRequest& request) {
        if (_lists.empty()) {
            return;
        }
        // The ids of the buttons are local to the list's tree node, which pushed its id
        char id[kIdSize];
        snprintf(id, sizeof(id), "remove%zu", index);
        ImGui::SameLine();
        ImGui::BeginDisabled(!_lists.back().canRemove);
        if (IconButton(ICON_LC_X, "Remove this element", id)) {
            request = ListRequest{ListAction::Remove, index};
        }
        ImGui::EndDisabled();
    }

    void ImGuiPropertyVisitor::VisitNewKey(String& key) {
        if (_lists.empty()) {
            return;
        }
        NewKeyDraft& draft = GetNewKeyDraft();
        const ImGuiID fieldId = ImGui::GetID("##newKey");
        const bool owned = draft.owner == fieldId;
        char empty[kNewKeySize] = {};
        char* text = owned ? draft.text : empty;
        if (ImGui::InputTextWithHint("##newKey", "new key", text, kNewKeySize) && !owned) {
            memcpy(draft.text, empty, sizeof(draft.text));
            draft.owner = fieldId;
            text = draft.text;
        }
        ItemTooltip("The key of the new entry");
        ImGui::SameLine();
        OpenList& list = _lists.back();
        list.addDrawn = true;
        ImGui::BeginDisabled(text[0] == '\0');
        if (IconButton(ICON_LC_PLUS, "Add an entry under this key", "add")) {
            key.assign(text);
            text[0] = '\0';
            list.addClicked = true;
        }
        ImGui::EndDisabled();
    }

    void ImGuiPropertyVisitor::EndList(ListRequest& request) {
        if (!_lists.empty()) {
            const OpenList list = _lists.back();
            _lists.pop_back();
            if (!list.addDrawn) {
                ImGui::BeginDisabled(!list.canAdd);
                if (IconButton(ICON_LC_PLUS, "Add an element", "add")) {
                    request.action = ListAction::Add;
                }
                ImGui::EndDisabled();
            } else if (list.addClicked) {
                request.action = ListAction::Add;
            }
        }
        EndCompound();
    }

    bool ImGuiPropertyVisitor::VisitPresence(StringView name, bool& present) {
        const FieldAttributes& attributes = CurrentField();
        const DisabledScope disabled(attributes);
        char id[kIdSize];
        snprintf(id, sizeof(id), "##present%d", _idCounter++);
        const bool changed = ImGui::Checkbox(id, &present);
        ItemTooltip(present ? "Holds a value: clear to remove it" : "Holds no value: check to add one");
        // The value, when there is one, is drawn on this row after the checkbox; without one the name stands there
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (!present) {
            ImGui::TextDisabled("%.*s", static_cast<int>(name.size()), name.data());
            FieldTooltip(attributes);
        }
        return changed;
    }

    bool ImGuiPropertyVisitor::VisitAlternative(StringView /*name*/, size_t count, size_t& index) {
        const FieldAttributes& attributes = CurrentField();
        const DisabledScope disabled(attributes);
        char label[kIdSize];
        snprintf(label, sizeof(label), "alternative##%d", _idCounter++);
        char preview[kIdSize];
        snprintf(preview, sizeof(preview), "%zu", index);
        bool changed = false;
        if (ImGui::BeginCombo(label, preview)) {
            char item[kIdSize];
            for (size_t i = 0; i < count; ++i) {
                snprintf(item, sizeof(item), "%zu", i);
                const bool isSelected = i == index;
                if (ImGui::Selectable(item, isSelected)) {
                    changed = i != index;
                    index = i;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        FieldTooltip(attributes);
        return changed;
    }

}  // namespace BECore
