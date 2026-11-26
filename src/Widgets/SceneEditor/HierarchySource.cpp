#include "HierarchySource.h"

#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Widgets/EditorLayout/IconTable.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <Widgets/SceneEditor/HierarchyPanel.h>

namespace BECore {

    namespace {

        SceneNode& NodeOf(HierarchyRow row) {
            return *static_cast<SceneNode*>(row.handle);
        }

    }  // namespace

    HierarchyRow LocalHierarchySource::GetRoot() const {
        return RowOf(_scene.GetRootNode());
    }

    size_t LocalHierarchySource::GetChildCount(HierarchyRow row) const {
        return NodeOf(row).GetChildren().size();
    }

    HierarchyRow LocalHierarchySource::GetChild(HierarchyRow row, size_t index) const {
        return RowOf(*NodeOf(row).GetChildren()[index]);
    }

    const char* LocalHierarchySource::GetName(HierarchyRow row) const {
        return NodeOf(row).GetName().CStr();
    }

    const char* LocalHierarchySource::GetIcon(HierarchyRow row) const {
        return IconOfNode(NodeOf(row));
    }

    const void* LocalHierarchySource::GetId(HierarchyRow row) const {
        return row.handle;
    }

    bool LocalHierarchySource::IsHidden(HierarchyRow row) const {
        return NodeOf(row).HasEditorFlag(EditorFlags::Hidden);
    }

    bool LocalHierarchySource::HasComponent(HierarchyRow row, StringView type) const {
        for (const auto& component : NodeOf(row).GetComponents()) {
            if (component->GetTypeMeta().typeName == type) {
                return true;
            }
        }
        return false;
    }

    void LocalHierarchySource::CollectComponentTypes(Vector<String>& out) const {
        Vector<StringView> types;
        HierarchyModel::CollectComponentTypes(_scene.GetRootNode(), types);
        out.clear();
        for (const StringView type : types) {
            out.emplace_back(type.data(), type.size());
        }
    }

    bool LocalHierarchySource::IsSelected(HierarchyRow row) const {
        return _state.selected == &NodeOf(row) || _selection.Contains(&NodeOf(row));
    }

    void LocalHierarchySource::Click(HierarchyRow row, uint32_t modifiers) {
        _state.selected = &NodeOf(row);
        _state.clickedNode = &NodeOf(row);
        _state.clickModifiers = modifiers;
    }

    SceneNode* LocalHierarchySource::EditableNode(HierarchyRow row) const {
        return &NodeOf(row);
    }

}  // namespace BECore
