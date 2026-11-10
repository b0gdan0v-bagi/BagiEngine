#include "PreferencesWindow.h"

#include <imgui.h>

namespace BECore {

    namespace {

        constexpr const char* kWindowTitle = "Preferences";
        constexpr const char* kRebindPopupId = "Rebind Shortcut";
        constexpr ImVec2 kWindowSize{560.0f, 460.0f};
        constexpr ImVec4 kMessageColor{1.0f, 0.8f, 0.0f, 1.0f};
        constexpr float kChordColumnEms = 11.0f;
        constexpr float kActionsColumnEms = 17.0f;
        constexpr float kPopupWidthEms = 28.0f;

        // The keys a chord can name, read in the frame the user presses one
        std::optional<KeyChord> ReadPressedChord() {
            for (int index = ImGuiKey_NamedKey_BEGIN; index < ImGuiKey_NamedKey_END; ++index) {
                const ImGuiKey key = static_cast<ImGuiKey>(index);
                if (!IsNamedKey(key) || !ImGui::IsKeyPressed(key, false)) {
                    continue;
                }
                const ImGuiIO& io = ImGui::GetIO();
                uint8_t mods = 0;
                mods |= io.KeyCtrl ? kChordModCtrl : 0;
                mods |= io.KeyAlt ? kChordModAlt : 0;
                mods |= io.KeyShift ? kChordModShift : 0;
                return KeyChord{key, mods};
            }
            return std::nullopt;
        }

        const char* RefusalText(AssignRefusal refusal) {
            switch (refusal) {
                case AssignRefusal::UnknownCommand:
                    return "This command does not exist";
                case AssignRefusal::FixedCommand:
                    return "The shortcut of this command is fixed";
                case AssignRefusal::NeedsModifier:
                    return "Use Ctrl or Alt with a key, or an F key";
                case AssignRefusal::Reserved:
                    return "The editor keeps this shortcut for itself";
                case AssignRefusal::None:
                    break;
            }
            return "This shortcut cannot be used";
        }

        eastl::string TitleOf(PoolString commandId) {
            const EditorCommand* command = FindEditorCommand(commandId);
            return command ? eastl::string(command->title.data(), command->title.size()) : eastl::string(commandId.ToStringView());
        }

    }  // namespace

