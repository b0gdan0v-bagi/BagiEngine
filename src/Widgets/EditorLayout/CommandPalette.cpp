#include "CommandPalette.h"

#include <Widgets/EditorLayout/CommandFilter.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr const char* kPopupId = "##CommandPalette";
        constexpr float kPaletteWidth = 480.0f;
        constexpr float kPaletteListHeight = 240.0f;
        constexpr float kPaletteTopFraction = 0.2f;  // of the viewport height: the popup sits in the upper part, like a search box
        constexpr eastl::string_view kFixedSuffix = " (fixed)";

        // Up and Down in a single-line field would otherwise move ImGui's navigation out of it
        int OnQueryKey(ImGuiInputTextCallbackData* data) {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
                int* delta = static_cast<int*>(data->UserData);
                *delta = (data->EventKey == ImGuiKey_UpArrow) ? -1 : 1;
            }
            return 0;
        }

        eastl::string ChordText(const EditorCommand& command, const HotkeyMap& hotkeys) {
            eastl::string text;
            if (const std::optional<KeyChord> chord = hotkeys.GetChord(command.id)) {
                text = ToString(*chord);
                if (!command.rebindable) {
                    text.append(kFixedSuffix.data(), kFixedSuffix.size());
                }
            }
            return text;
        }

    }  // namespace

    PoolString CommandPalette::Draw(const HotkeyMap& hotkeys) {
        if (_openRequested) {
            _openRequested = false;
            _query[0] = '\0';
            _selected = 0;
            _focusQuery = true;
            ImGui::OpenPopup(kPopupId);
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 anchor(viewport->GetCenter().x, viewport->WorkPos.y + viewport->WorkSize.y * kPaletteTopFraction);
        ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(kPaletteWidth, 0.0f));
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
        if (!ImGui::BeginPopup(kPopupId, flags)) {
            return {};
        }
        if (_focusQuery) {
            _focusQuery = false;
            ImGui::SetKeyboardFocusHere();
        }
        int move = 0;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##query", "Type a command", _query, sizeof(_query), ImGuiInputTextFlags_CallbackHistory, OnQueryKey, &move)) {
            _selected = 0;
        }

        const eastl::vector<const EditorCommand*> matches = FilterCommands(_query, GetEditorCommands());
        const int count = static_cast<int>(matches.size());
        _selected = MoveSelection(_selected, move, count);

        PoolString chosen;
        const bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        if (enterPressed && count > 0) {
            chosen = matches[static_cast<size_t>(_selected)]->id;
        }

        if (ImGui::BeginChild("##list", ImVec2(0.0f, kPaletteListHeight))) {
            if (count == 0) {
                ImGui::TextDisabled("No command matches");
            }
            const float rowWidth = ImGui::GetContentRegionAvail().x;
            for (int row = 0; row < count; ++row) {
                const EditorCommand& command = *matches[static_cast<size_t>(row)];
                const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                const eastl::string title(command.title.data(), command.title.size());
                ImGui::PushID(row);
                if (ImGui::Selectable(title.c_str(), row == _selected)) {
                    chosen = command.id;
                }
                ImGui::PopID();
                if (row == _selected && move != 0) {
                    ImGui::SetScrollHereY();
                }

                // The chord, right-aligned on the row
                const eastl::string chordText = ChordText(command, hotkeys);
                if (!chordText.empty()) {
                    const float textWidth = ImGui::CalcTextSize(chordText.c_str()).x;
                    const ImVec2 textPos(rowStart.x + rowWidth - textWidth - ImGui::GetStyle().ItemSpacing.x, rowStart.y);
                    ImGui::GetWindowDrawList()->AddText(textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), chordText.c_str());
                }
            }
        }
        ImGui::EndChild();

        if (!chosen.Empty()) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return chosen;
    }

}  // namespace BECore
