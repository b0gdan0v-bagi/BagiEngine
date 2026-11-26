#include "RemoteComponent.h"

#include <BECore/Control/ControlParams.h>

namespace BECore {

    bool RemoteComponent::Load(StringView result, size_t index) {
        if (index == _index && result == StringView(_text.data(), _text.size())) {
            return false;
        }
        Clear();
        _index = index;
        _text.assign(result.begin(), result.end());
        if (!_tape.Parse(StringView(_text.data(), _text.size()))) {
            return true;
        }
        const JsonView root(_tape, 0);
        Optional<String> node = GetParamString(root, "node");
        Optional<String> type = GetParamString(root, "component");
        const Optional<JsonView> fields = FindParam(root, "fields");
        if (node && type && fields && fields->IsObject()) {
            _node = std::move(*node);
            _type = std::move(*type);
            _fields = fields->Index();
        }
        return true;
    }

    void RemoteComponent::Clear() {
        _text.clear();
        _fields = 0;
        _node.clear();
        _type.clear();
        _index = 0;
    }

    bool RemoteComponent::Walk(const RemoteSchema& schema, RemotePendingWrites& writes, IPropertyVisitor& visitor) {
        const RemoteType* type = IsLoaded() ? schema.FindType(_type) : nullptr;
        return type != nullptr && _walker.Walk(schema, *type, JsonView(_tape, _fields), {_node, _type, _index}, writes, visitor);
    }

}  // namespace BECore
