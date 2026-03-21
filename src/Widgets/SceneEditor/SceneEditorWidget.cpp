#include "SceneEditorWidget.h"

#include "ComponentInspectors.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Generated/SceneEditorWidget.gen.hpp>
#include <imgui.h>

namespace BECore {

    namespace {

        void BuildNodeXml(pugi::xml_node parent, const SceneNode& node) {
            pugi::xml_node nodeEl = parent.append_child("node");
            nodeEl.append_attribute("name").set_value(node.GetName().CStr());

            const auto& components = node.GetComponents();
            if (components.empty()) {
                return;
            }

            pugi::xml_node compsEl = nodeEl.append_child("components");
            for (const auto& comp : components) {
                pugi::xml_node compEl = compsEl.append_child("component");
                const auto& typeMeta = comp->GetTypeMeta();
                compEl.append_attribute("type").set_value(typeMeta.typeName.data());

                if (typeMeta.typeName == eastl::string_view("TransformComponent")) {
                    const auto& t = static_cast<const TransformComponent&>(*comp);
                    compEl.append_attribute("x").set_value(t._x);
                    compEl.append_attribute("y").set_value(t._y);
                    compEl.append_attribute("width").set_value(t._width);
                    compEl.append_attribute("height").set_value(t._height);
                } else if (typeMeta.typeName == eastl::string_view("QuadRendererComponent")) {
                    const auto& q = static_cast<const QuadRendererComponent&>(*comp);
                    compEl.append_attribute("r").set_value(static_cast<unsigned int>(q._color.r));
                    compEl.append_attribute("g").set_value(static_cast<unsigned int>(q._color.g));
                    compEl.append_attribute("b").set_value(static_cast<unsigned int>(q._color.b));
                    compEl.append_attribute("a").set_value(static_cast<unsigned int>(q._color.a));
                }
            }
        }

    }  // namespace

    // =========================================================================
    // IWidget interface
    // =========================================================================

    bool SceneEditorWidget::Initialize(IDeserializer& /*deserializer*/) {
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
        // Toolbar
        // ----------------------------------------------------------------
        ImGui::BeginDisabled(!_isDirty);
        if (ImGui::Button("Save")) {
            SaveScene();
        }
        ImGui::EndDisabled();

        if (_isDirty) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(unsaved changes)");
        }

        ImGui::Separator();

        // ----------------------------------------------------------------
        // Left panel - Hierarchy
        // ----------------------------------------------------------------
        const float leftPanelWidth = 300.0f;
        ImGui::BeginChild("##hierarchy", ImVec2(leftPanelWidth, 0.0f), true);

        ImGui::TextUnformatted("Hierarchy");
        ImGui::Separator();

        auto& rootNode = scene->GetRootNode();
        RenderHierarchyNode(rootNode);

        ImGui::Separator();
        if (ImGui::Button("[+ Add Node]", ImVec2(-1.0f, 0.0f))) {
            _contextMenuTargetNode = nullptr;
            _showAddNodePopup = true;
            snprintf(_nodeNameBuffer, sizeof(_nodeNameBuffer), "NewNode");
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

        // ----------------------------------------------------------------
        // Trigger popups (must happen inside Begin/End, after child windows)
        // ----------------------------------------------------------------
        if (_showAddNodePopup) {
            ImGui::OpenPopup("Add Node##modal");
            _showAddNodePopup = false;
        }
        if (_showAddComponentPopup) {
            ImGui::OpenPopup("Add Component##modal");
            _showAddComponentPopup = false;
        }

        RenderAddNodePopup();
        RenderAddComponentPopup();

        ImGui::End();
    }

    void SceneEditorWidget::Draw() {
        // All rendering is done in Update
    }

    // =========================================================================
    // Private methods
    // =========================================================================

    void SceneEditorWidget::RenderHierarchyNode(SceneNode& node) {
        // Take a copy for safe iteration — RemoveChild may modify the parent's vector
        const auto children = node.GetChildren();
        const bool isLeaf = children.empty();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (isLeaf) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (_selectedNode == &node) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        const bool isOpen = ImGui::TreeNodeEx(&node, flags, "%s", node.GetName().CStr());

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            _selectedNode = &node;
        }

