#include "GameSelectorWidget.h"

#include <BECore/Reflection/IDeserializer.h>
#include <Games/GameManager.h>
#include <Games/IGame.h>
#include <Generated/GameSelectorWidget.gen.hpp>
#include <imgui.h>

namespace BECore {

    bool GameSelectorWidget::Initialize(IDeserializer& /*deserializer*/) {
        return true;
    }

    void GameSelectorWidget::Update() {
        auto& mgr = GameManager::GetInstance();
        const auto& games = mgr.GetAll();

        ImGui::SetNextWindowSize(ImVec2(280.0f, 100.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Games")) {
            ImGui::End();
            return;
        }

        if (games.empty()) {
            ImGui::TextDisabled("No games registered");
            ImGui::End();
            return;
        }

        // Sync the selection with the currently active game whenever the user hasn't
        // touched the combo yet, or the active game changed externally.
        if (_selectedIndex < 0 || _selectedIndex >= static_cast<int>(games.size())) {
            _selectedIndex = 0;
        }
        if (auto* active = mgr.GetActive()) {
            for (size_t i = 0; i < games.size(); ++i) {
                if (games[i].Get() == active) {
                    _selectedIndex = static_cast<int>(i);
                    break;
                }
            }
        }

        const auto& selectedGame = games[static_cast<size_t>(_selectedIndex)];
        const auto previewName = selectedGame->GetName().ToStringView();
        if (ImGui::BeginCombo("##game_combo", previewName.data())) {
            for (int i = 0; i < static_cast<int>(games.size()); ++i) {
                const bool isSelected = (i == _selectedIndex);
                const auto label = games[static_cast<size_t>(i)]->GetName().ToStringView();
                if (ImGui::Selectable(label.data(), isSelected)) {
                    _selectedIndex = i;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Switch")) {
            mgr.SwitchTo(games[static_cast<size_t>(_selectedIndex)]->GetName());
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            mgr.ResetActive();
        }

        if (auto* active = mgr.GetActive()) {
            ImGui::TextDisabled("Active: %.*s", static_cast<int>(active->GetName().ToStringView().size()), active->GetName().ToStringView().data());
        } else {
            ImGui::TextDisabled("Active: <none>");
        }

        ImGui::End();
    }

    void GameSelectorWidget::Draw() {}

}  // namespace BECore
