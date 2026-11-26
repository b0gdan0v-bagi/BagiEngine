#include "RemoteTree.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/Scene/SceneControl.h>
#include <BECore/Scene/SceneNodePath.h>

namespace BECore {

    namespace {

        /// The path of the parent of the node at @p path: all before the last `/` that is no escaped character of a name; nothing for a top node
        Optional<StringView> ParentPath(StringView path) {
            for (size_t end = path.size(); end > 0; --end) {
                if (path[end - 1] != '/') {
                    continue;
                }
                size_t escapes = 0;
                while (escapes + 1 < end && path[end - 2 - escapes] == '\\') {
                    ++escapes;
                }
                if (escapes % 2 == 0) {
                    return path.substr(0, end - 1);
                }
            }
            return {};
        }

        int32_t ReadLayer(JsonView node) {
            const int64_t layer = GetParamInt(node, "layer").value_or(0);
            const bool fits = layer >= std::numeric_limits<int32_t>::min() && layer <= std::numeric_limits<int32_t>::max();
            return fits ? static_cast<int32_t>(layer) : 0;
        }

    }  // namespace

    bool RemoteTree::Load(StringView result) {
        if (result == StringView(_text)) {
            return false;
        }
        Clear();
        _text.assign(result.data(), result.size());
        if (!_tape.Parse(_text)) {
            return true;
        }
        const JsonView root(_tape, 0);
        _sceneName = GetParamString(root, "scene").value_or(String{});
        _truncated = GetParamBool(root, "truncated").value_or(false);
        for (const JsonView source : FindParam(root, "nodes").value_or(JsonView{}).Items()) {
            if (_nodes.size() >= SceneControlLimits::kMaxTreeNodes) {
                _truncated = true;
                break;
            }
            Optional<String> path = GetParamString(source, "path");
            if (!path) {
                continue;
            }
            size_t depth = 0;
            if (const Optional<StringView> parent = ParentPath(*path)) {
                depth = _openNodes.size();
                while (depth > 0 && StringView(_nodes[_openNodes[depth - 1]].path) != *parent) {
                    --depth;
                }
                if (depth == 0 || depth >= SceneNodePathLimits::kMaxSegments) {
                    continue;
                }
            }
            _openNodes.resize(depth);
            RemoteTreeNode node;
            node.path = std::move(*path);
            node.name = GetParamString(source, "name").value_or(String{});
            node.layer = ReadLayer(source);
            for (const JsonView component : FindParam(source, "components").value_or(JsonView{}).Items()) {
                if (String type; component.ReadText(type)) {
                    node.components.push_back(std::move(type));
                }
            }
            const auto index = static_cast<uint32_t>(_nodes.size());
            (_openNodes.empty() ? _topNodes : _nodes[_openNodes.back()].children).push_back(index);
            _openNodes.push_back(index);
            _nodes.push_back(std::move(node));
        }
        return true;
    }

    void RemoteTree::Clear() {
        _text.clear();
        _nodes.clear();
        _topNodes.clear();
        _openNodes.clear();
        _sceneName.clear();
        _truncated = false;
    }

    const RemoteTreeNode* RemoteTree::Find(StringView path) const {
        for (const RemoteTreeNode& node : _nodes) {
            if (StringView(node.path) == path) {
                return &node;
            }
        }
        return nullptr;
    }

}  // namespace BECore
