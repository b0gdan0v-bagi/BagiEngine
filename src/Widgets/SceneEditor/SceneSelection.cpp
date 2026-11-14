#include "SceneSelection.h"

#include "HierarchyModel.h"

namespace BECore {

    bool SceneSelection::Set(SceneNode* node) {
        if (node == nullptr) {
            return Clear();
        }
        if (_nodes.size() == 1 && _nodes.front() == node) {
            return false;
        }
        _nodes.clear();
        _nodes.push_back(node);
        return true;
    }

    bool SceneSelection::Toggle(SceneNode* node) {
        if (node == nullptr) {
            return false;
        }
        const auto found = eastl::find(_nodes.begin(), _nodes.end(), node);
        if (found != _nodes.end()) {
            _nodes.erase(found);
        } else {
            _nodes.push_back(node);
        }
        return true;
    }

    bool SceneSelection::Add(SceneNode* node) {
        if (node == nullptr || Contains(node)) {
            return false;
        }
        _nodes.push_back(node);
        return true;
    }

    bool SceneSelection::Clear() {
        const bool changed = !_nodes.empty();
        _nodes.clear();
        return changed;
    }

    bool SceneSelection::Contains(const SceneNode* node) const {
        return eastl::find(_nodes.begin(), _nodes.end(), node) != _nodes.end();
    }

    SceneNode* SceneSelection::Primary() const {
        return _nodes.empty() ? nullptr : _nodes.back();
    }

    Span<SceneNode* const> SceneSelection::Nodes() const {
        return {_nodes.data(), _nodes.size()};
    }

    size_t SceneSelection::Count() const {
        return _nodes.size();
    }

    bool SceneSelection::Prune(Scene& scene) {
        const size_t before = _nodes.size();
        SceneNode& root = scene.GetRootNode();
        _nodes.erase(eastl::remove_if(_nodes.begin(), _nodes.end(), [&root](const SceneNode* node) { return HierarchyModel::FindNode(root, node) == nullptr; }), _nodes.end());
        return _nodes.size() != before;
    }

    bool ApplyClick(SceneSelection& selection, SceneNode* node, uint32_t modifiers) {
        if (node == nullptr) {
            return modifiers == 0 ? selection.Clear() : false;
        }
        if ((modifiers & kSelectionModifierCtrl) != 0) {
            return selection.Toggle(node);
        }
        if ((modifiers & kSelectionModifierShift) != 0) {
            return selection.Add(node);
        }
        return selection.Set(node);
    }

}  // namespace BECore
