#include "SceneEditorWidget.h"

#include "ComponentInspectors.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Generated/SceneEditorWidget.gen.hpp>
#include <imgui.h>

namespace BECore {

    // =========================================================================
    // IWidget interface
    // =========================================================================

    bool SceneEditorWidget::Initialize(IDeserializer& /*deserializer*/) {
        _nodeNameBuffer.resize(256, '\0');
        return true;
    }

    void SceneEditorWidget::Update() {
        ImGui::SetNextWindowSize(ImVec2(1000.0f, 600.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Scene Editor")) {
            ImGui::End();
            return;
        }

        auto& sceneManager = CoreManager::GetSceneManager();
        auto* scene = sceneManager.GetActiveScene();

        if (!scene) {
            ImGui::TextDisabled("No active scene");
            ImGui::End();
            return;
        }

        // ----------------------------------------------------------------
        // Left panel - Hierarchy
        // ----------------------------------------------------------------
        const float leftPanelWidth = 300.0f;
        ImGui::BeginChild("##hierarchy", ImVec2(leftPanelWidth, 0.0f), true);

        ImGui::TextUnformatted("Hierarchy");
        ImGui::Separator();

        // Reset node counter for tree node IDs
        _hierarchyNodeCounter = 0;

        // Render root node tree
        auto& rootNode = scene->GetRootNode();
        RenderHierarchyNode(rootNode, _hierarchyNodeCounter++);

        ImGui::Separator();
        if (ImGui::Button("[+ Add Node]", ImVec2(-1.0f, 0.0f))) {
            _showAddNodePopup = true;
            _nodeNameBuffer.assign("NewNode");
        }

        ImGui::EndChild();

        // ----------------------------------------------------------------
        // Right panel - Inspector
        // ----------------------------------------------------------------
        ImGui::SameLine();
        ImGui::BeginChild("##inspector", ImVec2(0.0f, 0.0f), true);

        ImGui::TextUnformatted("Inspector");
        ImGui::Separator();

        RenderInspector();

        ImGui::EndChild();

        ImGui::End();

        // Popups
        RenderAddNodePopup();
        RenderAddComponentPopup();
    }

    void SceneEditorWidget::Draw() {
        // All rendering is done in Update
    }

    // =========================================================================
    // Private methods
    // =========================================================================

    void SceneEditorWidget::RenderHierarchyNode(SceneNode& node, int nodeId) {
        const auto& children = node.GetChildren();
        bool isLeaf = children.empty();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (isLeaf) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (_selectedNode == &node) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        bool isOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(nodeId)), flags, "%s", node.GetName().CStr());

        if (ImGui::IsItemClicked()) {
            _selectedNode = &node;
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Add Child")) {
                _showAddNodePopup = true;
                _nodeNameBuffer.assign("ChildNode");
            }
            if (ImGui::MenuItem("Delete") && _selectedNode == &node) {
                auto* parent = node.GetParent();
                if (parent) {
                    parent->RemoveChild(&node);
                    _selectedNode = nullptr;
                }
            }
            ImGui::EndPopup();
        }

        // Render children
        if (isOpen) {
            for (const auto& child : children) {
                RenderHierarchyNode(*child.Get(), _hierarchyNodeCounter++);
            }
            ImGui::TreePop();
        }
    }

    void SceneEditorWidget::RenderInspector() {
        if (!_selectedNode) {
            ImGui::TextDisabled("Select a node to inspect");
            return;
        }

        // Node name editor
        ImGui::TextUnformatted("Name:");
        ImGui::SameLine();

        eastl::string nameStr(_selectedNode->GetName().CStr());
        nameStr.resize(256, '\0');
        if (ImGui::InputText("##node_name", &nameStr[0], nameStr.capacity())) {
            // Trim null terminators for display
            size_t endPos = nameStr.find('\0');
            if (endPos != eastl::string::npos) {
                nameStr.erase(endPos);
            }
            _selectedNode->SetName(PoolString::Intern(nameStr));
        }

        // Entity ID (read-only)
        ImGui::TextUnformatted("Entity:");
        ImGui::SameLine();
        ImGui::Text("#%u", static_cast<unsigned int>(_selectedNode->GetEntity()));

        ImGui::Separator();

        // Components
        ImGui::TextUnformatted("Components");

        const auto& components = _selectedNode->GetComponents();
        for (size_t i = 0; i < components.size(); ++i) {
            const auto& component = components[i];
            const auto& typeMeta = component->GetTypeMeta();

            ImGui::PushID(static_cast<int>(i));
            if (ImGui::CollapsingHeader(typeMeta.typeName.data())) {
                ImGui::Indent();

                // Render component-specific inspector
                RenderComponentInspector(const_cast<IntrusivePtr<IComponent>&>(component));

                // Remove button
                ImGui::SameLine();
                if (ImGui::SmallButton("[x]")) {
                    _selectedNode->RemoveComponent(PoolString::Intern(typeMeta.typeName));
                }

                ImGui::Unindent();
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::Button("[+ Add Component]", ImVec2(-1.0f, 0.0f))) {
            _showAddComponentPopup = true;
        }
    }

    void SceneEditorWidget::RenderAddNodePopup() {
        if (!_showAddNodePopup) {
            return;
        }

        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Add Node##modal", &_showAddNodePopup)) {
            ImGui::TextUnformatted("Node name:");
            ImGui::InputText("##add_node_name", &_nodeNameBuffer[0], _nodeNameBuffer.capacity());

            if (ImGui::Button("OK", ImVec2(120.0f, 0.0f))) {
                if (!_nodeNameBuffer.empty() && _selectedNode) {
                    size_t endPos = _nodeNameBuffer.find('\0');
                    if (endPos != eastl::string::npos) {
                        _nodeNameBuffer.erase(endPos);
                    }
                    _selectedNode->AddChild(PoolString::Intern(_nodeNameBuffer));
                    _showAddNodePopup = false;
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                _showAddNodePopup = false;
            }

            ImGui::EndPopup();
        }
    }

    void SceneEditorWidget::RenderAddComponentPopup() {
        if (!_showAddComponentPopup) {
            return;
        }

        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Add Component##modal", &_showAddComponentPopup)) {
            ImGui::TextUnformatted("Component type:");
            ImGui::Separator();

            const auto& factory = AbstractFactory<IComponent>::GetInstance();
            const auto& registeredTypes = factory.GetRegisteredTypes();

            for (const auto& meta : registeredTypes) {
                eastl::string metaString{meta.typeName};
                if (ImGui::Selectable(metaString.c_str())) {
                    if (_selectedNode) {
                        auto newComponent = factory.Create(meta);
                        if (newComponent) {
                            _selectedNode->AddComponent(newComponent);
                            _showAddComponentPopup = false;
                        }
                    }
                }
            }

            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                _showAddComponentPopup = false;
            }

            ImGui::EndPopup();
        }
    }

}  // namespace BECore
