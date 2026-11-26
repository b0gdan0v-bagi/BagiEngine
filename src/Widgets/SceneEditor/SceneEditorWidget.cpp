#include "SceneEditorWidget.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Generated/SceneEditorWidget.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/PanelAssets/PanelBars.h>
#include <Widgets/SceneEditor/ComponentInspectors.h>
#include <Widgets/SceneEditor/HierarchyPanel.h>
#include <Widgets/SceneEditor/SceneAssetDrop.h>
#include <Widgets/SceneEditor/SceneConfigWriter.h>
#include <Widgets/SceneEditor/SceneDialogEvents.h>
#include <Widgets/SceneEditor/SceneEditorDocuments.h>
#include <Widgets/SceneEditor/ScenePick.h>
#include <Widgets/SceneEditor/UIEditEvents.h>
#include <Widgets/SceneEditor/UIRectInspector.h>
#include <imgui.h>

namespace BECore {

    namespace {
        constexpr float kUITreeHeight = 160.0f;
        constexpr StringView kToolbarAsset = "SceneEditorToolbarPanel";
        constexpr StringView kInspectorAsset = "SceneInspectorPanel";
        constexpr StringView kComponentsSlot = "sceneComponents";
    }  // namespace

    SceneEditorWidget::~SceneEditorWidget() {
        if (SceneManager* const sceneManager = CoreManager::FindSceneManager()) {
            sceneManager->ClearEditorFlags();
        }
    }