    bool PreferencesWindow::Draw(EditorSettings& settings) {
        if (!_isOpen) {
            return false;
        }
        if (_focusRequested) {
            _focusRequested = false;
            ImGui::SetNextWindowFocus();
        }
        ImGui::SetNextWindowSize(kWindowSize, ImGuiCond_FirstUseEver);

        bool changed = false;
        if (ImGui::Begin(kWindowTitle, &_isOpen, ImGuiWindowFlags_NoDocking)) {
            if (ImGui::BeginTabBar("##PreferencesTabs")) {
                if (ImGui::BeginTabItem("General")) {
                    changed = DrawGeneralTab(settings);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Hotkeys")) {
                    DrawHotkeysTab(settings);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            DrawRebindPopup(settings);
        }
        ImGui::End();
        return changed;
    }

    bool PreferencesWindow::DrawGeneralTab(EditorSettings& settings) {
        EditorPreferences preferences = settings.GetPreferences();
        bool changed = false;
        bool commit = false;  // save now: a click, or the end of a slider drag

        const eastl::string currentTheme(EnumUtils<EditorTheme>::ToString(preferences.theme));
        if (ImGui::BeginCombo("Theme", currentTheme.c_str())) {
            const auto& values = EnumUtils<EditorTheme>::Values();
            const auto& names = EnumUtils<EditorTheme>::Names();
            for (size_t index = 0; index < values.size(); ++index) {
                const eastl::string name(names[index].data(), names[index].size());
                if (ImGui::Selectable(name.c_str(), values[index] == preferences.theme)) {
                    preferences.theme = values[index];
                    changed = commit = true;
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::SliderFloat("Font scale", &preferences.fontScale, kMinFontScale, kMaxFontScale, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
            changed = true;
        }
        commit |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::SeparatorText("Viewport");
        if (ImGui::Checkbox("Show a grid over the image", &preferences.viewportGrid)) {
            changed = commit = true;
        }
        ImGui::BeginDisabled(!preferences.viewportGrid);
        if (ImGui::SliderInt("Grid step (image pixels)", &preferences.viewportGridStep, kMinViewportGridStep, kMaxViewportGridStep, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            changed = true;
        }
        commit |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndDisabled();

        if (changed) {
            settings.SetPreferences(preferences);
        }
        if (commit) {
            _preferencesError = settings.SavePreferences() ? eastl::string() : settings.GetLastSaveError();
        }
        if (!_preferencesError.empty()) {
            ImGui::TextColored(kMessageColor, "Not saved: %s", _preferencesError.c_str());
        }
        return changed;
    }

    void PreferencesWindow::DrawHotkeysTab(EditorSettings& settings) {
        HotkeyMap& hotkeys = settings.GetHotkeys();
        const float em = ImGui::GetFontSize();

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::BeginTable("##Hotkeys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(0.0f, -footerHeight))) {
            ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, em * kChordColumnEms);
            ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed, em * kActionsColumnEms);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            int row = 0;
            for (const EditorCommand& command : GetEditorCommands()) {
                ImGui::PushID(row++);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(command.title.data(), command.title.data() + command.title.size());

                ImGui::TableNextColumn();
                const std::optional<KeyChord> chord = hotkeys.GetChord(command.id);
                if (chord) {
                    ImGui::TextUnformatted(ToString(*chord).c_str());
                } else {
                    ImGui::TextDisabled("none");
                }

                ImGui::TableNextColumn();
                if (!command.rebindable) {
                    ImGui::TextDisabled("fixed");
                } else {
                    if (ImGui::Button("Rebind")) {
                        StartRebind(command.id);
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!chord);
                    if (ImGui::Button("Clear")) {
                        hotkeys.Unbind(command.id);
                        _hotkeysMessage.clear();
                        SaveHotkeys(settings);
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(hotkeys.IsDefault(command.id));
                    if (ImGui::Button("Reset")) {
                        _hotkeysMessage.clear();
                        const AssignResult result = hotkeys.ResetOne(command.id);
                        if (result.status == AssignStatus::Conflict) {
                            _hotkeysMessage = Format("The default shortcut is used by {}", TitleOf(result.otherCommandId));
                        } else {
                            SaveHotkeys(settings);
                        }
                    }
                    ImGui::EndDisabled();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (ImGui::Button("Reset all")) {
            hotkeys.ResetAll();
            _hotkeysMessage.clear();
            SaveHotkeys(settings);
        }
        if (!_hotkeysMessage.empty()) {
            ImGui::TextColored(kMessageColor, "%s", _hotkeysMessage.c_str());
        }
    }

    void PreferencesWindow::StartRebind(PoolString commandId) {
        _rebindCommand = commandId;
        _stage = RebindStage::Capturing;
        _refusal.clear();
        _hotkeysMessage.clear();
        _openRebindPopup = true;
    }

    void PreferencesWindow::SaveHotkeys(EditorSettings& settings) {
        if (!settings.SaveHotkeys()) {
            _hotkeysMessage = Format("Not saved: {}", settings.GetLastSaveError());
        }
    }

    void PreferencesWindow::DrawRebindPopup(EditorSettings& settings) {
        if (_openRebindPopup) {
            _openRebindPopup = false;
            ImGui::OpenPopup(kRebindPopupId);
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * kPopupWidthEms, 0.0f));
        if (!ImGui::BeginPopupModal(kRebindPopupId, nullptr, ImGuiWindowFlags_NoResize)) {
            _stage = RebindStage::Closed;
            return;
        }

        const eastl::string title = TitleOf(_rebindCommand);
        HotkeyMap& hotkeys = settings.GetHotkeys();
        bool close = false;

        if (_stage == RebindStage::Conflict) {
            ImGui::TextWrapped("%s is already used by %s.", ToString(_pendingChord).c_str(), TitleOf(_conflictCommand).c_str());
            ImGui::TextWrapped("Swap takes it from there and gives it to %s.", title.c_str());
            if (ImGui::Button("Swap")) {
                hotkeys.AssignSwapping(_rebindCommand, _pendingChord);
                _hotkeysMessage.clear();
                SaveHotkeys(settings);
                close = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                close = true;
            }
        } else {
            ImGui::TextWrapped("Press the new shortcut for %s. Esc cancels.", title.c_str());
            if (!_refusal.empty()) {
                ImGui::TextColored(kMessageColor, "%s", _refusal.c_str());
            }
            // Enter and Space must reach the chord reader, not press the focused button
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            if (ImGui::Button("Cancel")) {
                close = true;
            }
            ImGui::PopItemFlag();

            // The frame that opened the popup may still carry the key that activated the Rebind button
            const std::optional<KeyChord> pressed = ImGui::IsWindowAppearing() ? std::nullopt : ReadPressedChord();
            if (pressed) {
                if (pressed->key == ImGuiKey_Escape && pressed->mods == 0) {
                    close = true;
                } else {
                    const AssignResult result = hotkeys.Assign(_rebindCommand, *pressed);
                    if (result.status == AssignStatus::Assigned) {
                        _hotkeysMessage.clear();
                        SaveHotkeys(settings);
                        close = true;
                    } else if (result.status == AssignStatus::Conflict) {
                        _pendingChord = *pressed;
                        _conflictCommand = result.otherCommandId;
                        _stage = RebindStage::Conflict;
                    } else {
                        _refusal = RefusalText(result.refusal);
                    }
                }
            }
        }

        if (close) {
            _stage = RebindStage::Closed;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

}  // namespace BECore
