#include "ImGuiWidget.h"

#include <Application/Application.h>

#include <imgui.h>

namespace Core {

    void ImGuiWidget::Draw() {
        if (ImGui::Begin("Debug Widget")) {
            if (ImGui::Button("Quit")) {
                Application::GetInstance().StopApplication();
            }
            ImGui::End();
        }
    }

}  // namespace Core