    bool SceneEditorWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorCommandEvent, &SceneEditorWidget::OnCommand>(this);
        Subscribe<SelectSceneNodeRequestEvent, &SceneEditorWidget::OnSelectRequest>(this);
        Subscribe<SelectUIElementRequestEvent, &SceneEditorWidget::OnSelectUIElementRequest>(this);
        Subscribe<ViewportEditClickEvent, &SceneEditorWidget::OnViewportClick>(this);
        Subscribe<ViewportBoxSelectEvent, &SceneEditorWidget::OnViewportBoxSelect>(this);
        Subscribe<ViewportAssetDropEvent, &SceneEditorWidget::OnViewportAssetDrop>(this);
        Subscribe<NodeTransformEditEvent, &SceneEditorWidget::OnTransformEdit>(this);
        Subscribe<SceneEditedEvent, &SceneEditorWidget::OnSceneEdited>(this);
        Subscribe<SceneNodeAddedEvent, &SceneEditorWidget::OnNodeAdded>(this);
        Subscribe<UIRectDragEvent, &SceneEditorWidget::OnUIRectDrag>(this);
        _documents = CoreManager::GetEditorDocuments().Add([this](Vector<EditorDocumentState>& out) { AppendDocuments(out); });
        _hostHandle = SceneEditorHostRegistry::GetInstance().Register(*this);
        _componentsSlot = PanelSlots::GetInstance().Register(kComponentsSlot, [this] { DrawComponents(); });
        return true;
    }

    SceneNode* SceneEditorWidget::FindInspected() const {
        SceneManager* const sceneManager = CoreManager::FindSceneManager();
        Scene* const scene = sceneManager != nullptr ? sceneManager->GetActiveScene() : nullptr;
        if (scene == nullptr || scene != _lastScene || _hierarchy.selected == nullptr) {
            return nullptr;
        }
        return HierarchyModel::FindNode(scene->GetRootNode(), _hierarchy.selected);
    }

    SceneInspectedNode SceneEditorWidget::GetInspected() const {
        SceneInspectedNode inspected;
        SceneManager* const sceneManager = CoreManager::FindSceneManager();
        inspected.hasScene = sceneManager != nullptr && sceneManager->GetActiveScene() != nullptr;
        inspected.dirty = _isDirty;
        if (const SceneNode* const node = FindInspected()) {
            inspected.hasSelection = true;
            inspected.selectionCount = _selection.Count();
            inspected.locked = node->HasEditorFlag(EditorFlags::Locked);
            inspected.name = node->GetName();
            inspected.layer = node->GetLayer();
            inspected.sourceRef = node->GetSourceRef();
        }
        return inspected;
    }

    void SceneEditorWidget::RenameInspected(StringView name) {
        SceneNode* const node = FindInspected();
        if (node == nullptr || node->HasEditorFlag(EditorFlags::Locked)) {
            return;
        }
        node->SetName(PoolString::Intern(name));
        _isDirty = true;
    }

    void SceneEditorWidget::SetInspectedLayer(int32_t layer) {
        SceneNode* const node = FindInspected();
        if (node == nullptr || node->HasEditorFlag(EditorFlags::Locked)) {
            return;
        }
        node->SetLayer(layer);
        _isDirty = true;
    }

    void SceneEditorWidget::AppendDocuments(Vector<EditorDocumentState>& out) {
        if (SceneManager* const sceneManager = CoreManager::FindSceneManager()) {
            if (Scene* const scene = sceneManager->GetActiveScene()) {
                AppendSceneDocuments(out, CoreManager::GetConfigManager(), *scene, _isDirty, _uiFileSession);
            }
        }
    }

    void SceneEditorWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewSceneEditor) {
            _open = !_open;
        }
        // The same rule as the Save button: nothing to save while there are no unsaved changes
        if (event.commandId == EditorCommandIds::kSceneSave && _isDirty) {
            SaveScene();
        }
        if (event.commandId == EditorCommandIds::kSceneEditorAddComponent) {
            SceneNode* const node = FindInspected();
            if (node != nullptr && !node->HasEditorFlag(EditorFlags::Locked)) {
                SceneAddComponentRequestEvent::Emit(_lastScene, node);
            }
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
        SelectUIElement(*hit.node, hit.element);
    }

    void SceneEditorWidget::OnSelectUIElementRequest(const SelectUIElementRequestEvent& event) {
        Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
        if (!scene || !HierarchyPanel::Reveal(*scene, _hierarchy, event.scene, event.node)) {
            return;
        }
        _focusRequested = true;
        SyncSelection(*scene);
        // Reveal only validated the pointer; the writable node is the one the selection now holds
        if (SceneNode* const node = _selection.Primary(); node == event.node) {
            SelectUIElement(*node, event.element);
        }
    }

    void SceneEditorWidget::SelectUIElement(SceneNode& node, const UIElement* element) {
        // The node is the primary now: show the element in the "UI elements" section (the inspector keeps a UI selection of the selected node only)
        if (!element || &node != _selection.Primary()) {
            return;
        }
        if (const IntrusivePtr<UICanvasComponent> canvas = node.GetComponent<UICanvasComponent>()) {
            if (UIElement* const found = ScenePick::FindElement(*canvas, element)) {
                ResetUISelection();
                _uiSelectionNode = &node;
                _selectedUIElement = IntrusivePtr<UIElement>(found);
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

    void SceneEditorWidget::OnViewportAssetDrop(const ViewportAssetDropEvent& event) {
        Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
        if (!scene) {
            return;
        }
        SceneAssetDrop drop{event.kind, event.path, Vec2{scene->ScreenToSceneX(event.x), scene->ScreenToSceneY(event.y)}, Vec2{}};
        if (event.kind == AssetKind::Texture) {
            const ResourceHandle<ITexture> texture = CoreManager::GetResourceManager().Load<ITexture>(event.path.ToStringView());
            if (texture && texture->GetState() == ResourceState::Loaded) {
                drop.textureSize = Vec2{static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight())};
            }
        }
        const NodeFileLoader loader = MakeFileSystemNodeLoader(CoreManager::GetFileSystem());
        const std::expected<SceneNode*, String> added = AddSceneNodeOfAsset(*scene, drop, loader);
        if (!added) {
            LOG_ERROR("SceneEditor: nothing dropped into scene '{}': {}", scene->GetName().CStr(), added.error());
            return;
        }
        _isDirty = true;
        if (HierarchyPanel::Reveal(*scene, _hierarchy, scene, *added)) {
            _focusRequested = true;
            SyncSelection(*scene);
        }
    }

    void SceneEditorWidget::OnTransformEdit(const NodeTransformEditEvent& event) {
        if (event.phase == NodeTransformEditPhase::Commit && !event.edits.empty()) {
            _isDirty = true;
        }
    }

    void SceneEditorWidget::OnSceneEdited(const SceneEditedEvent& event) {
        if (event.scene != nullptr && event.scene == CoreManager::GetSceneManager().GetActiveScene()) {
            _isDirty = true;
        }
    }

    void SceneEditorWidget::OnNodeAdded(const SceneNodeAddedEvent& event) {
        if (event.scene != nullptr && event.scene == CoreManager::GetSceneManager().GetActiveScene()) {
            _isDirty = true;
            _hierarchy.revealNode = event.parent;
        }
    }

    void SceneEditorWidget::OnUIRectDrag(const UIRectDragEvent& event) {
        const bool selected = event.element != nullptr && event.element == _selectedUIElement.Get();
        switch (event.phase) {
            case UIRectDragPhase::Begin:
                break;
            case UIRectDragPhase::Change:
                UIRectEditEvent::Emit(event.scene, event.node, event.element, event.before, event.after);
                if (selected) {
                    _inexactUIElement = event.exact ? IntrusivePtr<UIElement>() : _selectedUIElement;
                }
                break;
            case UIRectDragPhase::Commit: {
                _isDirty = true;
                // The node is alive in the active scene for a Commit (the session ends a drag that lost it); the element is found in its canvas by pointer, never dereferenced before
                Scene* const scene = CoreManager::GetSceneManager().GetActiveScene();
                SceneNode* const node = scene && scene == event.scene ? HierarchyModel::FindNode(scene->GetRootNode(), event.node) : nullptr;
                const IntrusivePtr<UICanvasComponent> canvas = node ? node->GetComponent<UICanvasComponent>() : IntrusivePtr<UICanvasComponent>();
                if (UIElement* const element = canvas ? ScenePick::FindElement(*canvas, event.element) : nullptr) {
                    _uiFileSession.Note(IntrusivePtr<UIElement>(element));
                }
                break;
            }
            case UIRectDragPhase::Cancel:
                if (selected) {
                    _inexactUIElement = nullptr;
                }
                break;
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
        const Span<SceneNode* const> nodes = _selection.Nodes();
        if (scene != _announcedNodesScene || nodes.size() != _announcedNodes.size() || !eastl::equal(nodes.begin(), nodes.end(), _announcedNodes.begin())) {
            _announcedNodesScene = scene;
            _announcedNodes.assign(nodes.begin(), nodes.end());
            SceneSelectionChangedEvent::Emit(scene, nodes);
        }
    }

    void SceneEditorWidget::AnnounceUIElement(const Scene* scene) {
        const SceneNode* const node = _hierarchy.selected;
        // The inspector drops a UI selection that belongs to another node only when it draws, so the pair is checked here as well
        const UIElement* const element = node != nullptr && node == _uiSelectionNode ? _selectedUIElement.Get() : nullptr;
        if (scene == _announcedUIScene && node == _announcedUINode && element == _announcedUIElement) {
            return;
        }
        _announcedUIScene = scene;
        _announcedUINode = node;
        _announcedUIElement = element;
        UIElementSelectedEvent::Emit(scene, node, element);
    }

    void SceneEditorWidget::Update() {
        // A reveal (a pick in the viewport, a request) opens a closed panel too
        if (_focusRequested) {
            _open = true;
        }
        if (!_open) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(1000.0f, 600.0f), ImGuiCond_FirstUseEver);
        if (_focusRequested) {
            ImGui::SetNextWindowFocus();
            _focusRequested = false;
        }
        if (!ImGui::Begin("Scene Editor", &_open)) {
            ImGui::End();
            return;
        }

        auto& sceneManager = CoreManager::GetSceneManager();
        auto* scene = sceneManager.GetActiveScene();

        if (scene != _lastScene) {
            // A replaced scene frees its nodes: the selection would dangle
            _hierarchy.Reset();
            _selection.Clear();
            ResetUISelection();
            _uiFileSession.Clear();
            _lastScene = scene;
        }

        if (!scene) {
            AnnounceSelection(nullptr);
            AnnounceUIElement(nullptr);
            EmptyState(ICON_LC_LAYERS, "No active scene");
            ImGui::End();
            return;
        }

        PanelBarsRegistry::Draw(kToolbarAsset);

        const float leftPanelWidth = 300.0f;
        ImGui::BeginChild("##hierarchy", ImVec2(leftPanelWidth, 0.0f), true);

        HierarchyPanel::Draw(*scene, _hierarchy, _selection);

        ImGui::EndChild();
        SyncSelection(*scene);
        ApplyHierarchyAnswers(*scene);
        AnnounceSelection(scene);
        AnnounceUIElement(scene);

        ImGui::SameLine();
        ImGui::BeginChild("##inspector", ImVec2(0.0f, 0.0f), true);

        if (_hierarchy.selected != nullptr && _uiSelectionNode != _hierarchy.selected) {
            ResetUISelection();
            _uiSelectionNode = _hierarchy.selected;
        }
        if (!PanelBarsRegistry::Draw(kInspectorAsset)) {
            DrawComponents();
        }

        ImGui::EndChild();

        ImGui::End();
    }

    void SceneEditorWidget::Draw() {
        // All rendering is done in Update
    }

    void SceneEditorWidget::ApplyHierarchyAnswers(Scene& scene) {
        if (_hierarchy.dirty) {
            _isDirty = true;
            _hierarchy.dirty = false;
        }
        if (_hierarchy.nodeRemoved) {
            // A removed subtree may have held the canvas element in the inspector (the dialogs find their node again every frame)
            ResetUISelection();
            _hierarchy.nodeRemoved = false;
        }
        if (_hierarchy.addNodeRequested) {
            _hierarchy.addNodeRequested = false;
            SceneNode* const parent = _hierarchy.addNodeParent;
            _hierarchy.addNodeParent = nullptr;
            SceneAddNodeRequestEvent::Emit(&scene, parent);
        }
    }

    void SceneEditorWidget::DrawUIFileStatus() {
        const SceneNode* const node = _hierarchy.selected;
        if (!node || !_lastScene) {
            return;
        }
        const UINodeFileSession::Status status = _uiFileSession.StatusFor(*_lastScene, *node);
        switch (status.target) {
            case UINodeFileTarget::Inline:
                ImGui::TextDisabled("Edits the authored rect, live; saved in the scene file");
                break;
            case UINodeFileTarget::Shared:
                ImGui::TextDisabled("%s has %zu instances in the scene: edits are saved as an override in the scene file", status.ref.CStr(), status.instances);
                break;
            case UINodeFileTarget::File:
                if (status.elements > 0) {
                    ImGui::TextDisabled("%zu element(s) will be written into %s on Save", status.elements, status.ref.CStr());
                } else {
                    ImGui::TextDisabled("Edits of the rect are written into %s on Save", status.ref.CStr());
                }
                break;
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
            DrawUIFileStatus();
            ImGui::BeginChild("##ui_tree", ImVec2(0.0f, kUITreeHeight), true);
            DrawUIElementTree(canvas, _selectedUIElement);
            ImGui::EndChild();

            if (_selectedUIElement) {
                if (ImGui::CollapsingHeader("Rect", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const UIRectSpec before = _selectedUIElement->style.rect;
                    changed = InspectUIRect(canvas, *_selectedUIElement, _inexactUIElement);
                    if (changed) {
                        _uiFileSession.Note(_selectedUIElement);
                        UIRectEditEvent::Emit(_lastScene, _hierarchy.selected, _selectedUIElement.Get(), before, _selectedUIElement->style.rect);
                    }
                }
            } else {
                EmptyState(ICON_LC_MOUSE_POINTER_CLICK, "Select an element");
            }
            ImGui::TreePop();
        }
        return changed;
    }

    void SceneEditorWidget::DrawComponents() {
        if (_hierarchy.selected == nullptr) {
            return;
        }
        // A locked node is read-only: the whole list is disabled
        ImGui::BeginDisabled(_hierarchy.selected->HasEditorFlag(EditorFlags::Locked));

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

        ImGui::EndDisabled();
    }

    void SceneEditorWidget::SaveScene() {
        auto* scene = CoreManager::GetSceneManager().GetActiveScene();
        if (!scene) {
            return;
        }

        // The node files first: an instance whose rects are in its file then has nothing to override in the scene file. A failure here is logged and never stops the scene
        _uiFileSession.Save(*scene);

        auto& configManager = CoreManager::GetConfigManager();
        auto xmlRoot = configManager.GetConfig(ConfigNames::scene);
        if (!xmlRoot) {
            return;
        }

        // An instance of a node file is saved as its <node ref> with the overrides; one that cannot be goes inline and is told here
        const NodeFileLoader loader = MakeFileSystemNodeLoader(CoreManager::GetFileSystem());
        Vector<String> notes;
        if (!WriteSceneToConfig(xmlRoot.GetPugiNode(), *scene, &loader, &notes)) {  // <SceneManager>
            return;
        }
        for (const String& note : notes) {
            LOG_WARNING("SceneEditor: scene '{}': {}", scene->GetName().CStr(), note);
        }

        if (configManager.SaveConfig(ConfigNames::scene)) {
            _isDirty = false;
        }
    }

}  // namespace BECore
