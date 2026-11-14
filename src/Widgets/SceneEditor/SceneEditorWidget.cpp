#include "SceneEditorWidget.h"

#include "ComponentInspectors.h"
#include "HierarchyPanel.h"
#include "SceneConfigWriter.h"
#include "ScenePick.h"
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
        Subscribe<ViewportEditClickEvent, &SceneEditorWidget::OnViewportClick>(this);
        Subscribe<ViewportBoxSelectEvent, &SceneEditorWidget::OnViewportBoxSelect>(this);
        Subscribe<NodeTransformEditEvent, &SceneEditorWidget::OnTransformEdit>(this);
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
            if (HierarchyPanel::Reveal(*scene, _hierarchy, event.scene, event.node)) {
                _focusRequested = true;
                SyncSelection(*scene);
            }
        }
    }

    void SceneEditorWidget::OnViewportClick(const ViewportEditClickEvent& event) {
        constexpr uint8_t kLeftButton = 0;
        Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
        if (event.button != kLeftButton || !scene) {
            return;
        }
        const ScenePick::Hit hit = ScenePick::PickAt(*scene, event.x, event.y);
        ApplyClick(_selection, hit.node, event.modifiers);
        _hierarchy.selected = _selection.Primary();
        if (!hit.node || !_selection.Contains(hit.node)) {
            return;  // a click on nothing, or a Ctrl+click that took the node out of the selection
        }
        if (!HierarchyPanel::Reveal(*scene, _hierarchy, scene, hit.node)) {
            return;
        }
        _hierarchy.selected = _selection.Primary();
        _focusRequested = true;
        if (!hit.element || hit.node != _selection.Primary()) {
            return;
        }
        // The node is the primary now: show the element in the "UI elements" section (RenderInspector keeps a UI selection of the selected node)
        if (const IntrusivePtr<UICanvasComponent> canvas = hit.node->GetComponent<UICanvasComponent>()) {
            if (UIElement* const element = ScenePick::FindElement(*canvas, hit.element)) {
                ResetUISelection();
                _uiSelectionNode = hit.node;
                _selectedUIElement = IntrusivePtr<UIElement>(element);
            }
        }
    }

    void SceneEditorWidget::OnViewportBoxSelect(const ViewportBoxSelectEvent& event) {
        Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
        if (!scene) {
            return;
        }
        ScenePick::PickInRect(*scene, event.x0, event.y0, event.x1, event.y1, _boxHits);
        if (event.modifiers == 0) {
            _selection.Clear();
        }
        for (SceneNode* const node : _boxHits) {
            _selection.Add(node);
        }
        _hierarchy.selected = _selection.Primary();
        if (_hierarchy.selected && HierarchyPanel::Reveal(*scene, _hierarchy, scene, _hierarchy.selected)) {
            _focusRequested = true;
        }
    }

    void SceneEditorWidget::OnTransformEdit(const NodeTransformEditEvent& event) {
        if (event.phase == NodeTransformEditPhase::Commit && !event.edits.empty()) {
            _isDirty = true;
        }
    }

    void SceneEditorWidget::SyncSelection(Scene& scene) {
        if (_hierarchy.clickedNode != nullptr) {
            ApplyClick(_selection, _hierarchy.clickedNode, _hierarchy.clickModifiers);
            _hierarchy.clickedNode = nullptr;
            _hierarchy.clickModifiers = 0;
            _selection.Prune(scene);
        } else {
            _selection.Prune(scene);
            SceneNode* const selected = _hierarchy.selected;
            if (selected != nullptr && selected != _selection.Primary() && HierarchyModel::FindNode(scene.GetRootNode(), selected) != nullptr) {
                _selection.Set(selected);
            }
        }
        _hierarchy.selected = _selection.Primary();
    }

    void SceneEditorWidget::AnnounceSelection(const Scene* scene) {
        if (_announcedSelection.Changed(scene, _hierarchy.selected)) {
            SceneNodeSelectedEvent::Emit(scene, _hierarchy.selected);
        }
        const eastl::span<SceneNode* const> nodes = _selection.Nodes();
        if (scene != _announcedNodesScene || nodes.size() != _announcedNodes.size() || !eastl::equal(nodes.begin(), nodes.end(), _announcedNodes.begin())) {
            _announcedNodesScene = scene;
            _announcedNodes.assign(nodes.begin(), nodes.end());
            SceneSelectionChangedEvent::Emit(scene, nodes);
        }
    }

    void SceneEditorWidget::Update() {
        ImGui::SetNextWindowSize(ImVec2(1000.0f, 600.0f), ImGuiCond_FirstUseEver);
        if (_focusRequested) {
            ImGui::SetNextWindowFocus();
            _focusRequested = false;
        }
        if (!ImGui::Begin("Scene Editor")) {
            ImGui::End();
            return;
        }

        auto& sceneManager = CoreManager::GetSceneManager();
        auto* scene = sceneManager.GetActiveScene();

        if (scene != _lastScene) {
            // A replaced scene frees its nodes: the selection would dangle
            _hierarchy.Reset();
            _selection.Clear();
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

        HierarchyPanel::Draw(*scene, _hierarchy, _selection);

        ImGui::EndChild();
        SyncSelection(*scene);
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
        if (_selection.Count() > 1) {
            ImGui::TextDisabled("%zu nodes selected; showing the last one", _selection.Count());
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
            _hierarchy.selected->SetName(PoolString::Intern(StringView(nameBuffer)));
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
                        target->AddChild(PoolString::Intern(StringView(_nodeNameBuffer)));
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
        eastl::vector<String> notes;
        if (!WriteSceneToConfig(xmlRoot.GetPugiNode(), *scene, &loader, &notes)) {  // <SceneManager>
            return;
        }
        for (const String& note : notes) {
            LOG_WARNING(Format("SceneEditor: scene '{}': {}", scene->GetName().CStr(), note.c_str()).c_str());
        }

        if (configManager.SaveConfig("SceneConfig"_intern)) {
            _isDirty = false;
        }
    }

}  // namespace BECore
