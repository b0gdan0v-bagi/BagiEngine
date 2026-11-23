#include "FieldInspectors.h"

#include <EASTL/array.h>
#include <EASTL/unordered_map.h>
#include <Widgets/ConfigEditor/FlagValue.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr size_t k_textBufSize = 256;
        constexpr const char* k_unknownValueMarker = "(not a known value)";
        // The label of every control: the field's own id is on the ImGui id stack (PushID in RenderFieldInspector)
        constexpr const char* k_valueLabel = "##value";
        using TextBuf = Array<char, k_textBufSize>;

        // Persistent char buffers for InputText fields, keyed by widgetId
        HashMap<String, TextBuf> s_textBuffers;

        char* GetOrCreateTextBuffer(StringView widgetId, StringView initialValue) {
            auto it = s_textBuffers.find(widgetId);
            if (it == s_textBuffers.end()) {
                auto& buf = s_textBuffers[String(widgetId.data(), widgetId.size())];
                buf.fill('\0');
                const size_t copyLen = std::min(initialValue.size(), buf.size() - 1);
                std::copy_n(initialValue.data(), copyLen, buf.data());
                return buf.data();
            }
            return it->second.data();
        }

        InspectorResult RenderBool(StringView currentValue) {
            bool value = (currentValue == "true");
            if (ImGui::Checkbox(k_valueLabel, &value)) {
                return {true, value ? String{"true"} : String{"false"}};
            }
            return {};
        }

        InspectorResult RenderInt(StringView currentValue) {
            int value = 0;
            std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), value);
            ImGui::SetNextItemWidth(-1.0f);
            const ScopedMonoFont mono;
            if (ImGui::InputInt(k_valueLabel, &value, 0, 0)) {
                return {true, Format("{}", value)};
            }
            return {};
        }

        InspectorResult RenderFloat(StringView currentValue) {
            float value = 0.0f;
            std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), value);
            ImGui::SetNextItemWidth(-1.0f);
            const ScopedMonoFont mono;
            if (ImGui::InputFloat(k_valueLabel, &value, 0.0f, 0.0f, "%.6g")) {
                return {true, Format("{}", value)};
            }
            return {};
        }

        InspectorResult RenderEnum(StringView currentValue, const Vector<String>& options) {
            if (options.empty()) {
                return {};
            }

            int selectedIndex = -1;
            for (int i = 0; i < static_cast<int>(options.size()); ++i) {
                if (StringView{options[i].c_str(), options[i].size()} == currentValue) {
                    selectedIndex = i;
                    break;
                }
            }

            // A value that is not an option (a typo, or a name the code no longer knows) is shown as it is and flagged
            const bool isUnknown = selectedIndex < 0 && !currentValue.empty();
            char preview[k_textBufSize];
            const size_t previewLength = std::min(currentValue.size(), k_textBufSize - 1);
            std::memcpy(preview, currentValue.data(), previewLength);
            preview[previewLength] = '\0';
            ImGui::SetNextItemWidth(isUnknown ? -(ImGui::CalcTextSize(k_unknownValueMarker).x + ImGui::GetStyle().ItemSpacing.x) : -1.0f);

            InspectorResult result;
            if (ImGui::BeginCombo(k_valueLabel, preview)) {
                for (int i = 0; i < static_cast<int>(options.size()); ++i) {
                    const bool isSelected = (i == selectedIndex);
                    ImGui::PushID(i);
                    if (ImGui::Selectable(options[static_cast<size_t>(i)].c_str(), isSelected) && !isSelected) {
                        result = {true, options[static_cast<size_t>(i)]};
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

            if (isUnknown) {
                ImGui::SameLine();
                ImGui::TextColored(GetImGuiPalette().warning, "%s", k_unknownValueMarker);
            }
            return result;
        }

        InspectorResult RenderFlags(StringView currentValue, const Vector<String>& options) {
            if (options.empty()) {
                return {};
            }

            // At most one box changes per frame (one press, one key): the first change is the one applied
            int toggledOption = -1;
            bool toggledOptionState = false;
            String toggledUnknown;
            bool toggledUnknownState = false;
            bool anyChanged = false;

            for (size_t i = 0; i < options.size(); ++i) {
                bool state = HasFlagToken(currentValue, StringView(options[i].data(), options[i].size()));
                ImGui::PushID(static_cast<int>(i));
                const bool clicked = ImGui::Checkbox(options[i].c_str(), &state);
                ImGui::PopID();
                if (clicked && !anyChanged) {
                    anyChanged = true;
                    toggledOption = static_cast<int>(i);
                    toggledOptionState = state;
                }
            }

            // A token that is not an option stays a checked row until the user unchecks it, so saving never drops it silently
            ForEachUnknownFlagToken(currentValue, options, [&](StringView token) {
                char label[k_textBufSize];
                const FormatResult written = FormatToN(label, sizeof(label) - 1, "{} {}", token, k_unknownValueMarker);
                label[std::min(written.size, sizeof(label) - 1)] = '\0';
                bool state = true;
                ImGui::PushID(token.data(), token.data() + token.size());
                const bool clicked = ImGui::Checkbox(label, &state);
                ImGui::PopID();
                if (clicked && !anyChanged) {
                    anyChanged = true;
                    toggledUnknown.assign(token.data(), token.size());
                    toggledUnknownState = state;
                }
            });

            if (!anyChanged) {
                return {};
            }

            Vector<String> result;
            for (size_t i = 0; i < options.size(); ++i) {
                const bool checked = (static_cast<int>(i) == toggledOption) ? toggledOptionState : HasFlagToken(currentValue, StringView(options[i].data(), options[i].size()));
                if (checked) {
                    result.push_back(options[i]);
                }
            }
            ForEachUnknownFlagToken(currentValue, options, [&](StringView token) {
                const bool toggledThis = !toggledUnknown.empty() && StringView(toggledUnknown.data(), toggledUnknown.size()) == token;
                if (!toggledThis || toggledUnknownState) {
                    result.emplace_back(token.data(), token.size());
                }
            });
            return {true, JoinFlagTokens(result)};
        }

        InspectorResult RenderString(StringView widgetId, StringView currentValue) {
            char* buf = GetOrCreateTextBuffer(widgetId, currentValue);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText(k_valueLabel, buf, k_textBufSize, ImGuiInputTextFlags_EnterReturnsTrue)) {
                return {true, String{buf}};
            }
            return {};
        }

    }  // namespace

    InspectorResult RenderFieldInspector(StringView widgetId, StringView currentValue, const FieldHint& hint) {
        // The field's own id scopes the fixed labels of its controls, so no label is formatted per frame
        ImGui::PushID(widgetId.data(), widgetId.data() + widgetId.size());
        InspectorResult result;
        switch (hint.type) {
            case FieldType::Bool:
                result = RenderBool(currentValue);
                break;
            case FieldType::Int:
                result = RenderInt(currentValue);
                break;
            case FieldType::Float:
                result = RenderFloat(currentValue);
                break;
            case FieldType::Enum:
                result = RenderEnum(currentValue, hint.options);
                break;
            case FieldType::Flags:
                result = RenderFlags(currentValue, hint.options);
                break;
            case FieldType::String:
            default:
                result = RenderString(widgetId, currentValue);
                break;
        }
        ImGui::PopID();
        return result;
    }

    void ClearFieldInspectorState() {
        s_textBuffers.clear();
    }

}  // namespace BECore