        bool nodeDeleted = false;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Add Child")) {
                _contextMenuTargetNode = &node;
                _showAddNodePopup = true;
                snprintf(_nodeNameBuffer, sizeof(_nodeNameBuffer), "ChildNode");
            }
            if (ImGui::MenuItem("Delete")) {
                auto* parent = node.GetParent();
                if (parent) {
                    if (_selectedNode == &node) {
                        _selectedNode = nullptr;
                    }
                    parent->RemoveChild(&node);
                    _isDirty = true;
                    nodeDeleted = true;
                }
            }
            ImGui::EndPopup();
        }

        if (isOpen) {
            if (!nodeDeleted) {
                for (const auto& child : children) {
                    RenderHierarchyNode(*child.Get());
                }
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

        char nameBuffer[256] = {};
        snprintf(nameBuffer, sizeof(nameBuffer), "%s", _selectedNode->GetName().CStr());
        if (ImGui::InputText("##node_name", nameBuffer, sizeof(nameBuffer))) {
            _selectedNode->SetName(PoolString::Intern(eastl::string_view(nameBuffer)));
            _isDirty = true;
        }

        // Entity ID (read-only)
        ImGui::TextUnformatted("Entity:");
        ImGui::SameLine();
        ImGui::Text("#%u", static_cast<unsigned int>(_selectedNode->GetEntity()));

        ImGui::Separator();
        ImGui::TextUnformatted("Components");

        const auto& components = _selectedNode->GetComponents();
        PoolString componentToRemove;
        for (size_t i = 0; i < components.size(); ++i) {
            const auto& component = components[i];
            const auto& typeMeta = component->GetTypeMeta();

            ImGui::PushID(static_cast<int>(i));

            bool visible = true;
            const bool isOpen = ImGui::CollapsingHeader(typeMeta.typeName.data(), &visible);

            if (isOpen) {
                ImGui::Indent();
                if (RenderComponentInspector(component.Get())) {
                    _isDirty = true;
                }
                ImGui::Unindent();
            }

            ImGui::PopID();

            if (!visible) {
                componentToRemove = PoolString::Intern(typeMeta.typeName);
                break;
            }
        }

        if (!componentToRemove.Empty()) {
            _selectedNode->RemoveComponent(componentToRemove);
            _isDirty = true;
        }

        ImGui::Separator();
        if (ImGui::Button("[+ Add Component]", ImVec2(-1.0f, 0.0f))) {
            _showAddComponentPopup = true;
        }
    }

    void SceneEditorWidget::RenderAddNodePopup() {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Add Node##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Node name:");
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputText("##add_node_name", _nodeNameBuffer, sizeof(_nodeNameBuffer));

            if (ImGui::Button("OK", ImVec2(120.0f, 0.0f))) {
                if (_nodeNameBuffer[0] != '\0') {
                    auto* scene = CoreManager::GetSceneManager().GetActiveScene();
                    if (scene) {
                        SceneNode* target = _contextMenuTargetNode ? _contextMenuTargetNode : &scene->GetRootNode();
                        target->AddChild(PoolString::Intern(eastl::string_view(_nodeNameBuffer)));
                        _isDirty = true;
                    }
                }
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void SceneEditorWidget::RenderAddComponentPopup() {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Add Component##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Select component type:");
            ImGui::Separator();

            const auto& factory = AbstractFactory<IComponent>::GetInstance();
            const auto& registeredTypes = factory.GetRegisteredTypes();

            bool firstItem = true;
            for (const auto& meta : registeredTypes) {
                if (firstItem) {
                    if (ImGui::IsWindowAppearing()) {
                        ImGui::SetKeyboardFocusHere();
                    }
                    firstItem = false;
                }
                if (ImGui::Selectable(meta.typeName.data())) {
                    if (_selectedNode) {
                        auto newComponent = factory.Create(meta);
                        if (newComponent) {
                            _selectedNode->AddComponent(newComponent);
                            _isDirty = true;
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void SceneEditorWidget::SaveScene() {
        auto* scene = CoreManager::GetSceneManager().GetActiveScene();
        if (!scene) {
            return;
        }

        scene->PrepareForSave();

        auto& configManager = CoreManager::GetConfigManager();
        auto xmlRoot = configManager.GetConfig("SceneConfig"_intern);
        if (!xmlRoot) {
            return;
        }

        pugi::xml_node pugiRoot = xmlRoot.GetPugiNode();  // <SceneManager>

        // Rebuild <scenes> from the current runtime state
        pugiRoot.remove_child("scenes");
        pugi::xml_node scenesEl = pugiRoot.append_child("scenes");

        pugi::xml_node sceneEl = scenesEl.append_child("scene");
        sceneEl.append_attribute("type").set_value("Scene");
        sceneEl.append_attribute("name").set_value(scene->GetName().CStr());

        pugi::xml_node nodesEl = sceneEl.append_child("nodes");
        for (const auto& nodePtr : scene->GetRootNode().GetChildren()) {
            BuildNodeXml(nodesEl, *nodePtr.Get());
        }

        if (configManager.SaveConfig("SceneConfig"_intern)) {
            _isDirty = false;
        }
    }

}  // namespace BECore
