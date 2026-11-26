#include "SceneEditorWidget.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneConfigWriter.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/ScenePick.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Events/SceneEvents.h>
#include <Generated/SceneEditorWidget.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/PanelAssets/PanelBars.h>
#include <Widgets/RemoteEditor/RemoteSessionRegistry.h>
#include <Widgets/SceneEditor/ComponentInspectors.h>
#include <Widgets/SceneEditor/ComponentListView.h>
#include <Widgets/SceneEditor/HierarchyPanel.h>
#include <Widgets/SceneEditor/SceneDialogEvents.h>
#include <Widgets/SceneEditor/SceneEditorDocuments.h>
#include <Widgets/SceneEditor/UIEditEvents.h>
#include <Widgets/SceneEditor/UIRectInspector.h>
#include <imgui.h>

namespace BECore {

    namespace {
        constexpr float kUITreeHeight = 160.0f;
        constexpr StringView kToolbarAsset = "SceneEditorToolbarPanel";
        constexpr StringView kInspectorAsset = "SceneInspectorPanel";
        constexpr StringView kComponentsSlot = "sceneComponents";

        /// The session while the editor shows an attached program instead of the local scene, else null
        RemoteSession* FindRemote() {
            return RemoteSessionRegistry::GetInstance().GetRemote();
        }

        class LocalComponentList final : public IComponentListSource {
        public:
            LocalComponentList(SceneNode& node, FunctionRef<void(IComponent&)> drawFields) : _node(node), _drawFields(drawFields) {}

            [[nodiscard]] size_t GetCount() const override {
                return _node.GetComponents().size();
            }

            [[nodiscard]] const char* GetTypeName(size_t index) const override {
                return _node.GetComponents()[index]->GetTypeMeta().typeName.data();
            }

            /// A locked node is read-only
            [[nodiscard]] bool IsReadOnly() const override {
                return _node.HasEditorFlag(EditorFlags::Locked);
            }

            [[nodiscard]] bool IsRemovable() const override {
                return true;
            }

            void DrawFields(size_t index) override {
                _drawFields(*_node.GetComponents()[index]);
            }

        private:
            SceneNode& _node;
            FunctionRef<void(IComponent&)> _drawFields;
        };
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
        Subscribe<NodeTransformEditEvent, &SceneEditorWidget::OnTransformEdit>(this);
        Subscribe<SceneEditedEvent, &SceneEditorWidget::OnSceneEdited>(this);
        Subscribe<SceneEvents::SceneNodeEditedEvent, &SceneEditorWidget::OnNodeEdited>(this);
        Subscribe<SceneEvents::SceneSavedEvent, &SceneEditorWidget::OnSceneSaved>(this);
        Subscribe<SceneNodeAddedEvent, &SceneEditorWidget::OnNodeAdded>(this);
        Subscribe<UIRectDragEvent, &SceneEditorWidget::OnUIRectDrag>(this);
        _documents = CoreManager::GetEditorDocuments().Add([this](Vector<EditorDocumentState>& out) { AppendDocuments(out); });
        _hostHandle = SceneEditorHostRegistry::GetInstance().Register(*this);
        _componentsSlot = PanelSlots::GetInstance().Register(kComponentsSlot, [this] { DrawComponents(); });
        return true;
    }

    SceneNode* SceneEditorWidget::FindInspected() const {
        if (FindRemote() != nullptr) {
            return nullptr;
        }
        SceneManager* const sceneManager = CoreManager::FindSceneManager();
        Scene* const scene = sceneManager != nullptr ? sceneManager->GetActiveScene() : nullptr;
        if (scene == nullptr || scene != _lastScene || _hierarchy.selected == nullptr) {
            return nullptr;
        }
        return HierarchyModel::FindNode(scene->GetRootNode(), _hierarchy.selected);
    }

