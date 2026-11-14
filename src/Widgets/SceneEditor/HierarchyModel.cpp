#include "HierarchyModel.h"

#include <BECore/Reflection/ClassMeta.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore::HierarchyModel {

    namespace {

        constexpr float kDropBeforeLimit = 0.25f;
        constexpr float kDropAfterLimit = 0.75f;

        char LowerAscii(char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool IsSpace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        }

        bool ContainsIgnoreCase(StringView text, StringView part) {
            if (part.size() > text.size()) {
                return false;
            }
            for (size_t start = 0; start + part.size() <= text.size(); ++start) {
                size_t i = 0;
                while (i < part.size() && LowerAscii(text[start + i]) == LowerAscii(part[i])) {
                    ++i;
                }
                if (i == part.size()) {
                    return true;
                }
            }
            return false;
        }

        bool HasComponentOfType(const SceneNode& node, StringView typeName) {
            for (const auto& component : node.GetComponents()) {
                if (component->GetTypeMeta().typeName == typeName) {
                    return true;
                }
            }
            return false;
        }

        size_t IndexAmongSiblings(const SceneNode& node) {
            const SceneNode* parent = node.GetParent();
            if (!parent) {
                return 0;
            }
            const auto& siblings = parent->GetChildren();
            for (size_t i = 0; i < siblings.size(); ++i) {
                if (siblings[i].Get() == &node) {
                    return i;
                }
            }
            return siblings.size();
        }

        // The index of a drop among the children of its new parent, counted after the dragged node left its old place
        size_t DropIndex(const SceneNode& dragged, const SceneNode& target, DropPlace place) {
            if (place == DropPlace::Into) {
                const size_t count = target.GetChildren().size();
                // Inside its own parent the node leaves the list first, so the end is one earlier
                return &target == dragged.GetParent() ? count - 1 : count;
            }
            size_t index = IndexAmongSiblings(target) + (place == DropPlace::After ? 1 : 0);
            if (target.GetParent() == dragged.GetParent() && IndexAmongSiblings(dragged) < index) {
                --index;
            }
            return index;
        }

        void AddComponentTypes(const SceneNode& node, eastl::vector<StringView>& out) {
            for (const auto& component : node.GetComponents()) {
                out.push_back(component->GetTypeMeta().typeName);
            }
            for (const auto& child : node.GetChildren()) {
                AddComponentTypes(*child, out);
            }
        }

    }  // namespace

    SceneNode* FindNode(SceneNode& root, const SceneNode* wanted) {
        if (&root == wanted) {
            return &root;
        }
        for (const auto& child : root.GetChildren()) {
            if (SceneNode* found = FindNode(*child, wanted)) {
                return found;
            }
        }
        return nullptr;
    }

    bool ContainsNode(const SceneNode& root, const SceneNode* node) {
        // FindNode takes a mutable root only to hand back a mutable node; this one never writes through it
        return FindNode(const_cast<SceneNode&>(root), node) != nullptr;
    }

    void AncestorsOf(const SceneNode& node, eastl::vector<SceneNode*>& out) {
        out.clear();
        for (SceneNode* ancestor = node.GetParent(); ancestor != nullptr; ancestor = ancestor->GetParent()) {
            out.push_back(ancestor);
        }
        eastl::reverse(out.begin(), out.end());
    }

    bool IsFilterActive(StringView query, StringView componentType) {
        return !query.empty() || !componentType.empty();
    }

    bool NodeMatchesFilter(const SceneNode& node, StringView query, StringView componentType) {
        if (!query.empty() && !ContainsIgnoreCase(node.GetName().ToStringView(), query)) {
            return false;
        }
        return componentType.empty() || HasComponentOfType(node, componentType);
    }

    bool SubtreeMatchesFilter(const SceneNode& node, StringView query, StringView componentType) {
        if (NodeMatchesFilter(node, query, componentType)) {
            return true;
        }
        for (const auto& child : node.GetChildren()) {
            if (SubtreeMatchesFilter(*child, query, componentType)) {
                return true;
            }
        }
        return false;
    }

    DropPlace ClassifyDropPlace(float fractionOfRowHeight) {
        if (fractionOfRowHeight < kDropBeforeLimit) {
            return DropPlace::Before;
        }
        return fractionOfRowHeight >= kDropAfterLimit ? DropPlace::After : DropPlace::Into;
    }

    bool CanDrop(const SceneNode& dragged, const SceneNode& target, DropPlace place) {
        if (dragged.GetParent() == nullptr || dragged.HasEditorFlag(EditorFlags::Locked)) {
            return false;
        }
        if (&target == &dragged || target.IsDescendantOf(dragged)) {
            return false;
        }
        const SceneNode* const newParent = place == DropPlace::Into ? &target : target.GetParent();
        if (newParent == nullptr || newParent->HasEditorFlag(EditorFlags::Locked)) {
            return false;
        }

        // The node stays where it is: a drop on the neighbour that already touches it, or at the end of the parent it already ends
        if (newParent == dragged.GetParent()) {
            return DropIndex(dragged, target, place) != IndexAmongSiblings(dragged);
        }
        return true;
    }

    DropTarget ResolveDrop(const SceneNode& dragged, SceneNode& target, DropPlace place) {
        return {place == DropPlace::Into ? &target : target.GetParent(), DropIndex(dragged, target, place)};
    }

    OperationResult ApplyOperation(Scene& scene, const Operation& operation) {
        OperationResult result;
        SceneNode* const node = operation.node;
        if (node == nullptr || node->GetParent() == nullptr || !node->IsDescendantOf(scene.GetRootNode())) {
            return result;
        }
        // A locked node is neither removed nor moved, and nothing is moved into a locked parent (a copy of it is allowed: the copy is unlocked)
        const bool locked = node->HasEditorFlag(EditorFlags::Locked);
        const bool parentLocked = operation.parent != nullptr && operation.parent->HasEditorFlag(EditorFlags::Locked);
        if ((operation.kind == OperationKind::Delete && locked) || (operation.kind == OperationKind::Move && (locked || parentLocked))) {
            return result;
        }
        switch (operation.kind) {
            case OperationKind::Delete: {
                SceneNode* const parent = node->GetParent();
                parent->RemoveChild(node);
                result.changed = true;
                result.reveal = parent;
                break;
            }
            case OperationKind::Duplicate: {
                const IntrusivePtr<SceneNode> copy = scene.DuplicateNode(*node);
                if (copy) {
                    result.changed = true;
                    result.created = copy.Get();
                    result.reveal = copy->GetParent();
                }
                break;
            }
            case OperationKind::Move:
                if (scene.MoveNode(*node, operation.parent, operation.index)) {
                    result.changed = true;
                    result.reveal = node->GetParent();
                }
                break;
            case OperationKind::None:
                break;
        }
        return result;
    }

    bool IsSelfOrAncestorOf(const SceneNode* ancestor, const SceneNode* node) {
        return ancestor != nullptr && node != nullptr && (ancestor == node || node->IsDescendantOf(*ancestor));
    }

    bool ValidateName(StringView text) {
        if (text.size() > kMaxNodeNameLength) {
            return false;
        }
        for (const char c : text) {
            if (!IsSpace(c)) {
                return true;
            }
        }
        return false;
    }

    void CollectComponentTypes(const SceneNode& root, eastl::vector<StringView>& out) {
        out.clear();
        AddComponentTypes(root, out);
        eastl::sort(out.begin(), out.end());
        out.erase(eastl::unique(out.begin(), out.end()), out.end());
    }

}  // namespace BECore::HierarchyModel
