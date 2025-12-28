#include "ImGuiWidget.h"

#include "Core/Events/Events.h"

#include <Application/Application.h>

#include <imgui.h>

namespace Core {

    void ImGuiWidget::Draw() {
        if (ImGui::Begin("Debug Widget")) {
            if (ImGui::Button("Quit")) {
                Application::GetInstance().GetEventManager().Emit(QuitEvent{});
            }
            ImGui::End();
        }
    }

}  // namespace Core

