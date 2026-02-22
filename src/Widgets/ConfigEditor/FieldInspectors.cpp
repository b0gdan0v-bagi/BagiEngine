#include "FieldInspectors.h"

#include <EASTL/array.h>
#include <EASTL/unordered_map.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr size_t k_textBufSize = 256;
        using TextBuf = eastl::array<char, k_textBufSize>;

        // Persistent char buffers for InputText fields, keyed by widgetId
        eastl::unordered_map<eastl::string, TextBuf> s_textBuffers;

        char* GetOrCreateTextBuffer(const eastl::string& widgetId, eastl::string_view initialValue) {
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

        InspectorResult RenderBool(const eastl::string& widgetId, eastl::string_view currentValue) {
            bool value = (currentValue == "true");
            const eastl::string label = Format("##{}", widgetId);
            if (ImGui::Checkbox(label.c_str(), &value)) {
                return {true, value ? eastl::string{"true"} : eastl::string{"false"}};
            }
            return {};
        }

        InspectorResult RenderInt(const eastl::string& widgetId, eastl::string_view currentValue) {
            int value = 0;
            std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), value);
            const eastl::string label = Format("##{}", widgetId);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputInt(label.c_str(), &value, 0, 0)) {
                return {true, Format("{}", value)};
            }
            return {};
        }

        InspectorResult RenderFloat(const eastl::string& widgetId, eastl::string_view currentValue) {
            float value = 0.0f;
            std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), value);
            const eastl::string label = Format("##{}", widgetId);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputFloat(label.c_str(), &value, 0.0f, 0.0f, "%.6g")) {
                return {true, Format("{}", value)};
            }
            return {};
        }

        InspectorResult RenderEnum(const eastl::string& widgetId, eastl::string_view currentValue,
                                   const eastl::vector<eastl::string>& options) {
            if (options.empty()) {
                return {};
            }

            int selectedIndex = -1;
            for (int i = 0; i < static_cast<int>(options.size()); ++i) {
                if (eastl::string_view{options[i].c_str(), options[i].size()} == currentValue) {
                    selectedIndex = i;
                    break;
                }
            }

            // Build null-terminated list for ImGui combo
            eastl::string items;
            for (const auto& opt : options) {
                items.append(opt);
                items.push_back('\0');
            }
            items.push_back('\0');

            const eastl::string label = Format("##{}", widgetId);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo(label.c_str(), &selectedIndex, items.c_str())) {
                if (selectedIndex >= 0 && selectedIndex < static_cast<int>(options.size())) {
                    return {true, options[static_cast<size_t>(selectedIndex)]};
                }
            }
            return {};
        }

        InspectorResult RenderFlags(const eastl::string& widgetId, eastl::string_view currentValue,
                                    const eastl::vector<eastl::string>& options) {
            if (options.empty()) {
                return {};
            }

            // Parse current active flags from the value string
            auto isActive = [&](const eastl::string& flag) {
                eastl::string_view remaining = currentValue;
                while (!remaining.empty()) {
                    const auto pos = remaining.find('|');
                    eastl::string_view token =
                        (pos == eastl::string_view::npos) ? remaining : remaining.substr(0, pos);
                    while (!token.empty() && token.front() == ' ') {
                        token.remove_prefix(1);
                    }
                    while (!token.empty() && token.back() == ' ') {
                        token.remove_suffix(1);
                    }
                    if (token == eastl::string_view{flag.c_str(), flag.size()}) {
                        return true;
                    }
                    if (pos == eastl::string_view::npos) {
                        break;
                    }
                    remaining = remaining.substr(pos + 1);
                }
                return false;
            };

            bool anyChanged = false;
            eastl::vector<bool> checked(options.size());
            for (size_t i = 0; i < options.size(); ++i) {
                checked[i] = isActive(options[i]);
            }

            for (size_t i = 0; i < options.size(); ++i) {
                const eastl::string checkLabel = Format("{}##flag{}", options[i], widgetId);
                bool state = checked[i];
                if (ImGui::Checkbox(checkLabel.c_str(), &state)) {
                    checked[i] = state;
                    anyChanged = true;
                }
            }

            if (anyChanged) {
                eastl::string result;
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

        InspectorResult RenderString(const eastl::string& widgetId, eastl::string_view currentValue) {
            char* buf = GetOrCreateTextBuffer(widgetId, currentValue);
            const eastl::string label = Format("##{}", widgetId);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText(label.c_str(), buf, k_textBufSize,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                return {true, eastl::string{buf}};
            }
            return {};
        }

    } // namespace

    // =========================================================================
    // Public API
    // =========================================================================

    InspectorResult RenderFieldInspector(const eastl::string& widgetId,
                                          eastl::string_view currentValue, const FieldHint& hint) {
        switch (hint.type) {
            case FieldType::Bool:   return RenderBool(widgetId, currentValue);
            case FieldType::Int:    return RenderInt(widgetId, currentValue);
            case FieldType::Float:  return RenderFloat(widgetId, currentValue);
            case FieldType::Enum:   return RenderEnum(widgetId, currentValue, hint.options);
            case FieldType::Flags:  return RenderFlags(widgetId, currentValue, hint.options);
            case FieldType::String: return RenderString(widgetId, currentValue);
            default:                return RenderString(widgetId, currentValue);
        }
    }

    void ClearFieldInspectorState() {
        s_textBuffers.clear();
    }

} // namespace BECore
