#include "SceneEditorWidget.h"

#include "ComponentInspectors.h"
#include "SceneConfigWriter.h"
#include "UIRectInspector.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Generated/SceneEditorWidget.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {
        constexpr float kUITreeHeight = 160.0f;
    }

    // =========================================================================
    // IWidget interface
    // =========================================================================

    bool SceneEditorWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorCommandEvent, &SceneEditorWidget::OnCommand>(this);
        return true;
    }

    void SceneEditorWidget::OnCommand(const EditorCommandEvent& event) {
        // The same rule as the Save button: nothing to save while there are no unsaved changes
        if (event.commandId == EditorCommandIds::kSceneSave && _isDirty) {
            SaveScene();
        }
    }

    void SceneEditorWidget::Update() {
        ImGui::SetNextWindowSize(ImVec2(1000.0f, 600.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Scene Editor")) {
            ImGui::End();
            return;
        }

        auto& sceneManager = CoreManager::GetSceneManager();
        auto* scene = sceneManager.GetActiveScene();

        if (scene != _lastScene) {
            // A replaced scene frees its nodes: the selection would dangle
            _selectedNode = nullptr;
            _contextMenuTargetNode = nullptr;
            ResetUISelection();
            _lastScene = scene;
        }

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
            ImGui::TextColored(GetImGuiPalette().warning, "(unsaved changes)");
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
                        ResetUISelection();
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

    void SceneEditorWidget::ResetUISelection() {
        _selectedUIElement = nullptr;
        _inexactUIElement = nullptr;
        _uiSelectionNode = nullptr;
    }

    bool SceneEditorWidget::RenderUIElements(UICanvasComponent& canvas) {
        bool changed = false;
        if (ImGui::TreeNodeEx("UI elements", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Edits the authored style, live; saved in the scene file (an instance: as an override), not in the node file");
            ImGui::BeginChild("##ui_tree", ImVec2(0.0f, kUITreeHeight), true);
            DrawUIElementTree(canvas, _selectedUIElement);
            ImGui::EndChild();

            if (_selectedUIElement) {
                if (ImGui::CollapsingHeader("Rect", ImGuiTreeNodeFlags_DefaultOpen)) {
                    changed = InspectUIRect(canvas, *_selectedUIElement, _inexactUIElement);
                }
            } else {
                ImGui::TextDisabled("Select an element");
            }
            ImGui::TreePop();
        }
        return changed;
    }

    void SceneEditorWidget::RenderInspector() {
        if (!_selectedNode) {
            ImGui::TextDisabled("Select a node to inspect");
            return;
        }

        if (_uiSelectionNode != _selectedNode) {
            ResetUISelection();
            _uiSelectionNode = _selectedNode;
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

        // Draw layer: a lower layer is drawn first, equal layers in scene order
        ImGui::TextUnformatted("Layer:");
        ImGui::SameLine();

        int32_t layer = _selectedNode->GetLayer();
        if (ImGui::InputInt("##node_layer", &layer)) {
            _selectedNode->SetLayer(layer);
            _isDirty = true;
        }

        if (!_selectedNode->GetSourceRef().Empty()) {
            ImGui::TextDisabled("Instance of %s", _selectedNode->GetSourceRef().CStr());
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
                if (typeMeta.typeHash == UICanvasComponent::GetStaticTypeMeta().typeHash && RenderUIElements(static_cast<UICanvasComponent&>(*component.Get()))) {
                    _isDirty = true;
                }
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

        auto& configManager = CoreManager::GetConfigManager();
        auto xmlRoot = configManager.GetConfig("SceneConfig"_intern);
        if (!xmlRoot) {
            return;
        }

        // An instance of a node file is saved as its <node ref> with the overrides; one that cannot be goes inline and is told here
        const NodeFileLoader loader = MakeFileSystemNodeLoader(CoreManager::GetFileSystem());
        eastl::vector<eastl::string> notes;
        if (!WriteSceneToConfig(xmlRoot.GetPugiNode(), *scene, &loader, &notes)) {  // <SceneManager>
            return;
        }
        for (const eastl::string& note : notes) {
            LOG_WARNING(Format("SceneEditor: scene '{}': {}", scene->GetName().CStr(), note.c_str()).c_str());
        }

        if (configManager.SaveConfig("SceneConfig"_intern)) {
            _isDirty = false;
        }
    }

}  // namespace BECore