    SceneInspectedNode SceneEditorWidget::GetInspected() const {
        if (RemoteSession* const remote = FindRemote()) {
            return RemoteSceneView::Inspect(*remote);
        }
        SceneInspectedNode inspected;
        SceneManager* const sceneManager = CoreManager::FindSceneManager();
        inspected.hasScene = sceneManager != nullptr && sceneManager->GetActiveScene() != nullptr;
        inspected.dirty = _isDirty;
        if (const SceneNode* const node = FindInspected()) {
            inspected.hasSelection = true;
            inspected.selectionCount = _selection.Count();
            inspected.locked = node->HasEditorFlag(EditorFlags::Locked);
            inspected.name = node->GetName().ToStringView();
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
        if (RemoteSession* const remote = FindRemote()) {
            RemoteSceneView::SetLayer(*remote, layer);
            return;
        }
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
                AppendSceneDocuments(out, Configs(), *scene, _isDirty, _uiFileSession);
            }
        }
    }

    void SceneEditorWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewSceneEditor) {
            _open = !_open;
        }
        // The same rule as the Save button: nothing to save while there are no unsaved changes
        if (event.commandId == EditorCommandIds::kSceneSave) {
            if (RemoteSession* const remote = FindRemote()) {
                RemoteSceneView::Save(*remote);
            } else if (_isDirty) {
                SaveScene();
            }
        }
        if (event.commandId == EditorCommandIds::kSceneEditorAddComponent) {
            SceneNode* const node = FindInspected();
            if (node != nullptr && !node->HasEditorFlag(EditorFlags::Locked)) {
                SceneAddComponentRequestEvent::Emit(_lastScene, node);
            }
        }
    }

    void SceneEditorWidget::OnSelectRequest(const SelectSceneNodeRequestEvent& event) {
        if (FindRemote() != nullptr) {
            return;
        }
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
        if (event.button != kLeftButton || !scene || FindRemote() != nullptr) {
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
        if (!scene || FindRemote() != nullptr || !HierarchyPanel::Reveal(*scene, _hierarchy, event.scene, event.node)) {
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
        if (!scene || FindRemote() != nullptr) {
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
        if (event.phase == NodeTransformEditPhase::Commit && !event.edits.empty() && FindRemote() == nullptr) {
            _isDirty = true;
        }
    }

    void SceneEditorWidget::OnSceneEdited(const SceneEditedEvent& event) {
        if (event.scene != nullptr && event.scene == CoreManager::GetSceneManager().GetActiveScene()) {
            _isDirty = true;
        }
    }

    void SceneEditorWidget::OnNodeEdited(const SceneEvents::SceneNodeEditedEvent& event) {
        if (event.scene == nullptr || event.scene != CoreManager::GetSceneManager().GetActiveScene()) {
            return;
        }
        for (const SceneNode* node = event.node; node != nullptr; node = node->GetParent()) {
            if (node->IsGenerated()) {
                return;
            }
        }
        _isDirty = true;
    }

    void SceneEditorWidget::OnSceneSaved(const SceneEvents::SceneSavedEvent& event) {
        if (event.scene != nullptr && event.scene == CoreManager::GetSceneManager().GetActiveScene() && _uiFileSession.PendingCount() == 0) {
            _isDirty = false;
        }
    }

    void SceneEditorWidget::OnNodeAdded(const SceneNodeAddedEvent& event) {
        if (event.scene != nullptr && event.scene == CoreManager::GetSceneManager().GetActiveScene()) {
            _isDirty = true;
            _hierarchy.revealNode = event.parent;
        }
    }

    void SceneEditorWidget::OnUIRectDrag(const UIRectDragEvent& event) {
        if (FindRemote() != nullptr) {
            return;
        }
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

    void SceneEditorWidget::ForgetLocalSelection() {
        _hierarchy.Reset();
        _selection.Clear();
        ResetUISelection();
        AnnounceSelection(nullptr);
        AnnounceUIElement(nullptr);
    }

    void SceneEditorWidget::Update() {
        RemoteSession* const remote = FindRemote();
        if (remote != nullptr) {
            // Before the open check: the other panels let go of the local scene also while this window is closed
            if (!_showsRemote) {
                ForgetLocalSelection();
                _remoteView.Reset();
            }
            RemoteSceneView::ReleaseTypedText(*remote);
            // A pick in the viewport opens a closed panel, as a local one does
            if (_remoteView.TakeReveal(*remote)) {
                _focusRequested = true;
            }
        }
        _showsRemote = remote != nullptr;

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

        if (remote != nullptr) {
            _remoteView.Draw(*remote, kToolbarAsset, kInspectorAsset);
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

    ConfigManager& SceneEditorWidget::Configs() const {
        return _configManager != nullptr ? *_configManager : CoreManager::GetConfigManager();
    }

    void SceneEditorWidget::DrawComponents() {
        if (RemoteSession* const remote = FindRemote()) {
            RemoteSceneView::DrawComponents(*remote);
            return;
        }
        if (_hierarchy.selected == nullptr) {
            return;
        }
        const auto drawFields = [this](IComponent& component) {
            if (component.GetTypeMeta().typeHash == UICanvasComponent::GetStaticTypeMeta().typeHash && RenderUIElements(static_cast<UICanvasComponent&>(component))) {
                _isDirty = true;
            }
            if (RenderComponentInspector(&component)) {
                _isDirty = true;
            }
        };
        LocalComponentList list(*_hierarchy.selected, drawFields);
        if (const Optional<size_t> removed = DrawComponentList(list)) {
            _hierarchy.selected->RemoveComponent(PoolString::Intern(list.GetTypeName(*removed)));
            _isDirty = true;
        }
    }

    void SceneEditorWidget::SaveScene() {
        auto* scene = CoreManager::GetSceneManager().GetActiveScene();
        if (!scene) {
            return;
        }

        // The node files first: an instance whose rects are in its file then has nothing to override in the scene file. A failure here is logged and never stops the scene
        _uiFileSession.Save(*scene);

        if (SaveSceneToConfig(Configs(), *scene)) {
            _isDirty = false;
        }
    }

}  // namespace BECore
