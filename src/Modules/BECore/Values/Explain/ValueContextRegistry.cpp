#include "ValueContextRegistry.h"

namespace BECore {

    void ValueContextRegistry::Register(StringView name, ExplainContextProvider provider) {
        _providers.insert_or_assign(String(name), std::move(provider));
    }

    const ExplainContextProvider* ValueContextRegistry::Find(StringView name) const {
        const auto found = _providers.find(name);
        return found == _providers.end() ? nullptr : &found->second;
    }

    Vector<String> ValueContextRegistry::Names() const {
        Vector<String> names;
        names.reserve(_providers.size());
        for (const auto& entry : _providers) {
            names.push_back(entry.first);
        }
        return names;
    }

    void ValueContextRegistry::Clear() {
        _providers.clear();
    }

}  // namespace BECore
