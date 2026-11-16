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
        using TextBuf = Array<char, k_textBufSize>;

        // Persistent char buffers for InputText fields, keyed by widgetId
        HashMap<String, TextBuf> s_textBuffers;

        char* GetOrCreateTextBuffer(const String& widgetId, StringView initialValue) {
            auto it = s_textBuffers.find(widgetId);
            if (it == s_textBuffers.end()) {
                auto& buf = s_textBuffers[widgetId];
                buf.fill('\0');
                const size_t copyLen = std::min(initialValue.size(), buf.size() - 1);
                std::copy_n(initialValue.data(), copyLen, buf.data());
                return buf.data();
            }
            return it->second.data();
        }

        InspectorResult RenderBool(const String& widgetId, StringView currentValue) {
            bool value = (currentValue == "true");
            const String label = Format("##{}", widgetId);
            if (ImGui::Checkbox(label.c_str(), &value)) {
                return {true, value ? String{"true"} : String{"false"}};
            }
            return {};
        }

        InspectorResult RenderInt(const String& widgetId, StringView currentValue) {
            int value = 0;
            std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), value);
            const String label = Format("##{}", widgetId);
            ImGui::SetNextItemWidth(-1.0f);
            const ScopedMonoFont mono;
            if (ImGui::InputInt(label.c_str(), &value, 0, 0)) {
                return {true, Format("{}", value)};
            }
            return {};
        }

        InspectorResult RenderFloat(const String& widgetId, StringView currentValue) {
            float value = 0.0f;
            std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), value);
            const String label = Format("##{}", widgetId);
            ImGui::SetNextItemWidth(-1.0f);
            const ScopedMonoFont mono;
            if (ImGui::InputFloat(label.c_str(), &value, 0.0f, 0.0f, "%.6g")) {
                return {true, Format("{}", value)};
            }
            return {};
        }

        InspectorResult RenderEnum(const String& widgetId, StringView currentValue, const Vector<String>& options) {
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
            const String preview{currentValue.data(), currentValue.size()};

            const String label = Format("##{}", widgetId);
            ImGui::SetNextItemWidth(isUnknown ? -(ImGui::CalcTextSize(k_unknownValueMarker).x + ImGui::GetStyle().ItemSpacing.x) : -1.0f);

            InspectorResult result;
            if (ImGui::BeginCombo(label.c_str(), preview.c_str())) {
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

        InspectorResult RenderFlags(const String& widgetId, StringView currentValue, const Vector<String>& options) {
            if (options.empty()) {
                return {};
            }

            // A token that is not an option stays a checked row until the user unchecks it, so saving never drops it silently
            const Vector<String> tokens = SplitFlagTokens(currentValue);
            const Vector<String> unknown = UnknownFlagTokens(currentValue, options);

            bool anyChanged = false;
            Vector<bool> checked(options.size());
            for (size_t i = 0; i < options.size(); ++i) {
                checked[i] = eastl::find(tokens.begin(), tokens.end(), options[i]) != tokens.end();
                const String checkLabel = Format("{}##flag{}", options[i], widgetId);
                bool state = checked[i];
                if (ImGui::Checkbox(checkLabel.c_str(), &state)) {
                    checked[i] = state;
                    anyChanged = true;
                }
            }

            Vector<bool> unknownChecked(unknown.size(), true);
            for (size_t i = 0; i < unknown.size(); ++i) {
                const String checkLabel = Format("{} {}##unknownFlag{}{}", unknown[i], k_unknownValueMarker, widgetId, unknown[i]);
                bool state = true;
                if (ImGui::Checkbox(checkLabel.c_str(), &state)) {
                    unknownChecked[i] = state;
                    anyChanged = true;
                }
            }

            if (!anyChanged) {
                return {};
            }

            Vector<String> result;
            for (size_t i = 0; i < options.size(); ++i) {
                if (checked[i]) {
                    result.push_back(options[i]);
                }
            }
            for (size_t i = 0; i < unknown.size(); ++i) {
                if (unknownChecked[i]) {
                    result.push_back(unknown[i]);
                }
            }
            return {true, JoinFlagTokens(result)};
        }

        InspectorResult RenderString(const String& widgetId, StringView currentValue) {
            char* buf = GetOrCreateTextBuffer(widgetId, currentValue);
            const String label = Format("##{}", widgetId);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText(label.c_str(), buf, k_textBufSize, ImGuiInputTextFlags_EnterReturnsTrue)) {
                return {true, String{buf}};
            }
            return {};
        }

    }  // namespace

    InspectorResult RenderFieldInspector(const String& widgetId, StringView currentValue, const FieldHint& hint) {
        switch (hint.type) {
            case FieldType::Bool:
                return RenderBool(widgetId, currentValue);
            case FieldType::Int:
                return RenderInt(widgetId, currentValue);
            case FieldType::Float:
                return RenderFloat(widgetId, currentValue);
            case FieldType::Enum:
                return RenderEnum(widgetId, currentValue, hint.options);
            case FieldType::Flags:
                return RenderFlags(widgetId, currentValue, hint.options);
            case FieldType::String:
                return RenderString(widgetId, currentValue);
            default:
                return RenderString(widgetId, currentValue);
        }
    }

    void ClearFieldInspectorState() {
        s_textBuffers.clear();
    }

}  // namespace BECore
