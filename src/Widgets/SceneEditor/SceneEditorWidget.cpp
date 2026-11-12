#include "SceneEditorWidget.h"

#include "ComponentInspectors.h"
#include "HierarchyPanel.h"
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
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/EditorLayout/IconTable.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {
        constexpr float kUITreeHeight = 160.0f;
        constexpr size_t kComponentLabelSize = 96;
        constexpr float kPopupButtonWidth = 120.0f;
    }  // namespace

    // =========================================================================
    // IWidget interface
    // =========================================================================

    SceneEditorWidget::~SceneEditorWidget() {
        if (SceneManager* const sceneManager = CoreManager::FindSceneManager()) {
            sceneManager->ClearEditorFlags();
        }
    }

    bool SceneEditorWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorCommandEvent, &SceneEditorWidget::OnCommand>(this);
        Subscribe<SelectSceneNodeRequestEvent, &SceneEditorWidget::OnSelectRequest>(this);
        return true;
    }

    void SceneEditorWidget::OnCommand(const EditorCommandEvent& event) {
        // The same rule as the Save button: nothing to save while there are no unsaved changes
        if (event.commandId == EditorCommandIds::kSceneSave && _isDirty) {
            SaveScene();
        }
    }

    void SceneEditorWidget::OnSelectRequest(const SelectSceneNodeRequestEvent& event) {
        if (Scene* scene = CoreManager::GetSceneManager().GetActiveScene()) {
            HierarchyPanel::Reveal(*scene, _hierarchy, event.scene, event.node);
        }
    }

    void SceneEditorWidget::AnnounceSelection(const Scene* scene) {
        if (_announcedSelection.Changed(scene, _hierarchy.selected)) {
            SceneNodeSelectedEvent::Emit(scene, _hierarchy.selected);
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
            _hierarchy.Reset();
            _contextMenuTargetNode = nullptr;
            ResetUISelection();
            _lastScene = scene;
        }

        if (!scene) {
            AnnounceSelection(nullptr);
            EmptyState(ICON_LC_LAYERS, "No active scene");
            ImGui::End();
            return;
        }

        // ----------------------------------------------------------------
        // Toolbar
        // ----------------------------------------------------------------
        ImGui::BeginDisabled(!_isDirty);
        if (IconTextButton(ICON_LC_SAVE, "Save", "Write the active scene back to its config file")) {
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

        HierarchyPanel::Draw(*scene, _hierarchy);

        ImGui::EndChild();
        ApplyHierarchyAnswers();
        AnnounceSelection(scene);

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

    void SceneEditorWidget::ApplyHierarchyAnswers() {
        if (_hierarchy.dirty) {
            _isDirty = true;
            _hierarchy.dirty = false;
        }
        if (_hierarchy.nodeRemoved) {
            // A removed subtree may have held the node of the add popup or the canvas element in the inspector
            _contextMenuTargetNode = nullptr;
            ResetUISelection();
            _hierarchy.nodeRemoved = false;
        }
        if (_hierarchy.addNodeRequested) {
            _hierarchy.addNodeRequested = false;
            _contextMenuTargetNode = _hierarchy.addNodeParent;
            _hierarchy.addNodeParent = nullptr;
            _showAddNodePopup = true;
            snprintf(_nodeNameBuffer, sizeof(_nodeNameBuffer), "%s", _contextMenuTargetNode ? "ChildNode" : "NewNode");
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
                EmptyState(ICON_LC_MOUSE_POINTER_CLICK, "Select an element");
            }
            ImGui::TreePop();
        }
        return changed;
    }

    void SceneEditorWidget::RenderInspector() {
        if (!_hierarchy.selected) {
            EmptyState(ICON_LC_MOUSE_POINTER_CLICK, "Select a node to inspect");
            return;
        }

        if (_uiSelectionNode != _hierarchy.selected) {
            ResetUISelection();
            _uiSelectionNode = _hierarchy.selected;
        }

        // A locked node is read-only: the whole body below is disabled
        const bool locked = _hierarchy.selected->HasEditorFlag(EditorFlags::Locked);
        if (locked) {
            ImGui::TextDisabled(ICON_LC_LOCK " Locked in the editor");
        }
        ImGui::BeginDisabled(locked);

        // Node name editor
        ImGui::TextUnformatted("Name:");
        ImGui::SameLine();

        char nameBuffer[256] = {};
        snprintf(nameBuffer, sizeof(nameBuffer), "%s", _hierarchy.selected->GetName().CStr());
        if (ImGui::InputText("##node_name", nameBuffer, sizeof(nameBuffer))) {
            _hierarchy.selected->SetName(PoolString::Intern(eastl::string_view(nameBuffer)));
            _isDirty = true;
        }

        // Draw layer: a lower layer is drawn first, equal layers in scene order
        ImGui::TextUnformatted("Layer:");
        ImGui::SameLine();

        int32_t layer = _hierarchy.selected->GetLayer();
        bool layerChanged = false;
        {
            ScopedMonoFont mono;
            layerChanged = ImGui::InputInt("##node_layer", &layer);
        }
        if (layerChanged) {
            _hierarchy.selected->SetLayer(layer);
            _isDirty = true;
        }

        if (!_hierarchy.selected->GetSourceRef().Empty()) {
            ImGui::TextDisabled("Instance of %s", _hierarchy.selected->GetSourceRef().CStr());
        }

        // Entity ID (read-only)
        ImGui::TextUnformatted("Entity:");
        ImGui::SameLine();
        ImGui::Text("#%u", static_cast<unsigned int>(_hierarchy.selected->GetEntity()));

        ImGui::Separator();
        ImGui::TextUnformatted("Components");

        const auto& components = _hierarchy.selected->GetComponents();
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
            _hierarchy.selected->RemoveComponent(componentToRemove);
            _isDirty = true;
        }

        ImGui::Separator();
        if (IconTextButton(ICON_LC_CIRCLE_PLUS, "Add Component", "Add a component to the selected node", -1.0f)) {
            _showAddComponentPopup = true;
        }
        ImGui::EndDisabled();
    }

    void SceneEditorWidget::RenderAddNodePopup() {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Add Node##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Node name:");
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputText("##add_node_name", _nodeNameBuffer, sizeof(_nodeNameBuffer));

            if (IconTextButton(ICON_LC_CHECK, "OK", nullptr, kPopupButtonWidth)) {
                if (_nodeNameBuffer[0] != '\0') {
                    auto* scene = CoreManager::GetSceneManager().GetActiveScene();
                    if (scene) {
                        SceneNode* target = _contextMenuTargetNode ? _contextMenuTargetNode : &scene->GetRootNode();
                        target->AddChild(PoolString::Intern(eastl::string_view(_nodeNameBuffer)));
                        _hierarchy.revealNode = target;
                        _isDirty = true;
                    }
                }
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (IconTextButton(ICON_LC_X, "Cancel", nullptr, kPopupButtonWidth)) {
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
                // A stack buffer: the label is "icon name", and the popup draws it for every registered class
                char label[kComponentLabelSize];
                snprintf(label, sizeof(label), "%s %s", IconOfClass(meta), meta.typeName.data());
                if (ImGui::Selectable(label)) {
                    if (_hierarchy.selected) {
                        auto newComponent = factory.Create(meta);
                        if (newComponent) {
                            _hierarchy.selected->AddComponent(newComponent);
                            _isDirty = true;
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::Separator();
            if (IconTextButton(ICON_LC_X, "Cancel", nullptr, kPopupButtonWidth)) {
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
