#include "RemoteHierarchySource.h"

#include <BECore/Reflection/TypeRegistry.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Widgets/EditorLayout/IconTable.h>
#include <Widgets/RemoteEditor/RemoteSession.h>

namespace BECore {

    namespace {

        /// What the local scene names its root
        constexpr const char* kRootName = "Root";
        /// Its address names the root row to ImGui: no hash of a path stands for it, since the empty path is a node's
        constexpr char kRootId = 0;

        /// The node of a row; null for the root row
        const RemoteTreeNode* NodeOf(HierarchyRow row) {
            return static_cast<const RemoteTreeNode*>(row.handle);
        }

    }  // namespace

    Optional<HierarchyRow> RemoteHierarchySource::Reveal(StringView path) {
        _revealed = _session.GetTree().Find(path);
        if (_revealed == nullptr) {
            return {};
        }
        // A row only carries the pointer: nothing writes through it
        return HierarchyRow{const_cast<RemoteTreeNode*>(_revealed)};
    }

    bool RemoteHierarchySource::IsOnRevealPath(HierarchyRow row) const {
        if (_revealed == nullptr) {
            return false;
        }
        const RemoteTreeNode* const node = NodeOf(row);
        if (node == nullptr) {
            return true;  // the root is above every node
        }
        // A `/` after the whole path of a node separates segments: one of a name follows an unpaired `\`, which no path ends in
        const StringView target = _revealed->path;
        return target.size() > node->path.size() && target.starts_with(StringView(node->path)) && target[node->path.size()] == '/';
    }

    size_t RemoteHierarchySource::GetChildCount(HierarchyRow row) const {
        const RemoteTreeNode* const node = NodeOf(row);
        return node != nullptr ? node->children.size() : _session.GetTree().GetTopNodes().size();
    }

    HierarchyRow RemoteHierarchySource::GetChild(HierarchyRow row, size_t index) const {
        const RemoteTreeNode* const node = NodeOf(row);
        const Vector<uint32_t>& children = node != nullptr ? node->children : _session.GetTree().GetTopNodes();
        // A row only carries the pointer: nothing writes through it
        return {const_cast<RemoteTreeNode*>(&_session.GetTree().GetNodes()[children[index]])};
    }

    const char* RemoteHierarchySource::GetName(HierarchyRow row) const {
        const RemoteTreeNode* const node = NodeOf(row);
        return node != nullptr ? node->name.c_str() : kRootName;
    }

    const char* RemoteHierarchySource::GetIcon(HierarchyRow row) const {
        if (const RemoteTreeNode* const node = NodeOf(row)) {
            for (const String& component : node->components) {
                const TypeInfo* const type = TypeRegistry::GetInstance().Find(StringView(component));
                if (type != nullptr && !type->meta.icon.empty()) {
                    return IconOfClass(type->meta);
                }
            }
        }
        return GetNodeIcon();
    }

    const void* RemoteHierarchySource::GetId(HierarchyRow row) const {
        const RemoteTreeNode* const node = NodeOf(row);
        if (node == nullptr) {
            return &kRootId;
        }
        // ImGui takes the value of this pointer as the id and never reads through it
        return reinterpret_cast<const void*>(static_cast<uintptr_t>(StringUtils::GetHash(StringView(node->path))));
    }

    bool RemoteHierarchySource::HasComponent(HierarchyRow row, StringView type) const {
        const RemoteTreeNode* const node = NodeOf(row);
        return node != nullptr && eastl::any_of(node->components.begin(), node->components.end(), [type](const String& component) { return StringView(component) == type; });
    }

    void RemoteHierarchySource::CollectComponentTypes(Vector<String>& out) const {
        out.clear();
        for (const RemoteTreeNode& node : _session.GetTree().GetNodes()) {
            out.insert(out.end(), node.components.begin(), node.components.end());
        }
        eastl::sort(out.begin(), out.end());
        out.erase(eastl::unique(out.begin(), out.end()), out.end());
    }

    bool RemoteHierarchySource::IsSelected(HierarchyRow row) const {
        const RemoteTreeNode* const node = NodeOf(row);
        const Optional<String>& selection = _session.GetSelection();
        return node != nullptr && selection && *selection == node->path;
    }

    void RemoteHierarchySource::Click(HierarchyRow row, uint32_t /*modifiers*/) {
        if (const RemoteTreeNode* const node = NodeOf(row)) {
            _session.Select(node->path);
        } else {
            _session.ClearSelection();
        }
    }

    bool RemoteHierarchySource::IsTruncated() const {
        return _session.GetTree().IsTruncated();
    }

}  // namespace BECore
