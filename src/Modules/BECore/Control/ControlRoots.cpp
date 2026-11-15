#include "ControlRoots.h"

namespace BECore {

    void ControlRoots::Register(StringView name, ControlRoot root) {
        _roots.insert_or_assign(String(name), std::move(root));
    }

    const ControlRoot* ControlRoots::Find(StringView name) const {
        const auto found = _roots.find(name);
        return found == _roots.end() ? nullptr : &found->second;
    }

    Vector<String> ControlRoots::Names() const {
        Vector<String> names;
        names.reserve(_roots.size());
        for (const auto& entry : _roots) {
            names.push_back(entry.first);
        }
        return names;
    }

    TypedRef ControlRoots::Resolve(StringView name) const {
        const ControlRoot* root = Find(name);
        if (root == nullptr || !root->resolve) {
            return {};
        }
        return root->resolve();
    }

}  // namespace BECore
