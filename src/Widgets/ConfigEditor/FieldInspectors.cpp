#include "FieldInspectors.h"

#include <EASTL/array.h>
#include <EASTL/unordered_map.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr size_t k_textBufSize = 256;
        constexpr const char* k_unknownValueMarker = "(not a known value)";
        using TextBuf = Array<char, k_textBufSize>;

        // Persistent char buffers for InputText fields, keyed by widgetId
        eastl::unordered_map<String, TextBuf> s_textBuffers;

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

        // -------------------------------------------------------------------------
        // Per-type renderers
        // -------------------------------------------------------------------------

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

            // Parse current active flags from the value string
            auto isActive = [&](const String& flag) {
                StringView remaining = currentValue;
                while (!remaining.empty()) {
                    const auto pos = remaining.find('|');
                    StringView token = (pos == StringView::npos) ? remaining : remaining.substr(0, pos);
                    while (!token.empty() && token.front() == ' ') {
                        token.remove_prefix(1);
                    }
                    while (!token.empty() && token.back() == ' ') {
                        token.remove_suffix(1);
                    }
                    if (token == StringView{flag.c_str(), flag.size()}) {
                        return true;
                    }
                    if (pos == StringView::npos) {
                        break;
                    }
                    remaining = remaining.substr(pos + 1);
                }
                return false;
            };

            bool anyChanged = false;
            Vector<bool> checked(options.size());
            for (size_t i = 0; i < options.size(); ++i) {
                checked[i] = isActive(options[i]);
            }

            for (size_t i = 0; i < options.size(); ++i) {
                const String checkLabel = Format("{}##flag{}", options[i], widgetId);
                bool state = checked[i];
                if (ImGui::Checkbox(checkLabel.c_str(), &state)) {
                    checked[i] = state;
                    anyChanged = true;
                }
            }

            if (anyChanged) {
                String result;
                for (size_t i = 0; i < options.size(); ++i) {
                    if (checked[i]) {
                        if (!result.empty()) {
                            result.push_back('|');
                        }
                        result.append(options[i]);
                    }
                }
                return {true, std::move(result)};
            }
            return {};
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

    // =========================================================================
    // Public API
    // =========================================================================

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
