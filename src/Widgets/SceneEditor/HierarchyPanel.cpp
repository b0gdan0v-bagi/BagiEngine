#include "HierarchyPanel.h"

#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Scene/IComponent.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/EditorLayout/IconTable.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr size_t kComponentLabelSize = 96;
        constexpr const char* kNodePayload = "BE_SCENE_NODE";
        constexpr float kDropLineThickness = 2.0f;
        constexpr float kDropBoxRounding = 3.0f;
        constexpr int kToggleCount = 2;  // the eye and the lock at the right edge of a row

        bool IsLocked(const SceneNode& node) {
            return node.HasEditorFlag(EditorFlags::Locked);
        }

        // The width the eye and the lock take at the right edge of a row (a rename field leaves it free)
        float ToggleStripWidth() {
            return static_cast<float>(kToggleCount) * ImGui::GetFrameHeight() + static_cast<float>(kToggleCount - 1) * ImGui::GetStyle().ItemSpacing.x;
        }

        void ToggleFlag(SceneNode& node, EditorFlags flag) {
            const EditorFlags flags = node.GetEditorFlags();
            node.SetEditorFlags(node.HasEditorFlag(flag) ? (flags & ~flag) : (flags | flag));
        }

        // The eye and the lock of a row, drawn over its right edge. They change editor state only: the scene is not dirty, nothing is saved
        void DrawToggles(SceneNode& node, float rowRight, float rowTop) {
            ImGui::SetCursorScreenPos(ImVec2(rowRight - ToggleStripWidth(), rowTop));
            ImGui::PushID(&node);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            const bool hidden = node.HasEditorFlag(EditorFlags::Hidden);
            if (IconButton(hidden ? ICON_LC_EYE_OFF : ICON_LC_EYE, hidden ? "Show in the editor" : "Hide in the editor", "##hide")) {
                ToggleFlag(node, EditorFlags::Hidden);
            }
            ImGui::SameLine();
            const bool locked = IsLocked(node);
            if (IconButton(locked ? ICON_LC_LOCK : ICON_LC_LOCK_OPEN, locked ? "Unlock in the editor" : "Lock in the editor", "##lock")) {
                ToggleFlag(node, EditorFlags::Locked);
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }

        bool FilterIsOn(const HierarchyState& state) {
            return HierarchyModel::IsFilterActive(state.searchBuffer, state.componentType);
        }

        void DrawRenameField(HierarchyState& state, SceneNode& node) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-(ToggleStripWidth() + ImGui::GetStyle().ItemSpacing.x));
            if (state.renameFocus) {
                ImGui::SetKeyboardFocusHere();
                state.renameFocus = false;
            }
            const bool entered = ImGui::InputText("##rename", state.renameBuffer, sizeof(state.renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            // Enter commits; Escape and a click elsewhere end the field without a change. The name is interned only here
            if (entered && HierarchyModel::ValidateName(state.renameBuffer)) {
                node.SetName(PoolString::Intern(eastl::string_view(state.renameBuffer)));
                state.dirty = true;
            }
            if (entered || ImGui::IsItemDeactivated()) {
                state.renameTarget = nullptr;
            }
        }

        void DrawAddComponentMenu(HierarchyState& state, SceneNode& node) {
            if (!ImGui::BeginMenu(ICON_LC_PUZZLE " Add Component")) {
                return;
            }
            const auto& factory = AbstractFactory<IComponent>::GetInstance();
            for (const auto& meta : factory.GetRegisteredTypes()) {
                char label[kComponentLabelSize];
                snprintf(label, sizeof(label), "%s %s", IconOfClass(meta), meta.typeName.data());
                if (ImGui::MenuItem(label)) {
                    if (auto component = factory.Create(meta)) {
                        node.AddComponent(component);
                        state.dirty = true;
                    }
                }
            }
            ImGui::EndMenu();
        }

        void DrawContextMenu(HierarchyState& state, SceneNode& node) {
            if (!ImGui::BeginPopupContextItem()) {
                return;
            }
            const bool isRoot = node.GetParent() == nullptr;
            const bool locked = IsLocked(node);
            // A locked node is read-only: nothing is added to it, renamed or deleted (a copy of it is allowed)
            if (ImGui::MenuItem(ICON_LC_FOLDER_PLUS " Add Child", nullptr, false, !locked)) {
                state.addNodeRequested = true;
                state.addNodeParent = &node;
            }
            ImGui::BeginDisabled(locked);
            DrawAddComponentMenu(state, node);
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_LC_PENCIL " Rename", "F2", false, !locked)) {
                HierarchyPanel::BeginRename(state, node);
            }
            if (ImGui::MenuItem(ICON_LC_COPY " Duplicate", "Ctrl+D", false, !isRoot)) {
                state.pending = {HierarchyModel::OperationKind::Duplicate, &node, nullptr, 0};
            }
            if (ImGui::MenuItem(ICON_LC_TRASH_2 " Delete", "Del", false, !isRoot && !locked)) {
                state.pending = {HierarchyModel::OperationKind::Delete, &node, nullptr, 0};
            }
            ImGui::EndPopup();
        }

        // The drop line (before, after) or box (into) of a drag over the row just drawn, in the accent colour
        void DrawDropMark(HierarchyModel::DropPlace place) {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const ImU32 color = ImGui::GetColorU32(GetImGuiPalette().accent);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            switch (place) {
                case HierarchyModel::DropPlace::Before:
                    drawList->AddLine(ImVec2(min.x, min.y), ImVec2(max.x, min.y), color, kDropLineThickness);
                    break;
                case HierarchyModel::DropPlace::After:
                    drawList->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), color, kDropLineThickness);
                    break;
                case HierarchyModel::DropPlace::Into:
                    drawList->AddRect(min, max, color, kDropBoxRounding, ImDrawFlags_None, kDropLineThickness);
                    break;
            }
        }

        // The row just drawn as the source and the target of a drag. A move waits in state.pending
        void HandleDragDrop(HierarchyState& state, SceneNode& node, bool filterOn) {
            if (filterOn) {
                // The shown order is not the real one, so a drop could not mean what the line says
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    ImGui::SetTooltip("Drag and drop is off while a filter is on");
                }
                return;
            }
            if (node.GetParent() != nullptr && !IsLocked(node) && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                SceneNode* dragged = &node;
                ImGui::SetDragDropPayload(kNodePayload, &dragged, sizeof(dragged));
                ImGui::Text("%s %s", IconOfNode(node), node.GetName().CStr());
                ImGui::EndDragDropSource();
            }
            if (state.dragNode == nullptr || !ImGui::BeginDragDropTarget()) {
                return;
            }
            const ImVec2 min = ImGui::GetItemRectMin();
            const float height = ImGui::GetItemRectMax().y - min.y;
            const float fraction = height > 0.0f ? (ImGui::GetMousePos().y - min.y) / height : 0.0f;
            const HierarchyModel::DropPlace place = HierarchyModel::ClassifyDropPlace(fraction);
            if (HierarchyModel::CanDrop(*state.dragNode, node, place)) {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kNodePayload, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                if (payload != nullptr) {
                    DrawDropMark(place);
                    if (payload->IsDelivery()) {
                        const HierarchyModel::DropTarget target = HierarchyModel::ResolveDrop(*state.dragNode, node, place);
                        state.pending = {HierarchyModel::OperationKind::Move, state.dragNode, target.parent, target.index};
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        void DrawNode(HierarchyState& state, SceneNode& node, bool filterOn, bool ancestorHidden) {
            const auto& children = node.GetChildren();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
            if (children.empty()) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (state.selected == &node) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            // A filter opens the path to its matches; an ancestor that is not a match and a hidden node (its own flag or an ancestor's) are dimmed
            const bool hidden = ancestorHidden || node.HasEditorFlag(EditorFlags::Hidden);
            const bool dimmed = hidden || (filterOn && !HierarchyModel::NodeMatchesFilter(node, state.searchBuffer, state.componentType));
            if (state.revealNode == &node || (filterOn && !children.empty())) {
                ImGui::SetNextItemOpen(true);
            }

            const bool renaming = state.renameTarget == &node;
            if (dimmed) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            }
            const bool isOpen = renaming ? ImGui::TreeNodeEx(&node, flags, "%s", IconOfNode(node)) : ImGui::TreeNodeEx(&node, flags, "%s %s", IconOfNode(node), node.GetName().CStr());
            if (dimmed) {
                ImGui::PopStyleColor();
            }
            const float rowRight = ImGui::GetItemRectMax().x;
            const float rowTop = ImGui::GetItemRectMin().y;
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                state.selected = &node;
            }
            if (renaming) {
                DrawRenameField(state, node);
            } else {
                HandleDragDrop(state, node, filterOn);
                DrawContextMenu(state, node);
            }
            DrawToggles(node, rowRight, rowTop);

            if (isOpen) {
                // No copy of the child list: every change of the tree waits in state.pending until the walk is over
                for (const auto& child : children) {
                    if (!filterOn || HierarchyModel::SubtreeMatchesFilter(*child, state.searchBuffer, state.componentType)) {
                        DrawNode(state, *child.Get(), filterOn, hidden);
                    }
                }
                ImGui::TreePop();
            }
        }

        void DrawTypeCombo(HierarchyState& state, Scene& scene) {
            char preview[kComponentLabelSize];
            snprintf(preview, sizeof(preview), "%s %s", ICON_LC_FILTER, state.componentType.empty() ? "Any component" : state.componentType.data());
            ImGui::SetNextItemWidth(-1.0f);
            if (!ImGui::BeginCombo("##component_type", preview)) {
                state.typeComboOpen = false;
                ItemTooltip("Show only the nodes that have this component");
                return;
            }
            if (!state.typeComboOpen) {
                // Collected when the combo opens, not every frame
                HierarchyModel::CollectComponentTypes(scene.GetRootNode(), state.componentTypes);
                state.typeComboOpen = true;
            }
            if (ImGui::Selectable("Any component", state.componentType.empty())) {
                state.componentType = {};
            }
            for (const eastl::string_view type : state.componentTypes) {
                if (ImGui::Selectable(type.data(), type == state.componentType)) {
                    state.componentType = type;
                }
            }
            ImGui::EndCombo();
        }

        void DrawFilterBar(HierarchyState& state, Scene& scene) {
            if (state.focusSearch) {
                ImGui::SetKeyboardFocusHere();
                state.focusSearch = false;
            }
            const float clearWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetNextItemWidth(-clearWidth);
            ImGui::InputTextWithHint("##search", ICON_LC_SEARCH " Search (Ctrl+F)", state.searchBuffer, sizeof(state.searchBuffer));
            ImGui::SameLine();
            ImGui::BeginDisabled(state.searchBuffer[0] == '\0');
            if (IconButton(ICON_LC_X, "Clear the search text", "##clear_search")) {
                state.searchBuffer[0] = '\0';
            }
            ImGui::EndDisabled();
            DrawTypeCombo(state, scene);
        }

        // Keys of the focused panel; a text field (rename, later the search box) keeps them to itself
        void HandleKeys(HierarchyState& state) {
            if (!ImGui::IsWindowFocused() || ImGui::GetIO().WantTextInput || state.selected == nullptr) {
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                HierarchyPanel::BeginRename(state, *state.selected);
            } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !IsLocked(*state.selected)) {
                state.pending = {HierarchyModel::OperationKind::Delete, state.selected, nullptr, 0};
            } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
                state.pending = {HierarchyModel::OperationKind::Duplicate, state.selected, nullptr, 0};
            }
        }

        void ApplyPending(Scene& scene, HierarchyState& state) {
            const HierarchyModel::Operation operation = state.pending;
            state.pending = {};
            if (operation.kind == HierarchyModel::OperationKind::None) {
                return;
            }
            // Asked before the node may be gone
            const bool deletes = operation.kind == HierarchyModel::OperationKind::Delete;
            const bool lostSelection = deletes && HierarchyModel::IsSelfOrAncestorOf(operation.node, state.selected);
            const bool lostRename = deletes && HierarchyModel::IsSelfOrAncestorOf(operation.node, state.renameTarget);

            const HierarchyModel::OperationResult result = HierarchyModel::ApplyOperation(scene, operation);
            if (!result.changed) {
                return;
            }
            state.dirty = true;
            state.revealNode = result.reveal;
            if (deletes) {
                state.nodeRemoved = true;
                state.selected = lostSelection ? nullptr : state.selected;
                state.renameTarget = lostRename ? nullptr : state.renameTarget;
            }
            if (result.created) {
                state.selected = result.created;
            }
        }

    }  // namespace

    void HierarchyState::Reset() {
        *this = HierarchyState{};
    }

    namespace HierarchyPanel {

        void BeginRename(HierarchyState& state, SceneNode& node) {
            if (IsLocked(node)) {
                return;
            }
            state.renameTarget = &node;
            state.renameFocus = true;
            snprintf(state.renameBuffer, sizeof(state.renameBuffer), "%s", node.GetName().CStr());
        }

        void Draw(Scene& scene, HierarchyState& state) {
            ImGui::TextUnformatted("Hierarchy");
            ImGui::Separator();

            if (ImGui::IsWindowFocused() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
                state.focusSearch = true;
            }
            // The payload of a drag holds a pointer: it is used only when the node is found in the tree
            state.dragNode = nullptr;
            if (const ImGuiPayload* payload = ImGui::GetDragDropPayload(); payload != nullptr && payload->IsDataType(kNodePayload)) {
                SceneNode* dragged = *static_cast<SceneNode* const*>(payload->Data);
                state.dragNode = HierarchyModel::ContainsNode(scene.GetRootNode(), dragged) ? dragged : nullptr;
            }

            DrawFilterBar(state, scene);
            HandleKeys(state);

            const bool filterOn = FilterIsOn(state);
            if (filterOn && !HierarchyModel::SubtreeMatchesFilter(scene.GetRootNode(), state.searchBuffer, state.componentType)) {
                EmptyState(ICON_LC_SEARCH, "No node matches the filter");
            } else {
                DrawNode(state, scene.GetRootNode(), filterOn, false);
            }
            state.revealNode = nullptr;
            ApplyPending(scene, state);

            ImGui::Separator();
            ImGui::BeginDisabled(IsLocked(scene.GetRootNode()));
            if (IconTextButton(ICON_LC_CIRCLE_PLUS, "Add Node", "Add a node at the root of the scene", -1.0f)) {
                state.addNodeRequested = true;
                state.addNodeParent = nullptr;
            }
            ImGui::EndDisabled();
        }

    }  // namespace HierarchyPanel

}  // namespace BECore
