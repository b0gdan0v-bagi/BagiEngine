#include "HierarchyPanel.h"

#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Scene/IComponent.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/EditorLayout/IconTable.h>
#include <Widgets/Icons/IconsLucide.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr size_t kComponentLabelSize = 96;

        void DrawRenameField(HierarchyState& state, SceneNode& node) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
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
            if (ImGui::MenuItem(ICON_LC_FOLDER_PLUS " Add Child")) {
                state.addNodeRequested = true;
                state.addNodeParent = &node;
            }
            DrawAddComponentMenu(state, node);
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_LC_PENCIL " Rename", "F2")) {
                HierarchyPanel::BeginRename(state, node);
            }
            if (ImGui::MenuItem(ICON_LC_COPY " Duplicate", "Ctrl+D", false, !isRoot)) {
                state.pending = {HierarchyModel::OperationKind::Duplicate, &node, nullptr, 0};
            }
            if (ImGui::MenuItem(ICON_LC_TRASH_2 " Delete", "Del", false, !isRoot)) {
                state.pending = {HierarchyModel::OperationKind::Delete, &node, nullptr, 0};
            }
            ImGui::EndPopup();
        }

        void DrawNode(HierarchyState& state, SceneNode& node) {
            const auto& children = node.GetChildren();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
            if (children.empty()) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (state.selected == &node) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (state.revealNode == &node) {
                ImGui::SetNextItemOpen(true);
            }

            const bool renaming = state.renameTarget == &node;
            const bool isOpen = renaming ? ImGui::TreeNodeEx(&node, flags, "%s", IconOfNode(node)) : ImGui::TreeNodeEx(&node, flags, "%s %s", IconOfNode(node), node.GetName().CStr());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                state.selected = &node;
            }
            if (renaming) {
                DrawRenameField(state, node);
            } else {
                DrawContextMenu(state, node);
            }

            if (isOpen) {
                // No copy of the child list: every change of the tree waits in state.pending until the walk is over
                for (const auto& child : children) {
                    DrawNode(state, *child.Get());
                }
                ImGui::TreePop();
            }
        }

        // Keys of the focused panel; a text field (rename, later the search box) keeps them to itself
        void HandleKeys(HierarchyState& state) {
            if (!ImGui::IsWindowFocused() || ImGui::GetIO().WantTextInput || state.selected == nullptr) {
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                HierarchyPanel::BeginRename(state, *state.selected);
            } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
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
            state.renameTarget = &node;
            state.renameFocus = true;
            snprintf(state.renameBuffer, sizeof(state.renameBuffer), "%s", node.GetName().CStr());
        }

        void Draw(Scene& scene, HierarchyState& state) {
            ImGui::TextUnformatted("Hierarchy");
            ImGui::Separator();

            HandleKeys(state);
            DrawNode(state, scene.GetRootNode());
            state.revealNode = nullptr;
            ApplyPending(scene, state);

            ImGui::Separator();
            if (IconTextButton(ICON_LC_CIRCLE_PLUS, "Add Node", "Add a node at the root of the scene", -1.0f)) {
                state.addNodeRequested = true;
                state.addNodeParent = nullptr;
            }
        }

    }  // namespace HierarchyPanel

}  // namespace BECore
