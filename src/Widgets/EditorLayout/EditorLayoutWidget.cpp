#include "EditorLayoutWidget.h"

#include <BECore/Reflection/IDeserializer.h>
#include <Events/ApplicationEvents.h>
#include <Generated/EditorLayoutWidget.gen.hpp>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    bool EditorLayoutWidget::Initialize(IDeserializer& /*deserializer*/) {
        return true;
    }

    void EditorLayoutWidget::Update() {
        // Cover the entire OS window work-area with a borderless, decoration-less host.
        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::SetNextWindowViewport(mainViewport->ID);

        constexpr ImGuiWindowFlags hostFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove     |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
        ImGui::Begin("##EditorRoot", nullptr, hostFlags);
        ImGui::PopStyleVar(3);

        // --- Menu bar ---
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Quit")) {
                    ApplicationEvents::QuitEvent::Emit();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Reset to Defaults")) {
                    _forceResetLayout = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // --- DockSpace ---
        const ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");

        bool doReset = _forceResetLayout;
        _forceResetLayout = false;

        if (!_layoutInitialized) {
            _layoutInitialized = true;
            doReset = (ImGui::DockBuilderGetNode(dockspaceId) == nullptr);
        }

        if (doReset) {
            BuildDefaultLayout(dockspaceId, mainViewport->WorkSize);
        }

        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        ImGui::End();
    }

    void EditorLayoutWidget::Draw() {
    }

    void EditorLayoutWidget::BuildDefaultLayout(ImGuiID id, ImVec2 size) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(id, size);

        // Right: Sprite Library (25%)
        ImGuiID right, center;
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.25f, &right, &center);

        // Left: Scene Editor / Config Editor (22% of remaining)
        ImGuiID left, viewport;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, &left, &viewport);

        // Left panel split: Scene Editor (top 70%) / Config Editor (bottom 30%)
        ImGuiID leftTop, leftBottom;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.3f, &leftBottom, &leftTop);

        ImGui::DockBuilderDockWindow("Scene Editor",   leftTop);
        ImGui::DockBuilderDockWindow("Config Editor",  leftBottom);
        ImGui::DockBuilderDockWindow("Viewport",       viewport);
        ImGui::DockBuilderDockWindow("Sprite Library", right);

        ImGui::DockBuilderFinish(id);
    }

}  // namespace BECore
